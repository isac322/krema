#pragma once

#include <QObject>
#include <QVariantMap>

namespace Krema
{

class AttentionManager;

/**
 * @brief MediaAttentionProvider listens to MPRIS D-Bus signals.
 * Maps track changes to Level 1 (Low) urgency.
 */
class MediaAttentionProvider : public QObject
{
    Q_OBJECT
public:
    explicit MediaAttentionProvider(AttentionManager *manager, QObject *parent = nullptr);
    ~MediaAttentionProvider() override;

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    AttentionManager *m_manager;
};

} // namespace Krema
