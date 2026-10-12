// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>

#include <cstdint>

struct wl_pointer;
struct wl_registry;
struct wl_seat;
struct wl_surface;

namespace krema
{

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
 * This tracker binds its own wl_seat from its own registry and keeps its own
 * wl_pointer for as long as that seat has the pointer capability: created
 * when the capability appears (also after Krema started without a pointer
 * device) and released when it goes away. The proxies live on the display's
 * default queue, so callbacks run on the GUI thread through Qt's event loop.
 * It records only the compositor's real enter/leave events and emits
 * pointerInsideChanged() when they change whether the pointer is on the
 * window. KWin advertises one seat; the first advertised seat is tracked.
 * Without Wayland, a seat or a pointer it reports the pointer as outside.
 */
class PointerFocusTracker : public QObject
{
    Q_OBJECT

public:
    explicit PointerFocusTracker(QWindow *window, QObject *parent = nullptr);
    ~PointerFocusTracker() override;

    PointerFocusTracker(const PointerFocusTracker &) = delete;
    PointerFocusTracker &operator=(const PointerFocusTracker &) = delete;

    /// True when the compositor's pointer focus is the window's current surface.
    [[nodiscard]] bool isPointerInside() const;

Q_SIGNALS:
    /// Emitted when a compositor enter/leave changes isPointerInside().
    void pointerInsideChanged();

private:
    void bindSeat(uint32_t name, uint32_t version);
    void releaseSeat();
    /// Create or release the wl_pointer to follow the seat's capabilities.
    void setCapabilities(uint32_t capabilities);
    void releasePointer();

    /// Store the new focus and emit pointerInsideChanged() if the result of
    /// isPointerInside() changed.
    void setFocusedSurface(wl_surface *surface);

    static void handleGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
    static void handleGlobalRemove(void *data, wl_registry *registry, uint32_t name);
    static void handleCapabilities(void *data, wl_seat *seat, uint32_t capabilities);
    static void handleEnter(void *data, wl_pointer *pointer, uint32_t serial, wl_surface *surface, int32_t x, int32_t y);
    static void handleLeave(void *data, wl_pointer *pointer, uint32_t serial, wl_surface *surface);

    QPointer<QWindow> m_window;
    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    uint32_t m_seatName = 0;
    wl_pointer *m_pointer = nullptr;
    // Compared by address only; never dereferenced (it may be stale after
    // the window's surface is recreated).
    wl_surface *m_focusedSurface = nullptr;
    bool m_lastInside = false;
};

} // namespace krema
