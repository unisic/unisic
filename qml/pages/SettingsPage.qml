import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Unisic
import "../components"

Item {
    id: page
    // paneArea is already inset by spacingXL on both sides, so don't subtract it
    // again here (that left the cards mis-centered with a big right-hand gap).
    readonly property int cardWidth: Math.min(paneArea.width, 694)
    property int tab: 0

    // Free colour choice for the recording halo. Declared at the page root, not
    // inside the row: the row is inside a Flickable, and a popup parented there
    // would be clipped by it.
    UColorPopup {
        id: haloColorPopup
        onPicked: (c) => App.settings.cursorHighlightColor = c
    }

    // Release notes for the running version, opened from the "Current version" row.
    UPatchNotes {
        id: settingsPatchNotes
        version: App.appVersion
    }

    // On-demand system/dependency check (the General → Diagnostics button).
    // markSeenOnClose:false — a manual peek must not consume the first-run latch.
    USystemCheck {
        id: settingsSystemCheck
        markSeenOnClose: false
    }

    // ---- settings search ----
    property string searchQuery: ""
    readonly property bool searchActive: searchQuery.length > 0
    property var searchResults: []
    // Set when jumping to a result; highlights matching row labels for a moment.
    property string highlightQuery: ""
    // Debounced: rebuildSearch walks every pane's full item tree and resets the
    // results Repeater — per keystroke that's a lot of churn for characters the
    // user is still typing. Clearing skips the debounce so leaving search (and
    // jumpTo's searchQuery = "") reacts instantly.
    onSearchQueryChanged: {
        if (searchActive) {
            searchDebounce.restart()
        } else {
            searchDebounce.stop()
            rebuildSearch()
        }
    }
    Timer {
        id: searchDebounce
        interval: 160
        onTriggered: page.rebuildSearch()
    }
    Timer {
        id: highlightTimer
        interval: 4000
        onTriggered: page.highlightQuery = ""
    }
    function jumpTo(result) {
        highlightQuery = searchQuery.toLowerCase()
        highlightTimer.restart()
        tab = result.tab
        // Latch the destination pane as a REAL visit: onLoaded won't re-fire
        // (search already built the Loader), so without this the visited pane
        // is torn down on the next tab switch, losing its scroll position.
        for (var i = 0; i < paneArea.children.length; ++i) {
            var ld = paneArea.children[i]
            if (ld && ld.tabIndex === result.tab)
                ld.touched = true
        }
        searchQuery = ""
        // Scroll the row's pane so the highlighted match is actually on
        // screen — panes keep their last scroll position, so without this
        // the 4s label tint can flash entirely below the fold.
        if (result.row) Qt.callLater(function () {
            var fl = result.row.parent
            while (fl && fl.contentY === undefined)
                fl = fl.parent
            if (!fl)
                return
            var y = result.row.mapToItem(fl.contentItem, 0, 0).y
            fl.contentY = Math.max(0, Math.min(y - Theme.spacingXL, fl.contentHeight - fl.height))
        })
    }
    function rebuildSearch() {
        if (!searchActive) { searchResults = []; return }
        var q = searchQuery.toLowerCase()
        var out = []
        for (var i = 0; i < paneArea.children.length; ++i) {
            var ld = paneArea.children[i]
            if (!ld || ld.tabIndex === undefined || !ld.item)
                continue
            collectRows(ld.item, ld.tabIndex, q, out)
        }
        searchResults = out
    }
    // Settings help: "?" badge click. Dialog shows the short summary first
    // (the tooltip text, expanded), then the detailed explanation.
    function showHelp(label, shortText, detail, reason) {
        var t = shortText
        if (detail && detail.length > 0)
            t += "\n\n" + detail
        if (reason && reason.length > 0)
            t += "\n\n⚠ " + qsTr("Greyed out: ") + reason
        helpDialog.title = label
        helpDialog.text = t
        helpDialog.open()
    }
    UConfirmDialog {
        id: helpDialog
        showCancel: false
        confirmText: qsTr("Close")
    }

    function collectRows(obj, tabIdx, q, out) {
        // Skip invisible subtrees (rows/cards whose own gate is off, e.g.
        // "Notification position" with the capture popup disabled) — listing
        // them would produce dead-end jumps. Works because the panes stay
        // effectively visible (transparent + inert) while searching.
        if (!obj || obj.visible === false)
            return
        if (obj.isSettingRow === true && obj.label !== undefined && obj.label.length > 0
            && obj.label.toLowerCase().indexOf(q) >= 0)
            out.push({ label: obj.label, tab: tabIdx, row: obj })
        var kids = obj.children
        for (var i = 0; kids && i < kids.length; ++i)
            collectRows(kids[i], tabIdx, q, out)
    }

    // Local absolute path -> file URL. Per-segment encode: encodeURI leaves
    // '#'/'?' unescaped, so a filename containing them would be parsed as a URL
    // fragment/query and the thumbnail would fail to load.
    function fileUrl(p) {
        return "file://" + p.split("/").map(encodeURIComponent).join("/")
    }
    // Tooltip label from a path: basename without the image extension.
    function iconLabel(p) {
        return p.split("/").pop().replace(/\.(svg|svgz|png|jpe?g|webp|xpm|ico)$/i, "")
    }

    // FPS dropdown options (15/30/45/60); snap a stored value to the nearest.
    readonly property var fpsOpts: [15, 30, 45, 60]
    function nearestFps(v) {
        var best = 0, bd = 1e9
        for (var i = 0; i < fpsOpts.length; ++i) {
            var d = Math.abs(fpsOpts[i] - v)
            if (d < bd) { bd = d; best = i }
        }
        return best
    }
    property var appAudioNodes: []
    // Async: pw-dump runs off the GUI thread and returns via onAudioApplicationNodesReady.
    function refreshAppAudioNodes() { App.requestAudioApplicationNodes() }
    Connections {
        target: App
        function onAudioApplicationNodesReady(nodes) { page.appAudioNodes = nodes }
    }
    // Load once on open so persisted, non-reactive helper models are ready
    // before their panes are visited.
    Component.onCompleted: {
        if (App.perAppAudioAvailable)
            page.refreshAppAudioNodes()
    }
    readonly property var appAudioIds: [""].concat(appAudioNodes.map(function(n) { return n.id }))
    readonly property var appAudioLabels: [qsTr("Off")].concat(appAudioNodes.map(function(n) { return n.label }))
    readonly property var taskDestinationIds: [""].concat(App.uploads.destinations.map(function(d) { return d.name }))
    readonly property var taskDestinationLabels: [qsTr("Use active destination")].concat(App.uploads.destinations.map(function(d) { return d.name }))

    // Categories in the left rail. One category = one thing the user is trying
    // to configure; everything about that thing lives in that one pane
    // (notifications used to be split across three tabs, editor across two).
    // The Developer category (index 8) only exists in a dev build.
    readonly property var tabNames: {
        var t = [qsTr("General"), qsTr("Capture"), qsTr("Recording"), qsTr("Editor"),
                 qsTr("Saving"), qsTr("Notifications"), qsTr("Appearance"), qsTr("Hotkeys")]
        if (App.devBuild)
            t.push(qsTr("Developer"))
        return t
    }
    readonly property var tabIcons: {
        var t = ["configure", "camera-photo", "media-record", "edit",
                 "document-save", "bell", "fill-color", "keyboard"]
        if (App.devBuild)
            t.push("terminal")
        return t
    }

    // Core built-ins (hardcoded in Theme.qml) + themes from <config>/themes —
    // the user's own PLUS the decorative built-ins seeded there as real files
    // (hot-reloaded: the customThemes dependency refreshes the combo as files
    // come and go).
    readonly property var builtinThemeIds: ["system", "unisic", "dark", "light"]
    readonly property var builtinThemeNames: [qsTr("System Theme"), "Unisic", qsTr("Dark"), qsTr("Light")]
    readonly property var themeIds: {
        var a = builtinThemeIds.slice()
        var c = ThemeController.customThemes
        for (var i = 0; i < c.length; ++i) a.push(c[i].id)
        return a
    }
    readonly property var themeNames: {
        var a = builtinThemeNames.slice()
        var c = ThemeController.customThemes
        for (var i = 0; i < c.length; ++i) a.push(c[i].name)
        return a
    }
    readonly property var toolbarPosIds: ["follow", "top-left", "top-center", "top-right",
                                          "middle-left", "middle-center", "middle-right",
                                          "bottom-left", "bottom-center", "bottom-right"]
    readonly property var toolbarPosNames: [qsTr("Follow selection"), qsTr("Top left"), qsTr("Top center"), qsTr("Top right"),
                                            qsTr("Middle left"), qsTr("Middle center"), qsTr("Middle right"),
                                            qsTr("Bottom left"), qsTr("Bottom center"), qsTr("Bottom right")]
    readonly property var popupPosIds: ["top-left", "top-center", "top-right",
                                        "bottom-left", "bottom-center", "bottom-right"]
    readonly property var popupPosNames: [qsTr("Top left"), qsTr("Top center"), qsTr("Top right"),
                                          qsTr("Bottom left"), qsTr("Bottom center"), qsTr("Bottom right")]
    // Watermark gets its OWN list: it supports a true middle-of-image "center"
    // that the notification popup positions (edge-anchored) must not offer.
    readonly property var watermarkPosIds: ["top-left", "top-center", "top-right",
                                            "bottom-left", "bottom-center", "bottom-right", "center"]
    readonly property var watermarkPosNames: [qsTr("Top left"), qsTr("Top center"), qsTr("Top right"),
                                              qsTr("Bottom left"), qsTr("Bottom center"), qsTr("Bottom right"),
                                              qsTr("Center")]

    // The capture-card action buttons (ids, icons, hide/show and reorder) are
    // edited entirely inside UNotifPreview on the Notifications pane now — it
    // owns its own order model and reads/writes notificationActionOrder and
    // hiddenNotifActions directly, so the page keeps no second copy.

    function toolHidden(id) {
        var csv = App.settings.hiddenTools
        return csv ? ("," + csv + ",").indexOf("," + id + ",") >= 0 : false
    }
    function setToolHidden(id, hidden) {
        var csv = App.settings.hiddenTools
        var list = csv ? csv.split(",").filter(function (x) { return x.length > 0 }) : []
        list = list.filter(function (x) { return x !== id })
        if (hidden) list.push(id)
        App.settings.hiddenTools = list.join(",")
    }
    // Per-tool freedesktop icon-name overrides (JSON map in editorToolIcons).
    // Parsed once per setting change — iconOverride() is called from two
    // bindings on every tool row, and each keystroke in an override field used
    // to re-parse the whole JSON map per call (~40 parses per character).
    readonly property var iconOverrideMap: parseIconOverrides(App.settings.editorToolIcons)
    function parseIconOverrides(j) {
        if (!j) return {}
        try { return JSON.parse(j) } catch (e) { return {} }
    }
    function iconOverride(id) {
        return iconOverrideMap[id] || ""
    }
    function setIconOverride(id, name) {
        var j = App.settings.editorToolIcons
        var m = {}
        if (j) { try { m = JSON.parse(j) } catch (e) { m = {} } }
        if (name && name !== "") m[id] = name; else delete m[id]
        App.settings.editorToolIcons = JSON.stringify(m)
    }

    // One setting, drawn as its own bordered surface card (the welcome-screen
    // "ToggleCard" language applied across all of Settings): the label on top,
    // the one-line `help` summary inline underneath it, and the control on the
    // right. A "?" badge appears only when there is MORE to say (`helpDetail`,
    // or the reason an unavailable row is greyed) — clicking it opens the full
    // in-app help dialog. `footer` holds optional full-width content laid out
    // below the label/control row (the hotkey chip editor uses it so a hotkey
    // stays one card).
    component SettingRow: Rectangle {
        id: settingRow
        readonly property bool isSettingRow: true   // marker for the settings search
        property alias label: labelText.text
        property string help: ""
        property string helpDetail: ""
        // Capability gating: unavailable options are greyed out (not hidden) with
        // a one-line reason, so a release build still shows what it can't do.
        property bool available: true
        property string hint: ""
        default property alias control: slot.data
        property alias footer: footerSlot.data
        // The one-line detail shown under the label: the greyed reason when the
        // row is unavailable, otherwise the `help` summary.
        readonly property string detail: (!available && hint !== "") ? hint : help
        readonly property bool hasBadge: helpDetail !== "" || (!available && hint !== "")

        width: parent ? parent.width : 0
        implicitHeight: inner.implicitHeight + 2 * Theme.spacingM
        radius: Theme.radiusM
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
        opacity: available ? 1.0 : 0.5

        Column {
            id: inner
            x: Theme.spacingM
            y: Theme.spacingM
            width: parent.width - 2 * Theme.spacingM
            spacing: footerSlot.childrenRect.height > 0 ? Theme.spacingS : 0

            Item {
                id: head
                width: parent.width
                // Deliberately independent of slot.height: every control is ≤44px
                // and the label column governs the rest, so the head never needs
                // to grow to the control — and reading slot.height here (slot hugs
                // its childrenRect and is vCenter-anchored to this head) would be a
                // size↔position binding loop.
                height: Math.max(44, labelCol.implicitHeight)

                Column {
                    id: labelCol
                    anchors.left: parent.left
                    anchors.right: slot.left
                    anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Item {
                        width: parent.width
                        height: labelText.implicitHeight
                        Text {
                            id: labelText
                            anchors.left: parent.left
                            width: Math.max(0, parent.width - (settingRow.hasBadge ? 24 : 0))
                            elide: Text.ElideRight
                            // Briefly tinted when this row was just jumped to from search.
                            color: page.highlightQuery.length > 0
                                   && text.toLowerCase().indexOf(page.highlightQuery) >= 0
                                   ? Theme.accent : Theme.textPrimary
                            font.pixelSize: Theme.fontM
                        }
                        Rectangle {
                            id: helpBadge
                            visible: settingRow.hasBadge
                            anchors.left: labelText.left
                            anchors.leftMargin: Math.min(labelText.implicitWidth, labelText.width) + 8
                            anchors.verticalCenter: labelText.verticalCenter
                            width: 16; height: 16; radius: 8
                            color: helpMouse.containsMouse ? Theme.accent : "transparent"
                            border.width: 1
                            border.color: helpMouse.containsMouse ? Theme.accent : Theme.textTertiary
                            z: 10
                            Text {
                                anchors.centerIn: parent
                                text: "?"
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                color: helpMouse.containsMouse ? Theme.textOnAccent : Theme.textTertiary
                            }
                            MouseArea {
                                id: helpMouse
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.showHelp(settingRow.label, settingRow.help,
                                                         settingRow.helpDetail,
                                                         settingRow.available ? "" : settingRow.hint)
                            }
                        }
                    }
                    Text {
                        id: detailText
                        visible: settingRow.detail !== ""
                        width: parent.width
                        text: settingRow.detail
                        wrapMode: Text.WordWrap
                        color: (!settingRow.available && settingRow.hint !== "")
                               ? Theme.danger : Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
                Item {
                    id: slot
                    // Only the CONTROL is disabled on an unavailable row — the "?"
                    // badge must stay clickable so the dialog can explain why.
                    enabled: settingRow.available
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: childrenRect.width
                    height: childrenRect.height
                }
            }
            Item {
                id: footerSlot
                width: parent.width
                height: childrenRect.height
                enabled: settingRow.available
            }
        }
    }

    // One hotkey action: the label/help header of a normal setting card, with
    // the full-width multi-binding chip editor as the card's footer — chips read
    // left to right (primary first) and the "+ Add" ghost button is always at
    // the end of the list, which is where the eye expects it.
    component HotkeyRow: SettingRow {
        id: hotkeyRow
        property string shortcuts: ""
        signal changed(string shortcuts)
        footer: UShortcutList {
            width: parent ? parent.width : 0
            enabled: hotkeyRow.available
            opacity: hotkeyRow.available ? 1.0 : 0.45
            shortcuts: hotkeyRow.shortcuts
            onChanged: (t) => hotkeyRow.changed(t)
        }
    }

    component SectionTitle: Text {
        color: Theme.textPrimary
        font.pixelSize: Theme.fontL
        font.weight: Font.Bold
        // A little air between the header and the first card below it (the
        // section Column packs its children tight at spacingS).
        bottomPadding: Theme.spacingXS
    }

    // A scrollable settings pane: give it cards as default content.
    component ScrollPane: Flickable {
        id: fl
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: paneCol.height + Theme.spacingXL
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        default property alias content: paneCol.data
        Column {
            id: paneCol
            width: fl.width
            spacing: Theme.spacingL
        }
        MiddleScroll { flickable: fl }
        WheelBoost { flickable: fl }
    }

    // A settings section: the welcome screen groups per-setting cards under a
    // plain bold header on the flat background rather than boxing a whole
    // section in one big card, so this is a transparent container (no gradient,
    // border or shadow of its own) that just stacks its header + cards. It
    // mirrors UCard's default-content API so a section body — `Column { title;
    // rows }` — drops in unchanged.
    component SettingsGroup: Item {
        default property alias content: groupInner.data
        implicitWidth: groupInner.childrenRect.width
        implicitHeight: groupInner.childrenRect.height
        Item {
            id: groupInner
            anchors.fill: parent
        }
    }

    // One entry in the category rail: icon + label, accent-tinted when active.
    component NavItem: Rectangle {
        id: nav
        property string iconName: ""
        property string label: ""
        property bool active: false
        signal clicked()
        width: parent ? parent.width : 180
        height: 38
        radius: Theme.radiusM
        color: active ? Theme.alpha(Theme.accent, 0.18)
             : navMouse.containsMouse ? Theme.alpha(Theme.accent, 0.10)
             : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 3; height: 18; radius: 1.5
            color: Theme.accent
            visible: nav.active
        }
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            UIcon {
                name: nav.iconName
                size: 18
                color: nav.active ? Theme.accent : Theme.textSecondary
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: nav.label
                color: nav.active ? Theme.textPrimary : Theme.textSecondary
                font.pixelSize: Theme.fontM
                font.weight: nav.active ? Font.DemiBold : Font.Normal
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        MouseArea {
            id: navMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: nav.clicked()
        }
    }

    // Import/Export use the DESKTOP's native file picker (C++ QFileDialog via
    // the platform theme), not a QML FileDialog — the latter fell back to the
    // Basic-styled Qt Quick dialog here, which looked out of place.

    // ---------------- header ----------------
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingXL
        anchors.bottomMargin: 0
        height: 44
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Settings")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
        }
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingS
            UButton { compact: true; variant: "tonal"; iconName: "folder-open"; text: qsTr("Import"); onClicked: App.importSettingsDialog() }
            UButton { compact: true; variant: "tonal"; iconName: "document-save"; text: qsTr("Export"); onClicked: App.exportSettingsDialog() }
        }
    }

    // Persistence warning: the config dir isn't writable, so nothing sticks
    // across launches. Tells the user exactly why + where to fix it.
    Rectangle {
        id: persistWarn
        visible: !App.settings.persistent
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingM
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingXL
        anchors.rightMargin: Theme.spacingXL
        height: visible ? warnText.implicitHeight + 2 * Theme.spacingM : 0
        radius: Theme.radiusM
        color: Theme.alpha(Theme.danger, 0.15)
        border.width: 1
        border.color: Theme.danger
        Text {
            id: warnText
            anchors.left: parent.left; anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Theme.spacingM
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            font.pixelSize: Theme.fontS + 1
            // Remedy points at the REAL config dir (lowercase ~/.config/unisic,
            // also correct under a custom XDG_CONFIG_HOME) — the legacy
            // capital-U path would fix nothing on a case-sensitive filesystem.
            text: qsTr("⚠ Settings can't be saved. Your config file is not writable, so changes reset every launch. Fix its permissions:\n    sudo chown -R $USER %1")
                  .arg(App.settings.configPath().replace(/\/[^\/]+$/, ""))
        }
    }

    // ---------------- category rail ----------------
    // Settings search: type to list matching rows across ALL categories; click
    // a result to jump to its pane (label flashes accent for a moment). Sits
    // on top of the rail so "find a setting" and "browse categories" are the
    // same place.
    UTextField {
        id: searchField
        anchors.top: persistWarn.bottom
        anchors.topMargin: persistWarn.visible ? Theme.spacingM : 0
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingXL
        width: navRail.width
        placeholder: qsTr("Search settings…")
        text: page.searchQuery
        onEdited: (t) => page.searchQuery = t
    }

    Flickable {
        id: navRail
        anchors.top: searchField.bottom
        anchors.topMargin: Theme.spacingM
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingXL
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingL
        width: 180
        clip: true
        contentWidth: width
        contentHeight: navCol.height
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: navCol
            width: parent.width
            spacing: 2
            Repeater {
                model: page.tabNames
                delegate: NavItem {
                    required property int index
                    required property string modelData
                    label: modelData
                    iconName: page.tabIcons[index]
                    active: page.tab === index && !page.searchActive
                    // Picking a category is a navigation intent — leave search.
                    onClicked: { page.searchQuery = ""; page.tab = index }
                }
            }
        }
    }

    // ---------------- panes ----------------
    Item {
        id: paneArea
        anchors.top: searchField.top
        anchors.left: navRail.right
        anchors.leftMargin: Theme.spacingL
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingXL
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingL
        clip: true

        // ===== search results (shown instead of the panes while typing) =====
        Flickable {
            visible: page.searchActive
            // Above the panes, which stay visible-but-transparent during search.
            z: 1
            anchors.fill: parent
            contentWidth: width
            contentHeight: resultsCol.height + Theme.spacingXL
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: resultsCol
                width: parent.width
                spacing: Theme.spacingS
                Text {
                    visible: page.searchResults.length === 0
                    text: qsTr("No settings match “%1”").arg(page.searchQuery)
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontM
                }
                Repeater {
                    model: page.searchResults
                    delegate: Rectangle {
                        required property var modelData
                        width: page.cardWidth
                        height: 46
                        radius: Theme.radiusM
                        color: resMouse.containsMouse ? Theme.surfaceHi : Theme.surface
                        border.width: 1
                        border.color: Theme.divider
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingM
                            anchors.right: tabChip.left
                            anchors.rightMargin: Theme.spacingM
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            elide: Text.ElideRight
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontM
                        }
                        Rectangle {
                            id: tabChip
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingM
                            anchors.verticalCenter: parent.verticalCenter
                            width: chipText.implicitWidth + 16
                            height: 22
                            radius: 11
                            color: Theme.alpha(Theme.accent, 0.18)
                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: page.tabNames[modelData.tab] !== undefined ? page.tabNames[modelData.tab] : ""
                                color: Theme.accent
                                font.pixelSize: Theme.fontS
                            }
                        }
                        MouseArea {
                            id: resMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.jumpTo(modelData)
                        }
                    }
                }
            }
        }

        // ===== General (0): language, app behavior, updates =====
        // Lazily built on first visit, then kept alive (preserves scroll
        // position). Nine always-instantiated panes made opening Settings
        // build hundreds of controls for tabs never shown.
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 0
            // Visible-but-inert while searching: per-row `visible:` gates keep
            // their real values so collectRows can skip hidden rows.
            visible: page.tab === 0 || page.searchActive
            opacity: (page.tab === 0 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 0 && !page.searchActive
            property bool touched: false
            // Search needs every pane instantiated so it can walk the rows.
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("General") }
                    SettingRow {
                        label: qsTr("Language")
                        help: qsTr("Language of the Unisic interface.")
                        helpDetail: qsTr("“System” follows your desktop locale. Changing the language applies immediately to the interface; a few system dialogs may only switch after a restart.")
                        UComboBox {
                            width: 180
                            property var ids: ["system", "en", "pl", "es", "it", "fr"]
                            // Native names on purpose — every user recognises
                            // their own language regardless of the current UI.
                            model: [qsTr("System"), "English", "Polski", "Español", "Italiano", "Français"]
                            currentIndex: Math.max(0, ids.indexOf(App.settings.uiLanguage))
                            onActivated: (i) => App.settings.uiLanguage = ids[i]
                        }
                    }
                    SettingRow {
                        available: App.ocrAvailable
                        hint: App.ocrAvailable ? ""
                              : qsTr("OCR is not built in. Install tesseract and a language pack, then rebuild.")
                        label: qsTr("Detect languages automatically")
                        help: qsTr("Detects the script and recognizes with the matching language pack.")
                        helpDetail: qsTr("No need to know Tesseract language codes. With the OSD data installed (the “osd” Tesseract pack), Unisic detects the script of each capture - Latin, Arabic, Hebrew, Chinese/Japanese/Korean, Devanagari, and so on - and recognizes with just that script's installed packs, which is faster and more accurate than loading them all. Without the OSD pack it falls back to loading every installed pack. Install the packs for the scripts you use.")
                        USwitch { checked: App.settings.ocrAutoLanguage; onToggled: (c) => App.settings.ocrAutoLanguage = c }
                    }
                    SettingRow {
                        available: App.ocrAvailable && !App.settings.ocrAutoLanguage
                        label: qsTr("OCR languages")
                        help: qsTr("Tesseract language spec used when recognizing text.")
                        helpDetail: (App.qrAvailable
                                     ? qsTr("Combine languages with “+”, e.g. “pol+eng”; each needs its Tesseract langpack installed. OCR also scans QR and bar codes: a code found in the region copies its content instead of the surrounding text.")
                                     : qsTr("Combine languages with “+”, e.g. “pol+eng”; each needs its Tesseract langpack installed."))
                        UTextField {
                            width: 150
                            text: App.settings.ocrLanguages
                            placeholder: "pol+eng"
                            onEdited: (t) => App.settings.ocrLanguages = t
                        }
                    }
                    // Built in, but nothing to recognize with yet — the real
                    // "OCR does nothing" trap, distinct from "not built in".
                    Text {
                        visible: App.ocrAvailable && !App.ocrHasLanguages
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("No Tesseract language pack is installed, so OCR can't recognize anything yet. Install one, e.g. “tesseract-langpack-eng”.")
                        color: Theme.danger
                        font.pixelSize: Theme.fontS
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Application") }
                    SettingRow {
                        label: qsTr("Closing the window minimizes to tray")
                        help: qsTr("Close button hides to the tray instead of quitting.")
                        helpDetail: qsTr("The app keeps running in the background: global hotkeys, uploads and recordings stay active. Quit for real from the tray icon's menu.")
                        USwitch { checked: App.settings.minimizeToTrayOnClose; onToggled: (c) => App.settings.minimizeToTrayOnClose = c }
                    }
                    SettingRow {
                        label: qsTr("Start at login (minimized to tray)")
                        help: qsTr("Launches Unisic automatically when you log in.")
                        helpDetail: qsTr("Creates an XDG autostart entry that starts the app hidden in the tray, so hotkeys work right away without a visible window.")
                        USwitch { checked: App.autostartEnabled; onToggled: (c) => App.autostartEnabled = c }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Updates") }
                    SettingRow {
                        label: qsTr("Current version")
                        help: qsTr("The Unisic version you are running. Click it to see what's new.")
                        Text {
                            id: settingsVersion
                            height: 44
                            verticalAlignment: Text.AlignVCenter
                            text: "v" + App.appVersion
                                  + (App.buildNumber === "dev" ? " · dev" : " · build " + App.buildNumber)
                                  + (App.buildDate ? " · " + App.buildDate : "")
                            color: settingsVersionMouse.containsMouse ? Theme.accent : Theme.textSecondary
                            font.pixelSize: Theme.fontM
                            MouseArea {
                                id: settingsVersionMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { settingsPatchNotes.open(); App.markPatchNotesSeen() }
                            }
                        }
                    }
                    SettingRow {
                        available: App.buildNumber !== "dev"
                        hint: qsTr("Automatic checks are disabled in dev builds.")
                        label: qsTr("Automatic updates")
                        help: qsTr("Checks for a new release shortly after startup and once a day, then installs it in the background.")
                        helpDetail: qsTr("Only the latest release version is fetched from the GitHub API - nothing about you or your system is sent. AppImage installs are downloaded and swapped in place automatically; the new version starts on the next launch (or via the tray's Restart entry). Package installs are updated by the system package manager instead.")
                        USwitch {
                            checked: App.settings.autoCheckUpdates
                            onToggled: (c) => App.settings.autoCheckUpdates = c
                        }
                    }
                    SettingRow {
                        available: App.buildNumber !== "dev"
                        label: qsTr("Update channel")
                        help: qsTr("Which releases to offer: stable only, or the newest including pre-releases.")
                        helpDetail: qsTr("Beta fetches the most recent GitHub release even when it is marked a pre-release, so you get new features earlier at the cost of stability.")
                        UComboBox {
                            width: 160
                            model: [qsTr("Stable"), qsTr("Beta")]
                            readonly property var ids: ["stable", "beta"]
                            currentIndex: Math.max(0, ids.indexOf(App.settings.updateChannel))
                            onActivated: (i) => App.settings.updateChannel = ids[i]
                        }
                    }
                    SettingRow {
                        label: qsTr("Check now")
                        help: qsTr("Ask GitHub for the latest release immediately.")
                        UButton {
                            compact: true
                            variant: "tonal"
                            text: App.updater.busy && !App.updater.downloading
                                  ? qsTr("Checking…") : qsTr("Check now")
                            enabled: !App.updater.busy
                            onClicked: App.updater.checkNow()
                        }
                    }
                    Text {
                        visible: App.updater.statusText !== ""
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: App.updater.statusText
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    Column {
                        visible: App.updater.updateAvailable
                        width: parent.width
                        spacing: Theme.spacingS
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: qsTr("Version %1 is available").arg(App.updater.latestVersion)
                            color: Theme.accent
                            font.pixelSize: Theme.fontM
                            font.weight: Font.DemiBold
                        }
                        Row {
                            spacing: Theme.spacingM
                            UButton {
                                // AppImage in a writable location: the download starts by
                                // itself on discovery — this button is the manual retry.
                                visible: App.updater.canSelfUpdate && !App.updater.restartPending
                                text: App.updater.downloading
                                      ? qsTr("Downloading… %1%").arg(Math.round(App.updater.downloadProgress * 100))
                                      : qsTr("Update now")
                                enabled: !App.updater.busy
                                onClicked: App.updater.downloadAndInstall()
                            }
                            UButton {
                                visible: App.updater.restartPending
                                text: qsTr("Restart now")
                                onClicked: App.updater.restartNow()
                            }
                        }
                        Text {
                            // No self-update path here — the package manager (or, for a
                            // read-only AppImage, its owner) does the swap.
                            visible: !App.updater.canSelfUpdate
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: App.buildNumber === "dev"
                                  ? qsTr("Self-update is disabled in dev builds.")
                                  : App.updater.installKind === "appimage"
                                    ? qsTr("The AppImage location is read-only - it can't update itself from here.")
                                    : qsTr("This install updates natively through your package manager (the package set up its repository).")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontS
                        }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Diagnostics") }
                    SettingRow {
                        label: qsTr("System check")
                        help: qsTr("See which optional tools (FFmpeg, wl-clipboard, OCR packs) are installed.")
                        helpDetail: qsTr("Unisic runs on the built-in Wayland APIs alone; these external tools are optional and unlock recording, the most reliable clipboard copy, and text recognition. The check lists what is present and how to install the rest.")
                        UButton {
                            compact: true
                            variant: "tonal"
                            text: qsTr("Run system check")
                            onClicked: settingsSystemCheck.open()
                        }
                    }
                    SettingRow {
                        label: qsTr("Welcome screen")
                        help: qsTr("Reopen the short setup card shown on the first launch.")
                        helpDetail: qsTr("Lists the shortcuts that are bound, where captures are saved and what happens after each one, and lets you change the theme, language and after-capture actions. Opening it from here never changes anything on its own - leave it with Skip and nothing is touched.")
                        UButton {
                            compact: true
                            variant: "tonal"
                            text: qsTr("Show welcome screen")
                            onClicked: App.showWelcome()
                        }
                    }
                    SettingRow {
                        label: qsTr("Diagnostics")
                        help: qsTr("Copy a text summary of your setup for a bug report.")
                        helpDetail: qsTr("Copies your Unisic and Qt versions, desktop and session, compiled-in features and detected tools to the clipboard. Nothing is sent anywhere - you paste it into an issue yourself.")
                        UButton {
                            compact: true
                            variant: "tonal"
                            iconName: "edit-copy"
                            text: qsTr("Copy diagnostics")
                            onClicked: { App.copyText(App.systemDiagnostics()); App.showToast(qsTr("Diagnostics copied")) }
                        }
                    }
                }
            }

        }
        }

        // ===== Capture (1): taking the shot — behavior, the selection
        // overlay, and what happens right after (the pipeline). =====
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 1
            visible: page.tab === 1 || page.searchActive
            opacity: (page.tab === 1 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 1 && !page.searchActive
            property bool touched: false
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Capture behavior") }
                    SettingRow {
                        label: qsTr("Capture delay")
                        help: qsTr("Waits this long before taking the capture.")
                        helpDetail: qsTr("Gives you time to open menus or tooltips that would close when the capture UI appears. Applies to every capture mode, including hotkeys.")
                        UValueCombo {
                            width: 130
                            values: [0, 50, 100, 200, 300, 500, 1000, 2000, 3000, 5000, 10000]
                            from: 0; to: 10000
                            suffix: " ms"
                            value: App.settings.captureDelayMs
                            onChanged: (v) => App.settings.captureDelayMs = v
                        }
                    }
                    SettingRow {
                        label: qsTr("Include mouse cursor")
                        help: App.capScreenshotCursor ? qsTr("Draws the mouse pointer into the capture.") : qsTr("The plain Screenshot portal cannot include the cursor on this desktop.")
                        helpDetail: qsTr("KWin ScreenShot2, grim and recording ScreenCast streams support cursor embedding. The plain portal Screenshot API does not expose a cursor mode.")
                        USwitch { checked: App.settings.includeCursor; enabled: App.capScreenshotCursor || App.devBuild; onToggled: (c) => App.settings.includeCursor = c }
                    }
                    SettingRow {
                        label: qsTr("Do not disturb while capturing")
                        help: App.capDoNotDisturb
                              ? qsTr("Temporarily pauses desktop notifications during captures and recordings.")
                              : qsTr("Available on KDE Plasma; this desktop does not expose the compatible notification inhibitor.")
                        helpDetail: qsTr("The previous notification state is restored as soon as capture stops, fails, or is cancelled. Encoding and uploads do not keep notifications paused.")
                        USwitch {
                            checked: App.settings.doNotDisturbWhileCapturing
                            enabled: App.capDoNotDisturb || App.devBuild
                            onToggled: (c) => App.settings.doNotDisturbWhileCapturing = c
                        }
                    }
                    SettingRow {
                        label: qsTr("Capture on release")
                        help: qsTr("Takes the screenshot the moment you release the selection.")
                        helpDetail: qsTr("Region screenshots only: releasing the mouse button after drawing the selection captures immediately - no Enter, double-click or toolbar button. This skips the on-overlay annotation stage (you can still annotate afterwards in the editor). Picking a GIF recording region is unaffected and keeps its Start button.")
                        USwitch { checked: App.settings.captureOnRelease; onToggled: (c) => App.settings.captureOnRelease = c }
                    }
                    SettingRow {
                        label: qsTr("Keep region between captures")
                        help: qsTr("The selection overlay opens with your last region already selected.")
                        helpDetail: qsTr("Region screenshots only: the rectangle of your most recent region capture is pre-selected on its screen, so repeating a shot is just Enter (or a drag to adjust). The rectangle survives an app restart. The tray menu and `unisic --recapture` still repeat it without opening the overlay at all.")
                        USwitch { checked: App.settings.rememberRegion; onToggled: (c) => App.settings.rememberRegion = c }
                    }
                    SettingRow {
                        label: qsTr("Full screen captures")
                        help: qsTr("What the full-screen capture takes: every monitor, or the one under the cursor.")
                        helpDetail: qsTr("“All monitors” grabs the whole workspace stitched together. “Screen under cursor” grabs only the monitor the pointer is on - handy on multi-monitor setups. Applies to the hotkey, the tray entry and `unisic --fullscreen` alike; the tray's dedicated screen-under-cursor entry and `unisic --monitor` always take a single screen.")
                        UComboBox {
                            width: 200
                            model: [qsTr("All monitors"), qsTr("Screen under cursor")]
                            readonly property var ids: ["workspace", "screen"]
                            currentIndex: Math.max(0, ids.indexOf(App.settings.fullscreenScope))
                            onActivated: (i) => App.settings.fullscreenScope = ids[i]
                        }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Capture overlay") }
                    SettingRow {
                        label: qsTr("Measurement copy format")
                        help: qsTr("How the ruler's sizes are written when you press Ctrl+C.")
                        helpDetail: qsTr("The Measure tool copies its measurements as text. Readable: “842 × 317” / “412 px”. Plain: “842x317” / “412”. CSS: “width: 842px; height: 317px”.")
                        UComboBox {
                            width: 200
                            model: [qsTr("Readable (842 × 317)"), qsTr("Plain (842x317)"), qsTr("CSS")]
                            readonly property var ids: ["readable", "plain", "css"]
                            currentIndex: Math.max(0, ids.indexOf(App.settings.measureCopyFormat))
                            onActivated: (i) => App.settings.measureCopyFormat = ids[i]
                        }
                    }
                    SettingRow {
                        label: qsTr("Toolbar position")
                        help: qsTr("Where the annotation toolbar sits on the selection overlay.")
                        helpDetail: qsTr("“Follow selection” keeps it glued to the selected region; the fixed positions pin it to a screen edge, which helps when it keeps covering what you select.")
                        UComboBox {
                            width: 200
                            model: page.toolbarPosNames
                            currentIndex: Math.max(0, page.toolbarPosIds.indexOf(App.settings.overlayToolbarPosition))
                            onActivated: (i) => App.settings.overlayToolbarPosition = page.toolbarPosIds[i]
                        }
                    }
                    SettingRow {
                        label: qsTr("Show alignment guides while selecting")
                        help: qsTr("Crosshair lines from the cursor to the screen edges.")
                        helpDetail: qsTr("Shown while picking a region (screenshots and recordings alike) to help align the selection with on-screen elements. Purely visual and never captured into the image.")
                        USwitch { checked: App.settings.selectionGuides; onToggled: (c) => App.settings.selectionGuides = c }
                    }
                    SettingRow {
                        label: qsTr("Show a pixel loupe while selecting")
                        help: qsTr("A magnifier by the cursor shows the exact pixel you are on.")
                        helpDetail: qsTr("A zoomed pixel grid follows the cursor with the hovered pixel highlighted, plus its position and colour - so a selection edge lands on exactly the pixel you mean. Scroll on the overlay to zoom it in and out; scroll all the way out to hide the loupe. Purely visual and never captured into the image.")
                        USwitch { checked: App.settings.pixelLoupe; onToggled: (c) => App.settings.pixelLoupe = c }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("After capture") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Each enabled action runs immediately when the region is dropped. The editor opens alongside the others without blocking them.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Copy image to clipboard")
                        help: qsTr("Puts every capture on the clipboard automatically.")
                        helpDetail: qsTr("On Wayland the copy is mirrored through wl-copy (when installed), which keeps the clipboard content alive reliably even when no Unisic window has focus.")
                        USwitch { checked: App.settings.copyToClipboard; onToggled: (c) => App.settings.copyToClipboard = c }
                    }
                    SettingRow {
                        label: qsTr("Save to disk automatically")
                        help: qsTr("Saves every capture into your save folder without asking.")
                        helpDetail: qsTr("Files are named from the filename template. When off, a capture exists only in the notification/editor until you save it explicitly.")
                        USwitch { checked: App.settings.autoSave; onToggled: (c) => App.settings.autoSave = c }
                    }
                    SettingRow {
                        label: qsTr("Upload to the active server")
                        help: qsTr("Uploads every capture immediately after taking it.")
                        helpDetail: qsTr("Uses the server selected on the Servers page. The result link can be auto-copied or opened via the options below.")
                        USwitch { checked: App.settings.uploadAfterCapture; onToggled: (c) => App.settings.uploadAfterCapture = c }
                    }
                    SettingRow {
                        label: qsTr("Open the editor")
                        help: qsTr("Opens every capture in the annotation editor.")
                        helpDetail: qsTr("The editor never blocks other after-capture actions: saving, copying and uploading run independently at the same time.")
                        USwitch { checked: App.settings.openEditor; onToggled: (c) => App.settings.openEditor = c }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("After upload") }
                    SettingRow {
                        label: qsTr("Copy link to clipboard")
                        help: qsTr("Copies the upload URL once the upload finishes.")
                        helpDetail: qsTr("Ready to paste anywhere. Combine with auto-upload for a seamless capture-to-link flow.")
                        USwitch { checked: App.settings.afterUploadCopyLink; onToggled: (c) => App.settings.afterUploadCopyLink = c }
                    }
                    SettingRow {
                        label: qsTr("Open link in browser")
                        help: qsTr("Opens the uploaded file's URL in your browser.")
                        helpDetail: qsTr("Runs after every successful upload, using the system default browser.")
                        USwitch { checked: App.settings.afterUploadOpenInBrowser; onToggled: (c) => App.settings.afterUploadOpenInBrowser = c }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("External action") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Run one program after each capture. Use $input for the capture file and $output for an optional result file. Commands are launched directly, without a shell.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Command")
                        help: qsTr("Example: oxipng -o 4 $input --out $output")
                        helpDetail: qsTr("If the program is missing or exits with an error, the other save/copy/upload/editor actions still continue.")
                        UTextField {
                            // Caps at 420 on wide windows but shrinks with the card
                            // so the label + field never overflow the narrow (min
                            // window) card and clip the label off-screen.
                            width: Math.min(420, page.cardWidth - 140)
                            text: App.settings.externalActionCommand
                            placeholder: qsTr("program $input $output")
                            onEdited: (t) => App.settings.externalActionCommand = t
                        }
                    }
                    SettingRow {
                        label: qsTr("Run after capture")
                        USwitch {
                            checked: App.settings.externalActionEnabled
                            enabled: App.settings.externalActionCommand.trim() !== ""
                            onToggled: (c) => App.settings.externalActionEnabled = c
                        }
                    }
                }
            }

        }
        }

        // ===== Recording (2): GIF + video parameters and audio =====
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 2
            visible: page.tab === 2 || page.searchActive
            opacity: (page.tab === 2 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 2 && !page.searchActive
            property bool touched: false
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Recording") }
                    SettingRow {
                        label: qsTr("GIF frame rate")
                        help: qsTr("Frames per second sampled into the GIF.")
                        helpDetail: qsTr("Higher is smoother but grows the file quickly. 10-15 fps is usually plenty for UI demos.")
                        UComboBox { width: 130; model: ["15 FPS", "30 FPS", "45 FPS", "60 FPS"]; readonly property var opts: [15,30,45,60]; currentIndex: page.nearestFps(App.settings.gifFps); onActivated: (i) => App.settings.gifFps = opts[i] }
                    }
                    SettingRow {
                        label: qsTr("GIF max duration")
                        help: qsTr("Auto-stops GIF recording after this many seconds. 0 = unlimited.")
                        helpDetail: qsTr("A safety cap, since GIFs get huge fast. 0 disables the cap and recording runs until you stop it.")
                        UValueCombo {
                            width: 120
                            values: [0, 5, 10, 15, 30, 60, 120, 300, 600]
                            from: 0; to: 600
                            suffix: " s"
                            tooltip: qsTr("0 = unlimited")
                            value: App.settings.gifMaxDurationSec
                            onChanged: (v) => App.settings.gifMaxDurationSec = v
                        }
                    }
                    SettingRow {
                        label: qsTr("GIF quality")
                        help: qsTr("Color fidelity of the generated GIF.")
                        helpDetail: qsTr("Higher quality uses a richer palette (two-pass palettegen) at the cost of file size and conversion time.")
                        UComboBox {
                            width: 180
                            model: [qsTr("Fast / small"), qsTr("Balanced"), qsTr("Best")]
                            currentIndex: App.settings.gifQuality
                            onActivated: (i) => App.settings.gifQuality = i
                        }
                    }
                    SettingRow {
                        label: qsTr("MP4 frame rate")
                        help: qsTr("Frames per second for video recordings.")
                        helpDetail: qsTr("30 fps suits most screen content; 60 fps doubles smoothness and file size.")
                        UComboBox { width: 130; model: ["15 FPS", "30 FPS", "45 FPS", "60 FPS"]; readonly property var opts: [15,30,45,60]; currentIndex: page.nearestFps(App.settings.videoFps); onActivated: (i) => App.settings.videoFps = opts[i] }
                    }
                    SettingRow {
                        label: qsTr("Countdown before recording")
                        help: qsTr("Waits this many seconds before recording starts, showing a 3-2-1 cue.")
                        helpDetail: qsTr("Gives you a moment to switch windows or get ready. 0 starts immediately. Applies to GIF and video, region and full screen.")
                        UValueCombo {
                            width: 120
                            values: [0, 1, 2, 3, 5, 10]
                            from: 0; to: 10
                            suffix: " s"
                            tooltip: qsTr("0 = start immediately")
                            value: App.settings.recordCountdownSec
                            onChanged: (v) => App.settings.recordCountdownSec = v
                        }
                    }
                    SettingRow {
                        label: qsTr("Instant replay length")
                        help: qsTr("Keeps only the most recent encoded seconds while replay is active.")
                        helpDetail: qsTr("The ring uses fixed-count two-second segments in the disk cache, not raw frames in RAM. Saving snapshots completed segments without stopping the ring.")
                        UValueCombo { width: 130; values: [10,15,30,60,120,300,600]; from: 10; to: 600; suffix: " s"; value: App.settings.instantReplaySeconds; onChanged: (v) => App.settings.instantReplaySeconds = v }
                    }
                }
            }

            // Cursor overlay for recordings — its own card, a sibling of
            // Recording / Video acceleration / Audio, not buried among the
            // frame-rate rows.
            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Overlays in recordings") }
                    SettingRow {
                        label: qsTr("Highlight the cursor")
                        help: App.capCursorMetadata
                              ? qsTr("Unisic draws the pointer itself, so it can be styled, enlarged and highlighted.")
                              : qsTr("This desktop's screen-cast portal cannot deliver the cursor separately, which this needs.")
                        helpDetail: qsTr("Needs “Include mouse cursor”. Unisic asks the portal for the cursor as data instead of burnt into the picture, then draws the pointer itself, sharp and with a halo. The pointer is hidden whenever the desktop hides it - a game that hides the cursor stays cursor-less.")
                        USwitch {
                            checked: App.settings.cursorHighlight
                            enabled: (App.capCursorMetadata && App.settings.includeCursor) || App.devBuild
                            onToggled: (c) => App.settings.cursorHighlight = c
                        }
                    }
                    SettingRow {
                        visible: App.settings.cursorHighlight
                        label: qsTr("Halo")
                        help: qsTr("The glow drawn under the pointer. Turn it off to keep only the pointer and clicks.")
                        USwitch {
                            checked: App.settings.cursorHighlightHalo
                            onToggled: (c) => App.settings.cursorHighlightHalo = c
                        }
                    }
                    SettingRow {
                        visible: App.settings.cursorHighlight && App.settings.cursorHighlightHalo
                        label: qsTr("Halo colour")
                        help: qsTr("Colour of the glow drawn under the pointer.")
                        Row {
                            spacing: 6
                            anchors.verticalCenter: parent.verticalCenter
                            Repeater {
                                model: ["#FFD600", "#00E5FF", "#FF4757", "#7CFF6B", "#FFFFFF"]
                                delegate: ColorDot {
                                    required property var modelData
                                    dotColor: modelData
                                    active: Qt.colorEqual(App.settings.cursorHighlightColor, modelData)
                                    onClicked: App.settings.cursorHighlightColor = modelData
                                }
                            }
                            // Any colour, not just the five: the halo has to be
                            // able to contrast with whatever is being recorded.
                            UIconButton {
                                iconName: "color-picker"
                                iconSize: 16
                                width: 30; height: 30
                                tooltip: qsTr("More colors")
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: haloColorPopup.open()
                            }
                        }
                    }
                    SettingRow {
                        visible: App.settings.cursorHighlight
                        label: qsTr("Show a ripple on click")
                        // The reason is computed once per shown row rather than bound:
                        // probing libinput opens a udev context, too heavy for a binding.
                        readonly property string blocked: App.clickCaptureBlockedReason()
                        help: blocked === "" ? qsTr("Draws an expanding ring wherever you click.") : blocked
                        USwitch {
                            checked: App.settings.cursorClickRipple
                            enabled: parent.blocked === "" || App.devBuild
                            onToggled: (c) => App.settings.cursorClickRipple = c
                        }
                    }
                    SettingRow {
                        label: qsTr("Show pressed keys")
                        // Same one-shot probe rule as the ripple row above.
                        readonly property string blocked: App.keystrokeCaptureBlockedReason()
                        help: blocked === "" ? qsTr("Draws a badge with each key press (“Ctrl+Shift+T”) into recordings.") : blocked
                        helpDetail: qsTr("A screenkey-style pill at the bottom of the recording shows shortcuts and typed keys, with held modifiers and a ×N repeat counter. Works in GIF and video recordings. Key labels use the physical (US) key legend.")
                        USwitch {
                            checked: App.settings.recordKeystrokes
                            enabled: parent.blocked === "" || App.devBuild
                            onToggled: (c) => App.settings.recordKeystrokes = c
                        }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Audio") }
                    SettingRow {
                        label: qsTr("Record system audio")
                        help: qsTr("Captures what you hear (system output) into video recordings.")
                        helpDetail: qsTr("Taken from the default output monitor via PipeWire/Pulse. Mixed with the microphone when both are enabled. GIFs have no audio.")
                        USwitch { checked: App.settings.recordSystemAudio; onToggled: (c) => App.settings.recordSystemAudio = c }
                    }
                    SettingRow {
                        label: qsTr("Record microphone")
                        help: qsTr("Captures your microphone into video recordings.")
                        helpDetail: qsTr("Uses the default input device. Mixed with system audio when both are enabled. GIFs have no audio.")
                        USwitch { checked: App.settings.recordMicrophone; onToggled: (c) => App.settings.recordMicrophone = c }
                    }
                    SettingRow {
                        label: qsTr("Application audio only")
                        help: App.perAppAudioAvailable
                              ? qsTr("Capture one selected application's PipeWire audio stream.")
                              : qsTr("Requires the pw-dump and pw-record helpers.")
                        helpDetail: qsTr("Start audio playback in the application, press Refresh, then select it. This can be mixed with the microphone or system audio. A kernel FIFO keeps PCM buffering bounded.")
                        Row {
                            spacing: Theme.spacingS
                            UComboBox {
                                width: 170
                                enabled: App.perAppAudioAvailable
                                model: page.appAudioLabels
                                currentIndex: Math.max(0, page.appAudioIds.indexOf(App.settings.recordAppAudioNode))
                                onActivated: (i) => App.settings.recordAppAudioNode = page.appAudioIds[i]
                            }
                            UButton { compact: true; variant: "tonal"; text: qsTr("Refresh"); enabled: App.perAppAudioAvailable; onClicked: page.refreshAppAudioNodes() }
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Applies to video recordings (MP4/WebM); GIFs have no audio.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Video acceleration") }
                    SettingRow {
                        label: qsTr("MP4 encoder")
                        help: qsTr("Automatic picks a working hardware encoder; it is much faster than software.")
                        helpDetail: qsTr("Automatic uses VAAPI or NVENC when they actually encode on this machine, and falls back to software otherwise - a hardware encoder that is merely listed but broken is skipped. Hardware encoding accelerates MP4 (H.264); WebM uses AV1 on NVIDIA GPUs with an AV1 encoder (RTX 40 series and newer) and software VP9 everywhere else.")
                        UComboBox {
                            width: 240
                            property var ids: ["auto", "software", "vaapi", "nvenc"]
                            model: [qsTr("Automatic (recommended)"),
                                    qsTr("Software (portable)"),
                                    App.vaapiAvailable ? qsTr("VAAPI") : qsTr("VAAPI (unavailable)"),
                                    App.nvencAvailable ? qsTr("NVENC") : qsTr("NVENC (unavailable)")]
                            currentIndex: Math.max(0, ids.indexOf(App.settings.videoEncoder))
                            onActivated: (i) => {
                                if ((i === 2 && !App.vaapiAvailable) || (i === 3 && !App.nvencAvailable))
                                    return
                                App.settings.videoEncoder = ids[i]
                            }
                        }
                    }
                    SettingRow {
                        visible: App.settings.videoFormat === "webm"
                        label: qsTr("WebM is slow to save")
                        help: qsTr("VP9 (WebM) has no hardware encoder here and takes several times longer than MP4. Switch the format to MP4 above for fast, hardware-accelerated saves.")
                    }
                }
            }
        }
        }

        // ===== Editor (3): everything about annotating — defaults, session
        // persistence, background removal, the tool set and its icons. =====
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 3
            visible: page.tab === 3 || page.searchActive
            opacity: (page.tab === 3 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 3 && !page.searchActive
            property bool touched: false
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Editor defaults") }
                    SettingRow {
                        label: qsTr("Stroke color")
                        help: qsTr("Default color for new annotations.")
                        helpDetail: qsTr("Used by pen, shapes, arrows and text until you pick another color in the editor. Recent colors are remembered.")
                        Row {
                            spacing: 6
                            ColorDot { dotColor: "#FF4757"; active: App.settings.editorStrokeColor.toLowerCase() === "#ff4757"; onClicked: App.settings.editorStrokeColor = "#FF4757" }
                            ColorDot { dotColor: "#FFD84D"; active: App.settings.editorStrokeColor.toLowerCase() === "#ffd84d"; onClicked: App.settings.editorStrokeColor = "#FFD84D" }
                            ColorDot { dotColor: "#2ED573"; active: App.settings.editorStrokeColor.toLowerCase() === "#2ed573"; onClicked: App.settings.editorStrokeColor = "#2ED573" }
                            ColorDot { dotColor: "#1E90FF"; active: App.settings.editorStrokeColor.toLowerCase() === "#1e90ff"; onClicked: App.settings.editorStrokeColor = "#1E90FF" }
                            ColorDot { dotColor: "#C8ACD6"; active: App.settings.editorStrokeColor.toLowerCase() === "#c8acd6"; onClicked: App.settings.editorStrokeColor = "#C8ACD6" }
                        }
                    }
                    SettingRow {
                        label: qsTr("Stroke width")
                        help: qsTr("Default line thickness for annotations.")
                        helpDetail: qsTr("Also scales arrow heads and the pixelate block size. Adjustable per-annotation in the editor toolbar.")
                        UValueCombo {
                            width: 110
                            values: [1, 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32, 48, 64]
                            from: 1; to: 64
                            suffix: " px"
                            value: App.settings.editorStrokeWidth
                            onChanged: (v) => App.settings.editorStrokeWidth = v
                        }
                    }
                    SettingRow {
                        label: qsTr("Text size")
                        help: qsTr("Default font size for the text tool.")
                        helpDetail: qsTr("Measured in image pixels, so it stays consistent regardless of display scaling.")
                        UValueCombo {
                            width: 110
                            values: [8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 56, 64, 72, 96, 144]
                            from: 8; to: 144
                            suffix: " px"
                            value: App.settings.editorFontSize
                            onChanged: (v) => App.settings.editorFontSize = v
                        }
                    }
                    SettingRow {
                        label: qsTr("Always start with the default colors")
                        help: qsTr("Color picks made while annotating last for that session only.")
                        helpDetail: qsTr("With this on, changing the stroke, fill, text outline or text background color in the editor or on the capture overlay does not overwrite your saved defaults - the next session starts again from the colors configured in Settings → Editor.")
                        USwitch { checked: App.settings.editorResetColors; onToggled: (c) => App.settings.editorResetColors = c }
                    }
                    SettingRow {
                        label: qsTr("Always start with the default tool options")
                        help: qsTr("Stroke width, text style and fill toggles last for that session only.")
                        helpDetail: qsTr("Covers everything except colors: stroke width, font family and size, bold/italic/underline, the text outline and background toggles, and shape fill. With this on the next session starts again from the defaults configured in Settings → Editor.")
                        USwitch { checked: App.settings.editorResetTools; onToggled: (c) => App.settings.editorResetTools = c }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Editor tools") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Hide tools you don't use; they disappear from the editor and the capture overlay.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    Repeater {
                        model: ToolCatalog.tools.filter(function (t) { return t.hideable })
                        delegate: Item {
                            width: parent.width
                            height: 40
                            Row {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                UIcon { name: modelData.iconName; size: 18; color: Theme.textSecondary; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: modelData.label; color: Theme.textPrimary; font.pixelSize: Theme.fontM; anchors.verticalCenter: parent.verticalCenter }
                            }
                            USwitch {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !page.toolHidden(modelData.id)
                                onToggled: (c) => page.setToolHidden(modelData.id, !c)
                            }
                        }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Editor tool icons") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Choose the icon set for the drawing tools only; the main app icons stay fixed.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Icon style")
                        help: qsTr("Icon set used by the editor toolbars.")
                        helpDetail: qsTr("“System” takes icons from your desktop icon theme (with Breeze as fallback); “custom” uses the bundled monochrome set that follows the app theme.")
                        UComboBox {
                            width: 220
                            model: [qsTr("Custom (bundled)"), qsTr("System (desktop theme)")]
                            currentIndex: App.settings.editorIconStyle === "system" ? 1 : 0
                            onActivated: (i) => App.settings.editorIconStyle = (i === 1 ? "system" : "custom")
                        }
                    }
                    Text {
                        visible: App.settings.editorIconStyle === "system"
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Optional: override individual tools with a freedesktop icon name.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    Repeater {
                        model: App.settings.editorIconStyle === "system"
                               ? ToolCatalog.tools.filter(function (t) { return t.editor || t.overlay })
                               : []
                        delegate: Item {
                            width: parent.width
                            height: 44
                            Row {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                UIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: page.iconOverride(modelData.id) || modelData.iconName
                                    iconStyle: "system"
                                    size: 18
                                    color: Theme.textSecondary
                                }
                                Text { text: modelData.label; color: Theme.textPrimary; font.pixelSize: Theme.fontM; anchors.verticalCenter: parent.verticalCenter }
                            }
                            UTextField {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 200
                                text: page.iconOverride(modelData.id)
                                placeholder: modelData.iconName
                                onEdited: (t) => page.setIconOverride(modelData.id, t.trim())
                            }
                        }
                    }
                }
            }
        }
        }

        // ===== Saving (4): where files land and what they're called =====
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 4
            visible: page.tab === 4 || page.searchActive
            opacity: (page.tab === 4 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 4 && !page.searchActive
            property bool touched: false
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Storage & file naming") }
                    Text {
                        text: qsTr("Screenshots folder")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS
                    }
                    Row {
                        width: parent.width
                        spacing: Theme.spacingM
                        UTextField {
                            width: parent.width - 110 - Theme.spacingM
                            text: App.settings.saveDirectory
                            onEdited: (t) => App.settings.saveDirectory = t
                        }
                        UButton { width: 110; text: qsTr("Open"); variant: "tonal"; onClicked: App.openDirectory("") }
                    }
                    Text {
                        text: qsTr("Recordings folder (GIF and video)")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS
                    }
                    Row {
                        width: parent.width
                        spacing: Theme.spacingM
                        UTextField {
                            width: parent.width - 110 - Theme.spacingM
                            text: App.settings.videoSaveDirectory
                            onEdited: (t) => App.settings.videoSaveDirectory = t
                        }
                        UButton { width: 110; text: qsTr("Open"); variant: "tonal"; onClicked: App.openDirectory(App.settings.videoSaveDirectory) }
                    }
                    SettingRow {
                        label: qsTr("Image format")
                        help: qsTr("File format for saved captures: PNG, JPEG or WebP.")
                        helpDetail: qsTr("PNG is lossless and largest; JPEG and WebP are smaller with adjustable quality. The format also applies to uploads and to the clipboard-encoded image where relevant.")
                        UComboBox {
                            width: 150
                            model: ["png", "jpg", "webp"]
                            currentIndex: Math.max(0, model.indexOf(App.settings.imageFormat))
                            onActivated: (i) => App.settings.imageFormat = model[i]
                        }
                    }
                    SettingRow {
                        label: qsTr("Quality (JPEG/WebP): %1").arg(App.settings.imageQuality)
                        help: qsTr("Compression quality for lossy formats.")
                        helpDetail: qsTr("Higher means better fidelity and larger files. PNG ignores this setting because it is always lossless.")
                        USlider {
                            width: 200
                            from: 10; to: 100
                            value: App.settings.imageQuality
                            onMoved: (v) => App.settings.imageQuality = Math.round(v)
                        }
                    }
                    Column {
                        width: parent.width
                        spacing: 4
                        Text {
                            text: qsTr("Filename template. Available tokens: %date%, %time%, %datetime%, %unix%, %rand%, %i% (counter)")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontS
                        }
                        UTextField {
                            id: templateField
                            width: parent.width
                            text: App.settings.filenameTemplate
                            onEdited: (t) => App.settings.filenameTemplate = t
                        }
                        Text {
                            // filenamePreview() is a plain Q_INVOKABLE with no
                            // dependency tracking — reference the settings it
                            // uses so the preview updates while typing.
                            text: {
                                App.settings.filenameTemplate
                                App.settings.imageFormat
                                App.settings.filenameCounter
                                return qsTr("Preview: %1").arg(App.filenamePreview())
                            }
                            color: Theme.accent
                            font.pixelSize: Theme.fontS
                            font.family: "monospace"
                        }
                    }
                    SettingRow {
                        label: qsTr("Open file after saving")
                        help: qsTr("Opens each capture in your image viewer after saving.")
                        helpDetail: qsTr("Uses the system default application for the file type. Independent from the editor; this only opens the saved file.")
                        USwitch { checked: App.settings.openAfterSave; onToggled: (c) => App.settings.openAfterSave = c }
                    }
                    SettingRow {
                        label: qsTr("Ask where to save")
                        help: qsTr("Prompts for a location for each capture instead of saving straight to the folder.")
                        helpDetail: qsTr("Requires saving to be enabled. Cancelling the dialog skips the save - the capture still lands in history and on the clipboard. The screenshots folder above is the starting location.")
                        USwitch { checked: App.settings.askWhereToSave; onToggled: (c) => App.settings.askWhereToSave = c }
                    }
                    SettingRow {
                        label: qsTr("Date subfolders")
                        help: qsTr("Organises saved screenshots and recordings into per-month subfolders (yyyy-MM).")
                        helpDetail: qsTr("Keeps busy capture folders tidy. The subfolder is created under both the screenshots and the recordings folder above.")
                        USwitch { checked: App.settings.dateSubfolders; onToggled: (c) => App.settings.dateSubfolders = c }
                    }
                    SettingRow {
                        label: qsTr("Strip image metadata")
                        help: qsTr("Removes text, DPI and description metadata from saved files.")
                        helpDetail: qsTr("Captures normally carry no metadata; this guarantees a clean PNG/JPEG/WebP even when the editor or a loaded source added some.")
                        USwitch { checked: App.settings.stripMetadata; onToggled: (c) => App.settings.stripMetadata = c }
                    }
                }
            }
            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Watermark") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Adds a text or logo stamp to the captured image before it is saved, copied, uploaded, shown in history or opened in the editor.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Apply watermark")
                        help: qsTr("Applies the selected stamp to every new screenshot.")
                        helpDetail: qsTr("The stamp is baked into the final capture once, before the independent save, clipboard, upload, history and editor actions. It does not alter recordings or existing files.")
                        USwitch { checked: App.settings.watermarkEnabled; onToggled: (c) => App.settings.watermarkEnabled = c }
                    }
                    SettingRow {
                        available: App.settings.watermarkEnabled
                        label: qsTr("Watermark type")
                        help: qsTr("Use a text label or an image logo.")
                        UComboBox {
                            width: 180
                            model: [qsTr("Text"), qsTr("Logo image")]
                            currentIndex: App.settings.watermarkType === "image" ? 1 : 0
                            onActivated: (i) => App.settings.watermarkType = i === 1 ? "image" : "text"
                        }
                    }
                    SettingRow {
                        available: App.settings.watermarkEnabled && App.settings.watermarkType === "text"
                        label: qsTr("Watermark text")
                        help: qsTr("The label stamped onto the screenshot.")
                        UTextField {
                            width: 260
                            text: App.settings.watermarkText
                            onEdited: (t) => App.settings.watermarkText = t
                        }
                    }
                    SettingRow {
                        available: App.settings.watermarkEnabled && App.settings.watermarkType === "image"
                        label: qsTr("Logo image")
                        help: App.settings.watermarkImagePath.length > 0
                              ? App.settings.watermarkImagePath
                              : qsTr("Choose a PNG, SVG, JPEG or WebP image.")
                        UButton {
                            compact: true
                            variant: "tonal"
                            text: App.settings.watermarkImagePath.length > 0
                                  ? qsTr("Change image…") : qsTr("Choose image…")
                            onClicked: App.pickWatermarkImage()
                        }
                    }
                    SettingRow {
                        available: App.settings.watermarkEnabled
                        label: qsTr("Position")
                        help: qsTr("Corner, centre edge, or middle of the image for the watermark.")
                        UComboBox {
                            width: 180
                            model: page.watermarkPosNames
                            currentIndex: Math.max(0, page.watermarkPosIds.indexOf(App.settings.watermarkPosition))
                            onActivated: (i) => App.settings.watermarkPosition = page.watermarkPosIds[i]
                        }
                    }
                    SettingRow {
                        available: App.settings.watermarkEnabled
                        label: qsTr("Opacity: %1%").arg(App.settings.watermarkOpacity)
                        help: qsTr("How strongly the watermark appears over the capture.")
                        USlider {
                            width: 200
                            from: 10; to: 100
                            value: App.settings.watermarkOpacity
                            onMoved: (v) => App.settings.watermarkOpacity = Math.round(v)
                        }
                    }
                }
            }
        }
        }

        // ===== Notifications (5): all capture feedback in one place — the
        // cards (master switch, style, position, timing) AND the sound cues.
        // The style row lived on Appearance and the rest on Preferences; that
        // split is exactly what made these settings hard to find. =====
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 5
            visible: page.tab === 5 || page.searchActive
            opacity: (page.tab === 5 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 5 && !page.searchActive
            property bool touched: false
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Notifications") }
                    // The live capture card sits at the very top, above the
                    // settings that shape it — the same interactive selector the
                    // welcome screen uses. It shows the real card in the chosen
                    // style and corner; click an action to hide it (click the
                    // faded one to bring it back), drag to reorder. No popup is
                    // thrown at the screen corner.
                    Column {
                        width: parent.width
                        spacing: Theme.spacingS
                        visible: App.settings.showCapturePopup && App.capCustomNotification
                        UNotifPreview {
                            width: parent.width
                            style: App.settings.capturePopupStyle
                            position: App.settings.capturePopupPosition
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: qsTr("Hide the buttons you never press; the rest spread out over the freed room. A button still only appears when the capture can back it - an upload link, OCR support, a saved file.")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontS
                        }
                    }
                    SettingRow {
                        label: qsTr("Show notifications")
                        // No notification backend at all → the switch would do
                        // nothing; grey it out with the reason instead.
                        available: App.capNativeNotification || App.capCustomNotification
                        hint: (App.capNativeNotification || App.capCustomNotification) ? ""
                              : qsTr("No notification support was detected on this desktop: there is no notification server and the compositor has no layer-shell, so Unisic cannot show any notification.")
                        help: qsTr("Master switch for all app notifications.")
                        helpDetail: qsTr("Covers toasts and capture notifications alike. When off, Unisic stays completely silent: captures, uploads and errors produce no visual feedback outside the main window.")
                        USwitch { checked: App.settings.showNotifications; onToggled: (c) => App.settings.showNotifications = c }
                    }
                    SettingRow {
                        label: qsTr("Show app notifications (stylized)")
                        // The stylized card needs layer-shell; without it the
                        // native desktop notification is used regardless.
                        available: App.capCustomNotification
                        hint: App.capCustomNotification ? ""
                              : qsTr("This compositor has no layer-shell support, so Unisic cannot draw its own stylized card. Native desktop notifications are used instead.")
                        help: qsTr("Draws capture notifications as Unisic's own themed card.")
                        helpDetail: qsTr("On: the capture notification is Unisic's stylized always-on-top card (layer-shell), with the position, style and auto-hide options below.\nOff or unsupported: a native desktop notification is shown instead - the capture feedback itself never disappears; use the master switch above to silence everything.")
                        USwitch {
                            checked: App.settings.showCapturePopup
                            enabled: App.settings.showNotifications
                            onToggled: (c) => App.settings.showCapturePopup = c
                        }
                    }
                    SettingRow {
                        visible: App.settings.showCapturePopup
                        available: App.capCustomNotification
                        hint: App.capCustomNotification ? ""
                              : qsTr("The style only applies to the card Unisic draws itself; a native notification is drawn by the system server.")
                        label: qsTr("Notification style")
                        help: qsTr("How the capture card looks, from full card to tiny pill.")
                        helpDetail: qsTr("Casual: the full card with a large thumbnail, title and a row of action buttons.\nCompact: a tighter card with a medium thumbnail, filename and the same actions.\nSmall: one slim row with tiny inline action icons.\nMinimal: a pill with just the filename; clicking it opens the floating preview.\nThumbnail: image-first, the capture fills the card and actions appear on hover.\n\nApplies to the next capture.")
                        UComboBox {
                            id: popupStyleCombo
                            width: 180
                            model: [qsTr("Casual"), qsTr("Compact"), qsTr("Small"), qsTr("Minimal"), qsTr("Thumbnail")]
                            currentIndex: Math.max(0, ["casual", "compact", "small", "minimal", "thumbnail"].indexOf(App.settings.capturePopupStyle))
                            onActivated: (i) => App.settings.capturePopupStyle = ["casual", "compact", "small", "minimal", "thumbnail"][i]
                        }
                    }
                    SettingRow {
                        // Corner applies to any card Unisic positions itself — the
                        // layer-shell card AND the GNOME XWayland helper (which is
                        // handed this corner). Only a native server-drawn
                        // notification ignores it.
                        visible: App.settings.showCapturePopup
                        available: App.capCustomNotification
                        hint: App.capCustomNotification ? ""
                              : qsTr("The system notification server decides the position here, because this compositor has no card for Unisic to place.")
                        label: qsTr("Notification position")
                        help: qsTr("Screen corner where the capture card appears.")
                        helpDetail: qsTr("Applies to the card Unisic draws itself (the layer-shell card or the GNOME XWayland card). Native desktop notifications are placed by the system notification server and ignore this.")
                        UComboBox {
                            id: popupPosCombo
                            width: 180
                            model: page.popupPosNames
                            currentIndex: Math.max(0, page.popupPosIds.indexOf(App.settings.capturePopupPosition))
                            onActivated: (i) => App.settings.capturePopupPosition = page.popupPosIds[i]
                        }
                    }
                    SettingRow {
                        visible: App.settings.showCapturePopup
                        label: qsTr("Distance from the screen edge")
                        help: qsTr("Gap between the capture card and the edge of the screen.")
                        helpDetail: qsTr("Unisic already keeps the card clear of panels that reserve space for themselves. Raise this when a dock or panel still sits in the way - Wayland gives an app no way to see where those are, so this is the manual knob.")
                        UValueCombo {
                            id: popupMarginCombo
                            width: 120
                            values: [0, 8, 16, 24, 48, 64, 96]
                            from: 0; to: 400
                            suffix: " px"
                            value: App.settings.capturePopupMargin
                            onChanged: (v) => App.settings.capturePopupMargin = v
                        }
                    }
                    SettingRow {
                        visible: App.settings.showCapturePopup
                        label: qsTr("Notification auto-hide")
                        help: qsTr("How long the capture card stays on screen. 0 keeps it open.")
                        helpDetail: qsTr("After this many seconds the card disappears on its own. Set 0 to keep it open until you dismiss it manually.")
                        UValueCombo {
                            width: 120
                            values: [0, 3, 5, 8, 10, 15, 30, 60]
                            from: 0; to: 600
                            suffix: " s"
                            tooltip: qsTr("0 = keep open")
                            value: App.settings.capturePopupDurationSec
                            onChanged: (v) => App.settings.capturePopupDurationSec = v
                        }
                    }
                    SettingRow {
                        visible: App.settings.showCapturePopup
                        label: qsTr("Hide it during fullscreen / Do Not Disturb")
                        help: qsTr("Mutes capture cards while a fullscreen app or DND is active.")
                        helpDetail: qsTr("Uses the notification server's inhibition state (fullscreen application, Do Not Disturb, screen sharing). Inhibitors that were already stuck when Unisic started are ignored, so a misbehaving third-party app can't silence your capture feedback forever.")
                        USwitch { checked: App.settings.muteOnFullscreen; onToggled: (c) => App.settings.muteOnFullscreen = c }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Sounds") }
                    SettingRow {
                        label: qsTr("Capture sound")
                        help: qsTr("Plays a short sound when a screenshot is taken.")
                        helpDetail: qsTr("A fullscreen capture has no on-screen feedback, so it can be hard to tell it happened. Pick a bundled cue - Shutter, Click, Beep, Ding or Pop - a custom sound, or Off. Custom sounds are .wav/.ogg files in ~/.config/unisic/sounds (add them there or with the + button). The sound plays through the system audio (pw-play/paplay/aplay).")
                        Row {
                            spacing: Theme.spacingS
                            UComboBox {
                                id: soundCombo
                                width: 160
                                anchors.verticalCenter: parent.verticalCenter
                                property var ids: App.captureSoundIds()
                                function labelFor(sid) {
                                    if (sid === "off") return qsTr("Off")
                                    if (sid === "shutter") return qsTr("Shutter")
                                    if (sid === "click") return qsTr("Click")
                                    if (sid === "beep") return qsTr("Beep")
                                    if (sid === "ding") return qsTr("Ding")
                                    if (sid === "pop") return qsTr("Pop")
                                    if (sid === "chime") return qsTr("Chime")
                                    if (sid === "blip") return qsTr("Blip")
                                    if (sid === "snap") return qsTr("Snap")
                                    if (sid === "knock") return qsTr("Knock")
                                    return sid
                                }
                                model: ids.map(labelFor)
                                currentIndex: Math.max(0, ids.indexOf(App.settings.captureSound))
                                onActivated: (i) => App.settings.captureSound = ids[i]
                            }
                            UIconButton {
                                iconName: "play"; iconSize: 15
                                width: 34; height: 34
                                anchors.verticalCenter: parent.verticalCenter
                                tooltip: qsTr("Preview")
                                enabled: App.settings.captureSound !== "off"
                                onClicked: App.previewCaptureSound()
                            }
                            UIconButton {
                                iconName: "list-add"; iconSize: 15
                                width: 34; height: 34
                                anchors.verticalCenter: parent.verticalCenter
                                tooltip: qsTr("Add custom sound")
                                onClicked: {
                                    var id = App.addCustomSound()
                                    if (id !== "") {
                                        App.settings.captureSound = id
                                        soundCombo.ids = App.captureSoundIds()
                                        recSoundCombo.ids = App.captureSoundIds()
                                    }
                                }
                            }
                        }
                    }
                    SettingRow {
                        label: qsTr("Recording sound")
                        help: qsTr("Plays a short sound when a recording or GIF is finished.")
                        helpDetail: qsTr("Encoding can take a while after you stop a recording, so this cue tells you the file is actually ready. It is separate from the screenshot sound: pick a bundled cue - Shutter, Click, Beep, Ding or Pop - a custom sound, or Off. Custom sounds are .wav/.ogg files in ~/.config/unisic/sounds (shared with the capture sound).")
                        Row {
                            spacing: Theme.spacingS
                            UComboBox {
                                id: recSoundCombo
                                width: 160
                                anchors.verticalCenter: parent.verticalCenter
                                property var ids: App.captureSoundIds()
                                model: ids.map(soundCombo.labelFor)
                                currentIndex: Math.max(0, ids.indexOf(App.settings.recordingSound))
                                onActivated: (i) => App.settings.recordingSound = ids[i]
                            }
                            UIconButton {
                                iconName: "play"; iconSize: 15
                                width: 34; height: 34
                                anchors.verticalCenter: parent.verticalCenter
                                tooltip: qsTr("Preview")
                                enabled: App.settings.recordingSound !== "off"
                                onClicked: App.previewRecordingSound()
                            }
                            UIconButton {
                                iconName: "list-add"; iconSize: 15
                                width: 34; height: 34
                                anchors.verticalCenter: parent.verticalCenter
                                tooltip: qsTr("Add custom sound")
                                onClicked: {
                                    var id = App.addCustomSound()
                                    if (id !== "") {
                                        App.settings.recordingSound = id
                                        soundCombo.ids = App.captureSoundIds()
                                        recSoundCombo.ids = App.captureSoundIds()
                                    }
                                }
                            }
                        }
                    }
                    SettingRow {
                        label: qsTr("Recording start sound")
                        help: qsTr("Plays a short sound the moment recording begins (after the countdown).")
                        helpDetail: qsTr("Separate from the finished-recording cue: this fires when capture actually starts. Pick a bundled cue - Shutter, Click, Beep, Ding or Pop - a custom sound, or Off.")
                        Row {
                            spacing: Theme.spacingS
                            UComboBox {
                                id: recStartSoundCombo
                                width: 160
                                anchors.verticalCenter: parent.verticalCenter
                                property var ids: App.captureSoundIds()
                                model: ids.map(soundCombo.labelFor)
                                currentIndex: Math.max(0, ids.indexOf(App.settings.recordStartSound))
                                onActivated: (i) => App.settings.recordStartSound = ids[i]
                            }
                            UIconButton {
                                iconName: "play"; iconSize: 15
                                width: 34; height: 34
                                anchors.verticalCenter: parent.verticalCenter
                                tooltip: qsTr("Preview")
                                enabled: App.settings.recordStartSound !== "off"
                                onClicked: App.previewRecordStartSound()
                            }
                            UIconButton {
                                iconName: "list-add"; iconSize: 15
                                width: 34; height: 34
                                anchors.verticalCenter: parent.verticalCenter
                                tooltip: qsTr("Add custom sound")
                                onClicked: {
                                    var id = App.addCustomSound()
                                    if (id !== "") {
                                        App.settings.recordStartSound = id
                                        soundCombo.ids = App.captureSoundIds()
                                        recSoundCombo.ids = App.captureSoundIds()
                                        recStartSoundCombo.ids = App.captureSoundIds()
                                    }
                                }
                            }
                        }
                    }
                    SettingRow {
                        label: qsTr("Sound volume: %1 %").arg(App.settings.soundVolume)
                        help: qsTr("Playback volume for the capture and recording sound cues.")
                        helpDetail: qsTr("0 is muted. Applied via the player (pw-play/paplay); aplay has no volume flag and plays at the sample level.")
                        USlider {
                            width: 200
                            from: 0; to: 100
                            value: App.settings.soundVolume
                            onMoved: (v) => App.settings.soundVolume = Math.round(v)
                        }
                    }
                }
            }
        }
        }

        // ===== Appearance (6): theme, window chrome, tray icon =====
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 6
            visible: page.tab === 6 || page.searchActive
            opacity: (page.tab === 6 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 6 && !page.searchActive
            property bool touched: false
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Appearance") }
                    SettingRow {
                        label: qsTr("Theme")
                        help: qsTr("Color theme for the whole app.")
                        helpDetail: qsTr("“System” follows your desktop's light/dark scheme live; the other entries are fixed palettes. Windows, cards and the editor all re-theme instantly.")
                        UComboBox {
                            width: 220
                            model: page.themeNames
                            currentIndex: Math.max(0, page.themeIds.indexOf(ThemeController.themeName))
                            onActivated: (i) => ThemeController.themeName = page.themeIds[i]
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("The System theme follows your desktop's light/dark mode and accent color.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Custom themes")
                        help: qsTr("Drop .json theme files into the themes folder - they appear in the list above and reload live while you edit them.")
                        helpDetail: qsTr("Opening the folder for the first time creates a commented example theme (8 colors are enough; everything else is derived, and any derived color can be overridden). Share the file to share the theme. A broken file is skipped and its reason is listed here.")
                        Row {
                            spacing: Theme.spacingS
                            anchors.verticalCenter: parent.verticalCenter
                            UButton { compact: true; variant: "tonal"; text: qsTr("Open themes folder"); onClicked: ThemeController.openThemesFolder() }
                            UButton { compact: true; variant: "tonal"; text: qsTr("Reload"); onClicked: ThemeController.reloadCustomThemes() }
                        }
                    }
                    Text {
                        width: parent.width
                        visible: ThemeController.customThemeErrors.length > 0
                        wrapMode: Text.WordWrap
                        text: ThemeController.customThemeErrors.join("\n")
                        color: Theme.danger
                        font.pixelSize: Theme.fontS
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Window decoration") }
                    SettingRow {
                        label: qsTr("Use system window decoration")
                        help: qsTr("Uses the desktop's normal title bar and borders.")
                        helpDetail: qsTr("When off, Unisic draws its own frameless chrome. Turn this on if window dragging/snapping misbehaves on your compositor.")
                        USwitch { checked: App.settings.useSystemDecoration; onToggled: (c) => App.settings.useSystemDecoration = c }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Off = Unisic draws its own title bar with themed minimize/maximize/close buttons.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingM
                    SectionTitle { text: qsTr("System tray icon") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Click an icon to use it in the system tray. Drop your own .png/.svg files into the icons folder and they appear here automatically.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }

                    Flow {
                        width: parent.width
                        spacing: Theme.spacingS

                        // Shared by both tile Repeaters below.
                        Component {
                            id: trayTileDelegate
                            Rectangle {
                                id: tile
                                required property var modelData
                                readonly property bool sel: App.settings.trayIconPath === modelData.path
                                width: 62; height: 62
                                radius: Theme.radiusM
                                color: sel ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                                            : Theme.surfaceHi
                                border.width: sel ? 2 : 1
                                border.color: sel ? Theme.accent : Theme.divider
                                Image {
                                    anchors.centerIn: parent
                                    width: 38; height: 38
                                    source: tile.modelData.src
                                    fillMode: Image.PreserveAspectFit
                                    sourceSize.width: 76; sourceSize.height: 76
                                    smooth: true
                                    asynchronous: true
                                }
                                MouseArea {
                                    id: tileMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: App.selectTrayIcon(tile.modelData.path)
                                }
                                // UHoverTip, not Controls' ToolTip: the Basic
                                // style's grey box matches nothing else here.
                                UHoverTip {
                                    anchor: parent
                                    text: tile.modelData.label
                                    show: tileMouse.containsMouse && tile.modelData.path !== ""
                                }
                            }
                        }

                        Repeater {
                            // Rebuilds when the drop-in folder or the OS
                            // light/dark scheme changes. Deliberately does NOT
                            // reference trayIconPath: selection is tracked per
                            // delegate (sel), and reading it here would rebuild
                            // every tile — re-running the trayIconThumb
                            // recolors — on each selection click.
                            model: {
                                // Referenced so the thumbnails re-render when the
                                // OS light/dark scheme flips.
                                var color = App.trayContrastColor
                                var bundled = App.bundledTrayIcons
                                var presets = App.trayIconPresets
                                var arr = [{ path: "", label: qsTr("Default"),
                                             src: "qrc:/resources/icons/unisic.svg" }]
                                // App-shipped presets are monochrome — recolor the
                                // preview to contrast the scheme, like the tray.
                                for (var b = 0; b < bundled.length; b++)
                                    arr.push({ path: bundled[b],
                                               label: page.iconLabel(bundled[b]),
                                               src: App.trayIconThumb(bundled[b], color) })
                                for (var i = 0; i < presets.length; i++)
                                    arr.push({ path: presets[i],
                                               label: page.iconLabel(presets[i]),
                                               src: page.fileUrl(presets[i]) })
                                return arr
                            }
                            delegate: trayTileDelegate
                        }

                        Repeater {
                            // A file picked from elsewhere still gets a tile so
                            // the current choice is always visible/selected.
                            // Its own Repeater: this is the one tile that must
                            // follow trayIconPath, and it is at most one item.
                            model: {
                                var cur = App.settings.trayIconPath
                                if (cur !== "" && App.trayIconPresets.indexOf(cur) === -1
                                        && App.bundledTrayIcons.indexOf(cur) === -1)
                                    return [{ path: cur, label: page.iconLabel(cur),
                                              src: page.fileUrl(cur) }]
                                return []
                            }
                            delegate: trayTileDelegate
                        }

                        // "+" tile — pick an image; it is copied into the icons
                        // folder and selected.
                        Rectangle {
                            width: 62; height: 62
                            radius: Theme.radiusM
                            color: Theme.surfaceHi
                            border.width: 1
                            border.color: Theme.divider
                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                color: Theme.textSecondary
                                font.pixelSize: 30
                            }
                            MouseArea {
                                id: addIconMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.addTrayIcon()
                            }
                            UHoverTip {
                                anchor: parent
                                text: qsTr("Add an icon (copies it here)")
                                show: addIconMouse.containsMouse
                            }
                        }
                    }

                    Row {
                        spacing: Theme.spacingS
                        UButton { compact: true; variant: "tonal"; iconName: "folder-open"; text: qsTr("Open folder with icons"); onClicked: App.openDirectory(App.trayIconsDir()) }
                        UButton {
                            compact: true; variant: "ghost"; text: qsTr("Reset")
                            visible: App.settings.trayIconPath !== ""
                            onClicked: App.clearTrayIcon()
                        }
                    }
                }
            }
        }
        }

        // ===== Hotkeys (7) =====
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 7
            visible: page.tab === 7 || page.searchActive
            opacity: (page.tab === 7 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 7 && !page.searchActive
            property bool touched: false
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {

            SettingsGroup {
                // Also shown on a desktop with no hotkey backend but a writable
                // shortcut store (COSMIC/GNOME/Cinnamon/Xfce): record the keys
                // here, then "Add shortcuts to <desktop>" binds them as commands.
                visible: App.hotkeysAvailable || App.desktopShortcutsAuto
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Global hotkeys") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: !App.hotkeysAvailable
                              ? qsTr("Record the key you want for each action, then use “Add shortcuts to %1” below — Unisic binds each one as a command in %1's keyboard settings.").arg(App.desktopShortcutName)
                              : App.hotkeyBackend === "portal"
                              ? qsTr("Registered through the system GlobalShortcuts portal. Your desktop may show a one-time confirmation dialog; the binding it decides on is final (on Hyprland bind the ids in hyprland.conf).")
                              : qsTr("Registered through KDE global shortcuts (KGlobalAccel). Each action can hold several bindings: record one, then use the small chip to add alternatives (up to 4). Remove a binding with its ×.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    HotkeyRow {
                        label: qsTr("Full screen")
                        help: qsTr("Hotkey: capture all monitors at once.")
                        helpDetail: qsTr("Grabs the entire workspace silently (KWin path) or via the portal elsewhere, then runs the normal after-capture pipeline. The “Full screen captures” preference in Capture can narrow it to the screen under the cursor.")
                        shortcuts: App.settings.hotkeyFullScreen
                        onChanged: (t) => { App.settings.hotkeyFullScreen = t; App.applyHotkey("capture-fullscreen") }
                    }
                    HotkeyRow {
                        label: qsTr("Region")
                        help: qsTr("Hotkey: capture a selected region.")
                        helpDetail: qsTr("Opens the selection overlay with annotation tools, so you can draw on the frozen screen before the capture is finalized.")
                        shortcuts: App.settings.hotkeyRegion
                        onChanged: (t) => { App.settings.hotkeyRegion = t; App.applyHotkey("capture-region") }
                    }
                    HotkeyRow {
                        label: qsTr("Window")
                        help: qsTr("Hotkey: capture a single window.")
                        helpDetail: qsTr("Uses the desktop's window picker where available, so you get exactly one window without manual cropping.")
                        shortcuts: App.settings.hotkeyWindow
                        onChanged: (t) => { App.settings.hotkeyWindow = t; App.applyHotkey("capture-window") }
                    }
                    HotkeyRow {
                        label: qsTr("Video start/stop")
                        help: qsTr("Hotkey: toggle video recording.")
                        helpDetail: qsTr("First press opens the recording setup for a region; pressing again while recording stops and finalizes the file. Ctrl+Esc is the always-on emergency stop.")
                        shortcuts: App.settings.hotkeyRecord
                        onChanged: (t) => { App.settings.hotkeyRecord = t; App.applyHotkey("record-video") }
                    }
                    HotkeyRow {
                        label: qsTr("GIF start/stop")
                        help: qsTr("Hotkey: toggle GIF recording.")
                        helpDetail: qsTr("Same flow as video recording, but the result is converted into an optimized GIF (two-pass palette) when you stop.")
                        shortcuts: App.settings.hotkeyGif
                        onChanged: (t) => { App.settings.hotkeyGif = t; App.applyHotkey("record-gif") }
                    }
                    HotkeyRow {
                        label: qsTr("OCR region (copy text)")
                        available: App.ocrAvailable
                        hint: App.ocrAvailable ? ""
                              : qsTr("OCR is not built in. Install tesseract and a language pack, then rebuild.")
                        help: qsTr("Hotkey: select a region, its text lands in the clipboard.")
                        helpDetail: (App.qrAvailable
                                     ? qsTr("Opens the region selector and runs OCR on the crop. Nothing is saved and no notification is shown; the recognized text is simply copied. QR and bar codes are read too: a code in the region copies its content instead.")
                                     : qsTr("Opens the region selector and runs OCR on the crop. Nothing is saved and no notification is shown; the recognized text is simply copied."))
                        shortcuts: App.settings.hotkeyOcr
                        onChanged: (t) => { App.settings.hotkeyOcr = t; App.applyHotkey("ocr-region") }
                    }
                    HotkeyRow {
                        label: qsTr("Copy last capture")
                        help: qsTr("Hotkey: puts the most recent screenshot back on the clipboard.")
                        helpDetail: qsTr("Copies the last screenshot taken in this session, whenever you press it. A dedicated shortcut never collides with the normal Ctrl+C - this replaces the old 2-second Ctrl+C grab, which could steal an ordinary copy right after a capture.")
                        shortcuts: App.settings.hotkeyCopyLast
                        onChanged: (t) => { App.settings.hotkeyCopyLast = t; App.applyHotkey("copy-last") }
                    }
                    HotkeyRow {
                        label: qsTr("Instant replay")
                        help: qsTr("Starts the rolling replay ring; later presses save the recent segment window.")
                        helpDetail: qsTr("The first press opens the screen-sharing portal and starts the bounded encoded ring. While it is active, each press saves the latest configured duration without stopping capture.")
                        shortcuts: App.settings.hotkeyInstantReplay
                        onChanged: (t) => { App.settings.hotkeyInstantReplay = t; App.applyHotkey("instant-replay") }
                    }
                    UButton {
                        anchors.right: parent.right
                        text: App.hotkeysAvailable ? qsTr("Apply hotkeys")
                                                   : qsTr("Add shortcuts to %1").arg(App.desktopShortcutName)
                        compact: true
                        onClicked: {
                            if (App.hotkeysAvailable) { App.applyHotkeys(); App.showToast(qsTr("Hotkeys re-registered")) }
                            else App.installDesktopShortcuts()
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: !App.hotkeysAvailable
                              ? qsTr("Changing a key here updates the stored shortcut; press “Add shortcuts to %1” to write it into the desktop.").arg(App.desktopShortcutName)
                              : App.hotkeyBackend === "portal"
                              ? qsTr("Keys recorded here are suggestions passed to the portal; the system dialog confirms or adjusts them.")
                              : qsTr("Shortcuts apply immediately and stay in sync with KDE System Settings; an edit made there shows up here too.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }

            SettingsGroup {
                id: taskPresetCard
                visible: App.hotkeysAvailable
                width: page.cardWidth
                property var taskIds: ["default", "copy", "edit", "save", "upload",
                                       "copy-save", "copy-edit", "copy-upload", "save-upload",
                                       "copy-save-upload", "all"]
                property var taskLabels: [qsTr("Use global actions"), qsTr("Copy only"), qsTr("Edit only"), qsTr("Save only"), qsTr("Upload only"),
                                          qsTr("Copy + save"), qsTr("Copy + edit"), qsTr("Copy + upload"), qsTr("Save + upload"),
                                          qsTr("Copy + save + upload"), qsTr("All actions")]
                function usesUpload(id) { return id === "default" || id.indexOf("upload") >= 0 || id === "all" }
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Per-hotkey task presets") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Each screenshot hotkey can run its own action profile without changing the global After capture switches.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Full screen hotkey")
                        Row {
                            spacing: Theme.spacingS
                            UComboBox {
                                width: 150; model: taskPresetCard.taskLabels
                                currentIndex: Math.max(0, taskPresetCard.taskIds.indexOf(App.settings.fullScreenTask))
                                onActivated: (i) => App.settings.fullScreenTask = taskPresetCard.taskIds[i]
                            }
                            UComboBox {
                                width: 150; model: page.taskDestinationLabels
                                enabled: taskPresetCard.usesUpload(App.settings.fullScreenTask)
                                currentIndex: Math.max(0, page.taskDestinationIds.indexOf(App.settings.fullScreenTaskDestination))
                                onActivated: (i) => App.settings.fullScreenTaskDestination = page.taskDestinationIds[i]
                            }
                        }
                    }
                    SettingRow {
                        label: qsTr("Region hotkey")
                        Row {
                            spacing: Theme.spacingS
                            UComboBox {
                                width: 150; model: taskPresetCard.taskLabels
                                currentIndex: Math.max(0, taskPresetCard.taskIds.indexOf(App.settings.regionTask))
                                onActivated: (i) => App.settings.regionTask = taskPresetCard.taskIds[i]
                            }
                            UComboBox {
                                width: 150; model: page.taskDestinationLabels
                                enabled: taskPresetCard.usesUpload(App.settings.regionTask)
                                currentIndex: Math.max(0, page.taskDestinationIds.indexOf(App.settings.regionTaskDestination))
                                onActivated: (i) => App.settings.regionTaskDestination = page.taskDestinationIds[i]
                            }
                        }
                    }
                    SettingRow {
                        label: qsTr("Window hotkey")
                        Row {
                            spacing: Theme.spacingS
                            UComboBox {
                                width: 150; model: taskPresetCard.taskLabels
                                currentIndex: Math.max(0, taskPresetCard.taskIds.indexOf(App.settings.windowTask))
                                onActivated: (i) => App.settings.windowTask = taskPresetCard.taskIds[i]
                            }
                            UComboBox {
                                width: 150; model: page.taskDestinationLabels
                                enabled: taskPresetCard.usesUpload(App.settings.windowTask)
                                currentIndex: Math.max(0, page.taskDestinationIds.indexOf(App.settings.windowTaskDestination))
                                onActivated: (i) => App.settings.windowTaskDestination = page.taskDestinationIds[i]
                            }
                        }
                    }
                }
            }

            // The "no hotkey backend" install / copy-paste card sits LAST on this
            // pane on purpose: the key recorders come first, and this heavier card
            // (with its one-click writer) is far less in-your-face down here than
            // it was leading the pane.
            SettingsGroup {
                id: unavCard
                visible: !App.hotkeysAvailable
                width: page.cardWidth
                property bool manualOpen: false
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Global hotkeys unavailable on this desktop") }
                    UCard {
                        width: parent.width
                        Column {
                            width: parent.width
                            spacing: Theme.spacingM
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: qsTr("This desktop offers neither KGlobalAccel nor a working GlobalShortcuts portal, so Unisic cannot register global shortcuts itself. Instead each capture action can be bound to a command in your desktop's own shortcut settings; a running Unisic instance then picks it up.")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontS + 1
                            }
                            // Auto path: COSMIC/GNOME/Cinnamon/Xfce — one click
                            // writes the desktop's config with the keys above.
                            Column {
                                visible: App.desktopShortcutsAuto
                                width: parent.width
                                spacing: Theme.spacingS
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    text: qsTr("Unisic can add these to %1 for you, using the keys above — they stay editable in %1's own keyboard settings.").arg(App.desktopShortcutName)
                                    color: Theme.textTertiary
                                    font.pixelSize: Theme.fontS
                                }
                                Row {
                                    spacing: Theme.spacingS
                                    UButton {
                                        text: qsTr("Add shortcuts to %1").arg(App.desktopShortcutName)
                                        onClicked: App.installDesktopShortcuts()
                                    }
                                    UButton {
                                        text: qsTr("Remove")
                                        variant: "ghost"
                                        compact: true
                                        onClicked: App.removeDesktopShortcuts()
                                    }
                                }
                            }
                            Rectangle {
                                visible: App.desktopShortcutsAuto
                                width: parent.width; height: 1
                                color: Theme.divider
                            }
                            // Copy-paste: tucked behind a toggle where auto exists,
                            // shown directly on niri/sway/etc. (the only path there).
                            UButton {
                                visible: App.desktopShortcutsAuto
                                variant: "ghost"
                                compact: true
                                text: unavCard.manualOpen ? qsTr("Hide commands") : qsTr("Show commands")
                                onClicked: unavCard.manualOpen = !unavCard.manualOpen
                            }
                            Text {
                                visible: !App.desktopShortcutsAuto || unavCard.manualOpen
                                width: parent.width
                                wrapMode: Text.WordWrap
                                textFormat: Text.MarkdownText
                                text: (App.desktopShortcutsAuto ? "" : qsTr("Bind these commands in your desktop:") + "\n\n")
                                      + App.desktopShortcutManualText()
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontS
                            }
                        }
                    }
                }
            }
        }
        }

        // ===== Developer (dev build only, tab 8) =====
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 8
            // Gated on App.devBuild: in a release build the pane must never
            // instantiate, or the search would list its rows and jumping to
            // one would expose the smoke-test buttons on a hidden tab.
            // Visible-but-inert while searching: per-row `visible:` gates keep
            // their real values so collectRows can skip hidden rows.
            visible: App.devBuild && (page.tab === 8 || page.searchActive)
            opacity: (page.tab === 8 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 8 && !page.searchActive
            property bool touched: false
            // Search needs every pane instantiated so it can walk the rows.
            active: App.devBuild && (touched || visible || page.searchActive)
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Developer") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Dev build. Compositor capabilities detected on this system. F8 (or the button) runs the full smoke test.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Native notifications")
                        help: qsTr("Whether a desktop notification server is available.")
                        helpDetail: qsTr("Detected from org.freedesktop.Notifications on the session bus. Without it (e.g. bare Sway) capture cards need the layer-shell path instead.")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capNativeNotification ? "✓" : "-"
                               color: App.capNativeNotification ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    SettingRow {
                        label: qsTr("Custom card (layer-shell)")
                        help: qsTr("Whether the compositor supports wlr-layer-shell surfaces.")
                        helpDetail: qsTr("Layer-shell powers the always-on-top capture card, the selection overlay above fullscreen apps and the pinned preview. KWin, wlroots and COSMIC have it; GNOME does not.")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capCustomNotification ? "✓" : "-"
                               color: App.capCustomNotification ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    SettingRow {
                        label: qsTr("Recording border")
                        help: qsTr("Whether a border can be drawn around the recorded region.")
                        helpDetail: qsTr("Drawn as a click-through overlay surface just outside the recorded area, so the frame never appears inside the recording itself. Hosted on layer-shell (KWin, wlroots, COSMIC), a KWin fullscreen fallback, or an XWayland helper on GNOME.")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capRecordBorder ? "✓" : "-"
                               color: App.capRecordBorder ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    SettingRow {
                        label: qsTr("PipeWire (build)")
                        help: qsTr("Whether this build was compiled against PipeWire.")
                        helpDetail: qsTr("Set at build time by pipewire-devel (the HAVE_PIPEWIRE guard). Without it every recording path is compiled out, no matter what the desktop supports.")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capPipeWireBuild ? "✓" : "-"
                               color: App.capPipeWireBuild ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    SettingRow {
                        label: qsTr("ScreenCast portal")
                        help: qsTr("Whether this desktop has a ScreenCast portal backend.")
                        helpDetail: qsTr("Probed at startup by reading the version property of org.freedesktop.portal.ScreenCast. The backend is what asks for permission and opens the PipeWire stream; a running pipewire daemon does not imply one. KDE, GNOME, wlroots and COSMIC have it - the -xapp backend (Cinnamon, MATE, XFCE) and -lxqt do not.")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capScreenCastPortal ? "✓" : "-"
                               color: App.capScreenCastPortal ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    SettingRow {
                        label: qsTr("Video preview")
                        help: qsTr("Whether the trim editor can show a live video preview.")
                        helpDetail: qsTr("Needs the QtMultimedia QML module (qt6-qtmultimedia). Without it the trim editor falls back to a slider-only range picker.")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capVideoPlayback ? "✓" : "-"
                               color: App.capVideoPlayback ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    Row {
                        width: parent.width
                        spacing: Theme.spacingS
                        UButton {
                            compact: true; variant: "tonal"
                            text: App.smokeTestRunning ? qsTr("Running…") : qsTr("Run full smoke test (F8)")
                            enabled: !App.smokeTestRunning
                            onClicked: App.runSmokeTest()
                        }
                        UButton {
                            compact: true; variant: "tonal"
                            text: qsTr("Copy log")
                            visible: App.smokeTestLog !== ""
                            onClicked: { App.copyText(App.smokeTestLog); App.showToast(qsTr("Smoke test log copied")) }
                        }
                    }
                    Rectangle {
                        visible: App.smokeTestLog !== ""
                        width: parent.width
                        height: 200
                        radius: Theme.radiusM
                        color: Theme.background
                        border.width: 1
                        border.color: Theme.divider
                        clip: true
                        Flickable {
                            anchors.fill: parent
                            anchors.margins: 8
                            contentWidth: width
                            contentHeight: logText.height
                            boundsBehavior: Flickable.StopAtBounds
                            Text {
                                id: logText
                                width: parent.width
                                text: App.smokeTestLog
                                color: Theme.textSecondary
                                font.family: "monospace"
                                font.pixelSize: Theme.fontS
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                }
            }

            SettingsGroup {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Run a single action") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Trigger each path on its own to verify it by hand. Every new feature must add its trigger here and to the smoke test.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    Flow {
                        width: parent.width
                        spacing: Theme.spacingS
                        UButton { compact: true; variant: "tonal"; text: qsTr("Welcome screen"); onClicked: App.devTestWelcome() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture fullscreen"); onClicked: App.captureFullScreen() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture region"); onClicked: App.captureRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture window"); onClicked: App.captureWindow() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture screen at cursor"); onClicked: App.captureScreenUnderCursor() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Re-capture last region"); onClicked: App.recaptureLastRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec GIF (screen)"); onClicked: App.startGifFullScreen() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec GIF (region)"); onClicked: App.startGifRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec MP4 (screen)"); onClicked: App.startVideoScreen() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec MP4 (region)"); onClicked: App.startVideoRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec MP4 (window)"); onClicked: App.startVideoWindow() }
                        UButton { compact: true; variant: "danger"; text: qsTr("Stop recording"); onClicked: App.stopRecording() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Record border (4 s)"); onClicked: App.devTestRecordBorder() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Test notification"); onClicked: App.devTestNotification() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Card preview (3 s)"); onClicked: App.devTestCardPreview() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Notification action order"); onClicked: App.devTestNotificationOrder() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Open editor"); onClicked: App.devTestEditor() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Tool shortcuts (editor)"); onClicked: App.devTestEditor() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Tool shortcuts (overlay)"); onClicked: App.captureRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Edit from history"); onClicked: App.devTestEditFromHistory() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Open a file…"); onClicked: App.openFileForEditing() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Verify hotkey binds"); onClicked: App.devTestHotkeyBinds() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Alternate hotkeys"); onClicked: App.devTestAltHotkeys() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Desktop shortcuts (bind commands)"); onClicked: App.devTestDesktopShortcuts() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Upload test image"); onClicked: App.devTestUpload() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Settings round-trip"); onClicked: App.devTestSettingsRoundTrip() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Copy last capture"); onClicked: App.devTestCopyLast() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Klipper clipboard history"); onClicked: App.devTestClipboardHistory() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Show capture in folder"); onClicked: App.devTestShowInFolder() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Open preview window"); onClicked: App.devTestPreview() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Pin preview from history"); onClicked: App.devTestPreviewFromHistory() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Add history entry"); onClicked: App.devTestHistory() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Add starred history entry"); onClicked: App.devTestFavoriteHistory() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("History drag payload"); onClicked: App.devTestHistoryDrag() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("History search + filters"); onClicked: App.devTestHistoryFilter() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Export ZIP"); onClicked: App.devTestZipExport() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Imgur Client-ID guard"); onClicked: App.devTestImgurSetup() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Notification drag payload"); onClicked: App.devTestNotificationDrag() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("OCR region"); enabled: App.ocrAvailable; onClicked: App.captureRegionOcr() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture sound"); onClicked: App.devTestCaptureSound() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Recording sound"); onClicked: App.devTestRecordingSound() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Record start sound"); onClicked: App.devTestRecordStartSound() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Trash sound"); onClicked: App.devTestTrashSound() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Text render"); onClicked: App.devTestTextRender() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Keystroke badge"); onClicked: App.devTestKeystrokeBadge() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Custom theme"); onClicked: App.devTestCustomTheme() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Paste clipboard"); onClicked: App.devTestClipboardPaste() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture delay"); onClicked: App.devTestCaptureDelay() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Copy as"); onClicked: App.devTestCopyAs() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Watermark"); onClicked: App.devTestWatermark() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Callout"); onClicked: App.devTestCallout() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Shift snap"); onClicked: App.devTestShiftSnap() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("QR preview"); enabled: App.qrAvailable; onClicked: App.devTestQrPreview() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Copy diagnostics"); onClicked: App.devTestDiagnostics() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Dependency report"); onClicked: App.devTestSystemCheck() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("System check dialog"); onClicked: settingsSystemCheck.open() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Do not disturb"); enabled: App.capDoNotDisturb; onClicked: App.devTestDoNotDisturb() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("External action"); onClicked: App.devTestExternalAction() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Task preset"); onClicked: App.devTestTaskPreset() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("CLI output"); onClicked: App.devTestCliOutput() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Measure"); onClicked: App.devTestMeasureTools() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Hardware encoder"); onClicked: App.devTestHardwareEncoder() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Freeze recorder (watchdog)"); onClicked: App.devTestFreezeRecorder() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Per-app audio"); onClicked: App.devTestPerAppAudio() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Instant replay"); enabled: App.recordingAvailable; onClicked: App.devTestInstantReplay() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Trim recording"); onClicked: App.devTestTrimRecording() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Trim cut (exact + lossless)"); onClicked: App.devTestTrimCut() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Recording pause excise"); onClicked: App.devTestPauseExcise() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Screenshot cursor"); onClicked: App.devTestCursorCapability() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Shape edit"); onClicked: App.devTestShapeEdit() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Magnifier"); onClicked: App.devTestMagnify() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Eyedropper"); onClicked: App.devTestEyedropper() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Pixel loupe"); onClicked: App.devTestPixelLoupe() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture on release"); onClicked: App.devTestCaptureOnRelease() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("OCR boxes"); enabled: App.ocrAvailable; onClicked: App.devTestOcrBoxes() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("OCR highlight + redact"); enabled: App.ocrAvailable; onClicked: App.devTestOcrHighlight() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Auto-redact pattern"); enabled: App.ocrAvailable; onClicked: App.devTestOcrRedactPattern() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Style presets"); onClicked: App.devTestStylePresets() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Cursor overlay"); onClicked: App.devTestCursorOverlay() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("OCR auto language"); enabled: App.ocrAvailable; onClicked: App.devTestOcrAutoLang() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Language"); onClicked: App.devTestLanguage() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Update check"); onClicked: App.devTestUpdateCheck() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Simulate update"); onClicked: App.devTestUpdateAvailable() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Auto-restart gate"); onClicked: App.devTestAutoRestart() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Filename + save routing"); onClicked: App.devTestFilename() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Save-as dialog"); onClicked: App.devTestSaveDialog() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Record countdown"); onClicked: App.devTestCountdown() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Full-screen countdown"); onClicked: App.devTestFullscreenCountdown() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Toggle autostart"); onClicked: App.autostartEnabled = !App.autostartEnabled }
                    }
                }
            }
        }
        }
    }
}
