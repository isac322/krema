// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"

#include <QRasterWindow>

#include <memory>

class QScreen;

namespace krema
{

/**
 * Transparent strip along a screen edge that reports pointer entry.
 *
 * Follow Active mode unmaps the docks of inactive screens, and an unmapped
 * surface receives no pointer events, so the Mouse trigger needs a mapped
 * surface of its own on every inactive screen. The strip covers the same
 * edge area as a hidden auto-hide dock's trigger region
 * (kEdgeTriggerThickness along the dock edge); nothing is painted, so it
 * uses a raster backing store instead of a scene graph.
 */
class EdgeTrigger : public QRasterWindow
{
    Q_OBJECT

public:
    /// Create a strip on @p screen along @p edge. It is shown with show().
    EdgeTrigger(QScreen *screen, DockPlatform::Edge edge);
    ~EdgeTrigger() override;

    /// Move the strip to another edge of its screen.
    void setEdge(DockPlatform::Edge edge);

    [[nodiscard]] bool isHovered() const;

Q_SIGNALS:
    void hoveredChanged(bool hovered);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void applySize();
    void setHovered(bool hovered);

    std::unique_ptr<DockPlatform> m_platform;
    bool m_hovered = false;
};

} // namespace krema
