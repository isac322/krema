// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "debugmanager.h"

Q_LOGGING_CATEGORY(lcApp, "krema.app")
Q_LOGGING_CATEGORY(lcGeom, "krema.geom")
Q_LOGGING_CATEGORY(lcInput, "krema.input")
Q_LOGGING_CATEGORY(lcAnim, "krema.anim")
Q_LOGGING_CATEGORY(lcPreview, "krema.preview")
Q_LOGGING_CATEGORY(lcModel, "krema.model")
Q_LOGGING_CATEGORY(lcShell, "krema.shell")
Q_LOGGING_CATEGORY(lcShader, "krema.shader")
Q_LOGGING_CATEGORY(lcConfig, "krema.config")

namespace krema
{

DebugManager *DebugManager::self()
{
    static DebugManager instance;
    return &instance;
}

DebugManager::DebugManager(QObject *parent)
    : QObject(parent)
{
    for (int i = 0; i < Count; ++i) {
        m_enabled[i] = false;
    }
}

const char *DebugManager::categoryName(Category cat)
{
    switch (cat) {
    case App:
        return "APP";
    case Geom:
        return "GEOM";
    case Input:
        return "INPUT";
    case Anim:
        return "ANIM";
    case Preview:
        return "PREVIEW";
    case Model:
        return "MODEL";
    case Shell:
        return "SHELL";
    case Shader:
        return "SHADER";
    case Config:
        return "CONFIG";
    default:
        return "UNKNOWN";
    }
}

const char *DebugManager::categoryColor(Category cat)
{
    // ANSI Color Codes
    const char *reset = "\x1b[0m";
    const char *bold = "\x1b[1m";
    const char *red = "\x1b[31m";
    const char *green = "\x1b[32m";
    const char *yellow = "\x1b[33m";
    const char *blue = "\x1b[34m";
    const char *magenta = "\x1b[35m";
    const char *cyan = "\x1b[36m";

    switch (cat) {
    case App:
        return bold;
    case Geom:
        return blue; // Bold Blue handled in log handler
    case Input:
        return yellow; // Bold Yellow handled in log handler
    case Anim:
        return magenta;
    case Preview:
        return cyan;
    case Model:
        return yellow;
    case Shell:
        return green;
    case Shader:
        return cyan;
    case Config:
        return magenta;
    default:
        return reset;
    }
}

void DebugManager::app(const QString &msg)
{
    qCDebug(lcApp) << msg;
}
void DebugManager::geom(const QString &msg)
{
    qCDebug(lcGeom) << msg;
}
void DebugManager::input(const QString &msg)
{
    qCDebug(lcInput) << msg;
}
void DebugManager::anim(const QString &msg)
{
    qCDebug(lcAnim) << msg;
}
void DebugManager::preview(const QString &msg)
{
    qCDebug(lcPreview) << msg;
}
void DebugManager::model(const QString &msg)
{
    qCDebug(lcModel) << msg;
}
void DebugManager::shell(const QString &msg)
{
    qCDebug(lcShell) << msg;
}
void DebugManager::shader(const QString &msg)
{
    qCDebug(lcShader) << msg;
}
void DebugManager::config(const QString &msg)
{
    qCDebug(lcConfig) << msg;
}

} // namespace krema
