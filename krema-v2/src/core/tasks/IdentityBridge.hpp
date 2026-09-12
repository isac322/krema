#pragma once

#include "krema_core_export.h"
#include <QString>
#include <QStringList>
#include <QUrl>

namespace Krema
{

/**
 * @brief IdentityBridge standardizes disparate app identifiers (AppId, LauncherUrl, DisplayName)
 * to prevent "Ghost Icons" (duplicates) and ensure 100% accurate icon resolution.
 * This is the "Armor" ported from the v1 reference project.
 */
class KREMA_CORE_EXPORT IdentityBridge
{
public:
    /**
     * @brief Normalizes a raw AppId by stripping suffixes and handling case-sensitivity.
     */
    static QString normalizeAppId(const QString &rawId);

    /**
     * @brief Generates a list of potential icon names based on all available identity metadata.
     */
    static QStringList getIconCandidates(const QString &rawId, const QUrl &launcherUrl, const QString &displayName);

    /**
     * @brief Resolves the best single icon name from available candidates.
     */
    static QString resolveBestIcon(const QString &rawId, const QUrl &launcherUrl, const QString &displayName);

private:
    static QString stripDesktopSuffix(const QString &id);
};

} // namespace Krema
