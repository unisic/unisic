import QtQuick
import QtQuick.Window
import Unisic
import "../components"

Item {
    id: page

    // ---- selection ----
    // Keyed by entryId, never by row: a filter switch renumbers every row, and a
    // batch delete shifts the rows behind it. Actions resolve ids through
    // App.history.entryById()/removeByIds(), so a selected tile scrolled out of
    // the viewport (no delegate) stays addressable.
    property var selection: ({})
    property int selectionCount: 0
    // Shift-click extends the selection from the last plainly-clicked row.
    property int anchorRow: -1

    function isSelected(id) { return page.selection[id] === true }
    function selectedIds() {
        return Object.keys(page.selection).map(Number)
    }
    // Every mutation below builds a NEW object and assigns that. A `var` property
    // compares what it is handed: mutating the held object and assigning the same
    // reference back reads as "unchanged", so no change signal fires and every
    // binding on the selection (the tile's checkmark, its accent border) keeps
    // showing the old state. Measured: 1 of 3 same-reference writes notified,
    // 3 of 3 new-object writes did.
    function toggleSelect(id) {
        var s = Object.assign({}, page.selection)
        if (s[id]) delete s[id]
        else s[id] = true
        page.selection = s
        page.selectionCount = Object.keys(s).length
    }
    function selectIds(ids) {
        var s = Object.assign({}, page.selection)
        for (var i = 0; i < ids.length; ++i)
            s[ids[i]] = true
        page.selection = s
        page.selectionCount = Object.keys(s).length
    }
    function clearSelection() {
        page.selection = ({})
        page.selectionCount = 0
        page.anchorRow = -1
    }

    // ---- per-entry actions, shared by click, keyboard and the hover strip ----
    function activate(kind, filePath) {
        // Images get the floating preview; a recording has no still to show, so
        // it opens in the system player.
        if (filePath === "") return
        if (kind === "image") App.previewFromHistory(filePath)
        else App.openFile(filePath)
    }
    function copyOne(kind, filePath, url) {
        if (filePath === "") return
        if (kind === "image") App.copyImageFromHistory(filePath)
        else App.copyAsFromHistory(filePath, url, "path")
    }

    // Midnight of the day `daysAgo` days back — the boundary both date labels
    // below compare against.
    function dayStart(daysAgo) {
        var now = new Date()
        var d = new Date(now.getFullYear(), now.getMonth(), now.getDate())
        d.setDate(d.getDate() - daysAgo)
        return d
    }

    function dateGroup(ts) {
        if (!ts) return ""
        var d = new Date(ts)
        if (d >= page.dayStart(0)) return qsTr("Today")
        if (d >= page.dayStart(1)) return qsTr("Yesterday")
        if (d >= page.dayStart(6)) return qsTr("Earlier this week")
        // Same-year captures don't need the year spelled out on every group.
        return Qt.formatDate(d, d.getFullYear() === new Date().getFullYear() ? "MMMM" : "MMMM yyyy")
    }

    // Tile timestamp. A full "yyyy-MM-dd HH:mm" spent most of the tile's one
    // meta line on a date the floating group header already gives away, and
    // pushed the size and dimensions out of the tile — so the date shrinks as
    // it gets more recent, down to just a time for today's captures.
    function tileTime(ts) {
        if (!ts) return ""
        var d = new Date(ts)
        // "HH:mm", not Locale.ShortFormat: several locales (pl among them) put
        // seconds in their short time format, and a capture's second is noise
        // that costs the size or the dimensions their place on the line.
        if (d >= page.dayStart(0)) return Qt.formatTime(d, "HH:mm")
        if (d >= page.dayStart(1)) return qsTr("Yesterday") + " " + Qt.formatTime(d, "HH:mm")
        if (d.getFullYear() === new Date().getFullYear())
            return Qt.formatDate(d, "d MMM") + " " + Qt.formatTime(d, "HH:mm")
        return Qt.formatDate(d, "d MMM yyyy")
    }

    HistoryFilterModel {
        id: filter
        sourceModel: App.history
    }

    // The tile's height is arithmetic over its parts, and a guessed constant for
    // the two footer lines was ~10 px short — so the meta line was cut off by the
    // card's clip. Measure the lines instead: this also survives a font or DPI
    // the guess was never checked against.
    FontMetrics { id: nameMetrics; font.pixelSize: Theme.fontS }
    FontMetrics { id: metaMetrics; font.pixelSize: Theme.fontS - 1 }
    // tile margins (2×6) + card margins (2×8) + the Column's two 6 px gaps.
    readonly property int tileChrome: 12 + 16 + 12
    readonly property int nameLine: Math.ceil(nameMetrics.height)
    readonly property int metaLine: Math.ceil(metaMetrics.height)

    UConfirmDialog {
        id: clearAllConfirm
        title: qsTr("Clear the whole history?")
        text: qsTr("This removes every history entry AND moves the capture files to the trash.\n\nStarred (favorite) captures are kept, both the entry and the file.")
        confirmText: qsTr("Clear all")
        destructive: true
        onAccepted: { page.clearSelection(); App.history.clearAll() }
    }
    UConfirmDialog {
        id: deleteSelectedConfirm
        title: qsTr("Delete the selected captures?")
        text: qsTr("This moves the selected capture files to the trash.\n\nStarred (favorite) captures in the selection are kept — un-star them first.")
        confirmText: qsTr("Delete")
        destructive: true
        onAccepted: {
            App.history.removeByIds(page.selectedIds())
            page.clearSelection()
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingL

        // ---- title + search + clear all ----
        Item {
            width: parent.width
            height: 40

            Text {
                id: titleText
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("History")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
            }
            Text {
                anchors.left: titleText.right
                anchors.leftMargin: Theme.spacingM
                anchors.baseline: titleText.baseline
                visible: App.history.count > 0
                text: filter.filtering ? qsTr("%1 of %2").arg(filter.count).arg(App.history.count)
                                       : qsTr("%1 items").arg(App.history.count)
                color: Theme.textTertiary
                font.pixelSize: Theme.fontS
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS

                UTextField {
                    id: searchField
                    width: 220
                    anchors.verticalCenter: parent.verticalCenter
                    placeholder: qsTr("Search name or link")
                    // edited() is user input only, so every programmatic reset
                    // below clears filter.searchText itself.
                    onEdited: (t) => filter.searchText = t.trim()
                    Keys.onEscapePressed: { searchField.text = ""; filter.searchText = "" }
                }
                UIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "close"
                    iconSize: 14
                    visible: searchField.text !== ""
                    tooltip: qsTr("Clear search")
                    onClicked: { searchField.text = ""; filter.searchText = "" }
                }
                UButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Clear all")
                    variant: "ghost"
                    compact: true
                    enabled: App.history.count > 0
                    onClicked: clearAllConfirm.open()
                }
            }
        }

        // ---- filter chips / selection actions ----
        // One row, two states: filtering is pointless while a batch is staged,
        // and stacking both bars would cost a whole tile row of height.
        Item {
            width: parent.width
            height: 32
            visible: App.history.count > 0

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS
                visible: page.selectionCount === 0

                Repeater {
                    // kind: the value written to HistoryFilterModel.kindFilter,
                    // matched against the entry's category. "Clips" is the
                    // instant-replay ring's output — a saved replay is an .mp4
                    // like any recording, so only the history entry's origin
                    // separates them.
                    model: [
                        { label: qsTr("All"),        kind: "" },
                        { label: qsTr("Images"),     kind: "image" },
                        { label: qsTr("GIFs"),       kind: "gif" },
                        { label: qsTr("Recordings"), kind: "video" },
                        { label: qsTr("Clips"),      kind: "replay" }
                    ]
                    delegate: UFilterChip {
                        text: modelData.label
                        checked: filter.kindFilter === modelData.kind
                        onClicked: filter.kindFilter = modelData.kind
                    }
                }
                Rectangle {
                    width: 1; height: 18; color: Theme.divider
                    anchors.verticalCenter: parent.verticalCenter
                }
                UFilterChip {
                    text: qsTr("Starred")
                    iconName: "star-filled"
                    checked: filter.favoritesOnly
                    onClicked: filter.favoritesOnly = !filter.favoritesOnly
                }
                UFilterChip {
                    text: qsTr("Uploaded")
                    iconName: "globe"
                    checked: filter.uploadedOnly
                    onClicked: filter.uploadedOnly = !filter.uploadedOnly
                }
            }

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS
                visible: page.selectionCount > 0

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1 selected").arg(page.selectionCount)
                    color: Theme.accent
                    font.pixelSize: Theme.fontM
                    font.weight: Font.DemiBold
                }
                Item { width: Theme.spacingS; height: 1 }
                UButton {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true; variant: "tonal"; iconName: "star-filled"; text: qsTr("Star")
                    onClicked: App.history.setFavoriteByIds(page.selectedIds(), true)
                }
                UButton {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true; variant: "tonal"; iconName: "star"; text: qsTr("Unstar")
                    onClicked: App.history.setFavoriteByIds(page.selectedIds(), false)
                }
                UButton {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true; variant: "tonal"; iconName: "content-copy"; text: qsTr("Copy paths")
                    onClicked: {
                        var ids = page.selectedIds(), paths = []
                        for (var i = 0; i < ids.length; ++i) {
                            var e = App.history.entryById(ids[i])
                            if (e.filePath) paths.push(e.filePath)
                        }
                        if (paths.length === 0) {
                            App.showToast(qsTr("Nothing to copy: the selected captures were never saved"))
                            return
                        }
                        App.copyText(paths.join("\n"))
                        App.showToast(qsTr("Copied %1 file paths").arg(paths.length))
                    }
                }
                UButton {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true; variant: "tonal"; iconName: "upload-cloud"; text: qsTr("Upload")
                    onClicked: {
                        var ids = page.selectedIds(), n = 0
                        for (var i = 0; i < ids.length; ++i) {
                            var e = App.history.entryById(ids[i])
                            if (e.filePath) { App.uploadFromHistory(e.filePath); ++n }
                        }
                        if (n === 0)
                            App.showToast(qsTr("Nothing to upload: the selected captures were never saved"))
                    }
                }
                UButton {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true; variant: "tonal"; iconName: "document-save"; text: qsTr("Export ZIP")
                    onClicked: App.exportEntriesToZipDialog(page.selectedIds())
                }
                UButton {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true; variant: "tonal"; iconName: "edit-delete"; text: qsTr("Delete")
                    onClicked: deleteSelectedConfirm.open()
                }
                UButton {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true; variant: "ghost"; text: qsTr("Cancel")
                    onClicked: page.clearSelection()
                }
            }
        }

        // ---- empty states ----
        // "No captures yet" and "nothing matches your filter" are different
        // problems and the second one needs a way out.
        Text {
            visible: App.history.count === 0
            text: qsTr("Nothing here yet. Captures and recordings will appear here with thumbnails.")
            color: Theme.textTertiary
            font.pixelSize: Theme.fontM
        }
        Column {
            visible: App.history.count > 0 && filter.count === 0
            spacing: Theme.spacingM
            Text {
                text: qsTr("No capture matches the current search and filters.")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontM
            }
            UButton {
                text: qsTr("Reset filters")
                variant: "tonal"
                compact: true
                onClicked: {
                    searchField.text = ""
                    filter.searchText = ""
                    filter.kindFilter = ""
                    filter.favoritesOnly = false
                    filter.uploadedOnly = false
                }
            }
        }

        // ---- grid ----
        Item {
            width: parent.width
            height: parent.height - y

            GridView {
                id: grid
                anchors.fill: parent
                clip: true
                model: filter
                boundsBehavior: Flickable.StopAtBounds
                focus: true
                // Fill the width evenly instead of leaving a ragged gutter at the
                // right edge: derive the column count from a minimum tile width
                // and hand the remainder back to the cells.
                readonly property int columns: Math.max(1, Math.floor(width / 250))
                cellWidth: Math.floor(width / columns)
                cellHeight: Math.round(cellWidth * 0.56) + page.nameLine + page.metaLine + page.tileChrome

                MiddleScroll { flickable: grid }
                WheelBoost { flickable: grid }

                // Click past the tiles = deselect, like a file manager.
                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: page.clearSelection()
                }

                Keys.onPressed: (e) => {
                    var cur = grid.currentItem
                    if (e.key === Qt.Key_Escape) {
                        if (page.selectionCount > 0) page.clearSelection()
                        else if (searchField.text !== "") { searchField.text = ""; filter.searchText = "" }
                        e.accepted = true
                    } else if (e.key === Qt.Key_A && (e.modifiers & Qt.ControlModifier)) {
                        page.selectIds(filter.entryIds())
                        e.accepted = true
                    } else if (e.key === Qt.Key_Space && cur) {
                        page.toggleSelect(cur.entryIdValue)
                        page.anchorRow = grid.currentIndex
                        e.accepted = true
                    } else if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && cur) {
                        page.activate(cur.kindValue, cur.filePathValue)
                        e.accepted = true
                    } else if (e.key === Qt.Key_C && (e.modifiers & Qt.ControlModifier) && cur) {
                        page.copyOne(cur.kindValue, cur.filePathValue, cur.urlValue)
                        e.accepted = true
                    } else if (e.key === Qt.Key_Delete) {
                        // Selection first: Delete on a staged batch must not
                        // silently delete only the focused tile instead.
                        if (page.selectionCount > 0) deleteSelectedConfirm.open()
                        else if (cur) App.history.removeByIds([cur.entryIdValue])
                        e.accepted = true
                    }
                }

                delegate: Item {
                    id: tile
                    width: grid.cellWidth
                    height: grid.cellHeight

                    // The keyboard handlers read the focused tile's entry through
                    // these (GridView.currentItem is this Item).
                    readonly property var entryIdValue: entryId
                    readonly property string kindValue: kind
                    readonly property string filePathValue: filePath
                    readonly property string urlValue: url

                    readonly property bool selected: page.isSelected(entryId)
                    readonly property bool current: grid.activeFocus && grid.currentIndex === index
                    // file:// URL of the thumbnail image, shared by the tile image
                    // and the drag pixmap.
                    readonly property string thumbUrl: thumbnail !== ""
                        ? "file://" + encodeURI(thumbnail).replace(/[?#]/g, encodeURIComponent)
                        : ""

                    Rectangle {
                        id: card
                        anchors.fill: parent
                        anchors.margins: 6
                        radius: Theme.radiusL
                        color: cardHover.hovered || tile.selected ? Theme.surfaceHi : Theme.surface
                        border.width: tile.selected || tile.current ? 2 : 1
                        border.color: tile.selected ? Theme.accent
                                    : tile.current ? Theme.alpha(Theme.accent, 0.6)
                                    : cardHover.hovered ? Theme.alpha(Theme.accent, 0.45) : Theme.divider
                        clip: true
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                        // HoverHandler (not a MouseArea): child button MouseAreas
                        // don't steal its hover, so the action strip stays up
                        // while the pointer is over a button.
                        HoverHandler { id: cardHover }

                        Column {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 6

                            Rectangle {
                                id: thumb
                                width: parent.width
                                height: Math.round(grid.cellWidth * 0.56)
                                radius: Theme.radiusM
                                color: Theme.primary
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: tile.thumbUrl
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                    sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                                }

                                // Drag the capture file out to another app — a file
                                // manager, or a video editor like LosslessCut for
                                // recordings. Cheap: the payload + MouseArea sit
                                // idle until a real drag gesture crosses the
                                // threshold, so it costs nothing while browsing.
                                // Declared before the badges/star/strip so those
                                // (higher in the stack) keep their clicks.
                                Item {
                                    id: dragProxy
                                    width: 1; height: 1
                                    Drag.active: dragArea.drag.active
                                    Drag.dragType: Drag.Automatic
                                    Drag.supportedActions: Qt.CopyAction
                                    Drag.mimeData: { "text/uri-list": App.fileDragUri(filePath) }
                                    Drag.imageSource: tile.thumbUrl
                                }
                                MouseArea {
                                    id: dragArea
                                    anchors.fill: parent
                                    cursorShape: filePath !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    drag.target: filePath !== "" ? dragProxy : null
                                    drag.threshold: 8
                                    onReleased: { dragProxy.x = 0; dragProxy.y = 0 }
                                    onClicked: (e) => {
                                        grid.currentIndex = index
                                        grid.forceActiveFocus()
                                        if (e.modifiers & Qt.ShiftModifier) {
                                            // Range from the last plain click; with
                                            // no anchor yet, this click becomes it.
                                            if (page.anchorRow >= 0)
                                                page.selectIds(filter.entryIdsBetween(page.anchorRow, index))
                                            else
                                                page.toggleSelect(entryId)
                                            page.anchorRow = index
                                        } else if (e.modifiers & Qt.ControlModifier) {
                                            page.toggleSelect(entryId)
                                            page.anchorRow = index
                                        } else if (page.selectionCount > 0) {
                                            // A staged batch turns plain clicks into
                                            // selection toggles — opening a preview
                                            // mid-batch would be a surprise, and the
                                            // batch is one Escape away.
                                            page.toggleSelect(entryId)
                                            page.anchorRow = index
                                        } else {
                                            page.activate(kind, filePath)
                                        }
                                    }
                                }

                                // Kind badge: bottom-left, clear of the checkbox and
                                // the star, and hidden while the action strip owns
                                // that edge.
                                Rectangle {
                                    visible: kind !== "image" && !strip.shown
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    anchors.margins: 6
                                    width: kindText.implicitWidth + 14
                                    height: 20
                                    radius: 10
                                    color: Theme.accent
                                    Text {
                                        id: kindText
                                        anchors.centerIn: parent
                                        text: kind.toUpperCase()
                                        color: Theme.textOnAccent
                                        font.pixelSize: 10
                                        font.weight: Font.Bold
                                    }
                                }

                                // Selection checkbox: top-left. Shown on hover, and
                                // pinned while a batch is staged so the selection
                                // stays readable without the pointer.
                                Rectangle {
                                    z: 2
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.margins: 6
                                    width: 22; height: 22; radius: 6
                                    visible: tile.selected || cardHover.hovered || page.selectionCount > 0
                                    color: tile.selected ? Theme.accent : Qt.rgba(0, 0, 0, 0.5)
                                    border.width: 1
                                    border.color: tile.selected ? Theme.accent : Qt.rgba(1, 1, 1, 0.6)
                                    UIcon {
                                        anchors.centerIn: parent
                                        visible: tile.selected
                                        name: "checkmark"
                                        size: 14
                                        color: Theme.textOnAccent
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            page.toggleSelect(entryId)
                                            page.anchorRow = index
                                        }
                                    }
                                }

                                // Star: top-right, dark scrim disc so it reads on
                                // any capture.
                                Rectangle {
                                    z: 2
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    anchors.margins: 6
                                    width: 26; height: 26; radius: 13
                                    color: Qt.rgba(0, 0, 0, 0.5)
                                    visible: favorite || cardHover.hovered
                                    UIcon {
                                        anchors.centerIn: parent
                                        name: favorite ? "star-filled" : "star"
                                        size: 15
                                        color: favorite ? Theme.accent : "#FFFFFF"
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: App.history.setFavoriteByIds([entryId], !favorite)
                                    }
                                }

                                // Action strip: slides up along the bottom edge on
                                // hover. The old full-tile scrim blacked out the
                                // very thumbnail you were aiming at and squeezed
                                // five controls into the middle of it.
                                Item {
                                    id: strip
                                    readonly property bool shown: cardHover.hovered && page.selectionCount === 0
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 38
                                    clip: true
                                    visible: opacity > 0
                                    opacity: strip.shown ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: Theme.animFast } }

                                    Rectangle {
                                        width: parent.width
                                        height: parent.height
                                        y: strip.shown ? 0 : parent.height
                                        Behavior on y { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.0) }
                                            GradientStop { position: 0.45; color: Qt.rgba(0, 0, 0, 0.65) }
                                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.85) }
                                        }

                                        Row {
                                            anchors.centerIn: parent
                                            anchors.verticalCenterOffset: 3
                                            spacing: 2
                                            UIconButton {
                                                iconName: kind === "image" ? "content-copy" : "document-open"
                                                tooltip: kind === "image" ? qsTr("Copy image") : qsTr("Copy file path")
                                                iconSize: 16; width: 30; height: 30
                                                enabled: filePath !== ""
                                                onClicked: page.copyOne(kind, filePath, url)
                                            }
                                            UIconButton {
                                                iconName: "globe"
                                                tooltip: qsTr("Copy link")
                                                iconSize: 16; width: 30; height: 30
                                                enabled: url !== ""
                                                onClicked: { App.copyText(url); App.showToast(qsTr("Link copied")) }
                                            }
                                            UIconButton {
                                                iconName: "folder-open"
                                                tooltip: qsTr("Open file")
                                                iconSize: 16; width: 30; height: 30
                                                enabled: filePath !== ""
                                                onClicked: App.openFile(filePath)
                                            }
                                            UIconButton {
                                                iconName: "edit"
                                                tooltip: qsTr("Edit (overwrites the file on save)")
                                                iconSize: 16; width: 30; height: 30
                                                visible: kind === "image" && filePath !== ""
                                                onClicked: App.editFromHistory(filePath)
                                            }
                                            UIconButton {
                                                iconName: "cut"
                                                tooltip: qsTr("Trim recording")
                                                iconSize: 16; width: 30; height: 30
                                                visible: (kind === "video" || kind === "gif") && filePath !== ""
                                                onClicked: App.openTrimRecording(filePath)
                                            }
                                            UMenuButton {
                                                iconOnly: true; iconName: "view-more"
                                                tooltip: qsTr("More")
                                                width: 30; height: 30
                                                actions: {
                                                    var a = []
                                                    if (kind === "image" && filePath !== "") {
                                                        a.push({ label: qsTr("File path"), iconName: "document-open",
                                                                 trigger: function() { App.copyAsFromHistory(filePath, url, "path") } })
                                                        a.push({ label: qsTr("Markdown image"), iconName: "text-marked",
                                                                 trigger: function() { App.copyAsFromHistory(filePath, url, "markdown") } })
                                                        a.push({ label: qsTr("HTML image"), iconName: "text-html",
                                                                 trigger: function() { App.copyAsFromHistory(filePath, url, "html") } })
                                                    }
                                                    if (filePath !== "")
                                                        a.push({ label: qsTr("Upload"), iconName: "upload-cloud", separatorBefore: a.length > 0,
                                                                 trigger: function() { App.uploadFromHistory(filePath) } })
                                                    if (App.qrAvailable && url !== "")
                                                        a.push({ label: qsTr("Show QR code"), iconName: "view-preview", separatorBefore: a.length > 0,
                                                                 trigger: function() { App.showQr(url) } })
                                                    if (kind === "image" && filePath !== "")
                                                        a.push({ label: qsTr("Pin as floating preview"), iconName: "window-pin",
                                                                 trigger: function() { App.previewFromHistory(filePath) } })
                                                    if (App.ocrAvailable && kind === "image" && filePath !== "")
                                                        a.push({ label: qsTr("Copy text (OCR)"), iconName: "ocr",
                                                                 trigger: function() { App.ocrFile(filePath) } })
                                                    a.push({ label: favorite ? qsTr("Starred. Unstar to allow deleting")
                                                                             : qsTr("Delete (moves file to trash)"),
                                                             iconName: "edit-delete", enabled: !favorite, separatorBefore: true,
                                                             trigger: function() { App.history.removeByIds([entryId]) } })
                                                    return a
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // Both footer lines are pinned to the height the cell
                            // was sized for, so a taller-than-expected line can
                            // never push the other one out of the card.
                            Text {
                                width: parent.width
                                height: page.nameLine
                                verticalAlignment: Text.AlignVCenter
                                text: url !== "" ? url
                                     : filePath !== "" ? filePath.split("/").pop() : qsTr("(not saved)")
                                color: url !== "" ? Theme.accent : Theme.textSecondary
                                font.pixelSize: Theme.fontS
                                elide: Text.ElideMiddle
                            }
                            Text {
                                width: parent.width
                                height: page.metaLine
                                verticalAlignment: Text.AlignVCenter
                                // Time, then whatever the file itself can tell us.
                                // An unsaved capture contributes neither, so the
                                // line collapses to the timestamp.
                                text: {
                                    var bits = []
                                    if (timestamp) bits.push(page.tileTime(timestamp))
                                    if (dimensions !== "") bits.push(dimensions)
                                    if (fileSizeText !== "") bits.push(fileSizeText)
                                    return bits.join(" · ")
                                }
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontS - 1
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            // Sticky date group of whichever row is topmost right now. GridView
            // has no sections, and regrouping the model into per-group rows would
            // re-instantiate every visible tile on each star toggle — this reads
            // the same information off the scroll position instead.
            Rectangle {
                id: datePill
                anchors.top: parent.top
                anchors.topMargin: Theme.spacingS
                // Centered, not cornered: the tiles keep their checkbox in the
                // top-left corner and their star in the top-right one, and the
                // pill would sit on top of whichever it shared a corner with.
                anchors.horizontalCenter: parent.horizontalCenter
                readonly property int topRow: grid.count > 0
                    ? grid.indexAt(grid.cellWidth / 2, grid.contentY + 4) : -1
                readonly property string label: topRow >= 0 ? page.dateGroup(filter.timestampAt(topRow)) : ""
                visible: datePill.label !== ""
                width: dateText.implicitWidth + 20
                height: 24
                radius: 12
                color: Theme.alpha(Theme.backgroundDeep, 0.85)
                border.width: 1
                border.color: Theme.divider
                Text {
                    id: dateText
                    anchors.centerIn: parent
                    text: datePill.label
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontS
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
