#include "IdentityBridge.hpp"
#include <QFileInfo>
#include <QIcon>

namespace Krema
{

QString IdentityBridge::normalizeAppId(const QString &rawId)
{
    if (rawId.isEmpty())
        return QString();

    QString id = rawId;
    if (id.startsWith(QLatin1String("applications:"))) {
        id.remove(0, 13);
    }

    id = stripDesktopSuffix(id).toLower();

    // THE IDENTITY BRIDGE: Functional Constraints (Ported from v1)
    // Handle core KDE apps that use inconsistent desktop entry names.
    if (id == QLatin1String("org.kde.dolphin") || id == QLatin1String("dolphin")) {
        return QStringLiteral("org.kde.dolphin");
    } else if (id == QLatin1String("org.kde.systemsettings") || id == QLatin1String("systemsettings")) {
        return QStringLiteral("org.kde.systemsettings");
    } else if (id == QLatin1String("org.kde.konsole") || id == QLatin1String("konsole")) {
        return QStringLiteral("org.kde.konsole");
    }

    return id;
}

QStringList IdentityBridge::getIconCandidates(const QString &rawId, const QUrl &launcherUrl, const QString &displayName)
{
    QStringList candidates;

    auto addCandidate = [&candidates](const QString &value) {
        if (value.isEmpty())
            return;
        if (!candidates.contains(value))
            candidates.append(value);

        QString lowered = value.toLower();
        if (!candidates.contains(lowered))
            candidates.append(lowered);
    };

    QString stripped = stripDesktopSuffix(rawId);

    // 1. Primary Identification
    addCandidate(rawId);
    addCandidate(stripped);

    // 2. Launcher URL Extraction
    if (launcherUrl.isValid()) {
        if (launcherUrl.isLocalFile()) {
            addCandidate(QFileInfo(launcherUrl.toLocalFile()).baseName());
        } else if (launcherUrl.scheme() == QLatin1String("applications")) {
            addCandidate(stripDesktopSuffix(launcherUrl.path()));
        }
    }

    // 3. Display Name
    addCandidate(displayName.trimmed());

    // 4. Steam Mapping
    if (stripped.startsWith(QLatin1String("steam_app_"))) {
        QString steamIcon = stripped;
        steamIcon.replace(QLatin1String("steam_app_"), QLatin1String("steam_icon_"));
        addCandidate(steamIcon);
        addCandidate(QStringLiteral("steam"));
    }

    // 5. Hardcoded Bridges (Rule 9: Functional Preservation)
    if (stripped == QLatin1String("org.kde.dolphin") || stripped == QLatin1String("dolphin")) {
        addCandidate(QStringLiteral("org.kde.dolphin"));
        addCandidate(QStringLiteral("dolphin"));
    } else if (stripped == QLatin1String("org.kde.konsole") || stripped == QLatin1String("konsole")) {
        addCandidate(QStringLiteral("org.kde.konsole"));
        addCandidate(QStringLiteral("konsole"));
    }

    return candidates;
}

QString IdentityBridge::resolveBestIcon(const QString &rawId, const QUrl &launcherUrl, const QString &displayName)
{
    const QStringList candidates = getIconCandidates(rawId, launcherUrl, displayName);

    for (const auto &name : candidates) {
        if (QIcon::hasThemeIcon(name)) {
            return name;
        }
    }

    // Fallback to the most likely candidate
    if (!candidates.isEmpty())
        return candidates.first();

    return QStringLiteral("application-x-executable");
}

QString IdentityBridge::stripDesktopSuffix(const QString &id)
{
    if (id.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
        return id.left(id.length() - 8);
    }
    return id;
}

} // namespace Krema
