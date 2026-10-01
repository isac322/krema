// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"
#include "style/backgroundstyle.h"

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
    Q_PROPERTY(int iconCacheVersion READ iconCacheVersion NOTIFY iconCacheVersionChanged)
    Q_PROPERTY(int edge READ edge NOTIFY edgeChanged)
    Q_PROPERTY(bool isVertical READ isVertical NOTIFY edgeChanged)
    Q_PROPERTY(int sideTooltipReserve READ sideTooltipReserve WRITE setSideTooltipReserve NOTIFY sideTooltipReserveChanged)

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

    /// Height of the visible panel bar + floating padding (excludes zoom overflow).
    /// Used for preview surface margin to ensure seamless input region adjacency.
    [[nodiscard]] int panelBarHeight() const;

    /// Recalculate surface size (called when iconSize/maxZoomFactor/floating change).
    void updateSize();

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

public Q_SLOTS:
    void applyBackgroundStyle();
    void bumpIconCacheVersion();

Q_SIGNALS:
    void backgroundColorChanged();
    void backgroundStyleTypeChanged();
    void floatingPaddingChanged();
    void iconCacheVersionChanged();
    void edgeChanged();
    void sideTooltipReserveChanged();

private:
    /// Extra height above the panel needed for zoomed icons.
    [[nodiscard]] int zoomOverflowHeight() const;

    void handleScreenChanged(QScreen *newScreen);
    void handleScreenGeometryChanged();

private Q_SLOTS:
    void handleScreenLockChanged(bool active);

private:
    std::unique_ptr<DockPlatform> m_platform;
    KremaSettings *m_settings = nullptr;
    ScreenSettings *m_screenSettings = nullptr;
    DockVisibilityController *m_visibilityController = nullptr;
    TaskIconProvider *m_iconProvider = nullptr;
    int m_iconCacheVersion = 0;
    int m_sideTooltipReserve = 0;
    DockPlatform::Edge m_edge = DockPlatform::Edge::Bottom;
    QMetaObject::Connection m_screenGeometryConnection;

    static constexpr int s_padding = 8;
    static constexpr int s_floatingMargin = 8;
    /// Minimum tooltip reserve; covers the tooltip height above/below a horizontal panel.
    static constexpr int s_tooltipReserve = 36;
};

} // namespace krema
