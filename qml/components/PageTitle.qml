import QtQuick
import Unisic.Kit

// The heading at the top of the Capture and Record pages.
Text {
    elide: Text.ElideRight
    color: Theme.textPrimary
    font.pixelSize: 26
    font.weight: Font.DemiBold
    font.letterSpacing: -0.52
    Accessible.role: Accessible.Heading
    Accessible.name: text
}
