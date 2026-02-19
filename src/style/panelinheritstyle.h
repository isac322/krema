// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "backgroundstyle.h"

namespace krema
{

/**
 * Default background style that inherits the Plasma panel color.
 *
 * Uses KWindowEffects to request KWin compositor blur behind the dock,
 * and reads the Plasma theme's panel color for the background tint.
 * Falls back to an opaque background when KWin blur is unavailable.
 */
class PanelInheritStyle : public BackgroundStyle
{
public:
    PanelInheritStyle();

    [[nodiscard]] QString name() const override;
    void applyToWindow(QWindow *window) override;
    void removeFromWindow(QWindow *window) override;
    [[nodiscard]] QColor backgroundColor() const override;
    [[nodiscard]] bool blurEnabled() const override;

private:
    QColor m_panelColor;
    bool m_blurAvailable = false;
};

} // namespace krema
