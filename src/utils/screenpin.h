// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QScreen>
#include <QString>
#include <QWindow>

namespace krema
{

/// Pin a layer-shell window to the output named by the KREMA_SCREEN environment
/// variable (e.g. KREMA_SCREEN=DP-1). No-op when the variable is unset or no
/// output matches.
///
/// Escape hatch until per-screen configuration lands: without it the dock lands
/// on Qt's primaryScreen(), which on Wayland is merely the first announced
/// wl_output — on multi-monitor setups this is often not the screen the user
/// considers primary (the compositor's primary output is not exposed to Qt).
///
/// Both the QWindow screen and the LayerShellQt screen must be set, and
/// wantsToBeOnActiveScreen disabled — otherwise the compositor is free to pick
/// another output for the surface.
inline void applyScreenPinFromEnv(QWindow *window, LayerShellQt::Window *layerWindow)
{
    if (!window || !layerWindow) {
        return;
    }
    const QString wantedScreen = qEnvironmentVariable("KREMA_SCREEN");
    if (wantedScreen.isEmpty()) {
        return;
    }
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (screen->name() == wantedScreen) {
            window->setScreen(screen);
            layerWindow->setWantsToBeOnActiveScreen(false);
            layerWindow->setScreen(screen);
            return;
        }
    }
}

}
