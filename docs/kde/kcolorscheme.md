# KColorScheme

> Source: `/usr/include/KF6/KColorScheme/` headers

## Overview

KColorScheme provides access to KDE color scheme colors, organized by color sets (View, Window, Button, etc.) and roles (Background, Foreground, Decoration, Shade).

---

## KColorScheme Class

### Enums

#### ColorSet

```cpp
enum ColorSet {
    View,           // Lightest, for item views (e.g., text editor background)
    Window,         // Default window background and chrome
    Button,         // Button colors
    Selection,      // Selected item colors
    Tooltip,        // Tooltip colors
    Complementary,  // Opposite of Window (dark for light theme)
    Header,         // Heading areas, toolbars
    NColorSets,     // Count sentinel
};
```

#### BackgroundRole

```cpp
enum BackgroundRole {
    NormalBackground,      // Normal background
    AlternateBackground,   // Alternate (list alternating rows)
    ActiveBackground,      // Active/attention
    LinkBackground,        // Link background
    VisitedBackground,     // Visited link background
    NegativeBackground,    // Error/critical
    NeutralBackground,     // Warning
    PositiveBackground,    // Success
};
```

#### ForegroundRole

```cpp
enum ForegroundRole {
    NormalText,        // Normal text
    InactiveText,      // Disabled/inactive text
    ActiveText,        // Active/attention text
    LinkText,          // Link text
    VisitedText,       // Visited link text
    NegativeText,      // Error/critical text
    NeutralText,       // Warning text
    PositiveText,      // Success text
};
```

#### DecorationRole

```cpp
enum DecorationRole {
    FocusColor,   // Active focus indicator
    HoverColor,   // Mouse hover indicator
};
```

#### ShadeRole

```cpp
enum ShadeRole {
    LightShade,      // Lightest shade (3D effect, highlights)
    MidlightShade,   // Between Light and Mid
    MidShade,        // Middle shade
    DarkShade,       // Dark shade (shadows)
    ShadowShade,     // Darkest shade (drop shadows)
};
```

### Constructor

```cpp
// Construct for a specific color group and color set
KColorScheme(QPalette::ColorGroup group = QPalette::Normal,
             ColorSet set = View,
             KSharedConfigPtr config = KSharedConfigPtr());
```

### Methods

```cpp
// Get colors by role
QBrush background(BackgroundRole role = NormalBackground) const;
QBrush foreground(ForegroundRole role = NormalText) const;
QBrush decoration(DecorationRole role) const;
QColor shade(ShadeRole role) const;

// Static shade calculation
static QColor shade(QColor color, ShadeRole role, qreal contrast = -1.0, qreal chromaAdjust = 0.0);

// Contrast
static qreal contrastF(KSharedConfigPtr config = KSharedConfigPtr());  // 0.0-1.0
static qreal frameContrast(KSharedConfigPtr config = KSharedConfigPtr());

// Palette helpers
static void adjustBackground(QPalette &palette, BackgroundRole newRole,
                             QPalette::ColorRole color = QPalette::Base,
                             ColorSet set = View, KSharedConfigPtr config = {});
static void adjustForeground(QPalette &palette, ForegroundRole newRole,
                             QPalette::ColorRole color = QPalette::Text,
                             ColorSet set = View, KSharedConfigPtr config = {});

// Create full application palette
static QPalette createApplicationPalette(KSharedConfigPtr config);

// Check if color set is supported in config
static bool isColorSetSupported(KSharedConfigPtr config, ColorSet set);
```

---

## KStatefulBrush

State-aware brush that adapts to widget state (Active, Inactive, Disabled).

### Constructors

```cpp
KStatefulBrush();  // empty
KStatefulBrush(KColorScheme::ColorSet set, KColorScheme::ForegroundRole role, KSharedConfigPtr = {});
KStatefulBrush(KColorScheme::ColorSet set, KColorScheme::BackgroundRole role, KSharedConfigPtr = {});
KStatefulBrush(KColorScheme::ColorSet set, KColorScheme::DecorationRole role, KSharedConfigPtr = {});
KStatefulBrush(QBrush normalBrush, QBrush activeBrush, QBrush disabledBrush, KSharedConfigPtr = {});
```

### Methods

```cpp
QBrush brush(QPalette::ColorGroup group) const;  // Get brush for explicit state
QBrush brush(const QPalette &palette) const;      // Get brush using palette's current color group
```

### Usage Pattern

```cpp
KStatefulBrush brush(KColorScheme::View, KColorScheme::NormalBackground);
widget->setPalette(QPalette());  // or custom palette
QColor bgColor = brush.brush(widget->palette()).color();
```

---

## KColorSchemeManager

Manages available color schemes and activation.

### Methods

```cpp
static KColorSchemeManager *instance();  // Global singleton (KF6.6+)

QAbstractItemModel *model() const;  // Available schemes (for combo box)
QModelIndex indexForSchemeId(const QString &id) const;
QModelIndex indexForScheme(const QString &name) const;

// Activation
void activateScheme(const QModelIndex &index);
void activateSchemeId(const QString &id);  // Public slot

// Query
QString activeSchemeId() const;
QString activeSchemeName() const;

// Persistence
void saveSchemeIdToConfigFile(const QString &id);
void setAutosaveChanges(bool autosave);
```

---

## Typical Usage

### Getting Colors

```cpp
#include <KColorScheme>

// Get View color set for normal state
KColorScheme scheme(QPalette::Normal, KColorScheme::View);

QColor bg = scheme.background().color();
QColor text = scheme.foreground().color();
QColor link = scheme.foreground(KColorScheme::LinkText).color();
QColor error = scheme.foreground(KColorScheme::NegativeText).color();
QColor hover = scheme.decoration(KColorScheme::HoverColor).color();
QColor shadow = scheme.shade(KColorScheme::ShadowShade);
```

### State-Aware Colors

```cpp
// Brush that changes with widget state
KStatefulBrush bgBrush(KColorScheme::Window, KColorScheme::NormalBackground);

// Get color for current state
QColor color = bgBrush.brush(widget->palette()).color();
```

### Application Palette

```cpp
// Create palette from system color scheme
QPalette pal = KColorScheme::createApplicationPalette(KSharedConfig::openConfig());
QApplication::setPalette(pal);
```
