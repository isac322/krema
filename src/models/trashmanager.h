// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

class KCoreDirLister;
class QFileSystemWatcher;

namespace krema
{

/**
 * Tracks the user's Trash location and exposes the operations used by the
 * native Trash dock item.
 *
 * The item count is maintained from KCoreDirLister's change signals rather
 * than polling. KIO jobs report failures through the standard notification
 * job UI delegate.
 */
class TrashManager : public QObject
{
    Q_OBJECT
public:
    enum Level {
        Empty = 0,
        Few,
        Moderate,
        Many,
    };
    Q_ENUM(Level)

    Q_PROPERTY(int itemCount READ itemCount NOTIFY itemCountChanged)
    Q_PROPERTY(bool empty READ isEmpty NOTIFY emptyChanged)
    Q_PROPERTY(Level level READ level NOTIFY levelChanged)
    Q_PROPERTY(QString iconName READ iconName NOTIFY iconNameChanged)

    explicit TrashManager(QObject *parent = nullptr);
    ~TrashManager() override;

    [[nodiscard]] int itemCount() const;
    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] Level level() const;
    [[nodiscard]] QString iconName() const;

    Q_INVOKABLE void openTrash();
    Q_INVOKABLE void trashUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void emptyTrash();

Q_SIGNALS:
    void itemCountChanged();
    void emptyChanged();
    void levelChanged();
    void iconNameChanged();

private:
    void updateTrashWatcher();
    void updateState();
    void updateIconName();

    QFileSystemWatcher *m_watcher = nullptr;
    KCoreDirLister *m_lister = nullptr;
    int m_itemCount = 0;
    bool m_empty = true;
    Level m_level = Empty;
    QString m_iconName;
};

} // namespace krema
