// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "desktopactions.h"

#include <KDesktopFile>
#include <KIO/ApplicationLauncherJob>
#include <KNotificationJobUiDelegate>
#include <KService>

#include <QUrl>

#include <optional>

namespace krema
{

namespace
{

// Same resolution as Plasma's task manager (Backend::jumpListActions):
// `applications:` URLs by menu id, desktop file URLs by path. A desktop file
// unknown to KSycoca (outside the XDG applications directories) is read
// directly.
KService::Ptr serviceForLauncherUrl(const QUrl &launcherUrl)
{
    if (!launcherUrl.isValid()) {
        return {};
    }
    if (launcherUrl.scheme() == QLatin1String("applications")) {
        return KService::serviceByMenuId(launcherUrl.path());
    }
    if (!launcherUrl.isLocalFile()) {
        return {};
    }

    const QString path = launcherUrl.toLocalFile();
    if (!KDesktopFile::isDesktopFile(path)) {
        return {};
    }
    if (KService::Ptr service = KService::serviceByDesktopPath(path)) {
        return service;
    }
    KService::Ptr service(new KService(path));
    return service->isValid() ? service : KService::Ptr();
}

} // namespace

QList<KServiceAction> desktopActions(const QUrl &launcherUrl)
{
    const KService::Ptr service = serviceForLauncherUrl(launcherUrl);
    if (!service) {
        return {};
    }

    const QList<KServiceAction> actions = service->actions();
    QList<KServiceAction> visible;
    visible.reserve(actions.size());
    std::optional<KServiceAction> pendingSeparator;
    for (const KServiceAction &action : actions) {
        if (action.noDisplay()) {
            continue;
        }
        if (action.isSeparator()) {
            if (!visible.isEmpty()) {
                pendingSeparator = action;
            }
            continue;
        }
        if (pendingSeparator) {
            visible.append(*pendingSeparator);
            pendingSeparator.reset();
        }
        visible.append(action);
    }
    return visible;
}

void launchDesktopAction(const KServiceAction &action)
{
    // TaskManager::runApp's pattern (TasksModel::requestNewInstance): KIO
    // requests the Wayland XDG activation token itself and the notification
    // delegate reports launch errors.
    auto *job = new KIO::ApplicationLauncherJob(action);
    job->setUiDelegate(new KNotificationJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled));
    job->start();
}

} // namespace krema
