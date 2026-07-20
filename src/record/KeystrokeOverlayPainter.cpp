#include "KeystrokeOverlayPainter.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPaintDevice>
#include <QPainter>
#include <QRectF>
#include <linux/input-event-codes.h>

bool KeystrokeOverlayPainter::isModifier(quint32 code)
{
    switch (code) {
    case KEY_LEFTCTRL: case KEY_RIGHTCTRL:
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:
    case KEY_LEFTALT: case KEY_RIGHTALT:
    case KEY_LEFTMETA: case KEY_RIGHTMETA:
        return true;
    default:
        return false;
    }
}

QString KeystrokeOverlayPainter::keyName(quint32 code)
{
    switch (code) {
    // Modifiers (chord names — KDE convention, AltGr kept distinct).
    case KEY_LEFTCTRL: case KEY_RIGHTCTRL:   return QStringLiteral("Ctrl");
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: return QStringLiteral("Shift");
    case KEY_LEFTALT:                        return QStringLiteral("Alt");
    case KEY_RIGHTALT:                       return QStringLiteral("AltGr");
    case KEY_LEFTMETA: case KEY_RIGHTMETA:   return QStringLiteral("Meta");
    // Letters.
    case KEY_A: return QStringLiteral("A"); case KEY_B: return QStringLiteral("B");
    case KEY_C: return QStringLiteral("C"); case KEY_D: return QStringLiteral("D");
    case KEY_E: return QStringLiteral("E"); case KEY_F: return QStringLiteral("F");
    case KEY_G: return QStringLiteral("G"); case KEY_H: return QStringLiteral("H");
    case KEY_I: return QStringLiteral("I"); case KEY_J: return QStringLiteral("J");
    case KEY_K: return QStringLiteral("K"); case KEY_L: return QStringLiteral("L");
    case KEY_M: return QStringLiteral("M"); case KEY_N: return QStringLiteral("N");
    case KEY_O: return QStringLiteral("O"); case KEY_P: return QStringLiteral("P");
    case KEY_Q: return QStringLiteral("Q"); case KEY_R: return QStringLiteral("R");
    case KEY_S: return QStringLiteral("S"); case KEY_T: return QStringLiteral("T");
    case KEY_U: return QStringLiteral("U"); case KEY_V: return QStringLiteral("V");
    case KEY_W: return QStringLiteral("W"); case KEY_X: return QStringLiteral("X");
    case KEY_Y: return QStringLiteral("Y"); case KEY_Z: return QStringLiteral("Z");
    // Top-row digits.
    case KEY_1: return QStringLiteral("1"); case KEY_2: return QStringLiteral("2");
    case KEY_3: return QStringLiteral("3"); case KEY_4: return QStringLiteral("4");
    case KEY_5: return QStringLiteral("5"); case KEY_6: return QStringLiteral("6");
    case KEY_7: return QStringLiteral("7"); case KEY_8: return QStringLiteral("8");
    case KEY_9: return QStringLiteral("9"); case KEY_0: return QStringLiteral("0");
    // Function keys.
    case KEY_F1: return QStringLiteral("F1");   case KEY_F2: return QStringLiteral("F2");
    case KEY_F3: return QStringLiteral("F3");   case KEY_F4: return QStringLiteral("F4");
    case KEY_F5: return QStringLiteral("F5");   case KEY_F6: return QStringLiteral("F6");
    case KEY_F7: return QStringLiteral("F7");   case KEY_F8: return QStringLiteral("F8");
    case KEY_F9: return QStringLiteral("F9");   case KEY_F10: return QStringLiteral("F10");
    case KEY_F11: return QStringLiteral("F11"); case KEY_F12: return QStringLiteral("F12");
    // Editing / navigation.
    case KEY_SPACE:     return QStringLiteral("Space");
    case KEY_ENTER:     return QStringLiteral("Enter");
    case KEY_TAB:       return QStringLiteral("Tab");
    case KEY_ESC:       return QStringLiteral("Esc");
    case KEY_BACKSPACE: return QStringLiteral("Backspace");
    case KEY_DELETE:    return QStringLiteral("Del");
    case KEY_INSERT:    return QStringLiteral("Ins");
    case KEY_HOME:      return QStringLiteral("Home");
    case KEY_END:       return QStringLiteral("End");
    case KEY_PAGEUP:    return QStringLiteral("PgUp");
    case KEY_PAGEDOWN:  return QStringLiteral("PgDn");
    case KEY_LEFT:      return QStringLiteral("←");
    case KEY_RIGHT:     return QStringLiteral("→");
    case KEY_UP:        return QStringLiteral("↑");
    case KEY_DOWN:      return QStringLiteral("↓");
    case KEY_MENU:      return QStringLiteral("Menu");
    case KEY_SYSRQ:     return QStringLiteral("PrtSc");
    case KEY_CAPSLOCK:  return QStringLiteral("CapsLock");
    case KEY_NUMLOCK:   return QStringLiteral("NumLock");
    case KEY_SCROLLLOCK:return QStringLiteral("ScrollLock");
    case KEY_PAUSE:     return QStringLiteral("Pause");
    case KEY_COMPOSE:   return QStringLiteral("Menu");
    // US-legend punctuation.
    case KEY_MINUS:      return QStringLiteral("-");
    case KEY_EQUAL:      return QStringLiteral("=");
    case KEY_LEFTBRACE:  return QStringLiteral("[");
    case KEY_RIGHTBRACE: return QStringLiteral("]");
    case KEY_SEMICOLON:  return QStringLiteral(";");
    case KEY_APOSTROPHE: return QStringLiteral("'");
    case KEY_GRAVE:      return QStringLiteral("`");
    case KEY_COMMA:      return QStringLiteral(",");
    case KEY_DOT:        return QStringLiteral(".");
    case KEY_SLASH:      return QStringLiteral("/");
    case KEY_BACKSLASH:  return QStringLiteral("\\");
    case KEY_102ND:      return QStringLiteral("<");
    // Numpad.
    case KEY_KP0: return QStringLiteral("Num0"); case KEY_KP1: return QStringLiteral("Num1");
    case KEY_KP2: return QStringLiteral("Num2"); case KEY_KP3: return QStringLiteral("Num3");
    case KEY_KP4: return QStringLiteral("Num4"); case KEY_KP5: return QStringLiteral("Num5");
    case KEY_KP6: return QStringLiteral("Num6"); case KEY_KP7: return QStringLiteral("Num7");
    case KEY_KP8: return QStringLiteral("Num8"); case KEY_KP9: return QStringLiteral("Num9");
    case KEY_KPPLUS:     return QStringLiteral("Num+");
    case KEY_KPMINUS:    return QStringLiteral("Num-");
    case KEY_KPASTERISK: return QStringLiteral("Num*");
    case KEY_KPSLASH:    return QStringLiteral("Num/");
    case KEY_KPDOT:      return QStringLiteral("Num.");
    case KEY_KPENTER:    return QStringLiteral("Enter");
    default:
        return QString();   // media keys etc.: not a badge's business
    }
}

QString KeystrokeOverlayPainter::heldModifierText() const
{
    if (m_held.isEmpty())
        return QString();
    // QKeySequence display order: Meta, Ctrl, Alt, Shift (AltGr after Alt).
    QStringList out;
    const auto holds = [this](quint32 a, quint32 b) {
        return m_held.contains(a) || m_held.contains(b);
    };
    if (holds(KEY_LEFTMETA, KEY_RIGHTMETA))   out << QStringLiteral("Meta");
    if (holds(KEY_LEFTCTRL, KEY_RIGHTCTRL))   out << QStringLiteral("Ctrl");
    if (m_held.contains(KEY_LEFTALT))         out << QStringLiteral("Alt");
    if (m_held.contains(KEY_RIGHTALT))        out << QStringLiteral("AltGr");
    if (holds(KEY_LEFTSHIFT, KEY_RIGHTSHIFT)) out << QStringLiteral("Shift");
    return out.join(QLatin1Char('+'));
}

void KeystrokeOverlayPainter::keyEvent(quint32 code, bool pressed, qint64 tNs)
{
    if (isModifier(code)) {
        if (pressed)
            m_held.insert(code);
        else
            m_held.remove(code);
        return;   // held-modifier display is derived at paint time
    }
    if (!pressed)
        return;
    const QString key = keyName(code);
    if (key.isEmpty())
        return;
    const QString mods = heldModifierText();
    const QString chord = mods.isEmpty() ? key : mods + QLatin1Char('+') + key;
    const bool visible = m_chordNs >= 0
        && tNs - m_chordNs < qint64(m_style.badgeMs + m_style.fadeMs) * 1000000LL;
    m_repeat = (visible && chord == m_chord) ? m_repeat + 1 : 1;
    m_chord = chord;
    m_chordNs = tNs;
}

QString KeystrokeOverlayPainter::textAt(qint64 nowNs) const
{
    if (m_chordNs >= 0
        && nowNs - m_chordNs < qint64(m_style.badgeMs + m_style.fadeMs) * 1000000LL) {
        return m_repeat > 1
            ? m_chord + QStringLiteral(" ×%1").arg(m_repeat)
            : m_chord;
    }
    return heldModifierText();
}

bool KeystrokeOverlayPainter::hasContent(qint64 nowNs) const
{
    return !textAt(nowNs).isEmpty();
}

void KeystrokeOverlayPainter::paint(QPainter &p, qint64 nowNs)
{
    const QString text = textAt(nowNs);
    if (text.isEmpty() || !p.device())
        return;
    const int fw = p.device()->width();
    const int fh = p.device()->height();
    if (fw < 40 || fh < 40)
        return;

    // Chords fade out over the last fadeMs; held-modifiers-only never fade
    // (they vanish on release).
    double alpha = 1.0;
    if (m_chordNs >= 0 && nowNs - m_chordNs >= 0) {
        const qint64 ageMs = (nowNs - m_chordNs) / 1000000LL;
        if (ageMs < m_style.badgeMs + m_style.fadeMs && ageMs > m_style.badgeMs)
            alpha = 1.0 - double(ageMs - m_style.badgeMs) / m_style.fadeMs;
    }

    // Scale with the frame so the badge survives GIF downscaling.
    const int px = qBound(13, fh / 24, 64);
    QFont font;
    font.setBold(true);
    font.setPixelSize(px);
    const QFontMetrics fm(font);
    const int tw = fm.horizontalAdvance(text);
    const int padX = px * 4 / 5, padY = px * 2 / 5;
    const int bw = tw + 2 * padX, bh = fm.height() + 2 * padY;
    const QRectF badge((fw - bw) / 2.0, fh - bh - qMax(12, fh / 26), bw, bh);

    QColor bg = m_style.badgeBg;
    bg.setAlphaF(bg.alphaF() * alpha);
    QColor fg = m_style.badgeText;
    fg.setAlphaF(fg.alphaF() * alpha);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(badge, bh / 2.0, bh / 2.0);
    p.setFont(font);
    p.setPen(fg);
    p.drawText(badge, Qt::AlignCenter, text);
    p.restore();
}

void KeystrokeOverlayPainter::reset()
{
    m_held.clear();
    m_chord.clear();
    m_chordNs = -1;
    m_repeat = 1;
}
