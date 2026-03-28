// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "previewcontroller.h"

#include "dockview.h"
#include "dockvisibilitycontroller.h"
#include "krema.h"
#include "models/dockmodel.h"

#include <LayerShellQt/Window>

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <cmath>

#include <QLoggingCategory>
#include <QQmlEngine>
#include <QQuickView>
#include <QScreen>

Q_LOGGING_CATEGORY(lcPreview, "krema.shell.preview")

namespace krema
{

PreviewController::PreviewController(DockModel *model, DockView *dockView, KremaSettings *settings, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_dockView(dockView)
    , m_settings(settings)
{
    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(200);
    connect(&m_hideTimer, &QTimer::timeout, this, [this]() {
        qCDebug(lcPreview) << "Hide timer fired, previewHovered:" << m_previewHovered;
        if (!m_previewHovered) {
            doHide();
        }
    });
}

PreviewController::~PreviewController() = default;

void PreviewController::initialize()
{
    // Share the dock's QQmlEngine so QML types (Kirigami, TaskManager, etc.) are available
    m_previewView = new QQuickView(m_dockView->engine(), nullptr);
    m_previewView->setColor(Qt::transparent);
    m_previewView->setResizeMode(QQuickView::SizeRootObjectToView);

    // Layer-shell configuration: overlay above the dock
    auto *layerWindow = LayerShellQt::Window::get(m_previewView);
    if (layerWindow) {
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setScope(QStringLiteral("krema-preview"));
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layerWindow->setExclusiveZone(0);
        layerWindow->setCloseOnDismissed(false);
    }

    // Configure anchors/margins/size for the current edge
    applyEdgeLayout();

    // Load the preview QML
    m_previewView->setSource(QUrl(QStringLiteral("qrc:/qml/PreviewPopup.qml")));

    if (m_previewView->status() == QQuickView::Error) {
        const auto errs = m_previewView->errors();
        for (const auto &err : errs) {
            qCCritical(lcPreview) << "Preview QML error:" << err.toString();
        }
    }

    // Block all meaningful input with a 1x1 region in the top-left corner.
    // IMPORTANT: QRegion() / QRegion(0,0,0,0) is empty → clears mask → accepts ALL input!
    m_previewView->setMask(QRegion(0, 0, 1, 1));

    // Pre-show the surface so compositor has it mapped and ready for input routing.
    // The 1x1 mask above prevents it from intercepting any meaningful input.
    m_previewView->show();

    qCDebug(lcPreview) << "Preview controller initialized (surface pre-shown)";
}

bool PreviewController::isVisible() const
{
    return m_visible;
}

int PreviewController::parentIndex() const
{
    return m_parentIndex;
}

QModelIndex PreviewController::rootModelIndex() const
{
    if (m_parentIndex < 0) {
        return {};
    }
    return m_model->taskModelIndex(m_parentIndex);
}

bool PreviewController::isGrouped() const
{
    if (m_parentIndex < 0) {
        return false;
    }
    return m_model->childCount(m_parentIndex) > 1;
}

QVariantList PreviewController::windowIds() const
{
    if (m_parentIndex < 0) {
        return {};
    }
    return m_model->windowIds(m_parentIndex);
}

QString PreviewController::appName() const
{
    if (m_parentIndex < 0) {
        return {};
    }
    const QModelIndex idx = m_model->tasksModel()->index(m_parentIndex, 0);
    return idx.data(TaskManager::AbstractTasksModel::AppName).toString();
}

qreal PreviewController::contentX() const
{
    return m_contentX;
}

qreal PreviewController::contentY() const
{
    return m_contentY;
}

qreal PreviewController::contentWidth() const
{
    return m_contentWidth;
}

qreal PreviewController::contentHeight() const
{
    return m_contentHeight;
}

void PreviewController::showPreview(int index, qreal itemGlobalPos, qreal itemExtent)
{
    m_hideTimer.stop();

    const bool indexChanged = (m_parentIndex != index);
    m_parentIndex = index;
    m_itemGlobalPos = itemGlobalPos;
    m_itemExtent = itemExtent;

    recalcContentPosition();

    if (indexChanged) {
        Q_EMIT parentIndexChanged();
    }
    Q_EMIT positionChanged();

    doShow();
}

void PreviewController::hidePreview()
{
    m_hideTimer.stop();
    doHide();
}

void PreviewController::hidePreviewDelayed()
{
    if (!m_visible) {
        return;
    }
    qCDebug(lcPreview) << "hidePreviewDelayed: starting" << m_hideTimer.interval() << "ms timer";
    m_hideTimer.start();
}

void PreviewController::cancelHide()
{
    m_hideTimer.stop();
}

void PreviewController::setPreviewHovered(bool hovered)
{
    qCDebug(lcPreview) << "setPreviewHovered:" << hovered;
    m_previewHovered = hovered;
    if (hovered) {
        m_hideTimer.stop();
    } else {
        if (m_visible) {
            m_hideTimer.start();
        }
    }
}

void PreviewController::setContentSize(qreal width, qreal height)
{
    bool changed = false;
    if (!qFuzzyCompare(m_contentWidth, width)) {
        m_contentWidth = width;
        changed = true;
    }
    if (!qFuzzyCompare(m_contentHeight, height)) {
        m_contentHeight = height;
        changed = true;
    }
    if (changed) {
        // Recalculate position to stay centered on the icon
        if (m_visible && m_parentIndex >= 0) {
            recalcContentPosition();
            Q_EMIT positionChanged();
        }
        Q_EMIT contentSizeChanged();

        // Update input region to match new content size
        if (m_visible) {
            updateInputRegion();
        }
    }
}

void PreviewController::doShow()
{
    if (!m_previewView) {
        return;
    }

    // Reapply edge layout (margins may change with icon size / zoom)
    applyEdgeLayout();

    if (!m_visible) {
        m_visible = true;
        // Surface is always pre-shown — no need to call show().
        // Just update the input region to accept events in the popup area.

        // Lock dock visibility while preview is open
        if (m_dockView->visibilityController()) {
            m_dockView->visibilityController()->setInteracting(true);
        }

        Q_EMIT visibleChanged(true);
    }

    updateInputRegion();
}

void PreviewController::doHide()
{
    if (!m_visible) {
        return;
    }

    m_visible = false;
    m_previewHovered = false;
    m_parentIndex = -1;

    // End preview keyboard nav if active
    if (m_previewKeyboardActive) {
        m_previewKeyboardActive = false;
        m_focusedThumbnailIndex = -1;
        Q_EMIT previewKeyboardActiveChanged();
        Q_EMIT focusedThumbnailIndexChanged();
    }

    // Block input on the preview surface (keep it mapped for fast re-show)
    updateInputRegion();

    // Release dock visibility lock
    if (m_dockView->visibilityController()) {
        m_dockView->visibilityController()->setInteracting(false);
    }

    Q_EMIT visibleChanged(false);
    Q_EMIT parentIndexChanged();
}

// --- Preview keyboard navigation (driven from dock surface) ---

bool PreviewController::isPreviewKeyboardActive() const
{
    return m_previewKeyboardActive;
}

int PreviewController::focusedThumbnailIndex() const
{
    return m_focusedThumbnailIndex;
}

void PreviewController::startPreviewKeyboardNav()
{
    if (!m_visible || m_parentIndex < 0) {
        return;
    }
    m_previewKeyboardActive = true;
    m_focusedThumbnailIndex = 0;
    Q_EMIT previewKeyboardActiveChanged();
    Q_EMIT focusedThumbnailIndexChanged();
}

void PreviewController::endPreviewKeyboardNav()
{
    if (!m_previewKeyboardActive) {
        return;
    }
    m_previewKeyboardActive = false;
    m_focusedThumbnailIndex = -1;
    Q_EMIT previewKeyboardActiveChanged();
    Q_EMIT focusedThumbnailIndexChanged();
}

void PreviewController::navigatePreviewThumbnail(int delta)
{
    if (!m_previewKeyboardActive) {
        return;
    }
    const int count = previewThumbnailCount();
    if (count == 0) {
        return;
    }
    int newIndex = m_focusedThumbnailIndex + delta;
    newIndex = qBound(0, newIndex, count - 1);
    if (newIndex != m_focusedThumbnailIndex) {
        m_focusedThumbnailIndex = newIndex;
        Q_EMIT focusedThumbnailIndexChanged();
    }
}

void PreviewController::activatePreviewThumbnail()
{
    const QModelIndex idx = focusedThumbnailModelIndex();
    if (!idx.isValid()) {
        return;
    }
    m_model->tasksModel()->requestActivate(idx);
    hidePreview();
}

void PreviewController::closePreviewThumbnail()
{
    const QModelIndex idx = focusedThumbnailModelIndex();
    if (!idx.isValid()) {
        return;
    }
    m_model->tasksModel()->requestClose(idx);

    // Adjust focus index if needed
    const int count = previewThumbnailCount();
    if (m_focusedThumbnailIndex >= count - 1) {
        m_focusedThumbnailIndex = qMax(0, count - 2);
        Q_EMIT focusedThumbnailIndexChanged();
    }
}

int PreviewController::previewThumbnailCount() const
{
    if (m_parentIndex < 0) {
        return 0;
    }
    // Single window (no children) counts as 1 thumbnail
    int childCount = m_model->childCount(m_parentIndex);
    if (childCount == 0) {
        // Check if it's a window (not just a launcher)
        const QModelIndex parentIdx = m_model->tasksModel()->index(m_parentIndex, 0);
        bool isWindow = parentIdx.data(TaskManager::AbstractTasksModel::IsWindow).toBool();
        return isWindow ? 1 : 0;
    }
    return childCount;
}

QString PreviewController::focusedThumbnailTitle() const
{
    const QModelIndex idx = focusedThumbnailModelIndex();
    if (!idx.isValid()) {
        return {};
    }
    return idx.data(Qt::DisplayRole).toString();
}

bool PreviewController::focusedThumbnailIsActive() const
{
    const QModelIndex idx = focusedThumbnailModelIndex();
    if (!idx.isValid()) {
        return false;
    }
    return idx.data(TaskManager::AbstractTasksModel::IsActive).toBool();
}

bool PreviewController::focusedThumbnailIsMinimized() const
{
    const QModelIndex idx = focusedThumbnailModelIndex();
    if (!idx.isValid()) {
        return false;
    }
    return idx.data(TaskManager::AbstractTasksModel::IsMinimized).toBool();
}

QModelIndex PreviewController::focusedThumbnailModelIndex() const
{
    if (m_parentIndex < 0 || m_focusedThumbnailIndex < 0) {
        return {};
    }
    int childCount = m_model->childCount(m_parentIndex);
    if (childCount == 0) {
        // Single window: use parent row directly
        return m_model->tasksModel()->index(m_parentIndex, 0);
    }
    if (m_focusedThumbnailIndex >= childCount) {
        return {};
    }
    return m_model->tasksModel()->makeModelIndex(m_parentIndex, m_focusedThumbnailIndex);
}

void PreviewController::setHideDelay(int ms)
{
    m_hideTimer.setInterval(ms);
}

void PreviewController::updateEdge()
{
    if (!m_previewView) {
        return;
    }
    applyEdgeLayout();
}

void PreviewController::applyEdgeLayout()
{
    if (!m_previewView) {
        return;
    }

    auto *layerWindow = LayerShellQt::Window::get(m_previewView);
    if (!layerWindow) {
        return;
    }

    const auto edge = m_dockView->platform()->edge();
    const bool vertical = (edge == DockPlatform::Edge::Left || edge == DockPlatform::Edge::Right);
    const int dockMargin = m_dockView->panelBarHeight() + static_cast<int>(std::ceil(m_settings->iconSize() * (m_settings->maxZoomFactor() - 1.0))) + 4;

    const QRect screenGeo = m_dockView->screen() ? m_dockView->screen()->geometry() : QRect(0, 0, 1920, 1080);

    // Anchors: same edge as dock + stretch along that edge
    LayerShellQt::Window::Anchors anchors;
    QMargins margins;

    switch (edge) {
    case DockPlatform::Edge::Bottom:
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        margins.setBottom(dockMargin);
        break;
    case DockPlatform::Edge::Top:
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        margins.setTop(dockMargin);
        break;
    case DockPlatform::Edge::Left:
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        margins.setLeft(dockMargin);
        break;
    case DockPlatform::Edge::Right:
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        margins.setRight(dockMargin);
        break;
    }

    layerWindow->setAnchors(anchors);
    layerWindow->setMargins(margins);

    // Surface size: stretch along dock axis, 400px in depth axis
    constexpr int previewDepth = 400;
    QSize size;
    if (vertical) {
        size = QSize(previewDepth, screenGeo.height());
    } else {
        size = QSize(screenGeo.width(), previewDepth);
    }

    m_previewView->setWidth(size.width());
    m_previewView->setHeight(size.height());
#ifdef KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE
    m_previewView->resize(size);
#else
    layerWindow->setDesiredSize(size);
#endif
}

void PreviewController::recalcContentPosition()
{
    if (!m_previewView) {
        return;
    }

    const auto edge = m_dockView->platform()->edge();
    const bool vertical = (edge == DockPlatform::Edge::Left || edge == DockPlatform::Edge::Right);

    constexpr qreal pad = 8;

    if (vertical) {
        // Vertical dock: center popup vertically on the icon
        const qreal popupCenter = m_itemGlobalPos + m_itemExtent / 2.0;
        m_contentY = popupCenter - m_contentHeight / 2.0;

        const int screenH = m_previewView->height();
        if (m_contentY < pad) {
            m_contentY = pad;
        }
        if (m_contentY + m_contentHeight > screenH - pad) {
            m_contentY = screenH - pad - m_contentHeight;
        }
        // contentX is determined by QML based on edge (left=0 or right=parent.width-width)
    } else {
        // Horizontal dock: center popup horizontally on the icon
        const qreal popupCenter = m_itemGlobalPos + m_itemExtent / 2.0;
        m_contentX = popupCenter - m_contentWidth / 2.0;

        const int screenW = m_previewView->width();
        if (m_contentX < pad) {
            m_contentX = pad;
        }
        if (m_contentX + m_contentWidth > screenW - pad) {
            m_contentX = screenW - pad - m_contentWidth;
        }
        // contentY is determined by QML based on edge (top=0 or bottom=parent.height-height)
    }
}

void PreviewController::updateInputRegion()
{
    if (!m_previewView) {
        return;
    }

    if (!m_visible) {
        // Hidden: block all meaningful input with a 1x1 region in the corner.
        // IMPORTANT: empty QRegion (including QRegion(0,0,0,0)) clears the mask,
        // which makes the entire surface accept ALL input — the opposite of intended!
        m_previewView->setMask(QRegion(0, 0, 1, 1));
        return;
    }

    constexpr int margin = 40;
    const auto edge = m_dockView->platform()->edge();
    const bool vertical = (edge == DockPlatform::Edge::Left || edge == DockPlatform::Edge::Right);

    if (vertical) {
        // Vertical: input region around contentY area
        const int surfaceW = m_previewView->width();
        const int w = static_cast<int>(m_contentWidth);

        int regionX;
        if (edge == DockPlatform::Edge::Left) {
            // Preview on right side of dock: content at left edge of surface
            regionX = 0;
        } else {
            // Preview on left side of dock: content at right edge of surface
            regionX = qMax(0, surfaceW - w - 60);
        }
        int regionW = surfaceW - regionX;

        const int y = qMax(0, static_cast<int>(m_contentY) - margin);
        const int bottom = qMin(m_previewView->height(), static_cast<int>(m_contentY + m_contentHeight) + margin);
        QRegion region(regionX, y, regionW, bottom - y);
        m_previewView->setMask(region);
    } else {
        // Horizontal: input region around contentX area
        const int surfaceH = m_previewView->height();
        const int h = static_cast<int>(m_contentHeight);

        int regionY;
        if (edge == DockPlatform::Edge::Top) {
            // Preview below dock: content at top edge of surface
            regionY = 0;
        } else {
            // Preview above dock: content at bottom edge of surface
            regionY = qMax(0, surfaceH - h - 60);
        }
        int regionH = surfaceH - regionY;

        const int x = qMax(0, static_cast<int>(m_contentX) - margin);
        const int right = qMin(m_previewView->width(), static_cast<int>(m_contentX + m_contentWidth) + margin);
        QRegion region(x, regionY, right - x, regionH);
        m_previewView->setMask(region);
    }
}

} // namespace krema
