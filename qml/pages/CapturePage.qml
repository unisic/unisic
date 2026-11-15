import QtQuick
import Unisic
import Unisic.Kit
import "../components"

// Atelier layout: what to capture on the left, how captures behave in an
// inspector column on the right. The inspector (components/Inspector.qml) is
// one of the sanctioned exceptions to "one full-width grid per page"
// (docs/dev/ui-kit.md), with the Record and Edit pages': it holds settings,
// not content, and keeps them in view while the tiles change.
Item {
    id: page

    // Nothing modal on this page - the flag exists on every page so Main.qml
    // can ask the active one without a per-page special case.
    readonly property bool modalOpen: false

    // Main.qml owns the page index; the "Open History", "Manage" and "set one"
    // links ask it to switch instead of reaching into it.
    signal openPage(int index)
    signal openSettings(int tab)

    // Fields sunk into a card (key caps, the delay switch, the Repeat and
    // Upload rows) take the window colour. A theme whose window colour equals
    // its card colour (the built-in light one) would lose them, so those
    // fall back to the background.
    readonly property color inset: Qt.colorEqual(Theme.primary, Theme.surface) ? Theme.background : Theme.primary

    // Smaller than the mock's 1440x900 (the default 1060x700 window, or one
    // too narrow for four 200 px tiles, which would stack them two by two):
    // tighter gaps, the four tiles in one row with their art scaled down and
    // the subtitles on two lines, so the tiles, More and the whole right
    // column still fit without scrolling (AGENTS.md, "fit above the fold").
    readonly property bool compact: height < 760 || width - aside.width - 2 * 40 < 4 * 200 + 3 * 14

    // Same idiom as the Servers page: reading App.uploads.destinations inside
    // the function makes the binding depend on it, so the list refreshes when a
    // destination is added, renamed or removed.
    function destNames() {
        var names = []
        for (var i = 0; i < App.uploads.destinations.length; ++i)
            names.push(App.uploads.destinations[i].name)
        return names
    }
    function activeDest() {
        var list = App.uploads.destinations
        for (var i = 0; i < list.length; ++i)
            if (list[i].name === App.settings.activeDestination)
                return list[i]
        return list.length > 0 ? list[0] : null
    }

    // ---------------------------------------------------------------- main --

    Flickable {
        id: pageFlick
        objectName: "capturePageFlick"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: aside.left
        contentHeight: col.height + 2 * col.y
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        MiddleScroll { flickable: pageFlick }
        WheelBoost { flickable: pageFlick }

        // A bigger window gives the room to Recent, in whole rows.
        Column {
            id: col
            objectName: "capturePageColumn"
            x: 40
            y: page.compact ? 20 : 28
            // Clamped: before the first layout the page is narrower than the
            // inspector, and a negative width sends the Recent GridView's
            // contentX off its start for good (it is not interactive, nothing
            // scrolls it back), leaving the row blank.
            width: Math.max(0, pageFlick.width - 2 * 40)
            spacing: page.compact ? 18 : 30

            Item {
                width: parent.width
                height: Math.max(titleCol.height, headerActions.height)

                Column {
                    id: titleCol
                    anchors.left: parent.left
                    anchors.right: headerActions.left
                    anchors.rightMargin: 24
                    anchors.bottom: parent.bottom
                    spacing: 6
                    PageTitle {
                        width: parent.width
                        text: qsTr("Capture")
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Screenshots open in the editor, where you annotate, then save, copy or upload.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                }

                Row {
                    id: headerActions
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    spacing: 8

                    Repeater {
                        model: [
                            { label: qsTr("Open image"), icon: "line-image", kbd: "",
                              desc: "" },
                            { label: qsTr("Paste"), icon: "line-clipboard", kbd: "Ctrl V",
                              desc: qsTr("Open the image on the clipboard in the editor, Ctrl+V") },
                        ]
                        delegate: Rectangle {
                            id: headBtn
                            required property var modelData
                            required property int index
                            function activate() {
                                if (index === 0) App.openFileForEditing("image")
                                else App.pasteFromClipboard()
                            }
                            width: headRow.width + (modelData.kbd !== "" ? 14 + 8 : 28)
                            height: 36
                            radius: 8
                            color: headMouse.containsMouse ? Theme.surfaceTop : Theme.surface
                            border.width: 1
                            border.color: Theme.alpha(Theme.accent, headMouse.containsMouse ? 0.32 : 0.14)
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                            Row {
                                id: headRow
                                x: 14
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: headBtn.modelData.kbd !== "" ? 10 : 8
                                UIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: headBtn.modelData.icon
                                    size: 16
                                    color: Theme.textPrimary
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: headBtn.modelData.label
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    Accessible.ignored: true
                                }
                                Kbd {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: headBtn.modelData.kbd !== ""
                                    text: headBtn.modelData.kbd
                                }
                            }
                            MouseArea {
                                id: headMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: headBtn.activate()
                            }
                            activeFocusOnTab: true
                            Keys.onSpacePressed: (e) => UKeys.activate(e, headBtn.activate)
                            Keys.onReturnPressed: (e) => UKeys.activate(e, headBtn.activate)
                            Keys.onEnterPressed: (e) => UKeys.activate(e, headBtn.activate)
                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.label
                            Accessible.description: modelData.desc
                            Accessible.focusable: true
                            Accessible.onPressAction: headBtn.activate()
                            UFocusRing { inset: -4 }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 12

                SectionLabel { text: qsTr("Screenshot") }

                // Flow, not Row: at the minimum window width the column is
                // narrower than four cards, so wrap instead of clipping the last
                // one (the Flickable has no horizontal scroll).
                Flow {
                    id: modeFlow
                    objectName: "captureModeFlow"
                    width: parent.width
                    spacing: page.compact ? 12 : 14

                    // The cards stretch to fill their row, and only counts that
                    // DIVIDE the card count are allowed: a row that fits all but
                    // one strands that one alone underneath, which reads as a
                    // mistake rather than a wrap.
                    readonly property int count: 4
                    readonly property int minCard: page.compact ? 130 : 200
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
                            { art: fullArt,   title: qsTr("Full screen"), sub: qsTr("Every monitor at once"),             hotkey: App.settings.hotkeyFullScreen, action: 0 },
                            { art: regionArt, title: qsTr("Region"),      sub: qsTr("Select, then annotate live"),        hotkey: App.settings.hotkeyRegion,     action: 1 },
                            { art: windowArt, title: qsTr("Window"),      sub: qsTr("The active window, frame included"), hotkey: App.settings.hotkeyWindow,     action: 2 },
                            { art: scrollArt, title: qsTr("Scrolling"),   sub: qsTr("A long page, stitched as it scrolls"), hotkey: App.settings.hotkeyScroll,   action: 3 },
                        ]

                        delegate: SourceTile {
                            required property var modelData
                            width: modeFlow.cardW
                            compact: page.compact
                            art: modelData.art
                            title: modelData.title
                            sub: modelData.sub
                            hotkey: modelData.hotkey
                            onOpenHotkeys: page.openSettings(7)
                            onActivated: {
                                if (modelData.action === 0) App.captureFullScreen()
                                else if (modelData.action === 1) App.captureRegion()
                                else if (modelData.action === 2) App.captureWindow()
                                else App.captureScroll()
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 12

                SectionLabel { text: qsTr("More ways to capture") }

                Grid {
                    id: moreGrid
                    objectName: "captureMoreGrid"
                    width: parent.width
                    // Same rule as the cards above: their minimum width, and only
                    // a column count that divides the four tiles.
                    columns: width >= 4 * modeFlow.minCard + 3 * columnSpacing ? 4
                           : width >= 2 * modeFlow.minCard + columnSpacing ? 2 : 1
                    columnSpacing: modeFlow.spacing
                    rowSpacing: modeFlow.spacing
                    readonly property real cellW: Math.floor((width - (columns - 1) * columnSpacing) / columns)

                    Repeater {
                        model: [
                            { iconName: "line-text-scan", title: qsTr("Copy text"),    sub: qsTr("OCR from a region"),     hotkey: App.settings.hotkeyOcr,    action: 0 },
                            { iconName: "line-ruler",     title: qsTr("Measure"),      sub: qsTr("Distances and angles"),  hotkey: "",                        action: 1 },
                            { iconName: "line-film",      title: qsTr("Record GIF"),
                              //: %1 is the GIF length limit in seconds.
                              sub: App.settings.gifMaxDurationSec > 0 ? qsTr("Region, up to %1 s").arg(App.settings.gifMaxDurationSec)
                                                                      : qsTr("Region, no time limit"),
                              hotkey: App.settings.hotkeyGif, action: 2 },
                            { iconName: "line-record",    title: qsTr("Record video"),
                              //: %1 is the video container, e.g. MP4.
                              sub: qsTr("Region, %1").arg(App.settings.videoFormat.toUpperCase()),
                              hotkey: App.settings.hotkeyRecord, action: 3 },
                        ]

                        delegate: ActionTile {
                            required property var modelData
                            width: moreGrid.cellW
                            compact: page.compact
                            iconName: modelData.iconName
                            title: modelData.title
                            sub: modelData.sub
                            hotkey: modelData.hotkey
                            onActivated: {
                                if (modelData.action === 0) App.captureRegionOcr()
                                else if (modelData.action === 1) App.captureMeasure()
                                else if (modelData.action === 2) App.startGifRegion()
                                else App.startVideoRegion()
                            }
                        }
                    }
                }
            }

            RecentStrip {
                id: recentSection
                width: parent.width
                model: App.history
                gridName: "captureRecentGrid"
                available: pageFlick.height - 2 * col.y - recentSection.y
                listName: qsTr("Recent captures")
                emptyText: qsTr("Your captures will show up here.")
                onOpenHistory: page.openPage(3)
            }
        }
    }

    // ------------------------------------------------------------- inspector --

    Inspector {
        id: aside
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        flickName: "captureAside"
        accessibleName: qsTr("Capture options")

        // The real order of AppContext::finishCapture: save, copy, start
        // the upload, then open the editor. Keep the list in that order,
        // or the subtitle and the numbers stop being true.
        Column {
            id: afterSection
            x: 20
            width: parent.width - 40
            topPadding: page.compact ? 14 : 22
            bottomPadding: page.compact ? 12 : 18
            spacing: 12

            Column {
                width: parent.width
                spacing: 2
                Text {
                    text: qsTr("After capture")
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Runs top to bottom after every screenshot.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontS
                }
            }

            Item {
                width: parent.width
                height: stepsCol.height

                Rectangle {
                    x: 11
                    y: 22
                    width: 2
                    height: Math.max(0, parent.height - 44)
                    color: Theme.alpha(Theme.accent, 0.16)
                }

                Column {
                    id: stepsCol
                    width: parent.width
                    Repeater {
                        model: [
                            { label: qsTr("Save to disk automatically"), key: "autoSave",
                              sub: App.settings.saveDirectory },
                            { label: qsTr("Copy image to clipboard"), key: "copyToClipboard",
                              sub: qsTr("Ready to paste anywhere") },
                            { label: qsTr("Upload and copy the link"), key: "uploadAfterCapture",
                              //: %1 is the name of the active upload server.
                              sub: App.settings.activeDestination !== "" ? qsTr("To %1").arg(App.settings.activeDestination)
                                                                         : qsTr("No server yet") },
                            { label: qsTr("Open the editor"), key: "openEditor",
                              sub: qsTr("Annotate and share from there") },
                        ]
                        delegate: Item {
                            id: step
                            required property var modelData
                            required property int index
                            readonly property bool on: App.settings[modelData.key]
                            width: stepsCol.width
                            height: Math.max(24, stepText.height) + (page.compact ? 12 : 18)

                            Rectangle {
                                width: 24; height: 24
                                radius: 12
                                anchors.verticalCenter: parent.verticalCenter
                                color: step.on ? Theme.accent : Theme.surface
                                border.width: step.on ? 0 : 1.5
                                border.color: Theme.tertiary
                                Text {
                                    anchors.centerIn: parent
                                    text: step.index + 1
                                    color: step.on ? Theme.textOnAccent : Theme.textSecondary
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    font.family: "monospace"
                                    Accessible.ignored: true
                                }
                            }
                            Column {
                                id: stepText
                                anchors.left: parent.left
                                anchors.leftMargin: 36
                                anchors.right: stepSwitch.left
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    width: parent.width
                                    wrapMode: Text.Wrap
                                    text: step.modelData.label
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    Accessible.ignored: true
                                }
                                Text {
                                    width: parent.width
                                    // The save folder keeps both ends of its path.
                                    elide: step.modelData.key === "autoSave" ? Text.ElideMiddle : Text.ElideRight
                                    text: step.modelData.sub
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontS
                                    Accessible.ignored: true
                                }
                            }
                            MiniSwitch {
                                id: stepSwitch
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                checked: step.on
                                //: Spoken name of an after-capture switch: step number, label.
                                accessibleName: qsTr("Step %1, %2").arg(step.index + 1).arg(step.modelData.label)
                                onToggled: (c) => App.settings[step.modelData.key] = c
                            }
                        }
                    }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.alpha(Theme.accent, 0.10) }

        Column {
            id: captureSection
            x: 20
            width: parent.width - 40
            topPadding: page.compact ? 12 : 18
            bottomPadding: page.compact ? 12 : 18
            spacing: page.compact ? 10 : 14

            SectionLabel { text: qsTr("Capture") }

            // Presets in seconds over the millisecond setting. "Off" is
            // the default 200 ms settle delay (and anything shorter),
            // which is not a countdown; picking it never lowers a
            // shorter value the user set in Settings.
            Column {
                width: parent.width
                spacing: 8
                Text {
                    id: delayLabel
                    text: qsTr("Delay")
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    Accessible.ignored: true
                }
                SegmentedControl {
                    readonly property var presets: [0, 3000, 5000, 10000]
                    objectName: "captureDelayPresets"
                    width: parent.width
                    accessibleName: delayLabel.text
                    labels: presets.map((ms) => ms === 0 ? qsTr("Off")
                        //: Capture delay preset, %1 is whole seconds.
                        : qsTr("%1 s").arg(ms / 1000))
                    currentIndex: App.settings.captureDelayMs <= 200 ? 0
                        : presets.indexOf(App.settings.captureDelayMs)
                    onPicked: (i) => {
                        if (presets[i] > 0)
                            App.settings.captureDelayMs = presets[i]
                        else if (App.settings.captureDelayMs > 200)
                            App.settings.captureDelayMs = 200
                    }
                }
            }

            InspectorRow {
                id: cursorRow
                label: qsTr("Include mouse cursor")
                enabled: App.capScreenshotCursor || App.devBuild
                MiniSwitch {
                    checked: App.settings.includeCursor
                    accessibleName: cursorRow.label
                    onToggled: (c) => App.settings.includeCursor = c
                }
            }

            // Present but disabled until a region shot exists (the tray
            // menu gates the same action the same way).
            Rectangle {
                id: repeatRow
                readonly property string size: {
                    var r = App.settings.lastCaptureRegion.split("|")
                    var g = r.length > 1 ? r[1].split(",") : []
                    return g.length === 4 ? g[2] + "×" + g[3] : ""
                }
                function activate() { if (enabled) App.recaptureLastRegion() }
                width: parent.width
                height: 38
                radius: 8
                enabled: App.settings.lastCaptureRegion !== ""
                opacity: enabled ? 1.0 : 0.5
                color: repeatMouse.containsMouse ? Theme.surfaceTop : page.inset
                border.width: 1
                border.color: Theme.alpha(Theme.accent, 0.12)
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
                UIcon {
                    id: repeatIcon
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: "line-repeat"
                    size: 16
                    color: Theme.accent
                }
                Text {
                    anchors.left: repeatIcon.right
                    anchors.leftMargin: 10
                    anchors.right: repeatSize.left
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: qsTr("Repeat last region")
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    Accessible.ignored: true
                }
                Text {
                    id: repeatSize
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: repeatRow.size
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    font.family: "monospace"
                    Accessible.ignored: true
                }
                MouseArea {
                    id: repeatMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: repeatRow.activate()
                }
                activeFocusOnTab: enabled
                Keys.onSpacePressed: (e) => UKeys.activate(e, repeatRow.activate)
                Keys.onReturnPressed: (e) => UKeys.activate(e, repeatRow.activate)
                Keys.onEnterPressed: (e) => UKeys.activate(e, repeatRow.activate)
                Accessible.role: Accessible.Button
                Accessible.name: repeatRow.size !== "" ? qsTr("%1, %2").arg(qsTr("Repeat last region")).arg(repeatRow.size)
                                                       : qsTr("Repeat last region")
                Accessible.focusable: activeFocusOnTab
                Accessible.onPressAction: repeatRow.activate()
                UFocusRing { inset: -4 }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.alpha(Theme.accent, 0.10) }

        Column {
            id: uploadSection
            x: 20
            width: parent.width - 40
            topPadding: page.compact ? 12 : 18
            bottomPadding: page.compact ? 12 : 18
            spacing: 10

            Item {
                width: parent.width
                height: Math.max(uploadLabel.height, manageLink.height)
                SectionLabel {
                    id: uploadLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Upload to")
                }
                Link {
                    id: manageLink
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Manage")
                    Accessible.description: qsTr("Open the Servers page")
                    onActivated: page.openPage(4)
                }
            }

            // The kit combo for the list and the keyboard; the face is
            // drawn over it (pointer events fall through to the combo).
            UComboBox {
                id: destCombo
                readonly property var dest: page.activeDest()
                width: parent.width
                height: 48
                radius: 8
                border.color: Theme.alpha(Theme.accent, listOpen ? 0.5 : 0.12)
                enabled: model.length > 0
                model: page.destNames()
                currentIndex: Math.max(0, page.destNames().indexOf(App.settings.activeDestination))
                accessibleName: qsTr("Upload server")
                onActivated: (i) => App.settings.activeDestination = model[i]

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: 7
                    color: destHover.hovered ? Theme.surfaceTop : page.inset
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    HoverHandler { id: destHover }
                    Rectangle {
                        id: destTile
                        x: 11
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28; height: 28
                        radius: 7
                        color: Theme.secondary
                        UIcon {
                            anchors.centerIn: parent
                            name: "line-upload"
                            size: 16
                            color: Theme.accent
                        }
                    }
                    Column {
                        anchors.left: destTile.right
                        anchors.leftMargin: 10
                        anchors.right: destChevron.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: destCombo.dest ? destCombo.dest.name : qsTr("No server yet")
                            color: Theme.textPrimary
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            Accessible.ignored: true
                        }
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: !destCombo.dest ? qsTr("Add one on the Servers page")
                                : destCombo.dest.type === "curl" ? qsTr("curl command") : qsTr("HTTP upload")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontS
                            Accessible.ignored: true
                        }
                    }
                    UIcon {
                        id: destChevron
                        anchors.right: parent.right
                        anchors.rightMargin: 11
                        anchors.verticalCenter: parent.verticalCenter
                        name: "line-chevron-down"
                        size: 16
                        color: Theme.textSecondary
                    }
                }
            }
        }

        // Pushes the status line to the bottom of a tall column.
        Item {
            width: parent.width
            height: Math.max(0, aside.flickable.height - afterSection.height - captureSection.height
                                - uploadSection.height - 2 - statusLine.height)
        }

        // Which session and backends the next capture goes through.
        // Read once per page load: a KWin denial mid-session moves the
        // screenshot backend on, and the next visit shows that.
        Rectangle {
            id: statusLine
            objectName: "captureStatusLine"
            readonly property string session: Qt.platform.pluginName === "xcb" ? "X11"
                : Qt.platform.os === "windows" ? "Windows" : "Wayland"
            readonly property string backends: App.screenshotBackend()
                + (App.recordingAvailable
                   ? " · " + (App.capX11Capture ? (Qt.platform.os === "windows" ? "DXGI" : "XShm") : "PipeWire")
                   : "")
            width: parent.width
            height: statusRow.height + 2 * statusRow.y - 1
            color: "transparent"
            Accessible.role: Accessible.StaticText
            Accessible.name: statusTitle.text + ", " + statusSub.text

            Rectangle { width: parent.width; height: 1; color: Theme.alpha(Theme.accent, 0.10) }

            Row {
                id: statusRow
                x: 20
                y: page.compact ? 11 : 17
                spacing: 10
                Rectangle {
                    width: 16; height: 16
                    radius: 8
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.alpha(App.recordingAvailable ? Theme.success : Theme.textSecondary, 0.14)
                    Rectangle {
                        anchors.centerIn: parent
                        width: 8; height: 8
                        radius: 4
                        color: App.recordingAvailable ? Theme.success : Theme.textSecondary
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        id: statusTitle
                        //: Capture page status line, %1 is the session: Wayland, X11 or Windows.
                        text: App.recordingAvailable ? qsTr("Ready on %1").arg(statusLine.session)
                            //: Screenshots work but recording does not; %1 is the session.
                            : qsTr("Screenshots only on %1").arg(statusLine.session)
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontS
                        font.weight: Font.Medium
                        Accessible.ignored: true
                    }
                    Text {
                        id: statusSub
                        width: Math.min(implicitWidth, statusLine.width - 40 - 22)
                        elide: Text.ElideRight
                        text: statusLine.backends
                        color: Theme.textSecondary
                        font.pixelSize: 11
                        font.family: "monospace"
                        Accessible.ignored: true
                    }
                }
            }
        }
    }

    Component { id: fullArt;   TileArt.FullScreenArt {} }
    Component { id: regionArt; TileArt.RegionArt {} }
    Component { id: windowArt; TileArt.WindowArt {} }
    Component { id: scrollArt; TileArt.ScrollArt {} }
}
