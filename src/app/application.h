// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QApplication>

#include <memory>

class KActionCollection;

namespace krema
{

class DockModel;
class DockSettings;
class DockView;
class PreviewController;
class SettingsWindow;

class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int &argc, char **argv);
    ~Application() override;

    int run();

private:
    void registerGlobalShortcuts();
    void applySettings();

    std::unique_ptr<DockSettings> m_settings;
    std::unique_ptr<DockView> m_dockView;
    std::unique_ptr<DockModel> m_dockModel;
    std::unique_ptr<SettingsWindow> m_settingsWindow;
    PreviewController *m_previewController = nullptr;
    KActionCollection *m_actionCollection = nullptr;
};

} // namespace krema
