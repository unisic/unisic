#include <QtTest>
#include <QTemporaryDir>
#include "Settings.h"
#include "upload/UploadManager.h"

class UploadUrlTest : public QObject
{
    Q_OBJECT
private slots:
    void curlTarget_data();
    void curlTarget();
    void responseTemplate();
    void offeredVariablesResolve();
    void liveBuzzheavierUpload();
};

void UploadUrlTest::curlTarget_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("want");
    QTest::newRow("append no slash") << "sftp://host/dir" << "a b.png" << "sftp://host/dir/a%20b.png";
    QTest::newRow("append slash") << "sftp://host/dir/" << "a b.png" << "sftp://host/dir/a%20b.png";
    QTest::newRow("token keeps query") << "https://host/up/%file%?to=inbox" << "a b.png"
                                       << "https://host/up/a%20b.png?to=inbox";
    QTest::newRow("token no escape") << "https://host/up/%file%" << "../../etc/x.png"
                                     << "https://host/up/.._.._etc_x.png";
    QTest::newRow("append no escape") << "sftp://host/dir/" << "../../etc/x.png"
                                      << "sftp://host/dir/.._.._etc_x.png";
}

void UploadUrlTest::curlTarget()
{
    QFETCH(QString, url);
    QFETCH(QString, name);
    QFETCH(QString, want);
    QCOMPARE(UploadManager::curlTargetUrl(url, name), want);
}

void UploadUrlTest::responseTemplate()
{
    const QJsonObject dest{{"urlPath", "https://host/$json:data.id$"}};
    QCOMPARE(UploadManager::extractUrl(dest, "urlPath", QByteArray(R"({"data":{"id":"abc123"}})")),
             QStringLiteral("https://host/abc123"));
}

// The destination editor offers each of these as a clickable chip, so each one
// is a promise that the sender substitutes it. This is the check that the two
// lists cannot drift: a chip for a token nothing resolves uploads the literal
// "%file%" and looks fine right up until the link is dead.
void UploadUrlTest::offeredVariablesResolve()
{
    QTemporaryDir cfg;
    QVERIFY(cfg.isValid());
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    Settings settings;
    UploadManager uploads(&settings);

    // The pills are painted from the same pattern the help carries, so a token
    // the chip types and the pattern misses would never be drawn as a variable.
    // Filled in first: a chip with caretBack > 0 inserts a token the user still
    // has to finish ($json:$ needs its path), and an unfinished one is meant to
    // stay unpilled, because it is not yet something extractUrl resolves.
    // Collected rather than asserted in place: QVERIFY returns, and a lambda
    // cannot both return early and yield a list.
    QStringList misses;
    auto tokensOf = [&](const QString &field, const QString &type) {
        QStringList out;
        const QVariantMap help = uploads.templateHelp(field, type);
        const QRegularExpression re(help.value("pattern").toString());
        const QVariantList vars = help.value("vars").toList();
        for (const QVariant &v : vars) {
            const QString token = v.toMap().value("token").toString();
            const int caretBack = v.toMap().value("caretBack").toInt();
            QString filled = token;
            if (caretBack > 0)
                filled.insert(filled.size() - caretBack, 'x');
            const QRegularExpressionMatch m = re.match(filled);
            if (!m.hasMatch() || m.captured(0) != filled)
                misses << field + "'s pattern misses " + filled;
            out << token;
        }
        out.sort();
        return out;
    };

    QCOMPARE(tokensOf("data", "http"), QStringList({"$base64$", "$filename$", "$mime$"}));
    QCOMPARE(tokensOf("requestUrl", "curl"), QStringList({"%file%"}));
    // Both senders substitute it, so both offer it - the descriptions differ
    // (only curl appends the name when the token is absent), the token does not.
    QCOMPARE(tokensOf("requestUrl", "http"), QStringList({"%file%"}));
    QCOMPARE(tokensOf("urlPath", "http"),
             QStringList({"$json:$", "$regex:$", "$text$"}));
    QVERIFY2(misses.isEmpty(), qPrintable(misses.join("; ")));

    // And through the code that has to consume them. $json:$ and $regex:$ are
    // offered half-written, with the caret parked where the path goes, so they
    // are finished here the way the user would.
    QCOMPARE(UploadManager::curlTargetUrl("https://host/up/%file%", "shot.png"),
             QStringLiteral("https://host/up/shot.png"));
    QCOMPARE(UploadManager::requestUrlWithFileName("https://host/up/%file%?to=inbox", "a b.png"),
             QStringLiteral("https://host/up/a%20b.png?to=inbox"));
    // The http URL takes NO append fallback: curl adds the name to a folder,
    // an API endpoint has to be posted to exactly as it stands.
    QCOMPARE(UploadManager::requestUrlWithFileName("https://api.host/v1/upload", "shot.png"),
             QStringLiteral("https://api.host/v1/upload"));
    const QByteArray answer(R"({"data":{"link":"https://host/x.png"}})");
    // Through named locals rather than brace-init or raw-string arguments: moc's
    // macro parser counts brackets without understanding either, and reads them
    // as the end of QCOMPARE's argument list.
    auto extract = [&](const QString &urlPath) {
        const QJsonObject dest{{QStringLiteral("urlPath"), urlPath}};
        return UploadManager::extractUrl(dest, "urlPath", answer);
    };
    const QString want = QStringLiteral("https://host/x.png");
    // Custom delimiter: the pattern itself contains )" .
    const QString byRegex = R"RX($regex:"link":"([^"]+)"$)RX";
    QCOMPARE(extract("$text$"), QString::fromUtf8(answer));
    QCOMPARE(extract("$json:data.link$"), want);
    QCOMPARE(extract(byRegex), want);
}

// The whole curl path against the real host it was built for: the %file% token
// puts the name in the URL, curl's stdout is parsed, and the link comes back.
// OFF by default - it puts a small PNG on a public file host, which is not
// something a plain `ctest` run should do behind anyone's back. Run it with
//   UNISIC_LIVE_UPLOAD=1 ctest --test-dir build -R upload_url --output-on-failure
void UploadUrlTest::liveBuzzheavierUpload()
{
    if (qgetenv("UNISIC_LIVE_UPLOAD") != QByteArray("1"))
        QSKIP("live upload: set UNISIC_LIVE_UPLOAD=1 (uploads a test image to buzzheavier.com)");

    // UploadManager writes destinations.json on construction; keep that out of
    // the real config directory.
    QTemporaryDir cfg;
    QVERIFY(cfg.isValid());
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());

    Settings settings;
    UploadManager uploads(&settings);
    bool done = false, ok = false;
    QString url, err;
    uploads.testDestination(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("buzzheavier live test")},
        {QStringLiteral("type"), QStringLiteral("curl")},
        {QStringLiteral("requestUrl"), QStringLiteral("https://w.buzzheavier.com/%file%")},
        {QStringLiteral("urlPath"), QStringLiteral("https://buzzheavier.com/$json:data.id$")},
    }, [&](bool o, const QString &u, const QString &e) {
        done = true; ok = o; url = u; err = e;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 60000);
    QVERIFY2(ok, qPrintable(err));
    QVERIFY2(url.startsWith(QStringLiteral("https://buzzheavier.com/"))
                 && url.size() > int(sizeof("https://buzzheavier.com/")),
             qPrintable(QStringLiteral("no id in the returned link: '%1'").arg(url)));
    qInfo() << "live upload landed at" << url;
}

QTEST_MAIN(UploadUrlTest)
#include "UploadUrlTest.moc"
