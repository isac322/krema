// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "models/taskiconprovider.h"

#include <QTest>

using namespace krema;

class TestTaskIconProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // --- requestPixmap basic ---

    void requestPixmap_unknownIcon_returnsFallback()
    {
        TaskIconProvider provider;
        QSize size;
        QPixmap pix = provider.requestPixmap(QStringLiteral("nonexistent-icon-name-12345"), &size, QSize(48, 48));

        // Should return a valid pixmap (fallback icon), not null
        QVERIFY(!pix.isNull());
        QCOMPARE(size.width(), 48);
        QCOMPARE(size.height(), 48);
    }

    void requestPixmap_parsesQueryString()
    {
        TaskIconProvider provider;
        QSize size;
        // "org.kde.dolphin?v=1" should parse to icon name "org.kde.dolphin"
        QPixmap pix = provider.requestPixmap(QStringLiteral("org.kde.dolphin?v=1"), &size, QSize(48, 48));
        QVERIFY(!pix.isNull());
    }

    void requestPixmap_respectsRequestedSize()
    {
        TaskIconProvider provider(false); // normalization off for simpler test
        QSize size;
        QPixmap pix = provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(64, 64));
        QVERIFY(!pix.isNull());
        QCOMPARE(pix.width(), 64);
        QCOMPARE(pix.height(), 64);
    }

    void requestPixmap_defaultSize48WhenNotRequested()
    {
        TaskIconProvider provider(false);
        QSize size;
        QPixmap pix = provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(-1, -1));
        QVERIFY(!pix.isNull());
        QCOMPARE(size.width(), 48);
    }

    // --- normalization ---

    void normalization_disabledReturnsSameSize()
    {
        TaskIconProvider provider(false);
        QSize size;
        QPixmap pix = provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(48, 48));
        QCOMPARE(size.width(), 48);
        QCOMPARE(size.height(), 48);
    }

    void normalization_enabledReturnsSameCanvasSize()
    {
        TaskIconProvider provider(true);
        QSize size;
        QPixmap pix = provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(48, 48));
        // Canvas should still be 48x48 (normalization only changes content within)
        QCOMPARE(size.width(), 48);
        QCOMPARE(size.height(), 48);
    }

    // --- iconScale ---

    void iconScale_defaultIsFullCanvas()
    {
        TaskIconProvider provider(false);
        QSize size;
        QPixmap pix = provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(64, 64));
        QCOMPARE(size.width(), 64);
    }

    void iconScale_halfSizeKeepsCanvasSize()
    {
        TaskIconProvider provider(false);
        provider.setIconScale(0.5);
        QSize size;
        QPixmap pix = provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(64, 64));
        // Canvas should remain 64x64 (icon shrunk within)
        QCOMPARE(size.width(), 64);
        QCOMPARE(size.height(), 64);
    }

    void iconScale_clampsBelowMin()
    {
        TaskIconProvider provider;
        provider.setIconScale(0.1); // Below 0.5 min — should clamp to 0.5
        QSize size;
        QPixmap pix = provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(64, 64));
        QVERIFY(!pix.isNull());
    }

    void iconScale_clampsAboveMax()
    {
        TaskIconProvider provider;
        provider.setIconScale(2.0); // Above 1.0 max — should clamp to 1.0
        QSize size;
        QPixmap pix = provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(64, 64));
        QVERIFY(!pix.isNull());
    }

    // --- cache ---

    void clearCache_subsequentRequestStillWorks()
    {
        TaskIconProvider provider;
        QSize size;
        provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(48, 48));
        provider.clearCache();
        QPixmap pix = provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(48, 48));
        QVERIFY(!pix.isNull());
    }

    void setNormalizationEnabled_toggle()
    {
        TaskIconProvider provider(true);
        QSize size;
        provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(48, 48));

        provider.setNormalizationEnabled(false);
        QPixmap pix = provider.requestPixmap(QStringLiteral("application-x-executable"), &size, QSize(48, 48));
        QVERIFY(!pix.isNull());
    }
};

QTEST_MAIN(TestTaskIconProvider)
#include "test_taskiconprovider.moc"
