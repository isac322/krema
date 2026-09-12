#include "MediaAttentionProvider.hpp"
#include "AttentionManager.hpp"
#include <QTimer>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>

namespace Krema
{

MediaAttentionProvider::MediaAttentionProvider(AttentionManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    // Subscribe to MPRIS properties changes on session bus
    QDBusConnection::sessionBus().connect(QString(), // Any service
                                          QString(), // Any path
                                          QStringLiteral("org.freedesktop.DBus.Properties"),
                                          QStringLiteral("PropertiesChanged"),
                                          this,
                                          SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
}

MediaAttentionProvider::~MediaAttentionProvider()
{
}

void MediaAttentionProvider::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    Q_UNUSED(invalidated);

    if (interface == QStringLiteral("org.mpris.MediaPlayer2.Player")) {
        if (changed.contains(QStringLiteral("Metadata"))) {
            // Track changed! Trigger Level 1 (Low) Informational attention.
            // Placeholder slotId for Media Widget
            m_manager->setUrgency(1001, UrgencyLevel::Low);

            // Auto-reset to Idle after a few seconds
            QTimer::singleShot(3000, this, [this]() {
                m_manager->setUrgency(1001, UrgencyLevel::Idle);
            });
        }
    }
}

} // namespace Krema
