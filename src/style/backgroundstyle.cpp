// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "backgroundstyle.h"

#include <KColorScheme>
#include <KWindowEffects>

namespace krema
{

void applyBackgroundToWindow(QWindow *window, BackgroundStyleType type, const QRegion &region)
{
    if (!window) {
        return;
    }

    // Always remove previous effects first
    removeBackgroundFromWindow(window);

    // PanelInherit: compositor handles blur + contrast. QML provides Header color with opacity.
    // All other styles: no compositor effects — QML handles the background directly.
    switch (type) {
    case BackgroundStyleType::PanelInherit:
        if (KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind)) {
            KWindowEffects::enableBlurBehind(window, true, region);
            KWindowEffects::enableBackgroundContrast(window, true, 1.0, 1.0, 1.0, region);
        }
        break;

    case BackgroundStyleType::Acrylic:
        if (KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind)) {
            KWindowEffects::enableBlurBehind(window, true, region);
        }
        break;

    case BackgroundStyleType::Tinted:
    case BackgroundStyleType::Transparent:
        // No compositor effects — QML handles the background directly.
        break;
    }
}

void removeBackgroundFromWindow(QWindow *window)
{
    if (!window) {
        return;
    }

    KWindowEffects::enableBlurBehind(window, false);
    KWindowEffects::enableBackgroundContrast(window, false);
}

QColor computeBackgroundColor(BackgroundStyleType type, const QString &tintColorHex, qreal opacity, bool useAccentColor, bool useSystemColor)
{
    if (type == BackgroundStyleType::Transparent) {
        return Qt::transparent;
    }

    QColor color;

    switch (type) {
    case BackgroundStyleType::PanelInherit:
    case BackgroundStyleType::Acrylic: {
        if (useAccentColor) {
            KColorScheme scheme(QPalette::Normal, KColorScheme::Selection);
            color = scheme.background(KColorScheme::NormalBackground).color();
        }
        if (!useAccentColor || !color.isValid()) {
            KColorScheme scheme(QPalette::Normal, KColorScheme::Header);
            color = scheme.background(KColorScheme::NormalBackground).color();
        }
        break;
    }

    case BackgroundStyleType::Tinted: {
        if (useSystemColor) {
            // System color mode: same as PanelInherit/Acrylic (Header or Selection)
            if (useAccentColor) {
                KColorScheme scheme(QPalette::Normal, KColorScheme::Selection);
                color = scheme.background(KColorScheme::NormalBackground).color();
            }
            if (!useAccentColor || !color.isValid()) {
                KColorScheme scheme(QPalette::Normal, KColorScheme::Header);
                color = scheme.background(KColorScheme::NormalBackground).color();
            }
        } else {
            color = QColor::fromString(tintColorHex);
            if (!color.isValid()) {
                KColorScheme scheme(QPalette::Normal, KColorScheme::Header);
                color = scheme.background(KColorScheme::NormalBackground).color();
            }
        }
        break;
    }

    default:
        break;
    }

    color.setAlphaF(static_cast<float>(opacity));
    return color;
}

bool styleUsesBlur(BackgroundStyleType type)
{
    return type == BackgroundStyleType::PanelInherit || type == BackgroundStyleType::Acrylic;
}

bool isStyleAvailable(BackgroundStyleType /* type */)
{
    return true;
}

} // namespace krema
