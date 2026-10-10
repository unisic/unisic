// Every icon a QML file names as a literal must have a bundled glyph in the kit.
// IconImageProvider asks the desktop icon theme first on the System theme, and
// themes differ: Breeze has "view-preview", Adwaita does not, so on GNOME the
// provider came back empty and the log filled with "Failed to get image from
// provider" (#118). A bundled twin is the answer that works on every desktop.
#include <QtTest>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>

class IconCoverageTest : public QObject
{
    Q_OBJECT
private slots:
    void everyLiteralIconNameHasABundledGlyph();
};

void IconCoverageTest::everyLiteralIconNameHasABundledGlyph()
{
    const QString root = QStringLiteral(UNISIC_SOURCE_DIR);
    const QDir glyphs(root + QStringLiteral("/external/unisic-kit/resources/icons/sym"));
    QVERIFY2(glyphs.exists(), "kit submodule not checked out");

    // iconName: "x"  and  image://icon/x  - the two literal forms the QML uses.
    const QRegularExpression literal(
        QStringLiteral("(?:iconName\\s*:\\s*\"|image://icon/)([a-z][a-z0-9-]*)"));
    QSet<QString> missing;
    int seen = 0;
    for (const QString &dir : {root + QStringLiteral("/qml"),
                               root + QStringLiteral("/external/unisic-kit/qml")}) {
        QDirIterator it(dir, {QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            QVERIFY(f.open(QIODevice::ReadOnly));
            auto m = literal.globalMatch(QString::fromUtf8(f.readAll()));
            while (m.hasNext()) {
                const QString name = m.next().captured(1);
                ++seen;
                if (!glyphs.exists(name + QStringLiteral(".svg")))
                    missing.insert(name);
            }
        }
    }
    QVERIFY2(seen > 20, "icon scan found almost nothing - the pattern is stale");
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("no bundled glyph for: ")
                        + QStringList(missing.begin(), missing.end()).join(QLatin1Char(' '))));
}

QTEST_APPLESS_MAIN(IconCoverageTest)
#include "IconCoverageTest.moc"
