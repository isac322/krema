// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "screensettings.h"

#include "krema.h"

#include "utils/debugmanager.h"

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
        if (!m_group.hasKey(QStringLiteral("IconSize"))) {
            Q_EMIT iconSizeChanged();
        } else {
            qCWarning(lcConfig).nospace() << "DESYNC BLOCKED: Global 'IconSize' changed, but screen '" << m_screenName << "' is enforcing override '"
                                          << m_group.readEntry(QStringLiteral("IconSize")) << "'. Update ignored.";
        }
    });
    connect(m_fallback, &KremaSettings::EdgeChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("Edge"))) {
            Q_EMIT edgeChanged();
        } else {
            qCWarning(lcConfig).nospace() << "DESYNC BLOCKED: Global 'Edge' changed, but screen '" << m_screenName << "' is enforcing override '"
                                          << m_group.readEntry(QStringLiteral("Edge")) << "'. Update ignored.";
        }
    });
    connect(m_fallback, &KremaSettings::AlignmentChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("Alignment"))) {
            Q_EMIT alignmentChanged();
        } else {
            qCWarning(lcConfig).nospace() << "DESYNC BLOCKED: Global 'Alignment' changed, but screen '" << m_screenName << "' is enforcing override '"
                                          << m_group.readEntry(QStringLiteral("Alignment")) << "'. Update ignored.";
        }
    });
    connect(m_fallback, &KremaSettings::VisibilityModeChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("VisibilityMode"))) {
            Q_EMIT visibilityModeChanged();
        } else {
            qCWarning(lcConfig).nospace() << "DESYNC BLOCKED: Global 'VisibilityMode' changed, but screen '" << m_screenName
                                          << "' is enforcing override. Update ignored.";
        }
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
    connect(m_fallback, &KremaSettings::PanelHeightChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("PanelHeight")))
            Q_EMIT panelHeightChanged();
    });
    connect(m_fallback, &KremaSettings::SeparatorStyleChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("SeparatorStyle")))
            Q_EMIT separatorStyleChanged();
    });
    connect(m_fallback, &KremaSettings::SeparatorOpacityChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("SeparatorOpacity")))
            Q_EMIT separatorOpacityChanged();
    });
    connect(m_fallback, &KremaSettings::SeparatorWidthChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("SeparatorWidth")))
            Q_EMIT separatorWidthChanged();
    });
    connect(m_fallback, &KremaSettings::MaxLengthChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("MaxLength")))
            Q_EMIT maxLengthChanged();
    });
    connect(m_fallback, &KremaSettings::PanelLengthModeChanged, this, [this]() {
        if (!m_group.hasKey(QStringLiteral("PanelLengthMode")))
            Q_EMIT panelLengthModeChanged();
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
    int val = readWithFallback(QStringLiteral("IconSize"), m_fallback->iconSize());
    bool isOverride = m_group.hasKey(QStringLiteral("IconSize"));
    qCDebug(lcScreenSettings) << "ScreenSettings::iconSize() for" << m_screenName << "Value:" << val << "IsOverride:" << isOverride;
    return val;
}

int ScreenSettings::edge() const
{
    return readWithFallback(QStringLiteral("Edge"), m_fallback->edge());
}

int ScreenSettings::alignment() const
{
    return readWithFallback(QStringLiteral("Alignment"), m_fallback->alignment());
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

int ScreenSettings::panelHeight() const
{
    return readWithFallback(QStringLiteral("PanelHeight"), m_fallback->panelHeight());
}

int ScreenSettings::separatorStyle() const
{
    return readWithFallback(QStringLiteral("SeparatorStyle"), m_fallback->separatorStyle());
}

double ScreenSettings::separatorOpacity() const
{
    return readWithFallback(QStringLiteral("SeparatorOpacity"), m_fallback->separatorOpacity());
}

int ScreenSettings::separatorWidth() const
{
    return readWithFallback(QStringLiteral("SeparatorWidth"), m_fallback->separatorWidth());
}

int ScreenSettings::maxLength() const
{
    return readWithFallback(QStringLiteral("MaxLength"), m_fallback->maxLength());
}

int ScreenSettings::panelLengthMode() const
{
    return readWithFallback(QStringLiteral("PanelLengthMode"), m_fallback->panelLengthMode());
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

void ScreenSettings::setAlignment(int alignment)
{
    writeOverride(QStringLiteral("Alignment"), alignment);
    Q_EMIT alignmentChanged();
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

void ScreenSettings::setPanelHeight(int height)
{
    writeOverride(QStringLiteral("PanelHeight"), height);
    Q_EMIT panelHeightChanged();
}

void ScreenSettings::setSeparatorStyle(int style)
{
    writeOverride(QStringLiteral("SeparatorStyle"), style);
    Q_EMIT separatorStyleChanged();
}

void ScreenSettings::setSeparatorOpacity(double opacity)
{
    writeOverride(QStringLiteral("SeparatorOpacity"), opacity);
    Q_EMIT separatorOpacityChanged();
}

void ScreenSettings::setSeparatorWidth(int width)
{
    writeOverride(QStringLiteral("SeparatorWidth"), width);
    Q_EMIT separatorWidthChanged();
}

void ScreenSettings::setMaxLength(int length)
{
    writeOverride(QStringLiteral("MaxLength"), length);
    Q_EMIT maxLengthChanged();
}

void ScreenSettings::setPanelLengthMode(int mode)
{
    if (m_group.hasKey(QStringLiteral("PanelLengthMode")) && m_group.readEntry(QStringLiteral("PanelLengthMode"), 0) == mode)
        return;
    writeOverride(QStringLiteral("PanelLengthMode"), mode);
    Q_EMIT panelLengthModeChanged();
}

void ScreenSettings::clearOverrides()
{
    m_group.deleteGroup();
    qCInfo(lcScreenSettings) << "Cleared all overrides for screen:" << m_screenName;
    Q_EMIT hasOverridesChanged();
    // Re-emit all signals so DockShell picks up global defaults
    Q_EMIT iconSizeChanged();
    Q_EMIT edgeChanged();
    Q_EMIT alignmentChanged();
    Q_EMIT visibilityModeChanged();
    Q_EMIT backgroundStyleChanged();
    Q_EMIT backgroundOpacityChanged();
    Q_EMIT maxZoomFactorChanged();
    Q_EMIT floatingChanged();
    Q_EMIT cornerRadiusChanged();
    Q_EMIT pinnedLaunchersChanged();
    Q_EMIT panelHeightChanged();
    Q_EMIT separatorStyleChanged();
    Q_EMIT separatorOpacityChanged();
    Q_EMIT separatorWidthChanged();
    Q_EMIT maxLengthChanged();
    Q_EMIT panelLengthModeChanged();
}

void ScreenSettings::clearOverride(const QString &key)
{
    if (m_group.hasKey(key)) {
        m_group.deleteEntry(key);
        qCInfo(lcConfig).nospace() << "Override '" << key << "' cleared for screen '" << m_screenName << "'. Reverting to global fallback.";

        if (key == QStringLiteral("IconSize"))
            Q_EMIT iconSizeChanged();
        else if (key == QStringLiteral("Edge"))
            Q_EMIT edgeChanged();
        else if (key == QStringLiteral("Alignment"))
            Q_EMIT alignmentChanged();
        else if (key == QStringLiteral("VisibilityMode"))
            Q_EMIT visibilityModeChanged();
        else if (key == QStringLiteral("BackgroundStyle"))
            Q_EMIT backgroundStyleChanged();
        else if (key == QStringLiteral("BackgroundOpacity"))
            Q_EMIT backgroundOpacityChanged();
        else if (key == QStringLiteral("MaxZoomFactor"))
            Q_EMIT maxZoomFactorChanged();
        else if (key == QStringLiteral("Floating"))
            Q_EMIT floatingChanged();
        else if (key == QStringLiteral("CornerRadius"))
            Q_EMIT cornerRadiusChanged();
        else if (key == QStringLiteral("PanelHeight"))
            Q_EMIT panelHeightChanged();
        else if (key == QStringLiteral("SeparatorStyle"))
            Q_EMIT separatorStyleChanged();
        else if (key == QStringLiteral("SeparatorOpacity"))
            Q_EMIT separatorOpacityChanged();
        else if (key == QStringLiteral("SeparatorWidth"))
            Q_EMIT separatorWidthChanged();
        else if (key == QStringLiteral("MaxLength"))
            Q_EMIT maxLengthChanged();
        else if (key == QStringLiteral("PanelLengthMode"))
            Q_EMIT panelLengthModeChanged();
    }
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
        T val = m_group.readEntry(key, fallbackValue);
        qCDebug(lcConfig).nospace() << "Screen '" << m_screenName << "' resolving '" << key << "' -> OVERRIDE: " << val;
        return val;
    }
    qCDebug(lcConfig).nospace() << "Screen '" << m_screenName << "' resolving '" << key << "' -> GLOBAL FALLBACK: " << fallbackValue;
    return fallbackValue;
}

template<typename T>
void ScreenSettings::writeOverride(const QString &key, T value)
{
    m_group.writeEntry(key, value);
    qCInfo(lcConfig).nospace() << "Override '" << key << "' set to '" << value << "' for screen '" << m_screenName << "'.";
}

} // namespace krema
