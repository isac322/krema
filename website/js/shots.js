// Exhibit: feature clips and screenshots on one stage, picked from a mini
// Krema dock. Every .ex-clip figure in the stage is one picker item; its tile
// is built here from data-label and the poster (video) or image (still).
(() => {
  "use strict";

  const motionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");
  const reduced = () => motionQuery.matches;

  const initClips = () => {
    const ex = document.querySelector("[data-ex]");
    const stage = ex && ex.querySelector(".ex-stage");
    const dock = ex && ex.querySelector(".ex-dock");
    const toggle = ex && ex.querySelector(".ex-toggle");
    if (!stage || !dock || !toggle) return;
    const live = ex.querySelector(".ex-live");

    const MAX = 1.35; // peak magnification
    const REACH = 1.6; // falloff radius in tile pitches (tighter than the page dock)
    const LOOPS = 2; // plays of one clip before auto-advance
    const FADE = 180; // stage crossfade, ms (matches shots.css)
    const TAP_DRIFT = 10; // px of finger travel that turns a tap into a scrub
    const SCROLL_IDLE = 400; // ms after the last row scroll before the zoom settles

    // ---------- Model: one entry per figure, tile built from its data ----------

    const buildTile = (fig, key, faceSrc) => {
      const tile = document.createElement("button");
      tile.type = "button";
      tile.className = "ex-tile";
      tile.id = `ex-tab-${key}`;
      tile.setAttribute("role", "tab");
      tile.setAttribute("aria-controls", fig.id);
      tile.setAttribute("aria-selected", "false");
      tile.tabIndex = -1;
      const face = document.createElement("span");
      face.className = "ex-face";
      if (faceSrc) {
        const img = document.createElement("img");
        img.alt = "";
        img.loading = "lazy";
        img.decoding = "async";
        img.src = faceSrc;
        face.append(img);
      }
      const ind = document.createElement("span");
      ind.className = "ex-ind";
      ind.setAttribute("aria-hidden", "true");
      const label = document.createElement("span");
      label.className = "ex-label";
      label.textContent = fig.dataset.label || (fig.querySelector(".ex-name") || fig).textContent.trim();
      tile.append(face, ind, label);
      return tile;
    };

    const clips = [];
    for (const fig of stage.querySelectorAll(".ex-clip")) {
      const video = fig.querySelector("video");
      const still = video ? null : fig.querySelector(".ex-img");
      const key = fig.dataset.clip || fig.id;
      if ((!video && !still) || !fig.id || !key) {
        fig.remove();
        continue;
      }
      const tile = buildTile(fig, key, fig.dataset.face || (video ? video.getAttribute("poster") : still.getAttribute("src")));
      fig.setAttribute("role", "tabpanel");
      fig.setAttribute("aria-labelledby", tile.id);
      dock.append(tile);
      clips.push({
        tile,
        fig,
        video,
        still,
        face: tile.querySelector(".ex-face"),
        sources: video ? Array.from(video.querySelectorAll("source[data-src]")) : [],
        name: (fig.querySelector(".ex-name") || tile).textContent.trim(),
        line: (fig.querySelector(".ex-line") || {}).textContent || "",
        ready: false,
        loaded: false,
        seen: false,
        lastT: 0,
        center: 0,
        s: 1,
      });
    }
    if (!clips.length) {
      ex.hidden = true;
      return;
    }
    dock.setAttribute("role", "tablist");
    dock.setAttribute("aria-label", "Clips and screenshots");
    const clipOf = (el) => clips.find((c) => c.tile === el) || null;
    const readyClips = () => clips.filter((c) => c.ready);

    let active = null;
    let wantPlay = !reduced(); // reduced motion: nothing starts on its own
    let auto = !reduced(); // auto-advance through the videos until the user picks an item
    let inView = !("IntersectionObserver" in window);
    let loops = 0;
    let m = null;
    let needMeasure = true;
    let frame = 0;
    let pointerX = null; // clientX of a hovering mouse or scrubbing finger
    let focusClip = null;
    let scrolling = false;
    let scrollTimer = 0;
    let touch = null; // { id, x0, dragged }

    const schedule = () => {
      if (!frame) frame = requestAnimationFrame(render);
    };

    dock.hidden = false;
    toggle.hidden = false;
    ex.classList.add("is-pending"); // keeps the space, shows nothing until an item checks out

    // ---------- Loading: only the stage clip ever gets a src ----------

    const load = (c) => {
      if (!c.video || c.loaded) return;
      c.loaded = true;
      for (const s of c.sources) s.src = s.dataset.src;
      c.video.load();
    };

    // Detach the sources so the paused decoder and its buffers are released.
    const unload = (c) => {
      if (!c.video || !c.loaded) return;
      c.loaded = false;
      c.video.pause();
      for (const s of c.sources) s.removeAttribute("src");
      c.video.load();
    };

    // A still has nothing to play: the toggle stays in place but is disabled.
    const writeToggle = () => {
      toggle.setAttribute("aria-pressed", String(wantPlay));
      toggle.disabled = !!active && !active.video;
    };

    const sync = () => {
      if (!active || !active.video) return;
      const c = active;
      const v = c.video;
      if (wantPlay && inView && !document.hidden) {
        load(c);
        if (!v.paused) return;
        const p = v.play();
        if (p && p.catch) {
          p.catch((err) => {
            if (err && err.name === "NotSupportedError" && c.loaded) drop(c);
            else if (err && err.name === "NotAllowedError") {
              wantPlay = false;
              writeToggle();
            }
          });
        }
      } else if (!v.paused) {
        v.pause();
      }
    };

    // ---------- Selection ----------

    const writeTiles = () => {
      const list = readyClips();
      const tabStop = active || list[0] || null;
      for (const c of clips) {
        const on = c === active;
        c.fig.classList.toggle("is-on", on);
        c.tile.setAttribute("aria-selected", String(on));
        c.tile.tabIndex = c === tabStop ? 0 : -1;
        c.tile.classList.toggle("is-active", on);
        c.tile.classList.toggle("is-running", c.seen);
      }
    };

    const select = (c, { user = false, focus = false } = {}) => {
      if (!c || !c.ready) return;
      if (user) auto = false;
      if (c !== active) {
        const prev = active;
        active = c;
        c.seen = true;
        c.lastT = 0;
        loops = 0;
        ex.classList.remove("is-pending");
        writeTiles();
        writeToggle();
        if (prev && prev.video) {
          prev.video.pause();
          // Keep the outgoing frame for the crossfade, then free it.
          window.setTimeout(() => {
            if (prev !== active) unload(prev);
          }, reduced() ? 0 : FADE + 20);
        }
        if (user && live) live.textContent = `${c.name}. ${c.line}`.trim();
        sync();
      }
      if (focus) c.tile.focus();
    };

    const neighbour = (from, step) => {
      const list = readyClips();
      if (!list.length) return null;
      const i = list.indexOf(from);
      if (i < 0) return list[0];
      return list[(i + step + list.length) % list.length];
    };

    // Auto-advance rotates through the videos only; stills wait for a pick.
    const nextVideo = (from) => {
      const list = readyClips().filter((c) => c.video);
      if (!list.length) return null;
      const i = list.indexOf(from);
      return list[(i + 1) % list.length];
    };

    // The first item in document order that has checked out goes on stage.
    const start = () => {
      if (active) return;
      const first = clips[0];
      if (first && first.ready) select(first);
    };

    const drop = (c) => {
      const i = clips.indexOf(c);
      if (i < 0) return;
      const hadFocus = document.activeElement === c.tile;
      clips.splice(i, 1);
      unload(c);
      c.tile.remove();
      c.fig.remove();
      needMeasure = true;
      if (!clips.length) {
        active = null;
        ex.hidden = true;
        return;
      }
      if (c === active) {
        active = null;
        const next = clips.slice(i).find((k) => k.ready) || readyClips()[0];
        if (next) select(next, { focus: hadFocus });
        else ex.classList.add("is-pending");
      } else {
        writeTiles();
      }
      start();
      schedule();
    };

    const markReady = (c) => {
      if (!clips.includes(c)) return;
      c.ready = true;
      c.tile.classList.add("is-ready");
      needMeasure = true;
      writeTiles();
      start();
      schedule();
    };

    // ---------- Probe: the poster or image must load, a video needs one playable source ----------

    const imageOk = (url) =>
      new Promise((resolve) => {
        if (!url) return resolve(false);
        const img = new Image();
        img.onload = () => resolve(img.naturalWidth > 0);
        img.onerror = () => resolve(false);
        img.src = url;
      });

    // true = present, false = definitely missing, null = could not tell (e.g. file://).
    const exists = (url) =>
      window.fetch
        ? fetch(url, { method: "HEAD" }).then(
            (r) => (r.ok ? true : r.status === 404 || r.status === 410 ? false : null),
            () => null
          )
        : Promise.resolve(null);

    const probe = async (c) => {
      if (c.still) return (await imageOk(c.still.currentSrc || c.still.src)) ? markReady(c) : drop(c);
      if (!(await imageOk(c.video.poster))) return drop(c);
      const urls = c.sources
        .filter((s) => c.video.canPlayType(s.getAttribute("type") || "") !== "")
        .map((s) => s.dataset.src);
      for (const url of urls) {
        if ((await exists(url)) !== false) return markReady(c);
      }
      drop(c);
    };

    for (const c of clips) {
      if (c.video) {
        // A missing file surfaces as an error on the last candidate source.
        const last = c.sources[c.sources.length - 1];
        if (last) {
          last.addEventListener("error", () => {
            if (c.loaded && last.hasAttribute("src")) drop(c);
          });
        }
        c.video.addEventListener("timeupdate", () => {
          if (c !== active) return;
          const t = c.video.currentTime;
          if (t + 0.25 < c.lastT) loops += 1; // wrapped around: one more play done
          c.lastT = t;
          if (auto && loops >= LOOPS) {
            const next = nextVideo(c);
            if (next && next !== c) select(next);
          }
        });
      }
      probe(c);
    }

    // ---------- Play / pause ----------

    writeToggle();
    toggle.addEventListener("click", () => {
      wantPlay = !wantPlay;
      writeToggle();
      sync();
    });

    if (!inView) {
      new IntersectionObserver(
        (entries) => {
          inView = entries[entries.length - 1].isIntersecting;
          sync();
        },
        { threshold: 0.35 }
      ).observe(stage);
    }
    document.addEventListener("visibilitychange", sync);

    const onMotionChange = () => {
      if (!reduced()) return;
      wantPlay = false;
      auto = false;
      writeToggle();
      sync();
    };
    if (motionQuery.addEventListener) motionQuery.addEventListener("change", onMotionChange);

    // ---------- Stills: the stage image opens full size in a lightbox ----------

    let dialog = null;
    let opener = null;

    const ensureDialog = () => {
      if (dialog) return dialog;
      if (typeof HTMLDialogElement !== "function") return null;
      dialog = document.createElement("dialog");
      dialog.className = "ex-dialog";
      dialog.setAttribute("aria-label", "Screenshot");
      const close = document.createElement("button");
      close.type = "button";
      close.className = "ex-close";
      close.setAttribute("aria-label", "Close");
      close.textContent = "\u00d7";
      const img = document.createElement("img");
      img.alt = "";
      const cap = document.createElement("p");
      cap.className = "ex-dcap";
      dialog.append(close, img, cap);
      (ex.closest("section") || document.body).append(dialog);

      close.addEventListener("click", () => dialog.close());
      // Backdrop click: the event targets the dialog itself but lands outside its box.
      dialog.addEventListener("click", (e) => {
        if (e.target !== dialog) return;
        const r = dialog.getBoundingClientRect();
        const inside = e.clientX >= r.left && e.clientX <= r.right && e.clientY >= r.top && e.clientY <= r.bottom;
        if (!inside) dialog.close();
      });
      // Esc is handled natively (cancel -> close); every close path restores focus.
      dialog.addEventListener("close", () => {
        if (opener && document.contains(opener)) opener.focus();
        opener = null;
      });
      return dialog;
    };

    stage.addEventListener("click", (e) => {
      const link = e.target.closest(".ex-open");
      const src = link && link.querySelector("img");
      if (!src || !ensureDialog()) return;
      e.preventDefault();
      const img = dialog.querySelector("img");
      const w = src.getAttribute("width");
      const h = src.getAttribute("height");
      if (w && h) {
        img.width = +w;
        img.height = +h;
      }
      img.src = link.getAttribute("href") || src.currentSrc || src.src;
      img.alt = src.alt;
      const line = link.closest("figure")?.querySelector(".ex-line");
      dialog.querySelector(".ex-dcap").textContent = line ? line.textContent.trim() : "";
      opener = link;
      dialog.showModal();
      dialog.querySelector(".ex-close").focus();
    });

    // ---------- Picker: selection + keyboard ----------

    let suppressClick = false;
    dock.addEventListener("click", (e) => {
      if (suppressClick) {
        suppressClick = false;
        return;
      }
      const c = clipOf(e.target.closest(".ex-tile"));
      if (c) select(c, { user: true });
    });

    dock.addEventListener("keydown", (e) => {
      const c = clipOf(e.target.closest(".ex-tile"));
      if (!c) return;
      suppressClick = false;
      let next = null;
      if (e.key === "ArrowRight" || e.key === "ArrowDown") next = neighbour(c, 1);
      else if (e.key === "ArrowLeft" || e.key === "ArrowUp") next = neighbour(c, -1);
      else if (e.key === "Home") next = readyClips()[0];
      else if (e.key === "End") next = readyClips().slice(-1)[0];
      if (!next) return;
      e.preventDefault();
      select(next, { user: true, focus: true });
    });

    // ---------- Picker: parabolic zoom ----------

    const measure = () => {
      const list = readyClips();
      for (const c of list) c.center = c.tile.offsetLeft + c.tile.offsetWidth / 2;
      const pitch =
        list.length > 1 ? Math.abs(list[1].center - list[0].center) : list.length ? list[0].tile.offsetWidth : 0;
      const scroll = dock.scrollWidth > dock.clientWidth + 1;
      dock.classList.toggle("is-scroll", scroll);
      m = {
        reach: REACH * pitch,
        left: dock.getBoundingClientRect().left + dock.clientLeft,
        scroll,
        maxScroll: dock.scrollWidth - dock.clientWidth,
      };
      needMeasure = false;
    };

    const zoomAt = (d) => {
      if (d >= m.reach) return 1;
      const c = Math.cos((Math.PI * d) / (2 * m.reach));
      return 1 + (MAX - 1) * c * c;
    };

    const render = () => {
      frame = 0;
      if (needMeasure) measure();
      let x = null;
      if (pointerX !== null) x = pointerX - m.left + dock.scrollLeft;
      else if (scrolling) x = dock.scrollLeft + dock.clientWidth / 2;
      else if (focusClip && focusClip.ready) x = focusClip.center;
      dock.classList.toggle("is-tracking", pointerX !== null || scrolling);
      // A scrolling row fades out on whichever side still has tiles to reveal.
      if (m.scroll) {
        dock.classList.toggle("has-more-start", dock.scrollLeft > 2);
        dock.classList.toggle("has-more-end", dock.scrollLeft < m.maxScroll - 2);
      } else {
        dock.classList.remove("has-more-start", "has-more-end");
      }
      for (const c of clips) {
        const s = x === null || !c.ready || !m.reach ? 1 : zoomAt(Math.abs(x - c.center));
        if (Math.abs(s - c.s) < 0.0005) continue;
        c.s = s;
        if (c.face) c.face.style.scale = s === 1 ? "" : s.toFixed(4);
      }
    };

    dock.addEventListener(
      "pointermove",
      (e) => {
        if (e.pointerType === "mouse") {
          pointerX = e.clientX;
          schedule();
          return;
        }
        if (!touch || e.pointerId !== touch.id) return;
        if (!touch.dragged && Math.abs(e.clientX - touch.x0) > TAP_DRIFT) {
          touch.dragged = true;
          try {
            dock.setPointerCapture(e.pointerId);
          } catch (_) {
            /* pointer already gone */
          }
        }
        pointerX = e.clientX;
        schedule();
      },
      { passive: true }
    );

    dock.addEventListener(
      "pointerdown",
      (e) => {
        if (e.pointerType === "mouse") return;
        touch = { id: e.pointerId, x0: e.clientX, dragged: false };
        suppressClick = false;
        pointerX = e.clientX;
        needMeasure = true;
        schedule();
      },
      { passive: true }
    );

    const endTouch = (e) => {
      if (!touch || e.pointerId !== touch.id) return;
      // A scrub only magnifies; a tap (no drift) selects through the click.
      if (touch.dragged && e.type === "pointerup") suppressClick = true;
      touch = null;
      pointerX = null;
      schedule();
    };
    dock.addEventListener("pointerup", endTouch, { passive: true });
    dock.addEventListener("pointercancel", endTouch, { passive: true });
    dock.addEventListener(
      "pointerleave",
      (e) => {
        if (e.pointerType !== "mouse") return;
        pointerX = null;
        schedule();
      },
      { passive: true }
    );

    // Narrow screens: the row scrolls natively; the tile at its centre peaks.
    dock.addEventListener(
      "scroll",
      () => {
        if (touch) {
          touch = null; // the browser took the gesture over
          pointerX = null;
        }
        scrolling = true;
        window.clearTimeout(scrollTimer);
        scrollTimer = window.setTimeout(() => {
          scrolling = false;
          schedule();
        }, SCROLL_IDLE);
        schedule();
      },
      { passive: true }
    );

    dock.addEventListener("focusin", (e) => {
      const c = clipOf(e.target.closest(".ex-tile"));
      let visible = false;
      try {
        visible = e.target.matches(":focus-visible");
      } catch (_) {
        visible = true; // no :focus-visible support: treat focus as keyboard focus
      }
      focusClip = c && visible ? c : null;
      schedule();
    });
    dock.addEventListener("focusout", () => {
      focusClip = null;
      schedule();
    });

    const remeasure = () => {
      needMeasure = true;
      schedule();
    };
    window.addEventListener("resize", remeasure, { passive: true });
    if ("ResizeObserver" in window) new ResizeObserver(remeasure).observe(dock);
  };

  initClips();
})();
