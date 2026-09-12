// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "previewcontroller.h"

#include "dockview.h"
#include "dockvisibilitycontroller.h"
#include "krema.h"
#include "models/dockmodel.h"
#include "utils/debugmanager.h"

#include <LayerShellQt/Window>

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <cmath>

#include <QQmlEngine>
#include <QQuickView>
#include <QScreen>

#include "models/hyprlandtasksmodel.h"

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
        layerWindow->setLayer(LayerShellQt::Window::LayerTop);
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

qreal PreviewController::dockHeight() const
{
    return m_dockHeight;
}

void PreviewController::showPreview(int index, qreal itemLocalX, qreal itemLocalY, qreal itemWidth, qreal itemHeight)
{
    qCInfo(lcPreview) << "showPreview CALLED: Index [" << index << "] Local [" << itemLocalX << "," << itemLocalY << "]";
    m_hideTimer.stop();

    const bool indexChanged = (m_parentIndex != index);
    m_parentIndex = index;

    // --- Absolute Sync: Coordinate Reconciliation ---
    // We take the LOCAL coordinates (relative to dock window) and add
    // the dock's own screen offset to get the TRUE Global Y.
    const auto edge = m_dockView->platform()->edge();
    const int dockMargin = m_dockView->floatingPadding();
    const QRect screenGeo = m_dockView->screen() ? m_dockView->screen()->geometry() : QRect(0, 0, 1920, 1080);

    // Start with local
    m_itemGlobalX = itemLocalX;
    m_itemGlobalY = itemLocalY;

    // Add Window Offset to get Screen Coordinates
    if (edge == DockPlatform::Edge::Bottom) {
        m_itemGlobalY += (screenGeo.height() - m_dockView->height() - dockMargin);
        m_itemGlobalX += (screenGeo.width() - m_dockView->width()) / 2.0; // Dock is centered
    } else if (edge == DockPlatform::Edge::Top) {
        m_itemGlobalY += dockMargin;
        m_itemGlobalX += (screenGeo.width() - m_dockView->width()) / 2.0;
    } else if (edge == DockPlatform::Edge::Right) {
        m_itemGlobalX += (screenGeo.width() - m_dockView->width() - dockMargin);
        m_itemGlobalY += (screenGeo.height() - m_dockView->height()) / 2.0;
    } else if (edge == DockPlatform::Edge::Left) {
        m_itemGlobalX += dockMargin;
        m_itemGlobalY += (screenGeo.height() - m_dockView->height()) / 2.0;
    }

    m_itemWidth = itemWidth;
    m_itemHeight = itemHeight;

    recalcContentPosition();

    Q_EMIT positionChanged();

    if (indexChanged) {
        Q_EMIT parentIndexChanged();
    }

    qCDebug(lcPreview) << "RECONCILE:"
                       << "LocalY [" << itemLocalY << "]"
                       << "DockWinH [" << m_dockView->height() << "]"
                       << "ScreenH [" << screenGeo.height() << "]"
                       << "Result TrueY [" << m_itemGlobalY << "]";

    doShow();
}

void PreviewController::setDockHeight(qreal height)
{
    if (m_dockHeight != height) {
        m_dockHeight = height;
        Q_EMIT dockHeightChanged();
        recalcContentPosition();
    }
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
    if (m_model->isHyprland()) {
        m_model->hyprTasksModel()->requestActivate(m_parentIndex);
    } else {
        const QModelIndex idx = focusedThumbnailModelIndex();
        if (!idx.isValid()) {
            return;
        }
        m_model->kdeTasksModel()->requestActivate(idx);
    }
    hidePreview();
}

void PreviewController::closePreviewThumbnail()
{
    if (m_model->isHyprland()) {
        m_model->hyprTasksModel()->requestClose(m_parentIndex);
    } else {
        const QModelIndex idx = focusedThumbnailModelIndex();
        if (!idx.isValid()) {
            return;
        }
        m_model->kdeTasksModel()->requestClose(idx);
    }

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
        bool isWindow =
            parentIdx.data(m_model->isHyprland() ? static_cast<int>(HyprlandTasksModel::IsWindow) : static_cast<int>(TaskManager::AbstractTasksModel::IsWindow))
                .toBool();
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
    return idx.data(m_model->isHyprland() ? static_cast<int>(HyprlandTasksModel::IsActive) : static_cast<int>(TaskManager::AbstractTasksModel::IsActive))
        .toBool();
}

bool PreviewController::focusedThumbnailIsMinimized() const
{
    const QModelIndex idx = focusedThumbnailModelIndex();
    if (!idx.isValid()) {
        return false;
    }
    return idx.data(m_model->isHyprland() ? static_cast<int>(HyprlandTasksModel::IsMinimized) : static_cast<int>(TaskManager::AbstractTasksModel::IsMinimized))
        .toBool();
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

    if (m_model->isHyprland()) {
        return m_model->tasksModel()->index(m_parentIndex, 0);
    } else {
        return m_model->kdeTasksModel()->makeModelIndex(m_parentIndex, m_focusedThumbnailIndex);
    }
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

    const QRect screenGeo = m_dockView->screen() ? m_dockView->screen()->geometry() : QRect(0, 0, 1920, 1080);

    // --- Absolute Sync: Full-Screen Preview Surface ---
    // By making the surface cover the entire monitor, we ensure that
    // global screen coordinates match surface coordinates 1:1.
    // This eliminates offsets caused by dock padding or thickness.
    LayerShellQt::Window::Anchors anchors;
    anchors.setFlag(LayerShellQt::Window::AnchorTop);
    anchors.setFlag(LayerShellQt::Window::AnchorBottom);
    anchors.setFlag(LayerShellQt::Window::AnchorLeft);
    anchors.setFlag(LayerShellQt::Window::AnchorRight);

    layerWindow->setAnchors(anchors);
    layerWindow->setMargins(QMargins(0, 0, 0, 0));
    layerWindow->setExclusiveZone(-1); // LayerTop overlay, don't push windows

    m_previewView->setWidth(screenGeo.width());
    m_previewView->setHeight(screenGeo.height());
#ifdef KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE
    m_previewView->resize(screenGeo.size());
#else
    layerWindow->setDesiredSize(screenGeo.size());
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
    constexpr qreal preview_margin = 12;

    if (vertical) {
        // Vertical dock: center popup vertically on the icon center
        const qreal popupCenter = m_itemGlobalY + m_itemHeight / 2.0;
        m_contentY = popupCenter - m_contentHeight / 2.0;

        const int screenH = m_previewView->height();
        if (m_contentY < pad) {
            m_contentY = pad;
        }
        if (m_contentY + m_contentHeight > screenH - pad) {
            m_contentY = screenH - pad - m_contentHeight;
        }

        // Horizontal placement: Left or Right of the icon
        if (edge == DockPlatform::Edge::Left) {
            m_contentX = m_itemGlobalX + m_itemWidth + preview_margin;
        } else {
            m_contentX = m_itemGlobalX - m_contentWidth - preview_margin;
        }
    } else {
        // Horizontal dock: center popup horizontally on the icon center
        const qreal popupCenter = m_itemGlobalX + m_itemWidth / 2.0;
        m_contentX = popupCenter - m_contentWidth / 2.0;

        const int screenW = m_previewView->width();
        if (m_contentX < pad) {
            m_contentX = pad;
        }
        if (m_contentX + m_contentWidth > screenW - pad) {
            m_contentX = screenW - pad - m_contentWidth;
        }

        // Vertical placement: Above or Below the icon
        if (edge == DockPlatform::Edge::Top) {
            m_contentY = m_itemGlobalY + m_itemHeight + preview_margin;
        } else {
            m_contentY = m_itemGlobalY - m_contentHeight - preview_margin;
        }
    }

    // --- Diagnostic Log: Absolute Geometry Audit ---
    QString edgeName;
    switch (edge) {
    case DockPlatform::Edge::Top:
        edgeName = QStringLiteral("TOP");
        break;
    case DockPlatform::Edge::Bottom:
        edgeName = QStringLiteral("BOTTOM");
        break;
    case DockPlatform::Edge::Left:
        edgeName = QStringLiteral("LEFT");
        break;
    case DockPlatform::Edge::Right:
        edgeName = QStringLiteral("RIGHT");
        break;
    }

    qCInfo(lcPreview) << "GEOM DEBUG:"
                      << "PLACEMENT [" << edgeName << "]"
                      << "Icon Global [" << m_itemGlobalX << "," << m_itemGlobalY << "," << m_itemWidth << "," << m_itemHeight << "]"
                      << "Content Target [" << m_contentX << "," << m_contentY << "]"
                      << "Surface Size [" << m_previewView->width() << "x" << m_previewView->height() << "]";
}

void PreviewController::updateInputRegion()
{
    if (!m_previewView || !m_visible) {
        // Block interaction when hidden (empty QRegion would accept all input)
        m_previewView->setMask(QRegion(0, 0, 1, 1));
        return;
    }

    // --- C++ REGION 3: THE PREVIEW REGION (The "Thumbnail Mask") ---
    // Mask for the separate full-screen preview surface.
    // Defines exactly where thumbnails are interactive.
    // Since the surface is full-screen, we use absolute content coordinates.
    QRegion finalMask;

    const int regionX = static_cast<int>(m_contentX);
    const int regionY = static_cast<int>(m_contentY);
    const int regionW = static_cast<int>(m_contentWidth);
    const int regionH = static_cast<int>(m_contentHeight);

    finalMask += QRect(regionX, regionY, regionW, regionH);

    m_previewView->setMask(finalMask);
}

} // namespace krema
