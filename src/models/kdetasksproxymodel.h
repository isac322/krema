// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QIdentityProxyModel>

namespace krema
{

class KdeTasksProxyModel : public QIdentityProxyModel
{
    Q_OBJECT
public:
    enum CustomRoles {
        ActiveChildIndexRole = Qt::UserRole + 100
    };

    explicit KdeTasksProxyModel(QObject *parent = nullptr);
    ~KdeTasksProxyModel() override;

    void setSourceModel(QAbstractItemModel *sourceModel) override;

    QVariant data(const QModelIndex &proxyIndex, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void onSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles);
};

} // namespace krema
