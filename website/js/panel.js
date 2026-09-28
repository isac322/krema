// Section R (#roots): Plasma panel / Krema compare slider.
// - The native range input is the control (mouse, touch, keyboard); the line
//   and grip only mirror its value.
// - Below MIN_W the scene keeps a MIN_W-wide desktop and is scaled down, so
//   the panel never squeezes (a real panel would overflow the same way).
// - Krema's zoom follows the pointer like the real dock (DockItem.qml): each
//   icon scales from its bottom centre by 1 + (max - 1) * exp(-d² / σ²),
//   σ = 1.2 × icon size, without moving its neighbours.
// - The Plasma clock shows the visitor's local time in Plasma's en_US format.
(() => {
  "use strict";

  const cmp = document.querySelector("[data-cmp]");
  if (!cmp) return;

  const MIN_W = 920; // tasks end at x 638, the tray starts 280 px from the right
  const SCENE_H = 320;
  const ICON = 48;
  const PITCH = ICON + 4;
  const MAX_ZOOM = 1.6;
  const SIGMA2 = (ICON * 1.2) ** 2;

  const range = cmp.querySelector(".cmp-range");
  const dock = cmp.querySelector("[data-kd]");
  const tip = cmp.querySelector("[data-kd-tip]");
  const items = Array.from(dock.querySelectorAll(".kd-item"));
  const tasks = Array.from(cmp.querySelectorAll(".pp-task"));

  // ---------- Scale ----------
  let k = 1;
  let sceneW = 0;
  const fit = () => {
    const w = cmp.clientWidth;
    sceneW = Math.max(w, MIN_W);
    k = w / sceneW;
    cmp.style.setProperty("--k", k.toFixed(5));
    cmp.style.setProperty("--sw", String(sceneW));
    cmp.style.setProperty("--sh", String(SCENE_H));
    cmp.classList.add("is-scaled");
  };
  fit();
  new ResizeObserver(fit).observe(cmp);

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
  const dockLeft = (sceneW - (items.length * PITCH - 4 + 16)) / 2;
  range.value = String(Math.max(20, Math.min(50, Math.floor((dockLeft / sceneW) * 100) - 4)));
  setPos();

  // ---------- Pointer: Krema zoom and Plasma hover ----------
  // Task buttons start 190 px into the panel (panel at x 8), 54 px apart.
  const TASK_X = 8 + 190;
  const TASK_PITCH = 54;
  const ppTip = cmp.querySelector("[data-pp-tip]");
  let px = null; // pointer x in scene px, null when off the dock
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

  const render = () => {
    frame = 0;
    const row = dock.offsetLeft - dock.offsetWidth / 2 + 8; // .kd is translated -50%
    let peak = -1;
    let peakS = 1;
    items.forEach((li, i) => {
      let s = 1;
      if (px !== null) {
        const d = px - (row + i * PITCH + ICON / 2);
        s = 1 + (MAX_ZOOM - 1) * Math.exp(-(d * d) / SIGMA2);
        if (s < 1.002) s = 1;
      }
      li.style.scale = s === 1 ? "" : s.toFixed(4);
      li.style.zIndex = s === 1 ? "" : String(Math.round(s * 100)); // bigger icons draw on top
      if (px !== null && s > peakS) { peakS = s; peak = i; }
    });
    if (peak >= 0) {
      tip.textContent = items[peak].dataset.name;
      tip.style.translate = `${(row + peak * PITCH + ICON / 2 - tip.offsetWidth / 2).toFixed(1)}px 0`;
      tip.classList.add("is-on");
    } else tip.classList.remove("is-on");
  };
  const schedule = () => { if (!frame) frame = requestAnimationFrame(render); };

  let rect = null;
  const measure = () => { rect = cmp.getBoundingClientRect(); };
  cmp.addEventListener("pointerenter", measure, { passive: true });
  cmp.addEventListener("pointermove", (e) => {
    if (e.pointerType !== "mouse") return;
    if (!rect) measure();
    const x = (e.clientX - rect.left) / k;
    const y = (e.clientY - rect.top) / k;
    const split = (sceneW * Number(range.value)) / 100;
    const nearEdge = y > SCENE_H - 8 - 64 - 4;

    // Krema: zoom while the pointer is over the dock, on the Krema side.
    const dl = dock.offsetLeft - dock.offsetWidth / 2;
    const onDock = x >= split && nearEdge && x >= dl && x <= dl + dock.offsetWidth;
    const next = onDock ? x : null;
    if (next !== px) { px = next; schedule(); }

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
    if (px !== null) { px = null; schedule(); }
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
