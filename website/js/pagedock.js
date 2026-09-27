// Pagedock: the page's table of contents as a working mini Krema dock.
// Parabolic zoom, running/active indicators from reading progress, launch
// bounce + genie restore, dodge with bottom-edge reveal, roving keyboard focus.
(() => {
  "use strict";

  const nav = document.querySelector("[data-pagedock]");
  const list = nav && nav.querySelector(".pd-tiles");
  if (!list) return;

  const MAX = 1.6; // peak magnification
  const REACH = 2.5; // falloff radius in tile pitches
  const EDGE = 48; // px from the viewport bottom that reveals a dodged dock
  const GENIE_MS = 280;
  const BOUNCE_MS = 520;
  const READ_LINE = "-35% 0px -64% 0px"; // 1% band at 35% of the viewport

  const motionQuery = window.matchMedia("(prefers-reduced-motion: reduce)");
  const reduced = () => motionQuery.matches;
  const tip = nav.querySelector(".pd-tip");

  // ---------- Model ----------

  const items = Array.from(list.children).map((li) => {
    const tile = li.querySelector(".pd-tile");
    return {
      li,
      tile,
      face: tile && tile.querySelector(".pd-face"),
      badge: tile && tile.querySelector(":scope > .pd-badge"),
      center: 0,
      width: 0,
      s: 1,
      x: 0,
    };
  });
  const tileItems = items.filter((it) => it.tile);
  if (!tileItems.length) return;
  const itemOf = new Map(tileItems.map((it) => [it.tile, it]));

  const sectionOf = new Map(); // tile -> section
  const tileOf = new Map(); // section -> tile
  for (const { tile } of tileItems) {
    if (tile.hasAttribute("data-external")) continue;
    const id = tile.dataset.section;
    const sec = id && document.getElementById(id);
    if (!sec) continue;
    sectionOf.set(tile, sec);
    tileOf.set(sec, tile);
  }
  const sections = Array.from(tileOf.keys());

  // ---------- Stretchable background + edge hint ----------

  const piece = (cls) => {
    const el = document.createElement("span");
    el.className = cls;
    return el;
  };
  const bg = piece("pd-bg");
  bg.setAttribute("aria-hidden", "true");
  const bgL = piece("pd-bg-l");
  const bgM = piece("pd-bg-m");
  const bgR = piece("pd-bg-r");
  bg.append(bgL, bgM, bgR);
  nav.prepend(bg);
  nav.classList.add("pd-ready");

  const edge = piece("pd-edge");
  edge.setAttribute("aria-hidden", "true");
  nav.after(edge);

  // ---------- State ----------

  let m = null; // cached metrics
  let frame = 0;
  let needMeasure = true;
  let zoomDirty = true;
  let stateDirty = true;
  let hideDirty = true;

  let pointerX = null; // padding-box x of the pointer over the dock
  let pointerInside = false;
  let touchActive = false;
  let tracking = false;
  let appliedTracking = false;
  let focusItem = null;
  let focusInside = false;
  let lastY = -1; // last mouse clientY anywhere on the page
  let nearEdge = false;

  const onLine = new Set(); // sections crossing the reading line
  const running = new Set();
  let active = null;
  let atBottom = false;
  let docMax = 0;

  const overlapping = new Set();
  let footVisible = false;
  let hidden = false;
  let tipLabel = "";
  let tipOn = false;

  const schedule = () => {
    if (!frame) frame = requestAnimationFrame(render);
  };

  // ---------- Measure (reads only) ----------

  const measure = () => {
    const cs = getComputedStyle(nav);
    const rect = nav.getBoundingClientRect();
    for (const it of items) {
      it.width = it.li.offsetWidth;
      it.center = it.li.offsetLeft + it.width / 2;
    }
    const tile = tileItems[0].li.offsetWidth;
    const gap = parseFloat(getComputedStyle(list).columnGap) || 0;
    const bottom = parseFloat(cs.bottom) || 0;
    const height = nav.offsetHeight;
    const width = nav.offsetWidth;
    m = {
      tile,
      reach: REACH * (tile + gap),
      left: rect.left,
      right: rect.left + width,
      border: nav.clientLeft,
      width,
      height,
      top: window.innerHeight - bottom - height,
      padBottom: parseFloat(cs.paddingBottom) || 0,
      cap: bgL.offsetWidth,
    };
    docMax = document.documentElement.scrollHeight - window.innerHeight;
  };

  // ---------- Zoom ----------

  const zoomAt = (d) => {
    if (d >= m.reach) return 1;
    const c = Math.cos((Math.PI * d) / (2 * m.reach));
    return 1 + (MAX - 1) * c * c;
  };

  const target = () => {
    if (pointerX !== null) return pointerX;
    if (focusItem) return focusItem.center;
    return null;
  };

  // Keep the dock centred: grow symmetrically, every item shifted by the
  // growth of the items before it so zoomed tiles never overlap.
  const layout = (x) => {
    let growth = 0;
    for (const it of items) {
      it.s = x === null || !it.tile ? 1 : zoomAt(Math.abs(x - it.center));
      growth += (it.s - 1) * it.width;
    }
    let acc = -growth / 2;
    for (const it of items) {
      const d = (it.s - 1) * it.width;
      it.x = acc + d / 2;
      acc += d;
    }
    return growth / 2;
  };

  const writeZoom = () => {
    const x = target();
    const g = layout(x);
    for (const it of items) {
      it.li.style.translate = Math.abs(it.x) < 0.01 ? "" : `${it.x.toFixed(2)}px 0`;
      if (it.face) it.face.style.scale = it.s === 1 ? "" : it.s.toFixed(4);
      if (it.badge) {
        it.badge.style.translate =
          it.s === 1 ? "" : `${(((it.s - 1) * m.tile) / 2).toFixed(2)}px ${((1 - it.s) * m.tile).toFixed(2)}px`;
      }
    }
    const mid = m.width - 2 * m.cap;
    bgL.style.transform = g ? `translateX(${(-g).toFixed(2)}px)` : "";
    bgR.style.transform = g ? `translateX(${g.toFixed(2)}px)` : "";
    bgM.style.transform = g && mid > 0 ? `scaleX(${((mid + 2 * g) / mid).toFixed(5)})` : "";

    if (!tip) return;
    let peak = focusItem && pointerX === null ? focusItem : null;
    if (!peak && x !== null) {
      for (const it of tileItems) if (!peak || it.s > peak.s) peak = it;
      if (peak && peak.s <= 1.001) peak = null;
    }
    if (!peak) {
      if (tipOn) tip.classList.remove("is-on");
      tipOn = false;
      return;
    }
    const label = peak.tile.getAttribute("aria-label") || "";
    if (label !== tipLabel) tip.textContent = tipLabel = label;
    const lift = m.padBottom + peak.s * m.tile + 10;
    tip.style.transform = `translate(${(peak.center + peak.x).toFixed(2)}px, ${(-lift).toFixed(2)}px) translateX(-50%)`;
    if (!tipOn) tip.classList.add("is-on");
    tipOn = true;
  };

  // ---------- Section state ----------

  const pickActive = () => {
    let next = null;
    if (atBottom && sections.length) next = sections[sections.length - 1];
    else for (const sec of sections) if (onLine.has(sec)) next = sec;
    if (next && next !== active) {
      active = next;
      running.add(next);
      stateDirty = true;
    }
  };

  const setRoving = (tile) => {
    for (const it of tileItems) it.tile.setAttribute("tabindex", it.tile === tile ? "0" : "-1");
  };

  const writeState = () => {
    for (const [tile, sec] of sectionOf) {
      const isActive = sec === active;
      tile.classList.toggle("is-active", isActive);
      tile.classList.toggle("is-running", running.has(sec));
      if (isActive) tile.setAttribute("aria-current", "true");
      else tile.removeAttribute("aria-current");
    }
    if (!focusInside) setRoving((active && tileOf.get(active)) || tileItems[0].tile);
  };

  // ---------- Dodge ----------

  const writeHidden = () => {
    if (lastY >= 0) nearEdge = lastY >= window.innerHeight - EDGE || (nearEdge && lastY >= m.top - 16);
    const reveal = nearEdge || focusInside || pointerInside;
    const next = (overlapping.size > 0 || footVisible) && !reveal;
    if (next !== hidden) {
      hidden = next;
      nav.classList.toggle("is-hidden", hidden);
    }
  };

  const dodgeEls = Array.from(document.querySelectorAll("[data-dodge]"));
  let dodgeIO = null;
  let dodgeKey = "";

  // Observe only the bottom band of the viewport the dock occupies; the
  // horizontal overlap test uses the cached dock extent.
  const buildDodge = () => {
    if (!dodgeEls.length || !("IntersectionObserver" in window)) return;
    const top = Math.max(0, Math.floor(m.top));
    const key = `${top}|${Math.round(m.left)}|${Math.round(m.right)}`;
    if (dodgeIO && key === dodgeKey) return;
    dodgeKey = key;
    if (dodgeIO) dodgeIO.disconnect();
    overlapping.clear();
    dodgeIO = new IntersectionObserver(
      (entries) => {
        for (const en of entries) {
          const r = en.intersectionRect;
          const hit = en.isIntersecting && r.height > 0 && r.right > m.left - 8 && r.left < m.right + 8;
          if (hit) overlapping.add(en.target);
          else overlapping.delete(en.target);
        }
        hideDirty = true;
        schedule();
      },
      { rootMargin: `-${top}px 0px 0px 0px` },
    );
    for (const el of dodgeEls) dodgeIO.observe(el);
  };

  // ---------- Frame ----------

  function render() {
    frame = 0;
    // Reads first.
    if (needMeasure) {
      measure();
      needMeasure = false;
      zoomDirty = hideDirty = true;
      buildDodge();
      edge.style.width = `${m.width}px`;
    }
    const bottomNow = docMax > 0 && window.scrollY >= docMax - 2;
    if (bottomNow !== atBottom) {
      atBottom = bottomNow;
      pickActive();
    }

    // Writes.
    if (tracking !== appliedTracking) {
      appliedTracking = tracking;
      nav.classList.toggle("is-tracking", tracking);
    }
    if (zoomDirty) {
      zoomDirty = false;
      writeZoom();
    }
    if (stateDirty) {
      stateDirty = false;
      writeState();
    }
    if (hideDirty) {
      hideDirty = false;
      writeHidden();
    }
  }

  // ---------- Pointer ----------

  const localX = (clientX) => clientX - m.left - m.border;

  nav.addEventListener(
    "pointerenter",
    (e) => {
      if (e.pointerType === "touch") return;
      pointerInside = tracking = true;
      hideDirty = true;
      schedule();
    },
    { passive: true },
  );
  nav.addEventListener(
    "pointermove",
    (e) => {
      if (!m || (e.pointerType === "touch" && !touchActive)) return;
      pointerX = localX(e.clientX);
      zoomDirty = true;
      schedule();
    },
    { passive: true },
  );
  nav.addEventListener(
    "pointerdown",
    (e) => {
      if (e.pointerType !== "touch" || !m) return;
      touchActive = tracking = true;
      pointerX = localX(e.clientX);
      zoomDirty = true;
      schedule();
    },
    { passive: true },
  );
  const release = () => {
    touchActive = tracking = pointerInside = false;
    pointerX = null;
    zoomDirty = hideDirty = true;
    schedule();
  };
  nav.addEventListener("pointerleave", release, { passive: true });
  nav.addEventListener("pointercancel", release, { passive: true });
  nav.addEventListener(
    "pointerup",
    (e) => {
      if (e.pointerType === "touch") release();
    },
    { passive: true },
  );

  // Bottom-edge reveal, mouse only; one rAF per frame at most.
  document.addEventListener(
    "pointermove",
    (e) => {
      if (e.pointerType !== "mouse") return;
      lastY = e.clientY;
      hideDirty = true;
      schedule();
    },
    { passive: true },
  );

  // ---------- Keyboard ----------

  list.addEventListener("focusin", (e) => {
    const it = itemOf.get(e.target);
    if (!it) return;
    focusItem = it;
    focusInside = true;
    setRoving(it.tile);
    zoomDirty = hideDirty = true;
    schedule();
  });
  list.addEventListener("focusout", (e) => {
    if (e.relatedTarget && list.contains(e.relatedTarget)) return;
    focusItem = null;
    focusInside = false;
    zoomDirty = hideDirty = stateDirty = true;
    schedule();
  });
  list.addEventListener("keydown", (e) => {
    const it = itemOf.get(e.target);
    if (!it || e.altKey || e.ctrlKey || e.metaKey) return;
    const i = tileItems.indexOf(it);
    const n = tileItems.length;
    let next = -1;
    if (e.key === "ArrowRight") next = (i + 1) % n;
    else if (e.key === "ArrowLeft") next = (i - 1 + n) % n;
    else if (e.key === "Home") next = 0;
    else if (e.key === "End") next = n - 1;
    if (next < 0) return;
    e.preventDefault();
    tileItems[next].tile.focus();
  });

  // ---------- Launch: bounce + genie restore ----------

  const rootStyle = getComputedStyle(document.documentElement);
  const cssVar = (name, fallback) => rootStyle.getPropertyValue(name).trim() || fallback;
  const EASE = cssVar("--ease-out", "cubic-bezier(.22,1,.36,1)");
  const BLUE = cssVar("--blue", "#3DAEE9");

  const bounce = (it) => {
    if (!it.face || !it.face.animate) return;
    const up = "cubic-bezier(.2,.7,.3,1)";
    const down = "cubic-bezier(.6,0,.8,.4)";
    it.tile.classList.add("is-launching");
    const anim = it.face.animate(
      [
        { translate: "0 0", easing: up },
        { translate: "0 -14px", offset: 0.25, easing: down },
        { translate: "0 0", offset: 0.5, easing: up },
        { translate: "0 -14px", offset: 0.75, easing: down },
        { translate: "0 0" },
      ],
      { duration: BOUNCE_MS },
    );
    const done = () => it.tile.classList.remove("is-launching");
    anim.onfinish = done;
    anim.oncancel = done;
  };

  let ghost = null;
  let ghostAnims = [];
  let midTimer = 0;
  let pendingJump = null;

  const genie = (it, atMidpoint) => {
    const r = (it.face || it.tile).getBoundingClientRect();
    const vw = document.documentElement.clientWidth;
    const vh = window.innerHeight;
    if (!ghost) {
      ghost = document.createElement("div");
      ghost.className = "pd-genie";
      ghost.setAttribute("aria-hidden", "true");
      document.body.append(ghost);
    }
    for (const a of ghostAnims) a.cancel();
    if (pendingJump) {
      clearTimeout(midTimer);
      pendingJump = null;
    }
    ghost.hidden = false;

    // The ghost is a viewport-sized box scaled down onto the tile. Radius and
    // the 1px ring are compensated per axis so they read true at every scale.
    const r0 = r.width * 0.22;
    const frames = [];
    const N = 10;
    for (let k = 0; k <= N; k++) {
      const p = k / N;
      const x = r.left * (1 - p);
      const y = r.top * (1 - p);
      const w = r.width + (vw - r.width) * p;
      const h = r.height + (vh - r.height) * p;
      const sx = w / vw;
      const sy = h / vh;
      const rv = r0 + (16 - r0) * p;
      const bx = (1 / sx).toFixed(3);
      const by = (1 / sy).toFixed(3);
      frames.push({
        offset: p,
        transform: `translate(${x.toFixed(2)}px, ${y.toFixed(2)}px) scale(${sx.toFixed(5)}, ${sy.toFixed(5)})`,
        borderRadius: `${(rv / sx).toFixed(2)}px / ${(rv / sy).toFixed(2)}px`,
        boxShadow:
          `inset ${bx}px 0 0 0 ${BLUE}, inset -${bx}px 0 0 0 ${BLUE}, ` +
          `inset 0 ${by}px 0 0 ${BLUE}, inset 0 -${by}px 0 0 ${BLUE}`,
      });
    }
    const shape = ghost.animate(frames, { duration: GENIE_MS, easing: EASE, fill: "both" });
    const fade = ghost.animate([{ opacity: 1 }, { opacity: 1, offset: 0.6 }, { opacity: 0 }], {
      duration: GENIE_MS,
      easing: "linear",
      fill: "both",
    });
    ghostAnims = [shape, fade];
    fade.onfinish = () => {
      for (const a of ghostAnims) a.cancel();
      ghostAnims = [];
      ghost.hidden = true;
    };

    pendingJump = atMidpoint;
    midTimer = window.setTimeout(() => {
      const run = pendingJump;
      pendingJump = null;
      if (run) run();
    }, GENIE_MS / 2);
  };

  const jump = (sec) => {
    try {
      sec.scrollIntoView({ behavior: "instant", block: "start" });
    } catch {
      sec.scrollIntoView(true);
    }
    const heading = sec.matches("h1, h2") ? sec : sec.querySelector("h1, h2") || sec;
    if (!heading.hasAttribute("tabindex")) heading.setAttribute("tabindex", "-1");
    heading.focus({ preventScroll: true });
  };

  list.addEventListener("click", (e) => {
    const tile = e.target.closest && e.target.closest(".pd-tile");
    const sec = tile && sectionOf.get(tile);
    if (!sec) return; // external tiles are plain links
    if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
    e.preventDefault();
    const hash = `#${sec.id}`;
    if (location.hash !== hash) history.pushState(null, "", hash);
    if (reduced() || !document.body.animate) {
      jump(sec);
      return;
    }
    const it = itemOf.get(tile);
    if (!running.has(sec)) bounce(it);
    genie(it, () => jump(sec));
  });

  // ---------- Observers ----------

  if ("IntersectionObserver" in window && sections.length) {
    const lineIO = new IntersectionObserver(
      (entries) => {
        for (const en of entries) {
          if (en.isIntersecting) onLine.add(en.target);
          else onLine.delete(en.target);
        }
        pickActive();
        schedule();
      },
      { rootMargin: READ_LINE },
    );
    const thresholds = [0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1];
    const seenIO = new IntersectionObserver(
      (entries) => {
        for (const en of entries) {
          if (!en.isIntersecting || running.has(en.target)) continue;
          const viewH = en.rootBounds ? en.rootBounds.height : window.innerHeight;
          // Tall sections count once they fill 40% of the viewport.
          if (en.intersectionRatio >= 0.4 || en.intersectionRect.height >= 0.4 * viewH) {
            running.add(en.target);
            seenIO.unobserve(en.target);
            stateDirty = true;
          }
        }
        schedule();
      },
      { threshold: thresholds },
    );
    for (const sec of sections) {
      lineIO.observe(sec);
      seenIO.observe(sec);
    }

    const foot = document.querySelector(".foot");
    if (foot) {
      new IntersectionObserver((entries) => {
        footVisible = entries[entries.length - 1].isIntersecting;
        hideDirty = true;
        schedule();
      }).observe(foot);
    }
  }

  const remeasure = () => {
    needMeasure = true;
    schedule();
  };
  window.addEventListener("resize", remeasure, { passive: true });
  window.addEventListener("scroll", schedule, { passive: true });
  if ("ResizeObserver" in window) new ResizeObserver(remeasure).observe(document.body);
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(remeasure);

  setRoving(tileItems[0].tile);
  schedule();
})();
