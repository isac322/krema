// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once
#include <QObject>
#include <QString>

class KremaSettings;

namespace krema
{
class DockView;

class SettingsWindow : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(QString module READ module WRITE setModule NOTIFY moduleChanged)

public:
    explicit SettingsWindow(KremaSettings *settings, DockView *dockView, QObject *parent = nullptr);
    ~SettingsWindow() override;

    bool visible() const
    {
        return m_visible;
    }
    void setVisible(bool v);

    QString module() const
    {
        return m_module;
    }
    void setModule(const QString &m);

    Q_INVOKABLE void show();
    Q_INVOKABLE void show(const QString &module);
    Q_INVOKABLE void sync();

Q_SIGNALS:
    void visibleChanged(bool visible);
    void moduleChanged(const QString &module);
    void requestSync();

private:
    bool m_visible = false;
    QString m_module;
};
}
