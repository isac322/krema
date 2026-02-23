// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "docksettings.h"

#include <KConfigGroup>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcSettings, "krema.config")

namespace
{
const auto s_group = QStringLiteral("General");

// Default pinned launchers for first run
const QStringList s_defaultLaunchers = {
    QStringLiteral("applications:org.kde.dolphin.desktop"),
    QStringLiteral("applications:org.kde.konsole.desktop"),
    QStringLiteral("applications:org.kde.kate.desktop"),
    QStringLiteral("applications:systemsettings.desktop"),
};
} // namespace

namespace krema
{

DockSettings::DockSettings(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("kremarc")))
{
    qCDebug(lcSettings) << "Settings loaded from:" << m_config->name();
}

// --- Pinned launchers ---

QStringList DockSettings::pinnedLaunchers() const
{
    const KConfigGroup group = m_config->group(s_group);
    return group.readEntry(QStringLiteral("PinnedLaunchers"), s_defaultLaunchers);
}

void DockSettings::setPinnedLaunchers(const QStringList &launchers)
{
    KConfigGroup group = m_config->group(s_group);
    group.writeEntry(QStringLiteral("PinnedLaunchers"), launchers);
    save();
    Q_EMIT pinnedLaunchersChanged();
}

// --- Dock appearance ---

int DockSettings::iconSize() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("IconSize"), 48);
}

void DockSettings::setIconSize(int size)
{
    m_config->group(s_group).writeEntry(QStringLiteral("IconSize"), size);
    save();
    Q_EMIT settingsChanged();
}

int DockSettings::iconSpacing() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("IconSpacing"), 4);
}

void DockSettings::setIconSpacing(int spacing)
{
    m_config->group(s_group).writeEntry(QStringLiteral("IconSpacing"), spacing);
    save();
    Q_EMIT settingsChanged();
}

qreal DockSettings::maxZoomFactor() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("MaxZoomFactor"), 1.6);
}

void DockSettings::setMaxZoomFactor(qreal factor)
{
    m_config->group(s_group).writeEntry(QStringLiteral("MaxZoomFactor"), factor);
    save();
    Q_EMIT settingsChanged();
}

int DockSettings::cornerRadius() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("CornerRadius"), 12);
}

void DockSettings::setCornerRadius(int radius)
{
    m_config->group(s_group).writeEntry(QStringLiteral("CornerRadius"), radius);
    save();
    Q_EMIT settingsChanged();
}

bool DockSettings::floating() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("Floating"), true);
}

void DockSettings::setFloating(bool floating)
{
    m_config->group(s_group).writeEntry(QStringLiteral("Floating"), floating);
    save();
    Q_EMIT settingsChanged();
}

// --- Behavior ---

int DockSettings::visibilityMode() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("VisibilityMode"), 0);
}

void DockSettings::setVisibilityMode(int mode)
{
    m_config->group(s_group).writeEntry(QStringLiteral("VisibilityMode"), mode);
    save();
    Q_EMIT settingsChanged();
}

int DockSettings::edge() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("Edge"), 1); // Bottom
}

void DockSettings::setEdge(int edge)
{
    m_config->group(s_group).writeEntry(QStringLiteral("Edge"), edge);
    save();
    Q_EMIT settingsChanged();
}

int DockSettings::showDelay() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("ShowDelay"), 200);
}

void DockSettings::setShowDelay(int ms)
{
    m_config->group(s_group).writeEntry(QStringLiteral("ShowDelay"), ms);
    save();
    Q_EMIT settingsChanged();
}

int DockSettings::hideDelay() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("HideDelay"), 400);
}

void DockSettings::setHideDelay(int ms)
{
    m_config->group(s_group).writeEntry(QStringLiteral("HideDelay"), ms);
    save();
    Q_EMIT settingsChanged();
}

qreal DockSettings::backgroundOpacity() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("BackgroundOpacity"), 0.6);
}

void DockSettings::setBackgroundOpacity(qreal opacity)
{
    m_config->group(s_group).writeEntry(QStringLiteral("BackgroundOpacity"), opacity);
    save();
    Q_EMIT settingsChanged();
}

// --- Preview ---

bool DockSettings::previewEnabled() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("PreviewEnabled"), true);
}

void DockSettings::setPreviewEnabled(bool enabled)
{
    m_config->group(s_group).writeEntry(QStringLiteral("PreviewEnabled"), enabled);
    save();
    Q_EMIT settingsChanged();
}

int DockSettings::previewThumbnailSize() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("PreviewThumbnailSize"), 200);
}

void DockSettings::setPreviewThumbnailSize(int size)
{
    m_config->group(s_group).writeEntry(QStringLiteral("PreviewThumbnailSize"), size);
    save();
    Q_EMIT settingsChanged();
}

int DockSettings::previewHoverDelay() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("PreviewHoverDelay"), 500);
}

void DockSettings::setPreviewHoverDelay(int ms)
{
    m_config->group(s_group).writeEntry(QStringLiteral("PreviewHoverDelay"), ms);
    save();
    Q_EMIT settingsChanged();
}

int DockSettings::previewHideDelay() const
{
    return m_config->group(s_group).readEntry(QStringLiteral("PreviewHideDelay"), 200);
}

void DockSettings::setPreviewHideDelay(int ms)
{
    m_config->group(s_group).writeEntry(QStringLiteral("PreviewHideDelay"), ms);
    save();
    Q_EMIT settingsChanged();
}

void DockSettings::save()
{
    m_config->sync();
}

} // namespace krema
