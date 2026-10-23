// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

.pragma library

// Return the ground-plane margin needed to contain the projected panel shadow.
// The shader evaluates the panel SDF in light-space (Q), where the Gaussian
// radius is measured before projection. Convert both the panel overhang and
// the three-sigma softness back to ground coordinates.
function projectedMargin(panelWidth, panelHeight, elevation, lightX, lightY, lightZ, lightRadius) {
    const denominator = Math.max(lightZ - elevation, 1)
    const projection = elevation / denominator
    const panelOverhang = Math.max(
        (Math.max(panelWidth, 0) / 2 + Math.abs(lightX)) * projection,
        (Math.max(panelHeight, 0) / 2 + Math.abs(lightY)) * projection)
    const softness = 3 * Math.max(lightRadius, 0.5) * lightZ / denominator
    return Math.ceil(panelOverhang + softness + 10)
}
