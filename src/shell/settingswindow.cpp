// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "settingswindow.h"

namespace krema
{

SettingsWindow::SettingsWindow(KremaSettings *settings, DockView *dockView, QObject *parent)
    : QObject(parent)
    , m_visible(false)
{
    // These are ignored as they are no longer needed for window management
    Q_UNUSED(settings);
    Q_UNUSED(dockView);
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::setVisible(bool v)
{
    if (m_visible != v) {
        m_visible = v;
        Q_EMIT visibleChanged(v);
    }
}

void SettingsWindow::setModule(const QString &m)
{
    if (m_module != m) {
        m_module = m;
        Q_EMIT moduleChanged(m);
    }
}

void SettingsWindow::show()
{
    setModule(QStringLiteral(""));
    setVisible(true);
}

void SettingsWindow::show(const QString &defaultModule)
{
    setModule(defaultModule);
    setVisible(true);
}

void SettingsWindow::sync()
{
    Q_EMIT requestSync();
}

} // namespace krema
