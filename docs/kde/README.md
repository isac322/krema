# KDE Knowledge Base

Verified API documentation from actual KDE headers (`/usr/include/`).

## Index

### Core (used across all milestones)

| Document | Description |
|----------|-------------|
| [wayland-layer-shell.md](wayland-layer-shell.md) | LayerShellQt — Shell, Window, Anchor/Layer/KeyboardInteractivity enums, surface coordinates |
| [taskmanager-models.md](taskmanager-models.md) | TaskManager — model hierarchy, proxy chain, roles, Q_PROPERTIES, task interaction |
| [virtual-desktops.md](virtual-desktops.md) | VirtualDesktopInfo — desktop/activity tracking |
| [multi-monitor.md](multi-monitor.md) | QScreen API, LayerShellQt ScreenConfiguration, multi-monitor architecture |
| [kconfig.md](kconfig.md) | KConfig/KSharedConfig/KConfigGroup — persistent configuration, read/write, watchers |
| [kglobalaccel.md](kglobalaccel.md) | KGlobalAccel — global keyboard shortcuts, registration, conflict handling |
| [kaboutdata.md](kaboutdata.md) | KAboutData — application metadata, license, D-Bus/KGlobalAccel integration |

### M4: Drag & Drop

| Document | Description |
|----------|-------------|
| [drag-and-drop.md](drag-and-drop.md) | TasksModel DnD — MimeType/MimeData roles, move/syncLaunchers, launcher management, URL comparison |

### M5: Theming & i18n

| Document | Description |
|----------|-------------|
| [kirigami-theming.md](kirigami-theming.md) | Kirigami Units (gridUnit/spacing/duration) & PlatformTheme (ColorSet/colors/fonts) |
| [kcolorscheme.md](kcolorscheme.md) | KColorScheme — ColorSet/BackgroundRole/ForegroundRole/DecorationRole/ShadeRole |
| [kiconthemes.md](kiconthemes.md) | KIconLoader/KIconTheme/KIconColors — icon loading, sizing, SVG recoloring |
| [ki18n.md](ki18n.md) | KI18n — i18n/i18nc/i18np macros, KLocalizedString, QML integration |

### M6: Window Preview

| Document | Description |
|----------|-------------|
| [pipewire-thumbnails.md](pipewire-thumbnails.md) | KPipeWire — PipeWireSourceItem, stream management, KWin screencast |

### M8+: Notification Badges

| Document | Description |
|----------|-------------|
| [notification-badges.md](notification-badges.md) | Multi-source badge architecture — WatchedNotificationsModel, SNI, Unity API, EWMH; all apps coverage |
| [notification-watcher-protocol.md](notification-watcher-protocol.md) | RegisterWatcher protocol deep-dive — correct D-Bus path/interface, live test results, Krema bug diagnosis |
| [notification-badge-approaches.md](notification-badge-approaches.md) | How Plasma task manager actually implements badges — SmartLauncherItem (Unity API), RegisterWatcher live test, Q_CLASSINFO issue diagnosis |

### M7: Visual Effects

| Document | Description |
|----------|-------------|
| [kwindow-effects.md](kwindow-effects.md) | KWindowEffects (blur/contrast/slide), KWindowSystem, KWaylandExtras |
| [attention-animation-reference.md](attention-animation-reference.md) | Attention animation options — wiggle/bounce/glow/badge comparison, Plasma reference, SmartLauncherItem API |
| [icon-normalization-reference.md](icon-normalization-reference.md) | Icon size normalization algorithms — alpha bounding box, threshold trim, content-aware scale |

### M9: System Tray

| Document | Description |
|----------|-------------|
| [statusnotifieritem.md](statusnotifieritem.md) | KStatusNotifierItem — status/category/icon/tooltip/menu/activation |

### Accessibility

| Document | Description |
|----------|-------------|
| [accessibility-guide.md](accessibility-guide.md) | KDE/Qt accessibility best practices — QML Accessible API, AT-SPI2, keyboard navigation |
| [accessibility-audit.md](accessibility-audit.md) | Krema accessibility audit baseline and implementation tracking |

### Reference

| Document | Description |
|----------|-------------|
| [lessons-learned.md](lessons-learned.md) | Bug-fix lessons and debugging insights |
| [tasksmodel-virtual-session.md](tasksmodel-virtual-session.md) | Why TasksModel cannot detect windows in kwin-mcp virtual sessions — protocol chain, permission checks, workarounds |
