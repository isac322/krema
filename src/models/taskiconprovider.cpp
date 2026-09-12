// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "taskiconprovider.h"

#include "../utils/peiconextractor.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace krema
{

TaskIconProvider::TaskIconProvider(bool normalizationEnabled)
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
    , m_normalizationEnabled(normalizationEnabled)
{
}

void TaskIconProvider::setNormalizationEnabled(bool enabled)
{
    m_normalizationEnabled = enabled;
}

void TaskIconProvider::setIndicatorOffset(double offset)
{
    m_indicatorOffset = std::clamp(offset, 0.5, 1.0);
}

void TaskIconProvider::clearCache()
{
    m_cache.clear();
}

QPixmap TaskIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    using namespace Qt::StringLiterals;

    const int queryIdx = id.indexOf(QLatin1Char('?'));
    QString iconName = (queryIdx >= 0) ? id.left(queryIdx) : id;

    if (iconName.startsWith(u"file://"_s))
        iconName.remove(0, 7);
    if (iconName.endsWith(u".desktop"_s))
        iconName.chop(8);

    QString originalId = iconName;
    const int targetSize = std::max(requestedSize.width() > 0 ? requestedSize.width() : 48, requestedSize.height() > 0 ? requestedSize.height() : 48);

    QIcon icon;
    bool isDebug = QCoreApplication::arguments().contains(u"--debug-icons"_s);

    // 0. STAGE 0: The Steam Hunter
    if (originalId.startsWith(u"steam_app_") || originalId.startsWith(u"steam_icon_")) {
        QString appId = originalId.startsWith(u"steam_app_") ? originalId.mid(10) : originalId.mid(11);
        QString nativeExe = resolveSteamExePath(appId);
        if (!nativeExe.isEmpty()) {
            QImage rawImg = PeIconExtractor::extract(nativeExe);
            if (!rawImg.isNull()) {
                if (isDebug) {
                    qDebug().noquote() << "\x1b[35m[ICON SOURCE]\x1b[0m" << originalId << "-> \x1b[32mNATIVE EXE EXTRACTOR (Steam Hunter)\x1b[0m (" << nativeExe
                                       << ")";
                }
                icon = QIcon(QPixmap::fromImage(rawImg));
            }
        }
        if (icon.isNull()) {
            icon = resolveSteamIconLocal(appId);
        }
    }

    // 1. STAGE 1: Direct Raw .exe Catch (Non-Steam)
    if (icon.isNull() && originalId.endsWith(u".exe", Qt::CaseInsensitive)) {
        QImage rawImg = PeIconExtractor::extract(originalId);
        if (!rawImg.isNull()) {
            if (isDebug) {
                qDebug().noquote() << "\x1b[35m[ICON SOURCE]\x1b[0m" << originalId << "-> \x1b[32mNATIVE EXE EXTRACTOR (Direct Path)\x1b[0m";
            }
            icon = QIcon(QPixmap::fromImage(rawImg));
        }
    }

    // 2. STAGE 2: Theme Check
    // We try loading it directly instead of checking hasThemeIcon first to avoid library glitches
    if (icon.isNull()) {
        icon = QIcon::fromTheme(iconName);
    }

    QFile dbgFile(u"/tmp/krema_icon_debug.txt"_s);
    if (dbgFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&dbgFile);
        out << "=== REQUEST: " << id << " ===\n";
        out << "originalId: " << originalId << "\n";
        out << "iconName: " << iconName << "\n";
        out << "STAGE 2 fromTheme name: " << icon.name() << " isNull: " << icon.isNull() << " sizesEmpty: " << icon.availableSizes().isEmpty() << "\n";
    }

    // 2.5. STAGE 2.5: Flatpak Direct Extractor
    // Notice we added icon.name().isEmpty() to the bypass list!
    if (icon.isNull() || icon.availableSizes().isEmpty() || icon.name().isEmpty() || icon.name() == u"image-missing"_s || icon.name() == u"unknown"_s
        || icon.name() == u"wayland"_s || icon.name() != iconName) {
        if (dbgFile.isOpen()) {
            QTextStream(&dbgFile) << "-> ENTERED STAGE 2.5\n";
        }
        QStringList flatpakBases = {QDir::homePath() + u"/.local/share/flatpak/exports/share/icons/hicolor/"_s,
                                    u"/var/lib/flatpak/exports/share/icons/hicolor/"_s};
        QStringList sizes = {u"512x512/apps/"_s, u"256x256/apps/"_s, u"128x128/apps/"_s, u"64x64/apps/"_s, u"48x48/apps/"_s, u"scalable/apps/"_s};

        QString normalizedId = originalId;
        normalizedId = normalizedId.replace(u'_', u'-').replace(u' ', u'-').toLower();

        for (const QString &base : flatpakBases) {
            for (const QString &size : sizes) {
                QString pngPath = base + size + originalId + u".png"_s;
                if (QFile::exists(pngPath)) {
                    icon = QIcon(pngPath);
                    if (dbgFile.isOpen()) {
                        QTextStream(&dbgFile) << "-> FOUND EXACT PNG: " << pngPath << "\n";
                    }
                    break;
                }
                QString svgPath = base + size + originalId + u".svg"_s;
                if (QFile::exists(svgPath)) {
                    icon = QIcon(svgPath);
                    if (dbgFile.isOpen()) {
                        QTextStream(&dbgFile) << "-> FOUND EXACT SVG: " << svgPath << "\n";
                    }
                    break;
                }

                // Fallback: Suffix matching for short Wayland classes
                QDir appDir(base + size);
                if (appDir.exists()) {
                    QStringList filters = {u"*.png"_s, u"*.svg"_s};
                    QStringList matches = appDir.entryList(filters, QDir::Files);
                    for (const QString &match : matches) {
                        QString normalizedMatch = match;
                        normalizedMatch = normalizedMatch.replace(u'_', u'-').replace(u' ', u'-').toLower();

                        QString strippedMatch = normalizedMatch;
                        strippedMatch = strippedMatch.replace(u'-', u""_s);
                        QString strippedId = normalizedId;
                        strippedId = strippedId.replace(u'-', u""_s);

                        if (normalizedMatch == normalizedId + u".png"_s || normalizedMatch == normalizedId + u".svg"_s
                            || normalizedMatch.endsWith(u"."_s + normalizedId + u".png"_s) || normalizedMatch.endsWith(u"."_s + normalizedId + u".svg"_s)
                            || strippedMatch.endsWith(u"."_s + strippedId + u".png"_s) || strippedMatch.endsWith(u"."_s + strippedId + u".svg"_s)) {
                            QString finalPath = appDir.absoluteFilePath(match);
                            icon = QIcon(finalPath);
                            if (dbgFile.isOpen()) {
                                QTextStream(&dbgFile) << "-> FOUND SUFFIX MATCH: " << finalPath << "\n";
                            }
                            break;
                        }
                    }
                }
                if (!icon.isNull())
                    break;
            }
            if (!icon.isNull())
                break;
        }
    }

    // 3. STAGE 3: Desktop File Bridge
    if (icon.isNull()) {
        QString desktopFile = iconName + u".desktop"_s;
        QStringList paths = QStandardPaths::locateAll(QStandardPaths::ApplicationsLocation, desktopFile);
        if (!paths.isEmpty()) {
            QSettings settings(paths.first(), QSettings::IniFormat);
            settings.beginGroup(u"Desktop Entry"_s);
            QString realIcon = settings.value(u"Icon"_s).toString();
            if (!realIcon.isEmpty()) {
                if (realIcon.endsWith(u".exe", Qt::CaseInsensitive)) {
                    QImage rawImg = PeIconExtractor::extract(realIcon);
                    if (!rawImg.isNull())
                        icon = QIcon(QPixmap::fromImage(rawImg));
                } else if (realIcon.startsWith(u"steam_app_") || realIcon.startsWith(u"steam_icon_")) {
                    icon = resolveSteamIconLocal(realIcon.startsWith(u"steam_app_") ? realIcon.mid(10) : realIcon.mid(11));
                } else {
                    icon = QIcon::fromTheme(realIcon);
                    if (icon.isNull() || icon.availableSizes().isEmpty())
                        icon = QIcon(realIcon);
                }
            }
        }
    }

    // 4. FINAL FALLBACK: Only log if we really found nothing AND we're debugging
    if (icon.isNull()) {
        if (isDebug) {
            qDebug() << "\x1b[31m[ICON FAIL]\x1b[0m No source found for:" << originalId;
        }
        return QPixmap();
    }

    // Normalization and Processing
    QPixmap result;
    if (!m_normalizationEnabled) {
        result = icon.pixmap(QSize(targetSize, targetSize), 1.0);
    } else {
        auto info = analyzeIcon(originalId, icon);
        const bool hasSignificantPadding = info.contentRatio < 0.95;
        const double effectiveRatio = hasSignificantPadding ? info.contentRatio * std::sqrt(info.fillRatio) : info.contentRatio;

        // --- DIAGNOSTIC LOGGING ---
        qDebug().noquote() << "\x1b[36m[NORM MATH]\x1b[0m" << originalId << "| CR:" << info.contentRatio << "| FR:" << info.fillRatio
                           << "| EffR:" << effectiveRatio;

        if (effectiveRatio >= kMinContentRatio) {
            result = shrinkPixmap(icon, targetSize, (info.contentRatio > kEdgeToEdgeFill) ? kEdgeToEdgeFill / info.contentRatio : 1.0);
        } else {
            result = normalizePixmap(icon, targetSize, info);
        }
    }

    if (result.isNull())
        return QPixmap();

    if (m_indicatorOffset < 1.0) {
        int shrunkSize = static_cast<int>(std::round(targetSize * m_indicatorOffset));
        QImage scaled = result.toImage().scaled(shrunkSize, shrunkSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        result = QPixmap(targetSize, targetSize);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.drawImage((targetSize - scaled.width()) / 2, (targetSize - scaled.height()) / 2, scaled);
        painter.end();
    }

    if (size)
        *size = result.size();
    return result;
}

QString TaskIconProvider::resolveSteamExePath(const QString &appId)
{
    using namespace Qt::StringLiterals;
    bool debug = QCoreApplication::arguments().contains(u"--debug-icons"_s);

    QStringList libraryPaths;
    libraryPaths << QDir::homePath() + u"/.local/share/Steam"_s;

    QString vdfPath = QDir::homePath() + u"/.local/share/Steam/steamapps/libraryfolders.vdf"_s;
    QFile vdf(vdfPath);
    if (vdf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(vdf.readAll());
        QRegularExpression re(u"\"path\"\\s+\"([^\"]+)\""_s);
        auto it = re.globalMatch(content);
        while (it.hasNext()) {
            QString p = it.next().captured(1);
            if (!libraryPaths.contains(p))
                libraryPaths.append(p);
        }
    }

    for (const QString &lib : libraryPaths) {
        QString manifestPath = lib + u"/steamapps/appmanifest_"_s + appId + u".acf"_s;
        if (!QFile::exists(manifestPath))
            continue;

        QFile manifest(manifestPath);
        if (manifest.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromUtf8(manifest.readAll());
            QRegularExpression re(u"\"installdir\"\\s+\"([^\"]+)\""_s);
            auto match = re.match(content);
            if (match.hasMatch()) {
                QString gameDir = lib + u"/steamapps/common/"_s + match.captured(1);
                if (!QDir(gameDir).exists())
                    continue;

                QDirIterator dirIt(gameDir, {u"*.exe"_s}, QDir::Files, QDirIterator::Subdirectories);
                QString bestExe;
                qint64 maxSize = 0;
                while (dirIt.hasNext()) {
                    QString cur = dirIt.next();
                    if (cur.contains(u"redist"_s, Qt::CaseInsensitive) || cur.contains(u"crash"_s, Qt::CaseInsensitive))
                        continue;
                    QFileInfo fi(cur);
                    if (fi.size() > maxSize) {
                        maxSize = fi.size();
                        bestExe = cur;
                    }
                }
                if (!bestExe.isEmpty())
                    return bestExe;
            }
        }
    }

    if (debug)
        qDebug().noquote() << "\x1b[31m[HUNTER FAIL]\x1b[0m Could not find any .exe for Steam App:" << appId;
    return QString();
}

QIcon TaskIconProvider::resolveSteamIconLocal(const QString &appId)
{
    using namespace Qt::StringLiterals;
    QString root = QDir::homePath() + u"/.local/share/Steam/appcache/librarycache/"_s + appId;
    if (!QDir(root).exists())
        return QIcon();

    struct Candidate {
        QString path;
        int score;
        int res;
    };
    QList<Candidate> candidates;

    QDirIterator it(root, {u"*.png"_s, u"*.jpg"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString p = it.next();
        QFileInfo fi(p);
        QString name = fi.fileName().toLower();
        int score = 0;

        if (name.contains(u"logo"_s) && p.endsWith(u".png"_s))
            score += 1000;
        else if (fi.absolutePath() == root && name.length() >= 30)
            score += 500;
        else if (name.contains(u"hero"_s))
            score += 100;
        else if (name.contains(u"header"_s))
            score += 50;
        else
            score += 10;

        QImageReader reader(p);
        if (reader.canRead()) {
            QSize sz = reader.size();
            candidates.append({p, score, sz.width() * sz.height()});
        }
    }

    if (!candidates.isEmpty()) {
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
            if (a.score != b.score)
                return a.score > b.score;
            return a.res > b.res;
        });

        if (QCoreApplication::arguments().contains(u"--debug-icons"_s)) {
            qDebug().noquote() << "\x1b[35m[ICON SOURCE]\x1b[0m Steam App" << appId << "-> \x1b[33mSTEAM OFFLINE CACHE\x1b[0m (" << candidates.first().path
                               << ") [Score:" << candidates.first().score << "]";
        }
        return QIcon(candidates.first().path);
    }
    return QIcon();
}

QRect TaskIconProvider::findContentBounds(const QImage &image, int threshold)
{
    const int w = image.width(), h = image.height();
    if (w == 0 || h == 0)
        return {};
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
    return (bottom < 0) ? QRect() : QRect(left, top, right - left + 1, bottom - top + 1);
}

IconNormalizationInfo TaskIconProvider::analyzeIcon(const QString &iconName, const QIcon &icon)
{
    auto it = m_cache.constFind(iconName);
    if (it != m_cache.constEnd())
        return it.value();
    const auto sizes = icon.availableSizes();
    int probeSize = sizes.isEmpty() ? 256 : 0;
    for (const auto &s : sizes)
        probeSize = std::max({probeSize, s.width(), s.height()});
    QImage probeImage = icon.pixmap(QSize(probeSize, probeSize), 1.0).toImage();
    if (probeImage.isNull() || probeImage.format() != QImage::Format_ARGB32_Premultiplied)
        probeImage = probeImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QRect bounds = findContentBounds(probeImage, kAlphaThreshold);
    IconNormalizationInfo info;
    info.probeSize = probeSize;
    if (bounds.isEmpty()) {
        info.contentRatio = 1.0;
        info.fillRatio = 1.0;
        info.contentBounds = QRect(0, 0, probeSize, probeSize);
    } else {
        int contentDim = std::max(bounds.width(), bounds.height());
        info.contentRatio = static_cast<double>(contentDim) / probeSize;
        info.contentBounds = bounds;
        int pixels = 0;
        for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
            const auto *scanline = reinterpret_cast<const QRgb *>(probeImage.constScanLine(y));
            for (int x = bounds.left(); x <= bounds.right(); ++x) {
                if (qAlpha(scanline[x]) > kAlphaThreshold)
                    ++pixels;
            }
        }
        double bboxArea = static_cast<double>(bounds.width()) * bounds.height();
        info.fillRatio = (bboxArea > 0) ? pixels / bboxArea : 1.0;
    }
    m_cache.insert(iconName, info);
    return info;
}

QPixmap TaskIconProvider::normalizePixmap(const QIcon &icon, int targetSize, const IconNormalizationInfo &info)
{
    const double margin = (info.fillRatio < 0.95) ? 0.05 : kMinMarginRatio;
    // Increase targetFill to allow icons to occupy more of the slot
    double targetFill = std::min(info.contentRatio * std::sqrt(info.fillRatio) * 1.15, 1.0 - margin * 1.5);
    int targetContentPx = static_cast<int>(std::round(targetSize * targetFill));
    QImage img = icon.pixmap(QSize(static_cast<int>(std::ceil(static_cast<double>(targetContentPx) / info.contentRatio)),
                                   static_cast<int>(std::ceil(static_cast<double>(targetContentPx) / info.contentRatio))),
                             1.0)
                     .toImage();
    if (img.isNull()) {
        QPixmap f(targetSize, targetSize);
        f.fill(Qt::transparent);
        return f;
    }
    if (img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QRect bounds = findContentBounds(img, kAlphaThreshold);
    if (bounds.isEmpty())
        return icon.pixmap(QSize(targetSize, targetSize), 1.0);
    int contentDim = std::max(bounds.width(), bounds.height());
    QImage scaled = img.copy(QRect(bounds.center().x() - contentDim / 2, bounds.center().y() - contentDim / 2, contentDim, contentDim).intersected(img.rect()))
                        .scaled(targetContentPx, targetContentPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap res(targetSize, targetSize);
    res.fill(Qt::transparent);
    QPainter p(&res);
    p.drawImage((targetSize - scaled.width()) / 2, (targetSize - scaled.height()) / 2, scaled);
    p.end();
    return res;
}

QPixmap TaskIconProvider::shrinkPixmap(const QIcon &icon, int targetSize, double shrinkFactor)
{
    QPixmap orig = icon.pixmap(QSize(targetSize, targetSize), 1.0);
    if (orig.isNull()) {
        QPixmap f(targetSize, targetSize);
        f.fill(Qt::transparent);
        return f;
    }
    int s = static_cast<int>(std::round(targetSize * shrinkFactor));
    QImage scaled = orig.toImage().scaled(s, s, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap res(targetSize, targetSize);
    res.fill(Qt::transparent);
    QPainter p(&res);
    p.drawImage((targetSize - s) / 2, (targetSize - s) / 2, scaled);
    p.end();
    return res;
}

} // namespace krema
