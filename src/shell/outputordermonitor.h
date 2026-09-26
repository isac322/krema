// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>
#include <QStringList>

class QScreen;
class KremaWaylandOutputOrder;

namespace krema
{

/**
 * Tracks the compositor's preferred output order (the Plasma "primary"
 * output) through the kde_output_order_v1 Wayland protocol and resolves it
 * to a QScreen.
 *
 * Why this exists: on QtWayland, QGuiApplication::primaryScreen() is the
 * first wl_output the registry announced and it never changes afterwards —
 * it is unrelated to KWin's per-user output priority, which KWin publishes
 * only through kde_output_order_v1 (the protocol plasmashell itself uses for
 * panel placement). Krema is a Plasma-only desktop component, so using this
 * KDE-specific protocol is appropriate; other compositors get the Qt
 * fallback.
 *
 * Access through instance(). The returned object is intentionally leaked for
 * the process lifetime: destroying a Wayland client object after the Qt
 * Wayland platform has torn down the display crashes on some Qt versions, so
 * the monitor is created once and never deleted.
 *
 * Resolution semantics (mirrors LibKWorkspace OutputOrderWatcher):
 *  - The first output name in the order that maps to a live QScreen is the
 *    primary. Unknown names are skipped, so a list arriving slightly before
 *    the matching wl_output still resolves correctly.
 *  - The full received list is adopted only once every name maps to a
 *    QScreen (upstream behavior; keeps the whole order consistent).
 *  - With no protocol (isActive() == false), an empty list, or before the
 *    first 'done' event, falls back to QGuiApplication::primaryScreen().
 */
class OutputOrderMonitor : public QObject
{
    Q_OBJECT

public:
    /// Process-lifetime monitor. Safe to call only once a QGuiApplication
    /// exists; on non-Wayland platforms the protocol client is not created
    /// and every call falls back to QGuiApplication::primaryScreen().
    static OutputOrderMonitor *instance();

    /// The Plasma primary screen: first ordered output mapped to a QScreen,
    /// else QGuiApplication::primaryScreen() (may be nullptr on headless).
    [[nodiscard]] QScreen *primaryScreen() const;

    /// The last adopted output order (names as reported by the compositor).
    [[nodiscard]] QStringList outputOrder() const;

    /// Whether kde_output_order_v1 is advertised and the extension bound.
    [[nodiscard]] bool protocolActive() const;

    /// Whether at least one complete order ('done') was received, or the
    /// protocol is absent so the fallback is already authoritative. Used to
    /// delay initial shell placement until KWin's first list arrives.
    [[nodiscard]] bool orderReady() const;

    /// Pure resolution for tests: index of the first ordered name present in
    /// @p screenNames, or -1.
    static int firstOrderedMatch(const QStringList &order, const QStringList &screenNames);

Q_SIGNALS:
    /// Emitted whenever the resolved primary screen name changes — on order
    /// updates and on screen add/remove while an order is pending.
    void primaryOutputChanged();
    /// Emitted exactly once when orderReady() flips false -> true, after the
    /// order that caused it has been adopted and the primary recomputed.
    void orderReadyChanged();

private:
    explicit OutputOrderMonitor(QObject *parent = nullptr);

    /// Stops the dock from never appearing on a compositor that binds
    /// kde_output_order_v1 but never sends 'done' (non-KWin; KWin always
    /// emits done immediately after binding — verified in KWin's
    /// kde_output_order_v1 implementation which emits done() right after
    /// output_order_v1_bind).
    static constexpr int kOrderReadyFallbackMs = 4000;

    void onOrderReceived(const QStringList &order);
    void onScreenCountChanged();
    void recomputePrimary();

    KremaWaylandOutputOrder *m_wayland = nullptr;
    QStringList m_pendingOrder;
    QStringList m_order;
    QString m_lastPrimaryName;
    bool m_orderReady = false;

    // Unit tests exercise adoption gating through this seam (the slots are
    // private because the Wayland client emits them).
    friend class OutputOrderMonitorTestAccess;
};

} // namespace krema
