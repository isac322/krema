# KDE Settings Window Patterns

**Topic**: How KDE/Qt 6 applications implement settings/preferences windows
**Verified**: KF6 headers + installed QML modules in `/usr/lib/qt6/qml/`

---

## Overview of Available Approaches

### 1. KConfigDialog (KConfigWidgets)

**Header**: `<KF6/KConfigWidgets/KConfigDialog>`
**Inheritance**: `QObject → QDialog → KPageDialog → KConfigDialog`
**Use case**: QWidget-based KDE apps

```cpp
void MyApp::showSettings() {
    if (KConfigDialog::showDialog(QStringLiteral("settings"))) {
        return; // already open → raise
    }
    auto *dialog = new KConfigDialog(this, QStringLiteral("settings"), MySettings::self());
    dialog->addPage(new GeneralPage(), i18n("General"), "preferences-system");
    connect(dialog, &KConfigDialog::settingsChanged, this, &MyApp::loadSettings);
    dialog->show();
}
```

Key features:
- `KConfigDialog::showDialog(name)` — singleton by name, raises if already open
- `KConfigDialog::exists(name)` — check without raising
- Auto-manages Apply/OK/Cancel buttons via `KConfigDialogManager`
- Widget auto-binding: widget named `kcfg_MyOption` ↔ config entry `MyOption`

**Not for QML apps.** This is a widgets-only API.

---

### 2. KQuickConfigModule / KCMUtils (System Settings plugin)

**Headers**: `/usr/include/KF6/KCMUtilsQuick/`
**Use case**: Embedding config modules into KDE System Settings or `kcmshell6`

```cpp
// KQuickConfigModule: base class for QML KCMs
class KCMUTILSQUICK_EXPORT KQuickConfigModule : public KAbstractConfigModule {
    Q_PROPERTY(QQuickItem *mainUi READ mainUi CONSTANT)
    Q_PROPERTY(int columnWidth ...)
    Q_PROPERTY(int depth ...)
    // push()/pop() for page navigation
};

// Loader: KQuickConfigModuleLoader::loadModule(metaData, parent, args, engine)
```

KCM lifecycle: `load()` (on open) → `save()` (on Apply/OK) → `defaults()` (on Defaults)

This is for **System Settings plugins**, not standalone settings windows. Overkill for a simple app.

---

### 3. ConfigurationView + ConfigurationModule (Kirigami Addons) ← RECOMMENDED

**QML module**: `org.kde.kirigamiaddons.settings`
**Source**: `/usr/lib/qt6/qml/org/kde/kirigamiaddons/settings/`

This is the standard pattern for modern Kirigami QML apps.

#### ConfigurationModule — module descriptor

```qml
KirigamiSettings.ConfigurationModule {
    moduleId: "appearance"
    text: i18nc("@action:button", "Appearance")
    icon.name: "preferences-desktop-theme-global"
    page: () => Qt.createComponent("org.kde.myapp", "AppearancePage")
    initialProperties: () => ({ someParam: value })
    visible: true
    category: "_main_category"  // for grouping on mobile
}
```

#### ConfigurationView — controller

```qml
KirigamiSettings.ConfigurationView {
    id: configuration
    window: mainWindow  // required: parent Kirigami.ApplicationWindow

    modules: [
        KirigamiSettings.ConfigurationModule { ... },
        ...
    ]

    // Open settings, optionally pre-selecting a module
    // configuration.open()
    // configuration.open("appearance")
}
```

**What open() does internally:**
- Desktop: `Qt.createComponent('...private', 'ConfigWindow')` → `component.createObject(root.window, {...})`
- Mobile: pushes `ConfigMobilePage` onto pageStack
- Sets `root.configViewItem` to the created window/page
- If already open (`configViewItem != null`): calls `Qt.callLater(configViewItem.requestActivate)` instead

**ConfigWindow.qml** (`settings/private/ConfigWindow.qml`):
- A `Kirigami.ApplicationWindow` with sidebar (list of modules) + page stack
- Search field, `modality: Qt.WindowModal`
- Page cache (`pageCache` object, keyed by `moduleId`)
- Sidebar width: `Kirigami.Units.gridUnit * 13`
- Default window size: `gridUnit * 50` × `gridUnit * 30`

#### Key timing property

`ConfigurationView.configViewItem` is set **synchronously** after `open()`:
- `Qt.createComponent()` for installed QML modules = synchronous (Component.Ready immediately)
- `component.createObject()` = synchronous
- Therefore: after `QMetaObject::invokeMethod(configView, "open")` returns, `configViewItem` is already set

From C++:
```cpp
QMetaObject::invokeMethod(configView, "open");
QVariant item = configView->property("configViewItem");
QQuickWindow *win = qvariant_cast<QQuickWindow *>(item);
// win is valid immediately — no polling needed
```

---

### 4. StatefulWindow + AbstractKirigamiApplication (full app pattern)

**QML module**: `org.kde.kirigamiaddons.statefulapp`
**Use case**: Kirigami apps that have a main window

```qml
StatefulApp.StatefulWindow {
    id: root
    windowName: 'Main'
    application: MyApp {
        configurationView: KirigamiSettings.ConfigurationView {
            modules: [ ... ]
        }
    }
}
```

`AbstractKirigamiApplication.configurationView` property: when set, automatically creates a "Configure…" (`options_configure`) action that calls `configurationView.open()`.

**Not applicable for Krema**: dock has no main `ApplicationWindow` (it's a layer-shell surface).

---

## Krema's Current Implementation

**Files**: `src/shell/settingswindow.h`, `src/shell/settingswindow.cpp`, `src/qml/SettingsDialog.qml`

### Architecture

```
SettingsWindow (C++)
  └── QQmlApplicationEngine (m_engine)
        └── SettingsDialog.qml (hidden host Kirigami.ApplicationWindow, visible: false)
              └── ConfigurationView (objectName: "configuration")
                    └── ConfigWindow.qml (actual visible window, created by open())
```

### Current flow (show())

1. `ensureEngine()` — lazy-create `QQmlApplicationEngine`, load `SettingsDialog.qml`
2. Find `configView = root->findChild<QObject*>("configuration")`
3. `QMetaObject::invokeMethod(configView, "open")`
4. `QTimer::singleShot(0, ...)` → `findAndTrackConfigWindow(attempt=0)`
5. `findAndTrackConfigWindow` loops through `QGuiApplication::allWindows()` looking for the ConfigWindow
6. Retries up to 10 times at 50ms intervals if not found immediately

### Problem with the current approach

The **polling retry loop is unnecessary**. Since `Qt.createComponent()` + `createObject()` for
installed QML modules runs synchronously, `configViewItem` is set immediately after `invokeMethod`.
The window can be retrieved directly:

```cpp
QMetaObject::invokeMethod(configView, "open");
auto *win = qvariant_cast<QQuickWindow *>(configView->property("configViewItem"));
// win != nullptr immediately — no retry needed
```

The `QGuiApplication::allWindows()` search is also fragile (relies on exclusion heuristics).

### Recommended Fix

Replace `findAndTrackConfigWindow()` with direct property access:

```cpp
void SettingsWindow::show(const QString &defaultModule)
{
    ensureEngine();

    if (m_configWindow) {
        m_configWindow->show();
        m_configWindow->raise();
        m_configWindow->requestActivate();
        return;
    }

    if (m_engine->rootObjects().isEmpty()) {
        m_engine->load(QUrl(QStringLiteral("qrc:/qml/SettingsDialog.qml")));
        if (m_engine->rootObjects().isEmpty()) { return; }
    }

    auto *root = m_engine->rootObjects().first();
    auto *configView = root->findChild<QObject *>(QStringLiteral("configuration"));
    if (!configView) { return; }

    QMetaObject::invokeMethod(configView, "open",
                              Q_ARG(QVariant, defaultModule.isEmpty() ? QVariant() : QVariant(defaultModule)));

    // configViewItem is set synchronously by open() — no polling needed
    auto *win = qvariant_cast<QQuickWindow *>(configView->property("configViewItem"));
    if (!win) { return; }

    m_configWindow = win;
    win->setIcon(QGuiApplication::windowIcon());

    connect(win, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible) {
            Q_EMIT visibleChanged(false);
            m_configWindow = nullptr;
        }
    });

    win->show();
    win->raise();
    win->requestActivate();
    Q_EMIT visibleChanged(true);
}
```

### Alternative: Direct window architecture (no hidden host)

Instead of a hidden host ApplicationWindow, Krema could load a `KremaSettingsWindow.qml`
that is itself the window (like `ConfigWindow.qml` but with modules defined inline).

```cpp
// C++: load the settings window as root object
m_engine->load(QUrl("qrc:/qml/KremaSettingsWindow.qml"));
// rootObjects().first() IS the settings window — no searching needed
m_configWindow = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
```

This eliminates the hidden host pattern entirely. Trade-off: loses the reusable
`ConfigurationView` abstraction and must replicate the sidebar navigation manually
(or copy from `ConfigWindow.qml`).

---

## Why Retry Loop = Bad Architecture

The retry loop (`findAndTrackConfigWindow` with 10 attempts × 50ms) is a sign that:
1. The code doesn't know **which window** it created (looking by exclusion in all windows)
2. It assumes async window creation when it's actually sync
3. Up to 500ms additional latency for no reason
4. Fragile: breaks if other QQuickWindows exist in the process

The correct pattern: **keep a direct reference** to whatever was created.
For `ConfigurationView`: read `configViewItem` immediately after `open()`.
For a directly-loaded QML window: `engine.rootObjects().first()`.

---

## Notes

- `KConfigDialog` is widgets-only, not for QML apps
- `KQuickConfigModule` is for System Settings plugins, not standalone settings windows
- `kcmshell6` can run KCMs standalone but requires the plugin infrastructure
- `ConfigWindow.qml` has `modality: Qt.WindowModal` — it's modal relative to its parent window
- The hidden host `ApplicationWindow` serves as the transient parent for `ConfigWindow`
  (needed for `Qt.WindowModal` to work correctly on Wayland)
- A completely hidden parent window is still valid: it provides window parenting context
  without appearing on screen
