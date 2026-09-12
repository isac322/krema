#include "AttentionManager.hpp"

namespace Krema
{

AttentionManager::AttentionManager(QObject *parent)
    : QObject(parent)
{
}

AttentionManager::~AttentionManager()
{
}

void AttentionManager::setUrgency(uint32_t slotId, UrgencyLevel level)
{
    auto &state = m_states[slotId];
    if (state.level != level) {
        state.level = level;
        emit attentionChanged(slotId, state.level, state.count);
    }
}

void AttentionManager::setNotificationCount(uint32_t slotId, uint32_t count)
{
    auto &state = m_states[slotId];
    if (state.count != count) {
        state.count = count;
        emit attentionChanged(slotId, state.level, state.count);
    }
}

UrgencyLevel AttentionManager::urgency(uint32_t slotId) const
{
    return m_states.value(slotId).level;
}

uint32_t AttentionManager::notificationCount(uint32_t slotId) const
{
    return m_states.value(slotId).count;
}

} // namespace Krema
