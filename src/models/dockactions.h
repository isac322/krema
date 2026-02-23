// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

namespace krema
{

class DockModel;

/**
 * Task mutation actions for the dock.
 *
 * Separated from DockModel to isolate side effects (activate, pin,
 * drag reorder) from pure query methods.
 */
class DockActions : public QObject
{
    Q_OBJECT

public:
    explicit DockActions(DockModel *model, QObject *parent = nullptr);

    /// Activate the task at @p index. If it's a launcher, launch it.
    Q_INVOKABLE void activate(int index);

    /// Request a new instance of the app at @p index.
    Q_INVOKABLE void newInstance(int index);

    /// Close all windows for the task at @p index.
    Q_INVOKABLE void closeTask(int index);

    /// Toggle pinned state for the task at @p index.
    Q_INVOKABLE void togglePinned(int index);

    /// Cycle through child windows of the grouped task at @p index.
    Q_INVOKABLE void cycleWindows(int index, bool forward);

    /// Move the task at @p fromIndex to @p toIndex. Returns true on success.
    Q_INVOKABLE bool moveTask(int fromIndex, int toIndex);

    /// Add a pinned launcher from a URL (.desktop file or applications: URL).
    Q_INVOKABLE bool addLauncher(const QUrl &url);

    /// Remove the pinned launcher at @p index. Returns true on success.
    Q_INVOKABLE bool removeLauncher(int index);

    /// Open the given URLs with the application at @p index.
    Q_INVOKABLE void openUrlsWithTask(int index, const QList<QUrl> &urls);

Q_SIGNALS:
    void taskLaunching(int index);
    void pinnedLaunchersChanged();

private:
    DockModel *m_model;
};

} // namespace krema
