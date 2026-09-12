// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "baseisland.h"
#include <QAbstractItemModel>

namespace krema
{

BaseIsland::BaseIsland(const QString &id, QAbstractItemModel *model, QObject *parent)
    : QObject(parent)
    , m_islandId(id)
    , m_tasksModel(model)
{
}

BaseIsland::~BaseIsland() = default;

QString BaseIsland::islandId() const
{
    return m_islandId;
}

QObject *BaseIsland::tasksModel() const
{
    return static_cast<QObject *>(m_tasksModel);
}

} // namespace krema
