// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QApplication>

#include <memory>

class KAboutData;
class KActionCollection;
class KremaSettings;

namespace krema
{

class DockModel;
class LauncherEntryTracker;
class MultiDockManager;
class NotificationTracker;

class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int &argc, char **argv);
    ~Application() override;

    /// Starts the dock with @p aboutData, the metadata main() built with
    /// makeAboutData() and updated from the command line (--desktopfile).
    int run(const KAboutData &aboutData);

    /// The application metadata, defined once; usable before a QApplication
    /// exists.
    static KAboutData makeAboutData();

    /// Saves @p settings to disk whenever one of its user-facing entries
    /// changes. PinnedLaunchers is saved by its own handler in run().
    static void connectSettingsAutoSave(KremaSettings *settings, QObject *context);

    /// Migrates settings written by older Krema releases. A legacy
    /// ZoomAnimationDuration key becomes the Custom zoom animation preset with
    /// that duration and an ease-out curve in both directions (unless a preset
    /// is already stored); the legacy key is always removed and @p settings
    /// saved. Call right after load().
    static void migrateLegacySettings(KremaSettings *settings);

private:
    void registerGlobalShortcuts();

    std::unique_ptr<KremaSettings> m_settings;
    std::unique_ptr<DockModel> m_dockModel;
    std::unique_ptr<NotificationTracker> m_notificationTracker;
    std::unique_ptr<LauncherEntryTracker> m_launcherEntryTracker;
    std::unique_ptr<MultiDockManager> m_dockManager;
    KActionCollection *m_actionCollection = nullptr;
};

} // namespace krema
