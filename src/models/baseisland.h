// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>
#include <QString>

class QAbstractItemModel;

namespace krema
{

/**
 * @brief BaseIsland represents a logical group of items (Tier 2).
 * For Phase 1 of the 3-Tier Migration, this wraps an underlying QAbstractItemModel.
 */
class BaseIsland : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString islandId READ islandId CONSTANT)
    Q_PROPERTY(QObject *tasksModel READ tasksModel CONSTANT)

public:
    explicit BaseIsland(const QString &id, QAbstractItemModel *model, QObject *parent = nullptr);
    ~BaseIsland() override;

    [[nodiscard]] QString islandId() const;
    [[nodiscard]] QObject *tasksModel() const;

private:
    QString m_islandId;
    QAbstractItemModel *m_tasksModel = nullptr;
};

} // namespace krema
