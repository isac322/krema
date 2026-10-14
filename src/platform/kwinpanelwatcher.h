// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>

#include <memory>
#include <optional>

class QDBusServiceWatcher;
class QTemporaryFile;

namespace krema
{

/**
 * Reports where the compositor placed panels, per output.
 *
 * Wayland gives clients no way to read other surfaces' geometry, and the
 * work area (QScreen::availableGeometry, KX11Extras::workArea) is X11-only.
 * KWin's scripting API sees every layer-shell surface, so this loads a KWin
 * script (org.kde.kwin.Scripting) that reports, for each output, the frame
 * of Krema's probe surface (see WaylandDockPlatform) and of every other
 * dock-type surface, whenever one of them is added, removed or moves.
 * KWin re-places the probe whenever a panel's reserved space changes, so
 * those changes are reported too. The script is reloaded when KWin restarts.
 *
 * One instance serves the whole process; docks share it through acquire().
 */
class KWinPanelWatcher : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.bhyoo.krema.PanelWatcher")

public:
    struct OutputPanels {
        QRect geometry;
        /// Frame of Krema's probe surface on this output; null when absent.
        QRect anchor;
        /// Frames of the other processes' dock-type surfaces on this output.
        QList<QRect> panels;
    };

    /// Shared instance, created on first use and destroyed with its last user.
    [[nodiscard]] static std::shared_ptr<KWinPanelWatcher> acquire();

    ~KWinPanelWatcher() override;

    /// False once KWin refused or lacks the scripting API; true again when a
    /// restarted KWin accepts the script.
    [[nodiscard]] bool isAvailable() const;

    /// Last report for @p outputName; empty when it has not been reported.
    [[nodiscard]] OutputPanels output(const QString &outputName) const;

    /// Last reported cursor position in global screen coordinates; nullopt before the first report.
    [[nodiscard]] std::optional<QPoint> cursorPos() const;
public Q_SLOTS:
    /// Called by the KWin script over D-Bus with a JSON object keyed by output
    /// name: {"geometry": [x,y,w,h], "anchor": [x,y,w,h] | null, "panels": [[x,y,w,h], ...]}.
    Q_SCRIPTABLE void updatePanels(const QString &json);
    /// Called by the KWin script on pointer motion while at least one dock
    /// watches the outer screen edge.
    Q_SCRIPTABLE void notifyCursorMoved(int x, int y);

Q_SIGNALS:
    void panelsChanged();
    void availableChanged();
    void cursorMoved(const QPoint &pos);

private:
    KWinPanelWatcher();

    [[nodiscard]] bool ensureScriptFile();
    void load();
    void unload();
    void setAvailable(bool available);

    QString m_objectPath;
    std::unique_ptr<QTemporaryFile> m_scriptFile;
    std::unique_ptr<QDBusServiceWatcher> m_kwinWatcher;
    QString m_pluginName; // non-empty while loaded
    quint64 m_generation = 0;
    bool m_available = true;
    std::optional<QPoint> m_cursorPos;
    QHash<QString, OutputPanels> m_outputs;
};

} // namespace krema
