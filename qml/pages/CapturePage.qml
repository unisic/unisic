import QtQuick
import QtQuick.Effects
import QtQuick.Window
import Unisic
import Unisic.Kit
import "../components"

// Atelier layout: what to capture on the left, how captures behave in an
// inspector column on the right. The inspector is the one sanctioned exception
// to "one full-width grid per page" (docs/dev/ui-kit.md): it holds settings,
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
    FontMetrics { id: subMetrics; font.pixelSize: 13 }

    // Refreshes the "2 min ago" lines in Recent while the page stays open.
    property date now: new Date()
    Timer {
        interval: 60000
        repeat: true
        running: page.visible && page.Window.window !== null && page.Window.window.visible
        onTriggered: page.now = new Date()
    }

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

    // "Meta+Shift+Print" -> ["Meta", "Shift", "Print"]. A trailing "+" is the
    // plus key itself, not a separator. Only the first binding of a list is
    // shown: it is the one the tile names.
    function keysOf(hotkey) {
        return hotkey === "" ? [] : (hotkey.split(", ")[0].match(/[^+]+|\+$/g) || [])
    }

    function ago(ts) {
        if (!ts || isNaN(ts.getTime()))
            return ""
        var mins = Math.floor((page.now.getTime() - ts.getTime()) / 60000)
        var startOfToday = new Date(page.now.getFullYear(), page.now.getMonth(), page.now.getDate())
        if (mins < 1)
            return qsTr("Just now")
        if (mins < 60)
            //: Age of a capture in Recent, minutes. Short form, no plural.
            return qsTr("%1 min ago").arg(mins)
        if (ts >= startOfToday)
            //: Age of a capture in Recent, hours. Short form, no plural.
            return qsTr("%1 h ago").arg(Math.floor(mins / 60))
        if (ts >= new Date(startOfToday.getTime() - 86400000))
            return qsTr("Yesterday")
        return ts.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    component SectionLabel: Text {
        color: Theme.textSecondary
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.88
    }

    // A text link: accent, underline-free, keyboard and screen-reader reachable.
    component Link: Text {
        id: link
        signal activated()
        color: linkMouse.containsMouse ? Qt.lighter(Theme.accent, 1.12) : Theme.accent
        font.pixelSize: Theme.fontS
        font.weight: Font.Medium
        MouseArea {
            id: linkMouse
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: link.activated()
        }
        activeFocusOnTab: true
        Keys.onSpacePressed: (e) => UKeys.activate(e, link.activated)
        Keys.onReturnPressed: (e) => UKeys.activate(e, link.activated)
        Keys.onEnterPressed: (e) => UKeys.activate(e, link.activated)
        Accessible.role: Accessible.Link
        Accessible.name: text
        Accessible.focusable: true
        Accessible.onPressAction: link.activated()
        UFocusRing { inset: -3 }
    }

    // The inspector's compact switch (36x20). USwitch is the 50x30 one the
    // Settings rows use; this column is narrower and lists more of them.
    component MiniSwitch: Rectangle {
        id: sw
        property bool checked: false
        property string accessibleName: ""
        signal toggled(bool checked)
        function toggle() { if (sw.enabled) sw.toggled(!sw.checked) }
        width: 36
        height: 20
        radius: 10
        color: checked ? Theme.accent : Theme.secondary
        border.width: checked ? 0 : 1
        border.color: Theme.alpha(Theme.accent, 0.24)
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        Rectangle {
            width: sw.checked ? 16 : 14
            height: width
            radius: width / 2
            x: sw.checked ? 18 : 3
            y: (sw.height - height) / 2
            color: sw.checked ? Theme.primary : Theme.textSecondary
            Behavior on x { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -6
            cursorShape: Qt.PointingHandCursor
            onClicked: sw.toggle()
        }
        activeFocusOnTab: enabled
        Keys.onSpacePressed: (e) => UKeys.activate(e, sw.toggle)
        Keys.onReturnPressed: (e) => UKeys.activate(e, sw.toggle)
        Keys.onEnterPressed: (e) => UKeys.activate(e, sw.toggle)
        // CheckBox, as in USwitch: Accessible.Switch needs Qt 6.11.
        Accessible.role: Accessible.CheckBox
        Accessible.name: accessibleName
        Accessible.focusable: activeFocusOnTab
        Accessible.checkable: true
        Accessible.checked: checked
        Accessible.onToggleAction: sw.toggle()
        Accessible.onPressAction: sw.toggle()
        UFocusRing { inset: -3 }
    }

    // ---- card art: 220x132 drawings in plain rectangles (no QtQuick.Shapes,
    // which Debian splits into its own package). Coordinates follow the
    // design's SVG; a stroked shape is grown by half its stroke, since SVG
    // centres a stroke on the outline and a Rectangle border sits inside it.

    component Box: Rectangle {
        property real sx
        property real sy
        property real sw
        property real sh
        x: sx; y: sy; width: sw; height: sh
    }
    component Framed: Rectangle {
        property real sx
        property real sy
        property real sw
        property real sh
        property real stroke: 1.5
        property real rx: 0
        x: sx - stroke / 2; y: sy - stroke / 2
        width: sw + stroke; height: sh + stroke
        radius: rx > 0 ? rx + stroke / 2 : 0
        border.width: stroke
    }

    component FullScreenArt: Item {
        width: 220; height: 132
        Framed { sx: 20; sy: 28; sw: 104; sh: 66; rx: 4; color: Theme.alpha(Theme.accent, 0.16); border.color: Theme.accent }
        Box { sx: 30; sy: 38; sw: 40; sh: 5; radius: 2.5; color: Theme.alpha(Theme.accent, 0.5) }
        Box { sx: 30; sy: 50; sw: 84; sh: 34; radius: 3; color: Theme.tertiary }
        Framed { sx: 134; sy: 38; sw: 68; sh: 48; rx: 4; color: Theme.alpha(Theme.accent, 0.16); border.color: Theme.accent }
        Box { sx: 142; sy: 46; sw: 52; sh: 30; radius: 3; color: Theme.tertiary }
        // The stands: round-capped 2.5 px strokes.
        Box { sx: 70.75; sy: 92.75; sw: 2.5; sh: 14.5; radius: 1.25; color: Theme.tertiary }
        Box { sx: 56.75; sy: 104.75; sw: 30.5; sh: 2.5; radius: 1.25; color: Theme.tertiary }
        Box { sx: 166.75; sy: 84.75; sw: 2.5; sh: 22.5; radius: 1.25; color: Theme.tertiary }
        Box { sx: 154.75; sy: 104.75; sw: 26.5; sh: 2.5; radius: 1.25; color: Theme.tertiary }
    }

    component RegionArt: Item {
        id: regionArt
        width: 220; height: 132
        // 5 on, 4 off around the selection, starting at its top-left corner
        // and running clockwise, cut at the corners like the SVG dash array.
        readonly property var dashes: {
            var out = []
            var edges = [[58, 30, 1, 0, 108], [166, 30, 0, 1, 64], [166, 94, -1, 0, 108], [58, 94, 0, -1, 64]]
            var s0 = 0
            for (var e = 0; e < edges.length; ++e) {
                var ed = edges[e]
                for (var k = Math.floor(s0 / 9); k * 9 < s0 + ed[4]; ++k) {
                    var a = Math.max(k * 9, s0) - s0
                    var b = Math.min(k * 9 + 5, s0 + ed[4]) - s0
                    if (b <= a)
                        continue
                    var x1 = ed[0] + ed[2] * a, y1 = ed[1] + ed[3] * a
                    var x2 = ed[0] + ed[2] * b, y2 = ed[1] + ed[3] * b
                    out.push(ed[3] === 0
                             ? { x: Math.min(x1, x2), y: y1 - 0.75, w: Math.abs(x2 - x1), h: 1.5 }
                             : { x: x1 - 0.75, y: Math.min(y1, y2), w: 1.5, h: Math.abs(y2 - y1) })
                }
                s0 += ed[4]
            }
            return out
        }
        Box { sx: 14; sy: 14; sw: 96; sh: 64; radius: 4; color: Theme.secondary }
        Box { sx: 96; sy: 40; sw: 110; sh: 80; radius: 4; color: Theme.secondary }
        Box { sx: 108; sy: 54; sw: 60; sh: 5; radius: 2.5; color: Theme.tertiary }
        Box { sx: 108; sy: 66; sw: 86; sh: 40; radius: 3; color: Theme.tertiary }
        // The dimmed screen around the selection, as four bands.
        Box { sx: 0; sy: 0; sw: 220; sh: 30; color: Theme.alpha(Theme.backgroundDeep, 0.62) }
        Box { sx: 0; sy: 94; sw: 220; sh: 38; color: Theme.alpha(Theme.backgroundDeep, 0.62) }
        Box { sx: 0; sy: 30; sw: 58; sh: 64; color: Theme.alpha(Theme.backgroundDeep, 0.62) }
        Box { sx: 166; sy: 30; sw: 54; sh: 64; color: Theme.alpha(Theme.backgroundDeep, 0.62) }
        Repeater {
            model: regionArt.dashes
            delegate: Box {
                required property var modelData
                sx: modelData.x; sy: modelData.y; sw: modelData.w; sh: modelData.h
                color: Theme.accent
            }
        }
        Repeater {
            model: [[55, 27], [163, 27], [55, 91], [163, 91]]
            delegate: Framed {
                required property var modelData
                sx: modelData[0]; sy: modelData[1]; sw: 6; sh: 6
                color: Theme.textPrimary
                border.color: Theme.accent
            }
        }
        Box {
            sx: 84; sy: 102; sw: 56; sh: 16; radius: 4
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: "1280×720"
                color: Theme.textOnAccent
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.family: "monospace"
            }
        }
    }

    component WindowArt: Item {
        width: 220; height: 132
        Box { sx: 18; sy: 16; sw: 100; sh: 70; radius: 5; color: Theme.secondary; opacity: 0.55 }
        Box { sx: 128; sy: 62; sw: 76; sh: 56; radius: 5; color: Theme.secondary; opacity: 0.55 }
        Framed { sx: 58; sy: 34; sw: 110; sh: 78; rx: 6; stroke: 2; color: Theme.secondary; border.color: Theme.accent }
        Box { sx: 58; sy: 49.5; sw: 110; sh: 1; color: Theme.alpha(Theme.accent, 0.4) }
        Box { sx: 65.5; sy: 39.5; sw: 5; sh: 5; radius: 2.5; color: Theme.accent }
        Box { sx: 74.5; sy: 39.5; sw: 5; sh: 5; radius: 2.5; color: Theme.alpha(Theme.accent, 0.5) }
        Box { sx: 68; sy: 60; sw: 54; sh: 5; radius: 2.5; color: Theme.tertiary }
        Box { sx: 68; sy: 72; sw: 90; sh: 30; radius: 3; color: Theme.tertiary }
    }

    component ScrollArt: Item {
        width: 220; height: 132
        Box { sx: 70; sy: -10; sw: 80; sh: 160; radius: 4; color: Theme.secondary }
        Repeater {
            model: [[80, 4, 44, 5, 2.5], [80, 16, 60, 22, 3], [80, 46, 52, 5, 2.5],
                    [80, 58, 60, 22, 3], [80, 88, 40, 5, 2.5], [80, 100, 60, 22, 3]]
            delegate: Box {
                required property var modelData
                sx: modelData[0]; sy: modelData[1]; sw: modelData[2]; sh: modelData[3]
                radius: modelData[4]
                color: Theme.tertiary
            }
        }
        Framed { sx: 64; sy: 40; sw: 92; sh: 50; rx: 3; color: Theme.alpha(Theme.accent, 0.14); border.color: Theme.accent }
        // The scroll arrow: a shaft and a chevron, round-capped 1.75 px strokes.
        Box { sx: 175.125; sy: 35.125; sw: 1.75; sh: 59.75; radius: 0.875; color: Theme.accent }
        Repeater {
            model: [[173, 53.13], [179, -53.13]]
            delegate: Box {
                required property var modelData
                sx: modelData[0] - 5.875; sy: 90 - 0.875; sw: 11.75; sh: 1.75
                radius: 0.875
                rotation: modelData[1]
                color: Theme.accent
            }
        }
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
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: qsTr("Capture")
                        color: Theme.textPrimary
                        font.pixelSize: 26
                        font.weight: Font.DemiBold
                        font.letterSpacing: -0.52
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
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

                        // Hover feedback is color-only (surface, border): nothing
                        // on the page shifts under the pointer.
                        delegate: Rectangle {
                            id: modeTile
                            required property var modelData
                            // One entry point for pointer, keyboard and assistive
                            // tech, so the three can never drift apart.
                            function activate() {
                                if (modelData.action === 0) App.captureFullScreen()
                                else if (modelData.action === 1) App.captureRegion()
                                else if (modelData.action === 2) App.captureWindow()
                                else App.captureScroll()
                            }
                            width: modeFlow.cardW
                            height: tileCol.height + 10 + 14
                            radius: 12
                            color: cardMouse.containsMouse ? Theme.surfaceTop : Theme.surface
                            border.width: 1
                            border.color: Theme.alpha(Theme.accent, cardMouse.containsMouse ? 0.32 : 0.10)
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                            Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                            MouseArea {
                                id: cardMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: modeTile.activate()
                            }

                            // Every label inside is folded into the tile's own
                            // Accessible.name below, so each Text opts out and the
                            // button announces itself exactly once.
                            Column {
                                id: tileCol
                                x: 10
                                y: 10
                                width: parent.width - 20
                                spacing: page.compact ? 10 : 14

                                Rectangle {
                                    width: parent.width
                                    height: page.compact ? Math.min(132, Math.round(width * 132 / 220)) : 132
                                    radius: 8
                                    color: Theme.background
                                    clip: true
                                    Loader {
                                        sourceComponent: modeTile.modelData.art
                                        readonly property real k: Math.min(parent.width / 220, parent.height / 132, 1)
                                        scale: k
                                        transformOrigin: Item.TopLeft
                                        x: (parent.width - 220 * k) / 2
                                        y: (parent.height - 132 * k) / 2
                                    }
                                }
                                Column {
                                    width: parent.width
                                    leftPadding: 4
                                    spacing: 2
                                    Text {
                                        width: parent.width - 8
                                        elide: Text.ElideRight
                                        text: modeTile.modelData.title
                                        color: Theme.textPrimary
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        Accessible.ignored: true
                                    }
                                    Text {
                                        width: parent.width - 8
                                        // Two lines held open in a narrow tile,
                                        // so the four tiles in the row line up.
                                        height: page.compact ? Math.ceil(2 * subMetrics.lineSpacing) : implicitHeight
                                        wrapMode: page.compact ? Text.Wrap : Text.NoWrap
                                        maximumLineCount: page.compact ? 2 : 1
                                        elide: Text.ElideRight
                                        text: modeTile.modelData.sub
                                        color: Theme.textSecondary
                                        font.pixelSize: 13
                                        Accessible.ignored: true
                                    }
                                }
                                // Fixed height whether or not a hotkey is bound,
                                // so the tiles in a row always line up.
                                Item {
                                    width: parent.width
                                    height: 22
                                    Row {
                                        x: 4
                                        spacing: 4
                                        Repeater {
                                            model: page.keysOf(modeTile.modelData.hotkey)
                                            delegate: Kbd {
                                                required property string modelData
                                                text: modelData
                                            }
                                        }
                                    }
                                    Row {
                                        x: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: modeTile.modelData.hotkey === ""
                                        spacing: 4
                                        Text {
                                            text: qsTr("No hotkey yet,")
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontS
                                            Accessible.ignored: true
                                        }
                                        Link {
                                            text: qsTr("set one")
                                            Accessible.description: qsTr("Open Settings at Hotkeys")
                                            onActivated: page.openSettings(7)
                                        }
                                    }
                                }
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
                            UFocusRing { inset: -4 }
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

                        delegate: Rectangle {
                            id: moreTile
                            required property var modelData
                            function activate() {
                                if (modelData.action === 0) App.captureRegionOcr()
                                else if (modelData.action === 1) App.captureMeasure()
                                else if (modelData.action === 2) App.startGifRegion()
                                else App.startVideoRegion()
                            }
                            width: moreGrid.cellW
                            height: 56
                            radius: 10
                            color: moreMouse.containsMouse ? Theme.surfaceTop : Theme.surface
                            border.width: 1
                            border.color: Theme.alpha(Theme.accent, moreMouse.containsMouse ? 0.32 : 0.10)
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                            Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                            Rectangle {
                                id: moreIcon
                                x: page.compact ? 8 : 10
                                anchors.verticalCenter: parent.verticalCenter
                                width: page.compact ? 28 : 34; height: width
                                radius: 8
                                color: Theme.secondary
                                UIcon {
                                    anchors.centerIn: parent
                                    name: moreTile.modelData.iconName
                                    size: 18
                                    color: Theme.accent
                                }
                            }
                            Column {
                                anchors.left: moreIcon.right
                                anchors.leftMargin: page.compact ? 8 : 12
                                anchors.right: moreKey.visible ? moreKey.left : parent.right
                                anchors.rightMargin: moreKey.visible ? 12 : 10
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    id: moreTitle
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: moreTile.modelData.title
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    Accessible.ignored: true
                                }
                                Text {
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: moreTile.modelData.sub
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontS
                                    Accessible.ignored: true
                                }
                            }
                            Kbd {
                                id: moreKey
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                // The key gives way before the title would be cut;
                                // the shortcut stays in the accessible name.
                                visible: moreTile.modelData.hotkey !== ""
                                         && moreTile.width - moreIcon.x - moreIcon.width - (page.compact ? 8 : 12) - 32 - width
                                            >= moreTitle.implicitWidth
                                text: page.keysOf(moreTile.modelData.hotkey).join(" ")
                            }

                            MouseArea {
                                id: moreMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: moreTile.activate()
                            }

                            activeFocusOnTab: true
                            Keys.onSpacePressed: (e) => UKeys.activate(e, moreTile.activate)
                            Keys.onReturnPressed: (e) => UKeys.activate(e, moreTile.activate)
                            Keys.onEnterPressed: (e) => UKeys.activate(e, moreTile.activate)

                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.hotkey !== ""
                                             ? qsTr("%1, %2, shortcut %3").arg(modelData.title)
                                                   .arg(modelData.sub).arg(modelData.hotkey.split(", ")[0])
                                             : qsTr("%1, %2").arg(modelData.title).arg(modelData.sub)
                            Accessible.focusable: moreTile.activeFocusOnTab
                            Accessible.onPressAction: moreTile.activate()
                            UFocusRing { inset: -4 }
                        }
                    }
                }
            }

            Column {
                id: recentSection
                width: parent.width
                spacing: 12

                Item {
                    id: recentHead
                    width: parent.width
                    height: Math.max(recentLabel.height, historyLink.height)

                    SectionLabel {
                        id: recentLabel
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Recent")
                    }
                    Link {
                        id: historyLink
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Open History")
                        onActivated: page.openPage(3)
                    }
                }

                // The newest captures, newest first (HistoryStore prepends), in
                // as many whole rows as the page has room for below everything
                // else, never so many that the page scrolls. Not interactive and
                // no cache buffer, so the view builds those rows and part of the
                // next however long the history is.
                Item {
                    width: parent.width
                    height: recentList.rows * (recentList.baseH + recentList.spacing) - recentList.spacing

                    GridView {
                        id: recentList
                        objectName: "captureRecentGrid"
                        // Wider by one gap: a cell is a tile plus the gap after
                        // it, so the last tile ends on the column's edge.
                        width: parent.width + spacing
                        height: parent.height
                        interactive: false
                        cacheBuffer: 0
                        model: App.history
                        readonly property int spacing: 14
                        // Narrower tiles, up to six to a row, rather than one
                        // row cut by the bottom of the window.
                        readonly property int fitCellW: Math.floor((room - 8 - 40) * 16 / 10)
                        readonly property int perRow: Math.max(1, Math.floor(width / (150 + spacing)),
                            fitCellW > 80 ? Math.min(6, Math.ceil(width / (fitCellW + spacing))) : 0)
                        readonly property int cellW: Math.floor(width / perRow) - spacing
                        // A 16:10 thumbnail and the two lines under it.
                        readonly property int thumbH: Math.round(cellW * 10 / 16)

                        // One mask for every thumbnail (they share a size): an
                        // Image cannot round its own corners.
                        Rectangle {
                            id: thumbMask
                            width: recentList.cellW - 2
                            height: recentList.thumbH - 2
                            radius: 7
                            visible: false
                            layer.enabled: true
                        }
                        readonly property int baseH: thumbH + 8 + 40
                        readonly property real room: pageFlick.height - 2 * col.y - recentSection.y
                                                     - recentHead.height - recentSection.spacing
                        readonly property int fitRows: Math.max(1, Math.floor((room + spacing) / (baseH + spacing)))
                        readonly property int rows: Math.max(1, Math.min(fitRows, Math.ceil(count / perRow)))
                        cellWidth: width / perRow
                        cellHeight: baseH + spacing
                        Accessible.role: Accessible.List
                        Accessible.name: qsTr("Recent captures")

                        delegate: Item {
                            id: recentTile
                            required property int index
                            required property string filePath
                            required property string thumbnail
                            required property string url
                            required property string kind
                            required property var timestamp
                            required property string dimensions
                            required property string fileSizeText
                            readonly property string fileName: filePath !== "" ? filePath.split("/").pop()
                                                             : url !== "" ? url : qsTr("(not saved)")
                            readonly property string meta: {
                                var parts = [page.ago(timestamp), dimensions !== "" ? dimensions : fileSizeText]
                                return parts.filter(function (p) { return p !== "" }).join(" · ")
                            }
                            // One tag at most: the kind for a recording, the
                            // upload for a still.
                            readonly property string badge: kind === "gif" ? "GIF"
                                : kind === "video" ? filePath.split(".").pop().toUpperCase()
                                : url !== "" ? qsTr("Uploaded") : ""
                            // The view also builds part of the row after the
                            // last whole one (GridView looks ahead by up to a
                            // row whatever the cache buffer); it stays hidden
                            // and never loads its thumbnail.
                            readonly property bool shown: index < recentList.perRow * recentList.rows

                            function activate() {
                                if (filePath === "") return
                                if (kind === "image") App.previewFromHistory(filePath)
                                else App.openFile(filePath)
                            }
                            visible: shown
                            width: recentList.cellW
                            height: recentList.baseH
                            enabled: filePath !== ""

                            Rectangle {
                                id: recentThumb
                                width: parent.width
                                height: recentList.thumbH
                                radius: 8
                                color: Theme.background
                                border.width: 1
                                border.color: Theme.alpha(Theme.accent, recentMouse.containsMouse ? 0.45 : 0.12)
                                clip: true
                                Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                                Image {
                                    id: recentImage
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    source: recentTile.shown && recentTile.thumbnail !== ""
                                            ? App.fileDragUri(recentTile.thumbnail) : ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                    sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                                    layer.enabled: status === Image.Ready
                                    layer.effect: MultiEffect {
                                        maskEnabled: true
                                        maskSource: thumbMask
                                        maskThresholdMin: 0.5
                                        maskSpreadAtMin: 1.0
                                    }
                                }
                                UIcon {
                                    anchors.centerIn: parent
                                    visible: recentImage.status !== Image.Ready
                                    name: recentTile.kind === "image" ? "image" : "camera-video"
                                    size: 24
                                    color: Theme.textTertiary
                                }
                                Rectangle {
                                    x: 6; y: 6
                                    visible: recentTile.badge !== ""
                                    width: badgeText.implicitWidth + 12
                                    height: badgeText.implicitHeight + 4
                                    radius: 4
                                    color: Theme.alpha(Theme.mediaBase, 0.72)
                                    Text {
                                        id: badgeText
                                        anchors.centerIn: parent
                                        text: recentTile.badge
                                        color: Theme.mediaText
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.4
                                        font.capitalization: Font.AllUppercase
                                        Accessible.ignored: true
                                    }
                                }
                            }
                            Column {
                                anchors.top: recentThumb.bottom
                                anchors.topMargin: 8
                                width: parent.width
                                spacing: 1
                                Text {
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: recentTile.fileName
                                    color: Theme.textPrimary
                                    font.pixelSize: 11
                                    font.family: "monospace"
                                    Accessible.ignored: true
                                }
                                Text {
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: recentTile.meta
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontS
                                    Accessible.ignored: true
                                }
                            }

                            MouseArea {
                                id: recentMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: recentTile.activate()
                            }

                            activeFocusOnTab: visible && enabled
                            Keys.onSpacePressed: (e) => UKeys.activate(e, recentTile.activate)
                            Keys.onReturnPressed: (e) => UKeys.activate(e, recentTile.activate)
                            Keys.onEnterPressed: (e) => UKeys.activate(e, recentTile.activate)

                            Accessible.role: Accessible.Button
                            Accessible.name: [recentTile.fileName, recentTile.badge, recentTile.meta]
                                             .filter(function (p) { return p !== "" }).join(", ")
                            Accessible.focusable: recentTile.activeFocusOnTab
                            Accessible.onPressAction: recentTile.activate()
                            UFocusRing { inset: -3 }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: App.history.count === 0
                        text: qsTr("Your captures will show up here.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontM
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------- inspector --

    Rectangle {
        id: aside
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 300
        color: Theme.surface

        Rectangle {
            anchors.left: parent.left
            width: 1
            height: parent.height
            color: Theme.alpha(Theme.accent, 0.10)
        }

        Flickable {
            id: asideFlick
            objectName: "captureAside"
            anchors.fill: parent
            anchors.leftMargin: 1
            contentHeight: asideCol.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Capture options")

            MiddleScroll { flickable: asideFlick }
            WheelBoost { flickable: asideFlick }

            Column {
                id: asideCol
                width: asideFlick.width

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
                        Rectangle {
                            objectName: "captureDelayPresets"
                            width: parent.width
                            height: 28 + 6 + 2
                            radius: 9
                            color: page.inset
                            border.width: 1
                            border.color: Theme.alpha(Theme.accent, 0.10)
                            Accessible.role: Accessible.Grouping
                            Accessible.name: delayLabel.text

                            Row {
                                x: 4; y: 4
                                spacing: 2
                                Repeater {
                                    model: [0, 3000, 5000, 10000]
                                    delegate: Rectangle {
                                        id: seg
                                        required property int modelData
                                        readonly property bool pressed: modelData === 0
                                            ? App.settings.captureDelayMs <= 200
                                            : App.settings.captureDelayMs === modelData
                                        readonly property string label: modelData === 0 ? qsTr("Off")
                                            //: Capture delay preset, %1 is whole seconds.
                                            : qsTr("%1 s").arg(modelData / 1000)
                                        function activate() {
                                            if (modelData > 0)
                                                App.settings.captureDelayMs = modelData
                                            else if (App.settings.captureDelayMs > 200)
                                                App.settings.captureDelayMs = 200
                                        }
                                        width: (parent.parent.width - 8 - 3 * 2) / 4
                                        height: 28
                                        radius: 6
                                        color: pressed ? Theme.secondary
                                             : segMouse.containsMouse ? Theme.surfaceTop : "transparent"
                                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                                        Text {
                                            anchors.centerIn: parent
                                            text: seg.label
                                            color: seg.pressed ? Theme.textPrimary : Theme.textSecondary
                                            font.pixelSize: Theme.fontS
                                            font.weight: seg.pressed ? Font.DemiBold : Font.Normal
                                            Accessible.ignored: true
                                        }
                                        MouseArea {
                                            id: segMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: seg.activate()
                                        }
                                        activeFocusOnTab: true
                                        Keys.onSpacePressed: (e) => UKeys.activate(e, seg.activate)
                                        Keys.onReturnPressed: (e) => UKeys.activate(e, seg.activate)
                                        Keys.onEnterPressed: (e) => UKeys.activate(e, seg.activate)
                                        Accessible.role: Accessible.Button
                                        Accessible.name: label
                                        Accessible.checkable: true
                                        Accessible.checked: pressed
                                        Accessible.focusable: true
                                        Accessible.onPressAction: seg.activate()
                                        UFocusRing { inset: -1 }
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        width: parent.width
                        height: Math.max(cursorLabel.height, cursorSwitch.height)
                        opacity: enabled ? 1.0 : 0.5
                        enabled: App.capScreenshotCursor || App.devBuild
                        Text {
                            id: cursorLabel
                            anchors.left: parent.left
                            anchors.right: cursorSwitch.left
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            wrapMode: Text.Wrap
                            text: qsTr("Include mouse cursor")
                            color: Theme.textPrimary
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            Accessible.ignored: true
                        }
                        MiniSwitch {
                            id: cursorSwitch
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            checked: App.settings.includeCursor
                            accessibleName: cursorLabel.text
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
                    height: Math.max(0, asideFlick.height - afterSection.height - captureSection.height
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
        }
    }

    Component { id: fullArt;   FullScreenArt {} }
    Component { id: regionArt; RegionArt {} }
    Component { id: windowArt; WindowArt {} }
    Component { id: scrollArt; ScrollArt {} }
}
