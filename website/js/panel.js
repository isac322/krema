// Section R (#roots): Plasma panel / Krema compare slider.
// - The native range input is the control (mouse, touch, keyboard); the line
//   and grip only mirror its value.
// - Below MIN_W the scene keeps a MIN_W-wide desktop and is scaled down, so
//   the panel never squeezes (a real panel would overflow the same way).
// - Krema's zoom is the real Parabolic layout (origin/master
//   src/utils/zoomcalculator.h computeDockZoom, used from main.qml:878 and
//   applied by DockItem.qml:337-362): a Gaussian scale per icon, neighbours
//   pushed aside so icons stay edge to edge, the background growing by the
//   edge-clamped total growth, eased by a 150 ms OutCubic zoom amount.
// - The Plasma clock shows the visitor's local time in Plasma's en_US format.
(() => {
  "use strict";

  const cmp = document.querySelector("[data-cmp]");
  if (!cmp) return;

  const MIN_W = 920; // tasks end at x 638, the tray starts 280 px from the right
  const SCENE_H = 320;
  const ICON = 48;
  const GAP = 4;
  const PITCH = ICON + GAP;
  const PAD = 8; // dock padding around the icon row (each side)
  const ITEM_H = ICON + 8; // icon + reserved indicator-dot space
  const MAX_ZOOM = 1.6;
  const SIGMA = ICON * 1.2; // kDefaultZoomSigmaFactor, zoomcalculator.h:14
  const ZOOM_DUR = 150; // Kirigami.Units.shortDuration (main.qml:857-861)
  const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;

  const range = cmp.querySelector(".cmp-range");
  const dock = cmp.querySelector("[data-kd]");
  const tip = cmp.querySelector("[data-kd-tip]");
  const items = Array.from(dock.querySelectorAll(".kd-item"));
  const tasks = Array.from(cmp.querySelectorAll(".pp-task"));

  // ---------- Scale ----------
  let k = 1;
  let sceneW = 0;
  // Rest geometry in scene px (all the zoom layout needs; see computeDockZoom).
  let bgX0 = 0; // background rest left edge
  let bgW = 0; // background rest width
  let rowStart = 0; // leading edge of item 0
  let roomL = 0; // slack between the background edges and the scene
  let roomR = 0;
  const itemBottom = () => SCENE_H - 8 - 4; // dock margin + background bottom padding
  const fit = () => {
    const w = cmp.clientWidth;
    sceneW = Math.max(w, MIN_W);
    k = w / sceneW;
    bgW = items.length * PITCH - GAP + 2 * PAD;
    bgX0 = (sceneW - bgW) / 2;
    rowStart = bgX0 + PAD;
    roomL = bgX0;
    roomR = bgX0;
    cmp.style.setProperty("--k", k.toFixed(5));
    cmp.style.setProperty("--sw", String(sceneW));
    cmp.style.setProperty("--sh", String(SCENE_H));
    cmp.classList.add("is-scaled");
  };
  fit();
  new ResizeObserver(() => { fit(); schedule(); }).observe(cmp);

  // ---------- Slider ----------
  const words = (v) => {
    if (v <= 0) return "All Krema";
    if (v >= 100) return "All Plasma panel";
    if (v === 50) return "Half Plasma panel, half Krema";
    return `${v}% Plasma panel, ${100 - v}% Krema`;
  };
  const setPos = () => {
    const v = Number(range.value);
    cmp.style.setProperty("--pos", `${v}%`);
    range.setAttribute("aria-valuetext", words(v));
  };
  range.addEventListener("input", setPos);
  // Start just left of the dock so both sides read at a glance.
  range.value = String(Math.max(20, Math.min(50, Math.floor((bgX0 / sceneW) * 100) - 4)));
  setPos();

  // ---------- Pointer: Krema zoom and Plasma hover ----------
  // Task buttons start 190 px into the panel (panel at x 8), 54 px apart.
  const TASK_X = 8 + 190;
  const TASK_PITCH = 54;
  const ppTip = cmp.querySelector("[data-pp-tip]");
  let over = false; // pointer in the dock's input zone, on the Krema side
  let zoomActive = false; // main.qml _zoomActive: set by an icon hit, kept until the pointer leaves the zone
  let hoverTask = null;
  let frame = 0;

  const setHoverTask = (t) => {
    if (t === hoverTask) return;
    hoverTask?.classList.remove("is-hover");
    hoverTask = t;
    if (!t) { ppTip.classList.remove("is-on"); return; }
    t.classList.add("is-hover");
    ppTip.firstElementChild.textContent = t.dataset.name;
    ppTip.lastElementChild.textContent = t.dataset.sub;
    const c = TASK_X + tasks.indexOf(t) * TASK_PITCH + 26;
    ppTip.style.translate = `${Math.round(c - ppTip.offsetWidth / 2)}px 0`;
    ppTip.classList.add("is-on");
  };

  // No Math.erf in JS: Abramowitz-Stegun 7.1.26 (|error| <= 1.5e-7); only x >= 0
  // is ever passed.
  const erf = (x) => {
    const t = 1 / (1 + 0.3275911 * x);
    return 1 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * Math.exp(-x * x);
  };

  // Port of krema::computeDockZoom (src/utils/zoomcalculator.h:99-189), Parabolic
  // only. Returns per-item scales, per-item centre offsets in the scene's rest
  // frame, and the leading/trailing growth of the dock background.
  const layout = (cursor, M) => {
    const n = items.length;
    const scales = new Array(n);
    const offs = new Array(n);
    let growth = 0;
    for (let i = 0; i < n; i++) {
      const d = rowStart + i * PITCH + ICON / 2 - cursor;
      scales[i] = 1 + (M - 1) * Math.exp(-(d * d) / (SIGMA * SIGMA));
      growth += ICON * (scales[i] - 1);
    }
    // Zoomed icons stay edge to edge; growth splits between the ends in
    // proportion to how much of the bump lies over the row on each side.
    const rowA = rowStart - GAP / 2;
    const rowB = rowA + n * PITCH;
    const before = erf(Math.max(0, cursor - rowA) / SIGMA);
    const after = erf(Math.max(0, rowB - cursor) / SIGMA);
    const share = before + after > 0 ? before / (before + after) : 0.5;
    const fitted = Math.min(growth, roomL + roomR);
    const leading = Math.min(Math.max(fitted * share, fitted - roomR), roomL);
    const posShare = growth > 0 ? fitted / growth : 0;
    let edge = rowStart - leading;
    for (let i = 0; i < n; i++) {
      const w = ICON + ICON * (scales[i] - 1) * posShare;
      offs[i] = edge + w / 2 - (rowStart + i * PITCH + ICON / 2);
      edge += w + GAP;
    }
    return { scales, offs, leading, trailing: fitted - leading, fitted };
  };

  // Global zoom amount eased like dockPanel.zoomAmount (main.qml:856-864).
  let cursorX = 0; // last valid primary-axis cursor, kept on exit (zoomCursor)
  let cursorY = null;
  let zoomAmt = 0;
  let zoomGoal = 0;
  let zoomFrom = 0;
  let zoomT0 = 0;
  const setZoom = (on) => {
    const goal = on ? 1 : 0;
    if (goal === zoomGoal) return;
    zoomGoal = goal;
    zoomFrom = zoomAmt;
    zoomT0 = performance.now();
    if (reduced) { zoomAmt = goal; zoomFrom = goal; }
    schedule();
  };

  const render = () => {
    frame = 0;
    if (zoomAmt !== zoomGoal) {
      const t = Math.min(1, (performance.now() - zoomT0) / ZOOM_DUR);
      zoomAmt = zoomFrom + (zoomGoal - zoomFrom) * (1 - (1 - t) ** 3);
      if (t < 1) schedule();
      else zoomAmt = zoomGoal;
    }
    const L = layout(cursorX, 1 + (MAX_ZOOM - 1) * zoomAmt);
    // dockBackground grows by the clamped growth on each side (main.qml:738-755).
    dock.style.translate = "0 0";
    dock.style.left = `${(bgX0 - L.leading).toFixed(2)}px`;
    dock.style.width = `${(bgW + L.fitted).toFixed(2)}px`;
    // Items: Scale about the bottom centre, then Translate along the dock
    // (DockItem.qml transform list). The dock's left edge moved by -leading, so
    // each item's translate is leading + offset.
    let slot = -1;
    let best = Infinity;
    items.forEach((li, i) => {
      const s = L.scales[i];
      li.style.translate = `${(L.leading + L.offs[i]).toFixed(2)}px 0`;
      li.style.scale = s === 1 ? "" : s.toFixed(4);
      li.style.zIndex = s === 1 ? "" : String(Math.round(s * 100));
      const dist = Math.abs(cursorX - (rowStart + i * PITCH + ICON / 2 + L.offs[i]));
      const half = (ICON * s) / 2 + GAP / 2;
      if (dist <= half && dist < best) { slot = i; best = dist; }
    });
    // Tooltip on the item whose VISIBLE slot holds the pointer on both axes
    // (updateHoveredItem, main.qml:395-423).
    let hovered = -1;
    if (over && slot >= 0) {
      const s = L.scales[slot];
      if (cursorY >= itemBottom() - ITEM_H * s && cursorY <= itemBottom()) hovered = slot;
    }
    if (hovered >= 0 && !zoomActive) { zoomActive = true; setZoom(true); }
    if (hovered >= 0) {
      tip.textContent = items[hovered].dataset.name;
      tip.style.translate = `${(rowStart + hovered * PITCH + ICON / 2 + L.offs[hovered] - tip.offsetWidth / 2).toFixed(1)}px 0`;
      tip.classList.add("is-on");
    } else tip.classList.remove("is-on");
  };
  const schedule = () => { if (!frame) frame = requestAnimationFrame(render); };

  let rect = null;
  // Scene origin: inside .cmp's border (the scene is scaled from its top left).
  const measure = () => {
    const r = cmp.getBoundingClientRect();
    rect = { left: r.left + cmp.clientLeft, top: r.top + cmp.clientTop };
  };
  cmp.addEventListener("pointerenter", measure, { passive: true });
  cmp.addEventListener("pointermove", (e) => {
    if (e.pointerType !== "mouse") return;
    if (!rect) measure();
    const x = (e.clientX - rect.left) / k;
    const y = (e.clientY - rect.top) / k;
    const split = (sceneW * Number(range.value)) / 100;

    // Krema: the zone is the panel height plus the zoom extension above it
    // (onPositionChanged, main.qml:640-651); zoom starts on an icon hit and
    // lasts until the pointer leaves the zone. Cursor x is kept on exit so the
    // zoom-out collapses around it (zoomCursor, main.qml:840-846).
    const panelTop = SCENE_H - 8 - 64;
    const inZone = x >= split && y >= panelTop - ICON * (MAX_ZOOM - 1) && y <= SCENE_H - 8;
    if (inZone) { cursorX = x; cursorY = y; over = true; schedule(); }
    else if (over) { over = false; cursorY = null; zoomActive = false; setZoom(false); schedule(); }

    // Plasma: highlight the task button under the pointer, on the panel side.
    let t = null;
    if (x < split && y > SCENE_H - 8 - 46) {
      const off = x - TASK_X;
      const i = Math.floor(off / TASK_PITCH);
      if (i >= 0 && i < tasks.length && off - i * TASK_PITCH < TASK_PITCH - 2) t = tasks[i];
    }
    setHoverTask(t);
  }, { passive: true });
  cmp.addEventListener("pointerleave", () => {
    rect = null;
    if (over) { over = false; cursorY = null; zoomActive = false; setZoom(false); schedule(); }
    setHoverTask(null);
  }, { passive: true });
  window.addEventListener("scroll", () => { rect = null; }, { passive: true });

  // ---------- Plasma clock ----------
  const clock = cmp.querySelector("[data-clock]");
  if (clock && !clock.hasAttribute("data-static")) {
    const time = clock.querySelector(".pp-time");
    const date = clock.querySelector(".pp-date");
    const tf = new Intl.DateTimeFormat("en-US", { hour: "numeric", minute: "2-digit" });
    const df = new Intl.DateTimeFormat("en-US", { month: "numeric", day: "numeric", year: "2-digit" });
    const tick = () => {
      const now = new Date();
      time.textContent = tf.format(now).replace(/\u202f/g, " ");
      date.textContent = df.format(now);
      setTimeout(tick, 60000 - (now.getSeconds() * 1000 + now.getMilliseconds()) + 50);
    };
    tick();
  }
})();
