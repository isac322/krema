// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockvisibilitycontroller.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <QLoggingCategory>
#include <QWindow>

Q_LOGGING_CATEGORY(lcVisibility, "krema.shell.visibility")

namespace krema
{

DockVisibilityController::DockVisibilityController(DockPlatform *platform, TaskManager::TasksModel *tasksModel, QWindow *dockWindow, QObject *parent)
    : QObject(parent)
    , m_platform(platform)
    , m_tasksModel(tasksModel)
    , m_dockWindow(dockWindow)
{
    // Debounce evaluation to avoid rapid toggling
    m_evaluateTimer.setSingleShot(true);
    m_evaluateTimer.setInterval(300);
    connect(&m_evaluateTimer, &QTimer::timeout, this, &DockVisibilityController::evaluateVisibility);

    connectModelSignals();
}

DockVisibilityController::~DockVisibilityController() = default;

bool DockVisibilityController::isDockVisible() const
{
    return m_visible;
}

int DockVisibilityController::mode() const
{
    return static_cast<int>(m_mode);
}

void DockVisibilityController::setMode(int mode)
{
    setMode(static_cast<DockPlatform::VisibilityMode>(mode));
}

void DockVisibilityController::setMode(DockPlatform::VisibilityMode mode)
{
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    m_platform->setVisibilityMode(mode);

    qCDebug(lcVisibility) << "Visibility mode changed to:" << static_cast<int>(mode);

    // Immediately evaluate with new mode
    evaluateVisibility();
    Q_EMIT modeChanged();
}

void DockVisibilityController::setHovered(bool hovered)
{
    if (m_hovered == hovered) {
        return;
    }

    m_hovered = hovered;

    if (hovered) {
        // Show immediately on hover
        m_evaluateTimer.stop();
        setVisible(true);
    } else {
        // Debounce hide after mouse leaves
        m_evaluateTimer.start();
    }
}

void DockVisibilityController::evaluateVisibility()
{
    // If hovered, always show
    if (m_hovered) {
        setVisible(true);
        return;
    }

    switch (m_mode) {
    case DockPlatform::VisibilityMode::AlwaysVisible:
        setVisible(true);
        break;

    case DockPlatform::VisibilityMode::AlwaysHidden:
        setVisible(false);
        break;

    case DockPlatform::VisibilityMode::DodgeWindows:
        setVisible(!hasOverlappingWindow());
        break;

    case DockPlatform::VisibilityMode::SmartHide:
        setVisible(!hasMaximizedOrFullscreenWindow());
        break;
    }
}

void DockVisibilityController::setVisible(bool visible)
{
    if (m_visible == visible) {
        return;
    }

    m_visible = visible;
    qCDebug(lcVisibility) << "Dock visible:" << visible;

    // Update input region:
    // When hidden, set a thin 2px strip at the edge for hover detection.
    // When visible, clear the mask to accept full input.
    if (visible) {
        m_platform->setInputRegion(QRegion());
    } else {
        // 2px strip at the bottom edge for hover detection
        if (m_dockWindow) {
            const int w = m_dockWindow->width();
            const int h = m_dockWindow->height();
            m_platform->setInputRegion(QRegion(0, h - 2, w, 2));
        }
    }

    Q_EMIT dockVisibleChanged();
}

bool DockVisibilityController::hasOverlappingWindow() const
{
    if (!m_dockWindow) {
        return false;
    }

    const QRect dockRect = m_dockWindow->geometry();
    const int count = m_tasksModel->rowCount();

    for (int i = 0; i < count; ++i) {
        const QModelIndex idx = m_tasksModel->index(i, 0);

        // Only check actual windows (not launchers)
        if (!idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool()) {
            continue;
        }

        // Skip minimized windows
        if (idx.data(TaskManager::AbstractTasksModel::IsMinimized).toBool()) {
            continue;
        }

        const QRect windowGeometry = idx.data(TaskManager::AbstractTasksModel::Geometry).toRect();
        if (windowGeometry.isValid() && windowGeometry.intersects(dockRect)) {
            return true;
        }
    }

    return false;
}

bool DockVisibilityController::hasMaximizedOrFullscreenWindow() const
{
    const int count = m_tasksModel->rowCount();

    for (int i = 0; i < count; ++i) {
        const QModelIndex idx = m_tasksModel->index(i, 0);

        // Only check actual windows
        if (!idx.data(TaskManager::AbstractTasksModel::IsWindow).toBool()) {
            continue;
        }

        // Skip minimized windows
        if (idx.data(TaskManager::AbstractTasksModel::IsMinimized).toBool()) {
            continue;
        }

        const bool isMaximized = idx.data(TaskManager::AbstractTasksModel::IsMaximized).toBool();
        const bool isFullScreen = idx.data(TaskManager::AbstractTasksModel::IsFullScreen).toBool();

        if (isMaximized || isFullScreen) {
            return true;
        }
    }

    return false;
}

void DockVisibilityController::connectModelSignals()
{
    // Re-evaluate when windows are added/removed
    connect(m_tasksModel, &QAbstractItemModel::rowsInserted, this, [this]() {
        m_evaluateTimer.start();
    });
    connect(m_tasksModel, &QAbstractItemModel::rowsRemoved, this, [this]() {
        m_evaluateTimer.start();
    });
    connect(m_tasksModel, &QAbstractItemModel::modelReset, this, [this]() {
        m_evaluateTimer.start();
    });

    // Re-evaluate when window properties change (geometry, maximized, fullscreen, etc.)
    connect(m_tasksModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles) {
        Q_UNUSED(topLeft)
        Q_UNUSED(bottomRight)

        // Only care about roles that affect visibility
        static const QList<int> relevantRoles = {
            TaskManager::AbstractTasksModel::Geometry,
            TaskManager::AbstractTasksModel::IsMaximized,
            TaskManager::AbstractTasksModel::IsFullScreen,
            TaskManager::AbstractTasksModel::IsMinimized,
            TaskManager::AbstractTasksModel::IsWindow,
        };

        // If roles list is empty, treat as "any role changed"
        if (roles.isEmpty()) {
            m_evaluateTimer.start();
            return;
        }

        for (int role : roles) {
            if (relevantRoles.contains(role)) {
                m_evaluateTimer.start();
                return;
            }
        }
    });
}

} // namespace krema
