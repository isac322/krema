#pragma once

#include <QObject>
#include <QString>

#include "krema_core_export.h"

namespace Krema
{

class ITaskProvider;

/**
 * @brief Controller for workspace-aware task filtering.
 * Bridges platform workspace signals to the ITaskProvider.
 */
class KREMA_CORE_EXPORT WorkspaceController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString activeWorkspace READ activeWorkspace WRITE setActiveWorkspace NOTIFY activeWorkspaceChanged)

public:
    explicit WorkspaceController(ITaskProvider *provider, QObject *parent = nullptr);

    QString activeWorkspace() const
    {
        return m_activeWorkspace;
    }
    void setActiveWorkspace(const QString &workspaceId);

signals:
    void activeWorkspaceChanged();

private:
    ITaskProvider *m_provider;
    QString m_activeWorkspace;
};

} // namespace Krema
