/* Wordnav: scroll-driven zoom wordmark navigation.
   All layout is measured up front (and on resize / content changes); the scroll
   path only reads window.scrollY inside one rAF and writes transforms. */
(function () {
  "use strict";

  var root = document.querySelector("[data-wordnav]");
  if (!root) return;

  var nav = root.querySelector("nav");
  var list = root.querySelector(".wn-letters");
  var dot = root.querySelector(".wn-dot");
  var links = Array.prototype.slice.call(root.querySelectorAll(".wn-l"));
  if (!nav || !list || !links.length) return;

  var items = links.map(function (a) { return a.closest(".wn-item") || a.parentNode; });
  var sections = links.map(function (a) { return document.getElementById(a.dataset.section); });
  if (sections.some(function (s) { return !s; })) return;

  root.querySelectorAll(".wn-cap").forEach(function (c) { c.setAttribute("aria-hidden", "true"); });

  var N = links.length;
  var GAP = 2;            // keep in sync with .wn-letters gap in css/wordnav.css
  var READ_LINE = 0.35;   // reading line, fraction of viewport height from the top
  var SHOW_AT = 0.4;      // bar appears after scrolling past 40% of the hero
  var PEAK = 1.16, NEAR = 0.87, FAR = 0.72;

  // Measured state.
  var widths = new Array(N);
  var restCentres = new Array(N);
  var restTotal = 0;
  var listLeft = 0;
  var tops = new Array(N);
  var docEnd = 0;          // max scrollY
  var viewH = 0;
  var showY = 0;

  // Frame state.
  var needMeasure = true;
  var queued = false;
  var lastP = -1;
  var lastW = -1;
  var active = -1;
  var shown = null;

  function cos2(t) { var c = Math.cos(t * Math.PI / 2); return c * c; }

  // Zoom curve in index units: 0 -> 1.16, 1 -> 0.87, >= 2 -> 0.72, cos² eased between.
  function zoom(d) {
    d = Math.abs(d);
    if (d >= 2) return FAR;
    if (d >= 1) return FAR + (NEAR - FAR) * cos2(d - 1);
    return NEAR + (PEAK - NEAR) * cos2(d);
  }

  function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

  function measure() {
    var sy = window.scrollY;
    viewH = window.innerHeight;
    docEnd = Math.max(0, document.documentElement.scrollHeight - viewH);

    for (var i = 0; i < N; i++) tops[i] = sections[i].getBoundingClientRect().top + sy;
    var hero = sections[0].getBoundingClientRect();
    showY = hero.top + sy + hero.height * SHOW_AT;

    listLeft = list.offsetLeft;
    var x = 0;
    for (var j = 0; j < N; j++) {
      widths[j] = links[j].offsetWidth; // layout width, unaffected by transforms
      restCentres[j] = x + widths[j] / 2;
      x += widths[j] + GAP;
    }
    restTotal = x - GAP;
    lastP = -1;
    needMeasure = false;
  }

  // Continuous section position. Inside section i, p runs i-0.5 -> i+0.5, so the
  // peak glides between letters across boundaries while round(p) == i matches the section.
  function position(y) {
    var line = y + viewH * READ_LINE;
    var p = 0;
    if (line >= tops[0]) {
      var i = N - 1;
      while (i > 0 && line < tops[i]) i--;
      var end = i < N - 1 ? tops[i + 1] : Math.max(tops[i] + 1, docEnd + viewH);
      var span = Math.max(1, end - tops[i]);
      p = i + (line - tops[i]) / span - 0.5;
    }
    p = clamp(p, 0, N - 1);
    // A short last section may never reach the reading line; converge on it over the final half viewport.
    var tail = viewH * 0.5;
    if (docEnd > 0 && tail > 0) {
      var t = clamp((y - (docEnd - tail)) / tail, 0, 1);
      p += (N - 1 - p) * t * t * (3 - 2 * t);
    }
    return p;
  }

  function setActive(k) {
    if (k === active) return;
    if (active >= 0) {
      links[active].classList.remove("is-active");
      links[active].removeAttribute("aria-current");
    }
    links[k].classList.add("is-active");
    links[k].setAttribute("aria-current", "true");
    active = k;
  }

  function frame() {
    queued = false;
    if (needMeasure) measure();
    var y = window.scrollY;

    var isShown = y >= showY;
    if (isShown !== shown) {
      root.classList.toggle("is-shown", isShown);
      shown = isShown;
    }

    var p = position(y);
    var k = Math.round(p);
    setActive(k);

    if (Math.abs(p - lastP) < 0.0005 && lastW === restTotal) return;
    lastP = p;
    lastW = restTotal;

    // Reflow: lay the scaled letters out edge to edge, centred on the rest strip.
    var scales = new Array(N);
    var total = GAP * (N - 1);
    for (var i = 0; i < N; i++) {
      scales[i] = zoom(i - p);
      total += widths[i] * scales[i];
    }
    var x = (restTotal - total) / 2;
    var dotX = 0;
    for (var j = 0; j < N; j++) {
      var w = widths[j] * scales[j];
      var c = x + w / 2;
      items[j].style.transform = "translateX(" + (c - restCentres[j]).toFixed(2) + "px)";
      links[j].style.transform = "scale(" + scales[j].toFixed(4) + ")";
      if (j === k) dotX = c;
      x += w + GAP;
    }
    if (dot) dot.style.transform = "translateX(" + (listLeft + dotX - 3).toFixed(2) + "px)";
  }

  function schedule() {
    if (queued) return;
    queued = true;
    requestAnimationFrame(frame);
  }

  function remeasure() {
    needMeasure = true;
    schedule();
  }

  window.addEventListener("scroll", schedule, { passive: true });
  window.addEventListener("resize", remeasure, { passive: true });
  window.addEventListener("load", remeasure);

  var main = document.getElementById("main") || document.querySelector("main");
  if (main) {
    // Late images change section offsets; load doesn't bubble, so capture it.
    main.addEventListener("load", function (e) {
      if (e.target && e.target.tagName === "IMG") remeasure();
    }, true);
    if ("ResizeObserver" in window) new ResizeObserver(remeasure).observe(main);
  }
  if ("ResizeObserver" in window) new ResizeObserver(remeasure).observe(list);
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(remeasure);

  schedule();
})();
