# KIconThemes (KIconLoader / KIconTheme / KIconColors)

> Source: `/usr/include/KF6/KIconThemes/` headers

## KIconLoader

Global icon loading and management.

### Enums

#### Group (determines default icon size)

```cpp
enum Group {
    NoGroup     = -1,
    Desktop     = 0,
    FirstGroup  = 0,
    Toolbar,
    MainToolbar,
    Small,
    Panel,
    Dialog,
    LastGroup,
    User,
};
```

#### StdSizes

```cpp
enum StdSizes {
    SizeSmall       = 16,
    SizeSmallMedium = 22,
    SizeMedium      = 32,
    SizeLarge       = 48,
    SizeHuge        = 64,
    SizeEnormous    = 128,
};
```

#### Context (icon purpose)

```cpp
enum Context {
    Any, Action, Application, Device, MimeType,
    Animation, Category, Emblem, Emote,
    International, Place, StatusIcon,
};
```

#### States (visual state)

```cpp
enum States {
    DefaultState,
    ActiveState,
    DisabledState,
    SelectedState,
    LastState,
};
```

#### MatchType

```cpp
enum MatchType {
    MatchExact,
    MatchBest,
    MatchBestOrGreaterSize,
};
```

### Key Methods

```cpp
static KIconLoader *global();  // Global singleton

// Load icon as pixmap
QPixmap loadIcon(const QString &name, Group group, int size = 0,
                 int state = DefaultState, const QStringList &overlays = {},
                 QString *path_store = nullptr, bool canReturnNull = false);

// Load with DPI scaling
QPixmap loadScaledIcon(const QString &name, Group group, qreal scale,
                       const QSize &size = {}, int state = DefaultState,
                       const QStringList &overlays = {}, QString *path_store = nullptr,
                       bool canReturnNull = false,
                       std::optional<KIconColors> colors = std::nullopt);

// Load MIME type icon
QPixmap loadMimeTypeIcon(const QString &name, Group group, int size = 0,
                         int state = DefaultState, const QStringList &overlays = {},
                         QString *path_store = nullptr);

// Get icon file path
QString iconPath(const QString &name, int group_or_size, bool canReturnNull = false);

// Query available icons
QStringList queryIcons(int size, Context context = Any);
QStringList queryIconsByContext(int size, Context context = Any);

// Current size for icon group
int currentSize(Group group);

// Custom palette for SVG coloring
void setCustomPalette(const QPalette &palette);
void resetPalette();
bool hasCustomPalette() const;

// Get current theme
KIconTheme *theme() const;

// Notify icon changes
static void emitChange(Group group);

// Unknown icon fallback
static QPixmap unknown();
```

### Convenience Functions (KDE namespace)

```cpp
namespace KDE {
    QIcon icon(const QString &iconName, KIconLoader *iconLoader = nullptr);
    QIcon icon(const QString &iconName, const KIconColors &colors, KIconLoader * = nullptr);
    QIcon icon(const QString &iconName, const QStringList &overlays, KIconLoader * = nullptr);
}
```

---

## KIconTheme

Represents an installed icon theme.

### Key Methods

```cpp
KIconTheme(const QString &name, const QString &appName = {},
           const QString &basePathHint = {});

QString name() const;           // Display name
QString internalName() const;   // Internal ID
QString description() const;
QString dir() const;            // Theme directory
QStringList inherits() const;   // Fallback themes
bool isValid() const;
bool isHidden() const;
int depth() const;              // Min color depth (8 or 32)

// Sizes
int defaultSize(KIconLoader::Group group) const;
QList<int> querySizes(KIconLoader::Group group) const;

// Icon lookup
QString iconPath(const QString &name, int size,
                 KIconLoader::MatchType match, qreal scale = 1) const;
QString iconPathByName(const QString &name, int size,
                       KIconLoader::MatchType match, qreal scale = 1) const;

bool hasContext(KIconLoader::Context context) const;
bool followsColorScheme() const;  // SVG icons get colorized (monochrome themes)

// Static
static QStringList list();               // All installed themes
static QString current();                // Current theme name
static QString defaultThemeName();       // Default theme
static void reconfigure();               // Reload
static void forceThemeForTests(const QString &name);
static void initTheme();                 // Initialize Breeze with color scheme (KF6.3+)
```

---

## KIconColors

Controls SVG icon recoloring via CSS class injection.

### CSS Classes

SVG icons with these CSS classes will be recolored:
- `.ColorScheme-Text`
- `.ColorScheme-Background`
- `.ColorScheme-Highlight`
- `.ColorScheme-HighlightedText`
- `.ColorScheme-PositiveText`
- `.ColorScheme-NeutralText`
- `.ColorScheme-NegativeText`
- `.ColorScheme-Accent`

### Constructors

```cpp
KIconColors();                        // Default (from app palette)
KIconColors(const QColor &colors);    // All roles set to one color
KIconColors(const QPalette &palette); // From palette
```

### Properties

| Getter | Setter | CSS Class |
|--------|--------|-----------|
| `text()` | `setText()` | `.ColorScheme-Text` |
| `background()` | `setBackground()` | `.ColorScheme-Background` |
| `highlight()` | `setHighlight()` | `.ColorScheme-Highlight` |
| `highlightedText()` | `setHighlightedText()` | `.ColorScheme-HighlightedText` |
| `accent()` | `setAccent()` | `.ColorScheme-Accent` |
| `positiveText()` | `setPositiveText()` | `.ColorScheme-PositiveText` |
| `neutralText()` | `setNeutralText()` | `.ColorScheme-NeutralText` |
| `negativeText()` | `setNegativeText()` | `.ColorScheme-NegativeText` |

### Usage

```cpp
// Custom-colored icon
KIconColors colors;
colors.setText(Qt::white);
colors.setBackground(Qt::transparent);

QIcon icon = KDE::icon("document-open", colors);
```

---

## Dock-Relevant Patterns

### Loading Task Icons

```cpp
// Use KIconLoader for consistent theme integration
QPixmap icon = KIconLoader::global()->loadIcon(
    iconName, KIconLoader::Panel, 48, KIconLoader::DefaultState);
```

### Theme Color Scheme Awareness

```cpp
// Check if theme recolors icons (important for dock icon style)
if (KIconTheme(KIconTheme::current()).followsColorScheme()) {
    // Icons will be recolored via CSS classes — no manual coloring needed
}
```

### Icon Size for Dock Panel

```cpp
int panelIconSize = KIconLoader::global()->currentSize(KIconLoader::Panel);
```
