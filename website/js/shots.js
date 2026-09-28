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
    const EDGE = 0.15; // share of the row width at each end that scrolls it on hover
    const EDGE_SPEED = 720; // px/s with the pointer at the very edge
    const EDGE_TAU = 110; // ms; how quickly the glide speed follows the pointer
    const GLIDE_TAU = 70; // ms; how quickly a wheel or keyboard glide reaches its target
    const WHEEL_LINE = 33; // px per wheel "line" (Firefox reports lines, not pixels)

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
    let glideFrame = 0; // edge hover + wheel glide, see the picker section below
    let glideLast = 0;
    let glidePos = 0; // fractional scrollLeft; the browser may round the real one
    let edgeVel = 0; // px/s
    let glideTarget = null; // scrollLeft a wheel or keyboard glide is heading for
    let edgeHold = false; // wheel or keyboard input pauses the edge until the mouse moves
    let mouseXY = ""; // last mouse position, to tell a real move from a repeat event

    const schedule = () => {
      if (!frame) frame = requestAnimationFrame(render);
    };

    dock.hidden = false;
    // The rail holds the row and draws the edge chevrons: the row's own mask
    // and scrolling would fade or carry anything drawn inside it.
    const rail = document.createElement("div");
    rail.className = "ex-rail";
    dock.before(rail);
    rail.append(dock);
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
      if (focus) {
        c.tile.focus({ preventScroll: true });
        reveal(c);
      }
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
      stopGlide();
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
      // A scrolling row fades out on whichever side still has tiles to reveal;
      // the rail around it draws a chevron in each fade.
      const moreStart = m.scroll && dock.scrollLeft > 2;
      const moreEnd = m.scroll && dock.scrollLeft < m.maxScroll - 2;
      for (const el of [dock, rail]) {
        el.classList.toggle("has-more-start", moreStart);
        el.classList.toggle("has-more-end", moreEnd);
      }
      rail.classList.toggle("is-edge-start", edgeVel < -1);
      rail.classList.toggle("is-edge-end", edgeVel > 1);
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
          const xy = `${e.clientX},${e.clientY}`;
          if (xy !== mouseXY) edgeHold = false; // a real move hands the row back to the edge
          mouseXY = xy;
          pointerX = e.clientX;
          schedule();
          startGlide();
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
        startGlide(); // eases the edge glide to a stop
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

    // ---------- Picker: edge hover + wheel glide (mouse only) ----------
    // Hovering the outer EDGE of the row glides it that way, faster the deeper
    // the pointer sits in the zone; the speed eases in and out rather than
    // snapping. A vertical wheel over the row scrolls it sideways until it runs
    // out of room, then the page takes the wheel. The pointer's clientX stays
    // put while the row moves, so render() keeps the tile under it magnified.

    const edgeTarget = () => {
      if (pointerX === null || edgeHold || touch || reduced() || !m.scroll) return 0;
      const w = dock.clientWidth;
      const zone = w * EDGE;
      const x = pointerX - m.left;
      let d = 0;
      if (x < zone) d = -(zone - x) / zone;
      else if (x > w - zone) d = (x - (w - zone)) / zone;
      d = Math.max(-1, Math.min(1, d));
      if ((d < 0 && dock.scrollLeft <= 0.5) || (d > 0 && dock.scrollLeft >= m.maxScroll - 0.5)) return 0;
      return Math.sign(d) * d * d * EDGE_SPEED; // gentle near the zone's inner edge
    };

    const endGlide = () => {
      if (glideFrame) cancelAnimationFrame(glideFrame);
      glideFrame = 0;
      glideLast = 0;
      edgeVel = 0;
      dock.classList.remove("is-gliding");
      schedule();
    };

    const glide = (now) => {
      glideFrame = 0;
      if (needMeasure) measure();
      const dt = glideLast ? Math.min(now - glideLast, 50) : 1000 / 60;
      if (!glideLast) glidePos = dock.scrollLeft;
      else if (Math.abs(dock.scrollLeft - glidePos) > 1) {
        glidePos = dock.scrollLeft; // something else (a swipe, a scrollbar) moved the row: let it
        glideTarget = null;
        edgeVel = 0;
      }
      glideLast = now;
      const max = m.maxScroll;
      let pos = glidePos;
      const target = edgeTarget();
      if (target) glideTarget = null; // the edge takes over from a wheel glide
      if (glideTarget !== null) {
        pos += (glideTarget - pos) * (1 - Math.exp(-dt / GLIDE_TAU));
        if (Math.abs(glideTarget - pos) < 0.5) {
          pos = glideTarget;
          glideTarget = null;
        }
      } else {
        edgeVel += (target - edgeVel) * (1 - Math.exp(-dt / EDGE_TAU));
        pos += (edgeVel * dt) / 1000;
      }
      pos = Math.max(0, Math.min(max, pos));
      if (pos <= 0 || pos >= max) edgeVel = 0; // ran out of row
      glidePos = pos;
      dock.scrollLeft = pos;
      if (glideTarget === null && !target && Math.abs(edgeVel) < 12) return endGlide();
      schedule();
      glideFrame = requestAnimationFrame(glide);
    };

    function startGlide() {
      if (glideFrame || reduced()) return;
      if (needMeasure) measure();
      if (glideTarget === null && !edgeTarget() && !edgeVel) return;
      // Snapping would pull every small step back to the nearest tile.
      dock.classList.add("is-gliding");
      glideFrame = requestAnimationFrame(glide);
    }

    function stopGlide() {
      glideTarget = null;
      if (glideFrame) endGlide();
    }

    // Glide to a scrollLeft, or jump there with reduced motion. This is explicit
    // input, so a pointer parked in an edge zone must not undo it.
    const glideTo = (to) => {
      edgeHold = true;
      if (reduced()) {
        stopGlide();
        dock.scrollLeft = to;
        return;
      }
      glideTarget = to;
      edgeVel = 0;
      startGlide();
    };

    // Keyboard focus: bring the tile clear of the edge fades. Done by hand
    // because scroll snapping pulls a small scrollIntoView back to the tile
    // it started on.
    function reveal(c) {
      edgeHold = true; // a parked pointer must not scroll the focused tile away
      rail.scrollIntoView({ block: "nearest", behavior: reduced() ? "auto" : "smooth" });
      if (needMeasure) measure();
      if (!m.scroll) return;
      const pad = parseFloat(getComputedStyle(dock).getPropertyValue("--ex-fade")) || 0;
      const from = glideTarget !== null ? glideTarget : dock.scrollLeft;
      const l = c.tile.offsetLeft - pad;
      const r = c.tile.offsetLeft + c.tile.offsetWidth + pad - dock.clientWidth;
      const to = Math.max(0, Math.min(m.maxScroll, l < from ? l : r > from ? r : from));
      if (Math.abs(to - from) >= 1) glideTo(to);
      else if (edgeVel) stopGlide();
    }

    dock.addEventListener(
      "wheel",
      (e) => {
        if (e.ctrlKey || e.shiftKey) return; // zoom, and the native sideways wheel
        if (needMeasure) measure();
        if (!m.scroll) return;
        const unit = e.deltaMode === 1 ? WHEEL_LINE : e.deltaMode === 2 ? dock.clientWidth : 1;
        const dx = e.deltaX * unit;
        const dy = e.deltaY * unit;
        if (!dy || Math.abs(dx) >= Math.abs(dy)) return; // a sideways swipe scrolls natively
        const from = glideTarget !== null ? glideTarget : dock.scrollLeft;
        const max = m.maxScroll;
        if ((dy < 0 && from <= 0.5) || (dy > 0 && from >= max - 0.5)) return; // the page scrolls
        e.preventDefault();
        const to = Math.max(0, Math.min(max, from + dy));
        glideTo(to);
      },
      { passive: false }
    );
  };

  initClips();
})();
