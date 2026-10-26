// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QList>
#include <QRect>

#include <algorithm>

namespace krema
{

/// Distance from one screen edge to the inner edge of the visible
/// same-edge panel surface.
struct PanelInset {
    /// Depth of the space those panels reserve (their exclusive zones).
    int reserved = 0;
    /// Depth the dock must clear: the reserved depth, or the far side of a
    /// visible same-edge panel surface when it is deeper. A visible panel may
    /// reserve no space, so this can exceed reserved when reserved is zero.
    int total = 0;

    friend bool operator==(const PanelInset &, const PanelInset &) = default;
};

namespace detail
{
/// Distance from @p edge of @p output to the near (outer) and far (inner)
/// side of @p rect. Edge: 0=Top, 1=Bottom, 2=Left, 3=Right.
struct EdgeSpan {
    int outer;
    int inner;
};

inline EdgeSpan edgeSpan(const QRect &output, const QRect &rect, int edge)
{
    const int outTop = output.y();
    const int outBottom = output.y() + output.height();
    const int outLeft = output.x();
    const int outRight = output.x() + output.width();
    switch (edge) {
    case 0:
        return {rect.y() - outTop, rect.y() + rect.height() - outTop};
    case 2:
        return {rect.x() - outLeft, rect.x() + rect.width() - outLeft};
    case 3:
        return {outRight - (rect.x() + rect.width()), outRight - rect.x()};
    case 1:
    default:
        return {outBottom - (rect.y() + rect.height()), outBottom - rect.y()};
    }
}
} // namespace detail

/// Compute the panel inset on @p edge of @p output.
///
/// @p anchor is Krema's probe surface: a 1 px layer surface on the Bottom
/// layer anchored to @p edge only. The compositor places it right inside the
/// space every reserving panel reserves on that edge, whatever order the
/// panels started in, so its distance from the edge is the reserved depth.
/// Panels that auto-hide, dodge windows or let windows go below reserve
/// nothing, leaving the probe at the edge. When such a panel is visible, its
/// same-edge frame still contributes to total. The frame may include
/// transparent floating padding; KWin exposes no supported painted-surface
/// mask API.
///
/// @p panels are the visible frames of the other dock-type surfaces on the
/// output. For a positive reserved depth, those lying along @p edge and
/// overlapping the reserved band extend the inset to their far side. With no
/// reservation, an edge-touching or edge-crossing visible frame still
/// contributes to total.
inline PanelInset computePanelInset(const QRect &output, const QRect &anchor, const QList<QRect> &panels, int edge)
{
    if (output.isEmpty() || anchor.isNull()) {
        return {};
    }
    const bool horizontalEdge = edge == 0 || edge == 1;
    const int limit = (horizontalEdge ? output.height() : output.width()) / 2;

    // The probe is 2x1 on a horizontal edge and 1x2 on a vertical one, and
    // sits in its edge's half of the output. Right after an edge change the
    // last report still has the probe of the old edge: not a measurement for
    // this one. (Its position along the edge is no use: KWin centres it in the
    // area left by panels on the perpendicular edges.)
    const bool alongThisEdge = horizontalEdge ? anchor.width() > anchor.height() : anchor.height() > anchor.width();
    const int outer = detail::edgeSpan(output, anchor, edge).outer;
    if (!alongThisEdge || outer >= limit) {
        return {};
    }

    const int reserved = std::max(outer, 0);
    int total = reserved;
    for (const QRect &panel : panels) {
        if (!panel.intersects(output)) {
            continue;
        }
        // A panel along a perpendicular edge spans the whole band but is not
        // on this edge; only count panels lying along it.
        const bool alongEdge = horizontalEdge ? panel.width() > panel.height() : panel.height() > panel.width();
        if (!alongEdge) {
            continue;
        }
        const auto span = detail::edgeSpan(output, panel, edge);
        const bool inReservedBand = reserved > 0 && span.inner > 0 && span.outer < reserved;
        const bool visibleAtEdgeWithoutReserve = reserved == 0 && span.outer <= 0;
        if (inReservedBand || visibleAtEdgeWithoutReserve) {
            total = std::max(total, span.inner);
        }
    }
    return {reserved, std::min(total, limit)};
}

} // namespace krema
