// Krema landing page: parabolic-zoom dock demo and copy-to-clipboard buttons.
(() => {
  "use strict";

  const MAX_ZOOM = 1.75;

  const stage = document.querySelector(".stage");
  const desk = stage && stage.querySelector(".desk");
  const dock = stage && stage.querySelector(".dock");
  const tip = stage && stage.querySelector(".dock-tip");

  if (dock && desk && tip) {
    const tiles = Array.from(dock.querySelectorAll(".tile"));
    const n = tiles.length;
    const reduced = window.matchMedia("(prefers-reduced-motion: reduce)");
    let metrics = null;
    let pointerX = null; // pointer position relative to the desk center, px
    let focusIndex = -1;
    let frame = 0;
    let intro = 0;

    const syncMotion = () => stage.classList.toggle("no-motion", reduced.matches);
    syncMotion();
    reduced.addEventListener("change", syncMotion);

    const measure = () => {
      const cs = getComputedStyle(stage);
      const tile = parseFloat(cs.getPropertyValue("--tile")) || 32;
      const gap = parseFloat(cs.getPropertyValue("--gap")) || 5;
      const padY = parseFloat(cs.getPropertyValue("--pad-y")) || 6;
      // Falloff radius in tile pitches; narrower on small screens so the zoomed dock still fits.
      const reach = parseFloat(cs.getPropertyValue("--reach")) || 3;
      metrics = { tile, gap, padX: padY + 3, pitch: tile + gap, reach };
    };
    measure();

    // Resting center of tile i relative to the dock center. The dock stays
    // centered while it grows, so these anchors never move under the pointer.
    const restCenter = (i) => (i - (n - 1) / 2) * metrics.pitch;

    const scaleAt = (x, i) => {
      const d = Math.abs(x - restCenter(i));
      const radius = metrics.reach * metrics.pitch;
      if (d >= radius) return 1;
      const c = Math.cos((Math.PI / 2) * (d / radius));
      return 1 + (MAX_ZOOM - 1) * c * c;
    };

    const render = (x) => {
      if (x === null) {
        for (const t of tiles) t.style.setProperty("--s", "1");
        tip.classList.remove("is-on");
        return;
      }
      const { tile, gap, padX } = metrics;
      const scales = tiles.map((_, i) => scaleAt(x, i));
      let peak = 0;
      scales.forEach((s, i) => {
        tiles[i].style.setProperty("--s", s.toFixed(4));
        if (s > scales[peak]) peak = i;
      });

      // Place the label from the target layout so it does not trail the transition.
      const total = scales.reduce((sum, s) => sum + s * tile, 0) + gap * (n - 1) + padX * 2;
      let left = desk.clientWidth / 2 - total / 2 + padX;
      for (let i = 0; i < peak; i++) left += scales[i] * tile + gap;
      const center = left + (scales[peak] * tile) / 2;
      const deskRect = desk.getBoundingClientRect();
      const faceBottom = tiles[peak].querySelector(".face").getBoundingClientRect().bottom - deskRect.top;
      const top = faceBottom - scales[peak] * tile - tip.offsetHeight - 12;

      tip.textContent = tiles[peak].dataset.name;
      tip.style.left = `${center}px`;
      tip.style.top = `${top}px`;
      tip.classList.add("is-on");
    };

    const target = () => {
      if (pointerX !== null) return pointerX;
      if (focusIndex >= 0) return restCenter(focusIndex);
      return null;
    };

    const schedule = (x = target()) => {
      cancelAnimationFrame(frame);
      frame = requestAnimationFrame(() => render(x));
    };

    const stopIntro = () => {
      if (intro) cancelAnimationFrame(intro);
      intro = 0;
    };

    const relX = (e) => {
      const r = desk.getBoundingClientRect();
      return e.clientX - (r.left + r.width / 2);
    };

    dock.addEventListener("pointermove", (e) => {
      stopIntro();
      pointerX = relX(e);
      schedule();
    });
    dock.addEventListener("pointerdown", (e) => {
      stopIntro();
      pointerX = relX(e);
      schedule();
    });
    const leave = (e) => {
      if (e.pointerType !== "mouse" && e.type === "pointerleave" && e.buttons) return;
      pointerX = null;
      schedule();
    };
    dock.addEventListener("pointerleave", leave);
    dock.addEventListener("pointercancel", () => {
      pointerX = null;
      schedule();
    });
    dock.addEventListener("pointerup", (e) => {
      if (e.pointerType !== "mouse") {
        pointerX = null;
        schedule();
      }
    });

    // Roving tabindex: one tab stop, arrows move between tiles.
    tiles.forEach((t, i) => t.setAttribute("tabindex", i === 0 ? "0" : "-1"));
    const focusTile = (i) => {
      const next = (i + n) % n;
      tiles.forEach((t, j) => t.setAttribute("tabindex", j === next ? "0" : "-1"));
      tiles[next].focus();
    };
    dock.addEventListener("keydown", (e) => {
      const i = tiles.indexOf(document.activeElement);
      if (i < 0) return;
      const moves = { ArrowRight: i + 1, ArrowDown: i + 1, ArrowLeft: i - 1, ArrowUp: i - 1, Home: 0, End: n - 1 };
      if (e.key in moves) {
        e.preventDefault();
        focusTile(moves[e.key]);
      }
    });
    dock.addEventListener("focusin", (e) => {
      const i = tiles.indexOf(e.target);
      if (i < 0) return;
      stopIntro();
      focusIndex = i;
      tiles.forEach((t, j) => t.setAttribute("tabindex", j === i ? "0" : "-1"));
      if (e.target.matches(":focus-visible")) schedule();
      else focusIndex = -1;
    });
    dock.addEventListener("focusout", (e) => {
      if (!dock.contains(e.relatedTarget)) {
        focusIndex = -1;
        schedule();
      }
    });

    const describe = (t) => {
      const parts = [t.dataset.name];
      if (t.classList.contains("is-running")) parts.push("running");
      if (t.classList.contains("is-active")) parts.push("active");
      const badge = t.querySelector(".badge");
      if (badge) parts.push(`${badge.textContent} unread`);
      t.setAttribute("aria-label", parts.join(", "));
    };

    const activate = (t) => {
      tiles.forEach((o) => {
        if (o !== t && o.classList.contains("is-active")) {
          o.classList.remove("is-active");
          describe(o);
        }
      });
      t.classList.add("is-active");
      const badge = t.querySelector(".badge");
      if (badge) badge.remove();
      describe(t);
    };

    tiles.forEach((t) => {
      t.addEventListener("click", () => {
        if (t.classList.contains("is-running")) {
          activate(t);
          return;
        }
        if (reduced.matches) {
          t.classList.add("is-running");
          activate(t);
          return;
        }
        if (t.classList.contains("is-launching")) return;
        t.classList.add("is-launching");
        t.querySelector(".face").addEventListener(
          "animationend",
          () => {
            t.classList.remove("is-launching");
            t.classList.add("is-running");
            activate(t);
          },
          { once: true },
        );
      });
    });

    window.addEventListener("resize", () => {
      measure();
      schedule();
    });

    // One unprompted pass of the pointer so the zoom reads before anyone touches it.
    const playIntro = () => {
      if (reduced.matches || pointerX !== null || focusIndex >= 0) return;
      const span = ((n - 1) / 2 + 1) * metrics.pitch;
      const duration = 1900;
      let start = 0;
      const ease = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);
      const step = (now) => {
        if (!start) start = now;
        const p = Math.min((now - start) / duration, 1);
        render(-span + 2 * span * ease(p));
        if (p < 1) intro = requestAnimationFrame(step);
        else {
          intro = 0;
          render(target());
        }
      };
      intro = requestAnimationFrame(step);
    };
    window.setTimeout(playIntro, 700);
  }

  // Copy buttons for install commands.
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
