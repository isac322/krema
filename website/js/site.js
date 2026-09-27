// Krema landing page: JS flag, copy-to-clipboard buttons for install commands,
// and the live GitHub star count on the star CTAs.
(() => {
  "use strict";

  document.documentElement.classList.add("js");

  const status = document.querySelector(".install .status");
  const canCopy = !!(navigator.clipboard && window.isSecureContext);

  document.querySelectorAll(".cmd .copy").forEach((btn) => {
    const code = btn.parentElement.querySelector("code");
    const distro = btn.closest(".inst")?.querySelector("h3")?.textContent.trim() ?? "";
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

  // Star count: one GitHub API request, cached for an hour per tab session.
  // Chips stay hidden unless a real count arrives; the CTAs read fine without one.
  const chips = document.querySelectorAll("[data-stars]");
  if (chips.length) {
    const KEY = "krema:stars";
    const TTL = 60 * 60 * 1000;
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
      cached = JSON.parse(sessionStorage.getItem(KEY) ?? "null");
    } catch {
      cached = null;
    }

    if (cached && Number.isInteger(cached.n) && Date.now() - cached.t < TTL) {
      show(cached.n);
    } else {
      // Lay the chips out invisibly so the arriving count causes no layout shift.
      setState("pending");
      const abort = new AbortController();
      const timer = window.setTimeout(() => abort.abort(), 8000);
      fetch("https://api.github.com/repos/isac322/krema", { signal: abort.signal })
        .then((res) => (res.ok ? res.json() : Promise.reject(new Error(`HTTP ${res.status}`))))
        .then((data) => {
          const n = data.stargazers_count;
          if (!Number.isInteger(n)) throw new Error("no stargazers_count");
          try {
            sessionStorage.setItem(KEY, JSON.stringify({ n, t: Date.now() }));
          } catch {
            // Storage may be unavailable (private mode); the count still shows.
          }
          show(n);
        })
        .catch(() => setState("failed"))
        .finally(() => window.clearTimeout(timer));
    }
  }
})();
