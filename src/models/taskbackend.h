// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QAbstractItemModel>
#include <QByteArray>
#include <QList>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QRect>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include <functional>

class QQmlEngine;

/**
 * Runtime access to Plasma's task manager and notification settings.
 *
 * libtaskmanager and libnotificationmanager (plasma-workspace) keep their
 * SONAME across Plasma releases but do not keep their C++ ABI: Plasma 6.8
 * removed roles from the middle of AbstractTasksModel::AdditionalRoles and a
 * virtual from AbstractTasksModelIface, so a Krema binary linked against 6.7
 * crashed in TasksModel::componentComplete() on 6.8. Krema therefore does not
 * link them. It instantiates the types through the QML modules
 * org.kde.taskmanager and org.kde.notificationmanager that plasma-workspace
 * installs next to the libraries, so every virtual call happens inside a
 * plugin built against the running Plasma. Krema only talks to the objects
 * through Qt's public QAbstractItemModel and meta-object interfaces, and
 * resolves roles, properties and methods by name at runtime.
 */
namespace krema::tasks
{

/// AbstractTasksModel role values of the running Plasma, resolved by name.
/// A role the running Plasma does not provide stays -1 (data() then returns
/// an invalid QVariant).
struct Roles {
    int AppId = -1;
    int AppName = -1;
    int GenericName = -1;
    int LauncherUrl = -1;
    int LauncherUrlWithoutIcon = -1;
    int WinIdList = -1;
    int MimeType = -1;
    int MimeData = -1;
    int IsWindow = -1;
    int IsStartup = -1;
    int IsLauncher = -1;
    int HasLauncher = -1;
    int IsGroupParent = -1;
    int ChildCount = -1;
    int IsGroupable = -1;
    int IsActive = -1;
    int IsClosable = -1;
    int IsMinimizable = -1;
    int IsMinimized = -1;
    int IsMaximizable = -1;
    int IsMaximized = -1;
    int IsFullScreen = -1;
    int IsVirtualDesktopsChangeable = -1;
    int VirtualDesktops = -1;
    int IsOnAllVirtualDesktops = -1;
    int ScreenGeometry = -1;
    int Activities = -1;
    int IsDemandingAttention = -1;
    int SkipTaskbar = -1;
    int SkipPager = -1;
    int AppPid = -1;
    int StackingOrder = -1;
    int LastActivated = -1;
    int ApplicationMenuServiceName = -1;
    int ApplicationMenuObjectPath = -1;
    int IsHidden = -1;
    int CanLaunchNewInstance = -1;
};

/// Roles of the running Plasma. Valid only after Backend::ensureLoaded()
/// succeeded; before that every role is -1.
[[nodiscard]] const Roles &roles();

/**
 * Process-wide loader for the Plasma QML modules.
 *
 * Owns the QQmlEngine that instantiates TasksModel, VirtualDesktopInfo,
 * ActivityInfo and NotificationManager Settings. The engine is parented to
 * the QCoreApplication and outlives every object it created; those objects
 * use C++ ownership and the parent the caller passes.
 */
class Backend
{
public:
    [[nodiscard]] static Backend &instance();

    /// Loads both QML modules once, resolves roles and checks every type,
    /// property and method Krema relies on. Returns false (and fills
    /// errorString()) when the running Plasma cannot provide them.
    bool ensureLoaded();
    [[nodiscard]] QString errorString() const;

    /// Creates the root object of @p qml (a QML document whose imports are
    /// provided by the caller), applying @p initialProperties before
    /// componentComplete(). Returns nullptr and logs the QML errors on
    /// failure. The object gets C++ ownership and @p parent.
    [[nodiscard]] QObject *create(const QByteArray &qml, const QVariantMap &initialProperties, QObject *parent);

    /// TasksModel from org.kde.taskmanager. @p qmlBody is inserted inside
    /// `TasksModel { ... }` (static configuration such as
    /// `groupMode: TasksModel.GroupApplications`); @p initialProperties are
    /// applied before componentComplete().
    [[nodiscard]] QAbstractItemModel *createTasksModel(const QByteArray &qmlBody, const QVariantMap &initialProperties, QObject *parent);
    [[nodiscard]] QObject *createVirtualDesktopInfo(QObject *parent);
    [[nodiscard]] QObject *createActivityInfo(QObject *parent);
    /// Notification settings object from org.kde.notificationmanager.
    [[nodiscard]] QObject *createNotificationSettings(QObject *parent);

    [[nodiscard]] QQmlEngine *engine();

private:
    Backend() = default;

    QPointer<QQmlEngine> m_engine;
    QString m_error;
    bool m_loaded = false;
    bool m_attempted = false;
};

/**
 * Typed facade over a TasksModel instance created by Backend.
 *
 * Non-owning and cheap to copy. Every call goes through the meta-object, so
 * a member missing from the running Plasma logs a warning and returns a
 * default value instead of crashing.
 */
class TasksModel
{
public:
    TasksModel() = default;
    explicit TasksModel(QAbstractItemModel *model);

    [[nodiscard]] QAbstractItemModel *model() const;
    [[nodiscard]] bool isValid() const;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const;
    [[nodiscard]] QModelIndex index(int row, int column = 0, const QModelIndex &parent = {}) const;
    [[nodiscard]] QModelIndex makeModelIndex(int row, int childRow = -1) const;
    [[nodiscard]] QPersistentModelIndex makePersistentModelIndex(int row, int childRow = -1) const;
    [[nodiscard]] QModelIndex activeTask() const;

    [[nodiscard]] QStringList launcherList() const;
    void setLauncherList(const QStringList &launchers) const;
    [[nodiscard]] int launcherPosition(const QUrl &url) const;
    bool requestAddLauncher(const QUrl &url) const;
    bool requestRemoveLauncher(const QUrl &url) const;
    void syncLaunchers() const;
    bool move(int row, int newPos, const QModelIndex &parent = {}) const;

    void requestActivate(const QModelIndex &index) const;
    void requestNewInstance(const QModelIndex &index) const;
    void requestOpenUrls(const QModelIndex &index, const QList<QUrl> &urls) const;
    void requestClose(const QModelIndex &index) const;
    void requestToggleMinimized(const QModelIndex &index) const;
    void requestToggleMaximized(const QModelIndex &index) const;
    void requestPublishDelegateGeometry(const QModelIndex &index, const QRect &geometry, QObject *delegate = nullptr) const;

    /// Generic property access (e.g. "filterByVirtualDesktop", "regionGeometry").
    [[nodiscard]] QVariant property(const char *name) const;
    void setProperty(const char *name, const QVariant &value) const;
    /// Sets an enum-typed property by key name, e.g.
    /// setEnumProperty("filterByRegion", "Intersect").
    void setEnumProperty(const char *name, const char *key) const;

private:
    QPointer<QAbstractItemModel> m_model;
};

/// Connects @p signal (a signal name without signature, e.g.
/// "launcherListChanged") of @p sender to @p slot for the lifetime of
/// @p context. Returns false and logs when the signal does not exist.
bool connectSignal(QObject *sender, const char *signal, QObject *context, std::function<void()> slot);

/// Same for the NOTIFY signal of property @p property.
bool connectNotify(QObject *sender, const char *property, QObject *context, std::function<void()> slot);

} // namespace krema::tasks
