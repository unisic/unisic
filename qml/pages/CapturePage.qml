import QtQuick
import QtQuick.Effects
import Unisic
import Unisic.Kit
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

            // Flow, not Row: at the minimum window width the viewport is
            // narrower than the fixed-width cards, so wrap instead of clipping
            // the last one (the Flickable has no horizontal scroll).
            Flow {
                id: modeFlow
                width: parent.width
                spacing: Theme.spacingL

                // The cards stretch to fill their row, and only counts that
                // DIVIDE the card count are allowed — a row that fits all but
                // one strands that one alone underneath, which reads as a
                // mistake rather than a wrap.
                readonly property int count: 3
                readonly property int minCard: 180
                readonly property int fits: Math.max(1, Math.floor((width + spacing) / (minCard + spacing)))
                readonly property int perRow: {
                    for (var n = Math.min(fits, count); n > 1; --n)
                        if (count % n === 0)
                            return n
                    return 1
                }
                readonly property real cardW: Math.floor((width - (perRow - 1) * spacing) / perRow)

                Repeater {
                    model: [
                        { iconName: "monitor", title: qsTr("Full screen"), sub: qsTr("All monitors"), hotkey: App.settings.hotkeyFullScreen, action: 0 },
                        { iconName: "region",  title: qsTr("Region"), sub: qsTr("Select + annotate live"), hotkey: App.settings.hotkeyRegion, action: 1 },
                        { iconName: "window",  title: qsTr("Window"), sub: qsTr("Active window"), hotkey: App.settings.hotkeyWindow, action: 2 },
                    ]

                    // Hover feedback is color-only (surface, border, icon tint):
                    // the tiles never translate, scale or grow their shadow, so
                    // nothing on the page shifts under the pointer.
                    delegate: Rectangle {
                        width: modeFlow.cardW
                        height: 172
                        radius: Theme.radiusXL
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: cardMouse.containsMouse ? Theme.surfaceHiTop : Theme.surfaceTop }
                            GradientStop { position: 1.0; color: cardMouse.containsMouse ? Theme.surfaceHi : Theme.surfaceBottom }
                        }
                        border.width: 1
                        border.color: cardMouse.containsMouse ? Theme.alpha(Theme.accent, 0.55) : Theme.divider
                        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: Theme.shadow
                            shadowBlur: 0.7
                            shadowVerticalOffset: 4
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

            // Per-option cards on the flat background (the Settings visual
            // language), aligned to the same full-width grid as the tiles.
            Text {
                text: qsTr("After capture")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontL
                font.weight: Font.Bold
            }

            Flow {
                id: toggleFlow
                width: parent.width
                spacing: Theme.spacingM
                readonly property bool twoCol: width >= 640
                readonly property real cellW: twoCol ? (width - Theme.spacingM) / 2 : width

                Repeater {
                    model: [
                        { label: qsTr("Open the editor"), key: "openEditor", cursor: false },
                        { label: qsTr("Copy image to clipboard"), key: "copyToClipboard", cursor: false },
                        { label: qsTr("Save to disk automatically"), key: "autoSave", cursor: false },
                        { label: qsTr("Upload and copy the link"), key: "uploadAfterCapture", cursor: false },
                        { label: qsTr("Include mouse cursor"), key: "includeCursor", cursor: true },
                    ]
                    delegate: USettingRow {
                        width: toggleFlow.cellW
                        label: modelData.label
                        USwitch {
                            checked: App.settings[modelData.key]
                            enabled: !modelData.cursor || App.capScreenshotCursor || App.devBuild
                            onToggled: (c) => App.settings[modelData.key] = c
                        }
                    }
                }
            }
        }
    }
}
