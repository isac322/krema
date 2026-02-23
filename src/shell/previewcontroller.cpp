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

        // Same anchors as the dock: bottom + left + right (full width)
        LayerShellQt::Window::Anchors anchors;
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        layerWindow->setAnchors(anchors);

        // Margin: position above the dock panel + zoom overflow + extra gap
        QMargins margins;
        margins.setBottom(m_dockView->panelBarHeight() + static_cast<int>(std::ceil(m_settings->iconSize() * (m_settings->maxZoomFactor() - 1.0))) + 4);
        layerWindow->setMargins(margins);
    }

    // Singletons are registered globally, no need for per-engine context properties.

    // Set initial size (full screen width, reasonable height)
    const int screenWidth = m_dockView->screen() ? m_dockView->screen()->geometry().width() : 1920;
    m_previewView->setWidth(screenWidth);
    m_previewView->setHeight(400);

    if (layerWindow) {
        layerWindow->setDesiredSize(QSize(screenWidth, 400));
    }

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

qreal PreviewController::contentWidth() const
{
    return m_contentWidth;
}

qreal PreviewController::contentHeight() const
{
    return m_contentHeight;
}

void PreviewController::showPreview(int index, qreal itemGlobalX, qreal itemWidth)
{
    m_hideTimer.stop();

    const bool indexChanged = (m_parentIndex != index);
    m_parentIndex = index;
    m_itemGlobalX = itemGlobalX;
    m_itemWidth = itemWidth;

    // Calculate content position: center the popup above the hovered icon
    const qreal popupCenterX = m_itemGlobalX + m_itemWidth / 2.0;
    m_contentX = popupCenterX - m_contentWidth / 2.0;

    // Clamp to screen bounds
    const int screenWidth = m_previewView ? m_previewView->width() : 1920;
    if (m_contentX < 8) {
        m_contentX = 8;
    }
    if (m_contentX + m_contentWidth > screenWidth - 8) {
        m_contentX = screenWidth - 8 - m_contentWidth;
    }

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
            const qreal popupCenterX = m_itemGlobalX + m_itemWidth / 2.0;
            m_contentX = popupCenterX - m_contentWidth / 2.0;

            const int screenWidth = m_previewView ? m_previewView->width() : 1920;
            if (m_contentX < 8) {
                m_contentX = 8;
            }
            if (m_contentX + m_contentWidth > screenWidth - 8) {
                m_contentX = screenWidth - 8 - m_contentWidth;
            }
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

    // Update margin: position preview above dock panel + zoom overflow + extra gap
    auto *layerWindow = LayerShellQt::Window::get(m_previewView);
    if (layerWindow) {
        QMargins margins;
        margins.setBottom(m_dockView->panelBarHeight() + static_cast<int>(std::ceil(m_settings->iconSize() * (m_settings->maxZoomFactor() - 1.0))) + 4);
        layerWindow->setMargins(margins);
    }

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

    // Block input on the preview surface (keep it mapped for fast re-show)
    updateInputRegion();

    // Release dock visibility lock
    if (m_dockView->visibilityController()) {
        m_dockView->visibilityController()->setInteracting(false);
    }

    Q_EMIT visibleChanged(false);
    Q_EMIT parentIndexChanged();
}

void PreviewController::setHideDelay(int ms)
{
    m_hideTimer.setInterval(ms);
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

    // Accept input only in the popup area + margin to prevent blocking clicks outside.
    // Horizontal: contentX - margin to contentX + contentWidth + margin
    // Vertical: content top (with padding) to surface bottom
    // 40px margin: allows diagonal dock→preview movement and close button access
    const int surfaceH = m_previewView->height();
    const int h = static_cast<int>(m_contentHeight);
    const int top = qMax(0, surfaceH - h - 60);

    constexpr int margin = 40;
    const int x = qMax(0, static_cast<int>(m_contentX) - margin);
    const int right = qMin(m_previewView->width(), static_cast<int>(m_contentX + m_contentWidth) + margin);
    QRegion region(x, top, right - x, surfaceH - top);
    m_previewView->setMask(region);
}

} // namespace krema
