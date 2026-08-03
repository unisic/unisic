// The Repeater delegates below read `scene` and `root`, ids from outside their
// own component, which is exactly what Bound makes legal.
pragma ComponentBehavior: Bound

import QtQuick
import Unisic
import Unisic.Kit

// In-window preview of the capture overlay: a mock desktop with the selection
// chrome the overlay really draws - mode badge, mode frame, selection box and
// handles, the toolbar in its configured spot, the alignment guides and the
// pixel loupe.
//
// It exists because none of those settings could be SEEN from Settings. The
// overlay only appears once a capture is already running, over whatever the
// user happens to have on screen and holding an exclusive keyboard grab, so
// checking what a switch did meant leaving the page, taking a capture and
// coming back - and the toolbar-position drop-down and the mode badge were
// missed entirely (user-reported). This keeps the whole decision on the page.
//
// Deliberately an APPROXIMATION, not the real OverlayWindow: that one needs a
// frozen screen from the portal and an AnnotationCanvas per monitor. What is
// shared is what matters - the same Theme tokens, the same mode colours through
// Theme.modeColor(), and the same positioning RULES for the toolbar.
Item {
    id: root

    // One of OverlayController::purposeName()'s values.
    property string purpose: "shot"

    // The five capture modes, for the picker above this preview. Kept here and
    // not in the page so the preview and its picker cannot disagree about what
    // a mode is called.
    readonly property var modes: [
        { id: "shot",    iconName: "region",       label: qsTr("Screenshot") },
        { id: "measure", iconName: "measure",      label: qsTr("Measure") },
        { id: "ocr",     iconName: "ocr",          label: qsTr("Read text") },
        { id: "gif",     iconName: "gif",          label: qsTr("Record GIF") },
        { id: "video",   iconName: "media-record", label: qsTr("Record video") }
    ]
    function modeAt(id) {
        for (var i = 0; i < root.modes.length; ++i)
            if (root.modes[i].id === id)
                return root.modes[i]
        return root.modes[0]
    }
    readonly property var mode: root.modeAt(root.purpose)
    readonly property color modeColor: Theme.modeColor(root.purpose)
    readonly property bool showMode: App.settings.overlayModeBadge
    readonly property string modeStyle: App.settings.overlayModeStyle

    implicitHeight: Math.round(width * 0.5)

    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Capture overlay preview")
    Accessible.description: qsTr("A mock screen showing the capture overlay with the current settings: %1 mode.")
                            .arg(root.mode.label)

    Rectangle {
        id: scene
        objectName: "overlayPreviewScene"
        anchors.fill: parent
        radius: Theme.radiusM
        color: Theme.mediaBase
        border.width: 1
        border.color: Theme.divider
        clip: true

        // Mock desktop: a light half and a dark one, because the mode frame and
        // the selection have to read over both and the same colour rarely does.
        // Grey, not the media tokens at full strength: those are pure white and
        // pure black, and a black half turns the scrim into a highlight instead
        // of the dimming it really is.
        Rectangle {
            width: parent.width / 2
            height: parent.height
            color: Theme.alpha(Theme.mediaText, 0.72)
        }
        Rectangle {
            x: parent.width / 2
            width: parent.width / 2
            height: parent.height
            color: Theme.alpha(Theme.mediaText, 0.3)
        }
        // A few bars standing in for windows and text, so the scrim over the
        // unselected area has something to dim.
        Repeater {
            model: 7
            Rectangle {
                required property int index
                x: Math.round(scene.width * (0.06 + 0.13 * (index % 4)))
                y: Math.round(scene.height * (0.12 + 0.16 * (index % 5)))
                width: Math.round(scene.width * (index % 3 === 0 ? 0.22 : 0.14))
                height: Math.max(3, Math.round(scene.height * 0.035))
                radius: height / 2
                color: x < scene.width / 2 ? Theme.alpha(Theme.mediaBase, 0.22)
                                           : Theme.alpha(Theme.mediaText, 0.22)
            }
        }

        // The selection, in fractions of the mock screen. The icon style gets a
        // narrower one, placed so that it cuts the mode glyph without covering
        // it: the glyph lives in the middle and loses whatever the selection
        // overlaps, and a mock selection that swallowed the centre whole would
        // demonstrate the style by showing nothing at all.
        readonly property rect sel: root.showMode && root.modeStyle === "icon"
            ? Qt.rect(Math.round(width * 0.12), Math.round(height * 0.26),
                      Math.round(width * 0.36), Math.round(height * 0.46))
            : Qt.rect(Math.round(width * 0.18), Math.round(height * 0.26),
                      Math.round(width * 0.52), Math.round(height * 0.46))

        // Scrim outside the selection, drawn as the four bands around it: the
        // overlay dims everything you are not capturing.
        Repeater {
            model: [Qt.rect(0, 0, scene.width, scene.sel.y),
                    Qt.rect(0, scene.sel.y + scene.sel.height, scene.width,
                            scene.height - scene.sel.y - scene.sel.height),
                    Qt.rect(0, scene.sel.y, scene.sel.x, scene.sel.height),
                    Qt.rect(scene.sel.x + scene.sel.width, scene.sel.y,
                            scene.width - scene.sel.x - scene.sel.width, scene.sel.height)]
            Rectangle {
                required property rect modelData
                x: modelData.x; y: modelData.y
                width: modelData.width; height: modelData.height
                // Theme.primary at 150/255, the same tint and the same strength
                // AnnotationCanvas paints it with (uiScrim, alpha 150) - so a
                // light theme dims light here exactly as it does on the overlay.
                color: Theme.alpha(Theme.primary, 150 / 255)
            }
        }

        // Alignment guides: the crosshair from the cursor to the screen edges.
        // Anchored on the selection's bottom-right corner, which is where the
        // hand that drew it just let go.
        Rectangle {
            visible: App.settings.selectionGuides
            x: scene.sel.x + scene.sel.width
            width: 1
            height: scene.height
            color: Theme.alpha(Theme.accent, 0.45)
        }
        Rectangle {
            visible: App.settings.selectionGuides
            y: scene.sel.y + scene.sel.height
            width: scene.width
            height: 1
            color: Theme.alpha(Theme.accent, 0.45)
        }

        // Selection box, in the mode colour when the badge is on - the same
        // AnnotationCanvas.uiAccent the real overlay is given.
        Rectangle {
            x: scene.sel.x; y: scene.sel.y
            width: scene.sel.width; height: scene.sel.height
            color: "transparent"
            border.width: 2
            border.color: root.showMode ? root.modeColor : Theme.accent
        }
        Repeater {
            model: [Qt.point(0, 0), Qt.point(0.5, 0), Qt.point(1, 0),
                    Qt.point(0, 0.5), Qt.point(1, 0.5),
                    Qt.point(0, 1), Qt.point(0.5, 1), Qt.point(1, 1)]
            Rectangle {
                required property point modelData
                readonly property int s: 6
                x: scene.sel.x + modelData.x * scene.sel.width - s / 2
                y: scene.sel.y + modelData.y * scene.sel.height - s / 2
                width: s; height: s
                radius: 1
                color: root.showMode ? root.modeColor : Theme.accent
            }
        }

        // Dimension readout, the way the overlay puts it above the selection.
        Rectangle {
            x: scene.sel.x
            y: Math.max(2, scene.sel.y - height - 4)
            width: dimText.implicitWidth + 10
            height: 14
            radius: 7
            color: Theme.recBadgeBg
            Text {
                id: dimText
                anchors.centerIn: parent
                // Scaled up: the mock is a few hundred pixels wide, and a
                // readout in ITS pixels would look like a report about the
                // user's screen while naming a region nobody could select.
                text: Math.round(scene.sel.width * 4) + " × " + Math.round(scene.sel.height * 4)
                color: Theme.recBadgeText
                font.pixelSize: 8
                font.family: "monospace"
            }
        }

        // Pixel loupe: a magnified pixel grid parked by the corner the guides
        // cross at.
        Rectangle {
            visible: App.settings.pixelLoupe
            readonly property int s: 30
            x: Math.min(scene.width - s - 4, scene.sel.x + scene.sel.width + 6)
            y: Math.min(scene.height - s - 4, scene.sel.y + scene.sel.height + 6)
            width: s; height: s
            radius: 3
            color: Theme.recBadgeBg
            border.width: 1
            border.color: Theme.alpha(Theme.mediaText, 0.35)
            Grid {
                anchors.centerIn: parent
                rows: 4; columns: 4; spacing: 1
                Repeater {
                    model: 16
                    Rectangle {
                        required property int index
                        width: 5; height: 5
                        color: index === 5 ? Theme.accent
                                           : Theme.alpha(Theme.mediaText, 0.18 + 0.04 * (index % 5))
                    }
                }
            }
        }

        // Toolbar. Same placement rules as OverlayWindow.toolbarX/toolbarY,
        // scaled down: follow the selection (flipping above it when there is no
        // room below), or pin to the named edge.
        Rectangle {
            id: bar
            // The smoke check finds it by name and asserts the clamp keeps it
            // on the mock screen for all ten configured positions.
            objectName: "overlayPreviewToolbar"
            readonly property int m: 6
            readonly property string pos: App.settings.overlayToolbarPosition
            width: Math.min(scene.width - 2 * m, 96)
            height: 18
            radius: 9
            color: Theme.primary
            opacity: 0.94
            border.width: 1
            border.color: Theme.divider
            function clampX(v) { return Math.max(m, Math.min(v, scene.width - width - m)) }
            function clampY(v) { return Math.max(m, Math.min(v, scene.height - height - m)) }
            x: {
                if (pos === "follow")
                    return clampX(scene.sel.x + scene.sel.width / 2 - width / 2)
                if (pos.indexOf("left") >= 0) return clampX(m)
                if (pos.indexOf("right") >= 0) return clampX(scene.width - width - m)
                return clampX(scene.width / 2 - width / 2)
            }
            y: {
                if (pos === "follow") {
                    const below = scene.sel.y + scene.sel.height + m
                    if (below + height <= scene.height - m)
                        return below
                    return clampY(scene.sel.y - height - m)
                }
                // The top row leaves the mode badge its own line, exactly as the
                // overlay does.
                if (pos.indexOf("top") >= 0) return clampY(m + (root.showMode ? 20 : 0))
                if (pos.indexOf("bottom") >= 0) return clampY(scene.height - height - m)
                return clampY(scene.height / 2 - height / 2)
            }
            Row {
                anchors.centerIn: parent
                spacing: 5
                Repeater {
                    model: 6
                    Rectangle {
                        required property int index
                        width: 8; height: 8
                        radius: 2
                        color: index === 0 ? Theme.accent : Theme.alpha(Theme.textSecondary, 0.55)
                    }
                }
            }
        }

        // Mode frame: contrast hairline, the mode colour, contrast hairline.
        Rectangle {
            visible: root.showMode
            anchors.fill: parent
            radius: scene.radius
            color: "transparent"
            border.width: 1
            border.color: Theme.recordFrameContrast
        }
        Rectangle {
            visible: root.showMode
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(0, scene.radius - 1)
            color: "transparent"
            border.width: 3
            border.color: root.modeColor
        }
        Rectangle {
            visible: root.showMode
            anchors.fill: parent
            anchors.margins: 4
            radius: Math.max(0, scene.radius - 4)
            color: "transparent"
            border.width: 1
            border.color: Theme.recordFrameContrast
        }

        // Mode glyph variant, cut by the selection exactly as on the overlay -
        // the same component, so the mock cannot drift from it.
        UModeGlyph {
            objectName: "overlayPreviewModeIcon"
            anchors.fill: parent
            name: root.mode.iconName
            tint: root.modeColor
            hole: scene.sel
            opacity: 0.35
            visible: root.showMode && root.modeStyle === "icon"
        }

        // Mode badge.
        Rectangle {
            objectName: "overlayPreviewBadge"
            visible: root.showMode && root.modeStyle !== "icon"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 8
            width: badgeRow.implicitWidth + 16
            height: 20
            radius: 10
            color: Theme.recBadgeBg
            border.width: 1
            border.color: root.modeColor
            Row {
                id: badgeRow
                anchors.centerIn: parent
                spacing: 4
                UIcon {
                    name: root.mode.iconName
                    size: 11
                    color: root.modeColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: root.mode.label
                    color: Theme.recBadgeText
                    font.pixelSize: 10
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
