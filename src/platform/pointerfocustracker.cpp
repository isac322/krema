// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "pointerfocustracker.h"

#include <wayland-client.h>

#include <algorithm>
#include <string_view>

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>

namespace krema
{

namespace
{

void ignoreMotion(void *, wl_pointer *, uint32_t, wl_fixed_t, wl_fixed_t)
{
}

void ignoreButton(void *, wl_pointer *, uint32_t, uint32_t, uint32_t, uint32_t)
{
}

void ignoreAxis(void *, wl_pointer *, uint32_t, uint32_t, wl_fixed_t)
{
}

void ignoreFrame(void *, wl_pointer *)
{
}

void ignoreAxisSource(void *, wl_pointer *, uint32_t)
{
}

void ignoreAxisStop(void *, wl_pointer *, uint32_t, uint32_t)
{
}

void ignoreAxisDiscrete(void *, wl_pointer *, uint32_t, int32_t)
{
}

#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
void ignoreAxisValue120(void *, wl_pointer *, uint32_t, int32_t)
{
}
#endif

#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
void ignoreAxisRelativeDirection(void *, wl_pointer *, uint32_t, uint32_t)
{
}
#endif

#ifdef WL_POINTER_WARP_SINCE_VERSION
void ignoreWarp(void *, wl_pointer *, wl_fixed_t, wl_fixed_t)
{
}
#endif

void ignoreSeatName(void *, wl_seat *, const char *)
{
}

// wl_seat.release (and wl_pointer.release) need version 5; nothing newer is
// used, so the pointer never receives events from later versions either.
constexpr uint32_t s_seatVersion = 5;

} // namespace

PointerFocusTracker::PointerFocusTracker(QWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    auto *waylandApp = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    if (!waylandApp || !waylandApp->display()) {
        return;
    }
    static const wl_registry_listener listener = {
        .global = &PointerFocusTracker::handleGlobal,
        .global_remove = &PointerFocusTracker::handleGlobalRemove,
    };
    // A registry and seat of our own: Qt's seat proxy already has Qt's
    // listener, and Qt exposes no signal for its capability changes. The
    // registry inherits the display's default queue, which Qt dispatches on
    // the GUI thread; so do the seat and pointer created from it.
    m_registry = wl_display_get_registry(waylandApp->display());
    if (m_registry) {
        wl_registry_add_listener(m_registry, &listener, this);
    }
}

PointerFocusTracker::~PointerFocusTracker()
{
    releaseSeat();
    if (m_registry) {
        wl_registry_destroy(m_registry);
    }
}

void PointerFocusTracker::bindSeat(uint32_t name, uint32_t version)
{
    if (m_seat) {
        return;
    }
    static const wl_seat_listener listener = {
        .capabilities = &PointerFocusTracker::handleCapabilities,
        .name = ignoreSeatName,
    };
    m_seat = static_cast<wl_seat *>(wl_registry_bind(m_registry, name, &wl_seat_interface, std::min(version, s_seatVersion)));
    if (m_seat) {
        m_seatName = name;
        wl_seat_add_listener(m_seat, &listener, this);
    }
}

void PointerFocusTracker::releaseSeat()
{
    releasePointer();
    if (!m_seat) {
        return;
    }
    if (wl_proxy_get_version(reinterpret_cast<wl_proxy *>(m_seat)) >= WL_SEAT_RELEASE_SINCE_VERSION) {
        wl_seat_release(m_seat);
    } else {
        wl_seat_destroy(m_seat);
    }
    m_seat = nullptr;
    m_seatName = 0;
}

void PointerFocusTracker::setCapabilities(uint32_t capabilities)
{
    if (!(capabilities & WL_SEAT_CAPABILITY_POINTER)) {
        // The protocol makes the old pointer defunct; a regained capability
        // needs a new one.
        releasePointer();
        setFocusedSurface(nullptr);
        return;
    }
    if (m_pointer) {
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

    // If the pointer already lies on one of our surfaces, the compositor
    // sends the new pointer an enter right away.
    m_pointer = wl_seat_get_pointer(m_seat);
    if (m_pointer) {
        wl_pointer_add_listener(m_pointer, &listener, this);
    }
}

void PointerFocusTracker::releasePointer()
{
    if (!m_pointer) {
        return;
    }
    if (wl_proxy_get_version(reinterpret_cast<wl_proxy *>(m_pointer)) >= WL_POINTER_RELEASE_SINCE_VERSION) {
        wl_pointer_release(m_pointer);
    } else {
        wl_pointer_destroy(m_pointer);
    }
    m_pointer = nullptr;
    m_focusedSurface = nullptr;
}

bool PointerFocusTracker::isPointerInside() const
{
    if (!m_pointer || !m_focusedSurface || !m_window) {
        return false;
    }
    auto *nativeInterface = QGuiApplication::platformNativeInterface();
    if (!nativeInterface) {
        return false;
    }
    // Look the surface up each time: QtWayland recreates it when the window
    // is hidden and shown again.
    auto *surface = static_cast<wl_surface *>(nativeInterface->nativeResourceForWindow(QByteArrayLiteral("surface"), m_window.data()));
    return surface && surface == m_focusedSurface;
}

void PointerFocusTracker::setFocusedSurface(wl_surface *surface)
{
    m_focusedSurface = surface;
    // An enter on another surface of this client (e.g. the preview popup)
    // also leaves the window, so compare the derived state, not the surface.
    const bool inside = isPointerInside();
    if (inside != m_lastInside) {
        m_lastInside = inside;
        Q_EMIT pointerInsideChanged();
    }
}

void PointerFocusTracker::handleGlobal(void *data, wl_registry *, uint32_t name, const char *interface, uint32_t version)
{
    if (std::string_view(interface) == wl_seat_interface.name) {
        static_cast<PointerFocusTracker *>(data)->bindSeat(name, version);
    }
}

void PointerFocusTracker::handleGlobalRemove(void *data, wl_registry *, uint32_t name)
{
    auto *self = static_cast<PointerFocusTracker *>(data);
    if (self->m_seat && name == self->m_seatName) {
        self->releaseSeat();
        self->setFocusedSurface(nullptr);
    }
}

void PointerFocusTracker::handleCapabilities(void *data, wl_seat *, uint32_t capabilities)
{
    static_cast<PointerFocusTracker *>(data)->setCapabilities(capabilities);
}

void PointerFocusTracker::handleEnter(void *data, wl_pointer *, uint32_t, wl_surface *surface, int32_t, int32_t)
{
    static_cast<PointerFocusTracker *>(data)->setFocusedSurface(surface);
}

void PointerFocusTracker::handleLeave(void *data, wl_pointer *, uint32_t, wl_surface *)
{
    // Pointer focus is a single surface, so any leave ends it.
    static_cast<PointerFocusTracker *>(data)->setFocusedSurface(nullptr);
}

} // namespace krema
