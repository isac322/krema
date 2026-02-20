// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QApplication>

#include <memory>

namespace krema
{

class DockModel;
class DockSettings;
class DockView;

class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int &argc, char **argv);
    ~Application() override;

    int run();

private:
    void registerGlobalShortcuts();

    std::unique_ptr<DockSettings> m_settings;
    std::unique_ptr<DockView> m_dockView;
    std::unique_ptr<DockModel> m_dockModel;
};

} // namespace krema
