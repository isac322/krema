#include "BasePanel.hpp"
#include "BaseIsland.hpp"
#include <QVariant>

namespace Krema
{

BasePanel::BasePanel(QObject *parent)
    : QObject(parent)
{
}

BasePanel::~BasePanel()
{
}

void BasePanel::setEdge(ScreenEdge edge)
{
    if (m_edge != edge) {
        m_edge = edge;
        emit edgeChanged();
    }
}

void BasePanel::setWidth(float width)
{
    if (m_width != width) {
        m_width = width;
        emit widthChanged();
    }
}

void BasePanel::setFloatingOffset(float offset)
{
    if (m_floatingOffset != offset) {
        m_floatingOffset = offset;
        emit floatingOffsetChanged();
    }
}

void BasePanel::setThickness(float thickness)
{
    if (m_thickness != thickness) {
        m_thickness = thickness;
        emit thicknessChanged();
    }
}

QVariantList BasePanel::islandsVariant() const
{
    QVariantList list;
    for (BaseIsland *island : m_islands) {
        list.append(QVariant::fromValue(island));
    }
    return list;
}

void BasePanel::addIsland(BaseIsland *island)
{
    if (island && !m_islands.contains(island)) {
        m_islands.append(island);
        emit islandsChanged();
    }
}

void BasePanel::removeIsland(BaseIsland *island)
{
    if (island && m_islands.removeOne(island)) {
        emit islandsChanged();
    }
}

} // namespace Krema
