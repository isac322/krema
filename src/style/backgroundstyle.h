// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QWindow>

namespace krema
{

/**
 * Abstract interface for dock background rendering styles.
 *
 * Each style controls how the dock background is drawn, from simple
 * solid colors to complex shader-based effects. The interface is
 * designed for easy extension — new styles can be added without
 * modifying existing code.
 */
class BackgroundStyle
{
public:
    virtual ~BackgroundStyle() = default;

    /// Human-readable name for the settings UI.
    [[nodiscard]] virtual QString name() const = 0;

    /// Apply compositor-level effects (blur, contrast) to @p window.
    /// Called once when the style is activated or the window is created.
    virtual void applyToWindow(QWindow *window) = 0;

    /// Remove compositor-level effects from @p window.
    /// Called when switching to a different style.
    virtual void removeFromWindow(QWindow *window) = 0;

    /// Background color with alpha for the QML Rectangle.
    /// The QML dock background reads this via the C++ bridge.
    [[nodiscard]] virtual QColor backgroundColor() const = 0;

    /// Whether KWin blur should be enabled behind the dock.
    [[nodiscard]] virtual bool blurEnabled() const = 0;
};

} // namespace krema
