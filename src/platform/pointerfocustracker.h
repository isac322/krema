// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QPointer>
#include <QWindow>

#include <cstdint>

struct wl_pointer;
struct wl_surface;

namespace krema {

/**
 * Authoritative Wayland pointer focus for one window.
 *
 * Qt's hover state cannot answer "is the pointer over this window" after a
 * popup grab: when a grabbing popup (e.g. a QMenu) opens, QtWayland
 * synthesizes a leave event for the parent window although the compositor
 * keeps pointer focus on it, and when the popup closes it synthesizes the
 * matching enter only for xdg-shell parents. A layer-shell parent such as
 * the dock therefore looks "outside" until the next real motion.
 *
 * This tracker binds its own wl_pointer on Qt's seat (on Qt's default event
 * queue, so callbacks run on the GUI thread) and records only the
 * compositor's real enter/leave events. Without a Wayland seat or pointer
 * it reports the pointer as outside.
 */
class PointerFocusTracker {
public:
  explicit PointerFocusTracker(QWindow *window);
  ~PointerFocusTracker();

  PointerFocusTracker(const PointerFocusTracker &) = delete;
  PointerFocusTracker &operator=(const PointerFocusTracker &) = delete;

  /// True when the compositor's pointer focus is the window's current surface.
  [[nodiscard]] bool isPointerInside();

private:
  /// Create the wl_pointer once Qt's seat has the pointer capability.
  void ensurePointer();

  static void handleEnter(void *data, wl_pointer *pointer, uint32_t serial,
                          wl_surface *surface, int32_t x, int32_t y);
  static void handleLeave(void *data, wl_pointer *pointer, uint32_t serial,
                          wl_surface *surface);

  QPointer<QWindow> m_window;
  wl_pointer *m_pointer = nullptr;
  // Compared by address only; never dereferenced (it may be stale after
  // the window's surface is recreated).
  wl_surface *m_focusedSurface = nullptr;
};

} // namespace krema
