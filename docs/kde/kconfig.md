# KConfig (Configuration)

> Source: `/usr/include/KF6/KConfigCore/` headers

## Overview

KConfig provides INI-style persistent configuration. The core classes are:
- **KConfig** — Main config file access
- **KSharedConfig** — Reference-counted shared variant (preferred)
- **KConfigGroup** — Group-based key-value access
- **KConfigWatcher** — D-Bus change notifications

---

## Config File Locations

| Location | Path | Usage |
|----------|------|-------|
| App config | `~/.config/<appname>rc` | Primary settings (e.g., `~/.config/kremarc`) |
| State config | `~/.local/share/<appname>/<appname>rc` | Window positions, etc. |
| Global KDE | `~/.config/kdeglobals` | System-wide KDE settings |
| System-wide | `/etc/xdg/*` | Admin defaults (with cascade) |

---

## KSharedConfig (Recommended Entry Point)

```cpp
#include <KSharedConfig>

// Open app config (default: ~/.config/<appname>rc)
KSharedConfig::Ptr config = KSharedConfig::openConfig("kremarc");

// Open state config (for non-critical state like window geometry)
KSharedConfig::Ptr state = KSharedConfig::openStateConfig("krema");
```

### Factory Methods

```cpp
static KSharedConfig::Ptr openConfig(
    const QString &fileName = QString(),             // empty = QCoreApplication::applicationName()
    OpenFlags mode = FullConfig,                     // FullConfig, SimpleConfig, etc.
    QStandardPaths::StandardLocation type = QStandardPaths::GenericConfigLocation);

static KSharedConfig::Ptr openStateConfig(const QString &fileName = QString());
```

---

## KConfig OpenFlags

```cpp
enum OpenFlag {
    IncludeGlobals = 0x01,  // Merge with kdeglobals
    CascadeConfig  = 0x02,  // Cascade to system-wide configs
    SimpleConfig   = 0x00,  // Single file only
    NoCascade      = IncludeGlobals,
    NoGlobals      = CascadeConfig,
    FullConfig     = IncludeGlobals | CascadeConfig,  // Default: merge all
};
```

---

## KConfigGroup (Read/Write)

### Getting a Group

```cpp
#include <KConfigGroup>

KConfigGroup group = config->group("General");

// Nested groups
KConfigGroup sub = group.group("Advanced");
```

### Reading Values

```cpp
// With type inference from default value
QString str   = group.readEntry("Key", QString("default"));
int num       = group.readEntry("Key", 42);
bool flag     = group.readEntry("Key", true);
qreal val     = group.readEntry("Key", 1.0);
QStringList l = group.readEntry("Key", QStringList{});
QColor color  = group.readEntry("Key", QColor(Qt::white));
QRect rect    = group.readEntry("Key", QRect());

// Path with $HOME expansion
QString path  = group.readPathEntry("Key", QString());

// Check existence
bool exists = group.hasKey("Key");
```

### Writing Values

```cpp
group.writeEntry("Key", "value");
group.writeEntry("Key", 42);
group.writeEntry("Key", true);
group.writeEntry("Key", QStringList{"a", "b", "c"});
group.writeEntry("Key", QColor(Qt::red));

// Path with $HOME contraction
group.writePathEntry("Key", "/home/user/.config/file");

// Delete
group.deleteEntry("Key");
group.deleteGroup();  // Delete entire group

// Persist to disk (IMPORTANT: must call after writes)
config->sync();
```

### WriteConfigFlags

```cpp
enum WriteConfigFlag {
    Persistent = 0x01,              // Save on sync() — default
    Global     = 0x02,              // Write to kdeglobals instead
    Localized  = 0x04,              // Add locale tag to key
    Notify     = 0x08 | Persistent, // Notify KConfigWatchers via D-Bus
    Normal     = Persistent,        // Default
};

// Example: write with D-Bus notification
group.writeEntry("Theme", "dark", KConfigGroup::Notify);
config->sync();
```

### Supported Value Types

Strings, int, uint, bool, double, qlonglong, qulonglong, QStringList, QVariantList, QColor, QFont, QPoint, QPointF, QRect, QRectF, QSize, QSizeF, QDateTime, QDate, `std::chrono::duration`

---

## KConfig Methods

```cpp
QString name() const;              // Config file name
bool isDirty() const;              // Has unsaved changes
bool sync();                       // Write to disk
void markAsClean();                // Mark as not dirty
void reparseConfiguration();       // Reload from disk

QStringList groupList() const;     // All group names
QMap<QString, QString> entryMap(const QString &group = {}) const;  // All entries in group

AccessMode accessMode() const;     // NoAccess / ReadOnly / ReadWrite
bool isConfigWritable(bool warnUser);
bool isImmutable() const;          // System admin locked

void copyTo(const QString &file, KConfig *other = nullptr) const;
```

---

## KConfigWatcher (Change Notifications)

Watch for external config changes (other processes writing to the same file).

```cpp
#include <KConfigWatcher>

auto watcher = KConfigWatcher::create(config);
connect(watcher.data(), &KConfigWatcher::configChanged,
        this, [](const KConfigGroup &group, const QByteArrayList &keys) {
    qDebug() << "Changed keys in" << group.name() << ":" << keys;
});
```

Requires `WriteConfigFlag::Notify` on the writing side for D-Bus notification.

---

## KConfigBase (Abstract Base)

Both `KConfig` and `KConfigGroup` inherit from `KConfigBase`:

```cpp
class KConfigBase {
    virtual QStringList groupList() const = 0;
    bool hasGroup(const QString &group) const;
    KConfigGroup group(const QString &group);
    void deleteGroup(const QString &group, WriteConfigFlags flags = Normal);
    virtual bool sync() = 0;
    virtual AccessMode accessMode() const = 0;
    virtual bool isImmutable() const = 0;
};
```

---

## Krema Usage Pattern

```cpp
// docksettings.h
class DockSettings : public QObject {
    KSharedConfig::Ptr m_config;
    KConfigGroup m_group;

    DockSettings() {
        m_config = KSharedConfig::openConfig("kremarc");
        m_group = m_config->group("General");
    }

    int iconSize() const {
        return m_group.readEntry("IconSize", 48);
    }

    void setIconSize(int size) {
        m_group.writeEntry("IconSize", size);
        m_config->sync();
    }
};
```

### Config Keys Used in Krema

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `PinnedLaunchers` | QStringList | `{}` | Pinned launcher URLs |
| `IconSize` | int | 48 | Icon size in pixels |
| `IconSpacing` | int | 8 | Spacing between icons |
| `MaxZoomFactor` | qreal | 2.0 | Maximum zoom magnification |
| `CornerRadius` | int | 12 | Panel corner radius |
| `Floating` | bool | true | Floating panel mode |
| `VisibilityMode` | int | 0 | Dock visibility behavior |
| `Edge` | int | 0 | Screen edge (0=Bottom, 1=Top, 2=Left, 3=Right) |
| `ShowDelay` | int | 200 | Show delay in ms |
| `HideDelay` | int | 500 | Hide delay in ms |
