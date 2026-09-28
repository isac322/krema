/* Mechanics: "Try the settings".
 * Turns the stacked settings pages into a tabbed Kirigami-style window and
 * drives the preview strip above it. The preview lives in a 240px-high
 * design space scaled to the stage width. Dock zoom follows
 *   scale(d) = 1 + (MAX-1)·cos²(π·d / 2R)  for |d| < R, else 1.
 * One rAF loop runs only while the stage is on screen, the tab is visible,
 * reduced motion is off and the current page has a demo. */
(() => {
  const root = document.querySelector('[data-sw]');
  if (!root) return;

  const $ = (s, el = root) => el.querySelector(s);
  const $$ = (s, el = root) => Array.from(el.querySelectorAll(s));

  const stage = $('[data-stage]');
  const sp = $('[data-pv]');
  const dock = $('[data-dock]');
  const bar = $('[data-bar]');
  const win = $('[data-win]');
  const winTitle = $('[data-win-title]');
  const pop = $('[data-pop]');
  const ths = $$('.sp-th', pop);
  const screens = $('[data-screens]');
  const mons = $$('.sp-mon', screens);
  const cursor = $('[data-cursor]');
  const pill = $('[data-att-pill]');
  const live = $('[data-announce]');
  const side = $('[data-side]');
  const tabs = $$('[role="tab"]', side);
  const pages = $$('.sw-page');
  const orderList = $('[data-order]');
  const reopenBtn = $('[data-reopen]');
  const walkBtn = $('[data-walk]');
  const coverBtn = $('[data-cover]');
  const notifyBtn = $('[data-notify]');
  const followSet = $('[data-follow]');
  if (!stage || !sp || !dock || !tabs.length || !pages.length) return;

  const icons = {};
  $$('.sp-ico', dock).forEach((el) => { icons[el.dataset.app] = el; });
  const NAMES = { dolphin: 'Dolphin', konsole: 'Konsole', firefox: 'Firefox', kmail: 'KMail', discover: 'Discover', settings: 'System Settings' };
  let order = Object.keys(icons);

  const mq = window.matchMedia('(prefers-reduced-motion: reduce)');
  let reduced = mq.matches;

  const H = 240;
  const MINW = 480;
  const LOOP = new Set(['appearance', 'behavior', 'previews', 'attention', 'screens']);

  const S = {
    page: 'appearance', zoom: 1.6, size: 48, even: true, bg: 'panel', vis: 'always', preview: true,
    badge: 'number', att: 'wiggle', stop: 5, dnd: false, where: 'active', follow: 'mouse', vd: 'all',
    scheme: 'dark',
  };

  /* ---------- geometry ---------- */
  let W = MINW, K = 1;
  let b = 38, gap = 9, pad = 12, restW = 0, x0 = 0, dockTop = 0;
  // Where the last frame drew the row: slot centres and widths in design px,
  // after zoom and after shrinking to fit. Hit testing and dragging use these.
  let slots = [], slotW = [], rowL = 0, rowR = 0;
  const EDGE = 8;
  let centers = [];
  const TRAVEL_GAP = 20;
  const OVERLAP = 12;
  let popBox = null;
  let thPts = [];
  let monPts = [];

  const relayout = () => {
    b = Math.round(S.size * 0.95);
    gap = Math.round(b * 0.24);
    pad = Math.round(b * 0.32);
    const n = order.length;
    restW = n * b + (n - 1) * gap + 2 * pad;
    x0 = (W - restW) / 2;
    centers = order.map((_, i) => x0 + pad + i * (b + gap) + b / 2);
    slots = centers.slice();
    slotW = order.map(() => b);
    rowL = x0;
    rowR = x0 + restW;
    dockTop = H - 8 - (b + 16);
    sp.style.setProperty('--b', b + 'px');
    bar.style.width = restW + 'px';
    win.style.height = Math.max(60, dockTop - 18 - TRAVEL_GAP) + 'px';
    last.clear();
    measurePop();
  };

  const measurePop = () => {
    if (S.page !== 'previews') { popBox = null; return; }
    const pw = pop.offsetWidth, ph = pop.offsetHeight;
    const kx = centers[order.indexOf('konsole')];
    const left = Math.max(8, Math.min(W - pw - 8, kx - pw / 2));
    const top = Math.max(8, dockTop - 12 - ph);
    pop.style.left = left + 'px';
    pop.style.top = top + 'px';
    popBox = { left, top };
    thPts = ths.map((th) => (th.offsetParent
      ? { x: left + th.offsetLeft + th.offsetWidth * 0.55, y: top + th.offsetTop + th.offsetHeight * 0.6 }
      : null));
  };

  const measureMons = () => {
    monPts = mons.map((m) => ({ x: m.offsetLeft + m.offsetWidth * 0.62, y: m.offsetTop + m.offsetHeight * 0.45 }));
  };

  const fit = () => {
    const cw = stage.clientWidth;
    if (!cw) return;
    K = Math.min(1.1, cw / MINW);
    W = cw / K;
    sp.style.setProperty('--pv-w', W.toFixed(2) + 'px');
    sp.style.setProperty('--pv-k', K.toFixed(4));
    stage.style.height = Math.round(H * K) + 'px';
    relayout();
    if (S.page === 'screens') measureMons();
    request();
  };

  /* ---------- small helpers ---------- */
  const ease = (x) => { const c = Math.min(1, Math.max(0, x)); return c * c * (3 - 2 * c); };
  const lerp = (a, c, k) => ({ x: a.x + (c.x - a.x) * k, y: a.y + (c.y - a.y) * k });
  // 0 → 1 → 0 plateau: rises over [a,b], holds, falls over [c,d]
  const drift = (p, a, c, d, e) => (p < a ? 0 : p < c ? ease((p - a) / (c - a)) : p < d ? 1 : p < e ? 1 - ease((p - d) / (e - d)) : 0);

  const last = new Map();
  const setT = (el, v) => { if (last.get(el) !== v) { last.set(el, v); el.style.transform = v; } };
  const setC = (el, cls, on) => { if (el.classList.contains(cls) !== on) el.classList.toggle(cls, on); };

  const announce = (msg) => { live.textContent = ''; window.setTimeout(() => { live.textContent = msg; }, 30); };

  const restart = (el, cls) => { el.classList.remove(cls); void el.offsetWidth; el.classList.add(cls); };

  let winLabel = 'Konsole 1';
  let shownTitle = winLabel;
  const showTitle = (text) => {
    if (text === shownTitle) return;
    shownTitle = text;
    winTitle.textContent = text;
    if (!reduced) restart(win, 'is-swap');
  };

  /* ---------- state -> attributes ---------- */
  const onoff = (v) => (v ? 'on' : 'off');
  const syncAttrs = () => {
    const d = root.dataset;
    d.bg = S.bg; d.even = onoff(S.even); d.preview = onoff(S.preview); d.badge = S.badge;
    d.dnd = onoff(S.dnd); d.scheme = S.scheme;
    screens.dataset.vd = S.vd;
    pop.inert = S.page !== 'previews' || !S.preview;
    if (followSet) followSet.disabled = S.where !== 'active';
  };

  const outFmt = { zoom: (v) => v.toFixed(1) + '×', size: (v) => v + ' px', stop: (v) => (v ? v + ' s' : 'No limit') };

  const readInput = (el) => {
    const k = el.name;
    if (!(k in S)) return false;
    if (el.type === 'checkbox') S[k] = el.checked;
    else if (el.type === 'radio') { if (!el.checked) return false; S[k] = el.value; }
    else if (el.type === 'range') {
      S[k] = Number(el.value);
      const o = $(`[data-out="${k}"]`);
      if (o) o.textContent = outFmt[k](S[k]);
    } else S[k] = el.value;
    return true;
  };

  /* ---------- page (tabs) ---------- */
  let t0 = performance.now();
  let manualHidden = false;

  const selectPage = (name, focus) => {
    tabs.forEach((t) => {
      const on = t.getAttribute('aria-controls') === 'sw-p-' + name;
      t.setAttribute('aria-selected', on ? 'true' : 'false');
      t.tabIndex = on ? 0 : -1;
      if (on && focus) t.focus();
      if (on && side.scrollWidth > side.clientWidth) {
        const l = t.offsetLeft - 6, r = t.offsetLeft + t.offsetWidth + 6;
        if (l < side.scrollLeft) side.scrollLeft = l;
        else if (r > side.scrollLeft + side.clientWidth) side.scrollLeft = r - side.clientWidth;
      }
    });
    pages.forEach((p) => p.classList.toggle('is-off', p.dataset.page !== name));
    if (S.page === 'attention' && name !== 'attention') setAtt(null);
    S.page = name;
    root.dataset.page = name;
    pop.inert = name !== 'previews' || !S.preview;
    t0 = performance.now();
    measurePop();
    if (name === 'screens') measureMons();
    request();
  };

  side.hidden = false;
  pages.forEach((p) => {
    p.setAttribute('role', 'tabpanel');
    p.setAttribute('aria-labelledby', 'sw-tab-' + p.dataset.page);
  });

  tabs.forEach((t, i) => {
    t.addEventListener('click', () => selectPage(t.getAttribute('aria-controls').slice(5), false));
    t.addEventListener('keydown', (e) => {
      let j = -1;
      if (e.key === 'ArrowDown' || e.key === 'ArrowRight') j = (i + 1) % tabs.length;
      else if (e.key === 'ArrowUp' || e.key === 'ArrowLeft') j = (i - 1 + tabs.length) % tabs.length;
      else if (e.key === 'Home') j = 0;
      else if (e.key === 'End') j = tabs.length - 1;
      if (j < 0) return;
      e.preventDefault();
      selectPage(tabs[j].getAttribute('aria-controls').slice(5), true);
    });
  });

  /* ---------- attention ---------- */
  let attOn = null;
  const setAtt = (kind) => {
    if (kind === attOn) return;
    const tile = $('.sp-tile', icons.discover);
    if (attOn) { tile.classList.remove('a-' + attOn, 'a-static'); icons.discover.classList.remove('a-dot'); }
    attOn = kind;
    if (!kind) return;
    if (kind === 'dot') icons.discover.classList.add('a-dot');
    else tile.classList.add('a-' + kind);
    if (reduced) tile.classList.add('a-static');
  };

  /* ---------- previews: raise / close ---------- */
  let curTh = 0;
  const visibleThs = () => ths.map((th, i) => (th.classList.contains('is-gone') ? -1 : i)).filter((i) => i >= 0);
  const setCur = (i) => { curTh = i; ths.forEach((th, j) => th.classList.toggle('is-cur', j === i)); };
  setCur(0);

  ths.forEach((th, i) => {
    $('.sp-th-raise', th).addEventListener('click', () => {
      setCur(i);
      winLabel = 'Konsole ' + (i + 1);
      showTitle(winLabel);
      announce(`Konsole ${i + 1} raised`);
    });
    $('.sp-th-close', th).addEventListener('click', () => {
      th.classList.add('is-gone');
      const left = visibleThs();
      if (!left.length) {
        icons.konsole.classList.add('is-closed');
        reopenBtn.hidden = false;
        winLabel = 'Dolphin';
        showTitle(winLabel);
        announce('All Konsole windows closed');
        reopenBtn.focus();
      } else {
        if (curTh === i) setCur(left[0]);
        if (winLabel === 'Konsole ' + (i + 1)) { winLabel = 'Konsole ' + (curTh + 1); showTitle(winLabel); }
        announce(`Konsole ${i + 1} closed`);
        $('.sp-th-raise', ths[left.find((j) => j > i) ?? left[left.length - 1]]).focus();
      }
      measurePop();
    });
  });

  if (reopenBtn) reopenBtn.addEventListener('click', () => {
    ths.forEach((th) => th.classList.remove('is-gone'));
    icons.konsole.classList.remove('is-closed');
    setCur(0);
    reopenBtn.hidden = true;
    measurePop();
    announce('Konsole windows reopened');
    $('.sp-th-raise', ths[0]).focus();
  });

  /* ---------- keyboard walk (Keyboard first, Meta+F5) ---------- */
  let walkStart = -1;
  let ringOn = null;
  let ringTimer = 0;
  const setRing = (app) => {
    if (ringOn === app) return;
    if (ringOn) icons[ringOn].classList.remove('is-ring');
    ringOn = app;
    if (app) icons[app].classList.add('is-ring');
  };
  const startWalk = (onlyFirst) => {
    window.clearTimeout(ringTimer);
    if (reduced || onlyFirst) {
      walkStart = -1;
      setRing(order[0]);
      ringTimer = window.setTimeout(() => setRing(null), 2500);
    } else {
      walkStart = performance.now();
    }
    request();
  };
  if (walkBtn) walkBtn.addEventListener('click', () => { startWalk(false); announce('Focus moves through the dock icons'); });

  /* ---------- reorder ---------- */
  const syncChips = () => {
    if (!orderList) return;
    const lis = new Map($$('li', orderList).map((li) => [$('button', li).dataset.app, li]));
    order.forEach((app) => orderList.appendChild(lis.get(app)));
  };

  const moveApp = (app, to) => {
    const from = order.indexOf(app);
    to = Math.max(0, Math.min(order.length - 1, to));
    if (from === to) return false;
    order.splice(from, 1);
    order.splice(to, 0, app);
    relayout();
    request();
    return true;
  };

  if (orderList) orderList.addEventListener('keydown', (e) => {
    const btn = e.target.closest('button');
    if (!btn) return;
    const app = btn.dataset.app;
    const i = order.indexOf(app);
    let to = -1;
    if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') to = i - 1;
    else if (e.key === 'ArrowRight' || e.key === 'ArrowDown') to = i + 1;
    else if (e.key === 'Home') to = 0;
    else if (e.key === 'End') to = order.length - 1;
    if (to < 0 || to >= order.length) return;
    e.preventDefault();
    if (moveApp(app, to)) {
      syncChips();
      btn.focus();
      announce(`${NAMES[app]} moved to position ${to + 1} of ${order.length}`);
    }
  });

  /* ---------- shortcuts table ---------- */
  let slot = 0;
  $$('.sw-keys tbody tr').forEach((tr, i) => {
    const th = $('th', tr), td = $('td', tr);
    td.id = 'sw-key-d' + i;
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'sw-key';
    btn.setAttribute('aria-describedby', td.id);
    while (th.firstChild) btn.appendChild(th.firstChild);
    th.appendChild(btn);
    btn.addEventListener('click', () => {
      restart(btn, 'is-hit');
      window.setTimeout(() => btn.classList.remove('is-hit'), 450);
      const k = tr.dataset.key;
      if (k === 'toggle') {
        manualHidden = !manualHidden;
        announce(manualHidden ? 'Dock hidden' : 'Dock shown');
      } else if (k === 'slot' || k === 'new') {
        if (k === 'slot') slot = (slot % order.length) + 1;
        else if (!slot) slot = 1;
        const app = order[slot - 1];
        manualHidden = false;
        if (!reduced) restart($('.sp-tile', icons[app]), 'a-launch');
        winLabel = k === 'slot' ? NAMES[app] : `${NAMES[app]}, new window`;
        showTitle(winLabel);
        announce(k === 'slot' ? `Meta+${slot}: ${NAMES[app]} activated` : `Meta+Shift+${slot}: new ${NAMES[app]} window`);
      } else if (k === 'focus') {
        manualHidden = false;
        startWalk(true);
        announce(`Keyboard focus is on ${NAMES[order[0]]} in the dock`);
      }
      request();
    });
  });
  $$('.sp-tile', dock).forEach((el) => el.addEventListener('animationend', (e) => {
    if (e.animationName === 'sp-bounce' && el.classList.contains('a-launch')) el.classList.remove('a-launch');
  }));

  /* ---------- controls ---------- */

  const controls = $$('.sw-pages input, .sw-pages select');
  controls.forEach(readInput);

  const onControl = (e) => {
    const el = e.target;
    if (!readInput(el)) return;
    const k = el.name;
    syncAttrs();
    if (k === 'size') relayout();
    if (k === 'att' || k === 'stop' || k === 'dnd') { t0 = performance.now(); setAtt(null); }
    if (k === 'preview') measurePop();
    request();
  };
  root.addEventListener('input', onControl);
  root.addEventListener('change', (e) => { if (e.target.type === 'radio' || e.target.tagName === 'SELECT') onControl(e); });

  // Replay the demo moments on demand: a window sliding onto the dock
  // (Behavior) and an app asking for attention (Attention).
  if (coverBtn) coverBtn.addEventListener('click', () => {
    t0 = performance.now() - 3000;
    announce(S.vis === 'always' ? 'A window moves over the dock. Pick Dodge windows to see the dock step aside.' : 'A window moves over the dock.');
    request();
  });
  if (notifyBtn) notifyBtn.addEventListener('click', () => {
    t0 = performance.now();
    setAtt(null);
    announce(S.dnd ? 'Do Not Disturb is on, so the dock stays still.'
      : S.att === 'none' ? 'Attention animation is set to None, so the dock stays still.'
        : 'Discover asks for attention.');
    request();
  });

  /* ---------- pointer on the stage ---------- */
  let ptr = null;
  let overPop = false;
  let drag = null;
  const toDesign = (e) => {
    const r = stage.getBoundingClientRect();
    return { x: (e.clientX - r.left) / K, y: (e.clientY - r.top) / K };
  };
  const inDockZone = (p) => p && p.y > dockTop - b * (S.zoom - 1) - 12 && p.x > rowL - b && p.x < rowR + b;
  const nearestSlot = (x) => {
    let k = 0;
    for (let i = 1; i < slots.length; i++) if (Math.abs(x - slots[i]) < Math.abs(x - slots[k])) k = i;
    return k;
  };

  stage.addEventListener('pointermove', (e) => {
    overPop = !!e.target.closest('.sp-pop');
    if (overPop) { ptr = null; request(); return; }
    ptr = toDesign(e);
    if (drag) {
      drag.x = ptr.x;
      const to = nearestSlot(ptr.x);
      if (moveApp(drag.app, to)) drag.moved = true;
    }
    request();
  }, { passive: true });

  stage.addEventListener('pointerleave', () => { overPop = false; if (!drag) { ptr = null; request(); } }, { passive: true });

  stage.addEventListener('pointerdown', (e) => {
    if (S.page !== 'plasma' || e.button > 0) return;
    const p = toDesign(e);
    if (p.y < dockTop - 6) return;
    const i = nearestSlot(p.x);
    if (Math.abs(p.x - slots[i]) > slotW[i] / 2 + gap) return;
    drag = { app: order[i], x: p.x, moved: false };
    ptr = p;
    icons[drag.app].classList.add('is-drag');
    try { stage.setPointerCapture(e.pointerId); } catch (_) { /* pointer already gone */ }
    request();
  }, { passive: true });

  const endDrag = (e) => {
    if (!drag) return;
    const d = drag;
    drag = null;
    icons[d.app].classList.remove('is-drag');
    if (e && stage.hasPointerCapture && stage.hasPointerCapture(e.pointerId)) stage.releasePointerCapture(e.pointerId);
    if (d.moved) {
      syncChips();
      announce(`${NAMES[d.app]} moved to position ${order.indexOf(d.app) + 1} of ${order.length}`);
    }
    if (e && e.pointerType !== 'mouse') ptr = null;
    request();
  };
  stage.addEventListener('pointerup', endDrag, { passive: true });
  stage.addEventListener('pointercancel', endDrag, { passive: true });

  /* ---------- render ---------- */
  const zoomScale = (d) => {
    const R = b * 2.8;
    if (d >= R) return 1;
    const c = Math.cos((Math.PI * d) / (2 * R));
    return 1 + (S.zoom - 1) * c * c;
  };

  const monState = { focus: -1, docks: '' };

  const render = (now) => {
    const t = (now - t0) / 1000;
    const pg = S.page;
    const iconY = H - 24 - b * 0.55;
    let cur = null;
    let zx = null;
    let winOff = 0;
    let hide = manualHidden;
    let wheel = false;
    let title = winLabel;
    let hoverTh = -1;
    let pillText = '';

    if (pg === 'appearance') {
      zx = reduced ? centers[2] : W / 2 + restW * 0.55 * Math.sin((t * Math.PI * 2) / 6);
      cur = { x: zx, y: iconY };
      winOff = reduced ? 1 : drift(t % 7, 2, 3, 5, 6);
    } else if (pg === 'behavior') {
      const p = reduced ? 5 : t % 8;
      const kc = { x: centers[order.indexOf('konsole')], y: iconY };
      const away = { x: W * 0.66, y: 70 };
      cur = p < 3 ? kc : p < 4 ? lerp(kc, away, ease(p - 3)) : p < 7 ? away : lerp(away, kc, ease(p - 7));
      const atDock = p < 3.3 || p > 7.6;
      if (atDock) zx = cur.x;
      if (!icons.konsole.classList.contains('is-closed') && p < 3) {
        wheel = true;
        const vis = visibleThs();
        title = 'Konsole ' + (vis[Math.min(vis.length - 1, Math.floor(p)) % vis.length] + 1);
      }
      winOff = drift(p, 4, 4.8, 6.2, 7);
      const covered = winOff * (TRAVEL_GAP + OVERLAP) > TRAVEL_GAP + 1;
      if (S.vis === 'autohide' && !atDock) hide = true;
      if (S.vis === 'dodge' && covered && !atDock) hide = true;
    } else if (pg === 'previews') {
      const vis = S.preview ? visibleThs() : [];
      if (vis.length) {
        const p = reduced ? 0 : t % (vis.length * 2);
        hoverTh = vis[Math.floor(p / 2)];
        const pt = thPts[hoverTh];
        if (pt) cur = pt;
      }
    } else if (pg === 'attention') {
      // stop = 0 means no limit, like AttentionAnimationDuration in Krema.
      const endless = S.stop === 0;
      const p = reduced || endless ? 0 : t % (S.stop + 2.5);
      const running = !S.dnd && S.att !== 'none' && (endless || p < S.stop);
      setAtt(running ? S.att : null);
      if (running) {
        pillText = endless ? 'Discover needs you'
          : reduced ? `Discover needs you: stops after ${S.stop} s`
            : `Discover needs you: stops in ${Math.ceil(S.stop - p)} s`;
      }
    } else if (pg === 'screens') {
      let pm, fm, lastP;
      if (reduced) { pm = 1; fm = 0; lastP = true; }
      else {
        const p = t % 10;
        pm = p >= 2 && p < 7 ? 1 : 0;
        fm = p >= 4.5 && p < 8.5 ? 1 : 0;
        lastP = (p >= 2 && p < 4.5) || (p >= 7 && p < 8.5);
        const a = monPts[0], c = monPts[1];
        if (a && c) {
          if (p >= 2 && p < 2.6) cur = lerp(a, c, ease((p - 2) / 0.6));
          else if (p >= 7 && p < 7.6) cur = lerp(c, a, ease((p - 7) / 0.6));
          else cur = pm ? c : a;
        }
      }
      if (reduced) cur = monPts[pm] || null;
      let docks;
      if (S.where === 'primary') docks = '0';
      else if (S.where === 'every') docks = '01';
      else if (S.follow === 'mouse') docks = String(pm);
      else if (S.follow === 'keyboard') docks = String(fm);
      else docks = String(lastP ? pm : fm);
      if (monState.focus !== fm) { monState.focus = fm; mons.forEach((m, i) => m.classList.toggle('is-focus', i === fm)); }
      if (monState.docks !== docks) { monState.docks = docks; mons.forEach((m, i) => m.classList.toggle('has-dock', docks.includes(String(i)))); }
    }

    // Real pointer takes over the zoom and hides the demo cursor.
    if (ptr && pg !== 'screens') {
      cur = null;
      zx = drag ? drag.x : inDockZone(ptr) ? ptr.x : null;
    }

    if (overPop) cur = null;

    // Keyboard walk
    if (walkStart >= 0) {
      const step = Math.floor((now - walkStart) / 450);
      if (step >= order.length) { walkStart = -1; setRing(null); }
      else setRing(order[step]);
    }

    // Dock icons. Big icons at a high zoom factor can make the row wider than
    // the design space; shrink the whole row by f so it fits between EDGE
    // margins, and record where each slot landed.
    const n = order.length;
    const s = new Array(n);
    let total = 2 * pad + (n - 1) * gap;
    for (let i = 0; i < n; i++) {
      s[i] = zx == null ? 1 : zoomScale(Math.abs(zx - centers[i]));
      total += b * s[i];
    }
    const f = Math.min(1, (W - 2 * EDGE) / total);
    const left = (W - total * f) / 2;
    let x = left + pad * f;
    for (let i = 0; i < n; i++) {
      const app = order[i];
      const w = b * s[i] * f;
      slots[i] = x + w / 2;
      slotW[i] = w;
      let tx = x + w / 2 - b / 2;
      let sc = s[i] * f;
      if (drag && drag.app === app) { tx = drag.x - b / 2; sc = Math.max(sc, 1.12 * f); }
      setT(icons[app], `translate3d(${tx.toFixed(2)}px,0,0) scale(${sc.toFixed(4)})`);
      x += w + gap * f;
    }
    rowL = left;
    rowR = left + total * f;
    setT(bar, `translate3d(${left.toFixed(2)}px,0,0) scaleX(${((total * f) / restW).toFixed(4)})`);

    // Window + dock surface
    setT(win, `translate3d(0,${(winOff * (TRAVEL_GAP + OVERLAP)).toFixed(2)}px,0)`);
    setC(dock, 'is-hidden', hide);
    showTitle(title);
    setC($('.sp-wheel', icons.konsole), 'is-on', wheel);
    ths.forEach((th, i) => setC(th, 'is-hover', i === hoverTh && !ptr));

    if (pg !== 'attention' && attOn) setAtt(null);
    setC(pill, 'is-on', !!pillText);
    if (pillText && pill.textContent !== pillText) pill.textContent = pillText;

    // Demo cursor
    setC(cursor, 'is-on', !!cur);
    if (cur) setT(cursor, `translate3d(${(cur.x - 2).toFixed(1)}px,${(cur.y - 2).toFixed(1)}px,0)`);
  };

  /* ---------- loop control ---------- */
  let raf = 0;
  let onScreen = false;
  const animating = () => onScreen && !document.hidden && !reduced && (LOOP.has(S.page) || walkStart >= 0 || !!drag);
  const frame = (now) => {
    raf = 0;
    render(now);
    if (animating()) raf = requestAnimationFrame(frame);
  };
  function request() { if (!raf) raf = requestAnimationFrame(frame); }

  const setPaused = () => root.classList.toggle('is-paused', !onScreen || document.hidden);

  if ('IntersectionObserver' in window) {
    new IntersectionObserver((entries) => {
      onScreen = entries[entries.length - 1].isIntersecting;
      setPaused();
      request();
    }).observe(stage);
  } else onScreen = true;

  document.addEventListener('visibilitychange', () => { setPaused(); request(); });
  const onMotion = () => { reduced = mq.matches; setAtt(null); request(); };
  if (mq.addEventListener) mq.addEventListener('change', onMotion);

  if ('ResizeObserver' in window) new ResizeObserver(fit).observe(stage);
  else window.addEventListener('resize', fit, { passive: true });

  syncAttrs();
  selectPage('appearance', false);
  fit();
  setPaused();
})();
