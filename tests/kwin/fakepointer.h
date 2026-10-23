// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QByteArray>
#include <QPoint>

#include <wayland-client.h>

#include <cstdint>
#include <iterator>

namespace krema::test
{

// org_kde_kwin_fake_input (plasma-wayland-protocols fake-input.xml), declared
// here by hand: only requests up to pointer_motion_absolute (since 3) are sent.
inline const wl_interface *kNoTypes[] = {nullptr, nullptr, nullptr};
inline const wl_message kFakeInputRequests[] = {
    {"authenticate", "ss", kNoTypes},
    {"pointer_motion", "ff", kNoTypes},
    {"button", "uu", kNoTypes},
    {"axis", "uf", kNoTypes},
    {"touch_down", "2uff", kNoTypes},
    {"touch_motion", "2uff", kNoTypes},
    {"touch_up", "2u", kNoTypes},
    {"touch_cancel", "2", kNoTypes},
    {"touch_frame", "2", kNoTypes},
    {"pointer_motion_absolute", "3ff", kNoTypes},
};
inline constexpr int kFakeInputVersion = 3;
inline const wl_interface kFakeInputInterface =
    {"org_kde_kwin_fake_input", kFakeInputVersion, static_cast<int>(std::size(kFakeInputRequests)), kFakeInputRequests, 0, nullptr};
inline constexpr uint32_t kFakeInputAuthenticate = 0;
inline constexpr uint32_t kFakeInputButton = 2;
inline constexpr uint32_t kFakeInputPointerMotionAbsolute = 9;
inline constexpr uint32_t kBtnLeft = 0x110; // BTN_LEFT, linux/input-event-codes.h

/// Real pointer motion and clicks through KWin's normal input pipeline
/// (pointer focus, enter/leave, cursorPosChanged for KWin scripts), on a
/// Wayland connection of its own. run-with-kwin.sh disables KWin's permission
/// checks, so the restricted fake-input global is bindable. The device lives
/// as long as this object; the seat has a pointer only while some device does.
/// Create it before the receiving Qt application initializes its Wayland seat
/// when the compositor otherwise has no pointer device. A roundtrip on this
/// injector connection does not wait for Qt to bind its own wl_pointer or
/// dispatch mouse events; the receiving client must observe the actual input.
class FakePointer
{
public:
    FakePointer(const char *application, const char *reason)
    {
        m_display = wl_display_connect(nullptr);
        if (!m_display) {
            return;
        }
        wl_registry *registry = wl_display_get_registry(m_display);
        static const wl_registry_listener listener{&FakePointer::global, &FakePointer::globalRemove};
        wl_registry_add_listener(registry, &listener, this);
        wl_display_roundtrip(m_display);
        wl_registry_destroy(registry);
        send(kFakeInputAuthenticate, application, reason);
    }
    ~FakePointer()
    {
        if (m_fakeInput) {
            wl_proxy_destroy(m_fakeInput);
        }
        if (m_display) {
            wl_display_disconnect(m_display);
        }
    }
    FakePointer(const FakePointer &) = delete;
    FakePointer &operator=(const FakePointer &) = delete;

    [[nodiscard]] bool isValid() const
    {
        return m_fakeInput != nullptr;
    }
    /// Absolute move in global (compositor) coordinates.
    void moveTo(const QPoint &pos)
    {
        send(kFakeInputPointerMotionAbsolute, wl_fixed_from_int(pos.x()), wl_fixed_from_int(pos.y()));
    }
    void click()
    {
        send(kFakeInputButton, kBtnLeft, uint32_t(WL_POINTER_BUTTON_STATE_PRESSED));
        send(kFakeInputButton, kBtnLeft, uint32_t(WL_POINTER_BUTTON_STATE_RELEASED));
    }

private:
    /// Waits for KWin on this connection, not for another client's event delivery.
    template<typename... Args>
    void send(uint32_t opcode, Args... args)
    {
        if (!m_fakeInput) {
            return;
        }
        wl_proxy_marshal_flags(m_fakeInput, opcode, nullptr, wl_proxy_get_version(m_fakeInput), 0, args...);
        wl_display_roundtrip(m_display);
    }
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
    {
        auto *self = static_cast<FakePointer *>(data);
        if (!self->m_fakeInput && qstrcmp(interface, kFakeInputInterface.name) == 0 && version >= uint32_t(kFakeInputVersion)) {
            self->m_fakeInput = static_cast<wl_proxy *>(wl_registry_bind(registry, name, &kFakeInputInterface, kFakeInputVersion));
        }
    }
    static void globalRemove(void *, wl_registry *, uint32_t)
    {
    }

    wl_display *m_display = nullptr;
    wl_proxy *m_fakeInput = nullptr;
};

} // namespace krema::test
