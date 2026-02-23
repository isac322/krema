// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <cmath>

namespace krema
{

/// Compute parabolic zoom factor using Gaussian curve.
/// @param distance Distance from mouse to icon center (pixels)
/// @param iconSize Base icon size (pixels)
/// @param maxZoomFactor Maximum zoom multiplier (e.g. 1.6)
/// @return Zoom factor in range [1.0, maxZoomFactor]
inline double parabolicZoomFactor(double distance, double iconSize, double maxZoomFactor)
{
    if (maxZoomFactor <= 1.0) {
        return 1.0;
    }
    const double sigma = iconSize * 1.2;
    return 1.0 + (maxZoomFactor - 1.0) * std::exp(-(distance * distance) / (2.0 * sigma * sigma));
}

} // namespace krema
