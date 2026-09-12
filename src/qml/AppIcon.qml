// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import com.bhyoo.krema 1.0
import org.kde.kirigami as Kirigami

/**
 * A single dock item (app icon + indicator).
 *
 * Handles parabolic zoom calculation and displays the application icon
 * with status indicators beneath.
 */
Item {
    // === Notification (transient attention) ===
    // Implementation of the KDE Plasma Attention Protocol.
    // Triggers visual animations when an app demands attention via:
    // 1. Window metadata (IsDemandingAttention flag).
    // 2. Unity Launcher API (Urgency flag).
    // 3. Status Notifier Items (NeedsAttention state).
    // --- KDE state-driven launch tracking ---
    // Launch animation lifecycle:
    //   1. manualLaunching: click → IsStartup handoff bridge (a few ms)
    //   2. _isStartup: KDE startup notification drives bounce (until window or timeout)
    //   3. _waitingForWindow: keeps bounce alive after KDE's 5s timeout for slow apps
    // Bounce stops when any of:
    //   - ChildCount increases (new window created)
    //   - IsActive changes (single-instance app raised its existing window)
    //   - noOpDetectionTimer fires (already-active app, no new window in 2s → no-op)
    //   - launchSafetyTimer fires (IsStartup never arrived within 500ms)
    //   - maxLaunchTimer fires (30s absolute safety net)
    // --- State change handlers ---
    // --- Attention animations (6 types) ---
    // Type 4: Glow — animation is on MultiEffect (attentionGlow) above
    // Click handling is done in main.qml's dockMouseArea using
    // hoveredIndex-based hit testing to match the zoomed icon geometry.

    id: dockItem

    // Properties set by the Repeater delegate
    required property int index
    required property var model
    // Display name for tooltip (readable by parent)
    readonly property string displayName: model.display || ""
    // Accessible description for screen readers (state summary)
    readonly property string accessibleDescription: {
        let parts = [];
        if (DockModel.isPinned(index))
            parts.push(i18n("Pinned"));

        if (model.IsActive)
            parts.push(i18n("Active"));

        if (model.IsMinimized)
            parts.push(i18n("Minimized"));

        let count = model.ChildCount || 0;
        if (count > 1)
            parts.push(i18n("%1 windows", count));

        if (launching)
            parts.push(i18n("Starting"));

        if (_isDemandingAttention)
            parts.push(i18n("Attention requested"));

        if (_badgeCount > 0)
            parts.push(i18n("%1 notifications", _badgeCount));

        return parts.join(", ");
    }
    // Tracks keyboard navigation focus set by main.qml; toggles visual visibility of the focusRing.
    property bool isKeyboardFocused: false
    // Backing object for the Unity Launcher API, initialized via DockModel.launcherUrl.
    // Provides app-direct reporting for badge counts and urgency status.
    property QtObject _smartLauncherItem: null
    // Desktop entry name for notification lookup (e.g. "org.kde.dolphin", "slack")
    readonly property string _appId: {
        let _dep = model.display; // reactive dependency on model data
        return DockModel.appId(index);
    }
    // === Inbox (persistent badge) ===
    // Unified badge count calculation with a strict priority hierarchy.
    // 1st: Unity API (Direct app reporting, e.g. unread count in mail apps).
    // 2nd: WatchedNotifications (Passive fd.o watcher for generic notifications).
    readonly property int _badgeCount: {
        let _rev = NotificationTracker.revision; // reactive dependency
        // 1st: SmartLauncherItem (Unity API) — exact count from the app
        if (_smartLauncherItem && _smartLauncherItem.countVisible)
            return _smartLauncherItem.count;

        // 2nd: WatchedNotifications — unread notification count
        if (_appId.length > 0) {
            let n = NotificationTracker.unreadCount(_appId);
            if (n > 0)
                return n;

        }
        return 0;
    }
    // API support matrix:
    //   fd.o RegisterWatcher: notification(badge↑→animation) + inbox(badge count) + clear(focus/menu/SNI)
    //   Portal: transparent (routed to fd.o)
    //   SNI: notification(NeedsAttention→animation) + clear(→Active clears fd.o)
    //   SmartLauncher: inbox(count→badge, app-managed) + notification(urgent→animation)
    //   Window: notification(IsDemandingAttention→animation)
    //   Jobs: transparent (exposed via SmartLauncherItem.progress)
    //   DND: suppresses notification animation (badge persists)
    // Attention condition (Plasma-compatible: IsDemandingAttention OR SmartLauncher.urgent OR SNI NeedsAttention)
    readonly property bool _isDemandingAttention: {
        let _rev = NotificationTracker.revision; // reactive dependency
        return (model.IsDemandingAttention ?? false) || (_smartLauncherItem !== null && _smartLauncherItem.urgent) || (_appId.length > 0 && NotificationTracker.sniNeedsAttention(_appId));
    }
    // Timed attention animation state — triggers on new attention events, auto-clears after duration
    property bool _animationActive: false
    property int _prevBadgeCount: 0
    // Attention animation won't run during launch bounce or DND
    readonly property bool _showAttentionAnim: _animationActive && !launching && DockSettings.attentionAnimation > 0 && !NotificationTracker.dndActive
    readonly property int _attentionType: DockSettings.attentionAnimation
    // --- Visual Geometry (Rule 12: Ground Truth) ---
    // The absolute screen pixels occupied by the icon image, accounting for zoom scale and edge origin.
    readonly property real visualIconWidth: iconSize * currentScale
    readonly property real visualIconHeight: iconSize * currentScale
    // The visual top-left of the icon relative to the dock delegate (this item)
    readonly property real visualIconX: {
        let growth = iconSize * (currentScale - 1);
        let baseX = iconImage.x;
        switch (DockView.edge) {
        case 2:
            return baseX; // Left: grows right, X is fixed
        case 3:
            return baseX - growth; // Right: grows left
        default:
            return baseX - growth / 2; // Top/Bottom: centered horizontally
        }
    }
    readonly property real visualIconY: {
        let growth = iconSize * (currentScale - 1);
        let baseY = iconImage.y;
        switch (DockView.edge) {
        case 0:
            return baseY; // Top: grows down, Y is fixed
        case 1:
            return baseY - growth; // Bottom: grows up
        default:
            return baseY - growth / 2; // Left/Right: centered vertically
        }
    }
    // Blink opacity multiplier (animated by type 6)
    property real _blinkOpacity: 1
    // Dot blink opacity (animated by type 5)
    property real _dotBlinkOpacity: 1
    // Launch animation state
    property bool manualLaunching: false
    readonly property bool launching: manualLaunching || (_isStartup && !_noOpOverride) || _waitingForWindow
    // Drag and drop visual feedback (driven from main.qml)
    property bool isDragSource: false
    property bool isExternalDropTarget: false
    // Hover state (driven from main.qml)
    property bool isHovered: false
    // Configuration from DockView (passed as required properties)
    required property int iconSize
    property real maxZoomFactor: 1.6
    property int spacing: 4
    // --- Animation: itemCenterX (The specific point the icon grows from) ---
    property real itemCenterX: 0
    // --- Animation: zoomFactor (Real-time magnification multiplier) ---
    // Set imperatively by main.qml's updateZoomFactors() using actual visual centers.
    property real zoomFactor: 1
    readonly property bool _isStartup: model.IsStartup ?? false
    readonly property bool _isActive: model.IsActive ?? false
    readonly property int _childCount: model.ChildCount || 0
    // Virtual desktop opacity: mode 1 (DimOtherDesktops) dims icons on other desktops
    // DockModel.currentDesktop is a reactive dependency — triggers re-evaluation on desktop switch
    readonly property bool _isOnCurrentDesktop: {
        let _dep = DockModel.currentDesktop;
        return DockModel.isOnCurrentDesktop(index);
    }
    property int _prevChildCount: 0
    property bool _noOpOverride: false
    property bool _waitingForWindow: false
    property int _childCountAtLaunch: 0
    property alias iconImage: iconImage
    // --- Animation: currentScale (Animated value between 1.0 and max zoom factor) ---
    property real currentScale: 1
    property bool _zoomAnimReady: false
    // --- THE 5-UNIT STACK (Rule 17) ---
    // --- Unit 1: _unitPanelFloor (Internal distance from panel edge to indicator) ---
    readonly property real _unitPanelFloor: Math.max(4, Math.round(iconSize * 0.25))
    // --- Unit 2: _unitIndicator (Visual dot/dash height) ---
    readonly property real _unitIndicator: Math.max(2, Math.round(iconSize * 0.1))
    // --- Unit 3: _unitIconIndicatorGap (Space between indicator and icon base) ---
    readonly property real _unitIconIndicatorGap: Math.max(2, Math.round(iconSize * 0.125) + Math.round(iconSize * 0.15 * (1 - DockSettings.indicatorOffset)))
    // --- Unit 4: _unitIcon (Dynamic icon image height subject to zoom) ---
    readonly property real _unitIcon: iconSize * currentScale
    // --- Unit 5: _unitPanelCeiling (Internal space above icon, mirrors Unit 1 per Rule 2) ---
    readonly property real _unitPanelCeiling: _unitPanelFloor
    // --- DERIVED GEOMETRY ---
    // --- Derived: _maxTheoreticalThickness (The absolute mathematical envelope of a slot) ---
    readonly property real _maxTheoreticalThickness: iconSize + _unitPanelFloor + _unitIndicator + _unitIconIndicatorGap + _unitPanelCeiling
    // --- Derived: _currentVisualThickness (The real-time height of the zoomed slot) ---
    readonly property real _currentVisualThickness: _unitIcon + _unitPanelFloor + _unitIndicator + _unitIconIndicatorGap + _unitPanelCeiling
    // --- Derived: _unitIconBaseOffset (Total distance from panel edge to icon base) ---
    readonly property real _unitIconBaseOffset: _unitPanelFloor + _unitIndicator + _unitIconIndicatorGap
    // --- DEBUG PROTOCOL (Rule 12) ---
    readonly property bool _debugAll: false
    readonly property bool _debugGeom: KremaDebug.geomEnabled
    readonly property bool _debugHit: KremaDebug.inputEnabled
    readonly property bool _debugZoom: KremaDebug.animEnabled
    readonly property bool _debugNotif: KremaDebug.modelEnabled
    // Launch bounce animation — finishes current cycle gracefully when launching ends
    property bool _finishingBounce: false
    // Bounce property/value depends on edge direction
    readonly property string _bounceProp: DockView.isVertical ? "x" : "y"
    // --- Animation: _bounceTarget (Pixel distance for launch animation) ---
    readonly property real _bounceTarget: {
        switch (DockView.edge) {
        case 0:
            return 8; // Top: bounce down
        case 1:
            return -8; // Bottom: bounce up
        case 2:
            return 8; // Left: bounce right
        case 3:
            return -8; // Right: bounce left
        }
        return -8;
    }
    // --- Animation: _attentionBounceTarget (Pixel distance for "demands attention" jump) ---
    readonly property real _attentionBounceTarget: {
        switch (DockView.edge) {
        case 0:
            return 14; // Top: down
        case 1:
            return -14; // Bottom: up
        case 2:
            return 14; // Left: right
        case 3:
            return -14; // Right: left
        }
        return -14;
    }

    function _triggerAttention() {
        if (NotificationTracker.dndActive)
            return ;

        _animationActive = true;
        let dur = DockSettings.attentionAnimationDuration;
        if (dur > 0) {
            attentionTimer.interval = dur * 1000;
            attentionTimer.restart();
        }
    }

    // Standard button accessibility interface; triggers activation via DockActions.activate(index).
    Accessible.role: Accessible.Button
    Accessible.name: displayName
    Accessible.description: accessibleDescription
    Accessible.focusable: true
    Accessible.focused: isKeyboardFocused
    Accessible.onPressAction: DockActions.activate(index)
    // --- Notification → animation debug logging ---
    on_BadgeCountChanged: {
        KremaDebug.model("[NOTIF-TRACE] '" + displayName + "' appId=" + _appId + " badgeCount=" + _badgeCount);
        if (_badgeCount > _prevBadgeCount)
            _triggerAttention();

        _prevBadgeCount = _badgeCount;
    }
    on_IsDemandingAttentionChanged: {
        KremaDebug.model("[NOTIF-TRACE] '" + displayName + "' appId=" + _appId + " isDemandingAttention=" + _isDemandingAttention + " | model.IsDemandingAttention=" + (model.IsDemandingAttention ?? false) + " | smartLauncher.urgent=" + (_smartLauncherItem !== null && _smartLauncherItem.urgent) + " | sniNeedsAttention=" + (_appId.length > 0 ? NotificationTracker.sniNeedsAttention(_appId) : false) + " | badgeCount=" + _badgeCount);
        if (_isDemandingAttention)
            _triggerAttention();

    }
    on_BlinkOpacityChanged: {
        if (_blinkOpacity !== 1 && KremaDebug.animEnabled)
            KremaDebug.anim("_blinkOpacity=" + _blinkOpacity.toFixed(3) + " → iconOpacity=" + iconImage.opacity.toFixed(3) + " for '" + displayName + "'");

    }
    opacity: (DockModel.virtualDesktopMode === 1 && !_isOnCurrentDesktop) ? DockSettings.otherDesktopOpacity : 1
    on_IsStartupChanged: {
        if (_isStartup) {
            // KDE acknowledged the launch → release manualLaunching bridge
            if (manualLaunching)
                manualLaunching = false;

        } else {
            // IsStartup went false (window matched, cancelled, or KDE timeout).
            // If no new window appeared and not already active → slow app,
            // keep bouncing via _waitingForWindow.
            if (!_noOpOverride && _childCount === _childCountAtLaunch && !_isActive) {
                _waitingForWindow = true;
                maxLaunchTimer.restart();
            }
        }
    }
    on_IsActiveChanged: {
        if (_isActive && manualLaunching)
            manualLaunching = false;

        if (_isActive && _waitingForWindow) {
            _waitingForWindow = false;
            maxLaunchTimer.stop();
        }
        // Clear fd.o watcher badges when window gains focus.
        // SmartLauncher count is app-managed (separate source), so always clear fd.o unconditionally.
        if (_isActive && _appId.length > 0)
            NotificationTracker.clearUnreadNotifications(_appId);

    }
    on_ChildCountChanged: {
        if (_childCount > _prevChildCount) {
            if (manualLaunching)
                manualLaunching = false;

            _noOpOverride = false;
            _waitingForWindow = false;
            noOpDetectionTimer.stop();
            maxLaunchTimer.stop();
        }
        _prevChildCount = _childCount;
    }
    onManualLaunchingChanged: {
        if (manualLaunching) {
            _childCountAtLaunch = _childCount;
            _noOpOverride = false;
            _waitingForWindow = false;
            launchSafetyTimer.restart();
            maxLaunchTimer.stop();
            // Start no-op detection only if app is already active
            if (_isActive)
                noOpDetectionTimer.restart();

        } else {
            launchSafetyTimer.stop();
        }
    }
    // Update currentScale when zoomFactor changes
    onZoomFactorChanged: currentScale = zoomFactor
    // When item position shifts due to model reorganization (e.g. hideActivatedLaunchers
    // merges a launcher with its window, causing other delegates to shift), suppress
    // zoom animation to prevent the visual glitch where shifted icons animate their scale.
    // Normal mouse-driven zoom doesn't change itemCenterX, so this only fires during
    // model/layout changes.
    onItemCenterXChanged: {
        if (_zoomAnimReady) {
            _zoomAnimReady = false;
            currentScale = zoomFactor;
            Qt.callLater(function() {
                _zoomAnimReady = true;
            });
        }
    }
    // On delegate creation: apply zoom instantly (no animation) to avoid glitch
    // when Repeater recreates delegates due to model changes.
    // Qt.callLater() defers _zoomAnimReady until AFTER Row layout has set the
    // delegate's x position, ensuring itemCenterX and zoomFactor are correct.
    Component.onCompleted: {
        _prevChildCount = _childCount;
        currentScale = zoomFactor; // best guess pre-layout
        Qt.callLater(function() {
            currentScale = zoomFactor; // correct value after layout
            _zoomAnimReady = true;
        });
        // Create SmartLauncherItem for badge/urgent tracking
        let comp = Qt.createComponent("org.kde.plasma.private.taskmanager", "SmartLauncherItem");
        if (comp && comp.status === Component.Ready) {
            _smartLauncherItem = comp.createObject(dockItem);
            _smartLauncherItem.launcherUrl = Qt.binding(() => {
                return DockModel.launcherUrl(dockItem.index);
            });
        }
        if (comp)
            comp.destroy();

        // Debug: log appId for notification matching verification
        Qt.callLater(function() {
            if (_debugNotif && dockItem._appId.length > 0)
                KremaDebug.model("DockIcon created: '" + dockItem.displayName + "' appId=" + dockItem._appId);

            if (_debugGeom)
                KremaDebug.geom(`'${dockItem.displayName}' | SLOT X:${Math.round(x)} Y:${Math.round(y)} W:${width} H:${height} | ICON X:${Math.round(iconImage.x)} Y:${Math.round(iconImage.y)} W:${iconImage.width} H:${iconImage.height} (Scaled:${Math.round(iconImage.width * currentScale)}x${Math.round(iconImage.height * currentScale)}) | TotalH:${_maxTheoreticalThickness} | Floor:${_unitPanelFloor} | Indic:${_unitIndicator}+${_unitIconIndicatorGap} | Ceil:${_unitPanelCeiling}`);

        });
    }
    // Size: The delegate represents the "Inside World" territory (The Slot).
    // Rule 15: We use currentScale (animated) for the layout size so it smoothly
    // pushes neighbors away during the zoom wave, keeping the physical
    // territory in perfect sync with the visual pixels.
    width: DockView.isVertical ? _maxTheoreticalThickness : (iconSize * currentScale)
    height: DockView.isVertical ? (iconSize * currentScale) : _maxTheoreticalThickness
    // Reset attention properties when animation stops
    on_ShowAttentionAnimChanged: {
        if (KremaDebug.modelEnabled)
            KremaDebug.model("[NOTIF-TRACE] '" + displayName + "' appId=" + _appId + " showAttentionAnim=" + _showAttentionAnim + " | isDemandingAttention=" + _isDemandingAttention + " | launching=" + launching + " | attentionSetting=" + DockSettings.attentionAnimation);

        if (!_showAttentionAnim) {
            attentionBounceT.x = 0;
            attentionBounceT.y = 0;
            attentionRotateT.angle = 0;
            attentionScaleT.xScale = 1;
            attentionGlow.shadowOpacity = 0;
            _blinkOpacity = 1;
            _dotBlinkOpacity = 1;
        }
    }
    onLaunchingChanged: {
        if (launching) {
            _finishingBounce = false;
            bounceAnim.start();
        } else if (bounceAnim.running) {
            // Let the current cycle finish at faster speed
            _finishingBounce = true;
        }
    }

    Timer {
        id: attentionTimer

        onTriggered: dockItem._animationActive = false
    }

    // Launch Safety Protocol:
    // [0.5s] Fallback if the OS misses the startup signal.
    // [2.0s] No-Op detection (e.g. middle-click didn't open a new window).
    // [30.0s] Absolute ceiling to prevent eternal icon bouncing on crash.
    Timer {
        id: launchSafetyTimer

        interval: 500
        onTriggered: {
            if (dockItem.manualLaunching)
                dockItem.manualLaunching = false;

        }
    }

    // 2s No-Op Detection: Detects middle-click or 'New Instance' attempts on already active apps.
    // If no new window appears within 2s, it stops the launch bounce.
    Timer {
        id: noOpDetectionTimer

        interval: 2000
        onTriggered: {
            if (dockItem._childCount === dockItem._childCountAtLaunch)
                dockItem._noOpOverride = true;

        }
    }

    // 30s Absolute Ceiling: Maximum allowed time for an icon to bounce during startup.
    // Prevents eternal bouncing if an application crashes or fails to open a window.
    Timer {
        id: maxLaunchTimer

        interval: 30000
        onTriggered: {
            dockItem._waitingForWindow = false;
        }
    }

    // Application icon
    // Rule 1: The Fixed Floor (Grounded by Gravity)
    // The icon sits in the 'Inside World' above the indicators.
    Item {
        // (Scale transform merged into the main transform array below to prevent double-property syntax errors)

        id: iconImage

        // Valid if EITHER the C++ image loaded successfully OR Kirigami found the RAM icon
        property bool valid: internalIcon.status === Image.Ready || ramIcon.valid

        width: iconSize
        height: iconSize
        // Declarative Grounding in the 'Inside World'
        x: {
            if (!DockView.isVertical)
                return (parent.width - width) / 2;

            if (DockView.edge === 2)
                return _unitIconBaseOffset;
 // Left: anchor after flooring stack
            return parent.width - _unitIconBaseOffset - width; // Right: anchor before flooring stack
        }
        y: {
            if (DockView.isVertical)
                return (parent.height - height) / 2;

            if (DockView.edge === 0)
                return _unitIconBaseOffset;
 // Top: icon starts after flooring stack
            return parent.height - _unitIconBaseOffset - height; // Bottom: icon ends before flooring stack
        }
        // Highlight for active window / drag source dimming (× blink for type 6)
        opacity: {
            let base;
            if (dockItem.isDragSource)
                base = 0.3;
            else if (dockItem.model.IsActive)
                base = 1;
            else if (dockItem.model.IsMinimized)
                base = 0.5;
            else
                base = 0.8;
            return base * dockItem._blinkOpacity;
        }
        // Transforms: launch bounce + attention animations + Rule 1 Zoom
        transform: [
            Scale {
                origin.x: {
                    switch (DockView.edge) {
                    case 2:
                        return 0; // Left: grow right
                    case 3:
                        return iconImage.width; // Right: grow left
                    default:
                        return iconImage.width / 2;
                    }
                }
                origin.y: {
                    switch (DockView.edge) {
                    case 0:
                        return 0; // Top: grow down
                    case 1:
                        return iconImage.height; // Bottom: grow up
                    default:
                        return iconImage.height / 2;
                    }
                }
                xScale: currentScale
                yScale: currentScale
            },
            Translate {
                id: bounceTranslate

                x: 0
                y: 0
            },
            Translate {
                id: attentionBounceT

                x: 0
                y: 0
            },
            Rotation {
                id: attentionRotateT

                origin.x: iconSize / 2
                origin.y: iconSize / 2
                angle: 0
            },
            Scale {
                id: attentionScaleT

                origin.x: iconSize / 2
                origin.y: iconSize / 2
                xScale: 1
                yScale: xScale
            }
        ]

        // --- PIXEL HUGGER (Rule 12 Debug Visual) ---
        Rectangle {
            z: -1 // Behind the icon
            anchors.fill: parent
            color: "magenta"
            opacity: 0.4
            visible: false // [ISOLATION: SILENCED] _debugGeom
            border.color: "magenta"
            border.width: 1
            enabled: false

            QQC2.Label {
                text: "" // [ISOLATION: SILENCED] Math.round(parent.width) + "x" + Math.round(parent.height)
                font.pixelSize: 8
                font.bold: true
                color: "white"
                anchors.centerIn: parent
                opacity: 0.8
                visible: false // [ISOLATION: SILENCED]
            }

        }

        // --- HOVER GLOW ---
        // A subtle radial glow that follows the icon shape
        // --- Layer 2: Icon Hover Glow (MultiEffect background blur) ---
        Rectangle {
            id: hoverGlow

            anchors.centerIn: parent
            width: parent.width * 1.2
            height: parent.height * 1.2
            radius: width / 2
            // Pure white glow, very soft
            color: "white"
            opacity: dockItem.isHovered ? 0.3 : 0
            layer.enabled: true

            // Fast, snappy transition
            Behavior on opacity {
                NumberAnimation {
                    duration: Kirigami.Units.shortDuration
                }

            }

            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1.5
            }

        }

        // Backup: Native RAM Icon (Kirigami perfectly understands KDE's raw QIcon memory object)
        Kirigami.Icon {
            id: ramIcon

            width: 128
            height: 128
            anchors.centerIn: parent
            scale: iconSize / 128
            smooth: true
            visible: internalIcon.status !== Image.Ready
            source: model.decoration
        }

        // Primary: C++ Normalized Icon (Standard Image natively handles custom image:// URLs)
        Image {
            id: internalIcon

            width: 128
            height: 128
            anchors.centerIn: parent
            scale: iconSize / 128
            smooth: true
            sourceSize.width: 128
            sourceSize.height: 128
            source: dockItem._appId ? ("image://taskicon/" + dockItem._appId) : ""
        }

        // Fallback placeholder when icon is not available
        Rectangle {
            id: iconPlaceholder

            anchors.centerIn: parent
            width: parent.width * 0.85
            height: parent.height * 0.85
            radius: Kirigami.Units.largeSpacing
            color: Kirigami.Theme.highlightColor
            visible: !iconImage.valid
            Accessible.ignored: true

            QQC2.Label {
                anchors.centerIn: parent
                text: {
                    let name = dockItem.model.display || "";
                    return name.length > 0 ? name[0].toUpperCase() : "?";
                }
                font.pixelSize: iconSize * 0.4
                font.bold: true
                color: Kirigami.Theme.highlightedTextColor
                Accessible.ignored: true
            }

        }

        Behavior on opacity {
            // Disable during blink animation — rapid _blinkOpacity changes cause
            // the Behavior to restart every frame, preventing opacity from changing.
            enabled: !blinkAnim.running

            NumberAnimation {
                duration: Kirigami.Units.shortDuration
            }

        }

    }

    // Attention glow effect (type 4) — MultiEffect shadow pulse on iconImage
    MultiEffect {
        id: attentionGlow

        source: iconImage
        anchors.fill: iconImage
        paddingRect: Qt.rect(16, 16, 16, 16)
        visible: dockItem._showAttentionAnim && dockItem._attentionType === 4
        shadowEnabled: true
        shadowColor: Kirigami.Theme.highlightColor
        shadowBlur: 0.7
        shadowScale: 1.12
        shadowHorizontalOffset: 0
        shadowVerticalOffset: 0
        shadowOpacity: 0
        Accessible.ignored: true

        SequentialAnimation on shadowOpacity {
            running: attentionGlow.visible
            loops: Animation.Infinite

            NumberAnimation {
                to: 0.85
                duration: 800
                easing.type: Easing.InOutSine
            }

            NumberAnimation {
                to: 0.15
                duration: 800
                easing.type: Easing.InOutSine
            }

        }

    }

    // Badge count overlay (unified: SmartLauncher + WatchedNotifications)
    Rectangle {
        id: badge

        visible: dockItem._badgeCount > 0 && DockSettings.badgeDisplayMode !== 2
        anchors.right: iconImage.right
        anchors.top: iconImage.top
        anchors.rightMargin: -width * 0.2
        anchors.topMargin: -height * 0.2
        width: DockSettings.badgeDisplayMode === 1 ? Math.round(iconSize * 0.18) : Math.round(iconSize * 0.38)
        height: width
        radius: width / 2
        color: Kirigami.Theme.highlightColor
        Accessible.ignored: true
        layer.enabled: DockSettings.badgeDisplayMode === 0

        QQC2.Label {
            visible: DockSettings.badgeDisplayMode === 0
            anchors.centerIn: parent
            text: dockItem._badgeCount > 99 ? "99+" : dockItem._badgeCount.toString()
            color: Kirigami.Theme.highlightedTextColor
            font.pixelSize: parent.height * 0.55
            font.bold: true
            fontSizeMode: Text.Fit
            minimumPixelSize: 5
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            width: parent.width - 2
            height: parent.height - 2
        }

        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.alpha("black", 0.4)
            shadowBlur: 0.3
            shadowVerticalOffset: 1
        }

    }

    // Progress bar (SmartLauncher task progress)
    Rectangle {
        id: progressBar

        visible: dockItem._smartLauncherItem !== null && dockItem._smartLauncherItem.progressVisible
        anchors.bottom: iconImage.bottom
        anchors.horizontalCenter: iconImage.horizontalCenter
        anchors.bottomMargin: 2
        width: iconImage.width * 0.8
        height: 3
        radius: 1.5
        color: Qt.alpha(Kirigami.Theme.backgroundColor, 0.6)
        Accessible.ignored: true

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * (dockItem._smartLauncherItem ? dockItem._smartLauncherItem.progress / 100 : 0)
            radius: parent.radius
            color: Kirigami.Theme.highlightColor

            Behavior on width {
                NumberAnimation {
                    duration: Kirigami.Units.shortDuration
                }

            }

        }

    }

    // External drop target highlight (shown when dragging a file over this icon)
    Rectangle {
        anchors.fill: iconImage
        anchors.margins: -Kirigami.Units.smallSpacing
        radius: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing / 2
        color: "transparent"
        border.color: Kirigami.Theme.highlightColor
        border.width: 2
        visible: dockItem.isExternalDropTarget
        opacity: 0.9
    }

    SequentialAnimation {
        id: bounceAnim

        loops: 1
        onFinished: {
            if (dockItem._finishingBounce) {
                dockItem._finishingBounce = false;
                bounceTranslate.x = 0;
                bounceTranslate.y = 0;
            } else if (dockItem.launching) {
                bounceAnim.start();
            } else {
                bounceTranslate.x = 0;
                bounceTranslate.y = 0;
            }
        }

        NumberAnimation {
            target: bounceTranslate
            property: dockItem._bounceProp
            to: dockItem._bounceTarget
            duration: dockItem._finishingBounce ? Kirigami.Units.longDuration * 0.85 : Kirigami.Units.longDuration
            easing.type: Easing.OutQuad
        }

        NumberAnimation {
            target: bounceTranslate
            property: dockItem._bounceProp
            to: 0
            duration: dockItem._finishingBounce ? Kirigami.Units.longDuration * 0.85 : Kirigami.Units.longDuration
            easing.type: Easing.InQuad
        }

    }

    // Type 1: Bounce
    SequentialAnimation {
        running: dockItem._showAttentionAnim && dockItem._attentionType === 1
        onRunningChanged: KremaDebug.anim("bounceAttentionAnim.running=" + running + " for '" + dockItem.displayName + "'" + " | _attentionType=" + dockItem._attentionType)
        loops: Animation.Infinite

        NumberAnimation {
            target: attentionBounceT
            property: dockItem._bounceProp
            to: dockItem._attentionBounceTarget
            duration: 300
            easing.type: Easing.OutQuad
        }

        NumberAnimation {
            target: attentionBounceT
            property: dockItem._bounceProp
            to: 0
            duration: 300
            easing.type: Easing.InBounce
        }

        PauseAnimation {
            duration: 800
        }

    }

    // Type 2: Wiggle
    SequentialAnimation {
        running: dockItem._showAttentionAnim && dockItem._attentionType === 2
        loops: Animation.Infinite

        NumberAnimation {
            target: attentionRotateT
            property: "angle"
            to: 5
            duration: 80
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: attentionRotateT
            property: "angle"
            to: -5
            duration: 160
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: attentionRotateT
            property: "angle"
            to: 3
            duration: 120
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: attentionRotateT
            property: "angle"
            to: -3
            duration: 120
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: attentionRotateT
            property: "angle"
            to: 1
            duration: 100
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: attentionRotateT
            property: "angle"
            to: -1
            duration: 100
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: attentionRotateT
            property: "angle"
            to: 0
            duration: 80
            easing.type: Easing.InOutSine
        }

        PauseAnimation {
            duration: 2000
        }

    }

    // Type 3: Pulse
    SequentialAnimation {
        running: dockItem._showAttentionAnim && dockItem._attentionType === 3
        loops: Animation.Infinite

        NumberAnimation {
            target: attentionScaleT
            property: "xScale"
            to: 1.15
            duration: 600
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: attentionScaleT
            property: "xScale"
            to: 1
            duration: 600
            easing.type: Easing.InOutSine
        }

        PauseAnimation {
            duration: 400
        }

    }

    // Type 5: DotColor — animation on _dotBlinkOpacity
    SequentialAnimation {
        running: dockItem._showAttentionAnim && dockItem._attentionType === 5
        loops: Animation.Infinite

        NumberAnimation {
            target: dockItem
            property: "_dotBlinkOpacity"
            to: 0.3
            duration: 500
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: dockItem
            property: "_dotBlinkOpacity"
            to: 1
            duration: 500
            easing.type: Easing.InOutSine
        }

    }

    // Type 6: Blink — animation on _blinkOpacity
    SequentialAnimation {
        id: blinkAnim

        running: dockItem._showAttentionAnim && dockItem._attentionType === 6
        loops: Animation.Infinite
        onRunningChanged: {
            if (KremaDebug.animEnabled)
                KremaDebug.anim("blinkAnim.running=" + running + " for '" + dockItem.displayName + "'" + " | _showAttentionAnim=" + dockItem._showAttentionAnim + " | _attentionType=" + dockItem._attentionType + " (type: " + typeof dockItem._attentionType + ")");

        }

        NumberAnimation {
            target: dockItem
            property: "_blinkOpacity"
            to: 0.2
            duration: 400
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: dockItem
            property: "_blinkOpacity"
            to: 1
            duration: 400
            easing.type: Easing.InOutSine
        }

    }

    // Status indicator dots (Rule 1: Grounded by Gravity)
    // The dots sit in the 'Inside World' Floor.
    Flow {
        id: indicatorRow

        flow: DockView.isVertical ? Flow.TopToBottom : Flow.LeftToRight
        spacing: Kirigami.Units.smallSpacing
        Accessible.ignored: true
        // --- ABSOLUTE GROUNDING (Rule 1 Sync) ---
        // We anchor the indicators directly to the panel edge of the delegate.
        // This ensures they NEVER move vertically during the zoom wave.
        x: {
            if (DockView.isVertical) {
                if (DockView.edge === 2) return _unitPanelFloor
                if (DockView.edge === 3) return parent.width - implicitWidth - _unitPanelFloor
            }
            return (parent.width - implicitWidth) / 2
        }
        y: {
            if (!DockView.isVertical) {
                if (DockView.edge === 0) return _unitPanelFloor
                if (DockView.edge === 1) return parent.height - implicitHeight - _unitPanelFloor
            }
            return (parent.height - implicitHeight) / 2
        }

        Repeater {
            // Show dots based on window count (max 3)
            model: {
                if (!dockItem.model.IsWindow)
                    return 0;

                let count = dockItem.model.ChildCount || 1;
                return Math.min(count, 3);
            }

            Rectangle {
                // --- THE ACTIVE DASH LOGIC ---
                readonly property int activeDotIndex: dockItem.model.ActiveChildIndex !== undefined ? Math.min(dockItem.model.ActiveChildIndex, 2) : -1
                readonly property bool isThisDotActive: dockItem.model.IsActive && (activeDotIndex === index || (activeDotIndex === -1 && index === 0))

                // If the dock is horizontal, stretch width when active.
                width: DockView.isVertical ? _unitIndicator : (isThisDotActive ? Math.round(iconSize * 0.35) : _unitIndicator)
                // If the dock is vertical, stretch height when active.
                height: DockView.isVertical ? (isThisDotActive ? Math.round(iconSize * 0.35) : _unitIndicator) : _unitIndicator
                // Keep the pill shape completely round at the ends
                radius: _unitIndicator / 2
                color: (dockItem._showAttentionAnim && dockItem._attentionType === 5) ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                opacity: (dockItem._showAttentionAnim && dockItem._attentionType === 5) ? dockItem._dotBlinkOpacity : (dockItem.model.IsMinimized ? 0.4 : 0.8)

                // Smoothly animate the stretching effect
                Behavior on width {
                    NumberAnimation {
                        duration: 250
                        easing.type: Easing.OutBack
                    }

                }

                Behavior on height {
                    NumberAnimation {
                        duration: 250
                        easing.type: Easing.OutBack
                    }

                }
                // -----------------------------

                Behavior on color {
                    ColorAnimation {
                        duration: Kirigami.Units.longDuration
                    }

                }

                Behavior on opacity {
                    enabled: !(dockItem._showAttentionAnim && dockItem._attentionType === 5)

                    NumberAnimation {
                        duration: Kirigami.Units.shortDuration
                    }

                }

            }

        }

    }

    Behavior on opacity {
        NumberAnimation {
            duration: 150
        }

    }

    Behavior on currentScale {
        enabled: _zoomAnimReady

        NumberAnimation {
            duration: Kirigami.Units.shortDuration
            easing.type: Easing.OutCubic
        }

    }

}
