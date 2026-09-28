/* Features: parabolic accordion.
 * Wide (>=900px): each panel's flex-grow follows the dock zoom curve
 *   grow(d) = 1 + (MAX-1)·cos²(π·d / 2R)  for |d| < R, else 1
 * over the panel-index distance d to the pointer's fractional position.
 * Narrow: vertical stack magnified by distance to the viewport centre.
 * All style writes happen in one rAF, after the frame's reads. */
(() => {
  'use strict';

  const root = document.querySelector('[data-fx]');
  if (!root) return;
  const panels = Array.from(root.querySelectorAll('.fx-panel'));
  const n = panels.length;
  if (!n) return;
  const bodies = panels.map((p) => p.querySelector('.fx-body'));

  // Each panel is a labelled region whose body it expands (wide) or always
  // shows (narrow); aria-expanded mirrors the visual state.
  panels.forEach((p, i) => {
    p.setAttribute('role', 'region');
    const b = bodies[i];
    if (!b) return;
    if (!b.id) b.id = `${p.getAttribute('aria-labelledby') || 'fx-' + i}-body`;
    p.setAttribute('aria-controls', b.id);
  });
  const writeExpanded = (i, on) => {
    panels[i].setAttribute('aria-expanded', String(on));
  };
  let openW = -1;
  let lastRowH = -1;

  const MAX = 5;
  const R = 1.6;
  // How far the pointer may pull the peak off its panel's centre (index units).
  // Kept small so only the hovered panel crosses the open threshold.
  const LEAN = 0.12;
  const OPEN_AT = 0.6 * MAX;
  const CLOSE_BELOW = 0.56 * MAX;
  // Narrow mode: neighbours settle at this scale/opacity.
  const MIN_S = 0.94;
  const MIN_O = 0.75;

  const wideMq = window.matchMedia('(min-width: 900px)');
  const calmMq = window.matchMedia('(prefers-reduced-motion: reduce)');

  const grow = (d) => {
    const a = Math.abs(d);
    if (a >= R) return 1;
    const c = Math.cos((Math.PI * a) / (2 * R));
    return 1 + (MAX - 1) * c * c;
  };

  let wide = wideMq.matches;
  let rest = 0; // peak without pointer: last focused/clicked panel
  let hover = null; // { i, x } while a mouse/pen hovers a panel
  let raf = 0;

  const open = new Array(n).fill(false);
  const lastGrow = new Array(n).fill(-1);

  let centres = [];
  let viewH = 0;
  let active = -1;
  const lastS = new Array(n).fill(-1);
  const lastO = new Array(n).fill(-1);

  const schedule = () => {
    if (!raf) raf = requestAnimationFrame(frame);
  };

  function frame() {
    raf = 0;
    if (wide) frameWide();
    else frameNarrow();
  }

  function frameWide() {
    let pos = rest;
    if (hover) {
      // Single read, before any write this frame.
      const r = panels[hover.i].getBoundingClientRect();
      const f = r.width > 0 ? Math.min(1, Math.max(0, (hover.x - r.left) / r.width)) : 0.5;
      pos = hover.i + (f - 0.5) * 2 * LEAN;
    }
    for (let i = 0; i < n; i++) {
      const g = grow(i - pos);
      if (Math.abs(g - lastGrow[i]) > 0.001) {
        lastGrow[i] = g;
        panels[i].style.flexGrow = g.toFixed(3);
      }
      // Hysteresis: open above 60% of MAX, close only below 56%.
      const o = open[i] ? g >= CLOSE_BELOW : g >= OPEN_AT;
      if (o !== open[i]) {
        open[i] = o;
        panels[i].classList.toggle('is-open', o);
        writeExpanded(i, o);
      }
    }
  }

  function frameNarrow() {
    const centre = window.scrollY + viewH / 2;
    const reach = viewH * 0.55;
    const scale = !calmMq.matches;
    let best = -1;
    let bestD = Infinity;
    for (let i = 0; i < n; i++) {
      const d = Math.abs(centre - centres[i]);
      if (d < bestD) {
        bestD = d;
        best = i;
      }
      let k = 0;
      if (d < reach) {
        const c = Math.cos((Math.PI * d) / (2 * reach));
        k = c * c;
      }
      const s = scale ? MIN_S + (1 - MIN_S) * k : 1;
      const o = MIN_O + (1 - MIN_O) * k;
      if (Math.abs(s - lastS[i]) > 0.0005) {
        lastS[i] = s;
        panels[i].style.setProperty('--fx-s', s.toFixed(4));
      }
      if (Math.abs(o - lastO[i]) > 0.002) {
        lastO[i] = o;
        panels[i].style.setProperty('--fx-o', o.toFixed(3));
      }
    }
    if (best !== active) {
      if (active >= 0) panels[active].classList.remove('is-active');
      active = best;
      if (active >= 0) panels[active].classList.add('is-active');
    }
  }

  function measure() {
    if (wide) {
      // Fixed body width = the peak's width in the most crowded layout,
      // so text never reflows while panels grow.
      const gap = parseFloat(getComputedStyle(root).columnGap) || 0;
      const avail = root.clientWidth - gap * (n - 1);
      let worst = 0;
      for (let p = 0; p < n; p++) {
        let sum = 0;
        for (let i = 0; i < n; i++) sum += grow(i - p);
        if (sum > worst) worst = sum;
      }
      const w = Math.max(0, Math.floor((avail * grow(LEAN)) / worst) - 2);
      if (w !== openW) {
        openW = w;
        root.style.setProperty('--fx-open-w', w + 'px');
      }
      // Row height = tallest body at that width + its top offset + the
      // panel's borders, so no open panel ever clips text. Collapsed bodies
      // are clip-hidden but still laid out, so their heights are real.
      let tallest = 0;
      for (let i = 0; i < n; i++) {
        const b = bodies[i];
        if (!b) continue;
        const h = b.offsetTop + b.offsetHeight;
        if (h > tallest) tallest = h;
      }
      const border = panels[0].offsetHeight - panels[0].clientHeight;
      const rowH = Math.ceil(tallest + border);
      if (rowH !== lastRowH) {
        lastRowH = rowH;
        root.style.setProperty('--fx-row-h', rowH + 'px');
      }
    } else {
      const sy = window.scrollY;
      viewH = window.innerHeight;
      centres = panels.map((p) => {
        const r = p.getBoundingClientRect();
        return r.top + sy + r.height / 2;
      });
    }
  }

  function resetStyles() {
    for (let i = 0; i < n; i++) {
      const p = panels[i];
      p.style.removeProperty('flex-grow');
      p.style.removeProperty('--fx-s');
      p.style.removeProperty('--fx-o');
      p.classList.remove('is-open', 'is-active');
      open[i] = false;
      writeExpanded(i, !wide); // narrow: every body is shown
      lastGrow[i] = lastS[i] = lastO[i] = -1;
    }
    active = -1;
    hover = null;
  }

  const indexOf = (el) => {
    const p = el && el.closest ? el.closest('.fx-panel') : null;
    return p ? panels.indexOf(p) : -1;
  };

  root.addEventListener(
    'pointermove',
    (e) => {
      if (!wide || e.pointerType === 'touch') return;
      const i = indexOf(e.target);
      if (i < 0) return; // over a gap: keep the last position
      hover = { i, x: e.clientX };
      schedule();
    },
    { passive: true }
  );

  root.addEventListener(
    'pointerleave',
    () => {
      if (!hover) return;
      hover = null;
      schedule();
    },
    { passive: true }
  );

  root.addEventListener(
    'pointerdown',
    (e) => {
      const i = indexOf(e.target);
      if (i < 0) return;
      rest = i;
      if (e.pointerType === 'touch') hover = null;
      schedule();
    },
    { passive: true }
  );

  root.addEventListener('focusin', (e) => {
    const i = indexOf(e.target);
    if (i < 0) return;
    rest = i;
    // Keyboard focus wins until the pointer moves again.
    hover = null;
    schedule();
  });

  root.addEventListener('keydown', (e) => {
    if (e.altKey || e.ctrlKey || e.metaKey) return;
    const i = panels.indexOf(e.target);
    if (i < 0) return; // only when a panel itself has focus
    let j = -1;
    switch (e.key) {
      case 'ArrowRight':
        j = Math.min(n - 1, i + 1);
        break;
      case 'ArrowLeft':
        j = Math.max(0, i - 1);
        break;
      case 'Home':
        j = 0;
        break;
      case 'End':
        j = n - 1;
        break;
      default:
        return;
    }
    e.preventDefault();
    if (j !== i) panels[j].focus();
  });

  window.addEventListener(
    'scroll',
    () => {
      if (!wide) schedule();
    },
    { passive: true }
  );

  const remeasure = () => {
    measure();
    schedule();
  };
  window.addEventListener('resize', remeasure, { passive: true });
  if ('ResizeObserver' in window) {
    // Root width drives the wide body width; body height shifts the
    // narrow-mode offsets when content above reflows.
    const ro = new ResizeObserver(remeasure);
    ro.observe(root);
    ro.observe(document.body);
  }

  const onMode = () => {
    wide = wideMq.matches;
    resetStyles();
    remeasure();
  };
  if (wideMq.addEventListener) wideMq.addEventListener('change', onMode);
  else wideMq.addListener(onMode);
  const onCalm = () => {
    for (let i = 0; i < n; i++) lastS[i] = -1;
    schedule();
  };
  if (calmMq.addEventListener) calmMq.addEventListener('change', onCalm);
  else calmMq.addListener(onCalm);

  for (let i = 0; i < n; i++) writeExpanded(i, !wide);
  root.setAttribute('data-fx-live', '');
  measure();
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(remeasure);
  frame();
})();
