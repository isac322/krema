#include "BaseItem.hpp"
#include "../debug/KremaConsole.hpp"
#include "IconAnalyzer.hpp"

namespace Krema
{

BaseItem::BaseItem(uint32_t slotId, QObject *parent)
    : QObject(parent)
    , m_slotId(slotId)
{
}

BaseItem::~BaseItem()
{
}

void BaseItem::setTaskId(const QString &id)
{
    if (m_taskId != id) {
        m_taskId = id;
        emit taskIdChanged();
    }
}

void BaseItem::setTaskName(const QString &name)
{
    if (m_taskName != name) {
        m_taskName = name;
        emit taskNameChanged();
    }
}

void BaseItem::setTaskIcon(const QString &icon)
{
    if (m_taskIcon != icon) {
        m_taskIcon = icon;

        // Trigger Visual Normalization (Alpha Bounding Box)
        auto result = IconAnalyzer::instance().analyze(icon);
        if (m_normalizationScale != result.normalizationScale) {
            m_normalizationScale = result.normalizationScale;
            emit normalizationScaleChanged();
        }

        emit taskIconChanged();
    }
}

void BaseItem::setContentSize(float size)
{
    if (m_contentSize != size) {
        m_contentSize = size;
        emit contentSizeChanged();
    }
}

void BaseItem::setScaleFactor(float scale)
{
    if (m_scaleFactor != scale) {
        m_scaleFactor = scale;
        emit scaleFactorChanged();
    }
}

void BaseItem::setX(float x)
{
    if (m_x != x) {
        m_x = x;
        emit xChanged();
    }
}

void BaseItem::setY(float y)
{
    if (m_y != y) {
        m_y = y;
        emit yChanged();
    }
}

void BaseItem::setIsRunning(bool running)
{
    if (m_isRunning != running) {
        m_isRunning = running;
        emit isRunningChanged();
    }
}

void BaseItem::setUrgency(UrgencyLevel level)
{
    if (m_urgency != level) {
        m_urgency = level;
        emit urgencyChanged();
    }
}

void BaseItem::setNotificationCount(uint32_t count)
{
    if (m_notificationCount != count) {
        m_notificationCount = count;
        emit notificationCountChanged();
    }
}

} // namespace Krema
