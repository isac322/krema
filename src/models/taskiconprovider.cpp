// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "taskiconprovider.h"

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace krema
{

TaskIconProvider::TaskIconProvider(bool normalizationEnabled)
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
    , m_normalizationEnabled(normalizationEnabled)
{
}

QPixmap TaskIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Parse "org.kde.dolphin?v=0" → iconName = "org.kde.dolphin"
    const int queryIdx = id.indexOf(QLatin1Char('?'));
    const QString iconName = (queryIdx >= 0) ? id.left(queryIdx) : id;

    const int width = requestedSize.width() > 0 ? requestedSize.width() : 48;
    const int height = requestedSize.height() > 0 ? requestedSize.height() : 48;
    const int targetSize = std::max(width, height);

    QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull()) {
        icon = QIcon(iconName);
    }
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    }

    // Fast path: normalization disabled
    if (!m_normalizationEnabled) {
        QPixmap pixmap = icon.pixmap(QSize(targetSize, targetSize));
        if (pixmap.isNull()) {
            pixmap = QPixmap(targetSize, targetSize);
            pixmap.fill(Qt::transparent);
        }
        if (size) {
            *size = pixmap.size();
        }
        return pixmap;
    }

    // Analyze icon padding (cached)
    auto info = analyzeIcon(iconName, icon);

    // Shape correction only applies when there's actual padding to reclaim.
    // If the bounding box already fills the canvas (contentRatio > 0.95), scaling up
    // would clip the icon edges — so skip normalization entirely.
    // For icons WITH padding, use effective ratio that penalizes round shapes
    // (a circle's visual mass is ~sqrt(π/4) ≈ 89% of same-bbox square).
    const bool hasSignificantPadding = info.contentRatio < 0.95;
    const qreal effectiveRatio = hasSignificantPadding ? info.contentRatio * std::sqrt(info.fillRatio) : info.contentRatio;

    // If content fills most of the icon (accounting for shape), skip normalization.
    // Shrink so that content fills exactly kEdgeToEdgeFill of the canvas,
    // matching the target fill used by normalizePixmap for square icons.
    if (effectiveRatio >= kMinContentRatio) {
        const qreal shrinkFactor = (info.contentRatio > kEdgeToEdgeFill) ? kEdgeToEdgeFill / info.contentRatio : 1.0;

        QPixmap result = shrinkPixmap(icon, targetSize, shrinkFactor);
        if (size) {
            *size = result.size();
        }
        return result;
    }

    // Normalize: crop padding and rescale
    QPixmap result = normalizePixmap(icon, targetSize, info);
    if (size) {
        *size = result.size();
    }
    return result;
}

void TaskIconProvider::setNormalizationEnabled(bool enabled)
{
    m_normalizationEnabled = enabled;
}

void TaskIconProvider::clearCache()
{
    m_cache.clear();
}

QRect TaskIconProvider::findContentBounds(const QImage &image, int threshold)
{
    const int w = image.width();
    const int h = image.height();

    if (w == 0 || h == 0) {
        return {};
    }

    int top = h, bottom = -1, left = w, right = -1;

    for (int y = 0; y < h; ++y) {
        const auto *scanline = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (qAlpha(scanline[x]) > threshold) {
                if (y < top)
                    top = y;
                if (y > bottom)
                    bottom = y;
                if (x < left)
                    left = x;
                if (x > right)
                    right = x;
            }
        }
    }

    if (bottom < 0) {
        return {};
    }

    return QRect(left, top, right - left + 1, bottom - top + 1);
}

IconNormalizationInfo TaskIconProvider::analyzeIcon(const QString &iconName, const QIcon &icon)
{
    auto it = m_cache.constFind(iconName);
    if (it != m_cache.constEnd()) {
        return it.value();
    }

    // Determine probe size: use the largest available raster, or 256 for SVG
    const auto sizes = icon.availableSizes();
    int probeSize = 256;
    if (!sizes.isEmpty()) {
        // Find the largest available size
        for (const auto &s : sizes) {
            int maxDim = std::max(s.width(), s.height());
            if (maxDim > probeSize) {
                probeSize = maxDim;
            }
        }
    }
    // For SVG icons, availableSizes() is empty; 256 is a good probe size

    QImage probeImage = icon.pixmap(QSize(probeSize, probeSize)).toImage();
    if (probeImage.isNull() || probeImage.format() != QImage::Format_ARGB32_Premultiplied) {
        probeImage = probeImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    QRect bounds = findContentBounds(probeImage, kAlphaThreshold);

    IconNormalizationInfo info;
    info.probeSize = probeSize;

    if (bounds.isEmpty()) {
        // Fully transparent icon — treat as no padding
        info.contentRatio = 1.0;
        info.fillRatio = 1.0;
        info.contentBounds = QRect(0, 0, probeSize, probeSize);
    } else {
        int contentDim = std::max(bounds.width(), bounds.height());
        info.contentRatio = static_cast<qreal>(contentDim) / probeSize;
        info.contentBounds = bounds;

        // Count non-transparent pixels within content bounds to measure shape fill.
        // Square icon fills ~100% of bbox, circle fills ~π/4 ≈ 78.5%.
        int contentPixels = 0;
        for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
            const auto *scanline = reinterpret_cast<const QRgb *>(probeImage.constScanLine(y));
            for (int x = bounds.left(); x <= bounds.right(); ++x) {
                if (qAlpha(scanline[x]) > kAlphaThreshold) {
                    ++contentPixels;
                }
            }
        }
        const qreal bboxArea = static_cast<qreal>(bounds.width()) * bounds.height();
        info.fillRatio = (bboxArea > 0) ? contentPixels / bboxArea : 1.0;
    }

    m_cache.insert(iconName, info);
    return info;
}

QPixmap TaskIconProvider::normalizePixmap(const QIcon &icon, int targetSize, const IconNormalizationInfo &info)
{
    // Adaptive margin: circular icons (low fillRatio) have inherent visual spacing
    // in their transparent corners, so they need less explicit margin.
    // Square (fill≈1.0) → full margin (4%), Circle (fill≈0.785) → minimal margin (1%).
    const qreal margin = (info.fillRatio < 0.95) ? 0.01 : kMinMarginRatio;
    const qreal maxFill = 1.0 - margin * 2;

    const qreal effectiveRatio = info.contentRatio * std::sqrt(info.fillRatio);
    qreal targetFill = std::min(effectiveRatio * kMaxEffectiveScale, maxFill);
    int targetContentPx = static_cast<int>(std::round(targetSize * targetFill));

    // How large we need to load the icon so that its content region is >= targetContentPx
    int requiredLoadSize = static_cast<int>(std::ceil(static_cast<qreal>(targetContentPx) / info.contentRatio));

    // Determine actual load size
    const auto sizes = icon.availableSizes();
    int loadSize = requiredLoadSize;

    if (!sizes.isEmpty()) {
        // Raster icon: find smallest available size >= requiredLoadSize
        int bestSize = 0;
        int largestAvailable = 0;
        for (const auto &s : sizes) {
            int maxDim = std::max(s.width(), s.height());
            if (maxDim >= requiredLoadSize && (bestSize == 0 || maxDim < bestSize)) {
                bestSize = maxDim;
            }
            if (maxDim > largestAvailable) {
                largestAvailable = maxDim;
            }
        }
        loadSize = (bestSize > 0) ? bestSize : largestAvailable;
    }
    // SVG: loadSize = requiredLoadSize (exact render)

    // Load icon at the determined size
    QImage loadedImage = icon.pixmap(QSize(loadSize, loadSize)).toImage();
    if (loadedImage.isNull()) {
        QPixmap fallback(targetSize, targetSize);
        fallback.fill(Qt::transparent);
        return fallback;
    }
    if (loadedImage.format() != QImage::Format_ARGB32_Premultiplied) {
        loadedImage = loadedImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    // Find actual content bounds in the loaded image
    QRect bounds = findContentBounds(loadedImage, kAlphaThreshold);
    if (bounds.isEmpty()) {
        return icon.pixmap(QSize(targetSize, targetSize));
    }

    // Expand to square, centered on the content center
    int contentDim = std::max(bounds.width(), bounds.height());
    int cx = bounds.center().x();
    int cy = bounds.center().y();
    int half = contentDim / 2;

    QRect squareBounds(cx - half, cy - half, contentDim, contentDim);

    // Clamp to image bounds
    squareBounds = squareBounds.intersected(loadedImage.rect());

    // Crop the content
    QImage cropped = loadedImage.copy(squareBounds);

    // Scale the cropped content to targetContentPx
    QImage scaled = cropped.scaled(targetContentPx, targetContentPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Place centered on the target canvas
    QPixmap result(targetSize, targetSize);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    int offsetX = (targetSize - scaled.width()) / 2;
    int offsetY = (targetSize - scaled.height()) / 2;
    painter.drawImage(offsetX, offsetY, scaled);
    painter.end();

    return result;
}

QPixmap TaskIconProvider::shrinkPixmap(const QIcon &icon, int targetSize, qreal shrinkFactor)
{
    QPixmap original = icon.pixmap(QSize(targetSize, targetSize));
    if (original.isNull()) {
        original = QPixmap(targetSize, targetSize);
        original.fill(Qt::transparent);
        return original;
    }

    int shrunkSize = static_cast<int>(std::round(targetSize * shrinkFactor));
    QImage scaled = original.toImage().scaled(shrunkSize, shrunkSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QPixmap result(targetSize, targetSize);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    int offsetX = (targetSize - scaled.width()) / 2;
    int offsetY = (targetSize - scaled.height()) / 2;
    painter.drawImage(offsetX, offsetY, scaled);
    painter.end();

    return result;
}

} // namespace krema
