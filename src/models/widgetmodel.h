#pragma once

#include <QAbstractListModel>
#include <QStringList>

namespace krema
{

class WidgetModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum WidgetRoles {
        NameRole = Qt::UserRole + 1,
        PluginIdRole,
        IconRole
    };

    explicit WidgetModel(QObject *parent = nullptr);

    // Tells the UI how many items are in our list
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    // The "Fetcher": retrieves data for a specific icon or widget
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // The "Dictionary": maps C++ IDs to QML-friendly names
    QHash<int, QByteArray> roleNames() const override;

private:
    struct WidgetData {
        QString name;
        QString pluginId;
        QString icon;
    };
    QList<WidgetData> m_widgets;

    void scanForWidgets();
};

} // namespace krema
