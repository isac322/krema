#pragma once

#include "../KremaGlobals.hpp"
#include "krema_core_export.h"
#include <QObject>
#include <QString>

namespace Krema
{

/**
 * @brief BaseItem represents an atomic unit (Icon, Widget, Folder) in the dock.
 * This is Tier 3 in the 3-Tier Hierarchy.
 */
class KREMA_CORE_EXPORT BaseItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(uint32_t slotId READ slotId CONSTANT)
    Q_PROPERTY(QString taskId READ taskId WRITE setTaskId NOTIFY taskIdChanged)
    Q_PROPERTY(QString taskName READ taskName WRITE setTaskName NOTIFY taskNameChanged)
    Q_PROPERTY(QString taskIcon READ taskIcon WRITE setTaskIcon NOTIFY taskIconChanged)
    Q_PROPERTY(float contentSize READ contentSize WRITE setContentSize NOTIFY contentSizeChanged)
    Q_PROPERTY(float scaleFactor READ scaleFactor WRITE setScaleFactor NOTIFY scaleFactorChanged)
    Q_PROPERTY(float normalizationScale READ normalizationScale NOTIFY normalizationScaleChanged)
    Q_PROPERTY(float x READ x WRITE setX NOTIFY xChanged)
    Q_PROPERTY(float y READ y WRITE setY NOTIFY yChanged)
    Q_PROPERTY(bool isRunning READ isRunning WRITE setIsRunning NOTIFY isRunningChanged)
    Q_PROPERTY(UrgencyLevel urgency READ urgency WRITE setUrgency NOTIFY urgencyChanged)
    Q_PROPERTY(uint32_t notificationCount READ notificationCount WRITE setNotificationCount NOTIFY notificationCountChanged)

public:
    explicit BaseItem(uint32_t slotId, QObject *parent = nullptr);
    ~BaseItem() override;

    uint32_t slotId() const
    {
        return m_slotId;
    }

    QString taskId() const
    {
        return m_taskId;
    }
    void setTaskId(const QString &id);

    QString taskName() const
    {
        return m_taskName;
    }
    void setTaskName(const QString &name);

    QString taskIcon() const
    {
        return m_taskIcon;
    }
    void setTaskIcon(const QString &icon);

    float contentSize() const
    {
        return m_contentSize;
    }
    void setContentSize(float size);

    float scaleFactor() const
    {
        return m_scaleFactor;
    }
    void setScaleFactor(float scale);

    float normalizationScale() const
    {
        return m_normalizationScale;
    }

    float x() const
    {
        return m_x;
    }
    void setX(float x);

    float y() const
    {
        return m_y;
    }
    void setY(float y);

    bool isRunning() const
    {
        return m_isRunning;
    }
    void setIsRunning(bool running);

    UrgencyLevel urgency() const
    {
        return m_urgency;
    }
    void setUrgency(UrgencyLevel level);

    uint32_t notificationCount() const
    {
        return m_notificationCount;
    }
    void setNotificationCount(uint32_t count);

signals:
    void taskIdChanged();
    void taskNameChanged();
    void taskIconChanged();
    void contentSizeChanged();
    void scaleFactorChanged();
    void normalizationScaleChanged();
    void xChanged();
    void yChanged();
    void isRunningChanged();
    void urgencyChanged();
    void notificationCountChanged();

private:
    uint32_t m_slotId;
    QString m_taskId;
    QString m_taskName;
    QString m_taskIcon;
    float m_contentSize = 48.0f;
    float m_scaleFactor = 1.0f;
    float m_normalizationScale = 1.0f;
    float m_x = 0.0f;
    float m_y = 0.0f;
    bool m_isRunning = false;
    UrgencyLevel m_urgency = UrgencyLevel::Idle;
    uint32_t m_notificationCount = 0;
};

} // namespace Krema
