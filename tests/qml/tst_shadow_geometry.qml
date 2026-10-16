// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtTest
import "../../src/qml/shadowgeometry.js" as ShadowGeometry

Item {
    TestCase {
        name: "ShadowGeometry"

        function test_lowLightHeightIncludesPanelOverhang() {
            const margin = ShadowGeometry.projectedMargin(584, 50, 15, 0, -150, 100, 5)
            verify(margin >= 70, "margin was " + margin)
        }

        function test_extremeValidSettingsAreNotCapped() {
            const margin = ShadowGeometry.projectedMargin(584, 50, 50, 300, 300, 100, 20)
            verify(margin > 200, "margin was " + margin)
        }

        function test_highLightHeightRemainsFinite() {
            const margin = ShadowGeometry.projectedMargin(584, 50, 15, 0, -150, 2000, 5)
            verify(Number.isFinite(margin))
            verify(margin >= 25)
        }
    }
}
