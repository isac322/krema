#include "UnityAttentionProvider.hpp"
#include "AttentionManager.hpp"
#include <QDebug>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>

namespace Krema
{

UnityAttentionProvider::UnityAttentionProvider(AttentionManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    // Unity API: com.canonical.Unity.LauncherEntry
    QDBusConnection::sessionBus().connect(QString(),
                                          QString(),
                                          QStringLiteral("org.freedesktop.DBus.Properties"),
                                          QStringLiteral("PropertiesChanged"),
                                          this,
                                          SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
}

UnityAttentionProvider::~UnityAttentionProvider()
{
}

void UnityAttentionProvider::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    Q_UNUSED(invalidated);

    if (interface == QStringLiteral("com.canonical.Unity.LauncherEntry")) {
        // In a real implementation, we would use the object path to identify the application.
        // For M2.1 foundation, we are establishing the listener and urgency mapping logic.

        bool countVisible = false;
        if (changed.contains(QStringLiteral("count-visible"))) {
            countVisible = changed.value(QStringLiteral("count-visible")).toBool();
        }

        uint32_t count = 0;
        if (changed.contains(QStringLiteral("count"))) {
            count = changed.value(QStringLiteral("count")).toUInt();
        }

        bool urgent = false;
        if (changed.contains(QStringLiteral("urgent"))) {
            urgent = changed.value(QStringLiteral("urgent")).toBool();
        }

        // Universal Attention Mapping:
        // Urgent flag -> Level 3 (Critical)
        // Count > 0  -> Level 2 (Normal)
        // Otherwise  -> Level 0 (Idle)

        UrgencyLevel level = UrgencyLevel::Idle;
        if (urgent) {
            level = UrgencyLevel::Critical;
        } else if (countVisible && count > 0) {
            level = UrgencyLevel::Normal;
        }

        // TODO: Map DBus Object Path to SlotID
        // m_manager->setUrgency(slotId, level);
        // m_manager->setNotificationCount(slotId, count);
    }
}

} // namespace Krema
