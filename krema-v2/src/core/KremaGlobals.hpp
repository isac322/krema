#pragma once

#include <QObject>

namespace Krema
{
Q_NAMESPACE

/**
 * @brief Urgency levels for the Universal Attention Engine.
 */
enum class UrgencyLevel {
    Idle = 0, ///< No notifications.
    Low = 1, ///< Informational (Track change, etc).
    Normal = 2, ///< Active (Unread messages, etc).
    Critical = 3 ///< Urgent (Low battery, VOIP call, etc).
};
Q_ENUM_NS(UrgencyLevel)

enum class ScreenEdge {
    Bottom,
    Top,
    Left,
    Right
};
Q_ENUM_NS(ScreenEdge)

} // namespace Krema
