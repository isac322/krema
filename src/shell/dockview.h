// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"
#include "style/backgroundstyle.h"

#include <QQuickView>

#include <memory>

namespace TaskManager
{
class ActivityInfo;
class TasksModel;
class VirtualDesktopInfo;
}

namespace krema
{

class DockVisibilityController;

/**
 * Main dock window.
 *
 * DockView is a QQuickView that displays the dock QML UI.
 * It owns the platform backend and background style, and exposes
 * configuration properties to QML via Q_PROPERTY.
 */
class DockView : public QQuickView
{
    Q_OBJECT

    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY iconSizeChanged)
    Q_PROPERTY(int iconSpacing READ iconSpacing WRITE setIconSpacing NOTIFY iconSpacingChanged)
    Q_PROPERTY(qreal maxZoomFactor READ maxZoomFactor WRITE setMaxZoomFactor NOTIFY maxZoomFactorChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)
    Q_PROPERTY(bool floating READ isFloating WRITE setFloating NOTIFY floatingChanged)
    Q_PROPERTY(int floatingPadding READ floatingPadding NOTIFY floatingPaddingChanged)

public:
    explicit DockView(std::unique_ptr<DockPlatform> platform, QWindow *parent = nullptr);
    ~DockView() override;

    /// Initialize the dock view. @p tasksModel is used for visibility control.
    void initialize(TaskManager::TasksModel *tasksModel,
                    TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                    TaskManager::ActivityInfo *activityInfo,
                    DockPlatform::Edge edge,
                    DockPlatform::VisibilityMode visibilityMode);

    // --- Properties ---
    [[nodiscard]] QColor backgroundColor() const;

    [[nodiscard]] int iconSize() const;
    void setIconSize(int size);

    [[nodiscard]] int iconSpacing() const;
    void setIconSpacing(int spacing);

    [[nodiscard]] qreal maxZoomFactor() const;
    void setMaxZoomFactor(qreal factor);

    [[nodiscard]] int cornerRadius() const;
    void setCornerRadius(int radius);

    [[nodiscard]] bool isFloating() const;
    void setFloating(bool floating);

    [[nodiscard]] int floatingPadding() const;

    // --- Platform access ---
    [[nodiscard]] DockPlatform *platform() const;
    [[nodiscard]] DockVisibilityController *visibilityController() const;

Q_SIGNALS:
    void backgroundColorChanged();
    void iconSizeChanged();
    void iconSpacingChanged();
    void maxZoomFactorChanged();
    void cornerRadiusChanged();
    void floatingChanged();
    void floatingPaddingChanged();

private:
    void updateSize();
    void applyBackgroundStyle();

    /// Extra height above the panel needed for zoomed icons.
    [[nodiscard]] int zoomOverflowHeight() const;

    std::unique_ptr<DockPlatform> m_platform;
    std::unique_ptr<BackgroundStyle> m_backgroundStyle;
    DockVisibilityController *m_visibilityController = nullptr;

    int m_iconSize = 48;
    int m_iconSpacing = 4;
    qreal m_maxZoomFactor = 1.6;
    int m_cornerRadius = 12;
    bool m_floating = true;
    static constexpr int s_padding = 8;
    static constexpr int s_floatingMargin = 8;
    static constexpr int s_tooltipReserve = 36;
};

} // namespace krema
