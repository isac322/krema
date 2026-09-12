# DockView Variables

## Architectural Role
`DockView` is the central engine of the Krema dock. It is the primary `QQuickView` that hosts the entire dock's UI. Its pervasive presence in the codebase is due to its multi-functional role:
- **UI Bridge**: Acts as the main communication channel between the C++ backend and the QML frontend. It is exposed as the `DockView` context property for all QML components.
- **Platform Owner**: Owns the `DockPlatform` backend (Wayland layer-shell), handling all screen positioning, layer management, and visibility configuration.
- **Geometry & Style Hub**: Manages the dock's surface size, background blurs, padding, and layout updates.
- **Orchestrator**: Acts as the central node where reactivity from settings, visibility controllers, and icon providers is applied to the UI surface.

## Registry
| Name | Owner | Purpose | Consumers | Architectural Link |
| :--- | :--- | :--- | :--- | :--- |
| `backgroundColor` | `DockView` | Dock panel background color | `DockView`, `QML` | Style System |
| `backgroundStyleType` | `DockView` | Style identifier | `DockView`, `QML` | Style System |
| `floatingPadding` | `DockView` | Floating dock margin | `DockView`, `Shell` | Rule 12 |
| `iconCacheVersion` | `DockView` | Cache invalidation key | `DockView`, `IconProvider` | Performance |
| `edge` | `DockView` | Screen edge position | `DockView`, `Shell` | Layout |
| `isVertical` | `DockView` | Orientation state | `DockView`, `Shell` | Layout |
| `s_padding` | `DockView` | Default padding constant | `DockView` | Rule 17 |
| `s_floatingMargin` | `DockView` | Floating dock margin constant | `DockView` | Rule 17 |
| `s_tooltipReserve` | `DockView` | Tooltip reserved space | `DockView` | Interaction |
