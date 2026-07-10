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
// while Shift is held. The digit row is identical on US- and Polish-style
// layouts (the ones this targets); other layouts still record a working
// modifier-free symbol.
inline QString portable(int key, int modifiers)
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
        switch (key) {
        case Qt::Key_Exclam:      key = Qt::Key_1; break;
        case Qt::Key_At:          key = Qt::Key_2; break;
        case Qt::Key_NumberSign:  key = Qt::Key_3; break;
        case Qt::Key_Dollar:      key = Qt::Key_4; break;
        case Qt::Key_Percent:     key = Qt::Key_5; break;
        case Qt::Key_AsciiCircum: key = Qt::Key_6; break;
        case Qt::Key_Ampersand:   key = Qt::Key_7; break;
        case Qt::Key_Asterisk:    key = Qt::Key_8; break;
        case Qt::Key_ParenLeft:   key = Qt::Key_9; break;
        case Qt::Key_ParenRight:  key = Qt::Key_0; break;
        case Qt::Key_Underscore:  key = Qt::Key_Minus; break;
        case Qt::Key_Plus:        key = Qt::Key_Equal; break;
        default:
            break;
        }
    }

    return QKeySequence(mods.toInt() | key).toString(QKeySequence::PortableText);
}

} // namespace ShortcutFormat
