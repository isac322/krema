// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QImage>
#include <QString>

/**
 * @brief A standalone utility to extract high-resolution native icons from Windows PE (.exe) files.
 *
 * This class manually traverses the Portable Executable (PE) headers and Resource Directory
 * tree to locate RT_GROUP_ICON and RT_ICON blocks, extracting the raw PNG or DIB bytes
 * without relying on external dependencies like wrestool or Wine.
 */
class PeIconExtractor
{
public:
    /**
     * @brief Parses a Windows executable and returns the highest resolution icon found.
     * @param exePath Absolute path to the .exe file.
     * @return A QImage containing the parsed icon, or a null QImage if extraction fails.
     */
    static QImage extract(const QString &exePath);
};
