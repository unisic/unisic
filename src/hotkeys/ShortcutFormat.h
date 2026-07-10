#pragma once
#include <QKeySequence>
#include <Qt>

namespace ShortcutFormat {

// Portable key-sequence string for a recorded key press (the Settings
// shortcut recorder), or empty for a bare modifier press.
//
// Shift+digit quirk: the key event delivers the shifted SYMBOL (Shift+1
// arrives as Qt::Key_Exclam), but KGlobalAccel matches physical keys and
// KDE's own KCM stores "Meta+Shift+1" — recording "Meta+Shift+!" produces a
// binding that can never fire. Map shifted symbols back to their base keys
// while Shift is held.
//
// nativeScanCode gates the remap per physical key: the symbol positions in
// the map are US/Polish digit-row facts, and on e.g. a German layout '*' is
// Shift+Plus — blindly rewriting Key_Asterisk to Key_8 would bind the wrong
// physical key. Scancodes are layout-independent but PLATFORM-dependent:
// xcb reports XKB keycodes (digit row 1..0 = 10..19, minus 20, equal 21)
// while qtwayland reports raw evdev codes (the same keys minus 8: 2..13).
// BOTH are accepted — the symbol already selects the map entry, the
// scancode only confirms the physical position, so the dual match cannot
// alias across entries. Requiring only the XKB convention silently broke
// every recorded Shift+digit combo on Wayland (the gate never passed, the
// symbol was stored, and a 'Meta+Shift+!' binding never fires). 0
// (unknown) keeps the unconditional legacy mapping.
inline QString portable(int key, int modifiers, int nativeScanCode = 0)
{
    switch (key) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
        return {};
    default:
        break;
    }

    const auto allowed = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    const Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers) & allowed;

    if (mods & Qt::ShiftModifier) {
        struct Remap { int sym; int base; int scan; };
        static constexpr Remap kShiftRemap[] = {
            {Qt::Key_Exclam,      Qt::Key_1,     10},
            {Qt::Key_At,          Qt::Key_2,     11},
            {Qt::Key_NumberSign,  Qt::Key_3,     12},
            {Qt::Key_Dollar,      Qt::Key_4,     13},
            {Qt::Key_Percent,     Qt::Key_5,     14},
            {Qt::Key_AsciiCircum, Qt::Key_6,     15},
            {Qt::Key_Ampersand,   Qt::Key_7,     16},
            {Qt::Key_Asterisk,    Qt::Key_8,     17},
            {Qt::Key_ParenLeft,   Qt::Key_9,     18},
            {Qt::Key_ParenRight,  Qt::Key_0,     19},
            {Qt::Key_Underscore,  Qt::Key_Minus, 20},
            {Qt::Key_Plus,        Qt::Key_Equal, 21},
        };
        for (const Remap &m : kShiftRemap) {
            if (key == m.sym) {
                if (nativeScanCode == 0 || nativeScanCode == m.scan
                    || nativeScanCode + 8 == m.scan)
                    key = m.base;
                break;
            }
        }
    }

    return QKeySequence(mods.toInt() | key).toString(QKeySequence::PortableText);
}

} // namespace ShortcutFormat
