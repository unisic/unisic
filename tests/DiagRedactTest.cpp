#include <QtTest>

#include "../src/diag/DiagRedact.h"

// The privacy contract of the diagnostic log. This is the one part of the
// feature that is pure logic, so it belongs in ctest rather than in the F8
// smoke test: the log is pasted into public issue trackers by hand, and
// destinations.json really does hold live Authorization headers and API keys.
class DiagRedactTest : public QObject
{
    Q_OBJECT

    static QString r(const QString &line)
    {
        return DiagRedact::redact(line, QStringLiteral("/home/tester"),
                                  QStringLiteral("tester"));
    }

private slots:
    void shortensHomePath()
    {
        QCOMPARE(r(QStringLiteral("saved /home/tester/Pictures/shot.png")),
                 QStringLiteral("saved ~/Pictures/shot.png"));
    }

    void masksAuthorizationHeaders()
    {
        const QString out = r(QStringLiteral("headers: Authorization: Client-ID abc123def"));
        QVERIFY(!out.contains(QStringLiteral("abc123def")));
        QVERIFY(out.contains(QLatin1String(DiagRedact::marker())));
        // The field NAME survives, so the log still says what was sent.
        QVERIFY(out.contains(QStringLiteral("Authorization")));
    }

    void masksJsonStyleSecrets()
    {
        const QString out = r(QStringLiteral("{\"client_secret\": \"s3cr3tvalue\", \"url\": \"https://x.test\"}"));
        QVERIFY(!out.contains(QStringLiteral("s3cr3tvalue")));
        QVERIFY(out.contains(QStringLiteral("https://x.test")));
    }

    void masksBearerTokens()
    {
        const QString out = r(QStringLiteral("sending Bearer eyJhbGciOiJIUzI1NiJ9"));
        QVERIFY(!out.contains(QStringLiteral("eyJhbGciOiJIUzI1NiJ9")));
    }

    void masksUrlUserinfoAndQuery()
    {
        const QString user = r(QStringLiteral("ftp://bob:hunter2@files.test/upload"));
        QVERIFY(!user.contains(QStringLiteral("hunter2")));
        QVERIFY(user.contains(QStringLiteral("files.test")));

        const QString query = r(QStringLiteral("GET https://api.test/upload?key=abc123&t=9"));
        QVERIFY(!query.contains(QStringLiteral("abc123")));
        QVERIFY(query.contains(QStringLiteral("https://api.test/upload")));
    }

    void leavesOrdinaryLinesUntouched()
    {
        const QString plain = QStringLiteral("portal ScreenCast: stream 42 ready 2560x1440");
        QCOMPARE(r(plain), plain);
    }

    void isIdempotent()
    {
        const QString once = r(QStringLiteral("Authorization: Bearer abcdefghijkl and /home/tester/x"));
        QCOMPARE(r(once), once);
    }
};

QTEST_MAIN(DiagRedactTest)
#include "DiagRedactTest.moc"
