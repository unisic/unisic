// Pins KeystrokeOverlayPainter's display model: chord building (modifier
// order, ×N repeat), held-modifier display, lifetime/fade expiry, and that
// paint() actually lands a badge in the frame. Pure logic — timestamps are
// synthetic, no libinput involved.
#include <QtTest>
#include <QImage>
#include <QPainter>
#include <linux/input-event-codes.h>

#include "record/KeystrokeOverlayPainter.h"

namespace {
constexpr qint64 kMs = 1000000LL;   // ns per ms
}

class KeystrokeOverlayTest : public QObject
{
    Q_OBJECT
private slots:
    void chordShowsModifiersInCanonicalOrder();
    void chordSurvivesModifierRelease();
    void chordExpiresAfterLifetime();
    void repeatCounterCounts();
    void heldModifiersAloneShowAndClear();
    void unknownKeysIgnored();
    void paintDrawsBadgeIntoFrame();
};

void KeystrokeOverlayTest::chordShowsModifiersInCanonicalOrder()
{
    KeystrokeOverlayPainter p;
    // Held in "wrong" order — display must still be Meta, Ctrl, Alt, Shift.
    p.keyEvent(KEY_LEFTSHIFT, true, 0);
    p.keyEvent(KEY_LEFTCTRL, true, 1 * kMs);
    p.keyEvent(KEY_T, true, 2 * kMs);
    QCOMPARE(p.textAt(3 * kMs), QStringLiteral("Ctrl+Shift+T"));
}

void KeystrokeOverlayTest::chordSurvivesModifierRelease()
{
    KeystrokeOverlayPainter p;
    p.keyEvent(KEY_LEFTCTRL, true, 0);
    p.keyEvent(KEY_C, true, 1 * kMs);
    p.keyEvent(KEY_LEFTCTRL, false, 2 * kMs);
    p.keyEvent(KEY_C, false, 2 * kMs);
    // Everything released: the chord badge must keep showing for its lifetime.
    QCOMPARE(p.textAt(100 * kMs), QStringLiteral("Ctrl+C"));
}

void KeystrokeOverlayTest::chordExpiresAfterLifetime()
{
    KeystrokeOverlayPainter p;
    p.keyEvent(KEY_A, true, 0);
    const qint64 life = qint64(p.style().badgeMs + p.style().fadeMs) * kMs;
    QVERIFY(p.hasContent(life - 1));
    QVERIFY(!p.hasContent(life + 1));
    QCOMPARE(p.textAt(life + 1), QString());
}

void KeystrokeOverlayTest::repeatCounterCounts()
{
    KeystrokeOverlayPainter p;
    p.keyEvent(KEY_V, true, 0);
    p.keyEvent(KEY_V, false, 10 * kMs);
    p.keyEvent(KEY_V, true, 200 * kMs);
    p.keyEvent(KEY_V, true, 400 * kMs);
    QCOMPARE(p.textAt(401 * kMs), QStringLiteral("V ×3"));
    // A different key resets the counter.
    p.keyEvent(KEY_B, true, 500 * kMs);
    QCOMPARE(p.textAt(501 * kMs), QStringLiteral("B"));
}

void KeystrokeOverlayTest::heldModifiersAloneShowAndClear()
{
    KeystrokeOverlayPainter p;
    p.keyEvent(KEY_LEFTMETA, true, 0);
    QCOMPARE(p.textAt(1 * kMs), QStringLiteral("Meta"));
    p.keyEvent(KEY_LEFTMETA, false, 2 * kMs);
    QCOMPARE(p.textAt(3 * kMs), QString());
    QVERIFY(!p.hasContent(3 * kMs));
}

void KeystrokeOverlayTest::unknownKeysIgnored()
{
    KeystrokeOverlayPainter p;
    p.keyEvent(KEY_PLAYPAUSE, true, 0);
    QVERIFY(!p.hasContent(1 * kMs));
}

void KeystrokeOverlayTest::paintDrawsBadgeIntoFrame()
{
    KeystrokeOverlayPainter kp;
    kp.keyEvent(KEY_LEFTCTRL, true, 0);
    kp.keyEvent(KEY_S, true, 1 * kMs);
    kp.keyEvent(KEY_LEFTCTRL, false, 2 * kMs);   // nothing held afterwards

    QImage frame(640, 360, QImage::Format_ARGB32);
    frame.fill(QColor(128, 128, 128));
    const QImage before = frame;
    {
        QPainter p(&frame);
        kp.paint(p, 2 * kMs);
    }
    QVERIFY(frame != before);
    // The badge is bottom-center: that pixel must have changed…
    QVERIFY(frame.pixel(320, 340) != before.pixel(320, 340));
    // …and the top half must be untouched.
    QCOMPARE(frame.copy(0, 0, 640, 180), before.copy(0, 0, 640, 180));

    // Past its lifetime the painter must not touch the frame at all.
    QImage after = before;
    {
        QPainter p(&after);
        kp.paint(p, (1 + kp.style().badgeMs + kp.style().fadeMs + 10) * kMs);
    }
    QCOMPARE(after, before);
}

QTEST_MAIN(KeystrokeOverlayTest)
#include "KeystrokeOverlayTest.moc"
