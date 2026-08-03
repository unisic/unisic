#include <QtTest>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QStandardPaths>
#include "FilenameTemplate.h"
#include "ImageEncode.h"

// The conversion policy. Every re-encode in the app - a file brought in, the
// over-size rule, a Convert to entry in history, an upload in its own format -
// ends in ImageEncode::encode, so this is where the rules are pinned down:
// which bytes come out, what the result calls itself, and which substitutions
// are allowed to happen silently. The rename that must follow a substitution is
// checked with it, because a name that disagrees with the bytes is the one
// failure mode a user cannot recover from.
class ImageConvertTest : public QObject
{
    Q_OBJECT
private slots:
    void writesRealFormats();
    void transparentJpegBecomesPng();
    void detectsTransparency();
    void gifNeedsFfmpeg();
    void nullImageProducesNothing();
    void unknownFormatIsPng();
    void renamesToTheEncodedFormat();
    void alreadyInThatFormat();

private:
    static bool haveFfmpeg()
    {
        return !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    }
    // Qt's image plugins are a packaging decision, not a given: the Ubuntu and
    // Nix build images ship no WebP writer, so asking for one there really does
    // land in the encoder's fallback rather than in a broken build.
    static bool canWrite(const char *plugin)
    {
        return QImageWriter::supportedImageFormats().contains(QByteArray(plugin));
    }
    static QImage opaque(int w = 64, int h = 48)
    {
        QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        QLinearGradient g(0, 0, w, h);
        g.setColorAt(0.0, QColor(0xC8, 0xAC, 0xD6));
        g.setColorAt(1.0, QColor(0x17, 0x15, 0x3B));
        p.fillRect(img.rect(), g);
        p.end();
        return img;
    }
};

void ImageConvertTest::writesRealFormats()
{
    const QImage src = opaque();
    struct {
        const char *fmt;
        const char *plugin;
        const char *mime;
        QByteArray magic;
        int at;
    } cases[] = {
        { "png",  "png",  "image/png",  QByteArray("\x89PNG", 4), 0 },
        { "jpg",  "jpeg", "image/jpeg", QByteArray("\xFF\xD8\xFF", 3), 0 },
        { "jpeg", "jpeg", "image/jpeg", QByteArray("\xFF\xD8\xFF", 3), 0 },
        { "webp", "webp", "image/webp", QByteArray("WEBP", 4), 8 }, // RIFF<4-byte size>WEBP
    };
    for (const auto &c : cases) {
        const ImageEncode::Result r =
            ImageEncode::encode(src, QString::fromLatin1(c.fmt), 80);
        QVERIFY2(r.ok(), c.fmt);
        if (!canWrite(c.plugin)) {
            // No plugin for it: PNG bytes come out, and the result says PNG
            // rather than naming a format it did not write. That honesty is the
            // whole point of the fallback, so it is asserted here and not
            // skipped over.
            QCOMPARE(r.format, QStringLiteral("png"));
            QCOMPARE(r.mime, QStringLiteral("image/png"));
            QCOMPARE(r.fallbackReason, QStringLiteral("encoder"));
            QVERIFY2(r.bytes.startsWith(QByteArray("\x89PNG", 4)), c.fmt);
            continue;
        }
        // The header, not the extension: this is what a browser, a file manager
        // and every upload target read to decide what the file is.
        QVERIFY2(r.bytes.mid(c.at).startsWith(c.magic),
                 qPrintable(QStringLiteral("%1 header is %2")
                                .arg(QLatin1String(c.fmt),
                                     QString::fromLatin1(r.bytes.left(12).toHex()))));
        QCOMPARE(r.mime, QString::fromLatin1(c.mime));
        QVERIFY(r.fallbackReason.isEmpty());
        // "jpeg" normalizes to "jpg" - one spelling reaches the file name.
        QCOMPARE(r.format, FilenameTemplate::extensionFor(QString::fromLatin1(c.fmt)));
        // And it decodes back to the same picture, at the same size.
        QBuffer buf;
        buf.setData(r.bytes);
        QVERIFY(buf.open(QIODevice::ReadOnly));
        QImageReader reader(&buf);
        QCOMPARE(reader.size(), src.size());
    }
}

void ImageConvertTest::transparentJpegBecomesPng()
{
    QImage img(32, 32, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.fillRect(0, 0, 16, 32, QColor(0x43, 0x3D, 0x8B));
    p.end();

    const ImageEncode::Result r = ImageEncode::encode(img, QStringLiteral("jpg"), 80);
    QVERIFY(r.ok());
    // JPEG has no alpha channel: written as one, the transparent half would come
    // back solid black. PNG instead, and the reason is what the caller turns
    // into the toast that explains the format it did not ask for.
    QCOMPARE(r.format, QStringLiteral("png"));
    QCOMPARE(r.mime, QStringLiteral("image/png"));
    QCOMPARE(r.fallbackReason, QStringLiteral("alpha"));
    QVERIFY(r.bytes.startsWith(QByteArray("\x89PNG", 4)));
}

void ImageConvertTest::detectsTransparency()
{
    QVERIFY(!ImageEncode::hasTransparency(opaque()));
    // An RGB32 image has no alpha channel at all - the cheap early exit.
    QVERIFY(!ImageEncode::hasTransparency(opaque().convertToFormat(QImage::Format_RGB32)));

    // The scan samples at most 256x256 points, so a single transparent pixel in
    // a large image can legitimately be missed - but a transparent REGION, which
    // is what a shadowed window capture actually has, cannot be.
    QImage big(1024, 768, QImage::Format_ARGB32_Premultiplied);
    big.fill(QColor(0x17, 0x15, 0x3B));
    QPainter p(&big);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(0, 0, 64, 64, Qt::transparent);
    p.end();
    QVERIFY(ImageEncode::hasTransparency(big));
}

void ImageConvertTest::gifNeedsFfmpeg()
{
    const ImageEncode::Result r = ImageEncode::encode(opaque(), QStringLiteral("gif"), 90);
    QVERIFY(r.ok());
    if (haveFfmpeg()) {
        QCOMPARE(r.format, QStringLiteral("gif"));
        QCOMPARE(r.mime, QStringLiteral("image/gif"));
        QVERIFY(r.fallbackReason.isEmpty());
        QVERIFY(r.bytes.startsWith("GIF89a") || r.bytes.startsWith("GIF87a"));
    } else {
        // Qt writes no GIF, so without ffmpeg there is nothing to write one
        // with. Bytes that decode beat the extension that was asked for.
        QCOMPARE(r.format, QStringLiteral("png"));
        QCOMPARE(r.fallbackReason, QStringLiteral("gif"));
    }
}

void ImageConvertTest::nullImageProducesNothing()
{
    const ImageEncode::Result r = ImageEncode::encode(QImage(), QStringLiteral("png"), 80);
    QVERIFY(!r.ok());
    QVERIFY(r.format.isEmpty());
    // Refused before ffmpeg would have been started, so a null capture cannot
    // spawn a process.
    QVERIFY(!ImageEncode::encode(QImage(), QStringLiteral("gif"), 80).ok());
}

void ImageConvertTest::unknownFormatIsPng()
{
    // Whatever a hand-edited config puts in the setting, the encoder produces a
    // format that exists rather than refusing to save the capture.
    for (const char *fmt : { "tiff", "", "bmp42" }) {
        const ImageEncode::Result r =
            ImageEncode::encode(opaque(), QString::fromLatin1(fmt), 80);
        QVERIFY2(r.ok(), fmt);
        QCOMPARE(r.format, QStringLiteral("png"));
        // Not a fallback: png IS what extensionFor answered, so there is nothing
        // to explain to the user.
        QVERIFY(r.fallbackReason.isEmpty());
    }
}

void ImageConvertTest::renamesToTheEncodedFormat()
{
    QCOMPARE(FilenameTemplate::withExtension(QStringLiteral("Unisic_2026-01-30.jpg"),
                                             QStringLiteral("png")),
             QStringLiteral("Unisic_2026-01-30.png"));
    // No extension yet: one is added rather than the name being left bare.
    QCOMPARE(FilenameTemplate::withExtension(QStringLiteral("shot"), QStringLiteral("gif")),
             QStringLiteral("shot.gif"));
    // The LAST dot decides, so a template with dots in the timestamp keeps its
    // name instead of losing everything after the first one.
    QCOMPARE(FilenameTemplate::withExtension(QStringLiteral("2026-01-30_12.30.11.webp"),
                                             QStringLiteral("png")),
             QStringLiteral("2026-01-30_12.30.11.png"));
    // A leading dot is the base name, not an extension: stripping it would leave
    // nothing to name the file with.
    QCOMPARE(FilenameTemplate::withExtension(QStringLiteral(".hidden"), QStringLiteral("png")),
             QStringLiteral(".hidden.png"));

    // An upload is named after the encode has already happened, so the name has
    // to come from the mime the encoder reported and not from what was asked
    // for - the two differ exactly when a fallback fired.
    QCOMPARE(FilenameTemplate::extensionForMime(QStringLiteral("image/png")),
             QStringLiteral("png"));
    QCOMPARE(FilenameTemplate::extensionForMime(QStringLiteral("image/jpeg")),
             QStringLiteral("jpg"));
    QCOMPARE(FilenameTemplate::extensionForMime(QStringLiteral("image/webp")),
             QStringLiteral("webp"));
    QCOMPARE(FilenameTemplate::extensionForMime(QStringLiteral("image/gif")),
             QStringLiteral("gif"));
    // A failed encode reports no mime at all. Those bytes are never sent, so
    // png is simply the harmless answer rather than a special case.
    QCOMPARE(FilenameTemplate::extensionForMime(QString()), QStringLiteral("png"));

    // And the two together are the actual rule: ask for GIF, get PNG bytes
    // because ffmpeg is missing, and the server is told PNG.
    const ImageEncode::Result r = ImageEncode::encode(opaque(), QStringLiteral("gif"), 90);
    QCOMPARE(FilenameTemplate::withExtension(QStringLiteral("shot.png"),
                                             FilenameTemplate::extensionForMime(r.mime)),
             QStringLiteral("shot.") + r.format);
}

// The guard every convert or re-encode path runs first. Its whole job is to
// answer "no work needed" without ever saying that about a file that really is
// in another format.
void ImageConvertTest::alreadyInThatFormat()
{
    using FilenameTemplate::sameFormat;
    QVERIFY(sameFormat(QStringLiteral("png"), QStringLiteral("png")));
    // Case comes off a real file name, so it is not the caller's job to fold it.
    QVERIFY(sameFormat(QStringLiteral("PNG"), QStringLiteral("png")));
    // The pair that used to be spelled out by hand at every call site, and was
    // missing from the history upload path: same bytes, two spellings.
    QVERIFY(sameFormat(QStringLiteral("jpeg"), QStringLiteral("jpg")));
    QVERIFY(sameFormat(QStringLiteral("JPEG"), QStringLiteral("jpg")));
    QVERIFY(!sameFormat(QStringLiteral("png"), QStringLiteral("jpg")));
    QVERIFY(!sameFormat(QStringLiteral("webp"), QStringLiteral("gif")));
    // A suffix Unisic cannot write is still a real format, and converting it is
    // exactly what the user asked for. Running `have` through extensionFor would
    // answer png here and refuse the conversion.
    QVERIFY(!sameFormat(QStringLiteral("bmp"), QStringLiteral("png")));
    QVERIFY(!sameFormat(QStringLiteral("tiff"), QStringLiteral("png")));
    // No extension at all is not "already png" either.
    QVERIFY(!sameFormat(QString(), QStringLiteral("png")));
}

QTEST_MAIN(ImageConvertTest)
#include "ImageConvertTest.moc"
