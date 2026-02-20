# KStatusNotifierItem (System Tray)

> Source: `/usr/include/KF6/KStatusNotifierItem/` headers

## Overview

KStatusNotifierItem implements the Status Notifier Item D-Bus specification for system tray integration. Provides icons, menus, status indicators, and activation handling.

---

## Enums

### ItemStatus

```cpp
enum ItemStatus {
    Passive = 1,        // Hidden; not important
    Active = 2,         // Always visible; app is running
    NeedsAttention = 3, // Urgent; request attention (blinking)
};
```

### ItemCategory

```cpp
enum ItemCategory {
    ApplicationStatus = 1,  // Normal application (default)
    Communications = 2,     // IM, email
    SystemServices = 3,     // System daemon
    Hardware = 4,           // Hardware monitor, battery
    Reserved = 129,
};
```

---

## Constructor

```cpp
KStatusNotifierItem(QObject *parent = nullptr);                    // ID = app name
KStatusNotifierItem(const QString &id, QObject *parent = nullptr); // Custom ID
```

---

## Properties & Methods

### Identity

```cpp
QString id() const;                           // Unique identifier
void setTitle(const QString &title);          // Display title
QString title() const;

void setCategory(ItemCategory category);
ItemCategory category() const;

void setStatus(ItemStatus status);
ItemStatus status() const;
```

### Icon (Theme Name — Preferred)

```cpp
void setIconByName(const QString &name);
QString iconName() const;

void setOverlayIconByName(const QString &name);   // Badge overlay
QString overlayIconName() const;

void setAttentionIconByName(const QString &name);  // NeedsAttention icon
QString attentionIconName() const;

void setAttentionMovieByName(const QString &name);  // Animated attention
QString attentionMovieName() const;
```

### Icon (Pixmap — Fallback)

```cpp
void setIconByPixmap(const QIcon &icon);
QIcon iconPixmap() const;

void setOverlayIconByPixmap(const QIcon &icon);
QIcon overlayIconPixmap() const;

void setAttentionIconByPixmap(const QIcon &icon);
QIcon attentionIconPixmap() const;
```

### Tooltip

```cpp
// Set all tooltip fields at once
void setToolTip(const QString &iconName, const QString &title, const QString &subTitle);
void setToolTip(const QIcon &icon, const QString &title, const QString &subTitle);

// Individual fields
void setToolTipIconByName(const QString &name);
void setToolTipIconByPixmap(const QIcon &icon);
QString toolTipIconName() const;
QIcon toolTipIconPixmap() const;

void setToolTipTitle(const QString &title);
QString toolTipTitle() const;

void setToolTipSubTitle(const QString &subTitle);
QString toolTipSubTitle() const;
```

### Menu

```cpp
void setContextMenu(QMenu *menu);    // Right-click menu (app takes ownership)
QMenu *contextMenu() const;

void setIsMenu(bool isMenu);        // If true, shows menu on activate instead of window
bool isMenu() const;

void setStandardActionsEnabled(bool enabled);  // Show/hide default Quit action
bool standardActionsEnabled() const;
```

### Window Association

```cpp
void setAssociatedWindow(QWindow *window);  // Link to main window
QWindow *associatedWindow() const;
void hideAssociatedWindow();                // Explicit hide
```

### Notification

```cpp
void showMessage(const QString &title,
                 const QString &message,
                 const QString &icon,
                 int timeout = 10000);   // ms
```

### Activation

```cpp
// Public slot — toggle associated window visibility
void activate(const QPoint &pos = QPoint());
```

### Wayland Support

```cpp
QString providedToken() const;  // xdg_activation_v1 token for focus requests
```

### Quit Control

```cpp
void abortQuit();  // Cancel pending quit (call from quitRequested handler)
```

---

## Signals

| Signal | Description |
|--------|-------------|
| `activateRequested(bool active, QPoint pos)` | Click activation (`active`=show/hide, `pos`=screen coords) |
| `secondaryActivateRequested(QPoint pos)` | Right-click / alternate activation |
| `scrollRequested(int delta, Qt::Orientation)` | Scroll wheel on icon |
| `quitRequested()` | User triggered Quit; call `abortQuit()` to cancel |

---

## Deprecated API (KF6.6+)

Use `contextMenu()` instead:

```cpp
// Old way (deprecated)
void addAction(const QString &name, QAction *action);
void removeAction(const QString &name);
QAction *action(const QString &name) const;
QList<QAction *> actionCollection() const;

// New way
contextMenu()->addAction(action);
```

---

## Typical Usage

```cpp
#include <KStatusNotifierItem>

auto *tray = new KStatusNotifierItem("krema-dock", this);
tray->setCategory(KStatusNotifierItem::ApplicationStatus);
tray->setStatus(KStatusNotifierItem::Active);
tray->setTitle("Krema Dock");

// Icons
tray->setIconByName("krema");
tray->setAttentionIconByName("dialog-warning");

// Tooltip
tray->setToolTip("krema", "Krema Dock", "Running");

// Menu
auto *menu = new QMenu;
menu->addAction("Preferences...", this, &DockApp::showPreferences);
menu->addSeparator();
menu->addAction("Quit", qApp, &QCoreApplication::quit);
tray->setContextMenu(menu);

// Window association
tray->setAssociatedWindow(mainWindow);

// Handle activation
connect(tray, &KStatusNotifierItem::activateRequested,
        this, [this](bool active, const QPoint &pos) {
    if (active) showDock();
    else hideDock();
});

// Handle quit with confirmation
connect(tray, &KStatusNotifierItem::quitRequested,
        this, [tray]() {
    // Show confirmation dialog
    // If user cancels:
    tray->abortQuit();
});
```

---

## Design Notes

1. **Theme icon names preferred** over pixmaps — lighter, themeable
2. **Status levels**: Passive (hidden) -> Active (visible) -> NeedsAttention (urgent/blinking)
3. **D-Bus transport**: All communication automatic via `org.kde.StatusNotifierWatcher`
4. **Flatpak**: Requires `--talk-name=org.kde.StatusNotifierWatcher` permission
5. **Menu delegation**: App provides QMenu, system tray handles display
6. **Window lifecycle**: `activate()` auto-toggles associated window visibility
