// Star nudges: every gentle ask that points at the GitHub star.
//   Header pill fills after the hero + star tile in the page dock (live badge, one bounce)
//   + contextual toast after real interest (clips picked, zoom played with)
//   + real social proof (recent stargazers, goal to the next round number)
//   + copy-triggered bottom banner, idle wiggle on the star tile (max 3)
// Runs before pagedock.js so elements it creates with data-dodge are seen by the dock.
(() => {
  "use strict";
  const REPO = "isac322/krema";
  const SNOOZE_DAYS = 14;

  const reduced = () => window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const $ = (s, r = document) => r.querySelector(s);

  // ---------- Storage ----------
  const LS = "krema:star:";
  const get = (store, k) => { try { return store.getItem(LS + k); } catch { return null; } };
  const set = (store, k, v) => { try { store.setItem(LS + k, v); } catch { /* private mode */ } };
  const local = window.localStorage;
  const session = window.sessionStorage;

  const clicked = () => get(local, "clicked") === "1";
  const snoozed = (k) => Number(get(local, `${k}-until`) || 0) > Date.now();
  const snooze = (k) => set(local, `${k}-until`, String(Date.now() + SNOOZE_DAYS * 864e5));
  // At most one pop-up ask per visit, whichever fires first.
  const askedThisVisit = () => get(session, "asked") === "1";
  const markAsked = () => set(session, "asked", "1");

  // Visiting GitHub from any star CTA ends every nudge on this browser.
  // (We cannot know whether a star was actually given, so nothing claims it was.)
  document.addEventListener("click", (e) => {
    if (e.target.closest("[data-star-link], .star-btn")) set(local, "clicked", "1");
  });

  // ---------- Live count: reuse site.js's single request ----------
  const countReady = new Promise((resolve) => {
    const chip = $("[data-stars]");
    if (!chip) return resolve(null);
    const read = () => {
      try {
        const c = JSON.parse(sessionStorage.getItem("krema:stars") || "null");
        return c && Number.isInteger(c.n) ? c.n : null;
      } catch { return null; }
    };
    // site.js runs first: state is "pending" or "ready", or the chip is hidden on failure.
    const check = () => {
      if (chip.dataset.state === "ready") { resolve(read()); return true; }
      if (chip.hidden && !chip.dataset.state) { resolve(null); return true; }
      return false;
    };
    if (check()) return;
    const mo = new MutationObserver(() => { if (check()) mo.disconnect(); });
    mo.observe(chip, { attributes: true, attributeFilter: ["data-state", "hidden"] });
    window.setTimeout(() => { mo.disconnect(); resolve(read()); }, 10000);
  });

  // ---------- Live region for asks that appear without focus ----------
  const live = document.createElement("p");
  live.className = "status";
  live.setAttribute("role", "status");
  live.setAttribute("aria-live", "polite");
  document.body.append(live);
  const say = (t) => { live.textContent = ""; window.setTimeout(() => { live.textContent = t; }, 60); };

  // ---------- Header pill fills once the hero is behind you ----------
  const pill = $(".wn-star");
  const hero = $("#top");
  if (pill && hero && "IntersectionObserver" in window) {
    new IntersectionObserver(([en]) => pill.classList.toggle("is-hot", !en.isIntersecting), { threshold: 0 }).observe(hero);
  }

  // ---------- Star tile bounces once when the real count lands ----------
  const starTile = $(".pd-face--star");
  const dock = $("[data-pagedock]");
  const hop = (face, frames, ms) => (face && face.animate && !reduced() ? face.animate(frames, { duration: ms }) : null);
  const bounceFrames = (h) => {
    const up = "cubic-bezier(.2,.7,.3,1)";
    const down = "cubic-bezier(.6,0,.8,.4)";
    return [
      { translate: "0 0", easing: up },
      { translate: `0 -${h}px`, offset: 0.25, easing: down },
      { translate: "0 0", offset: 0.5, easing: up },
      { translate: `0 -${Math.round(h * 0.6)}px`, offset: 0.75, easing: down },
      { translate: "0 0" },
    ];
  };
  const tileH = () => (starTile ? starTile.getBoundingClientRect().height || 56 : 56);
  const whenDockShown = (fn) => {
    if (!dock || !dock.classList.contains("is-hidden")) return fn();
    const mo = new MutationObserver(() => {
      if (!dock.classList.contains("is-hidden")) { mo.disconnect(); fn(); }
    });
    mo.observe(dock, { attributes: true, attributeFilter: ["class"] });
  };
  countReady.then((n) => {
    if (n === null || !starTile || get(session, "bounced") === "1") return;
    window.setTimeout(() => whenDockShown(() => {
      set(session, "bounced", "1");
      hop(starTile, bounceFrames(Math.round(tileH() * 0.3)), 720);
    }), 1400);
  });

  // ---------- Contextual toast after real interest ----------
  const toast = document.createElement("div");
  toast.className = "star-toast is-out";
  toast.hidden = true;
  toast.setAttribute("data-dodge", "");
  toast.setAttribute("role", "dialog");
  toast.setAttribute("aria-modal", "false");
  toast.setAttribute("aria-labelledby", "star-toast-t");
  toast.setAttribute("aria-describedby", "star-toast-b");
  toast.innerHTML =
    '<span class="star-toast-ico" aria-hidden="true"><svg><use href="#i-star"/></svg></span>' +
    '<div><p class="star-toast-t" id="star-toast-t"></p><p class="star-toast-b" id="star-toast-b"></p></div>' +
    '<button type="button" class="star-close" aria-label="Dismiss">&times;</button>' +
    '<div class="star-toast-btns">' +
    `<a class="star-btn" href="https://github.com/${REPO}"><svg aria-hidden="true"><use href="#i-star"/></svg>Star on GitHub</a>` +
    '<button type="button" class="star-quiet">Not now, still deciding</button>' +
    "</div>";
  document.body.append(toast);

  const COPY = {
    zoom: ["Enjoying the zoom?", "A star on GitHub helps other Plasma users find Krema."],
    clips: ["Liked what you saw?", "A star on GitHub helps other Plasma users find Krema. It takes one click."],
  };

  // Overlays appear without taking focus; if the user tabbed into one and
  // dismisses it, focus goes back to whatever was focused when it opened.
  const restoreFocus = (overlay, backTo) => {
    if (!overlay.contains(document.activeElement)) return;
    if (backTo && backTo.isConnected && backTo !== document.body) backTo.focus();
    else document.activeElement.blur();
  };
  let toastOn = false;
  let toastReturnFocus = null;
  const hideToast = () => {
    if (!toastOn) return;
    toastOn = false;
    toast.classList.add("is-out");
    window.setTimeout(() => { toast.hidden = true; }, reduced() ? 0 : 380);
    restoreFocus(toast, toastReturnFocus);
    toastReturnFocus = null;
  };
  const showToast = (why) => {
    if (toastOn || clicked() || snoozed("toast") || askedThisVisit()) return false;
    markAsked();
    toastReturnFocus = document.activeElement;
    const [t, b] = COPY[why] || COPY.clips;
    $("#star-toast-t").textContent = t;
    $("#star-toast-b").textContent = b;
    toast.hidden = false;
    void toast.offsetWidth;
    toast.classList.remove("is-out");
    toastOn = true;
    say(`${t} ${b} Star on GitHub, or Not now.`);
    return true;
  };
  toast.querySelector(".star-quiet").addEventListener("click", () => { snooze("toast"); hideToast(); });
  toast.querySelector(".star-close").addEventListener("click", () => { snooze("toast"); hideToast(); });
  toast.querySelector(".star-btn").addEventListener("click", () => hideToast());
  toast.addEventListener("keydown", (e) => { if (e.key === "Escape") { snooze("toast"); hideToast(); } });

  // Trigger 1: three different clips picked by hand in the exhibit dock.
  const exDock = $(".ex-dock");
  let lastInput = 0;
  const picked = new Set();
  let pending = 0;
  const fireSoon = (why) => {
    // Wait for a pause so the toast never lands under a moving pointer.
    window.clearTimeout(pending);
    pending = window.setTimeout(() => showToast(why), 1200);
  };
  if (exDock) {
    const mark = () => { lastInput = Date.now(); };
    exDock.addEventListener("pointerdown", mark);
    exDock.addEventListener("keydown", mark);
    new MutationObserver((list) => {
      for (const m of list) {
        const el = m.target;
        if (el.getAttribute("aria-selected") === "true" && Date.now() - lastInput < 900) {
          picked.add(el);
          if (picked.size >= 3) fireSoon("clips");
        }
      }
    }).observe(exDock, { subtree: true, attributes: true, attributeFilter: ["aria-selected"] });
  }

  // Trigger 2: about four seconds of actually driving a zoom (hero letters,
  // the Roots comparison, or the exhibit dock).
  const zoomZones = [".lens", "[data-cmp]", ".ex-dock"].map((s) => $(s)).filter(Boolean);
  let zoomMs = 0;
  let lastMove = 0;
  for (const z of zoomZones) {
    z.addEventListener("pointermove", () => {
      const now = performance.now();
      const dt = now - lastMove;
      lastMove = now;
      if (dt < 120) zoomMs += dt;
      if (zoomMs > 4000) fireSoon("zoom");
    }, { passive: true });
  }

  // ---------- Real social proof ----------
  const proof = $("[data-star-proof]");
  const heroChip = $(".star-proof-chip");
  const STEPS = [10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000];
  const nextGoal = (n) => STEPS.find((s) => s > n) || Math.ceil((n + 1) / 10000) * 10000;
  const ago = (iso) => {
    const d = Math.floor((Date.now() - Date.parse(iso)) / 864e5);
    if (!Number.isFinite(d)) return "";
    if (d <= 0) return "today";
    if (d === 1) return "yesterday";
    if (d < 45) return `${d} days ago`;
    const mo = Math.round(d / 30);
    return mo < 18 ? `${mo} months ago` : `${Math.round(d / 365)} years ago`;
  };

  // Newest stargazers. GitHub lists oldest first, so read the last page.
  const recent = (n) => {
    const KEY = "krema:star:recent";
    try {
      const c = JSON.parse(sessionStorage.getItem(KEY) || "null");
      if (c && Date.now() - c.t < 36e5) return Promise.resolve(c.list);
    } catch { /* ignore */ }
    const page = Math.max(1, Math.ceil(n / 100));
    const ctl = new AbortController();
    const timer = window.setTimeout(() => ctl.abort(), 8000);
    return fetch(`https://api.github.com/repos/${REPO}/stargazers?per_page=100&page=${page}`, {
      headers: { Accept: "application/vnd.github.star+json" },
      signal: ctl.signal,
    })
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error(`HTTP ${r.status}`))))
      .then((rows) => {
        const list = rows
          .filter((r) => r && r.user && r.user.login)
          .slice(-5)
          .reverse()
          .map((r) => ({ login: r.user.login, avatar: r.user.avatar_url, url: r.user.html_url, at: r.starred_at }));
        try { sessionStorage.setItem(KEY, JSON.stringify({ t: Date.now(), list })); } catch { /* ignore */ }
        return list;
      })
      .catch(() => [])
      .finally(() => window.clearTimeout(timer));
  };

  if (proof) {
    const line = $("[data-star-line]", proof);
    const faces = $("[data-star-faces]", proof);
    const goal = $("[data-star-goal]", proof);
    const fill = $("[data-star-fill]", proof);
    const goalN = $("[data-star-goal-n]", proof);
    countReady.then((n) => {
      if (n === null) {
        // No real number: no numbers at all, just the plain ask.
        line.textContent = "Still looking for a Latte Dock successor? A star is how you vote for one on GitHub.";
        goal.hidden = true;
        goalN.hidden = true;
        return;
      }
      const g = nextGoal(n);
      goal.setAttribute("aria-label", `${n} of ${g} stars`);
      goalN.innerHTML = `<strong>${n} of ${g}</strong> stars. ${g - n} more reach the next round number.`;
      window.requestAnimationFrame(() => { fill.style.transform = `scaleX(${Math.min(1, n / g).toFixed(3)})`; });
      if (heroChip) {
        heroChip.querySelector("[data-star-hero-n]").textContent = `Starred by ${n} ${n === 1 ? "person" : "people"} on GitHub`;
        heroChip.hidden = false;
      }
      const noun = (k) => (k === 1 ? "other" : "others");
      line.innerHTML = `<strong>${n}</strong> ${n === 1 ? "person has" : "people have"} starred Krema. Add yours as a vote for a Latte&nbsp;Dock successor on Plasma&nbsp;6.`;
      recent(n).then((list) => {
        if (!list.length) return;
        faces.innerHTML = "";
        for (const u of list) {
          const a = document.createElement("a");
          a.href = u.url;
          a.setAttribute("data-dodge", "");
          const img = document.createElement("img");
          img.src = `${u.avatar}${u.avatar.includes("?") ? "&" : "?"}s=72`;
          img.alt = u.login;
          img.width = 36;
          img.height = 36;
          img.loading = "lazy";
          img.referrerPolicy = "no-referrer";
          img.addEventListener("error", () => a.remove());
          a.append(img);
          faces.append(a);
        }
        const newest = list[0];
        const when = newest.at ? ` The latest star came ${ago(newest.at)}.` : "";
        line.innerHTML =
          `<strong>${newest.login}</strong>${n > 1 ? ` and ${n - 1} ${noun(n - 1)}` : ""} starred Krema.${when} Add yours as a vote for a Latte&nbsp;Dock successor on Plasma&nbsp;6.`;
      });
    });
  }

  // ---------- Copy-triggered banner ----------
  const banner = $("[data-star-banner]");
  let bannerOn = false;
  let bannerReturnFocus = null;
  const hideBanner = () => {
    if (!bannerOn) return;
    bannerOn = false;
    banner.classList.add("is-out");
    window.setTimeout(() => { banner.hidden = true; }, reduced() ? 0 : 400);
    restoreFocus(banner, bannerReturnFocus);
    bannerReturnFocus = null;
  };
  const showBanner = () => {
    if (!banner || bannerOn || clicked() || snoozed("banner") || askedThisVisit()) return false;
    hideToast();
    markAsked();
    bannerReturnFocus = document.activeElement;
    banner.classList.add("is-out");
    banner.hidden = false;
    void banner.offsetWidth;
    banner.classList.remove("is-out");
    bannerOn = true;
    say("You copied the install command. One more click: star Krema on GitHub. Or choose Not now.");
    return true;
  };
  if (banner) {
    $("[data-star-later]", banner).addEventListener("click", () => { snooze("banner"); hideBanner(); });
    $("[data-star-close]", banner).addEventListener("click", () => { snooze("banner"); hideBanner(); });
    $(".star-btn", banner).addEventListener("click", () => hideBanner());
    banner.addEventListener("keydown", (e) => { if (e.key === "Escape") { snooze("banner"); hideBanner(); } });
    // Only a copy that actually succeeded counts (site.js adds .is-done).
    document.querySelectorAll(".cmd .copy").forEach((btn) => {
      new MutationObserver(() => {
        if (btn.classList.contains("is-done")) window.setTimeout(showBanner, 700);
      }).observe(btn, { attributes: true, attributeFilter: ["class"] });
    });
  }

  // ---------- Idle attention wiggle on the star tile ----------
  const IDLE_MS = 30000;
  const MAX_WIGGLES = 3;
  let idleTimer = 0;
  const wiggles = () => Number(get(session, "wiggles") || 0);
  const wiggle = () => {
    if (!starTile || reduced()) return false;
    starTile.animate(
      [
        { rotate: "0deg", translate: "0 0" },
        { rotate: "-9deg", translate: "0 -3px", offset: 0.15 },
        { rotate: "8deg", translate: "0 -5px", offset: 0.32 },
        { rotate: "-6deg", translate: "0 -3px", offset: 0.5 },
        { rotate: "4deg", translate: "0 -1px", offset: 0.68 },
        { rotate: "0deg", translate: "0 0" },
      ],
      { duration: 700, easing: "ease-in-out" },
    );
    return true;
  };
  const armIdle = () => {
    window.clearTimeout(idleTimer);
    if (wiggles() >= MAX_WIGGLES || clicked() || reduced()) return;
    idleTimer = window.setTimeout(() => {
      if (document.hidden || bannerOn || toastOn || (dock && dock.classList.contains("is-hidden"))) { armIdle(); return; }
      if (wiggle()) set(session, "wiggles", String(wiggles() + 1));
      armIdle();
    }, IDLE_MS);
  };
  for (const ev of ["pointermove", "pointerdown", "keydown", "scroll", "wheel", "touchstart"]) {
    window.addEventListener(ev, armIdle, { passive: true });
  }
  armIdle();
})();
