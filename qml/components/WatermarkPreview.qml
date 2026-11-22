import QtQuick
import QtQuick.Window
import Unisic.Kit

// A watermark on a mock capture, served by image://watermark (WatermarkPreview.cpp)
// from the live settings: Settings > Screenshots and Settings > Clips each show one.
Column {
    id: root
    property string source
    property string caption
    spacing: Theme.spacingXS
    Rectangle {
        width: parent.width
        height: Math.round(width * 0.56)
        radius: Theme.radiusM
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
        clip: true
        Image {
            anchors.fill: parent
            anchors.margins: 1
            // Asked for in device pixels so the preview is
            // sharp on a scaled display; the provider paints
            // exactly what is requested.
            sourceSize: Qt.size(
                Math.max(1, Math.round(width * (Screen.devicePixelRatio || 1))),
                Math.max(1, Math.round(height * (Screen.devicePixelRatio || 1))))
            source: root.source
            // Synchronous ON PURPOSE: `true` here moves the
            // provider onto a worker thread, where it reads
            // AppContext's settings unlocked. It refuses and
            // warns rather than racing, so this is a blank
            // box, not a crash - but make the provider
            // thread-safe before flipping it.
            asynchronous: false
            // Every setting change mints a new URL; caching
            // them would grow the pixmap cache for the whole
            // session over one drag of the opacity slider.
            cache: false
            fillMode: Image.PreserveAspectFit
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("Watermark preview")
            Accessible.description: qsTr("A sample capture with the current watermark settings applied.")
        }
    }
    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        text: root.caption
        color: Theme.textTertiary
        font.pixelSize: Theme.fontS
    }
}
