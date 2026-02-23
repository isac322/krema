// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QModelIndex>
#include <QObject>
#include <QTimer>

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
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentSizeChanged)

public:
    explicit PreviewController(DockModel *model, DockView *dockView, QObject *parent = nullptr);
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
    [[nodiscard]] qreal contentWidth() const;
    [[nodiscard]] qreal contentHeight() const;

    /// Show preview for the task at @p index, positioned above the icon.
    Q_INVOKABLE void showPreview(int index, qreal itemGlobalX, qreal itemWidth);

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

    /// Set the hide delay timer interval (from settings).
    void setHideDelay(int ms);

Q_SIGNALS:
    void visibleChanged(bool visible);
    void parentIndexChanged();
    void positionChanged();
    void contentSizeChanged();

private:
    void updateInputRegion();
    void doShow();
    void doHide();

    DockModel *m_model;
    DockView *m_dockView;
    QQuickView *m_previewView = nullptr;

    bool m_visible = false;
    bool m_previewHovered = false;
    int m_parentIndex = -1;

    qreal m_contentX = 0;
    qreal m_contentWidth = 280;
    qreal m_contentHeight = 200;
    qreal m_itemGlobalX = 0;
    qreal m_itemWidth = 0;

    QTimer m_hideTimer;
};

} // namespace krema
