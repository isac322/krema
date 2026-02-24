# KDE/Qt Accessibility Best Practices

Reference guide for implementing accessibility in KDE Plasma applications.

## QML Accessible API

Qt Quick provides the `Accessible` attached property for exposing UI semantics to assistive technologies.

### Key Properties

| Property | Type | Description |
|----------|------|-------------|
| `Accessible.role` | enum | AT-SPI role (Button, ListItem, ToolBar, Window, etc.) |
| `Accessible.name` | string | Primary identifier read by screen readers |
| `Accessible.description` | string | Extended description (read after name) |
| `Accessible.focusable` | bool | Whether the item can receive focus |
| `Accessible.focused` | bool | Whether the item currently has focus |
| `Accessible.onPressAction` | signal | Fired when assistive tech "presses" the item |

### Available Roles (subset)

- `Accessible.Button` — clickable action element
- `Accessible.ListItem` — item in a list
- `Accessible.ToolBar` — toolbar container
- `Accessible.PopupMenu` — popup/dropdown container
- `Accessible.Window` — top-level window
- `Accessible.Pane` — generic container
- `Accessible.StaticText` — non-interactive text

### Accessible.announce() (Qt 6.8+)

```qml
Accessible.announce(message, Accessible.Polite)   // queued after current speech
Accessible.announce(message, Accessible.Assertive) // interrupts current speech
```

Sends a live-region announcement to screen readers without changing focus.

## KDE HIG Accessibility Requirements

1. **Keyboard-only access**: All UI must be operable with keyboard alone
2. **No color-only information**: Never convey meaning through color alone
3. **Font scaling**: UI must remain usable with system font up to 14pt
4. **Orca compatibility**: `label + role` announcements (avoid duplicating role in label)
5. **Animation control**: Respect `Kirigami.Units.longDuration` which reflects KDE's "reduce animations" setting

## Focus & Keyboard Navigation

### Standard Mechanisms

- `FocusScope` — groups focus within a subtree
- `activeFocusOnTab: true` — enables Tab key navigation
- `KeyNavigation.left/right/up/down` — declarative arrow key navigation
- `Keys.onPressed` — imperative key handler

### Layer-Shell Considerations

Layer-shell surfaces do **not** participate in the system Tab chain. They cannot receive focus from Alt+Tab or Tab cycling. Instead:

- Use **global shortcuts** (KGlobalAccel) to enter focus mode
- Manage internal focus chain manually with `Keys.onPressed`
- Exit focus mode returns to the previous application

## AT-SPI2 Architecture

```
Qt QML Accessible → QAccessible → qt-at-spi bridge → D-Bus → Orca
```

- Qt maps `Accessible.role` to `QAccessible::Role`, which the bridge maps to AT-SPI roles
- `Accessible.name` → AT-SPI Name property (read by Orca)
- `Accessible.description` → AT-SPI Description property
- The bridge is automatic — no manual D-Bus code needed

## Testing

- **Orca**: Primary Linux screen reader. Start with `orca` command.
- **accerciser**: AT-SPI tree inspector. Shows roles, names, states.
- **kwin-mcp accessibility_tree**: Dumps the accessibility tree for a window.

## References

- [Qt QML Accessible](https://doc.qt.io/qt-6/qml-qtquick-accessible.html)
- [KDE HIG Accessibility](https://develop.kde.org/hig/accessibility/)
- [AT-SPI2 specification](https://www.freedesktop.org/wiki/Accessibility/AT-SPI2/)
