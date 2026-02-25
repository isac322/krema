// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QRect>

namespace krema
{

/// Cached normalization analysis for an icon.
struct IconNormalizationInfo {
    /// Bounding-box ratio: max(content width, height) / probeSize (0.0–1.0).
    qreal contentRatio = 1.0;
    /// Fill ratio within content bounding box (square=~1.0, circle=~π/4≈0.785).
    qreal fillRatio = 1.0;
    /// Size at which the icon was analyzed.
    int probeSize = 0;
    /// Content bounding rect within the probe image.
    QRect contentBounds;
};

/**
 * QML image provider for application icons.
 *
 * Resolves icon names via QIcon::fromTheme() and returns pixmaps
 * at the requested size. Optionally normalizes icon sizes by detecting
 * transparent padding and cropping/rescaling to fill the canvas.
 *
 * Used in QML as:
 *   Image { source: "image://icon/" + iconName + "?v=" + cacheVersion }
 */
class TaskIconProvider : public QQuickImageProvider
{
public:
    explicit TaskIconProvider(bool normalizationEnabled = true);

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

    void setNormalizationEnabled(bool enabled);
    void clearCache();

private:
    /// Find the bounding rect of non-transparent content in an image.
    static QRect findContentBounds(const QImage &image, int threshold = 25);

    /// Analyze an icon to determine its content ratio. Result is cached.
    IconNormalizationInfo analyzeIcon(const QString &iconName, const QIcon &icon);

    /// Load, crop, and scale an icon to fill the target size.
    QPixmap normalizePixmap(const QIcon &icon, int targetSize, const IconNormalizationInfo &info);

    /// Shrink an edge-to-edge icon to add breathing room.
    static QPixmap shrinkPixmap(const QIcon &icon, int targetSize);

    QHash<QString, IconNormalizationInfo> m_cache;
    bool m_normalizationEnabled = true;

    static constexpr int kAlphaThreshold = 25;
    static constexpr qreal kMinContentRatio = 0.92;
    static constexpr qreal kMaxEffectiveScale = 1.5;
    static constexpr qreal kMinMarginRatio = 0.04;
    static constexpr qreal kEdgeToEdgeThreshold = 0.99;
    static constexpr qreal kEdgeToEdgeFill = 0.92;
};

} // namespace krema
