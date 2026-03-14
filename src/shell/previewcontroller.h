// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QModelIndex>
#include <QObject>
#include <QTimer>

class KremaSettings;
class QQuickView;

namespace krema
{

class DockModel;
class DockView;

/**
 * Manages the preview popup window lifecycle.
 *
 * Creates a separate layer-shell overlay surface (full-width, transparent)
 * for showing PipeWire window thumbnails when hovering dock items.
 * Handles position calculation, hover transition logic, and input region.
 *
 * Preview keyboard navigation is driven from the dock surface — the dock
 * holds keyboard interactivity and forwards key events via Q_INVOKABLE methods.
 * This avoids unreliable focus transfer between layer-shell surfaces.
 */
class PreviewController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)
    Q_PROPERTY(int parentIndex READ parentIndex NOTIFY parentIndexChanged)
    Q_PROPERTY(QModelIndex rootModelIndex READ rootModelIndex NOTIFY parentIndexChanged)
    Q_PROPERTY(bool isGrouped READ isGrouped NOTIFY parentIndexChanged)
    Q_PROPERTY(QVariantList windowIds READ windowIds NOTIFY parentIndexChanged)
    Q_PROPERTY(QString appName READ appName NOTIFY parentIndexChanged)
    Q_PROPERTY(qreal contentX READ contentX NOTIFY positionChanged)
    Q_PROPERTY(qreal contentY READ contentY NOTIFY positionChanged)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentSizeChanged)

    // Preview keyboard navigation state (driven from dock)
    Q_PROPERTY(bool previewKeyboardActive READ isPreviewKeyboardActive NOTIFY previewKeyboardActiveChanged)
    Q_PROPERTY(int focusedThumbnailIndex READ focusedThumbnailIndex NOTIFY focusedThumbnailIndexChanged)

public:
    explicit PreviewController(DockModel *model, DockView *dockView, KremaSettings *settings, QObject *parent = nullptr);
    ~PreviewController() override;

    /// Create the preview QQuickView and configure layer-shell.
    void initialize();

    [[nodiscard]] bool isVisible() const;
    [[nodiscard]] int parentIndex() const;
    [[nodiscard]] QModelIndex rootModelIndex() const;
    [[nodiscard]] bool isGrouped() const;
    [[nodiscard]] QVariantList windowIds() const;
    [[nodiscard]] QString appName() const;
    [[nodiscard]] qreal contentX() const;
    [[nodiscard]] qreal contentY() const;
    [[nodiscard]] qreal contentWidth() const;
    [[nodiscard]] qreal contentHeight() const;

    [[nodiscard]] bool isPreviewKeyboardActive() const;
    [[nodiscard]] int focusedThumbnailIndex() const;

    /// Show preview for the task at @p index, positioned near the icon.
    /// @p itemGlobalPos is the icon's global X (horizontal) or Y (vertical).
    /// @p itemExtent is the icon's width (horizontal) or height (vertical).
    Q_INVOKABLE void showPreview(int index, qreal itemGlobalPos, qreal itemExtent);

    /// Reconfigure preview surface anchors/margins after edge change.
    void updateEdge();

    /// Hide the preview immediately.
    Q_INVOKABLE void hidePreview();

    /// Hide the preview after a delay (for dock→preview mouse transition).
    Q_INVOKABLE void hidePreviewDelayed();

    /// Cancel a pending delayed hide (mouse entered preview).
    Q_INVOKABLE void cancelHide();

    /// Update whether the mouse is hovering the preview surface.
    Q_INVOKABLE void setPreviewHovered(bool hovered);

    /// Update the content size from QML (after layout).
    Q_INVOKABLE void setContentSize(qreal width, qreal height);

    // --- Preview keyboard navigation (called from dock key handler) ---

    /// Start keyboard navigation in the preview (focus first thumbnail).
    Q_INVOKABLE void startPreviewKeyboardNav();

    /// End keyboard navigation in the preview.
    Q_INVOKABLE void endPreviewKeyboardNav();

    /// Navigate preview thumbnails by delta (-1 = left, +1 = right).
    Q_INVOKABLE void navigatePreviewThumbnail(int delta);

    /// Activate the currently focused preview thumbnail window.
    Q_INVOKABLE void activatePreviewThumbnail();

    /// Close the currently focused preview thumbnail window.
    Q_INVOKABLE void closePreviewThumbnail();

    /// Get the number of preview thumbnails.
    Q_INVOKABLE int previewThumbnailCount() const;

    /// Get the title of the currently focused thumbnail (for screen reader).
    Q_INVOKABLE QString focusedThumbnailTitle() const;

    /// Get whether the focused thumbnail is active/minimized (for screen reader).
    Q_INVOKABLE bool focusedThumbnailIsActive() const;
    Q_INVOKABLE bool focusedThumbnailIsMinimized() const;

    /// Set the hide delay timer interval (from settings).
    void setHideDelay(int ms);

Q_SIGNALS:
    void visibleChanged(bool visible);
    void parentIndexChanged();
    void positionChanged();
    void contentSizeChanged();
    void previewKeyboardActiveChanged();
    void focusedThumbnailIndexChanged();

private:
    void updateInputRegion();
    void applyEdgeLayout();
    void recalcContentPosition();
    void doShow();
    void doHide();

    /// Get the model index for the currently focused thumbnail.
    [[nodiscard]] QModelIndex focusedThumbnailModelIndex() const;

    DockModel *m_model;
    DockView *m_dockView;
    KremaSettings *m_settings;
    QQuickView *m_previewView = nullptr;

    bool m_visible = false;
    bool m_previewHovered = false;
    int m_parentIndex = -1;

    qreal m_contentX = 0;
    qreal m_contentY = 0;
    qreal m_contentWidth = 280;
    qreal m_contentHeight = 200;
    qreal m_itemGlobalPos = 0;
    qreal m_itemExtent = 0;

    // Preview keyboard navigation state
    bool m_previewKeyboardActive = false;
    int m_focusedThumbnailIndex = -1;

    QTimer m_hideTimer;
};

} // namespace krema
