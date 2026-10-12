# Desktop File Actions (KService + KIO)

Verified against the KF 6.30 headers in the Fedora 43 CI image
(`kf6-kservice-devel`, `kf6-kio-devel`, `kf6-kjobwidgets-devel`) and the
Plasma task manager (`plasma-desktop/applets/taskmanager/backend.cpp`,
`Backend::jumpListActions`; `plasma-workspace/libtaskmanager/tasktools.cpp`,
`runApp`). Krema uses it in `src/models/desktopactions.{h,cpp}` for the dock
item context menu.

## KService (`/usr/include/KF6/KService/kservice.h`)

```cpp
explicit KService(const QString &fullpath);      // parse a .desktop file directly (no KSycoca)
QList<KServiceAction> actions() const;           // "Only valid actions according to specification"; copies
static Ptr serviceByDesktopPath(const QString &_path);  // KSycoca lookup by entryPath()
static Ptr serviceByMenuId(const QString &_menuId);     // applications:<menuId> launcher URLs
static Ptr serviceByStorageId(const QString &_storageId);
static Ptr serviceByDesktopName(const QString &_name);  // ".desktop" implicit
```

- `actions()` follows the `Actions=` key order, not the group order in the file.
- It includes `NoDisplay=true` actions and `_SEPARATOR_` entries; callers filter.
- `serviceByDesktopPath()` returns `nullptr` for a desktop file outside the
  XDG applications directories; `KService(path)` reads it (check `isValid()`).

## KServiceAction (`/usr/include/KF6/KService/kserviceaction.h`)

```cpp
QString name() const;     // group name: [Desktop Action <name>]
QString text() const;     // localized Name=
QString icon() const;     // Icon= (may be empty)
QString exec() const;
bool noDisplay() const;   // suppress in menus
bool isSeparator() const; // "_SEPARATOR_" in Actions=
KServicePtr service() const; // since 5.69: the owning application
```

A `KServiceAction` is a value type (copyable) holding its `KService::Ptr`, so
it can be captured in a lambda and launched after the task row is gone.

## Launching (`/usr/include/KF6/KIOGui/kio/applicationlauncherjob.h`)

```cpp
explicit ApplicationLauncherJob(const KServiceAction &serviceAction, QObject *parent = nullptr);
void setStartupId(const QByteArray &startupId); // X11 startup id / Wayland XDG activation token
void start() override;                          // never exec(); auto-deletes after result
```

`KNotificationJobUiDelegate` lives in KJobWidgets
(`/usr/include/KF6/KJobWidgets/knotificationjobuidelegate.h`, `KF6::JobWidgets`),
not in KIO:

```cpp
explicit KNotificationJobUiDelegate(KJobUiDelegate::Flags flags = {KJobUiDelegate::AutoHandlingDisabled});
```

TaskManager launches apps (`TasksModel::requestNewInstance` →
`TaskManager::runApp`) as

```cpp
auto *job = new KIO::ApplicationLauncherJob(service);
job->setUiDelegate(new KNotificationJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled));
job->start();
```

Launch errors become KDE notifications. On Wayland, without `setStartupId()`
KIO's `KProcessRunner` requests an XDG activation token itself
(`KWaylandExtras::xdgActivationToken(qGuiApp->focusWindow() or the first window, …)`)
and passes it as `XDG_ACTIVATION_TOKEN`.

## Launcher URL → KService

`AbstractTasksModel::LauncherUrlWithoutIcon` is an `applications:<menuId>` URL
or a `file://…/<id>.desktop` URL (group parents report their first child's).
Plasma resolves `applications:` with `serviceByMenuId()` and file URLs with
`serviceByDesktopPath()`.

CMake: `find_package(KF6 COMPONENTS KIO JobWidgets)`, link `KF6::KIOGui`
and `KF6::JobWidgets`.
