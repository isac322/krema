// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <KServiceAction>

#include <QList>

class QUrl;

namespace krema
{

/**
 * Desktop entry actions ("[Desktop Action …]" groups, e.g. a browser's
 * "New Private Window") of the application behind a task launcher URL, as
 * TaskManager reports it in AbstractTasksModel::LauncherUrlWithoutIcon
 * (`applications:` menu id or a `.desktop` file URL, including desktop files
 * outside the XDG applications directories).
 *
 * Follows the Actions= order of the desktop file and drops NoDisplay
 * actions. Separators are kept only between visible actions, so the result
 * is empty when the application has nothing to show (or does not resolve).
 * Each action carries its KService, so it stays launchable after the task
 * row that produced it is gone.
 */
QList<KServiceAction> desktopActions(const QUrl &launcherUrl);

/// Launch @p action like TaskManager launches applications (KIO, with XDG
/// activation and launch errors reported as notifications).
void launchDesktopAction(const KServiceAction &action);

} // namespace krema
