// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "waylanddockplatform.h"

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPainter>
#include <QRasterWindow>
#include <qpa/qplatformnativeinterface.h>

#include <wayland-client.h>

#include <algorithm>
#include <functional>

Q_LOGGING_CATEGORY(lcWayland, "krema.platform.wayland")

namespace krema
{

namespace
{
/// Transparent 1 px surface: it only has to be mapped for the compositor to
/// place it (and apply its exclusive zone).
class ProbeWindow : public QRasterWindow
{
public:
    /// Called when the surface is mapped (first buffer committed) or unmapped.
    std::function<void(bool)> mappedChanged;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(QRect(QPoint(0, 0), size()), Qt::transparent);
    }

    void exposeEvent(QExposeEvent *event) override
    {
        // QRasterWindow paints and commits its buffer here, synchronously.
        QRasterWindow::exposeEvent(event);
        if (isExposed() != m_mapped) {
            m_mapped = isExposed();
            if (mappedChanged) {
                mappedChanged(m_mapped);
            }
        }
    }

private:
    bool m_mapped = false;
};

/// Commit @p window's pending layer-surface state (exclusive zone) now,
/// without waiting for its next frame. Only for a mapped surface: before its
/// first configure the shell integration owns the commits.
void commitMappedSurface(QWindow *window)
{
    auto *nativeInterface = QGuiApplication::platformNativeInterface();
    if (!window || !window->isExposed() || !nativeInterface) {
        return;
    }
    if (auto *surface = static_cast<wl_surface *>(nativeInterface->nativeResourceForWindow(QByteArrayLiteral("surface"), window))) {
        wl_surface_commit(surface);
    }
}

LayerShellQt::Window::Anchors edgeAnchor(DockPlatform::Edge edge)
{
    switch (edge) {
    case DockPlatform::Edge::Top:
        return LayerShellQt::Window::AnchorTop;
    case DockPlatform::Edge::Left:
        return LayerShellQt::Window::AnchorLeft;
    case DockPlatform::Edge::Right:
        return LayerShellQt::Window::AnchorRight;
    case DockPlatform::Edge::Bottom:
        break;
    }
    return LayerShellQt::Window::AnchorBottom;
}
} // namespace

WaylandDockPlatform::WaylandDockPlatform() = default;

void WaylandDockPlatform::setupWindow(QWindow *window)
{
    m_window = window;
    m_layerWindow = LayerShellQt::Window::get(window);

    if (!m_layerWindow) {
        qCCritical(lcWayland) << "LayerShellQt::Window::get() returned null!";
        return;
    }

    m_layerWindow->setLayer(LayerShellQt::Window::LayerTop);
    // KWin derives a layer surface's window type only from its namespace, and
    // only "dock" maps to a Dock. Any other value makes the dock a normal
    // window, which Show Desktop hides (issue #16).
    m_layerWindow->setScope(QStringLiteral("dock"));
    m_layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_layerWindow->setCloseOnDismissed(false);

    applyAnchors();
}

void WaylandDockPlatform::setScreen(QScreen *screen)
{
    m_screen = screen;
#ifdef KREMA_COMPAT_NO_LAYERSHELL_SCREEN
    // LayerShellQt < 6.6 has no Window::setScreen. With ScreenFromQWindow its
    // get_layer_surface() binds the wl_output of QWindow::screen() when the
    // surface is created on show(), so the Qt screen is the pin.
    if (m_window) {
        m_window->setScreen(screen);
        // Before the platform window exists QtWayland re-derives the screen
        // from the window geometry, so the position must sit on the target
        // output (layer-shell ignores absolute position). Once it exists
        // (LayerShellQt < 6.6 creates it in Window::get()), QtWayland pins a
        // toplevel to its screen origin, and a setPosition() from the stale
        // origin would re-derive the old screen instead.
        if (screen && !m_window->handle()) {
            m_window->setPosition(screen->geometry().topLeft());
        }
    }
    if (m_layerWindow) {
        m_layerWindow->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
    }
#else
    // The LayerShellQt-level screen is what get_layer_surface() binds to;
    // QWindow::setScreen alone does not pin the surface on QtWayland (the
    // platform window re-derives its screen from the window geometry when it
    // is created). The QWindow screen is still set so Qt-side geometry reads
    // see the right screen.
    if (m_window) {
        m_window->setScreen(screen);
    }
    if (m_layerWindow) {
        m_layerWindow->setScreen(screen);
    }
#endif
}

void WaylandDockPlatform::setEdge(Edge edge)
{
    if (m_edge == edge) {
        return;
    }
    // Keep LayerShellQt's current positive desired size while the compositor
    // replaces the old anchor set. Without this intermediate size, changing
    // orientation can briefly leave both old and new stretch axes unset and
    // KWin can reject a zero-width set_size state/commit.
    if (m_window && m_window->width() > 0 && m_window->height() > 0) {
        setSize(m_window->size());
    }
    m_edge = edge;
    if (m_layerWindow) {
        applyAnchors();
    }
}

void WaylandDockPlatform::setExclusiveZone(int zone)
{
    m_exclusiveZone = zone;
    applyExclusiveZone();
}

void WaylandDockPlatform::setPanelRelative(bool enabled)
{
    if (m_panelRelative == enabled) {
        return;
    }
    m_panelRelative = enabled;

    if (!enabled) {
        // Hand the reservation back to the dock surface: its commit and the
        // probe's destruction go out together, so KWin re-arranges once.
        // Two quick re-arranges (the reservation vanishing and returning, or
        // briefly held twice) can leave maximized windows at the
        // intermediate size.
        m_probeMapped = false;
        applyExclusiveZone();
        commitMappedSurface(m_window);
        m_probeLayerWindow = nullptr;
        m_probe.reset();
        return;
    }

    if (!m_window) {
        return;
    }
    auto probe = std::make_unique<ProbeWindow>();
    QSurfaceFormat format = probe->format();
    format.setAlphaBufferSize(8);
    probe->setFormat(format);
    // No input at all: QtWayland sends an empty input region for this flag
    // (an empty mask would accept everything).
    probe->setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    probe->resize(1, 1);
    // The dock surface keeps the reservation until the probe is mapped and
    // placed; then both change in one flush (see applyExclusiveZone).
    probe->mappedChanged = [this](bool mapped) {
        m_probeMapped = mapped;
        applyExclusiveZone();
    };
    m_probeLayerWindow = LayerShellQt::Window::get(probe.get());
    if (m_probeLayerWindow) {
        // The Bottom layer is what makes KWin place the probe after every
        // Top-layer panel, i.e. right inside the space they reserve,
        // whichever started first. Anchored to the edge only, it is never
        // stretched along it, so no perpendicular panel moves it.
        m_probeLayerWindow->setLayer(LayerShellQt::Window::LayerBottom);
        // "dock" keeps it a Dock window for KWin: Show Desktop leaves it
        // alone, and the panel watcher only looks at dock windows.
        m_probeLayerWindow->setScope(QStringLiteral("dock"));
        m_probeLayerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        m_probeLayerWindow->setCloseOnDismissed(false);
        m_probeLayerWindow->setAnchors(edgeAnchor(m_edge));
#ifndef KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE
        m_probeLayerWindow->setDesiredSize(QSize(1, 1));
#endif
    }
    m_probe = std::move(probe);
    applyExclusiveZone();
    // Follow the dock surface: it is re-created (hide + show) on screen
    // changes and after the output went away, and so is the probe.
    QObject::connect(m_window, &QWindow::visibleChanged, m_probe.get(), [this](bool visible) {
        if (visible) {
            showProbe();
        } else if (m_probe) {
            m_probe->hide();
        }
    });
    if (m_window->isVisible()) {
        showProbe();
    }
}

void WaylandDockPlatform::showProbe()
{
    if (!m_probe || !m_window) {
        return;
    }
    // The screen the dock surface is pinned to; QWindow::screen() can still
    // report another one right after creation (see DockView::initialize).
    QScreen *screen = m_screen ? m_screen.data() : m_window->screen();
    // The layer surface binds its output when it is (re)created on show().
    // QtWayland derives the Qt-side screen from the window position until the
    // compositor reports the output, so also place the window on the target
    // screen. (The platform window itself must survive: LayerShellQt set its
    // shell integration when it was created.)
    m_probe->hide();
    m_probe->setScreen(screen);
    if (screen) {
        m_probe->setPosition(screen->geometry().topLeft());
    }
    if (m_probeLayerWindow) {
#ifdef KREMA_COMPAT_NO_LAYERSHELL_SCREEN
        m_probeLayerWindow->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
#else
        m_probeLayerWindow->setScreen(screen);
#endif
    }
    m_probe->show();
}

void WaylandDockPlatform::setMargin(int margin)
{
    m_margin = margin;
    if (m_layerWindow) {
        QMargins margins;
        switch (m_edge) {
        case Edge::Bottom:
            margins.setBottom(margin);
            break;
        case Edge::Top:
            margins.setTop(margin);
            break;
        case Edge::Left:
            margins.setLeft(margin);
            break;
        case Edge::Right:
            margins.setRight(margin);
            break;
        }
        m_layerWindow->setMargins(margins);
    }
}

void WaylandDockPlatform::setVisibilityMode(VisibilityMode mode)
{
    m_visibilityMode = mode;
    applyExclusiveZone();
}

void WaylandDockPlatform::applyExclusiveZone()
{
    if (!m_layerWindow) {
        return;
    }

    const bool reserve = m_visibilityMode == VisibilityMode::AlwaysVisible;
    const int probeZone = reserve ? std::max(m_exclusiveZone, 0) : 0;
    if (m_panelRelative && m_probeMapped && m_probeLayerWindow) {
        // The dock stays at the screen edge and draws its offset from the
        // panels itself; the probe sits right past their reserved space and
        // carries the dock's reservation from there. Both surfaces commit
        // together so KWin re-arranges once (see setPanelRelative).
        m_probeLayerWindow->setExclusiveZone(probeZone);
        m_layerWindow->setExclusiveZone(-1);
        commitMappedSurface(m_probe.get());
        commitMappedSurface(m_window);
        return;
    }

    if (m_panelRelative && m_probeLayerWindow) {
        // The probe maps with its reservation. A dock surface that is not
        // mapped yet (startup, re-creation) never reserves; a mapped one (the
        // setting was just switched) keeps its reservation until the probe
        // maps and takes it over in the same flush.
        m_probeLayerWindow->setExclusiveZone(probeZone);
        if (!m_window || !m_window->isExposed()) {
            m_layerWindow->setExclusiveZone(-1);
            return;
        }
    }

    if (reserve) {
        // Reserve the panel thickness at the anchored edge (the surface is
        // anchored to that edge and both perpendicular ones, so the compositor
        // applies the zone there) so maximized windows end above the dock.
        m_layerWindow->setExclusiveZone(std::max(m_exclusiveZone, 0));
    } else {
        // AutoHide / DodgeWindows: ignore other surfaces' exclusive zones and
        // stay at the real screen edge.
        m_layerWindow->setExclusiveZone(-1);
    }
}

void WaylandDockPlatform::setSize(const QSize &size)
{
#ifdef KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE
    // LayerShellQt < 6.4 derives the layer-surface set_size from the QWindow
    // geometry (zeroing anchor-constrained axes itself), so size the window.
    // A 0 here means "stretch" for the layer surface, not a 0 px window: keep
    // the window's current extent on that axis. A 0 px wide window is never
    // rendered again, so the new size would never be committed.
    if (m_window) {
        m_window->resize(size.width() > 0 ? size.width() : m_window->width(), size.height() > 0 ? size.height() : m_window->height());
    }
#else
    if (m_layerWindow) {
        m_layerWindow->setDesiredSize(size);
    }
#endif
}

void WaylandDockPlatform::setInputRegion(const QRegion &region)
{
    if (m_window) {
        m_window->setMask(region);
    }
}

DockPlatform::Edge WaylandDockPlatform::edge() const
{
    return m_edge;
}

void WaylandDockPlatform::setKeyboardInteractivity(bool enabled)
{
    if (!m_layerWindow) {
        return;
    }
    m_layerWindow->setKeyboardInteractivity(enabled ? LayerShellQt::Window::KeyboardInteractivityExclusive : LayerShellQt::Window::KeyboardInteractivityNone);
}

void WaylandDockPlatform::applyAnchors()
{
    if (!m_layerWindow) {
        return;
    }

    LayerShellQt::Window::Anchors anchors;

    switch (m_edge) {
    case Edge::Bottom:
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        break;
    case Edge::Top:
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        break;
    case Edge::Left:
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        break;
    case Edge::Right:
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        break;
    }

    m_layerWindow->setAnchors(anchors);

    // Reapply margin after anchor change
    setMargin(m_margin);

    if (m_probeLayerWindow) {
        m_probeLayerWindow->setAnchors(edgeAnchor(m_edge));
        static_cast<QPaintDeviceWindow *>(m_probe.get())->update();
    }
}

} // namespace krema
