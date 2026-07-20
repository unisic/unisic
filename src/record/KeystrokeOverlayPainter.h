#pragma once
#include <QColor>
#include <QSet>
#include <QString>

class QPainter;

// Draws a screenkey-style keystroke badge (bottom-center pill: "Ctrl+Shift+T",
// "V ×3", held modifiers) into a recorded frame, in the frame's own pixel
// space. Fed raw evdev keycodes from KeyCapture; keeps no per-frame state
// beyond the current badge, so a dropped sample never desyncs it.
//
// Display model (chosen at paint time, so event ordering cannot wedge it):
//   1. a full chord (modifiers + a non-modifier key) is shown for badgeMs and
//      then fades out; pressing the same chord again while visible bumps a
//      "×N" repeat counter instead of restarting the text;
//   2. otherwise, while any modifier is physically held, the held modifiers
//      are shown (no fade — they disappear on release).
//
// Key labels are the PHYSICAL key legends (US/QWERTY): libinput gives evdev
// codes only, and the compositor's xkb layout is not observable from here.
// For letters, digits, F-keys and modifiers — what a shortcut badge is for —
// the legend matches every common layout.
//
// Not thread-safe and not meant to be: the recorder owns one and paints from
// the sampling timer (same contract as CursorOverlayPainter).
class KeystrokeOverlayPainter
{
public:
    struct Style {
        int badgeMs = 1400;   // full-chord lifetime before the fade starts
        int fadeMs = 300;     // fade-out tail
        // Badge colors — the active theme's keystrokeBg/keystrokeText tokens
        // (defaults match Theme.qml's). Alphas here are the base; the fade-out
        // multiplies them.
        QColor badgeBg{0, 0, 0, 175};
        QColor badgeText{255, 255, 255};
    };

    void setStyle(const Style &s) { m_style = s; }
    const Style &style() const { return m_style; }

    // A key changed state, tNs CLOCK_MONOTONIC nanoseconds (libinput's clock).
    void keyEvent(quint32 evdevCode, bool pressed, qint64 tNs);

    // True when anything would actually be drawn — lets the recorder skip the
    // whole compositing copy on a frame with nothing on it.
    bool hasContent(qint64 nowNs) const;

    // Paints into `p`, which must already be set up in frame pixel space; the
    // frame size is taken from p.device().
    void paint(QPainter &p, qint64 nowNs);

    void reset();

    // The text paint() would draw at `nowNs` ("" for none). Exposed for the
    // unit test and the dev harness — pixel output alone can't assert chords.
    QString textAt(qint64 nowNs) const;

    // Evdev keycode -> badge legend ("A", "F5", "Space", "←"); "" for keys the
    // badge ignores (media keys, etc.). Modifiers get their chord name.
    static QString keyName(quint32 evdevCode);
    static bool isModifier(quint32 evdevCode);

private:
    QString heldModifierText() const;

    Style m_style;
    QSet<quint32> m_held;      // physically-held modifier keycodes
    QString m_chord;           // last full-chord text (without the ×N suffix)
    qint64 m_chordNs = -1;     // when the chord was (re)armed; -1 = none
    int m_repeat = 1;          // consecutive same-chord presses while visible
};
