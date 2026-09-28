// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QTemporaryFile;

namespace krema
{

/**
 * Reports the first pointer motion anywhere on screen.
 *
 * Wayland delivers pointer events only to the surface under the pointer, so
 * the dock cannot see the pointer moving over other windows. While armed,
 * this loads a small KWin script (org.kde.kwin.Scripting) that waits for
 * KWin's `workspace.cursorPosChanged` and reports the first change back over
 * D-Bus; the script is unloaded when disarmed. Without KWin scripting the
 * watcher does nothing.
 */
class KWinPointerMotionWatcher : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.bhyoo.krema.PointerMotionWatcher")

public:
    explicit KWinPointerMotionWatcher(QObject *parent = nullptr);
    ~KWinPointerMotionWatcher() override;

    /// Start watching; pointerMoved() is emitted at most once per arm().
    void arm();
    /// Stop watching and unload the KWin script.
    void disarm();

public Q_SLOTS:
    /// Called by the KWin script over D-Bus.
    Q_SCRIPTABLE void notifyPointerMoved();

Q_SIGNALS:
    void pointerMoved();

private:
    [[nodiscard]] bool ensureScriptFile();

    int m_instance;
    QString m_objectPath;
    std::unique_ptr<QTemporaryFile> m_scriptFile;
    QString m_pluginName; // non-empty while armed
    quint64 m_generation = 0;
};

} // namespace krema
