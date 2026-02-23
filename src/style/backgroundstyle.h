// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QColor>
#include <QWindow>

namespace krema
{

enum class BackgroundStyleType : int {
    PanelInherit = 0,
    Transparent = 1,
    Tinted = 2, // UseSystemColor → Header color; otherwise user tint
    Acrylic = 3,
};

/// Apply compositor effects (blur, contrast) to @p window for the given style.
/// Removes any previous effects first.
/// @param region The region to apply blur/contrast to. Empty = whole window (avoid!).
void applyBackgroundToWindow(QWindow *window, BackgroundStyleType type, const QRegion &region = QRegion());

/// Remove all compositor effects (blur, contrast) from @p window.
void removeBackgroundFromWindow(QWindow *window);

/// Compute the background color for a QML Rectangle.
/// @param tintColorHex Tinted style user color (hex, e.g. "#1e3a5f")
/// @param opacity Base opacity (0.0–1.0)
/// @param useAccentColor If true, use Selection (accent) color instead of Header color
/// @param useSystemColor If true (Tinted), use system Header color instead of tintColorHex
QColor computeBackgroundColor(BackgroundStyleType type, const QString &tintColorHex, qreal opacity, bool useAccentColor = false, bool useSystemColor = false);

/// Whether the given style uses blur behind.
bool styleUsesBlur(BackgroundStyleType type);

/// Whether the given style is available on this system.
bool isStyleAvailable(BackgroundStyleType type);

} // namespace krema
