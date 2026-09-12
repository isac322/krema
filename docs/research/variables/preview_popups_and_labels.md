# Preview Popup and Icon Labels Research (Deep Implementation Details)

## Overview
This document outlines the architecture and implementation of the preview popup and associated icon labels in Krema. The system is designed for high performance using GPU-accelerated thumbnailing (PipeWire) and integrates closely with KDE's `TaskManager` model.

## Core Architecture
- **PreviewPopup.qml**: The root component for the thumbnail overlay. It uses a layer-shell surface and is driven by `PreviewController` (C++).
- **PreviewThumbnail.qml**: Represents an individual window preview. It handles the live rendering and interaction.
- **PreviewController (C++)**: The C++ singleton (`shell/previewcontroller.h`) managing the preview lifecycle.

## Implementation Details & Mathematical Logic

### 1. Positioning Logic (`recalcContentPosition`)
The popup position is calculated relative to the icon's global screen coordinates to ensure it stays anchored regardless of dock padding or edge settings.

- **Coordinate Reconciliation**:
    - `itemGlobalX`, `itemGlobalY` are derived by taking icon local coordinates and adding the dock's screen-relative offset:
        - `Bottom/Top`: `dockCenter_X` (centered) + offset, `GlobalY` = `ScreenY` + `dockMargin` (or `ScreenH` - `dockMargin`).
    - **Centering**:
        - Horizontal Dock: `m_contentX = (iconCenter_X) - (m_contentWidth / 2.0)`
        - Vertical Dock: `m_contentY = (iconCenter_Y) - (m_contentHeight / 2.0)`
    - **Clamping**: Popup coordinates are clamped to monitor boundaries (respecting a padding of `8px`).

### 2. Surface & Input Management
- **Full-Screen Surface**: The surface (configured via `LayerShellQt::Window`) spans the entire monitor to simplify coordinate math to 1:1 screen mapping.
- **Input Masking (`updateInputRegion`)**:
    - **Logic**: To prevent the transparent, full-screen surface from blocking background interaction, `setMask()` is used.
    - **Variables**: `m_contentX`, `m_contentY`, `m_contentWidth`, `m_contentHeight`.
    - **Mechanism**: The mask is set to exactly the area covered by the popup rectangle. When invisible, it is restricted to a 1x1 pixel in the corner to essentially disable interaction.

### 3. Lifecycle & Hover Transitions
- **`m_hideTimer`**: A single-shot 200ms timer handles the transition gap between exiting the dock and entering the preview popup.
- **State Variables**:
    - `m_visible`: Tracks overall popup visibility.
    - `m_previewHovered`: Tracked via `setPreviewHovered(bool)`. If `hovered` is true, the timer stops, keeping the popup visible. If false, the timer resumes, closing the popup after 200ms.

### 4. Keyboard Navigation Logic
- **Driving from Dock**: Navigation commands (`navigatePreviewThumbnail`, `activatePreviewThumbnail`, etc.) are received by the C++ controller, which updates `m_focusedThumbnailIndex`.
- **Focus Mapping**: `focusedThumbnailModelIndex()` maps the focused index to the `TaskManager` model row/child, ensuring interaction applies to the correct window.

## Settings & Dynamic Behavior
- **Sizing**: `DockSettings.previewThumbnailSize` dictates the thumbnail dimensions.
- **State Handling**: The popup model rebuilds when the `PreviewController.parentIndex` changes or when the underlying task data changes via `DockModel` events.

## Known Challenges & Solutions
- **Race Conditions**: `retryScreencast()` handles PipeWire stream registration latency.
- **Input/Focus**: Keyboard navigation is driven entirely from C++ properties to ensure stability across Wayland surfaces.
- **Ghost Popups**: "Activation Guard" and `IsWindow` gateway filters prevent premature animation-triggered popups.
- **Surface Transitions**: The `hideTimer` in `PreviewController` bridges the gap between mouse exit of an icon and entry into the thumbnail overlay.
