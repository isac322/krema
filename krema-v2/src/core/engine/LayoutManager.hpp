#pragma once

#include "../tasks/ITaskProvider.hpp"
#include "krema_core_export.h"
#include <QElapsedTimer>
#include <QMap>
#include <QObject>

namespace Krema
{

class BasePanel;
class BaseItem;

/**
 * @brief LayoutManager is the central orchestrator for the 3-Tier Hierarchy.
 * It implements the Direct Recursive Repulsion and Parabolic Zoom logic.
 */
class KREMA_CORE_EXPORT LayoutManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BasePanel *panel READ panel WRITE setPanel NOTIFY panelChanged)
    Q_PROPERTY(ITaskProvider *taskProvider READ taskProvider WRITE setTaskProvider NOTIFY taskProviderChanged)
    Q_PROPERTY(BaseItem *hoveredItem READ hoveredItem NOTIFY hoveredItemChanged)
    Q_PROPERTY(float maxZoomFactor READ maxZoomFactor WRITE setMaxZoomFactor NOTIFY maxZoomFactorChanged)

public:
    explicit LayoutManager(QObject *parent = nullptr);
    ~LayoutManager() override;

    BasePanel *panel() const
    {
        return m_panel;
    }
    void setPanel(BasePanel *panel);

    ITaskProvider *taskProvider() const
    {
        return m_taskProvider;
    }
    void setTaskProvider(ITaskProvider *provider);

    BaseItem *hoveredItem() const
    {
        return m_hoveredItem;
    }

    float maxZoomFactor() const
    {
        return m_maxZoomFactor;
    }
    void setMaxZoomFactor(float factor);

    /**
     * @brief Perform the mathematical layout pass (Tier 1 -> Tier 2 -> Tier 3).
     * Implements Rule 15: Direct Recursive Repulsion.
     */
    Q_INVOKABLE void updateLayout();

    /**
     * @brief Process mouse movement to drive the Parabolic Zoom wave.
     * @param zoomIntensity The kinetic bridge multiplier (0.0 to 1.0) for smooth entry/exit.
     */
    Q_INVOKABLE void processHover(float x, float y, float zoomIntensity = 1.0f);

    /**
     * @brief Interaction: Process a global click, resolving it to a specific task using Pythagorean hit-testing.
     */
    Q_INVOKABLE void processClick(float x, float y);

    /**
     * @brief Interaction: Activate a task.
     */
    Q_INVOKABLE void activateTask(const QString &id);

signals:
    void panelChanged();
    void taskProviderChanged();
    void layoutUpdated();
    void hoveredItemChanged();
    void maxZoomFactorChanged();

private:
    BasePanel *m_panel = nullptr;
    ITaskProvider *m_taskProvider = nullptr;
    BaseItem *m_hoveredItem = nullptr;
    float m_maxZoomFactor = 1.6f;
    QMap<BaseItem *, float> m_ghostGrid;
    bool m_isHovering = false;
    bool m_isUpdating = false;
    QElapsedTimer m_hoverEntryTimer;
    float m_lastMouseX = -1.0f;
};

} // namespace Krema
