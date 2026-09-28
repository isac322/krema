/* Hero lens: the headline "krema" magnifies under the pointer like Krema's dock.
   scale(d) = 1 + (MAX - 1) * cos²(π·d / (2R)) for |d| < R, else 1.
   Layout (offsetLeft/offsetWidth, unaffected by transforms) is cached and only
   re-measured on resize; each frame is pure arithmetic + transform writes.

   Containment (nothing is ever clipped, at any width):
   letters are packed edge to edge at their scaled widths, so every letter box
   lies inside the span [S, S + P], P = Σ widthᵢ·scaleᵢ. Each frame:
     1. fit = min(1, room / P), room = the lens content box right of the icon,
        minus a small ink allowance for the tightened letter-spacing;
     2. the whole span is scaled by fit around the pointer (sizes and positions);
     3. S is clamped to [roomL, roomR − P·fit] (always non-empty since P·fit ≤ room).
   Hence every letter lies in [roomL, roomR] ⊆ the lens content box ⊆ the section
   content column ⊆ the viewport, whatever the font, viewport or pointer. At rest
   (P = resting width ≤ room) fit = 1 and S = the k's layout position, i.e. the
   resting wordmark is unchanged. */
(() => {
  "use strict";

  const hero = document.getElementById("top");
  const lens = hero && hero.querySelector(".lens");
  if (!lens) return;
  const chars = Array.from(lens.querySelectorAll(".lens-ch"));
  const dot = lens.querySelector(".lens-dot");
  const n = chars.length;
  if (!n) return;

  const REST_RATIOS = [0.72, 0.87, 1.16, 0.87, 0.72];
  const rest = chars.map((_, i) => REST_RATIOS[i] ?? 1);
  const peak = rest.indexOf(Math.max(...rest));
  const MAX = 1.35;
  // Tight falloff (R counted in average letter widths): a neighbour one average
  // letter away reaches 1 + 0.35·cos²(π/2.6) ≈ 1.04 (was ≈ 1.20 at R = 2.2); with the
  // pointer on e's centre the narrow r gets ≈ 1.14 (was ≈ 1.25).
  const RADIUS_IN_LETTERS = 1.3;
  const INK_EM = 0.05; // glyph ink may overhang its box by |letter-spacing| (0.035em)
  const ENTER_MS = 140; // rest → live blend when the pointer arrives
  const TAP_HOLD_MS = 450; // touch: keep the zoom briefly after the finger lifts
  const INTRO_MS = 1100;
  const INTRO_DELAY_MS = 350;

  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

  // Cached geometry, in lens (padding-box) coordinates.
  const left = new Float64Array(n);
  const width = new Float64Array(n);
  const centre = new Float64Array(n);
  let originX = 0;
  let radius = 1;
  let wordStart = 0;
  let wordEnd = 0;
  let roomL = 0; // letters may occupy [roomL, roomR]
  let roomR = 0;

  // Per-frame scratch.
  const scale = new Float64Array(n);
  const packedLeft = new Float64Array(n);

  // Zero-size inline box on the baseline of the peak letter: its offsetTop
  // is the baseline inside the letter box, whatever the fallback font is.
  const probe = document.createElement("span");
  probe.className = "lens-probe";
  probe.setAttribute("aria-hidden", "true");
  chars[peak].appendChild(probe);

  function measure() {
    // Reads first…
    for (let i = 0; i < n; i++) {
      left[i] = chars[i].offsetLeft;
      width[i] = chars[i].offsetWidth;
      centre[i] = left[i] + width[i] / 2;
    }
    const base = probe.offsetTop;
    const dotTop = chars[peak].offsetTop + base;
    const fontPx = parseFloat(getComputedStyle(lens).fontSize) || 0;
    wordStart = left[0];
    wordEnd = left[n - 1] + width[n - 1];
    radius = Math.max(1, (RADIUS_IN_LETTERS * (wordEnd - wordStart)) / n);
    // The lens has no horizontal padding: its content box is [0, clientWidth].
    // Starting at the k keeps the zoom clear of the lockup icon.
    roomL = wordStart;
    roomR = Math.max(roomL + 1, lens.clientWidth - INK_EM * fontPx);
    originX = lens.getBoundingClientRect().left + lens.clientLeft;
    // …then writes (transform-origin and dot position only; no layout change).
    lens.style.setProperty("--base", `${base}px`);
    lens.style.setProperty("--dot-top", `${dotTop}px`);
  }

  function curve(d) {
    if (d <= -radius || d >= radius) return 1;
    const c = Math.cos((Math.PI * d) / (2 * radius));
    return 1 + (MAX - 1) * c * c;
  }

  function nearest(p) {
    let best = 0;
    let bestD = Infinity;
    for (let i = 0; i < n; i++) {
      const d = Math.abs(p - centre[i]);
      if (d < bestD) { bestD = d; best = i; }
    }
    return best;
  }

  // Where original-layout point p lands in the packed (scaled) layout.
  function mapPoint(p) {
    if (p <= wordStart) return packedLeft[0] - (wordStart - p);
    if (p >= wordEnd) return packedLeft[n - 1] + width[n - 1] * scale[n - 1] + (p - wordEnd);
    for (let i = 0; i < n; i++) {
      if (p < left[i] + width[i] || i === n - 1) {
        const f = Math.min(1, Math.max(0, (p - left[i]) / width[i]));
        return packedLeft[i] + f * width[i] * scale[i];
      }
    }
    return p;
  }

  /* p: pointer x in lens coords (ignored when blend is 0).
     blend: 0 = resting wordmark ratios, anchored at the k's left edge;
            1 = full parabolic zoom, anchored so the point under the pointer stays
                put unless that would push a letter out of the room. */
  function render(p, blend) {
    for (let i = 0; i < n; i++) {
      scale[i] = blend > 0 ? rest[i] + (curve(p - centre[i]) - rest[i]) * blend : rest[i];
    }
    // Pack letters edge to edge at their scaled widths: they never overlap and
    // spacing opens around the magnified letter, like Krema's own layout.
    let x = wordStart;
    for (let i = 0; i < n; i++) {
      packedLeft[i] = x;
      x += width[i] * scale[i];
    }
    const span = x - wordStart; // P
    const anchor = blend > 0 ? (p - mapPoint(p)) * blend : 0;

    // Containment (see header): fit the span into the room, then clamp it.
    const room = roomR - roomL;
    const fit = span > room ? room / span : 1;
    const ref = blend > 0 ? p : wordStart;
    let start = ref + (wordStart + anchor - ref) * fit;
    const maxStart = roomR - span * fit;
    if (start > maxStart) start = maxStart;
    if (start < roomL) start = roomL;

    for (let i = 0; i < n; i++) {
      const s = scale[i] * fit;
      const visLeft = start + (packedLeft[i] - wordStart) * fit;
      const tx = visLeft + (width[i] * s) / 2 - centre[i];
      chars[i].style.transform =
        `translate3d(${tx.toFixed(2)}px,0,0) scale(${s.toFixed(4)})`;
    }

    if (dot) {
      let dx = packedLeft[peak] + (width[peak] * scale[peak]) / 2;
      if (blend > 0) {
        const f = nearest(p);
        const liveX = packedLeft[f] + (width[f] * scale[f]) / 2;
        dx += (liveX - dx) * blend;
      }
      const dotX = start + (dx - wordStart) * fit;
      dot.style.transform = `translate3d(${dotX.toFixed(2)}px,0,0) translateX(-50%)`;
    }
  }

  function renderRest() {
    render(0, 0);
  }

  // ---------- Pointer-driven loop (one rAF per frame) ----------

  let active = false;
  let pointerX = 0;
  let enterStart = -1; // -1: set on next frame
  let raf = 0;
  let touchId = -1; // the finger that started on the wordmark row
  let holdTimer = 0;

  function frame(t) {
    raf = 0;
    if (!active) return;
    let blend = 1;
    if (!reduceMotion.matches) {
      if (enterStart < 0) enterStart = t;
      const k = Math.min(1, (t - enterStart) / ENTER_MS);
      blend = 1 - (1 - k) * (1 - k); // ease-out
    }
    render(pointerX, blend);
    if (blend < 1) raf = requestAnimationFrame(frame);
  }

  function schedule() {
    if (!raf) raf = requestAnimationFrame(frame);
  }

  function clearHold() {
    if (holdTimer) { clearTimeout(holdTimer); holdTimer = 0; }
  }

  function drive(e) {
    stopIntro();
    clearHold();
    pointerX = e.clientX - originX;
    if (!active) {
      active = true;
      enterStart = -1;
      lens.classList.add("is-live");
      hero.classList.add("is-touched");
    }
    schedule();
  }

  function release() {
    clearHold();
    if (!active) return;
    active = false;
    if (raf) { cancelAnimationFrame(raf); raf = 0; }
    // Removing is-live restores the spring transition in the same style pass.
    lens.classList.remove("is-live");
    renderRest();
  }

  function onDown(e) {
    if (e.pointerType === "mouse") { drive(e); return; }
    // Touch / pen: only a contact that starts on the wordmark row drives the lens.
    if (!lens.contains(e.target)) return;
    touchId = e.pointerId;
    // Keep moves coming to the lens even when the finger slides off a letter.
    try { lens.setPointerCapture(e.pointerId); } catch (_) { /* pointer already gone */ }
    drive(e);
  }

  function onMove(e) {
    if (e.pointerType === "mouse") drive(e);
    else if (e.pointerId === touchId) drive(e);
  }

  function onUp(e) {
    if (e.pointerType === "mouse" || e.pointerId !== touchId) return;
    touchId = -1;
    // A tap shows a brief zoom around the letter; a drag settles the same way.
    clearHold();
    holdTimer = setTimeout(release, TAP_HOLD_MS);
  }

  function onCancel(e) {
    // The browser took over (vertical scroll, pinch): reset at once.
    if (e.pointerId === touchId) touchId = -1;
    release();
  }

  hero.addEventListener("pointerdown", onDown, { passive: true });
  hero.addEventListener("pointermove", onMove, { passive: true });
  hero.addEventListener("pointerup", onUp, { passive: true });
  hero.addEventListener("pointercancel", onCancel, { passive: true });
  hero.addEventListener("pointerleave", (e) => {
    if (e.pointerType === "mouse") release();
  }, { passive: true });

  // ---------- Intro sweep: once, left → right ----------

  let introRaf = 0;
  let introTimer = 0;

  function stopIntro() {
    if (introTimer) { clearTimeout(introTimer); introTimer = 0; }
    if (introRaf) {
      cancelAnimationFrame(introRaf);
      introRaf = 0;
      if (!active) {
        lens.classList.remove("is-live");
        renderRest();
      }
    }
  }

  function startIntro() {
    introTimer = 0;
    const from = wordStart - radius * 0.5;
    const to = wordEnd + radius * 0.5;
    let start = -1;
    lens.classList.add("is-live");
    const step = (t) => {
      if (start < 0) start = t;
      const k = Math.min(1, (t - start) / INTRO_MS);
      const e = k < 0.5 ? 2 * k * k : 1 - 2 * (1 - k) * (1 - k); // ease-in-out
      // Fade the zoom in and out at the ends so it departs from and returns to rest.
      const blend = Math.min(1, k / 0.18, (1 - k) / 0.18);
      render(from + (to - from) * e, blend);
      if (k < 1) {
        introRaf = requestAnimationFrame(step);
      } else {
        introRaf = 0;
        lens.classList.remove("is-live");
        renderRest();
      }
    };
    introRaf = requestAnimationFrame(step);
  }

  // ---------- Init ----------

  measure();
  renderRest();
  // Enable transitions only after the measured rest pose has been painted.
  requestAnimationFrame(() => requestAnimationFrame(() => lens.classList.add("is-ready")));

  const remeasure = () => {
    measure();
    if (active) schedule();
    else if (!introRaf) renderRest();
  };

  if ("ResizeObserver" in window) {
    const ro = new ResizeObserver(remeasure);
    ro.observe(lens);
    ro.observe(hero);
  } else {
    window.addEventListener("resize", remeasure, { passive: true });
  }
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(remeasure);

  if (!reduceMotion.matches) {
    const r = hero.getBoundingClientRect();
    if (r.bottom > 0 && r.top < window.innerHeight) {
      introTimer = setTimeout(startIntro, INTRO_DELAY_MS);
    }
  }
})();
