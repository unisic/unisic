#include <QtTest>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QStandardPaths>
#include "FilenameTemplate.h"
#include "media/FfmpegUtil.h"

// The still-GIF save path. Qt cannot write a GIF at all (the bundled plugin
// only reads one), so this is the piece that has to be checked against real
// bytes rather than against a Qt call that would have failed loudly.
class StillGifTest : public QObject
{
    Q_OBJECT
private slots:
    void extensionKeepsGif();
    void encodesReadableGif();
    void qualityBuysColors();
    void nullImageIsEmpty();

private:
    static bool haveFfmpeg()
    {
        return !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    }
    // A gradient, not flat fill: a two-colour image encodes the same at every
    // palette size and would make the quality test pass without doing anything.
    static QImage sample(int w = 96, int h = 64)
    {
        QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        QLinearGradient g(0, 0, w, h);
        g.setColorAt(0.0, QColor(0xC8, 0xAC, 0xD6));
        g.setColorAt(0.5, QColor(0x43, 0x3D, 0x8B));
        g.setColorAt(1.0, QColor(0x17, 0x15, 0x3B));
        p.fillRect(img.rect(), g);
        p.end();
        return img;
    }
};

void StillGifTest::extensionKeepsGif()
{
    // Without this the format setting can never reach the encoder: the name
    // decides which branch saveImageTo takes.
    QCOMPARE(FilenameTemplate::extensionFor(QStringLiteral("gif")), QStringLiteral("gif"));
    QCOMPARE(FilenameTemplate::extensionFor(QStringLiteral("GIF")), QStringLiteral("gif"));
    // And nothing else leaked in with it.
    QCOMPARE(FilenameTemplate::extensionFor(QStringLiteral("tiff")), QStringLiteral("png"));
}

void StillGifTest::encodesReadableGif()
{
    if (!haveFfmpeg())
        QSKIP("no ffmpeg in PATH");
    const QImage src = sample();
    const QByteArray gif = FfmpegUtil::encodeStillGif(src, 90);
    QVERIFY2(!gif.isEmpty(), "encodeStillGif produced nothing");
    QVERIFY2(gif.startsWith("GIF89a") || gif.startsWith("GIF87a"),
             qPrintable(QStringLiteral("no GIF header: %1")
                            .arg(QString::fromLatin1(gif.left(6).toHex()))));

    QBuffer buf;
    buf.setData(gif);
    QVERIFY(buf.open(QIODevice::ReadOnly));
    QImageReader reader(&buf, "gif");
    // One frame: a still, so the app routes it to the image editor and not to
    // the trim window (AppContext::editableKindFor splits .gif on this).
    QCOMPARE(reader.imageCount(), 1);
    QCOMPARE(reader.size(), src.size());
    const QImage back = reader.read();
    QVERIFY(!back.isNull());
    QCOMPARE(back.size(), src.size());
}

void StillGifTest::qualityBuysColors()
{
    if (!haveFfmpeg())
        QSKIP("no ffmpeg in PATH");
    const QImage src = sample(256, 256);
    const QByteArray low = FfmpegUtil::encodeStillGif(src, 20);   // 64 colours
    const QByteArray high = FfmpegUtil::encodeStillGif(src, 100); // 256 colours
    QVERIFY(!low.isEmpty());
    QVERIFY(!high.isEmpty());
    // The palette lives in the file, so a bigger one is a bigger file. This is
    // the only externally visible proof that the quality slider reaches GIF.
    QVERIFY2(high.size() > low.size(),
             qPrintable(QStringLiteral("q100 %1 bytes is not larger than q20 %2 bytes")
                            .arg(high.size()).arg(low.size())));
}

void StillGifTest::nullImageIsEmpty()
{
    // No ffmpeg needed: refused before the process is started, which is what
    // keeps a null capture from spawning one.
    QVERIFY(FfmpegUtil::encodeStillGif(QImage(), 90).isEmpty());
}

QTEST_MAIN(StillGifTest)
#include "StillGifTest.moc"
