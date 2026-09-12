#pragma once

#include "krema_core_export.h"
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>

namespace Krema
{

/**
 * @brief Analysis result for an icon.
 */
struct IconAnalysisResult {
    float normalizationScale = 1.0f;
};

/**
 * @brief IconAnalyzer calculates the "Visual Size" of icons by scanning their alpha channel.
 * This prevents the "Firefox vs Terminal" problem where icons with large padding appear smaller.
 */
class KREMA_CORE_EXPORT IconAnalyzer : public QObject
{
    Q_OBJECT
public:
    static IconAnalyzer &instance();

    /**
     * @brief Analyzes an icon and returns a scale factor to normalize its visual size.
     */
    IconAnalysisResult analyze(const QString &iconName);

private:
    explicit IconAnalyzer(QObject *parent = nullptr);

    /**
     * @brief Scans an image to find the tightest bounding box of non-transparent pixels.
     */
    QRect findContentBounds(const QImage &image, int threshold);

    QHash<QString, IconAnalysisResult> m_cache;

    // Config: Matches reference project standards
    const int m_probeSize = 128;
    const int m_alphaThreshold = 25; // Ignore anti-aliasing artifacts and faint shadows
    const float m_targetRatio = 0.85f; // Most well-designed icons occupy ~85% of the slot
    const float m_maxNormalizationScale = 1.35f; // Cap scaling to prevent pixelated/overwhelming icons
};

} // namespace Krema
