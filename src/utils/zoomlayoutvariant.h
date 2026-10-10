// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "zoomcalculator.h"

#include <QVariantList>
#include <QVariantMap>

namespace krema
{

/// QML-facing form of computeDockZoom(): same arguments, result as a
/// QVariantMap (see DockView::zoomLayout and SettingsWindow::zoomLayout).
/// @p style is a krema::ZoomStyle int (0=Parabolic, 1=InPlace);
/// unknown values fall back to Parabolic. @p boundary is the first item
/// in the second zone, or -1 when no separator is present. @p boundaryGap
/// is the fixed extra primary-axis gap inserted before it.
/// minEdge/maxEdge bound the grown background (pass -Infinity/Infinity for
/// no bound).
/// Returns keys: scales, offsets (QVariantList of double), leadingGrowth
/// and trailingGrowth (double).
[[nodiscard]] inline QVariantMap zoomLayoutVariant(int count,
                                                   qreal restStart,
                                                   qreal iconSize,
                                                   qreal spacing,
                                                   int boundary,
                                                   qreal boundaryGap,
                                                   qreal restBackgroundStart,
                                                   qreal restBackgroundEnd,
                                                   qreal maxZoomFactor,
                                                   int style,
                                                   bool active,
                                                   qreal cursor,
                                                   qreal minEdge,
                                                   qreal maxEdge)
{
    // Unknown values fall back to the default, Parabolic.
    const ZoomStyle zoomStyle = style == static_cast<int>(ZoomStyle::InPlace) ? ZoomStyle::InPlace : ZoomStyle::Parabolic;
    const DockZoomLayout layout = computeDockZoom(count,
                                                  restStart,
                                                  iconSize,
                                                  spacing,
                                                  boundary,
                                                  boundaryGap,
                                                  restBackgroundStart,
                                                  restBackgroundEnd,
                                                  maxZoomFactor,
                                                  zoomStyle,
                                                  active,
                                                  cursor,
                                                  minEdge,
                                                  maxEdge);

    QVariantList scales;
    scales.reserve(static_cast<qsizetype>(layout.scales.size()));
    for (double scale : layout.scales) {
        scales.append(scale);
    }
    QVariantList offsets;
    offsets.reserve(static_cast<qsizetype>(layout.offsets.size()));
    for (double offset : layout.offsets) {
        offsets.append(offset);
    }

    return {
        {QStringLiteral("scales"), scales},
        {QStringLiteral("offsets"), offsets},
        {QStringLiteral("leadingGrowth"), layout.leadingGrowth},
        {QStringLiteral("trailingGrowth"), layout.trailingGrowth},
    };
}

} // namespace krema
