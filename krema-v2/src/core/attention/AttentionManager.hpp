#pragma once

#include "../KremaGlobals.hpp"
#include "krema_core_export.h"
#include <QHash>
#include <QObject>

namespace Krema
{

/**
 * @brief AttentionManager coordinates notification and urgency states across the dock.
 * It follows the "Universal Attention Engine" specification (Four-Level Urgency).
 */
class KREMA_CORE_EXPORT AttentionManager : public QObject
{
    Q_OBJECT
public:
    explicit AttentionManager(QObject *parent = nullptr);
    ~AttentionManager() override;

    /**
     * @brief Set the urgency level for a specific slot/item.
     */
    void setUrgency(uint32_t slotId, UrgencyLevel level);

    /**
     * @brief Set the notification count for a specific slot/item.
     */
    void setNotificationCount(uint32_t slotId, uint32_t count);

    /**
     * @brief Get the current urgency level for a slot.
     */
    UrgencyLevel urgency(uint32_t slotId) const;

    /**
     * @brief Get the current notification count for a slot.
     */
    uint32_t notificationCount(uint32_t slotId) const;

signals:
    /**
     * @brief Emitted when the state of an item changes.
     */
    void attentionChanged(uint32_t slotId, UrgencyLevel level, uint32_t count);

private:
    struct ItemState {
        UrgencyLevel level = UrgencyLevel::Idle;
        uint32_t count = 0;
    };

    QHash<uint32_t, ItemState> m_states;
};

} // namespace Krema
