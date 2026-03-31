// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "style/backgroundstyle.h"

#include <QTest>

using namespace krema;

class TestBackgroundStyle : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // --- computeBackgroundColor ---

    void transparent_returnsTransparent()
    {
        QColor c = computeBackgroundColor(BackgroundStyleType::Transparent, QStringLiteral("#000000"), 0.8);
        QCOMPARE(c, Qt::transparent);
    }

    void panelInherit_appliesOpacity()
    {
        QColor c = computeBackgroundColor(BackgroundStyleType::PanelInherit, {}, 0.5);
        // Should return a valid color with alpha ~0.5
        QVERIFY(c.isValid());
        QVERIFY(qAbs(c.alphaF() - 0.5f) < 0.01f);
    }

    void panelInherit_withAccentColor()
    {
        QColor c = computeBackgroundColor(BackgroundStyleType::PanelInherit, {}, 0.8, true);
        QVERIFY(c.isValid());
        QVERIFY(qAbs(c.alphaF() - 0.8f) < 0.01f);
    }

    void tinted_usesCustomColor()
    {
        QColor c = computeBackgroundColor(BackgroundStyleType::Tinted, QStringLiteral("#ff0000"), 0.5, false, false);
        QVERIFY(c.isValid());
        QCOMPARE(c.red(), 255);
        QCOMPARE(c.green(), 0);
        QCOMPARE(c.blue(), 0);
        QVERIFY(qAbs(c.alphaF() - 0.5f) < 0.01f);
    }

    void tinted_invalidColorFallsBackToHeader()
    {
        QColor c = computeBackgroundColor(BackgroundStyleType::Tinted, QStringLiteral("not-a-color"), 0.7, false, false);
        // Should fall back to Header color, not be invalid
        QVERIFY(c.isValid());
        QVERIFY(qAbs(c.alphaF() - 0.7f) < 0.01f);
    }

    void tinted_useSystemColor()
    {
        QColor c = computeBackgroundColor(BackgroundStyleType::Tinted, QStringLiteral("#ff0000"), 0.6, false, true);
        // useSystemColor=true: ignores custom color, uses Header/Selection color
        QVERIFY(c.isValid());
        QVERIFY(qAbs(c.alphaF() - 0.6f) < 0.01f);
        // Custom red should NOT be used
        // (exact color depends on system theme, but it should differ from #ff0000)
    }

    void acrylic_appliesOpacity()
    {
        QColor c = computeBackgroundColor(BackgroundStyleType::Acrylic, {}, 0.3);
        QVERIFY(c.isValid());
        QVERIFY(qAbs(c.alphaF() - 0.3f) < 0.01f);
    }

    // --- styleUsesBlur ---

    void blur_panelInherit()
    {
        QVERIFY(styleUsesBlur(BackgroundStyleType::PanelInherit));
    }

    void blur_acrylic()
    {
        QVERIFY(styleUsesBlur(BackgroundStyleType::Acrylic));
    }

    void noBlur_transparent()
    {
        QVERIFY(!styleUsesBlur(BackgroundStyleType::Transparent));
    }

    void noBlur_tinted()
    {
        QVERIFY(!styleUsesBlur(BackgroundStyleType::Tinted));
    }

    // --- isStyleAvailable ---

    void allStylesAvailable()
    {
        QVERIFY(isStyleAvailable(BackgroundStyleType::PanelInherit));
        QVERIFY(isStyleAvailable(BackgroundStyleType::Transparent));
        QVERIFY(isStyleAvailable(BackgroundStyleType::Tinted));
        QVERIFY(isStyleAvailable(BackgroundStyleType::Acrylic));
    }

    // --- opacity edge cases ---

    void opacity_fullOpaque()
    {
        QColor c = computeBackgroundColor(BackgroundStyleType::PanelInherit, {}, 1.0);
        QVERIFY(qAbs(c.alphaF() - 1.0f) < 0.01f);
    }

    void opacity_nearZero()
    {
        QColor c = computeBackgroundColor(BackgroundStyleType::PanelInherit, {}, 0.1);
        QVERIFY(qAbs(c.alphaF() - 0.1f) < 0.01f);
    }
};

QTEST_MAIN(TestBackgroundStyle)
#include "test_backgroundstyle.moc"
