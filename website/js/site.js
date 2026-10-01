// Krema landing page: JS flag, install distro tabs, copy-to-clipboard buttons
// for install commands, and the live GitHub star count on the star CTAs.
(() => {
  "use strict";

  document.documentElement.classList.add("js");

  const status = document.querySelector(".install .status");
  const canCopy = !!(navigator.clipboard && window.isSecureContext);

  // Distro picker: without JS every panel shows, stacked. With JS one panel
  // shows at a time behind a tablist with roving focus.
  document.querySelectorAll("[data-install]").forEach((root) => {
    const list = root.querySelector('[role="tablist"]');
    const tabs = Array.from(root.querySelectorAll('[role="tab"]'));
    const panels = tabs.map((t) => document.getElementById(t.getAttribute("aria-controls")));
    if (!list || panels.some((p) => !p)) return;
    const select = (i, focus) => {
      tabs.forEach((t, k) => {
        const on = k === i;
        t.setAttribute("aria-selected", on ? "true" : "false");
        t.tabIndex = on ? 0 : -1;
        panels[k].hidden = !on;
      });
      if (focus) tabs[i].focus();
    };
    tabs.forEach((t, i) => {
      t.addEventListener("click", () => select(i, false));
      t.addEventListener("keydown", (e) => {
        let n = -1;
        if (e.key === "ArrowRight" || e.key === "ArrowDown") n = (i + 1) % tabs.length;
        else if (e.key === "ArrowLeft" || e.key === "ArrowUp") n = (i - 1 + tabs.length) % tabs.length;
        else if (e.key === "Home") n = 0;
        else if (e.key === "End") n = tabs.length - 1;
        if (n < 0) return;
        e.preventDefault();
        select(n, true);
      });
    });
    list.hidden = false;
    select(Math.max(0, tabs.findIndex((t) => t.getAttribute("aria-selected") === "true")), false);
  });

  document.querySelectorAll(".cmd .copy").forEach((btn) => {
    const code = btn.parentElement.querySelector("code");
    const panel = btn.closest('[role="tabpanel"]');
    const tab = panel && document.getElementById(panel.getAttribute("aria-labelledby"));
    const distro = tab?.textContent.trim() ?? "";
    const label = btn.querySelector("span");
    const idle = `Copy the ${distro} install command`;
    let timer = 0;

    btn.hidden = false;
    btn.setAttribute("aria-label", idle);

    const reset = () => {
      btn.classList.remove("is-done", "is-failed");
      label.textContent = "Copy";
      btn.setAttribute("aria-label", idle);
    };

    const selectCode = () => {
      const range = document.createRange();
      range.selectNodeContents(code);
      const sel = window.getSelection();
      sel.removeAllRanges();
      sel.addRange(range);
    };

    btn.addEventListener("click", async () => {
      clearTimeout(timer);
      try {
        if (!canCopy) throw new Error("clipboard unavailable");
        await navigator.clipboard.writeText(code.textContent);
        btn.classList.add("is-done");
        label.textContent = "Copied";
        if (status) status.textContent = `${distro} command copied to the clipboard.`;
      } catch {
        selectCode();
        btn.classList.add("is-failed");
        label.textContent = "Press Ctrl+C";
        if (status) status.textContent = "Copy was blocked. The command is selected; press Ctrl+C to copy it.";
      }
      timer = window.setTimeout(reset, 2200);
    });
  });

  // Star count: at most one GitHub API request per hour per browser. The count
  // is kept in localStorage; a stale count is shown while a refresh is due or
  // when the request fails. Unauthenticated API calls share a 60/hour limit per
  // IP, so after a 403/429 no request is made until the limit resets.
  // Chips stay hidden unless a count (live or cached) exists.
  const chips = document.querySelectorAll("[data-stars]");
  if (chips.length) {
    const KEY = "krema:stars";
    const FRESH = 60 * 60 * 1000;
    const RETRY = 10 * 60 * 1000;
    const compact = new Intl.NumberFormat("en", { notation: "compact", maximumFractionDigits: 1 });

    const setState = (state) => {
      chips.forEach((chip) => {
        chip.hidden = state === "failed";
        if (state === "failed") delete chip.dataset.state;
        else chip.dataset.state = state;
      });
    };

    const show = (count) => {
      const text = compact.format(count).toLowerCase();
      chips.forEach((chip) => {
        (chip.querySelector("[data-stars-n]") ?? chip).textContent = text;
      });
      setState("ready");
      const noun = count === 1 ? "star" : "stars";
      document.querySelectorAll("[data-star-link]").forEach((link) => {
        link.setAttribute("aria-label", `Star Krema on GitHub (${count.toLocaleString("en")} ${noun})`);
      });
    };

    let cached = null;
    try {
      cached = JSON.parse(localStorage.getItem(KEY) ?? "null");
    } catch {
      cached = null;
    }
    const save = (value) => {
      try {
        localStorage.setItem(KEY, JSON.stringify(value));
      } catch {
        // Storage may be unavailable (private mode); the count still shows.
      }
    };
    // js/star.js reads the cached count once this is set: no request pending.
    const settle = () => chips.forEach((chip) => { chip.dataset.settled = ""; });
    const hasCount = cached && Number.isInteger(cached.n);
    const now = Date.now();

    if (hasCount && now - cached.t < FRESH) {
      show(cached.n);
      settle();
    } else if (cached && cached.retryAt > now) {
      if (hasCount) show(cached.n);
      else setState("failed");
      settle();
    } else {
      // Lay the chips out invisibly so the arriving count causes no layout shift.
      if (hasCount) show(cached.n);
      else setState("pending");
      const abort = new AbortController();
      const timer = window.setTimeout(() => abort.abort(), 8000);
      fetch("https://api.github.com/repos/isac322/krema", { signal: abort.signal })
        .then((res) => {
          if (res.ok) return res.json();
          const reset = Number(res.headers.get("x-ratelimit-reset")) * 1000;
          const limited = (res.status === 403 || res.status === 429) && reset > Date.now();
          return Promise.reject({ retryAt: limited ? reset : Date.now() + RETRY });
        })
        .then((data) => {
          const n = data.stargazers_count;
          if (!Number.isInteger(n)) throw new Error("no stargazers_count");
          save({ n, t: Date.now() });
          show(n);
        })
        .catch((err) => {
          save({ ...(hasCount ? cached : {}), retryAt: err && err.retryAt ? err.retryAt : Date.now() + RETRY });
          if (!hasCount) setState("failed");
        })
        .finally(() => {
          window.clearTimeout(timer);
          settle();
        });
    }
  }
})();
