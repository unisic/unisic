#include <QtTest>
#include "record/StreamGeometry.h"

using StreamGeometry::streamMatchesScreen;

// The wrong-monitor guard for recordings: a portal restore token silently
// replays the share it was created from, so the stream that arrives may be a
// different monitor or the whole workspace. These pin the decision table.
class StreamGeometryTest : public QObject
{
    Q_OBJECT
private slots:
    // 1:1 monitor stream — the normal KDE case.
    void exactMatchPasses()
    {
        QVERIFY(streamMatchesScreen(QPoint(0, 0), QSize(1920, 1080),
                                    QRect(0, 0, 1920, 1080), 1.0,
                                    QSize(3840, 1080), 2));
    }

    // Portal reported neither position nor size: nothing to refute.
    void unknownEverythingPasses()
    {
        QVERIFY(streamMatchesScreen(QPoint(INT_MIN, INT_MIN), QSize(),
                                    QRect(1920, 0, 1920, 1080), 1.0,
                                    QSize(3840, 1080), 2));
    }

    // Stream of the OTHER monitor: position exposes it.
    void wrongPositionFails()
    {
        QVERIFY(!streamMatchesScreen(QPoint(0, 0), QSize(1920, 1080),
                                     QRect(1920, 0, 1920, 1080), 1.0,
                                     QSize(3840, 1080), 2));
    }

    // Workspace stream with the target at the origin: position matches the
    // primary's top-left, only the size gives it away (the reported bug —
    // two 1920x1080 side by side arrive as one 3840x1080 stream).
    void workspaceStreamAtOriginFails()
    {
        QVERIFY(!streamMatchesScreen(QPoint(0, 0), QSize(3840, 1080),
                                     QRect(0, 0, 1920, 1080), 1.0,
                                     QSize(3840, 1080), 2));
    }

    // GNOME fractional scaling: same monitor streamed at its logical size —
    // uniform rescale, must PASS (beginEncoding rescales the crop).
    void fractionalScalingUniformRescalePasses()
    {
        // 2560x1440 physical at 1.25 → logical 2048x1152 stream.
        QVERIFY(streamMatchesScreen(QPoint(0, 0), QSize(2048, 1152),
                                    QRect(0, 0, 2048, 1152), 1.25,
                                    QSize(3968, 1152), 2));
    }

    // Uniform-scale trap: 2x2 grid of equal monitors is exactly 2x the single
    // monitor in both axes — aspect check alone would let it through.
    void uniformWorkspaceGridFails()
    {
        QVERIFY(!streamMatchesScreen(QPoint(0, 0), QSize(3840, 2160),
                                     QRect(0, 0, 1920, 1080), 1.0,
                                     QSize(3840, 2160), 4));
    }

    // Single monitor: the union IS the monitor, logical-size stream there is
    // fractional scaling — the union check must not fire.
    void singleMonitorLogicalStreamPasses()
    {
        QVERIFY(streamMatchesScreen(QPoint(0, 0), QSize(1280, 720),
                                    QRect(0, 0, 1280, 720), 1.5,
                                    QSize(1280, 720), 1));
    }

    // HiDPI monitor shared correctly: physical-size stream, dpr 2.
    void hidpiPhysicalStreamPasses()
    {
        QVERIFY(streamMatchesScreen(QPoint(1920, 0), QSize(3840, 2160),
                                    QRect(1920, 0, 1920, 1080), 2.0,
                                    QSize(3840, 1080), 2));
    }

    // Mixed-DPR workspace at the target's scale: union*dpr must also fail.
    void workspaceAtTargetScaleFails()
    {
        QVERIFY(!streamMatchesScreen(QPoint(0, 0), QSize(7680, 2160),
                                     QRect(0, 0, 1920, 1080), 2.0,
                                     QSize(3840, 1080), 2));
    }
};

QTEST_APPLESS_MAIN(StreamGeometryTest)
#include "StreamGeometryTest.moc"
