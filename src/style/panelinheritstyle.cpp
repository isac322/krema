// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "panelinheritstyle.h"

#include <KWindowEffects>

#include <QGuiApplication>
#include <QPalette>

namespace krema
{

PanelInheritStyle::PanelInheritStyle()
{
    // Read panel background color from the system palette.
    // On Plasma, QPalette::Window gives the panel/widget background color
    // which follows the global color scheme (light/dark).
    const QPalette palette = QGuiApplication::palette();
    m_panelColor = palette.color(QPalette::Window);
    m_panelColor.setAlphaF(0.6); // 60% opacity default

    m_blurAvailable = KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
}

QString PanelInheritStyle::name() const
{
    return QStringLiteral("Panel Inherit");
}

void PanelInheritStyle::applyToWindow(QWindow *window)
{
    if (!window) {
        return;
    }

    if (m_blurAvailable) {
        KWindowEffects::enableBlurBehind(window, true);
        KWindowEffects::enableBackgroundContrast(window, true, 1.0, 1.0, 1.0);
    } else {
        // Fallback: use fully opaque background
        m_panelColor.setAlphaF(1.0);
    }
}

void PanelInheritStyle::removeFromWindow(QWindow *window)
{
    if (!window) {
        return;
    }

    KWindowEffects::enableBlurBehind(window, false);
    KWindowEffects::enableBackgroundContrast(window, false);
}

QColor PanelInheritStyle::backgroundColor() const
{
    return m_panelColor;
}

bool PanelInheritStyle::blurEnabled() const
{
    return m_blurAvailable;
}

} // namespace krema
