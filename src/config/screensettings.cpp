// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "screensettings.h"

#include "krema.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcScreenSettings, "krema.config.screen")

namespace krema
{

ScreenSettings::ScreenSettings(const QString &screenName, KremaSettings *fallback, QObject *parent)
    : QObject(parent)
    , m_screenName(screenName)
    , m_fallback(fallback)
    , m_group(KSharedConfig::openConfig(QStringLiteral("kremarc"))->group(QStringLiteral("Screen-%1").arg(screenName)))
{
    qCDebug(lcScreenSettings) << "Created ScreenSettings for" << screenName << "hasOverrides:" << hasOverrides();

    // Forward global settings changes for non-overridden properties.
    // When a global setting changes and the per-screen group doesn't override it,
    // we re-emit our signal so the DockShell updates.
    connect(m_fallback, &KremaSettings::IconSizeChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("IconSize")))
            Q_EMIT iconSizeChanged();
    });
    connect(m_fallback, &KremaSettings::EdgeChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("Edge")))
            Q_EMIT edgeChanged();
    });
    connect(m_fallback, &KremaSettings::VisibilityModeChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("VisibilityMode")))
            Q_EMIT visibilityModeChanged();
    });
    connect(m_fallback, &KremaSettings::BackgroundStyleChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("BackgroundStyle")))
            Q_EMIT backgroundStyleChanged();
    });
    connect(m_fallback, &KremaSettings::BackgroundOpacityChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("BackgroundOpacity")))
            Q_EMIT backgroundOpacityChanged();
    });
    connect(m_fallback, &KremaSettings::MaxZoomFactorChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("MaxZoomFactor")))
            Q_EMIT maxZoomFactorChanged();
    });
    connect(m_fallback, &KremaSettings::FloatingChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("Floating")))
            Q_EMIT floatingChanged();
    });
    connect(m_fallback, &KremaSettings::CornerRadiusChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("CornerRadius")))
            Q_EMIT cornerRadiusChanged();
    });
}

QString ScreenSettings::screenName() const
{
    return m_screenName;
}

bool ScreenSettings::hasOverrides() const
{
    return m_group.exists() && !m_group.keyList().isEmpty();
}

// --- Readers with fallback ---

int ScreenSettings::iconSize() const
{
    return readWithFallback(QStringLiteral("IconSize"), m_fallback->iconSize());
}

int ScreenSettings::edge() const
{
    return readWithFallback(QStringLiteral("Edge"), m_fallback->edge());
}

int ScreenSettings::visibilityMode() const
{
    return readWithFallback(QStringLiteral("VisibilityMode"), m_fallback->visibilityMode());
}

int ScreenSettings::backgroundStyle() const
{
    return readWithFallback(QStringLiteral("BackgroundStyle"), m_fallback->backgroundStyle());
}

double ScreenSettings::backgroundOpacity() const
{
    return readWithFallback(QStringLiteral("BackgroundOpacity"), m_fallback->backgroundOpacity());
}

double ScreenSettings::maxZoomFactor() const
{
    return readWithFallback(QStringLiteral("MaxZoomFactor"), m_fallback->maxZoomFactor());
}

bool ScreenSettings::floating() const
{
    return readWithFallback(QStringLiteral("Floating"), m_fallback->floating());
}

int ScreenSettings::cornerRadius() const
{
    return readWithFallback(QStringLiteral("CornerRadius"), m_fallback->cornerRadius());
}

QStringList ScreenSettings::pinnedLaunchers() const
{
    if (m_group.hasKey(QStringLiteral("PinnedLaunchers"))) {
        return m_group.readEntry(QStringLiteral("PinnedLaunchers"), QStringList());
    }
    return m_fallback->pinnedLaunchers();
}

// --- Writers ---

void ScreenSettings::setIconSize(int size)
{
    writeOverride(QStringLiteral("IconSize"), size);
    Q_EMIT iconSizeChanged();
}

void ScreenSettings::setEdge(int edge)
{
    writeOverride(QStringLiteral("Edge"), edge);
    Q_EMIT edgeChanged();
}

void ScreenSettings::setVisibilityMode(int mode)
{
    writeOverride(QStringLiteral("VisibilityMode"), mode);
    Q_EMIT visibilityModeChanged();
}

void ScreenSettings::setBackgroundStyle(int style)
{
    writeOverride(QStringLiteral("BackgroundStyle"), style);
    Q_EMIT backgroundStyleChanged();
}

void ScreenSettings::setBackgroundOpacity(double opacity)
{
    writeOverride(QStringLiteral("BackgroundOpacity"), opacity);
    Q_EMIT backgroundOpacityChanged();
}

void ScreenSettings::setMaxZoomFactor(double factor)
{
    writeOverride(QStringLiteral("MaxZoomFactor"), factor);
    Q_EMIT maxZoomFactorChanged();
}

void ScreenSettings::setFloating(bool floating)
{
    writeOverride(QStringLiteral("Floating"), floating);
    Q_EMIT floatingChanged();
}

void ScreenSettings::setCornerRadius(int radius)
{
    writeOverride(QStringLiteral("CornerRadius"), radius);
    Q_EMIT cornerRadiusChanged();
}

void ScreenSettings::setPinnedLaunchers(const QStringList &launchers)
{
    m_group.writeEntry(QStringLiteral("PinnedLaunchers"), launchers);
    Q_EMIT pinnedLaunchersChanged();
}

void ScreenSettings::clearOverrides()
{
    m_group.deleteGroup();
    qCInfo(lcScreenSettings) << "Cleared all overrides for screen:" << m_screenName;
    Q_EMIT hasOverridesChanged();
    // Re-emit all signals so DockShell picks up global defaults
    Q_EMIT iconSizeChanged();
    Q_EMIT edgeChanged();
    Q_EMIT visibilityModeChanged();
    Q_EMIT backgroundStyleChanged();
    Q_EMIT backgroundOpacityChanged();
    Q_EMIT maxZoomFactorChanged();
    Q_EMIT floatingChanged();
    Q_EMIT cornerRadiusChanged();
    Q_EMIT pinnedLaunchersChanged();
}

void ScreenSettings::clearOverride(const QString &key)
{
    m_group.deleteEntry(key);
}

void ScreenSettings::save()
{
    m_group.sync();
}

// --- Template implementations ---

template<typename T>
T ScreenSettings::readWithFallback(const QString &key, T fallbackValue) const
{
    if (m_group.hasKey(key)) {
        return m_group.readEntry(key, fallbackValue);
    }
    return fallbackValue;
}

template<typename T>
void ScreenSettings::writeOverride(const QString &key, T value)
{
    m_group.writeEntry(key, value);
}

} // namespace krema
