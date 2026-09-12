// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>
#include <QString>

namespace krema
{

/**
 * Abstract base class for modular dock sections (widgets).
 *
 * Each DockModule represents a section of the dock (task manager,
 * launcher, separator, etc.). Concrete implementations provide a
 * QML component URL and an optional C++ data model.
 */
class DockModule : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString qmlSource READ qmlSource CONSTANT)
    Q_PROPERTY(QString widgetId READ widgetId CONSTANT)

public:
    explicit DockModule(QObject *parent = nullptr);
    ~DockModule() override;

    /// Unique identifier for this widget type.
    [[nodiscard]] virtual QString widgetId() const = 0;

    /// QML component URL to load for this section.
    [[nodiscard]] virtual QString qmlSource() const = 0;
};

} // namespace krema
