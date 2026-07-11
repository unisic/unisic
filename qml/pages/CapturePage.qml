import QtQuick
import QtQuick.Effects
import Unisic
import "../components"

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
                text: qsTr("Capture")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
            }
            Text {
                text: qsTr("Screenshots land in the editor, where you can annotate, then save, copy or upload.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
            }

            Item { width: 1; height: Theme.spacingS }

            // Flow, not Row: at the minimum window width the viewport is
            // narrower than the three fixed-width cards, so wrap instead of
            // clipping the third card (the Flickable has no horizontal scroll).
            Flow {
                width: parent.width
                spacing: Theme.spacingL

                Repeater {
                    model: [
                        { iconName: "monitor", title: qsTr("Full screen"), sub: qsTr("All monitors"), hotkey: App.settings.hotkeyFullScreen, action: 0 },
                        { iconName: "region",  title: qsTr("Region"), sub: qsTr("Select + annotate live"), hotkey: App.settings.hotkeyRegion, action: 1 },
                        { iconName: "window",  title: qsTr("Window"), sub: qsTr("Active window"), hotkey: App.settings.hotkeyWindow, action: 2 },
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
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                visible: modelData.hotkey !== ""
                                width: hotkeyText.implicitWidth + 18
                                height: 22
                                radius: 11
                                color: Theme.primary
                                Text {
                                    id: hotkeyText
                                    anchors.centerIn: parent
                                    text: modelData.hotkey.split(", ")[0]
                                    color: Theme.accent
                                    font.pixelSize: 11
                                    font.family: "monospace"
                                }
                            }
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.action === 0) App.captureFullScreen()
                                else if (modelData.action === 1) App.captureRegion()
                                else App.captureWindow()
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.spacingS }

            UCard {
                width: Math.min(parent.width, 694)
                Column {
                    width: parent.width
                    spacing: Theme.spacingM

                    Text {
                        text: qsTr("After capture")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontL
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: [
                            { label: qsTr("Open the editor"), key: "openEditor" },
                            { label: qsTr("Copy image to clipboard"), key: "copyToClipboard" },
                            { label: qsTr("Save to disk automatically"), key: "autoSave" },
                            { label: qsTr("Upload and copy the link"), key: "uploadAfterCapture" },
                        ]
                        delegate: Item {
                            width: parent.width
                            height: 38
                            Text {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontM
                            }
                            USwitch {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                checked: App.settings[modelData.key]
                                onToggled: (c) => App.settings[modelData.key] = c
                            }
                        }
                    }
                }
            }
        }
    }
}
