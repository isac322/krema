// Moments: screenshots as a Krema window-preview popup + lightbox dialog.
(() => {
  "use strict";

  const root = document.querySelector("[data-pv]");
  if (!root) return;

  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  // Entrance: the popup scales out of the tile once, when the section first shows.
  if (!reduced && "IntersectionObserver" in window) {
    root.classList.add("pv-armed");
    const io = new IntersectionObserver(
      (entries) => {
        if (!entries.some((e) => e.isIntersecting)) return;
        io.disconnect();
        // Let the armed (scaled-down) state paint before transitioning.
        requestAnimationFrame(() => root.classList.add("is-open"));
      },
      { threshold: 0.25 }
    );
    io.observe(root);
  }

  // Lightbox.
  const section = root.closest("section") || document;
  const dialog = section.querySelector(".pv-dialog");
  if (!dialog || typeof dialog.showModal !== "function") return;

  const dImg = dialog.querySelector("img");
  const dCap = dialog.querySelector(".pv-dcap");
  const closeBtn = dialog.querySelector(".pv-close");
  let opener = null;

  const open = (btn) => {
    const img = btn.querySelector("img");
    if (!img || !dImg) return;
    const cap = btn.closest("figure")?.querySelector(".pv-cap");
    const w = img.getAttribute("width");
    const h = img.getAttribute("height");
    if (w && h) {
      dImg.width = +w;
      dImg.height = +h;
    }
    dImg.src = img.currentSrc || img.src;
    dImg.alt = img.alt;
    if (dCap) dCap.textContent = cap ? cap.textContent.trim() : "";
    opener = btn;
    dialog.showModal();
    closeBtn?.focus();
  };

  root.addEventListener("click", (e) => {
    const btn = e.target.closest(".pv-open");
    if (btn && root.contains(btn)) open(btn);
  });

  closeBtn?.addEventListener("click", () => dialog.close());

  // Backdrop click: the event targets the dialog itself but lands outside its box.
  dialog.addEventListener("click", (e) => {
    if (e.target !== dialog) return;
    const r = dialog.getBoundingClientRect();
    const inside =
      e.clientX >= r.left && e.clientX <= r.right && e.clientY >= r.top && e.clientY <= r.bottom;
    if (!inside) dialog.close();
  });

  // Esc is handled natively (cancel -> close); every close path restores focus.
  dialog.addEventListener("close", () => {
    if (opener && document.contains(opener)) opener.focus();
    opener = null;
  });
})();
