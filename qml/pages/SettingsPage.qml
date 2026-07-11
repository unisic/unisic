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

    // ---- settings search ----
    property string searchQuery: ""
    readonly property bool searchActive: searchQuery.length > 0
    property var searchResults: []
    // Set when jumping to a result; highlights matching row labels for a moment.
    property string highlightQuery: ""
    onSearchQueryChanged: Qt.callLater(rebuildSearch)
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

    // The Developer tab (index 6) only exists in a dev build.
    readonly property var tabNames: {
        var t = [qsTr("General"), qsTr("Preferences"), qsTr("Appearance"),
                 qsTr("Editor"), qsTr("Recording"), qsTr("Hotkeys")]
        if (App.devBuild)
            t.push(qsTr("Developer"))
        return t
    }

    readonly property var themeIds: ["system", "unisic", "dark", "light",
                                     "catppuccin-mocha", "catppuccin-latte", "dracula", "nord", "gruvbox"]
    readonly property var themeNames: [qsTr("System Theme"), "Unisic", qsTr("Dark"), qsTr("Light"),
                                       "Catppuccin Mocha", "Catppuccin Latte", "Dracula", "Nord", "Gruvbox"]
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
    function iconOverride(id) {
        var j = App.settings.editorToolIcons
        if (!j) return ""
        try { var m = JSON.parse(j); return m[id] || "" } catch (e) { return "" }
    }
    function setIconOverride(id, name) {
        var j = App.settings.editorToolIcons
        var m = {}
        if (j) { try { m = JSON.parse(j) } catch (e) { m = {} } }
        if (name && name !== "") m[id] = name; else delete m[id]
        App.settings.editorToolIcons = JSON.stringify(m)
    }

    // One hotkey action: the label row (with the searchable "?" badge) on
    // top, the full-width multi-binding chip editor underneath — chips read
    // left to right (primary first) and the "+ Add" ghost button is always
    // at the end of the list, which is where the eye expects it.
    component HotkeyRow: Column {
        id: hotkeyRow
        property alias label: hotkeyHeader.label
        property alias help: hotkeyHeader.help
        property alias helpDetail: hotkeyHeader.helpDetail
        property alias available: hotkeyHeader.available
        property alias hint: hotkeyHeader.hint
        property string shortcuts: ""
        signal changed(string shortcuts)
        width: parent.width
        spacing: 2
        SettingRow {
            id: hotkeyHeader
            width: parent.width
        }
        UShortcutList {
            width: parent.width
            enabled: hotkeyRow.available
            opacity: hotkeyRow.available ? 1.0 : 0.45
            shortcuts: hotkeyRow.shortcuts
            onChanged: (t) => hotkeyRow.changed(t)
        }
    }

    component SettingRow: Item {
        id: settingRow
        readonly property bool isSettingRow: true   // marker for the settings search
        property alias label: labelText.text
        // "?" badge: `help` is the one-line summary (hover tooltip); clicking
        // opens the in-app help dialog with `help` on top and `helpDetail`
        // (the full explanation) below it. Rows without help show no badge.
        property string help: ""
        property string helpDetail: ""
        // Capability gating: unavailable options are greyed out (not hidden) with
        // a one-line reason, so a release build still shows what it can't do.
        property bool available: true
        property string hint: ""
        default property alias control: slot.data
        width: parent.width
        // Rows grow with tall controls (e.g. the multi-binding hotkey chips
        // wrapping to a second line); everything else keeps the 44px rhythm.
        height: Math.max(44, slot.height)
        opacity: available ? 1.0 : 0.45
        Text {
            id: labelText
            anchors.left: parent.left
            anchors.top: parent.top
            height: 44
            verticalAlignment: Text.AlignVCenter
            width: slot.x - Theme.spacingM
            elide: Text.ElideRight
            // Briefly tinted when this row was just jumped to from the search.
            color: page.highlightQuery.length > 0
                   && text.toLowerCase().indexOf(page.highlightQuery) >= 0
                   ? Theme.accent : Theme.textPrimary
            font.pixelSize: Theme.fontM
        }
        Item {
            id: slot
            // Only the CONTROL is disabled on an unavailable row — the "?"
            // badge must stay clickable so the dialog can explain why.
            enabled: settingRow.available
            anchors.right: parent.right
            anchors.top: parent.top
            width: childrenRect.width
            height: Math.max(44, childrenRect.height)
        }
        Rectangle {
            id: helpBadge
            visible: settingRow.help !== ""
            x: labelText.x + Math.min(labelText.implicitWidth, labelText.width) + 8
            y: (44 - height) / 2
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
                hoverEnabled: true
                cursorShape: Qt.WhatsThisCursor
                onClicked: {
                    helpTipDelay.stop()
                    helpTipPopup.close()
                    page.showHelp(settingRow.label, settingRow.help, settingRow.helpDetail,
                                  settingRow.available ? "" : settingRow.hint)
                }
                // Short delay so the tip doesn't flash on every mouse pass.
                onContainsMouseChanged: {
                    if (containsMouse)
                        helpTipDelay.start()
                    else {
                        helpTipDelay.stop()
                        helpTipPopup.close()
                    }
                }
            }
            Timer { id: helpTipDelay; interval: 240; onTriggered: helpTipPopup.open() }
            Popup {
                // Popup renders on the overlay layer, so the tooltip can never
                // be buried under later rows/cards or clipped by the Flickable.
                id: helpTipPopup
                parent: helpBadge
                x: -(width / 2) + parent.width / 2
                y: parent.height + 8
                margins: 8 // clamp inside the window near edges
                width: Math.min(helpTipText.implicitWidth, 300) + leftPadding + rightPadding
                closePolicy: Popup.NoAutoClose
                padding: 12
                background: Rectangle {
                    radius: Theme.radiusL
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.alpha(Theme.accent, 0.4)
                }
                enter: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 130; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 130; easing.type: Easing.OutCubic }
                }
                exit: Transition {
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 }
                }
                contentItem: Column {
                    spacing: 6
                    Text {
                        width: parent.width
                        text: settingRow.label
                        color: Theme.accent
                        font.pixelSize: Theme.fontS
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        id: helpTipText
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: settingRow.help
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontS + 1
                        lineHeight: 1.2
                    }
                    Text {
                        visible: settingRow.helpDetail !== "" || !settingRow.available
                        width: parent.width
                        text: qsTr("Click for the full explanation")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS - 1
                    }
                }
            }
        }
    }

    component SectionTitle: Text {
        color: Theme.textPrimary
        font.pixelSize: Theme.fontL
        font.weight: Font.DemiBold
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

    // ---------------- tab bar ----------------
    // A Flow (not a Row): with the Preferences + Developer tabs added the chips
    // no longer fit one line at narrow widths, and a Row ran them under the
    // search field. The Flow wraps extra chips onto a second line and its right
    // edge stops before the search box, so the two never overlap.
    Flow {
        id: tabBar
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingXL
        anchors.right: searchField.left
        anchors.rightMargin: Theme.spacingM
        anchors.topMargin: Theme.spacingM
        spacing: Theme.spacingS
        Repeater {
            model: page.tabNames
            delegate: Rectangle {
                required property int index
                required property string modelData
                width: tabLabel.implicitWidth + 28
                height: 34
                radius: Theme.radiusM
                color: page.tab === index ? Theme.accent
                     : tabMouse.containsMouse ? Theme.alpha(Theme.accent, 0.16)
                     : Theme.surface
                border.width: 1
                border.color: page.tab === index ? Theme.accent : Theme.divider
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
                Text {
                    id: tabLabel
                    anchors.centerIn: parent
                    text: modelData
                    color: page.tab === index ? Theme.textOnAccent : Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    font.weight: page.tab === index ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    id: tabMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.tab = index
                }
            }
        }
    }

    // Settings search: type to list matching rows across ALL tabs; click a
    // result to jump to its tab (label flashes accent for a moment). Pinned to
    // the top-right; the tab Flow reserves the space to its left.
    UTextField {
        id: searchField
        anchors.top: tabBar.top
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingXL
        width: 190
        placeholder: qsTr("Search settings…")
        text: page.searchQuery
        onEdited: (t) => page.searchQuery = t
    }

    // Persistence warning: the config dir isn't writable, so nothing sticks
    // across launches. Tells the user exactly why + where to fix it.
    Rectangle {
        id: persistWarn
        visible: !App.settings.persistent
        anchors.top: tabBar.bottom
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

    // ---------------- panes ----------------
    Item {
        id: paneArea
        anchors.top: persistWarn.visible ? persistWarn.bottom : tabBar.bottom
        anchors.topMargin: Theme.spacingM
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spacingXL
        anchors.rightMargin: Theme.spacingXL
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
                    text: qsTr("No settings match \u201C%1\u201D").arg(page.searchQuery)
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

        // ===== General =====
        // Lazily built on first visit, then kept alive (preserves scroll
        // position). Six always-instantiated panes made opening Settings
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
            UCard {
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
                            property var ids: ["system", "en", "pl", "es", "it"]
                            // Native names on purpose — every user recognises
                            // their own language regardless of the current UI.
                            model: [qsTr("System"), "English", "Polski", "Español", "Italiano"]
                            currentIndex: Math.max(0, ids.indexOf(App.settings.uiLanguage))
                            onActivated: (i) => App.settings.uiLanguage = ids[i]
                        }
                    }
                    SettingRow {
                        available: App.ocrAvailable
                        hint: App.ocrAvailable ? ""
                              : qsTr("OCR is not built in. Install tesseract and a language pack, then rebuild.")
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
                }
            }

            UCard {
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
                            text: qsTr("Filename template. Available tokens: %date%, %time%, %datetime%, %unix%, %rand%")
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
                                return qsTr("Preview: %1").arg(App.filenamePreview())
                            }
                            color: Theme.accent
                            font.pixelSize: Theme.fontS
                            font.family: "monospace"
                        }
                    }
                }
            }

            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Updates") }
                    SettingRow {
                        label: qsTr("Current version")
                        help: qsTr("The Unisic version you are running.")
                        Text {
                            height: 44
                            verticalAlignment: Text.AlignVCenter
                            text: "v" + App.appVersion
                                  + (App.buildNumber === "dev" ? " · dev" : " · build " + App.buildNumber)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontM
                        }
                    }
                    SettingRow {
                        available: App.buildNumber !== "dev"
                        hint: qsTr("Automatic checks are disabled in dev builds.")
                        label: qsTr("Automatic updates")
                        help: qsTr("Checks for a new release shortly after startup and once a day, then installs it in the background.")
                        helpDetail: qsTr("Only the latest release version is fetched from the GitHub API — nothing about you or your system is sent. AppImage installs are downloaded and swapped in place automatically; the new version starts on the next launch (or via the tray's Restart entry). Package installs are updated by the system package manager instead.")
                        USwitch {
                            checked: App.settings.autoCheckUpdates
                            onToggled: (c) => App.settings.autoCheckUpdates = c
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
                                  : App.updater.installKind === "flatpak"
                                    ? qsTr("The Flatpak sandbox can't replace the app from inside — install the new bundle.")
                                    : App.updater.installKind === "appimage"
                                      ? qsTr("The AppImage location is read-only — it can't update itself from here.")
                                      : qsTr("The update arrives through your package manager.")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontS
                        }
                    }
                }
            }

        }
        }

        // ===== Preferences =====
        // Behavioral / workflow settings moved out of the overloaded General
        // tab (notifications, capture behavior, app behavior, the after-capture
        // and after-upload pipelines).
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
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Notifications") }
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
                        helpDetail: qsTr("On: the capture notification is Unisic's stylized always-on-top card (layer-shell), with the position, style and auto-hide options below.\nOff or unsupported: a native desktop notification is shown instead — the capture feedback itself never disappears; use the master switch above to silence everything.")
                        USwitch {
                            checked: App.settings.showCapturePopup
                            enabled: App.settings.showNotifications
                            onToggled: (c) => App.settings.showCapturePopup = c
                        }
                    }
                    SettingRow {
                        // Corner only matters for the layer-shell card we position
                        // ourselves; a native notification is placed by the server.
                        visible: App.settings.showCapturePopup
                        available: App.layerShellActive
                        hint: App.layerShellActive ? ""
                              : qsTr("The system notification server decides the position here, because this compositor has no layer-shell card to place.")
                        label: qsTr("Notification position")
                        help: qsTr("Screen corner where the capture card appears.")
                        helpDetail: qsTr("Only applies to the layer-shell card, which Unisic positions itself. Native desktop notifications are placed by the system notification server and ignore this.")
                        UComboBox {
                            width: 180
                            model: page.popupPosNames
                            currentIndex: Math.max(0, page.popupPosIds.indexOf(App.settings.capturePopupPosition))
                            onActivated: (i) => App.settings.capturePopupPosition = page.popupPosIds[i]
                        }
                    }
                    SettingRow {
                        visible: App.settings.showCapturePopup
                        height: App.settings.showCapturePopup ? 44 : 0
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
                        height: App.settings.showCapturePopup ? 44 : 0
                        label: qsTr("Hide it during fullscreen / Do Not Disturb")
                        help: qsTr("Mutes capture cards while a fullscreen app or DND is active.")
                        helpDetail: qsTr("Uses the notification server's inhibition state (fullscreen application, Do Not Disturb, screen sharing). Inhibitors that were already stuck when Unisic started are ignored, so a misbehaving third-party app can't silence your capture feedback forever.")
                        USwitch { checked: App.settings.muteOnFullscreen; onToggled: (c) => App.settings.muteOnFullscreen = c }
                    }
                }
            }

            UCard {
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
                            values: [0, 50, 100, 200, 300, 500, 1000, 2000, 3000, 5000]
                            from: 0; to: 5000
                            suffix: " ms"
                            value: App.settings.captureDelayMs
                            onChanged: (v) => App.settings.captureDelayMs = v
                        }
                    }
                    SettingRow {
                        label: qsTr("Include mouse cursor")
                        help: qsTr("Draws the mouse pointer into the capture.")
                        helpDetail: qsTr("When supported by the active backend (portal or KWin), the cursor is composited into the image exactly where it was at capture time.")
                        USwitch { checked: App.settings.includeCursor; onToggled: (c) => App.settings.includeCursor = c }
                    }
                    SettingRow {
                        label: qsTr("Capture on release")
                        help: qsTr("Takes the screenshot the moment you release the selection.")
                        helpDetail: qsTr("Region screenshots only: releasing the mouse button after drawing the selection captures immediately — no Enter, double-click or toolbar button. This skips the on-overlay annotation stage (you can still annotate afterwards in the editor). Picking a GIF recording region is unaffected and keeps its Start button.")
                        USwitch { checked: App.settings.captureOnRelease; onToggled: (c) => App.settings.captureOnRelease = c }
                    }
                    SettingRow {
                        label: qsTr("Capture sound")
                        help: qsTr("Plays a short sound when a screenshot is taken.")
                        helpDetail: qsTr("A fullscreen capture has no on-screen feedback, so it can be hard to tell it happened. Pick a bundled cue — Shutter, Click, Beep, Ding or Pop — a custom sound, or Off. Custom sounds are .wav/.ogg files in ~/.config/unisic/sounds (add them there or with the + button). The sound plays through the system audio (pw-play/paplay/aplay).")
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
                        helpDetail: qsTr("Encoding can take a while after you stop a recording, so this cue tells you the file is actually ready. It is separate from the screenshot sound: pick a bundled cue — Shutter, Click, Beep, Ding or Pop — a custom sound, or Off. Custom sounds are .wav/.ogg files in ~/.config/unisic/sounds (shared with the capture sound).")
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
                }
            }

            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Editor defaults") }
                    SettingRow {
                        label: qsTr("Always start with the default colors")
                        help: qsTr("Color picks made while annotating last for that session only.")
                        helpDetail: qsTr("With this on, changing the stroke, fill, text outline or text background color in the editor or on the capture overlay does not overwrite your saved defaults — the next session starts again from the colors configured in Settings → Editor.")
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

            UCard {
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
                    SettingRow {
                        label: qsTr("Open file after saving")
                        help: qsTr("Opens each capture in your image viewer after saving.")
                        helpDetail: qsTr("Uses the system default application for the file type. Independent from the editor; this only opens the saved file.")
                        USwitch { checked: App.settings.openAfterSave; onToggled: (c) => App.settings.openAfterSave = c }
                    }
                }
            }

            UCard {
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

            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("After upload") }
                    SettingRow {
                        label: qsTr("Copy link to clipboard")
                        help: qsTr("Copies the upload URL once the upload finishes.")
                        helpDetail: qsTr("Ready to paste anywhere. Combine with auto-upload for a ShareX-style capture-to-link flow.")
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

        }
        }

        // ===== Appearance =====
        // Lazily built on first visit, then kept alive (preserves scroll
        // position). Six always-instantiated panes made opening Settings
        // build hundreds of controls for tabs never shown.
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 2
            // Visible-but-inert while searching: per-row `visible:` gates keep
            // their real values so collectRows can skip hidden rows.
            visible: page.tab === 2 || page.searchActive
            opacity: (page.tab === 2 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 2 && !page.searchActive
            property bool touched: false
            // Search needs every pane instantiated so it can walk the rows.
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            UCard {
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
                    SettingRow {
                        visible: App.settings.showCapturePopup
                        available: App.layerShellActive
                        hint: App.layerShellActive ? ""
                              : qsTr("The style only applies to the layer-shell card; a native notification is drawn by the system server.")
                        label: qsTr("Notification style")
                        help: qsTr("How the capture card looks, from full card to tiny pill.")
                        helpDetail: qsTr("Casual: the full card with a large thumbnail, title and a row of action buttons.\nCompact: a tighter card with a medium thumbnail, filename and the same actions.\nSmall: one slim row with tiny inline action icons.\nMinimal: a pill with just the filename; clicking it opens the floating preview.\nThumbnail: image-first, the capture fills the card and actions appear on hover.\n\nApplies to the next capture.")
                        UComboBox {
                            width: 180
                            model: [qsTr("Casual"), qsTr("Compact"), qsTr("Small"), qsTr("Minimal"), qsTr("Thumbnail")]
                            currentIndex: Math.max(0, ["casual", "compact", "small", "minimal", "thumbnail"].indexOf(App.settings.capturePopupStyle))
                            onActivated: (i) => App.settings.capturePopupStyle = ["casual", "compact", "small", "minimal", "thumbnail"][i]
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("The System theme follows your desktop's light/dark mode and accent color.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }

            UCard {
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

            UCard {
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

                        Repeater {
                            // Rebuilds whenever the drop-in folder or the current
                            // selection changes (both referenced below).
                            model: {
                                // Referenced so the thumbnails re-render when the
                                // OS light/dark scheme flips.
                                var color = App.trayContrastColor
                                var bundled = App.bundledTrayIcons
                                var presets = App.trayIconPresets
                                var cur = App.settings.trayIconPath
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
                                // A file picked from elsewhere still gets a tile so
                                // the current choice is always visible/selected.
                                if (cur !== "" && presets.indexOf(cur) === -1
                                        && bundled.indexOf(cur) === -1)
                                    arr.push({ path: cur, label: page.iconLabel(cur),
                                               src: page.fileUrl(cur) })
                                return arr
                            }
                            delegate: Rectangle {
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
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: App.selectTrayIcon(tile.modelData.path)
                                    ToolTip.text: tile.modelData.label
                                    ToolTip.visible: containsMouse && tile.modelData.path !== ""
                                    ToolTip.delay: 500
                                }
                            }
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
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.addTrayIcon()
                                ToolTip.text: qsTr("Add an icon (copies it here)")
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 500
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

            UCard {
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

        // ===== Editor =====
        // Lazily built on first visit, then kept alive (preserves scroll
        // position). Six always-instantiated panes made opening Settings
        // build hundreds of controls for tabs never shown.
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 3
            // Visible-but-inert while searching: per-row `visible:` gates keep
            // their real values so collectRows can skip hidden rows.
            visible: page.tab === 3 || page.searchActive
            opacity: (page.tab === 3 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 3 && !page.searchActive
            property bool touched: false
            // Search needs every pane instantiated so it can walk the rows.
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            UCard {
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
                }
            }

            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Capture overlay") }
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
                        label: qsTr("Smart pick (experimental)")
                        help: qsTr("Experimental: click once during region selection to pick the detected object (window, panel, image) under the cursor.")
                        helpDetail: qsTr("EXPERIMENTAL: detection is purely visual (pixels, no ML, no network, no compositor help), so it will not recognize every window or element and results vary with theme and content. With Smart pick on, the region overlay highlights the element under your cursor, so a single click selects its rectangle with no press-and-drag needed. It finds single elements (buttons, icons, text lines, thumbnails), groups of elements (a toolbar with its buttons, an icon grid, a form) and window-like frames. The scroll wheel changes the level: innermost element, its group, panels, up to the whole screen; the badge above the highlight shows size and level. Dragging always draws a manual rectangle, and the selection stays adjustable afterwards.")
                        USwitch { checked: App.settings.smartPick; onToggled: (c) => App.settings.smartPick = c }
                    }
                }
            }

            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Smart background removal") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        visible: !App.u2netAvailable
                        text: qsTr("This build was compiled without onnxruntime, so AI background removal (U-2-Net) is unavailable — the built-in heuristic object cutout still works. Install onnxruntime and rebuild to enable it.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        available: App.u2netAvailable
                        label: qsTr("Use U-2-Net for object cutout")
                        help: qsTr("Use the U-2-Net neural model (when downloaded) for the object-pick cutout and the editor's Remove background action.")
                        helpDetail: qsTr("When off, or the model is not downloaded, cutout falls back to the dependency-free heuristic segmenter. Runs fully offline once the model is fetched.")
                        USwitch {
                            checked: App.settings.useU2Net
                            enabled: App.u2netAvailable
                            onToggled: (c) => App.settings.useU2Net = c
                        }
                    }
                    SettingRow {
                        visible: App.u2netAvailable
                        label: qsTr("Model")
                        help: qsTr("Which neural model performs the cutout.")
                        helpDetail: qsTr("Models differ in size, speed and quality:\n• U-2-Net small — fastest, good general results.\n• U-2-Net full — noticeably better masks, big download.\n• U-2-Net human — tuned for people/portraits.\n• Silueta — U-2-Net full compressed to 43 MB.\n• IS-Net — newest, best fine edge detail.\n• Custom — any segmentation .onnx you provide (u2net-style input is assumed; the input size is read from the model).\nEach downloaded model is stored locally and reused; switching models applies immediately.")
                        UComboBox {
                            id: segModelCombo
                            width: 240
                            property var entries: App.u2netModels()
                            model: entries.map(function (e) { return e.label })
                            currentIndex: {
                                for (var i = 0; i < entries.length; ++i)
                                    if (entries[i].id === App.settings.segmentModel) return i
                                return 0
                            }
                            onActivated: (i) => {
                                if (entries[i].id === "custom") {
                                    // Cancelled picker keeps the previous model —
                                    // currentIndex re-evaluates from settings.
                                    App.pickU2NetCustomModel()
                                } else {
                                    App.settings.segmentModel = entries[i].id
                                }
                            }
                        }
                    }
                    Text {
                        visible: App.u2netAvailable && App.settings.segmentModel === "custom"
                        width: parent.width
                        wrapMode: Text.WrapAnywhere
                        text: App.settings.segmentCustomModel
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                        font.family: "monospace"
                    }
                    SettingRow {
                        visible: App.u2netAvailable
                        label: qsTr("Model file")
                        help: qsTr("Download state of the selected model. Each model is fetched once and stored locally.")
                        Row {
                            spacing: Theme.spacingS
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: App.u2netModelReady ? qsTr("Downloaded") : qsTr("Not downloaded")
                                color: App.u2netModelReady ? Theme.textSecondary : Theme.textTertiary
                                font.pixelSize: Theme.fontS
                            }
                            UButton {
                                compact: true; variant: "tonal"
                                visible: !App.u2netModelReady && App.settings.segmentModel !== "custom"
                                text: App.u2netBusy ? qsTr("Downloading…") : qsTr("Download")
                                enabled: !App.u2netBusy
                                onClicked: App.downloadU2NetModel()
                            }
                            UButton {
                                compact: true; variant: "tonal"
                                visible: App.settings.segmentModel === "custom"
                                text: qsTr("Choose file…")
                                onClicked: App.pickU2NetCustomModel()
                            }
                            UButton {
                                compact: true; variant: "danger"
                                visible: App.u2netModelReady && App.settings.segmentModel !== "custom"
                                text: qsTr("Delete")
                                onClicked: App.deleteU2NetModel()
                            }
                        }
                    }
                }
            }

            UCard {
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
        }
        }

        // ===== Recording =====
        // Lazily built on first visit, then kept alive (preserves scroll
        // position). Six always-instantiated panes made opening Settings
        // build hundreds of controls for tabs never shown.
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 4
            // Visible-but-inert while searching: per-row `visible:` gates keep
            // their real values so collectRows can skip hidden rows.
            visible: page.tab === 4 || page.searchActive
            opacity: (page.tab === 4 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 4 && !page.searchActive
            property bool touched: false
            // Search needs every pane instantiated so it can walk the rows.
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Recording") }
                    SettingRow {
                        label: qsTr("GIF frame rate")
                        help: qsTr("Frames per second sampled into the GIF.")
                        helpDetail: qsTr("Higher is smoother but grows the file quickly. 10–15 fps is usually plenty for UI demos.")
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
                }
            }

            UCard {
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
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Applies to video recordings (MP4/WebM); GIFs have no audio.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }
        }
        }

        // ===== Hotkeys =====
        // Lazily built on first visit, then kept alive (preserves scroll
        // position). Six always-instantiated panes made opening Settings
        // build hundreds of controls for tabs never shown.
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 5
            // Visible-but-inert while searching: per-row `visible:` gates keep
            // their real values so collectRows can skip hidden rows.
            visible: page.tab === 5 || page.searchActive
            opacity: (page.tab === 5 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 5 && !page.searchActive
            property bool touched: false
            // Search needs every pane instantiated so it can walk the rows.
            active: touched || visible || page.searchActive
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {

            // KGlobalAccel missing (niri/sway/GNOME…): the recorders below
            // would be dead — explain the compositor-bind route instead.
            UCard {
                visible: !App.hotkeysAvailable
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Global hotkeys unavailable on this desktop") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        textFormat: Text.MarkdownText
                        text: qsTr("This desktop has no KGlobalAccel service, so Unisic cannot register global shortcuts itself. Bind keys in your compositor instead; a running Unisic instance picks the command up:\n\n" +
                                   "```\nunisic --region | --fullscreen | --window | --gif\n```\n\n" +
                                   "niri (`config.kdl`):\n\n" +
                                   "```\nbinds {\n    Mod+Shift+S { spawn \"unisic\" \"--region\"; }\n    Print { spawn \"unisic\" \"--fullscreen\"; }\n}\n```")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS + 1
                    }
                }
            }

            UCard {
                visible: App.hotkeysAvailable
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Global hotkeys") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: App.hotkeyBackend === "portal"
                              ? qsTr("Registered through the system GlobalShortcuts portal. Your desktop may show a one-time confirmation dialog; the binding it decides on is final (on Hyprland bind the ids in hyprland.conf).")
                              : qsTr("Registered through KDE global shortcuts (KGlobalAccel). Each action can hold several bindings: record one, then use the small chip to add alternatives (up to 4). Remove a binding with its ×.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    HotkeyRow {
                        label: qsTr("Full screen")
                        help: qsTr("Hotkey: capture all monitors at once.")
                        helpDetail: qsTr("Grabs the entire workspace silently (KWin path) or via the portal elsewhere, then runs the normal after-capture pipeline.")
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
                        helpDetail: qsTr("Copies the last screenshot taken in this session, whenever you press it. A dedicated shortcut never collides with the normal Ctrl+C — this replaces the old 2-second Ctrl+C grab, which could steal an ordinary copy right after a capture.")
                        shortcuts: App.settings.hotkeyCopyLast
                        onChanged: (t) => { App.settings.hotkeyCopyLast = t; App.applyHotkey("copy-last") }
                    }
                    UButton {
                        anchors.right: parent.right
                        text: qsTr("Apply hotkeys")
                        compact: true
                        onClicked: { App.applyHotkeys(); App.showToast(qsTr("Hotkeys re-registered")) }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: App.hotkeyBackend === "portal"
                              ? qsTr("Keys recorded here are suggestions passed to the portal; the system dialog confirms or adjusts them.")
                              : qsTr("Shortcuts apply immediately and stay in sync with KDE System Settings; an edit made there shows up here too.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }
        }
        }

        // ===== Developer (dev build only, tab 6) =====
        // Lazily built on first visit, then kept alive (preserves scroll
        // position). Six always-instantiated panes made opening Settings
        // build hundreds of controls for tabs never shown.
        Loader {
            anchors.fill: parent
            readonly property int tabIndex: 6
            // Gated on App.devBuild: in a release build the pane must never
            // instantiate, or the search would list its rows and jumping to
            // one would expose the smoke-test buttons on a hidden tab.
            // Visible-but-inert while searching: per-row `visible:` gates keep
            // their real values so collectRows can skip hidden rows.
            visible: App.devBuild && (page.tab === 6 || page.searchActive)
            opacity: (page.tab === 6 && !page.searchActive) ? 1 : 0
            enabled: page.tab === 6 && !page.searchActive
            property bool touched: false
            // Search needs every pane instantiated so it can walk the rows.
            active: App.devBuild && (touched || visible || page.searchActive)
            // Search-driven loads stay transient: latch only a REAL visit, or one
            // search would pin every pane (~thousands of items) for the app lifetime.
            onLoaded: if (!page.searchActive) touched = true
            sourceComponent: ScrollPane {
            UCard {
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
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capNativeNotification ? "✓" : "—"
                               color: App.capNativeNotification ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    SettingRow {
                        label: qsTr("Custom card (layer-shell)")
                        help: qsTr("Whether the compositor supports wlr-layer-shell surfaces.")
                        helpDetail: qsTr("Layer-shell powers the always-on-top capture card, the selection overlay above fullscreen apps and the pinned preview. KWin, wlroots and COSMIC have it; GNOME does not.")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capCustomNotification ? "✓" : "—"
                               color: App.capCustomNotification ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    SettingRow {
                        label: qsTr("Recording border")
                        help: qsTr("Whether a border can be drawn around the recorded region.")
                        helpDetail: qsTr("Drawn as a click-through overlay surface just outside the recorded area, so the frame never appears inside the recording itself.")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capRecordBorder ? "✓" : "—"
                               color: App.capRecordBorder ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    UButton {
                        compact: true; variant: "tonal"
                        text: App.smokeTestRunning ? qsTr("Running…") : qsTr("Run full smoke test (F8)")
                        enabled: !App.smokeTestRunning
                        onClicked: App.runSmokeTest()
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

            UCard {
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
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture fullscreen"); onClicked: App.captureFullScreen() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture region"); onClicked: App.captureRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture window"); onClicked: App.captureWindow() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec GIF (screen)"); onClicked: App.startGifFullScreen() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec GIF (region)"); onClicked: App.startGifRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec MP4 (screen)"); onClicked: App.startVideoScreen() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec MP4 (region)"); onClicked: App.startVideoRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec MP4 (window)"); onClicked: App.startVideoWindow() }
                        UButton { compact: true; variant: "danger"; text: qsTr("Stop recording"); onClicked: App.stopRecording() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Test notification"); onClicked: App.devTestNotification() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Open editor"); onClicked: App.devTestEditor() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Edit from history"); onClicked: App.devTestEditFromHistory() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Verify hotkey binds"); onClicked: App.devTestHotkeyBinds() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Alternate hotkeys"); onClicked: App.devTestAltHotkeys() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Upload test image"); onClicked: App.devTestUpload() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Settings round-trip"); onClicked: App.devTestSettingsRoundTrip() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Copy last capture"); onClicked: App.devTestCopyLast() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Open preview window"); onClicked: App.devTestPreview() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Pin preview from history"); onClicked: App.devTestPreviewFromHistory() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Add history entry"); onClicked: App.devTestHistory() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Add starred history entry"); onClicked: App.devTestFavoriteHistory() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("OCR region"); enabled: App.ocrAvailable; onClicked: App.captureRegionOcr() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Smart pick detect"); onClicked: App.devTestSmartPick() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture sound"); onClicked: App.devTestCaptureSound() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Recording sound"); onClicked: App.devTestRecordingSound() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Trash sound"); onClicked: App.devTestTrashSound() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Text render"); onClicked: App.devTestTextRender() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Shape edit"); onClicked: App.devTestShapeEdit() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture on release"); onClicked: App.devTestCaptureOnRelease() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("OCR boxes"); enabled: App.ocrAvailable; onClicked: App.devTestOcrBoxes() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("U-2-Net segment"); enabled: App.u2netAvailable; onClicked: App.devTestU2Net() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Language"); onClicked: App.devTestLanguage() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Update check"); onClicked: App.devTestUpdateCheck() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Simulate update"); onClicked: App.devTestUpdateAvailable() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Auto-restart gate"); onClicked: App.devTestAutoRestart() }
                    }
                }
            }
        }
        }
    }
}

