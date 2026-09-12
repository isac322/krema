#include "TrayProvider.hpp"
#include <KStatusNotifierItem>
#include <QDebug>

namespace Krema
{

TrayProvider::TrayProvider(QObject *parent)
    : QObject(parent)
{
    // TODO: Connect to KStatusNotifierHost to discover icons
}

TrayProvider::~TrayProvider()
{
}

QList<TrayEntry> TrayProvider::items() const
{
    return m_items;
}

} // namespace Krema
