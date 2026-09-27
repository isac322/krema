// Krema landing page: JS flag plus copy-to-clipboard buttons for install commands.
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
})();
