#include <QtTest>
#include "FilenameTemplate.h"

// The save-name template feeds both the settings preview and the real save —
// a token that expands differently between them (or an illegal filename char
// slipping through) means the file on disk is not what the UI promised.
class FilenameTemplateTest : public QObject
{
    Q_OBJECT

    // Fixed clock so expansions are deterministic.
    const QDateTime now{QDate(2026, 7, 18), QTime(9, 5, 3)};

private slots:
    void emptyFallsBackToDefault()
    {
        QCOMPARE(FilenameTemplate::expand(QString(), 1, now),
                 QStringLiteral("Unisic_2026-07-18_09-05-03"));
        QCOMPARE(FilenameTemplate::expand(QStringLiteral("   "), 1, now),
                 QStringLiteral("Unisic_2026-07-18_09-05-03"));
    }

    void tokensExpand()
    {
        QCOMPARE(FilenameTemplate::expand(QStringLiteral("%date%"), 1, now),
                 QStringLiteral("2026-07-18"));
        QCOMPARE(FilenameTemplate::expand(QStringLiteral("%time%"), 1, now),
                 QStringLiteral("09-05-03"));
        QCOMPARE(FilenameTemplate::expand(QStringLiteral("%datetime%"), 1, now),
                 QStringLiteral("2026-07-18_09-05-03"));
        QCOMPARE(FilenameTemplate::expand(QStringLiteral("%unix%"), 1, now),
                 QString::number(now.toSecsSinceEpoch()));
        QCOMPARE(FilenameTemplate::expand(QStringLiteral("shot_%i%"), 42, now),
                 QStringLiteral("shot_42"));
        QCOMPARE(FilenameTemplate::expand(QStringLiteral("x"), -7, now),
                 QStringLiteral("x")); // counter unused when template has no %i%
    }

    void datetimeSurvivesDateReplacement()
    {
        // %date% is replaced first; "%datetime%" must not come out as
        // "2026-07-18time%" (the token contains "%date" but not "%date%").
        QCOMPARE(FilenameTemplate::expand(QStringLiteral("%datetime%"), 1, now),
                 QStringLiteral("2026-07-18_09-05-03"));
    }

    void randToken()
    {
        const QString a = FilenameTemplate::expand(QStringLiteral("%rand%"), 1, now);
        QCOMPARE(a.size(), 8);
        QVERIFY(QRegularExpression(QStringLiteral("[0-9a-f]{8}")).match(a).hasMatch());
        // UUID-backed: two expansions must not collide.
        QVERIFY(a != FilenameTemplate::expand(QStringLiteral("%rand%"), 1, now));
    }

    void illegalCharsReplaced()
    {
        QCOMPARE(FilenameTemplate::expand(QStringLiteral("a/b\\c:d*e?f\"g<h>i|j"), 1, now),
                 QStringLiteral("a_b_c_d_e_f_g_h_i_j"));
    }

    void unknownTokensPassThrough()
    {
        QCOMPARE(FilenameTemplate::expand(QStringLiteral("%width%_%height%"), 1, now),
                 QStringLiteral("%width%_%height%"));
    }

    void extensionMapping()
    {
        QCOMPARE(FilenameTemplate::extensionFor(QStringLiteral("png")), QStringLiteral("png"));
        QCOMPARE(FilenameTemplate::extensionFor(QStringLiteral("PNG")), QStringLiteral("png"));
        QCOMPARE(FilenameTemplate::extensionFor(QStringLiteral("jpeg")), QStringLiteral("jpg"));
        QCOMPARE(FilenameTemplate::extensionFor(QStringLiteral("JPEG")), QStringLiteral("jpg"));
        QCOMPARE(FilenameTemplate::extensionFor(QStringLiteral("jpg")), QStringLiteral("jpg"));
        QCOMPARE(FilenameTemplate::extensionFor(QStringLiteral("webp")), QStringLiteral("webp"));
        // Unsupported formats fall back to png (the encoder only does jpg/webp/png).
        QCOMPARE(FilenameTemplate::extensionFor(QStringLiteral("bmp")), QStringLiteral("png"));
        QCOMPARE(FilenameTemplate::extensionFor(QString()), QStringLiteral("png"));
    }
};

QTEST_APPLESS_MAIN(FilenameTemplateTest)
#include "FilenameTemplateTest.moc"
