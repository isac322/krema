// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "pointerfocustracker.h"

#include <wayland-client.h>

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>

namespace krema {

namespace {

void ignoreMotion(void *, wl_pointer *, uint32_t, wl_fixed_t, wl_fixed_t) {}

void ignoreButton(void *, wl_pointer *, uint32_t, uint32_t, uint32_t,
                  uint32_t) {}

void ignoreAxis(void *, wl_pointer *, uint32_t, uint32_t, wl_fixed_t) {}

void ignoreFrame(void *, wl_pointer *) {}

void ignoreAxisSource(void *, wl_pointer *, uint32_t) {}

void ignoreAxisStop(void *, wl_pointer *, uint32_t, uint32_t) {}

void ignoreAxisDiscrete(void *, wl_pointer *, uint32_t, int32_t) {}

#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
void ignoreAxisValue120(void *, wl_pointer *, uint32_t, int32_t) {}
#endif

#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
void ignoreAxisRelativeDirection(void *, wl_pointer *, uint32_t, uint32_t) {}
#endif

#ifdef WL_POINTER_WARP_SINCE_VERSION
void ignoreWarp(void *, wl_pointer *, wl_fixed_t, wl_fixed_t) {}
#endif

} // namespace

PointerFocusTracker::PointerFocusTracker(QWindow *window) : m_window(window) {
  ensurePointer();
}

PointerFocusTracker::~PointerFocusTracker() {
  if (!m_pointer) {
    return;
  }
  if (wl_proxy_get_version(reinterpret_cast<wl_proxy *>(m_pointer)) >=
      WL_POINTER_RELEASE_SINCE_VERSION) {
    wl_pointer_release(m_pointer);
  } else {
    wl_pointer_destroy(m_pointer);
  }
}

void PointerFocusTracker::ensurePointer() {
  if (m_pointer || !qGuiApp) {
    return;
  }
  auto *waylandApp =
      qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
  // Qt creates its own wl_pointer only once the seat advertises the pointer
  // capability; requesting one before that is a protocol error.
  if (!waylandApp || !waylandApp->seat() || !waylandApp->pointer()) {
    return;
  }

  // Every member is set: libwayland calls listener slots without null checks
  // for any event the bound version can send.
  static const wl_pointer_listener listener = {
      .enter = &PointerFocusTracker::handleEnter,
      .leave = &PointerFocusTracker::handleLeave,
      .motion = ignoreMotion,
      .button = ignoreButton,
      .axis = ignoreAxis,
      .frame = ignoreFrame,
      .axis_source = ignoreAxisSource,
      .axis_stop = ignoreAxisStop,
      .axis_discrete = ignoreAxisDiscrete,
#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
      .axis_value120 = ignoreAxisValue120,
#endif
#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
      .axis_relative_direction = ignoreAxisRelativeDirection,
#endif
#ifdef WL_POINTER_WARP_SINCE_VERSION
      .warp = ignoreWarp,
#endif
  };

  // The new proxy inherits the seat's queue (Qt's default queue), so the
  // callbacks are dispatched on the GUI thread by Qt's event loop.
  m_pointer = wl_seat_get_pointer(waylandApp->seat());
  if (m_pointer) {
    wl_pointer_add_listener(m_pointer, &listener, this);
  }
}

bool PointerFocusTracker::isPointerInside() {
  ensurePointer();
  if (!m_pointer || !m_focusedSurface || !m_window) {
    return false;
  }
  auto *nativeInterface = QGuiApplication::platformNativeInterface();
  if (!nativeInterface) {
    return false;
  }
  // Look the surface up each time: QtWayland recreates it when the window
  // is hidden and shown again.
  auto *surface =
      static_cast<wl_surface *>(nativeInterface->nativeResourceForWindow(
          QByteArrayLiteral("surface"), m_window.data()));
  return surface && surface == m_focusedSurface;
}

void PointerFocusTracker::handleEnter(void *data, wl_pointer *, uint32_t,
                                      wl_surface *surface, int32_t, int32_t) {
  static_cast<PointerFocusTracker *>(data)->m_focusedSurface = surface;
}

void PointerFocusTracker::handleLeave(void *data, wl_pointer *, uint32_t,
                                      wl_surface *) {
  // Pointer focus is a single surface, so any leave ends it.
  static_cast<PointerFocusTracker *>(data)->m_focusedSurface = nullptr;
}

} // namespace krema
