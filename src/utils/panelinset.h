// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QList>
#include <QRect>

#include <algorithm>
#include <cstdlib>

namespace krema
{

/// Distance from one screen edge to the inner edge of the always-visible
/// panels on that edge.
struct PanelInset {
    /// Depth of the space those panels reserve (their exclusive zones).
    int reserved = 0;
    /// Depth the dock must clear: the reserved depth, or the far side of a
    /// panel surface in that band when it is deeper (a floating Plasma panel
    /// reserves less than its surface, which also holds the floating gap).
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
/// space every always-visible panel reserves on that edge, whatever order the
/// panels started in, so its distance from the edge is the reserved depth.
/// Panels that auto-hide, dodge windows or let windows cover them reserve
/// nothing, leave the probe at the edge, and are ignored.
///
/// @p panels are the frames of the other dock-type surfaces on the output;
/// those lying along @p edge and overlapping the reserved band extend the
/// inset to their far side.
inline PanelInset computePanelInset(const QRect &output, const QRect &anchor, const QList<QRect> &panels, int edge)
{
    if (output.isEmpty() || anchor.isNull()) {
        return {};
    }
    const bool horizontalEdge = edge == 0 || edge == 1;
    const int limit = (horizontalEdge ? output.height() : output.width()) / 2;

    // Anchored to one edge only, the probe is centred along it and sits in
    // that edge's half of the output. A report from before an edge change
    // still has the probe of the old edge: not a measurement for this one.
    // (2 px of slack for rounding under fractional scaling.)
    const QPoint offset = anchor.center() - output.center();
    const int offCentre = horizontalEdge ? std::abs(offset.x()) : std::abs(offset.y());
    const int outer = detail::edgeSpan(output, anchor, edge).outer;
    if (offCentre > 2 || outer >= limit) {
        return {};
    }

    const int reserved = std::max(outer, 0);
    if (reserved == 0) {
        return {};
    }

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
        if (span.outer < reserved && span.inner > 0) {
            total = std::max(total, span.inner);
        }
    }
    return {reserved, std::min(total, limit)};
}

} // namespace krema
