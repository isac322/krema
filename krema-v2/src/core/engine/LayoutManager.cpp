#include "LayoutManager.hpp"
#include "../debug/KremaConsole.hpp"
#include "../tasks/ITaskProvider.hpp"
#include "BaseIsland.hpp"
#include "BaseItem.hpp"
#include "BasePanel.hpp"
#include <algorithm>
#include <cmath>

namespace Krema
{

extern bool g_debugGeom;
extern bool g_debugHit;

LayoutManager::LayoutManager(QObject *parent)
    : QObject(parent)
{
}

LayoutManager::~LayoutManager()
{
}

void LayoutManager::setPanel(BasePanel *panel)
{
    if (m_panel != panel) {
        m_panel = panel;
        emit panelChanged();
        updateLayout();
    }
}

void LayoutManager::setTaskProvider(ITaskProvider *provider)
{
    if (m_taskProvider != provider) {
        m_taskProvider = provider;
        emit taskProviderChanged();
    }
}

void LayoutManager::setMaxZoomFactor(float factor)
{
    if (m_maxZoomFactor != factor) {
        m_maxZoomFactor = factor;
        emit maxZoomFactorChanged();
        updateLayout();
    }
}

void LayoutManager::updateLayout()
{
    if (!m_panel || m_isUpdating)
        return;
    m_isUpdating = true;

    bool shouldLog = g_debugGeom && !m_isHovering;

    const float MAX_DOCK_WIDTH = 1200.0f;
    const float islandGap = 16.0f;

    // 1. First Pass: Calculate total unconstrained width
    float totalUnconstrainedWidth = 0.0f;
    for (BaseIsland *island : m_panel->islands()) {
        float currentItemX = island->padding();
        for (BaseItem *item : island->items()) {
            currentItemX += (item->contentSize() * item->scaleFactor()) + island->spacing();
        }
        totalUnconstrainedWidth += (currentItemX - island->spacing() + island->padding()) + islandGap;
    }
    totalUnconstrainedWidth = std::max(0.0f, totalUnconstrainedWidth - islandGap);

    // 2. Determine Squish Factor
    float squishFactor = (totalUnconstrainedWidth > MAX_DOCK_WIDTH) ? (MAX_DOCK_WIDTH / totalUnconstrainedWidth) : 1.0f;

    // 3. Second Pass: Apply Layout with Squish
    m_ghostGrid.clear();
    float currentIslandX = 0.0f;
    float currentGhostX = 0.0f;

    for (BaseIsland *island : m_panel->islands()) {
        island->setX(currentIslandX * squishFactor);

        float currentItemX = island->padding();
        float currentGhostItemX = island->padding();

        for (BaseItem *item : island->items()) {
            item->setX(currentItemX * squishFactor);
            item->setY(0.0f);

            m_ghostGrid[item] = currentGhostX + currentGhostItemX + (item->contentSize() / 2.0f);

            float itemWidth = (item->contentSize() * item->scaleFactor()) * squishFactor;
            currentItemX += (itemWidth / squishFactor) + island->spacing();
            currentGhostItemX += item->contentSize() + island->spacing();
        }

        float totalIslandWidth = ((currentItemX - island->spacing() + island->padding()) * squishFactor);
        float totalGhostIslandWidth = currentGhostItemX - island->spacing() + island->padding();

        island->setWidth(totalIslandWidth);

        currentIslandX += (totalGhostIslandWidth + islandGap);
        currentGhostX += (totalGhostIslandWidth + islandGap);
    }

    float finalPanelWidth = std::min(totalUnconstrainedWidth, MAX_DOCK_WIDTH);
    m_panel->setWidth(finalPanelWidth);

    if (shouldLog) {
        KREMA_GEOM_LOG(QStringLiteral("--- [End Layout] Total Panel Width: %1px | Squish: %2 ---").arg(finalPanelWidth).arg(squishFactor));
    }

    m_isUpdating = false;
    // emit layoutUpdated();
}

void LayoutManager::processClick(float mouseX, float mouseY)
{
    if (!m_panel || !m_taskProvider)
        return;

    for (BaseIsland *island : m_panel->islands()) {
        for (BaseItem *item : island->items()) {
            float visualCenterX = island->x() + item->x() + (item->contentSize() * item->scaleFactor()) / 2.0f;
            float visualCenterY = item->y();
            float dx = mouseX - visualCenterX;
            float dy = mouseY - visualCenterY;
            float distance = std::sqrt(dx * dx + dy * dy);

            float currentRadius = (item->contentSize() * item->scaleFactor()) / 2.0f;

            if (distance <= currentRadius) {
                activateTask(item->taskId());
                return;
            }
        }
    }
}

void LayoutManager::processHover(float mouseX, float mouseY, float zoomIntensity)
{
    if (!m_panel)
        return;

    bool isCurrentlyHovering = (mouseX > -500.0f);
    m_isHovering = isCurrentlyHovering;

    const float maxScale = 1.0f + (m_maxZoomFactor - 1.0f) * zoomIntensity;
    BaseItem *newHoveredItem = nullptr;

    for (BaseIsland *island : m_panel->islands()) {
        if (!island->isZoomEnabled()) {
            for (BaseItem *item : island->items()) {
                item->setScaleFactor(1.0f);
            }
            continue;
        }

        for (BaseItem *item : island->items()) {
            float itemCenterX = m_ghostGrid.value(item, 0.0f);
            float distance = std::abs(mouseX - itemCenterX);
            float scale = 1.0f;

            float zoomSigma = item->contentSize() * 1.2f;
            float sigma2 = zoomSigma * zoomSigma;

            if (distance < (zoomSigma * 2.5f)) {
                scale = 1.0f + (maxScale - 1.0f) * std::exp(-(distance * distance) / (2.0f * sigma2));
            }

            if (item->scaleFactor() != scale) {
                item->setScaleFactor(scale);
            }
        }
    }

    updateLayout();

    if (m_hoveredItem != newHoveredItem) {
        m_hoveredItem = newHoveredItem;
        emit hoveredItemChanged();
    }
}

void LayoutManager::activateTask(const QString &id)
{
    if (m_taskProvider)
        m_taskProvider->activateTask(id);
}

} // namespace Krema
