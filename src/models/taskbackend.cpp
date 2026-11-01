// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "taskbackend.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMetaEnum>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlEngine>

#include <memory>

Q_LOGGING_CATEGORY(lcTaskBackend, "krema.tasks")

namespace krema::tasks
{

namespace
{

Roles g_roles;

struct RoleField {
    const char *name;
    int Roles::*field;
    bool required;
};

// Roles Krema cannot work without are required: a running Plasma missing one
// fails ensureLoaded(). The others degrade the feature that reads them.
constexpr RoleField kRoleFields[] = {
    {"AppId", &Roles::AppId, true},
    {"AppName", &Roles::AppName, false},
    {"GenericName", &Roles::GenericName, false},
    {"LauncherUrl", &Roles::LauncherUrl, true},
    {"LauncherUrlWithoutIcon", &Roles::LauncherUrlWithoutIcon, true},
    {"WinIdList", &Roles::WinIdList, true},
    {"MimeType", &Roles::MimeType, false},
    {"MimeData", &Roles::MimeData, false},
    {"IsWindow", &Roles::IsWindow, true},
    {"IsStartup", &Roles::IsStartup, false},
    {"IsLauncher", &Roles::IsLauncher, true},
    {"HasLauncher", &Roles::HasLauncher, false},
    {"IsGroupParent", &Roles::IsGroupParent, true},
    {"ChildCount", &Roles::ChildCount, false},
    {"IsGroupable", &Roles::IsGroupable, false},
    {"IsActive", &Roles::IsActive, true},
    {"IsClosable", &Roles::IsClosable, false},
    {"IsMinimizable", &Roles::IsMinimizable, false},
    {"IsMinimized", &Roles::IsMinimized, true},
    {"IsMaximizable", &Roles::IsMaximizable, false},
    {"IsMaximized", &Roles::IsMaximized, false},
    {"IsFullScreen", &Roles::IsFullScreen, false},
    {"IsVirtualDesktopsChangeable", &Roles::IsVirtualDesktopsChangeable, false},
    {"VirtualDesktops", &Roles::VirtualDesktops, false},
    {"IsOnAllVirtualDesktops", &Roles::IsOnAllVirtualDesktops, false},
    {"ScreenGeometry", &Roles::ScreenGeometry, false},
    {"Activities", &Roles::Activities, false},
    {"IsDemandingAttention", &Roles::IsDemandingAttention, false},
    {"SkipTaskbar", &Roles::SkipTaskbar, false},
    {"SkipPager", &Roles::SkipPager, false},
    {"AppPid", &Roles::AppPid, false},
    {"StackingOrder", &Roles::StackingOrder, false},
    {"LastActivated", &Roles::LastActivated, false},
    {"ApplicationMenuServiceName", &Roles::ApplicationMenuServiceName, false},
    {"ApplicationMenuObjectPath", &Roles::ApplicationMenuObjectPath, false},
    {"IsHidden", &Roles::IsHidden, false},
    {"CanLaunchNewInstance", &Roles::CanLaunchNewInstance, false},
};

// Members of the plugin types Krema calls. Checked once against the running
// Plasma so a renamed member shows up as one clear startup error instead of
// a silently missing feature.
constexpr const char *kTasksModelProperties[] = {
    "launcherList",           "virtualDesktop", "screenGeometry",  "regionGeometry",        "activity",   "filterByVirtualDesktop", "filterByScreen",
    "filterByActivity",       "filterByRegion", "filterMinimized", "filterHidden",          "sortMode",   "separateLaunchers",      "launchInPlace",
    "hideActivatedLaunchers", "groupMode",      "groupInline",     "taskReorderingEnabled", "activeTask",
};
constexpr const char *kTasksModelMethods[] = {
    "requestAddLauncher",
    "requestRemoveLauncher",
    "launcherPosition",
    "requestActivate",
    "requestNewInstance",
    "requestOpenUrls",
    "requestClose",
    "requestToggleMinimized",
    "requestToggleMaximized",
    "requestPublishDelegateGeometry",
    "move",
    "syncLaunchers",
    "makeModelIndex",
    "makePersistentModelIndex",
};
constexpr const char *kVirtualDesktopInfoProperties[] = {"currentDesktop", "desktopIds"};
constexpr const char *kActivityInfoProperties[] = {"currentActivity"};
constexpr const char *kSettingsProperties[] = {
    "badgesInTaskManager",
    "badgeBlacklistedApplications",
    "notificationsInhibitedUntil",
    "notificationsInhibitedByApplication",
};

QByteArray rolesProbeSource()
{
    QByteArray qml = "import QtQml\nimport org.kde.taskmanager as TaskManager\nQtObject {\n";
    for (const RoleField &role : kRoleFields) {
        qml += "    property var role_";
        qml += role.name;
        qml += ": TaskManager.AbstractTasksModel.";
        qml += role.name;
        qml += "\n";
    }
    qml += "}\n";
    return qml;
}

bool hasMethod(const QMetaObject *meta, const char *name)
{
    for (int i = 0; i < meta->methodCount(); ++i) {
        if (meta->method(i).name() == name) {
            return true;
        }
    }
    return false;
}

template<std::size_t N>
QStringList missingMembers(const QObject *object, const char *const (&properties)[N])
{
    QStringList missing;
    for (const char *property : properties) {
        if (object->metaObject()->indexOfProperty(property) < 0) {
            missing << QString::fromLatin1(property);
        }
    }
    return missing;
}

void reportMissing(const char *type, const QStringList &missing)
{
    if (!missing.isEmpty()) {
        qCCritical(lcTaskBackend) << type << "of the running Plasma lacks" << missing.join(QLatin1String(", "))
                                  << "- the dock features using them will not work";
    }
}

class SignalRelay : public QObject
{
    Q_OBJECT

public:
    SignalRelay(std::function<void()> slot, QObject *parent)
        : QObject(parent)
        , m_slot(std::move(slot))
    {
    }

public Q_SLOTS:
    void relay()
    {
        m_slot();
    }

private:
    std::function<void()> m_slot;
};

bool connectMethod(QObject *sender, const QMetaMethod &signal, QObject *context, std::function<void()> slot)
{
    auto *relay = new SignalRelay(std::move(slot), context);
    const QMetaMethod relayMethod = relay->metaObject()->method(relay->metaObject()->indexOfSlot("relay()"));
    if (!QObject::connect(sender, signal, relay, relayMethod)) {
        delete relay;
        return false;
    }
    // A relay must not outlive its sender either.
    QObject::connect(sender, &QObject::destroyed, relay, &QObject::deleteLater);
    return true;
}

} // namespace

const Roles &roles()
{
    return g_roles;
}

Backend &Backend::instance()
{
    static Backend backend;
    return backend;
}

QQmlEngine *Backend::engine()
{
    if (!m_engine) {
        m_engine = new QQmlEngine(QCoreApplication::instance());
    }
    return m_engine;
}

QString Backend::errorString() const
{
    return m_error;
}

bool Backend::ensureLoaded()
{
    if (m_attempted) {
        return m_loaded;
    }
    m_attempted = true;

    std::unique_ptr<QObject> probe(create(rolesProbeSource(), {}, nullptr));
    if (!probe) {
        m_error = QStringLiteral("The QML module org.kde.taskmanager (plasma-workspace) could not be loaded");
        return false;
    }
    QStringList missingRequired;
    QStringList missingOptional;
    for (const RoleField &role : kRoleFields) {
        bool ok = false;
        const int value = probe->property(QByteArray("role_").append(role.name).constData()).toInt(&ok);
        if (ok) {
            g_roles.*role.field = value;
        } else {
            (role.required ? missingRequired : missingOptional) << QString::fromLatin1(role.name);
        }
    }
    if (!missingOptional.isEmpty()) {
        qCWarning(lcTaskBackend) << "AbstractTasksModel of the running Plasma lacks the roles" << missingOptional.join(QLatin1String(", "));
    }
    if (!missingRequired.isEmpty()) {
        m_error = QStringLiteral("The running Plasma task manager lacks the roles %1").arg(missingRequired.join(QLatin1String(", ")));
        return false;
    }

    std::unique_ptr<QObject> settingsProbe(createNotificationSettings(nullptr));
    if (!settingsProbe) {
        m_error = QStringLiteral("The QML module org.kde.notificationmanager (plasma-workspace) could not be loaded");
        return false;
    }

    m_loaded = true;
    return true;
}

QObject *Backend::create(const QByteArray &qml, const QVariantMap &initialProperties, QObject *parent)
{
    QQmlComponent component(engine());
    component.setData(qml, QUrl::fromLocalFile(QStringLiteral("/krema/taskbackend.qml")));
    if (component.isError()) {
        qCCritical(lcTaskBackend).noquote() << component.errorString();
        return nullptr;
    }
    QObject *object = component.createWithInitialProperties(initialProperties);
    if (!object) {
        qCCritical(lcTaskBackend).noquote() << component.errorString();
        return nullptr;
    }
    QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
    object->setParent(parent);
    return object;
}

QAbstractItemModel *Backend::createTasksModel(const QByteArray &qmlBody, const QVariantMap &initialProperties, QObject *parent)
{
    QObject *object = create("import org.kde.taskmanager\nTasksModel {\n" + qmlBody + "\n}\n", initialProperties, parent);
    auto *model = qobject_cast<QAbstractItemModel *>(object);
    if (object && !model) {
        delete object;
        return nullptr;
    }
    if (model) {
        static bool checked = false;
        if (!checked) {
            checked = true;
            QStringList missing = missingMembers(model, kTasksModelProperties);
            for (const char *method : kTasksModelMethods) {
                if (!hasMethod(model->metaObject(), method)) {
                    missing << QString::fromLatin1(method);
                }
            }
            reportMissing("TasksModel", missing);
        }
    }
    return model;
}

QObject *Backend::createVirtualDesktopInfo(QObject *parent)
{
    QObject *object = create("import org.kde.taskmanager\nVirtualDesktopInfo {}\n", {}, parent);
    if (object) {
        static bool checked = false;
        if (!checked) {
            checked = true;
            reportMissing("VirtualDesktopInfo", missingMembers(object, kVirtualDesktopInfoProperties));
        }
    }
    return object;
}

QObject *Backend::createActivityInfo(QObject *parent)
{
    QObject *object = create("import org.kde.taskmanager\nActivityInfo {}\n", {}, parent);
    if (object) {
        static bool checked = false;
        if (!checked) {
            checked = true;
            reportMissing("ActivityInfo", missingMembers(object, kActivityInfoProperties));
        }
    }
    return object;
}

QObject *Backend::createNotificationSettings(QObject *parent)
{
    QObject *object = create("import org.kde.notificationmanager\nSettings {}\n", {}, parent);
    if (object) {
        static bool checked = false;
        if (!checked) {
            checked = true;
            QStringList missing = missingMembers(object, kSettingsProperties);
            if (!hasMethod(object->metaObject(), "load")) {
                missing << QStringLiteral("load");
            }
            reportMissing("NotificationManager Settings", missing);
        }
    }
    return object;
}

TasksModel::TasksModel(QAbstractItemModel *model)
    : m_model(model)
{
}

QAbstractItemModel *TasksModel::model() const
{
    return m_model;
}

bool TasksModel::isValid() const
{
    return m_model;
}

namespace
{

template<typename R, typename... Args>
R invokeReturning(QObject *object, const char *method, Args &&...args)
{
    R result{};
    if (!object || !QMetaObject::invokeMethod(object, method, Qt::DirectConnection, qReturnArg(result), std::forward<Args>(args)...)) {
        qCWarning(lcTaskBackend) << "TasksModel::" << method << "is not available";
    }
    return result;
}

template<typename... Args>
void invoke(QObject *object, const char *method, Args &&...args)
{
    if (!object || !QMetaObject::invokeMethod(object, method, Qt::DirectConnection, std::forward<Args>(args)...)) {
        qCWarning(lcTaskBackend) << "TasksModel::" << method << "is not available";
    }
}

} // namespace

int TasksModel::rowCount(const QModelIndex &parent) const
{
    return m_model ? m_model->rowCount(parent) : 0;
}

QModelIndex TasksModel::index(int row, int column, const QModelIndex &parent) const
{
    return m_model ? m_model->index(row, column, parent) : QModelIndex();
}

QModelIndex TasksModel::makeModelIndex(int row, int childRow) const
{
    return invokeReturning<QModelIndex>(m_model, "makeModelIndex", row, childRow);
}

QPersistentModelIndex TasksModel::makePersistentModelIndex(int row, int childRow) const
{
    return invokeReturning<QPersistentModelIndex>(m_model, "makePersistentModelIndex", row, childRow);
}

QModelIndex TasksModel::activeTask() const
{
    return property("activeTask").value<QModelIndex>();
}

QStringList TasksModel::launcherList() const
{
    return property("launcherList").toStringList();
}

void TasksModel::setLauncherList(const QStringList &launchers) const
{
    setProperty("launcherList", launchers);
}

int TasksModel::launcherPosition(const QUrl &url) const
{
    if (!m_model) {
        return -1;
    }
    int position = -1;
    if (!QMetaObject::invokeMethod(m_model, "launcherPosition", Qt::DirectConnection, qReturnArg(position), url)) {
        qCWarning(lcTaskBackend) << "TasksModel::launcherPosition is not available";
        return -1;
    }
    return position;
}

bool TasksModel::requestAddLauncher(const QUrl &url) const
{
    return invokeReturning<bool>(m_model, "requestAddLauncher", url);
}

bool TasksModel::requestRemoveLauncher(const QUrl &url) const
{
    return invokeReturning<bool>(m_model, "requestRemoveLauncher", url);
}

void TasksModel::syncLaunchers() const
{
    invoke(m_model, "syncLaunchers");
}

bool TasksModel::move(int row, int newPos, const QModelIndex &parent) const
{
    return invokeReturning<bool>(m_model, "move", row, newPos, parent);
}

void TasksModel::requestActivate(const QModelIndex &index) const
{
    invoke(m_model, "requestActivate", index);
}

void TasksModel::requestNewInstance(const QModelIndex &index) const
{
    invoke(m_model, "requestNewInstance", index);
}

void TasksModel::requestOpenUrls(const QModelIndex &index, const QList<QUrl> &urls) const
{
    invoke(m_model, "requestOpenUrls", index, urls);
}

void TasksModel::requestClose(const QModelIndex &index) const
{
    invoke(m_model, "requestClose", index);
}

void TasksModel::requestToggleMinimized(const QModelIndex &index) const
{
    invoke(m_model, "requestToggleMinimized", index);
}

void TasksModel::requestToggleMaximized(const QModelIndex &index) const
{
    invoke(m_model, "requestToggleMaximized", index);
}

void TasksModel::requestPublishDelegateGeometry(const QModelIndex &index, const QRect &geometry, QObject *delegate) const
{
    invoke(m_model, "requestPublishDelegateGeometry", index, geometry, delegate);
}

QVariant TasksModel::property(const char *name) const
{
    return m_model ? m_model->property(name) : QVariant();
}

void TasksModel::setProperty(const char *name, const QVariant &value) const
{
    if (m_model && !m_model->setProperty(name, value)) {
        qCWarning(lcTaskBackend) << "TasksModel property" << name << "is not writable";
    }
}

void TasksModel::setEnumProperty(const char *name, const char *key) const
{
    if (!m_model) {
        return;
    }
    const QMetaObject *meta = m_model->metaObject();
    const int index = meta->indexOfProperty(name);
    if (index < 0) {
        qCWarning(lcTaskBackend) << "TasksModel property" << name << "is not available";
        return;
    }
    const QMetaProperty property = meta->property(index);
    bool ok = false;
    const int value = property.enumerator().isValid() ? property.enumerator().keyToValue(key, &ok) : -1;
    if (!ok) {
        qCWarning(lcTaskBackend) << "TasksModel property" << name << "has no value" << key;
        return;
    }
    property.write(m_model, value);
}

bool connectSignal(QObject *sender, const char *signal, QObject *context, std::function<void()> slot)
{
    if (!sender) {
        return false;
    }
    const QMetaObject *meta = sender->metaObject();
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() == QMetaMethod::Signal && method.name() == signal) {
            return connectMethod(sender, method, context, std::move(slot));
        }
    }
    qCWarning(lcTaskBackend) << meta->className() << "has no signal" << signal;
    return false;
}

bool connectNotify(QObject *sender, const char *property, QObject *context, std::function<void()> slot)
{
    if (!sender) {
        return false;
    }
    const int index = sender->metaObject()->indexOfProperty(property);
    const QMetaMethod notify = index >= 0 ? sender->metaObject()->property(index).notifySignal() : QMetaMethod();
    if (!notify.isValid()) {
        qCWarning(lcTaskBackend) << sender->metaObject()->className() << "has no notifiable property" << property;
        return false;
    }
    return connectMethod(sender, notify, context, std::move(slot));
}

} // namespace krema::tasks

#include "taskbackend.moc"
