#include "BaseIsland.hpp"
#include "BaseItem.hpp"
#include <QVariant>

namespace Krema
{

BaseIsland::BaseIsland(uint32_t islandId, QObject *parent)
    : QObject(parent)
    , m_islandId(islandId)
{
}

BaseIsland::~BaseIsland()
{
}

void BaseIsland::setX(float x)
{
    if (m_x != x) {
        m_x = x;
        emit xChanged();
    }
}

void BaseIsland::setWidth(float width)
{
    if (m_width != width) {
        m_width = width;
        emit widthChanged();
    }
}

void BaseIsland::setPadding(float padding)
{
    if (m_padding != padding) {
        m_padding = padding;
        emit paddingChanged();
    }
}

void BaseIsland::setSpacing(float spacing)
{
    if (m_spacing != spacing) {
        m_spacing = spacing;
        emit spacingChanged();
    }
}

void BaseIsland::setZoomEnabled(bool enabled)
{
    if (m_zoomEnabled != enabled) {
        m_zoomEnabled = enabled;
        emit zoomEnabledChanged();
    }
}

QVariantList BaseIsland::itemsVariant() const
{
    QVariantList list;
    for (auto item : m_items) {
        list.append(QVariant::fromValue(item));
    }
    return list;
}

void BaseIsland::addItem(BaseItem *item)
{
    if (item && !m_items.contains(item)) {
        m_items.append(item);
        emit itemsChanged();
    }
}

void BaseIsland::removeItem(BaseItem *item)
{
    if (item && m_items.removeOne(item)) {
        emit itemsChanged();
    }
}

} // namespace Krema
