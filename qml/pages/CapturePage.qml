import QtQuick
import QtQuick.Effects
import Unisic
import Unisic.Kit
import "../components"

Item {
    id: page

    // Nothing modal on this page - the flag exists on every page so Main.qml
    // can ask the active one without a per-page special case.
    readonly property bool modalOpen: false

    // Same idiom as the Servers page: reading App.uploads.destinations inside
    // the function makes the binding depend on it, so the list refreshes when a
    // destination is added, renamed or removed.
    function destNames() {
        var names = []
        for (var i = 0; i < App.uploads.destinations.length; ++i)
            names.push(App.uploads.destinations[i].name)
        return names
    }

    Flickable {
        id: pageFlick
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        MiddleScroll { flickable: pageFlick }
        WheelBoost { flickable: pageFlick }

        // spacingM between sections, not spacingL: at 1060x700 this page had
        // only 9 px of slack left under the fold (563 px of content in a 572 px
        // viewport), so one taller font or one wrapped translation started it
        // scrolling. Section gaps 20 -> 12 plus the title/subtitle pulled into
        // one header block buy 35 px back - measured 528 px of content, 44 px
        // of slack. Re-measure before adding a row here.
        Column {
            id: col
            width: parent.width
            spacing: Theme.spacingM

            Column {
                width: parent.width
                spacing: 3

                Text {
                    text: qsTr("Capture")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.Bold
                }
                Text {
                    // Wraps rather than clipping: a longer translation of this
                    // line is wider than the viewport, and the Flickable clips.
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Screenshots land in the editor, where you can annotate, then save, copy or upload.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                }
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
                        id: modeTile
                        // One entry point for pointer, keyboard and assistive
                        // tech, so the three can never drift apart.
                        function activate() {
                            if (modelData.action === 0) App.captureFullScreen()
                            else if (modelData.action === 1) App.captureRegion()
                            else App.captureWindow()
                        }
                        width: modeFlow.cardW
                        // 140, not 172: the option grid below grew a fourth row
                        // (delay / repeat / upload server) and the whole page
                        // must still fit above the fold at 1060x700. EditPage's
                        // tiles carry the same height - they share one grid.
                        height: 140
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

                        // Every label inside is folded into the tile's own
                        // Accessible.name above, so each Text opts out
                        // individually (ignoring the Column would only promote
                        // its children to the tile, not hide them) and the
                        // button announces itself exactly once.
                        Column {
                            anchors.centerIn: parent
                            spacing: 8
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
                                Accessible.ignored: true
                            }
                            Text {
                                text: modelData.sub
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontS
                                anchors.horizontalCenter: parent.horizontalCenter
                                Accessible.ignored: true
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
                                    Accessible.ignored: true
                                }
                            }
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: modeTile.activate()
                        }

                        // UKeys keeps the window's Ctrl+1..6 / Ctrl+V bubbling
                        // past a focused tile.
                        activeFocusOnTab: true
                        Keys.onSpacePressed: (e) => UKeys.activate(e, modeTile.activate)
                        Keys.onReturnPressed: (e) => UKeys.activate(e, modeTile.activate)
                        Keys.onEnterPressed: (e) => UKeys.activate(e, modeTile.activate)

                        Accessible.role: Accessible.Button
                        // The hotkey is part of the identity: it is the only
                        // place the shortcut for this mode is shown.
                        //: Spoken name of a capture tile: title, subtitle, hotkey.
                        Accessible.name: modelData.hotkey !== ""
                                         ? qsTr("%1, %2, shortcut %3").arg(modelData.title)
                                               .arg(modelData.sub).arg(modelData.hotkey.split(", ")[0])
                                         : qsTr("%1, %2").arg(modelData.title).arg(modelData.sub)
                        Accessible.focusable: modeTile.activeFocusOnTab
                        Accessible.onPressAction: modeTile.activate()
                        UFocusRing { inset: 3 }
                    }
                }
            }

            // Per-option cards on the flat background (the Settings visual
            // language), aligned to the same full-width grid as the tiles.
            // ONE header for ONE grid: a second section title would cost
            // another 43px of a page that has to fit above the fold, and the
            // rows read in order anyway (how the shot is taken, then what
            // happens to it).
            Text {
                text: qsTr("Capture options")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontL
                font.weight: Font.Bold
                // Keeps the section break reading as a break now that the
                // column spacing is tighter: 6 px above the header, 12 below.
                topPadding: Theme.spacingS
            }

            Flow {
                id: toggleFlow
                width: parent.width
                spacing: Theme.spacingM
                readonly property bool twoCol: width >= 640
                readonly property real cellW: twoCol ? (width - Theme.spacingM) / 2 : width

                // Exactly 8 cells: two full rows of capture options, then two
                // full rows of after-capture actions. Adding a ninth strands it
                // alone on a fifth row AND overflows the fold - re-measure
                // before you do.
                USettingRow {
                    width: toggleFlow.cellW
                    label: qsTr("Capture delay")
                    // MILLISECONDS, and the same preset list as Settings >
                    // Capture behavior. Deliberately not "seconds": the default
                    // is a 200 ms settle delay, which would render as "0 s" on
                    // a fresh install and let a user pick an apparently
                    // identical "0 s" that actually drops the settle window.
                    UValueCombo {
                        width: 130
                        values: [0, 50, 100, 200, 300, 500, 1000, 2000, 3000, 5000, 10000]
                        from: 0; to: 10000
                        suffix: " ms"
                        value: App.settings.captureDelayMs
                        tooltip: qsTr("Waits this long before taking the capture")
                        onChanged: (v) => App.settings.captureDelayMs = v
                    }
                }

                USettingRow {
                    width: toggleFlow.cellW
                    label: qsTr("Repeat last region")
                    // Present but disabled until a region shot exists (the tray
                    // menu gates the same action the same way): flipping
                    // `visible` here would reflow the whole grid.
                    UButton {
                        text: qsTr("Repeat")
                        iconName: "media-repeat"
                        variant: "tonal"
                        compact: true
                        enabled: App.settings.lastCaptureRegion !== ""
                        onClicked: App.recaptureLastRegion()
                    }
                }

                USettingRow {
                    width: toggleFlow.cellW
                    label: qsTr("Upload server")
                    UComboBox {
                        width: 190
                        model: page.destNames()
                        currentIndex: Math.max(0, page.destNames().indexOf(App.settings.activeDestination))
                        onActivated: (i) => App.settings.activeDestination = model[i]
                    }
                }

                Repeater {
                    model: [
                        { label: qsTr("Include mouse cursor"), key: "includeCursor", cursor: true },
                        { label: qsTr("Open the editor"), key: "openEditor", cursor: false },
                        { label: qsTr("Copy image to clipboard"), key: "copyToClipboard", cursor: false },
                        { label: qsTr("Save to disk automatically"), key: "autoSave", cursor: false },
                        { label: qsTr("Upload and copy the link"), key: "uploadAfterCapture", cursor: false },
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
