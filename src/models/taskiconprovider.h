// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QIcon>
#include <QPixmap>
#include <QQuickImageProvider>

namespace krema
{

/**
 * QML image provider for application icons.
 *
 * Resolves icon names via QIcon::fromTheme() and returns pixmaps
 * at the requested size. Used in QML as:
 *   Image { source: "image://icon/" + iconName }
 */
class TaskIconProvider : public QQuickImageProvider
{
public:
    TaskIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        const int width = requestedSize.width() > 0 ? requestedSize.width() : 48;
        const int height = requestedSize.height() > 0 ? requestedSize.height() : 48;
        const QSize pixmapSize(width, height);

        QIcon icon = QIcon::fromTheme(id);
        if (icon.isNull()) {
            // Fallback: try the id as-is (might be an absolute path)
            icon = QIcon(id);
        }
        if (icon.isNull()) {
            // Generic application icon as last resort
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        }

        QPixmap pixmap = icon.pixmap(pixmapSize);

        if (pixmap.isNull()) {
            // Return a transparent pixmap so QML can detect Image.status != Ready
            pixmap = QPixmap(pixmapSize);
            pixmap.fill(Qt::transparent);
        }

        if (size) {
            *size = pixmap.size();
        }

        return pixmap;
    }
};

} // namespace krema
