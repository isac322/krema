#pragma once

#include <QObject>
#include <QVariantMap>

namespace Krema
{

class AttentionManager;

/**
 * @brief UnityAttentionProvider monitors the Unity Launcher API for app badges.
 * This is used by many apps (Discord, Telegram, etc) to show unread counts.
 */
class UnityAttentionProvider : public QObject
{
    Q_OBJECT
public:
    explicit UnityAttentionProvider(AttentionManager *manager, QObject *parent = nullptr);
    ~UnityAttentionProvider() override;

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    AttentionManager *m_manager;
};

} // namespace Krema
