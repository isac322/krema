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

    // Layer-shell configuration: overlay above the dock.
    // IMPORTANT: scope must be "tooltip" so KWin classifies this surface as
    // WindowType::Tooltip (scopeToType() falls back to WindowType::Normal for
    // unknown scopes). A Normal-type always-mapped surface sits at the top of
    // Slide Back's "usable windows" list permanently — so when you raise a real
    // window via the dock, the topmost usable window never changes and the
    // Slide Back effect never fires. Tooltip windows are excluded from usable
    // windows, keeping the stacking "raised" signal intact.
    auto *layerWindow = LayerShellQt::Window::get(m_previewView);
    if (layerWindow) {
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setScope(QStringLiteral("tooltip"));
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

    // NOTE: The surface is intentionally NOT pre-shown. An always-mapped
    // full-width transparent surface appears as a dark/blurred strip along
    // the screen edge (blur effects render behind it) and pollutes KWin's
    // stacking order. The surface is mapped on demand in doShow() and
    // unmapped in doHide().
    qCDebug(lcPreview) << "Preview controller initialized (surface on-demand)";
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

    // Recompute popup position (needs content size from QML) and apply the
    // popup-sized surface layout (anchors + margins + size).
    recalcContentPosition();
    applyEdgeLayout();

    // Map the surface only while a preview is actually shown.
    if (!m_previewView->isVisible()) {
        m_previewView->show();
    }

    if (!m_visible) {
        m_visible = true;

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

    // Unmap the surface completely — no overlay remains on the screen edge.
    // (An always-mapped transparent surface renders as a dark/blurred strip
    // behind blur effects and pollutes KWin's stacking order.)
    // Reset the input mask first so a stale input region cannot leak if the
    // surface is re-mapped before doShow() updates it.
    // IMPORTANT: QRegion() / QRegion(0,0,0,0) is empty → clears mask → accepts ALL input!
    m_previewView->setMask(QRegion(0, 0, 1, 1));
    m_previewView->hide();

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
    const int dockMargin = m_dockView->panelBarHeight() + static_cast<int>(std::ceil(m_settings->iconSize() * (m_settings->maxZoomFactor() - 1.0))) + 4;

    // Anchors: only the dock edge (NOT stretched along it). A surface anchored
    // to both ends is full-screen-wide — blur effects then render behind the
    // whole strip, visible as a dark band whenever the preview is shown.
    // Anchoring only the dock edge lets us size the surface to the popup and
    // position it via margins.
    LayerShellQt::Window::Anchors anchors;
    QMargins margins;

    switch (edge) {
    case DockPlatform::Edge::Bottom:
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        margins.setBottom(dockMargin);
        break;
    case DockPlatform::Edge::Top:
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        margins.setTop(dockMargin);
        break;
    case DockPlatform::Edge::Left:
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        margins.setLeft(dockMargin);
        break;
    case DockPlatform::Edge::Right:
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        margins.setRight(dockMargin);
        break;
    }

    layerWindow->setAnchors(anchors);

    // Position along the dock axis via the unanchored-axis margin (computed in
    // recalcContentPosition so the popup centers on the hovered icon).
    switch (edge) {
    case DockPlatform::Edge::Bottom:
    case DockPlatform::Edge::Top:
        margins.setLeft(m_surfaceMarginAlongDock);
        margins.setRight(0);
        break;
    case DockPlatform::Edge::Left:
    case DockPlatform::Edge::Right:
        margins.setTop(m_surfaceMarginAlongDock);
        margins.setBottom(0);
        break;
    }
    layerWindow->setMargins(margins);

    // Surface size: popup-sized (set in doShow once content size is known);
    // while hidden the surface stays unmapped so no size matters here.
    if (m_visible) {
        const QSize size = surfaceSizeForContent();
        m_previewView->setWidth(size.width());
        m_previewView->setHeight(size.height());
#ifdef KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE
        m_previewView->resize(size);
#else
        layerWindow->setDesiredSize(size);
#endif
    }
}

QSize PreviewController::surfaceSizeForContent() const
{
    // Surface wraps the popup content plus a hover margin so the mouse can
    // travel between dock and preview without leaving the input region.
    constexpr int hoverMargin = 40;
    const int w = qMax(1, static_cast<int>(m_contentWidth) + hoverMargin * 2);
    const int h = qMax(1, static_cast<int>(m_contentHeight) + hoverMargin * 2);
    return QSize(w, h);
}

void PreviewController::recalcContentPosition()
{
    if (!m_previewView) {
        return;
    }

    const auto edge = m_dockView->platform()->edge();
    const bool vertical = (edge == DockPlatform::Edge::Left || edge == DockPlatform::Edge::Right);

    constexpr qreal pad = 8;
    constexpr int hoverMargin = 40;

    const QRect screenGeo = m_dockView->screen() ? m_dockView->screen()->geometry() : QRect(0, 0, 1920, 1080);

    // Popup center along the dock axis, from the icon's global position.
    const qreal popupCenter = m_itemGlobalPos + m_itemExtent / 2.0;

    // Surface size wraps the popup + hover margin (see surfaceSizeForContent).
    const int surfW = qMax(1, static_cast<int>(m_contentWidth) + hoverMargin * 2);
    const int surfH = qMax(1, static_cast<int>(m_contentHeight) + hoverMargin * 2);

    // Position of the popup's left/top within the surface: centered, with the
    // hover margin around it.
    const qreal contentLead = hoverMargin;

    if (vertical) {
        // Vertical dock: surface is positioned along screen Y via layer-shell
        // top margin; popup is horizontally at the surface edge (QML decides
        // left/right based on dock edge).
        m_contentY = contentLead;
        m_contentX = 0; // QML: left edge (Left dock) or right edge (Right dock)

        // Layer-shell margin: distance from screen top to the surface top so
        // the popup centers on the icon.
        qreal surfaceTop = popupCenter - surfH / 2.0;
        surfaceTop = qBound<qreal>(pad, surfaceTop, screenGeo.height() - surfH - pad);
        m_surfaceMarginAlongDock = static_cast<int>(surfaceTop);
    } else {
        // Horizontal dock: surface positioned along screen X via layer-shell
        // left margin; popup is vertically at the surface edge (QML decides
        // top/bottom based on dock edge).
        m_contentX = contentLead;
        m_contentY = 0; // QML: top edge (Top dock) or bottom edge (Bottom dock)

        qreal surfaceLeft = popupCenter - surfW / 2.0;
        surfaceLeft = qBound<qreal>(pad, surfaceLeft, screenGeo.width() - surfW - pad);
        m_surfaceMarginAlongDock = static_cast<int>(surfaceLeft);
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

    // The surface is popup-sized (content + hover margin on all sides), so the
    // entire surface is a valid hover/input area — the mouse can travel between
    // dock and preview without dropping hover.
    m_previewView->setMask(QRegion(0, 0, m_previewView->width(), m_previewView->height()));
}

} // namespace krema
