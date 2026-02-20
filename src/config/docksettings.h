// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <KSharedConfig>

#include <QObject>
#include <QStringList>

namespace krema
{

/**
 * Persistent dock settings backed by KConfig (~/.config/kremarc).
 *
 * Provides typed getters/setters with sensible defaults.
 * Changes are flushed to disk immediately.
 */
class DockSettings : public QObject
{
    Q_OBJECT

public:
    explicit DockSettings(QObject *parent = nullptr);

    // --- Pinned launchers ---
    [[nodiscard]] QStringList pinnedLaunchers() const;
    void setPinnedLaunchers(const QStringList &launchers);

    // --- Dock appearance ---
    [[nodiscard]] int iconSize() const;
    void setIconSize(int size);

    [[nodiscard]] int iconSpacing() const;
    void setIconSpacing(int spacing);

    [[nodiscard]] qreal maxZoomFactor() const;
    void setMaxZoomFactor(qreal factor);

    [[nodiscard]] int cornerRadius() const;
    void setCornerRadius(int radius);

    [[nodiscard]] bool floating() const;
    void setFloating(bool floating);

    // --- Behavior ---
    [[nodiscard]] int visibilityMode() const;
    void setVisibilityMode(int mode);

    [[nodiscard]] int edge() const;
    void setEdge(int edge);

Q_SIGNALS:
    void pinnedLaunchersChanged();
    void settingsChanged();

private:
    void save();

    KSharedConfig::Ptr m_config;
};

} // namespace krema
