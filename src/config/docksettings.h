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

    Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY settingsChanged)
    Q_PROPERTY(int iconSpacing READ iconSpacing WRITE setIconSpacing NOTIFY settingsChanged)
    Q_PROPERTY(qreal maxZoomFactor READ maxZoomFactor WRITE setMaxZoomFactor NOTIFY settingsChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY settingsChanged)
    Q_PROPERTY(bool floating READ floating WRITE setFloating NOTIFY settingsChanged)
    Q_PROPERTY(int visibilityMode READ visibilityMode WRITE setVisibilityMode NOTIFY settingsChanged)
    Q_PROPERTY(int edge READ edge WRITE setEdge NOTIFY settingsChanged)
    Q_PROPERTY(int showDelay READ showDelay WRITE setShowDelay NOTIFY settingsChanged)
    Q_PROPERTY(int hideDelay READ hideDelay WRITE setHideDelay NOTIFY settingsChanged)
    Q_PROPERTY(qreal backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY settingsChanged)
    Q_PROPERTY(bool previewEnabled READ previewEnabled WRITE setPreviewEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int previewThumbnailSize READ previewThumbnailSize WRITE setPreviewThumbnailSize NOTIFY settingsChanged)
    Q_PROPERTY(int previewHoverDelay READ previewHoverDelay WRITE setPreviewHoverDelay NOTIFY settingsChanged)
    Q_PROPERTY(int previewHideDelay READ previewHideDelay WRITE setPreviewHideDelay NOTIFY settingsChanged)

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

    [[nodiscard]] int showDelay() const;
    void setShowDelay(int ms);

    [[nodiscard]] int hideDelay() const;
    void setHideDelay(int ms);

    [[nodiscard]] qreal backgroundOpacity() const;
    void setBackgroundOpacity(qreal opacity);

    // --- Preview ---
    [[nodiscard]] bool previewEnabled() const;
    void setPreviewEnabled(bool enabled);

    [[nodiscard]] int previewThumbnailSize() const;
    void setPreviewThumbnailSize(int size);

    [[nodiscard]] int previewHoverDelay() const;
    void setPreviewHoverDelay(int ms);

    [[nodiscard]] int previewHideDelay() const;
    void setPreviewHideDelay(int ms);

Q_SIGNALS:
    void pinnedLaunchersChanged();
    void settingsChanged();

private:
    void save();

    KSharedConfig::Ptr m_config;
};

} // namespace krema
