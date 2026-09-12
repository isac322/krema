#include "IconAnalyzer.hpp"
#include <QDebug>
#include <QSettings>
#include <algorithm>

namespace Krema
{

IconAnalyzer &IconAnalyzer::instance()
{
    static IconAnalyzer inst;
    return inst;
}

IconAnalyzer::IconAnalyzer(QObject *parent)
    : QObject(parent)
{
    QSettings settings("Krema", "IconCache");
    for (const QString &key : settings.allKeys()) {
        m_cache.insert(key, {settings.value(key).toFloat()});
    }
}

IconAnalysisResult IconAnalyzer::analyze(const QString &iconName)
{
    if (iconName.isEmpty())
        return {1.0f};

    auto it = m_cache.constFind(iconName);
    if (it != m_cache.constEnd()) {
        return it.value();
    }

    QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull()) {
        return {1.0f};
    }

    QImage probeImage = icon.pixmap(m_probeSize, m_probeSize).toImage();
    if (probeImage.isNull()) {
        return {1.0f};
    }

    if (probeImage.format() != QImage::Format_ARGB32_Premultiplied) {
        probeImage = probeImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    QRect bounds = findContentBounds(probeImage, m_alphaThreshold);
    IconAnalysisResult result;

    if (bounds.isEmpty()) {
        result.normalizationScale = 1.0f;
    } else {
        int contentDim = std::max(bounds.width(), bounds.height());
        float contentRatio = static_cast<float>(contentDim) / m_probeSize;

        if (contentRatio > 0.05f && contentRatio < m_targetRatio) {
            result.normalizationScale = m_targetRatio / contentRatio;
            result.normalizationScale = std::min(result.normalizationScale, m_maxNormalizationScale);
        } else {
            result.normalizationScale = 1.0f;
        }
    }

    QSettings settings("Krema", "IconCache");
    settings.setValue(iconName, result.normalizationScale);
    m_cache.insert(iconName, result);
    return result;
}

QRect IconAnalyzer::findContentBounds(const QImage &image, int threshold)
{
    const int w = image.width();
    const int h = image.height();
    if (w == 0 || h == 0)
        return {};

    int top = h, bottom = -1, left = w, right = -1;

    for (int y = 0; y < h; ++y) {
        const QRgb *scanline = reinterpret_cast<const QRgb *>(image.constScanLine(y));
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

    return (bottom < 0) ? QRect() : QRect(left, top, right - left + 1, bottom - top + 1);
}

} // namespace Krema
