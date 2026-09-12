#include "widgetmodel.h"
#include <KPackage/PackageLoader>
#include <KPackage/PackageStructure>

namespace krema
{

WidgetModel::WidgetModel(QObject *parent)
    : QAbstractListModel(parent)
{
    scanForWidgets();
}

int WidgetModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_widgets.count();
}

QHash<int, QByteArray> WidgetModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[PluginIdRole] = "pluginId";
    roles[IconRole] = "icon";
    return roles;
}

QVariant WidgetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_widgets.count())
        return QVariant();

    const WidgetData &widget = m_widgets.at(index.row());

    switch (role) {
    case NameRole:
        return widget.name;
    case PluginIdRole:
        return widget.pluginId;
    case IconRole:
        return widget.icon;
    default:
        return QVariant();
    }
}

void WidgetModel::scanForWidgets()
{
    auto packages = KPackage::PackageLoader::self()->listPackages("Plasma/Applet");

    for (const auto &metaData : packages) {
        // We ensure we only pick high-quality widgets meant for panels
        if (metaData.value("X-Plasma-MainScript") == "ui/main.qml") {
            m_widgets.append({metaData.name(), metaData.pluginId(), metaData.iconName()});
        }
    }
}

} // namespace krema
