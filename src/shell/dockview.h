// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"
#include "style/backgroundstyle.h"
#include "utils/panelinset.h"

#include <QQuickView>
#include <QVariant>

#include <memory>

namespace TaskManager
{
class ActivityInfo;
class TasksModel;
class VirtualDesktopInfo;
}

class KremaSettings;

namespace krema
{

class DockVisibilityController;
class KWinPanelWatcher;
class PointerFocusTracker;
class ScreenSettings;
class TaskIconProvider;

/**
 * Main dock window.
 *
 * DockView is a QQuickView that displays the dock QML UI.
 * It owns the platform backend and exposes configuration
 * properties to QML via Q_PROPERTY.
 */
class DockView : public QQuickView
{
    Q_OBJECT

    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(int backgroundStyleType READ backgroundStyleType NOTIFY backgroundStyleTypeChanged)
    Q_PROPERTY(int floatingPadding READ floatingPadding NOTIFY floatingPaddingChanged)
    Q_PROPERTY(int shadowOverflow READ shadowOverflow NOTIFY shadowOverflowChanged)
    Q_PROPERTY(int iconCacheVersion READ iconCacheVersion NOTIFY iconCacheVersionChanged)
    Q_PROPERTY(int edge READ edge NOTIFY edgeChanged)
    Q_PROPERTY(bool isVertical READ isVertical NOTIFY edgeChanged)
    Q_PROPERTY(int sideTooltipReserve READ sideTooltipReserve WRITE setSideTooltipReserve NOTIFY sideTooltipReserveChanged)
    /// Whether the compositor's pointer focus is on the dock surface. Unlike
    /// Qt's hover state this ignores the leave QtWayland fakes for a popup
    /// grab (see PointerFocusTracker). False without a Wayland pointer.
    Q_PROPERTY(bool pointerInside READ isPointerInside NOTIFY pointerInsideChanged)

public:
    explicit DockView(std::unique_ptr<DockPlatform> platform, KremaSettings *settings, QWindow *parent = nullptr);
    ~DockView() override;

    /// Initialize the dock view. @p tasksModel is used for visibility control.
    void initialize(TaskManager::TasksModel *tasksModel,
                    TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                    TaskManager::ActivityInfo *activityInfo,
                    DockPlatform::Edge edge,
                    DockPlatform::VisibilityMode visibilityMode);

    // --- Properties ---
    [[nodiscard]] QColor backgroundColor() const;
    [[nodiscard]] int backgroundStyleType() const;
    [[nodiscard]] int floatingPadding() const;
    [[nodiscard]] int shadowOverflow() const;
    [[nodiscard]] int iconCacheVersion() const;
    [[nodiscard]] int edge() const;
    [[nodiscard]] bool isVertical() const;
    [[nodiscard]] int sideTooltipReserve() const;

    /// Space the in-scene tooltip needs beside a vertical panel (gap + max tooltip width).
    /// Published by QML from the tooltip's font-derived max width; vertical docks size
    /// their perpendicular surface reserve to fit it so the tooltip is never clipped.
    void setSideTooltipReserve(int reserve);

    /// Update the dock edge and recalculate surface size.
    void setEdge(DockPlatform::Edge edge);

    /// Height of the visible panel bar + floating padding (excludes zoom overflow),
    /// measured from the screen edge, so it includes the panel inset.
    /// Used for preview surface margin to ensure seamless input region adjacency.
    [[nodiscard]] int panelBarHeight() const;
    /// Outward visual extent needed by zoomed and bouncing icons.
    Q_INVOKABLE int animationOverflowHeight() const;

    /// Depth of the space other always-visible panels reserve on the dock's
    /// edge while the gap is measured from panels; 0 otherwise.
    [[nodiscard]] int reservedPanelInset() const;

    /// Recalculate surface size (called when iconSize/maxZoomFactor change).
    void updateSize();

    /// Re-apply a changed floating gap (Floating, margin, or panel inset):
    /// resize the surface, reflow the input region and reservation, and emit
    /// floatingPaddingChanged().
    void refreshFloatingPadding();

    /// Apply the FloatingMarginReference setting: measure the gap from the
    /// screen edge, or from the always-visible panels on the dock's edge
    /// (watched through KWin scripting; the screen edge when unavailable).
    void applyMarginReference();

    /// Set per-screen settings overlay for size calculations.
    void setScreenSettings(ScreenSettings *screenSettings);

    // --- Platform access ---
    [[nodiscard]] DockPlatform *platform() const;
    [[nodiscard]] DockVisibilityController *visibilityController() const;
    [[nodiscard]] TaskIconProvider *iconProvider() const;

    /// Dock zoom layout for QML (see krema::computeDockZoom).
    /// @p style is a krema::ZoomStyle int (0=Parabolic, 1=InPlace);
    /// unknown values fall back to Parabolic. @p boundary is the first item
    /// in the second zone, or -1 when no separator is present. @p boundaryGap
    /// is the fixed extra primary-axis gap inserted before it.
    /// minEdge/maxEdge bound the grown background (pass -Infinity/Infinity for
    /// no bound).
    /// Returns keys: scales, offsets (QVariantList of double), leadingGrowth
    /// and trailingGrowth (double).
    Q_INVOKABLE QVariantMap zoomLayout(int count,
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
                                       qreal maxEdge) const;

    /// See the pointerInside property.
    [[nodiscard]] bool isPointerInside() const;

public Q_SLOTS:
    void applyBackgroundStyle();
    void bumpIconCacheVersion();

Q_SIGNALS:
    void backgroundColorChanged();
    void backgroundStyleTypeChanged();
    void floatingPaddingChanged();
    void shadowOverflowChanged();
    void iconCacheVersionChanged();
    void edgeChanged();
    void sideTooltipReserveChanged();
    void pointerInsideChanged();

private:
    void handleScreenChanged(QScreen *newScreen);
    void handleScreenGeometryChanged();
    /// Whether the gap is currently measured from panels (setting on and KWin scripting available).
    [[nodiscard]] bool isPanelRelative() const;
    /// Recompute the panel inset for this dock's output and edge.
    void refreshPanelInset();

private Q_SLOTS:
    void handleScreenLockChanged(bool active);

private:
    std::unique_ptr<DockPlatform> m_platform;
    KremaSettings *m_settings = nullptr;
    ScreenSettings *m_screenSettings = nullptr;
    DockVisibilityController *m_visibilityController = nullptr;
    std::unique_ptr<PointerFocusTracker> m_pointerFocusTracker;
    TaskIconProvider *m_iconProvider = nullptr;
    int m_iconCacheVersion = 0;
    int m_sideTooltipReserve = 0;
    DockPlatform::Edge m_edge = DockPlatform::Edge::Bottom;
    QMetaObject::Connection m_screenGeometryConnection;
    std::shared_ptr<KWinPanelWatcher> m_panelWatcher;
    PanelInset m_panelInset;

    static constexpr int s_padding = 8;
    /// Minimum tooltip reserve; covers the tooltip height above/below a horizontal panel.
    static constexpr int s_tooltipReserve = 36;
};

} // namespace krema
