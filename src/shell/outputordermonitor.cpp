// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "outputordermonitor.h"

#include "qwayland-kde-output-order-v1.h"

#include <KWindowSystem>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
#include <QtWaylandClient/QWaylandClientExtension>

Q_LOGGING_CATEGORY(lcOutputOrder, "krema.shell.outputorder")

// Client for kde_output_order_v1. Binds the global and reassembles the
// ordered output list; 'output' events start a new list after each 'done'
// (mirrors plasma-workspace's WaylandOutputOrder).
class KremaWaylandOutputOrder
    : public QWaylandClientExtensionTemplate<KremaWaylandOutputOrder, &QtWayland::kde_output_order_v1::destroy>,
      public QtWayland::kde_output_order_v1
{
    Q_OBJECT
public:
    explicit KremaWaylandOutputOrder(QObject *parent)
        : QWaylandClientExtensionTemplate(1)
    {
        setParent(parent);
        initialize();
    }

protected:
    void kde_output_order_v1_output(const QString &outputName) override
    {
        if (m_done) {
            m_outputOrder.clear();
            m_done = false;
        }
        m_outputOrder.append(outputName);
    }

    void kde_output_order_v1_done() override
    {
        // No output event since the previous done means "no usable output".
        if (m_done) {
            m_outputOrder.clear();
        }
        m_done = true;
        Q_EMIT outputOrderChanged(m_outputOrder);
    }

Q_SIGNALS:
    void outputOrderChanged(const QStringList &outputOrder);

private:
    QStringList m_outputOrder;
    bool m_done = true;
};

namespace krema
{

OutputOrderMonitor *OutputOrderMonitor::instance()
{
    // Deliberately leaked: the object must outlive the Qt Wayland platform
    // teardown, which destroys application children too late for safe
    // wl_proxy destruction. See class docs.
    static auto *monitor = new OutputOrderMonitor();
    return monitor;
}

OutputOrderMonitor::OutputOrderMonitor(QObject *parent)
    : QObject(parent)
{
    if (KWindowSystem::isPlatformWayland()) {
        m_wayland = new KremaWaylandOutputOrder(this);
        if (m_wayland->isActive()) {
            // The extension emits from the Wayland dispatch thread; deliver
            // on the GUI thread so listeners only ever see Qt-side state.
            connect(m_wayland, &KremaWaylandOutputOrder::outputOrderChanged, this, &OutputOrderMonitor::onOrderReceived, Qt::QueuedConnection);
        } else {
            qCDebug(lcOutputOrder) << "kde_output_order_v1 not advertised; using QGuiApplication::primaryScreen() fallback";
        }
    }

    connect(qGuiApp, &QGuiApplication::screenAdded, this, &OutputOrderMonitor::onScreenCountChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OutputOrderMonitor::onScreenCountChanged);
}

int OutputOrderMonitor::firstOrderedMatch(const QStringList &order, const QStringList &screenNames)
{
    for (int i = 0; i < order.size(); ++i) {
        if (screenNames.contains(order.at(i))) {
            return i;
        }
    }
    return -1;
}

QScreen *OutputOrderMonitor::primaryScreen() const
{
    QStringList screenNames;
    const auto screens = QGuiApplication::screens();
    screenNames.reserve(screens.size());
    for (auto *screen : screens) {
        screenNames.append(screen->name());
    }

    const int index = firstOrderedMatch(m_order, screenNames);
    if (index >= 0) {
        for (auto *screen : screens) {
            if (screen->name() == m_order.at(index)) {
                return screen;
            }
        }
    }
    return QGuiApplication::primaryScreen();
}

QStringList OutputOrderMonitor::outputOrder() const
{
    return m_order;
}

bool OutputOrderMonitor::protocolActive() const
{
    return m_wayland && m_wayland->isActive();
}

bool OutputOrderMonitor::orderReady() const
{
    // With no protocol client the Qt fallback is authoritative immediately.
    return m_orderReady || !protocolActive();
}

void OutputOrderMonitor::onOrderReceived(const QStringList &order)
{
    m_orderReady = true;
    Q_EMIT orderReadyChanged();

    m_pendingOrder = order;

    // Adopt the order only once every name maps to a QScreen; the names can
    // arrive slightly before the matching wl_output/QScreen does. Screen
    // changes re-run adoption via onScreenCountChanged().
    const auto screens = QGuiApplication::screens();
    QStringList screenNames;
    screenNames.reserve(screens.size());
    for (auto *screen : screens) {
        screenNames.append(screen->name());
    }
    const bool allMapped = std::all_of(m_pendingOrder.cbegin(), m_pendingOrder.cend(), [&screenNames](const QString &name) {
        return screenNames.contains(name);
    });
    if (allMapped && m_order != m_pendingOrder) {
        m_order = m_pendingOrder;
        qCDebug(lcOutputOrder) << "adopted output order:" << m_order;
    }
    recomputePrimary();
}

void OutputOrderMonitor::onScreenCountChanged()
{
    // Retry adoption of a pending order whose screens have now appeared, and
    // re-resolve the primary (a screen may have gone away under us).
    if (!m_pendingOrder.isEmpty()) {
        onOrderReceived(m_pendingOrder);
        return;
    }
    recomputePrimary();
}

void OutputOrderMonitor::recomputePrimary()
{
    const auto *primary = primaryScreen();
    const QString name = primary ? primary->name() : QString();
    if (name != m_lastPrimaryName) {
        qCDebug(lcOutputOrder) << "primary output:" << m_lastPrimaryName << "->" << name;
        m_lastPrimaryName = name;
        Q_EMIT primaryOutputChanged();
    }
}

} // namespace krema

#include "outputordermonitor.moc"
