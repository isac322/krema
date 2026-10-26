// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "trashmanager.h"

#include <KCoreDirLister>
#include <KIO/CopyJob>
#include <KJob>
#include <KIO/EmptyTrashJob>
#include <KIO/OpenUrlJob>
#include <KIconLoader>
#include <KNotificationJobUiDelegate>

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QIcon>
#include <QStandardPaths>
#include <QStringList>

namespace krema
{

namespace
{

constexpr int s_fewMaximum = 4;
constexpr int s_moderateMaximum = 19;

void startWithErrorNotifications(KJob *job)
{
    if (!job) {
        return;
    }
    job->setUiDelegate(new KNotificationJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled));
    job->start();
}

} // namespace

TrashManager::TrashManager(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
    , m_lister(new KCoreDirLister(this))
{
    connect(m_lister, &KCoreDirLister::newItems, this, [this](const auto &) {
        updateState();
    });
    connect(m_lister, &KCoreDirLister::itemsAdded, this, [this](const auto &, const auto &) {
        updateState();
    });
    connect(m_lister, &KCoreDirLister::itemsDeleted, this, [this](const auto &) {
        updateState();
    });
    connect(m_lister, qOverload<>(&KCoreDirLister::clear), this, &TrashManager::updateState);
    connect(m_lister, qOverload<>(&KCoreDirLister::completed), this, &TrashManager::updateState);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        updateTrashWatcher();
        m_lister->openUrl(QUrl(QStringLiteral("trash:/")), KCoreDirLister::Reload);
    });

    if (auto *iconLoader = KIconLoader::global()) {
        connect(iconLoader, &KIconLoader::iconChanged, this, [this] {
            updateIconName();
        });
    }
    updateTrashWatcher();
    m_lister->setAutoUpdate(true);
    m_lister->openUrl(QUrl(QStringLiteral("trash:/")));
    updateState();
}

void TrashManager::updateTrashWatcher()
{
    const QString dataLocation = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataLocation.isEmpty()) {
        return;
    }

    const QString trashDirectory = QDir(dataLocation).filePath(QStringLiteral("Trash"));
    QStringList paths;
    if (QFileInfo(trashDirectory).isDir()) {
        paths.append(trashDirectory);

        const QString filesDirectory = QDir(trashDirectory).filePath(QStringLiteral("files"));
        if (QFileInfo(filesDirectory).isDir()) {
            paths.append(filesDirectory);
        }
        const QString infoDirectory = QDir(trashDirectory).filePath(QStringLiteral("info"));
        if (QFileInfo(infoDirectory).isDir()) {
            paths.append(infoDirectory);
        }
    } else if (QFileInfo(dataLocation).isDir()) {
        paths.append(dataLocation);
    }

    for (const QString &path : m_watcher->directories()) {
        if (!paths.contains(path)) {
            m_watcher->removePath(path);
        }
    }
    for (const QString &path : paths) {
        if (!m_watcher->directories().contains(path)) {
            m_watcher->addPath(path);
        }
    }
}

TrashManager::~TrashManager() = default;

int TrashManager::itemCount() const
{
    return m_itemCount;
}

bool TrashManager::isEmpty() const
{
    return m_empty;
}

TrashManager::Level TrashManager::level() const
{
    return m_level;
}

QString TrashManager::iconName() const
{
    return m_iconName;
}

void TrashManager::openTrash()
{
    auto *job = new KIO::OpenUrlJob(QUrl(QStringLiteral("trash:/")));
    startWithErrorNotifications(job);
}

void TrashManager::trashUrls(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    auto *job = KIO::trash(urls);
    if (!job) {
        return;
    }
    connect(job, &KJob::result, this, [this] {
        m_lister->openUrl(QUrl(QStringLiteral("trash:/")), KCoreDirLister::Reload);
    });
    startWithErrorNotifications(job);
}

void TrashManager::emptyTrash()
{
    auto *job = KIO::emptyTrash();
    if (!job) {
        return;
    }
    connect(job, &KJob::result, this, [this] {
        m_lister->openUrl(QUrl(QStringLiteral("trash:/")), KCoreDirLister::Reload);
    });
    startWithErrorNotifications(job);
}

void TrashManager::updateState()
{
    const int itemCount = m_lister ? m_lister->items().size() : 0;
    const bool empty = itemCount == 0;
    const Level level = itemCount == 0 ? Empty : itemCount <= s_fewMaximum ? Few : itemCount <= s_moderateMaximum ? Moderate : Many;

    if (m_itemCount != itemCount) {
        m_itemCount = itemCount;
        Q_EMIT itemCountChanged();
    }
    if (m_empty != empty) {
        m_empty = empty;
        Q_EMIT emptyChanged();
    }
    if (m_level != level) {
        m_level = level;
        Q_EMIT levelChanged();
    }
    updateIconName();
}

void TrashManager::updateIconName()
{
    const QString iconName = m_empty || !QIcon::hasThemeIcon(QStringLiteral("user-trash-full")) ? QStringLiteral("user-trash") : QStringLiteral("user-trash-full");
    if (m_iconName == iconName) {
        return;
    }
    m_iconName = iconName;
    Q_EMIT iconNameChanged();
}

} // namespace krema
