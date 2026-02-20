# Kirigami Theming (Units & PlatformTheme)

> Source: `/usr/include/KF6/Kirigami/Platform/` headers

## Units (Singleton)

Semantic sizing and duration values for consistent UI.

**Namespace:** `Kirigami::Platform`
**QML:** `import org.kde.kirigami as Kirigami` → `Kirigami.Units`

### Spacing Properties (int, pixels)

| Property | Description |
|----------|-------------|
| `gridUnit` | Fundamental unit of space (base for all sizing) |
| `smallSpacing` | Between small elements (icons, labels in buttons) |
| `mediumSpacing` | Between medium elements (buttons, text fields) |
| `largeSpacing` | Between large elements (headings, cards) |

### Duration Properties (int, milliseconds)

| Property | Scaled? | Description |
|----------|---------|-------------|
| `veryShortDuration` | Yes | Near-instant with smoothness hint |
| `shortDuration` | Yes | Hover events, UI accentuation |
| `longDuration` | Yes | Longer animations, dialogs |
| `veryLongDuration` | Yes | Specialty long animations |
| `humanMoment` | **No** | 2000ms — waiting threshold (NOT scaled by animation settings) |
| `toolTipDelay` | — | Delay before tooltip display |

### Visual Properties

| Property | Type | Description |
|----------|------|-------------|
| `cornerRadius` | qreal | Corner radius for buttons/rectangles (KF6.2+) |

### Icon Sizes (grouped property)

Access: `Kirigami.Units.iconSizes.small`, etc.

| Property | Description |
|----------|-------------|
| `sizeForLabels` | Largest size that fits in fontMetrics.height |
| `small` | Small icon size |
| `smallMedium` | Small-medium |
| `medium` | Medium |
| `large` | Large |
| `huge` | Huge |
| `enormous` | Enormous |

Method: `Q_INVOKABLE int roundedIconSize(int size)` — rounds to nearest valid icon size.

### Signals

All properties have corresponding `*Changed()` signals.

---

## PlatformTheme (Attached Property)

Color management with platform-specific integration.

**QML:** Automatic — every Item has `Kirigami.Theme` attached

### ColorSet Enum

```cpp
enum ColorSet {
    View = 0,          // Lightest, for item views
    Window,            // Default for windows and "chrome"
    Button,            // For buttons
    Selection,         // For selected areas
    Tooltip,           // For tooltips
    Complementary,     // Opposite of Window (dark for light theme)
    Header,            // Heading areas, toolbars
};
```

### ColorGroup Enum

```cpp
enum ColorGroup {
    Disabled = QPalette::Disabled,
    Active   = QPalette::Active,
    Inactive = QPalette::Inactive,
    Normal   = QPalette::Normal,
};
```

### Color Properties

#### Foreground (Text) Colors

| Property | Description |
|----------|-------------|
| `textColor` | Normal text |
| `disabledTextColor` | Disabled areas (mid-gray) |
| `highlightedTextColor` | Text on highlighted backgrounds |
| `activeTextColor` | Active/attention areas |
| `linkColor` | Links |
| `visitedLinkColor` | Visited links |
| `negativeTextColor` | Critical errors, destructive actions |
| `neutralTextColor` | Warnings, non-critical |
| `positiveTextColor` | Success, trusted content |

#### Background Colors

| Property | Description |
|----------|-------------|
| `backgroundColor` | Generic background |
| `alternateBackgroundColor` | Alternate (list rows, etc.) |
| `highlightColor` | Selected area background |
| `activeBackgroundColor` | Active/attention background |
| `linkBackgroundColor` | Link background |
| `visitedLinkBackgroundColor` | Visited link background |
| `negativeBackgroundColor` | Negative area background |
| `neutralBackgroundColor` | Neutral area background |
| `positiveBackgroundColor` | Positive area background |

#### Decoration Colors

| Property | Description |
|----------|-------------|
| `focusColor` | Active focus decoration |
| `hoverColor` | Mouse hover decoration |

### Font Properties

| Property | Type | Description |
|----------|------|-------------|
| `defaultFont` | QFont | Platform default font |
| `smallFont` | QFont | Small variant |
| `fixedWidthFont` | QFont | Monospace font |

### Frame Properties

| Property | Type | Description |
|----------|------|-------------|
| `frameContrast` | qreal (0-1) | For separators and outlines |
| `lightFrameContrast` | qreal | Half of frameContrast (Separator.Weight.Light) |

### Additional Properties

```cpp
Q_PROPERTY(bool inherit ...)                   // Inherit colorSet from ancestor (default: true)
Q_PROPERTY(bool useAlternateBackgroundColor ...) // Hint for views to use alternate
Q_PROPERTY(QPalette palette ...)               // Full QPalette
```

### Customization

Per-component color override:
```cpp
void setCustomTextColor(const QColor &color = QColor());       // empty = reset
void setCustomBackgroundColor(const QColor &color = QColor());
void setCustomHighlightColor(const QColor &color = QColor());
// ... same pattern for all colors
```

### Signals

| Signal | Description |
|--------|-------------|
| `colorsChanged()` | Any color changed |
| `defaultFontChanged(QFont)` | Default font changed |
| `smallFontChanged(QFont)` | Small font changed |
| `fixedWidthFontChanged(QFont)` | Monospace font changed |
| `colorSetChanged(ColorSet)` | Color set changed |
| `colorGroupChanged(ColorGroup)` | Color group changed |
| `paletteChanged(QPalette)` | Full palette changed |
| `inheritChanged(bool)` | Inherit mode changed |

---

## QML Usage

### Using Theme Colors

```qml
import org.kde.kirigami as Kirigami

Rectangle {
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    color: Kirigami.Theme.backgroundColor

    Text {
        color: Kirigami.Theme.textColor
        font: Kirigami.Theme.defaultFont
    }
}
```

### Using Units

```qml
Item {
    width: Kirigami.Units.gridUnit * 10
    height: Kirigami.Units.gridUnit * 2

    spacing: Kirigami.Units.smallSpacing

    Image {
        width: Kirigami.Units.iconSizes.medium
        height: width
    }

    Behavior on opacity {
        NumberAnimation { duration: Kirigami.Units.shortDuration }
    }
}
```

### Color Set Inheritance

```qml
// Parent sets color set
Rectangle {
    Kirigami.Theme.colorSet: Kirigami.Theme.Complementary
    Kirigami.Theme.inherit: false  // stop inheriting from ancestor

    // All children will use Complementary colors automatically
    Text { color: Kirigami.Theme.textColor }  // complementary text color
}
```

---

## Design Principles

1. **Color Sets**: Don't mix colors from different sets — each set is a complete color environment
2. **Inheritance**: Colors cascade down the QML tree; use `inherit: false` to break the chain
3. **Semantic durations**: Use `shortDuration`, `longDuration` instead of hardcoded ms values
4. **DPI scaling**: Icon sizes and units auto-scale for HiDPI
5. **humanMoment**: Special 2000ms value NOT scaled — use for "should I show a spinner?" decisions
6. **Change batching**: `PlatformThemeChangeTracker` (KF6.7+) batches multiple property changes into single signal
