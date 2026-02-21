# KGlobalAccel (Global Shortcuts)

> Source: `/usr/include/KF6/KGlobalAccel/` headers

## Overview

KGlobalAccel manages system-wide keyboard shortcuts that work across all applications. Shortcuts are stored in `~/.config/kglobalshortcutsrc` and managed via D-Bus.

---

## Basic Usage

```cpp
#include <KGlobalAccel>
#include <QAction>

// 1. Create a QAction with a unique objectName
auto *action = new QAction(this);
action->setObjectName("toggle-dock");    // Must be unique within component
action->setText(i18n("Toggle Dock"));     // Human-readable name for settings UI

// 2. Register global shortcut
KGlobalAccel::setGlobalShortcut(action, QKeySequence(Qt::META | Qt::Key_QuoteLeft));

// 3. Connect to triggered signal
connect(action, &QAction::triggered, this, &DockView::toggleVisibility);
```

---

## KGlobalAccel Class

### Singleton

```cpp
static KGlobalAccel *self();
```

### Shortcut Registration

```cpp
// Convenience: set shortcut with single key sequence
static bool setGlobalShortcut(QAction *action, const QKeySequence &shortcut);

// Full version: multiple key sequences
static bool setGlobalShortcut(QAction *action, const QList<QKeySequence> &shortcuts);

// Set default shortcut (user can override in System Settings)
bool setDefaultShortcut(QAction *action, const QList<QKeySequence> &shortcut,
                        GlobalShortcutLoading loadFlag = Autoloading);

// Set active shortcut
bool setShortcut(QAction *action, const QList<QKeySequence> &shortcut,
                 GlobalShortcutLoading loadFlag = Autoloading);
```

### Loading Flags

```cpp
enum GlobalShortcutLoading {
    Autoloading   = 0x0,  // Load from saved config if exists; use provided if first time
    NoAutoloading = 0x4,  // Always use provided shortcut, overwrite config
};
```

**Loading logic:**
- **First time** (no saved config): Both flags use the provided shortcut
- **Subsequent** (config exists):
  - `Autoloading`: Ignores provided, loads from config
  - `NoAutoloading`: Uses provided, overwrites config

### Querying

```cpp
QList<QKeySequence> shortcut(const QAction *action) const;
QList<QKeySequence> defaultShortcut(const QAction *action) const;
QList<QKeySequence> globalShortcut(const QString &componentName, const QString &actionId) const;
bool hasShortcut(const QAction *action) const;
```

### Removal

```cpp
void removeAllShortcuts(QAction *action);
```

### Conflict Handling

```cpp
// Check if key sequence is available
static bool isGlobalShortcutAvailable(const QKeySequence &seq,
                                       const QString &component = QString());

// Get all shortcuts using this key
static QList<KGlobalShortcutInfo> globalShortcutsByKey(const QKeySequence &seq,
                                                        MatchType type = Equal);

// Prompt user to steal shortcut from other app
static bool promptStealShortcutSystemwide(QWidget *parent,
                                           const QList<KGlobalShortcutInfo> &shortcuts,
                                           const QKeySequence &seq);

// Force steal without prompt
static void stealShortcutSystemwide(const QKeySequence &seq);
```

### Match Types (for multi-key sequences)

```cpp
enum MatchType {
    Equal,     // Exact match
    Shadows,   // New shortcut shadows existing
    Shadowed,  // Existing shadows new
};
```

### Signals

```cpp
void globalShortcutChanged(QAction *action, const QKeySequence &seq);
void globalShortcutActiveChanged(QAction *action, bool active);  // Press/release
```

### Component Management

```cpp
static bool cleanComponent(const QString &componentUnique);  // Remove orphaned shortcuts
static bool isComponentActive(const QString &componentName);
```

---

## KGlobalShortcutInfo

Information about a registered global shortcut.

```cpp
class KGlobalShortcutInfo {
    QString uniqueName() const;               // Action ID
    QString friendlyName() const;             // Action display name
    QString componentUniqueName() const;      // Component ID
    QString componentFriendlyName() const;    // Component display name
    QList<QKeySequence> keys() const;         // Active shortcuts
    QList<QKeySequence> defaultKeys() const;  // Default shortcuts
};
```

---

## QAction Requirements

For global shortcuts to work correctly:

1. **`objectName()`** — Must be unique within the component, must never change
2. **`setText()`** — Human-readable description shown in System Settings > Shortcuts
3. **Lifetime** — QAction must remain alive while shortcut is active
4. **Component** — Automatically derived from `KAboutData::componentName()`

---

## Storage

- **Config file:** `~/.config/kglobalshortcutsrc`
- **Format:** INI with `[component_name]` groups
- **D-Bus service:** `org.kde.kglobalaccel` (manages active shortcuts)

---

## Krema Shortcuts

| Action ID | Default Key | Description |
|-----------|------------|-------------|
| `toggle-dock` | `Meta+`` | Toggle dock visibility |
| `activate-entry-1..9` | `Meta+1..9` | Activate Nth dock entry |
| `new-instance-entry-1..9` | `Meta+Shift+1..9` | Launch new instance of Nth app |

### Registration Pattern

```cpp
void Application::registerGlobalShortcuts() {
    // Toggle dock
    auto *toggleAction = new QAction(this);
    toggleAction->setObjectName("toggle-dock");
    toggleAction->setText(i18n("Toggle Dock"));
    KGlobalAccel::setGlobalShortcut(toggleAction, QKeySequence(Qt::META | Qt::Key_QuoteLeft));
    connect(toggleAction, &QAction::triggered, m_dockView, &DockView::toggleVisibility);

    // Numbered entry activation (Meta+1..9)
    for (int i = 1; i <= 9; ++i) {
        auto *action = new QAction(this);
        action->setObjectName(QStringLiteral("activate-entry-%1").arg(i));
        action->setText(i18n("Activate Entry %1", i));
        KGlobalAccel::setGlobalShortcut(action, QKeySequence(Qt::META | (Qt::Key_1 + i - 1)));
        connect(action, &QAction::triggered, this, [this, i] {
            m_dockModel->activateEntry(i - 1);
        });
    }
}
```
