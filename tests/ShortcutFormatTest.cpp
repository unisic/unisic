#include <QtTest>
#include "ShortcutFormat.h"

// The shortcut recorder receives the SHIFTED symbol for Shift+digit presses
// (Shift+1 arrives as Qt::Key_Exclam). KGlobalAccel matches physical keys, so
// the recorded string must be "Shift+1", never "Shift+!" — the latter records
// a binding that can never fire.
class ShortcutFormatTest : public QObject
{
    Q_OBJECT
private slots:
    void shiftedDigitsUnshift_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<QString>("expected");
        QTest::newRow("Shift+1") << int(Qt::Key_Exclam)      << QStringLiteral("Meta+Shift+1");
        QTest::newRow("Shift+2") << int(Qt::Key_At)          << QStringLiteral("Meta+Shift+2");
        QTest::newRow("Shift+3") << int(Qt::Key_NumberSign)  << QStringLiteral("Meta+Shift+3");
        QTest::newRow("Shift+4") << int(Qt::Key_Dollar)      << QStringLiteral("Meta+Shift+4");
        QTest::newRow("Shift+5") << int(Qt::Key_Percent)     << QStringLiteral("Meta+Shift+5");
        QTest::newRow("Shift+6") << int(Qt::Key_AsciiCircum) << QStringLiteral("Meta+Shift+6");
        QTest::newRow("Shift+7") << int(Qt::Key_Ampersand)   << QStringLiteral("Meta+Shift+7");
        QTest::newRow("Shift+8") << int(Qt::Key_Asterisk)    << QStringLiteral("Meta+Shift+8");
        QTest::newRow("Shift+9") << int(Qt::Key_ParenLeft)   << QStringLiteral("Meta+Shift+9");
        QTest::newRow("Shift+0") << int(Qt::Key_ParenRight)  << QStringLiteral("Meta+Shift+0");
    }
    void shiftedDigitsUnshift()
    {
        QFETCH(int, key);
        QFETCH(QString, expected);
        const int mods = int(Qt::MetaModifier | Qt::ShiftModifier);
        QCOMPARE(ShortcutFormat::portable(key, mods), expected);
    }

    void plainKeysUntouched()
    {
        // Without Shift the symbols must stay symbols (AltGr layouts, etc.).
        QCOMPARE(ShortcutFormat::portable(Qt::Key_Exclam, int(Qt::MetaModifier)),
                 QStringLiteral("Meta+!"));
        // Ordinary bindings unchanged.
        QCOMPARE(ShortcutFormat::portable(Qt::Key_S, int(Qt::MetaModifier | Qt::ShiftModifier)),
                 QStringLiteral("Meta+Shift+S"));
        QCOMPARE(ShortcutFormat::portable(Qt::Key_1, int(Qt::MetaModifier | Qt::ShiftModifier)),
                 QStringLiteral("Meta+Shift+1"));
        QCOMPARE(ShortcutFormat::portable(Qt::Key_F8, 0), QStringLiteral("F8"));
    }

    void bareModifiersRejected()
    {
        QCOMPARE(ShortcutFormat::portable(Qt::Key_Shift, int(Qt::ShiftModifier)), QString());
        QCOMPARE(ShortcutFormat::portable(Qt::Key_Meta, int(Qt::MetaModifier)), QString());
        QCOMPARE(ShortcutFormat::portable(Qt::Key_Control, int(Qt::ControlModifier)), QString());
    }
};

QTEST_APPLESS_MAIN(ShortcutFormatTest)
#include "ShortcutFormatTest.moc"
