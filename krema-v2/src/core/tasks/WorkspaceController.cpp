#include "WorkspaceController.hpp"
#include "ITaskProvider.hpp"

namespace Krema
{

WorkspaceController::WorkspaceController(ITaskProvider *provider, QObject *parent)
    : QObject(parent)
    , m_provider(provider)
{
}

void WorkspaceController::setActiveWorkspace(const QString &workspaceId)
{
    if (m_activeWorkspace != workspaceId) {
        m_activeWorkspace = workspaceId;
        m_provider->setActiveWorkspace(workspaceId);
        emit activeWorkspaceChanged();
    }
}

} // namespace Krema
