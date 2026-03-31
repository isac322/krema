// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QApplication>

#include <memory>

class KActionCollection;
class KremaSettings;

namespace TaskManager
{
class ActivityInfo;
class TasksModel;
class VirtualDesktopInfo;
}

namespace krema
{

class DockModel;
class MultiDockManager;
class NotificationTracker;

class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int &argc, char **argv);
    ~Application() override;

    int run();

private:
    void registerGlobalShortcuts();

    std::unique_ptr<KremaSettings> m_settings;
    TaskManager::TasksModel *m_tasksModel = nullptr;
    TaskManager::VirtualDesktopInfo *m_virtualDesktopInfo = nullptr;
    TaskManager::ActivityInfo *m_activityInfo = nullptr;
    std::unique_ptr<DockModel> m_dockModel;
    std::unique_ptr<NotificationTracker> m_notificationTracker;
    std::unique_ptr<MultiDockManager> m_dockManager;
    KActionCollection *m_actionCollection = nullptr;
};

} // namespace krema
