// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "itaskssource.h"

namespace TaskManager
{
class TasksModel;
}

namespace krema
{

/**
 * Production adapter: wraps a TaskManager::TasksModel and delegates
 * all ITasksSource operations to it.
 *
 * DockModel and DockActions use this adapter in production so that
 * the same code can be exercised against MockTasksSource in tests.
 */
class TasksModelAdapter : public ITasksSource
{
public:
    explicit TasksModelAdapter(TaskManager::TasksModel *model);

    // --- Row / child counts ---
    [[nodiscard]] int rowCount() const override;
    [[nodiscard]] int childCount(int row) const override;

    // --- Data access ---
    [[nodiscard]] QVariant data(int row, int role) const override;
    [[nodiscard]] QVariant childData(int row, int childRow, int role) const override;

    // --- Launcher management ---
    [[nodiscard]] QStringList launcherList() const override;
    bool requestAddLauncher(const QUrl &url) override;
    bool requestRemoveLauncher(const QUrl &url) override;

    // --- Task actions ---
    void requestActivate(int row) override;
    void requestActivateChild(int row, int childRow) override;
    void requestNewInstance(int row) override;
    void requestClose(int row) override;
    void requestOpenUrls(int row, const QList<QUrl> &urls) override;

    // --- Reordering ---
    bool move(int fromRow, int toRow) override;
    void syncLaunchers() override;

private:
    TaskManager::TasksModel *m_model;
};

} // namespace krema
