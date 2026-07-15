import QtQuick
import QtQuick.Effects
import Unisic
import "../components"

// Files you already have, not captures: an image opens in the same editor a
// screenshot does, a recording in the same trim window History's Trim opens.
// Its own page rather than a fourth card on Capture — nothing here captures
// anything, and the two cases want different file dialogs.
Item {
    Flickable {
        id: pageFlick
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        MiddleScroll { flickable: pageFlick }
        WheelBoost { flickable: pageFlick }

        Column {
            id: col
            width: parent.width
            spacing: Theme.spacingL

            Text {
                text: qsTr("Edit")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
            }
            Text {
                text: qsTr("Open a picture or a video you already have — the same editor and trim window your captures use.")
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
            }

            Item { width: 1; height: Theme.spacingS }

            // Flow, not Row: same reason as the capture cards — a narrow window
            // wraps instead of clipping.
            Flow {
                width: parent.width
                spacing: Theme.spacingL

                Repeater {
                    model: [
                        { iconName: "edit", title: qsTr("Edit an image"),
                          sub: qsTr("Annotate, crop, blur"), kind: "image" },
                        { iconName: "cut", title: qsTr("Trim a video"),
                          sub: qsTr("Cut a start and an end"), kind: "video",
                          // Trimming shells out to ffmpeg; without recording
                          // support the rest of the app has no ffmpeg either.
                          available: true },
                    ]

                    delegate: Rectangle {
                        width: 218
                        height: 172
                        radius: Theme.radiusXL
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: cardMouse.containsMouse ? Theme.surfaceHiTop : Theme.surfaceTop }
                            GradientStop { position: 1.0; color: cardMouse.containsMouse ? Theme.surfaceHi : Theme.surfaceBottom }
                        }
                        border.width: 1
                        border.color: cardMouse.containsMouse ? Theme.alpha(Theme.accent, 0.55) : Theme.divider
                        scale: cardMouse.pressed ? 0.97 : 1.0
                        transform: Translate { y: cardMouse.containsMouse && !cardMouse.pressed ? -3 : 0
                                               Behavior on y { NumberAnimation { duration: Theme.animMed; easing.type: Easing.OutCubic } } }
                        Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutBack } }
                        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: Theme.shadow
                            shadowBlur: cardMouse.containsMouse ? 1.0 : 0.7
                            shadowVerticalOffset: cardMouse.containsMouse ? 8 : 4
                            shadowOpacity: 0.55
                        }

                        Rectangle {
                            x: parent.radius / 2
                            width: parent.width - parent.radius
                            height: 1; y: 1
                            color: Theme.edgeLight
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 10
                            UIcon {
                                name: modelData.iconName
                                size: 40
                                color: cardMouse.containsMouse ? Theme.accent : Theme.textPrimary
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                text: modelData.title
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontL
                                font.weight: Font.DemiBold
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                text: modelData.sub
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontS
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.openFileForEditing(modelData.kind)
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.spacingS }

            UCard {
                width: Math.min(parent.width, 694)
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    Text {
                        text: qsTr("Good to know")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Editing an image never touches the original: the editor saves where your captures go, under a new name, unless you overwrite it yourself. Trimming always writes a new file next to the source.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        visible: !App.capVideoPlayback
                        text: qsTr("Install qt6-qtmultimedia for a video preview while trimming; without it the trim window falls back to a slider-only range picker.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }
        }
    }
}
