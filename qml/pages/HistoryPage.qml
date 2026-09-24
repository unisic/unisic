import QtQuick
import QtQuick.Window
import Unisic
import Unisic.Kit
import "../components"

Item {
    id: page

    // Read by Main.qml (window.modalOpen): drag-and-drop and Ctrl+V are off
    // while a confirmation is up, or a dropped file would open an editor
    // window behind it.
    //
    // `visible`, NOT `opened` - the same rule (and reason) as the window-level
    // guard in Main.qml: a Popup is on screen and modal from the moment open()
    // is called, but only reports `opened` once its enter transition has
    // FINISHED, so `opened` leaves the drop zone live while the dialog is
    // already covering the window. `visible` covers the whole on-screen life,
    // fade-out included, which is the side to err on.
    readonly property bool modalOpen: clearAllConfirm.visible || deleteSelectedConfirm.visible

    // Also read by Main.qml (window.pageDragOut): true only while a tile is
    // being dragged OUT of Unisic. The tiles start a REAL system drag, which
    // the compositor offers back to our own window, so the window-level drop
    // target has to stand down until the drag finishes - otherwise dragging a
    // capture to another app covers the window with the "drop to open" overlay
    // and releasing inside it opens an editor for the file being exported.
    property bool dragOutActive: false

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

    // ---- spoken identity of a tile ----
    // The grid delegates are Items with a thumbnail and two elided lines; a
    // screen reader gets none of that for free, so the whole identity is
    // assembled here and set as the tile's Accessible.name.
    function kindLabel(kind) {
        if (kind === "image") return qsTr("Image")
        if (kind === "gif") return qsTr("GIF")
        if (kind === "video") return qsTr("Recording")
        return kind
    }
    function tileName(kind, filePath, url, timestamp) {
        var who = url !== "" ? url
                : filePath !== "" ? filePath.split("/").pop()
                : qsTr("(not saved)")
        var when = page.tileTime(timestamp)
        //: Spoken name of a history tile: file name or link, kind, time.
        return when !== "" ? qsTr("%1, %2, %3").arg(who).arg(page.kindLabel(kind)).arg(when)
                           //: Spoken name of a history tile with no timestamp: file name or link, kind.
                           : qsTr("%1, %2").arg(who).arg(page.kindLabel(kind))
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
        text: qsTr("This moves the selected capture files to the trash.\n\nStarred (favorite) captures in the selection are kept - un-star them first.")
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
        // Title on the left, actions on the right, and NOTHING between them
        // that can grow into the other side: at the 880 px minimum window the
        // history body is 580 px and the two blocks measured 598-636 px in
        // Spanish, French, Italian and Polish - the count text was drawn under
        // the search field. The search field gives width back instead (it is
        // the only elastic control here), and the count is bounded by the
        // actions so the worst case elides rather than overlaps.
        Item {
            id: historyHeader
            width: parent.width
            height: 40

            readonly property real leftWidth:
                titleText.implicitWidth
                + (countText.visible ? Theme.spacingM + countText.implicitWidth : 0)

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
                id: countText
                anchors.left: titleText.right
                anchors.leftMargin: Theme.spacingM
                anchors.right: historyActions.left
                anchors.rightMargin: Theme.spacingM
                anchors.baseline: titleText.baseline
                elide: Text.ElideRight
                visible: App.history.count > 0
                text: filter.filtering ? qsTr("%1 of %2").arg(filter.count).arg(App.history.count)
                                       : qsTr("%1 items").arg(App.history.count)
                color: Theme.textTertiary
                font.pixelSize: Theme.fontS
            }

            Row {
                id: historyActions
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS

                UTextField {
                    id: searchField
                    // 220 when the window can afford it, never below 130 - the
                    // placeholder is elided by the field itself, and a search
                    // box narrower than that stops being usable.
                    width: Math.max(130, Math.min(220, historyHeader.width - historyHeader.leftWidth
                                                  - Theme.spacingM - clearAllButton.width - Theme.spacingS
                                                  - (clearSearchButton.visible
                                                     ? clearSearchButton.width + Theme.spacingS : 0)))
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "magnify"
                    placeholder: qsTr("Search name or link")
                    // edited() is user input only, so every programmatic reset
                    // below clears filter.searchText itself.
                    onEdited: (t) => filter.searchText = t.trim()
                    Keys.onEscapePressed: { searchField.text = ""; filter.searchText = "" }
                }
                UIconButton {
                    id: clearSearchButton
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "close"
                    iconSize: 14
                    visible: searchField.text !== ""
                    tooltip: qsTr("Clear search")
                    onClicked: { searchField.text = ""; filter.searchText = "" }
                }
                UButton {
                    id: clearAllButton
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
            id: filterBar
            width: parent.width
            // Follows the chips when they wrap: seven translated chip labels
            // measure 568 px against a 580 px body at the 880 px minimum window
            // (Polish), so one more chip or one longer translation would have
            // been clipped away by the page's Flickable, which has no
            // horizontal scroll.
            height: Math.max(32, filterFlow.visible ? filterFlow.implicitHeight : 0)
            visible: App.history.count > 0

            Flow {
                id: filterFlow
                anchors.left: parent.left
                anchors.right: parent.right
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
                // Carried in a chip-tall Item: a Flow positions its children
                // itself, so the hairline cannot centre itself with an anchor.
                Item {
                    width: 1; height: 28
                    Rectangle { anchors.centerIn: parent; width: 1; height: 18; color: Theme.divider }
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
                // Icon-only, not labelled: with seven actions the labelled row
                // overflowed the window in longer languages (Polish), pushing
                // Export ZIP and Delete off the right edge. Tooltips keep them
                // discoverable while the row fits in every language.
                UIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "star-filled"; tooltip: qsTr("Star"); iconSize: 16; width: 32; height: 32
                    onClicked: App.history.setFavoriteByIds(page.selectedIds(), true)
                }
                UIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "star"; tooltip: qsTr("Unstar"); iconSize: 16; width: 32; height: 32
                    onClicked: App.history.setFavoriteByIds(page.selectedIds(), false)
                }
                UIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "content-copy"; tooltip: qsTr("Copy paths"); iconSize: 16; width: 32; height: 32
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
                UIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "upload-cloud"; tooltip: qsTr("Upload"); iconSize: 16; width: 32; height: 32
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
                UIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "document-save"; tooltip: qsTr("Export ZIP"); iconSize: 16; width: 32; height: 32
                    onClicked: App.exportEntriesToZipDialog(page.selectedIds())
                }
                UIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "edit-delete"; tooltip: qsTr("Delete"); iconSize: 16; width: 32; height: 32
                    onClicked: deleteSelectedConfirm.open()
                }
                UIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "close"; tooltip: qsTr("Cancel"); iconSize: 16; width: 32; height: 32
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
                // The GRID is the tab stop, not its tiles. Every other control
                // on the page is one now, so without this Tab walked straight
                // past the grid and there was no way back into it from the
                // keyboard. Landing here hands over to the arrow/Space/Return
                // handling below; Tab is not accepted by any of it, so the next
                // Tab leaves again and the delegates (deliberately not tab
                // stops - see the tile) stay out of the chain.
                activeFocusOnTab: true
                // Fill the width evenly instead of leaving a ragged gutter at the
                // right edge: derive the column count from a minimum tile width
                // and hand the remainder back to the cells.
                readonly property int columns: Math.max(1, Math.floor(width / 250))
                cellWidth: Math.floor(width / columns)
                cellHeight: Math.round(cellWidth * 0.56) + page.nameLine + page.metaLine + page.tileChrome

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Capture history")
                Accessible.description: qsTr("Arrow keys move, Space selects, Return opens, Delete removes")

                MiddleScroll { flickable: grid }
                WheelBoost {
                    flickable: grid
                    // The kit's 220 px default is written for a page of short
                    // rows; here a row is a whole tile (213 px at the default
                    // window, 223 px at 1920), so a notch moved exactly one
                    // row - a quarter of the viewport maximized, a sixth at
                    // 1440p, which is what "far too slow" felt like. A row and
                    // a half is the floor, and once about four and a half rows
                    // fit the viewport takes over, so the big window stops
                    // crawling while the small one still keeps ~30% of the
                    // previous view. WheelBoost caps a boosted notch at 90% of
                    // the viewport by itself, so nothing here can skip a row
                    // nobody saw.
                    stepPx: Math.max(Math.round(grid.cellHeight * 1.5), Math.round(grid.height / 3))
                }

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

                    // Deliberately NOT a tab stop: the GridView owns keyboard
                    // navigation (currentIndex + its own Keys handler), and a
                    // per-tile tab stop would fight it - Tab would walk a
                    // hundred captures one by one and the arrow keys would then
                    // move a different cursor than the one the ring shows.
                    // AT-SPI still enumerates and can Press every tile.
                    Accessible.role: Accessible.ListItem
                    Accessible.name: page.tileName(kind, filePath, url, timestamp)
                    Accessible.description: favorite ? qsTr("Starred") : ""
                    Accessible.selectable: true
                    Accessible.selected: tile.selected
                    Accessible.onPressAction: page.activate(kind, filePath)

                    Rectangle {
                        id: card
                        anchors.fill: parent
                        anchors.margins: 6
                        radius: Theme.radiusL
                        color: card.hovering || tile.selected ? Theme.surfaceHi : Theme.surface
                        border.width: tile.selected || tile.current ? 2 : 1
                        border.color: tile.selected ? Theme.accent
                                    : tile.current ? Theme.alpha(Theme.accent, 0.6)
                                    : card.hovering ? Theme.alpha(Theme.accent, 0.45) : Theme.divider
                        clip: true
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                        // HoverHandler (not a MouseArea): child button MouseAreas
                        // don't steal its hover, so the action strip stays up
                        // while the pointer is over a button.
                        HoverHandler { id: cardHover }

                        // The More menu renders outside the card. Count an open
                        // menu as hover so the card's colour feedback does not
                        // flicker while the pointer moves onto the popup.
                        readonly property bool hovering: cardHover.hovered || moreMenu.menuOpen

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
                                    // Raise the window's drag guard for exactly
                                    // the drag's lifetime. These two bracket the
                                    // blocking QDrag inside Qt (dragStarted is
                                    // emitted immediately before it, dragFinished
                                    // immediately after), so the guard is up
                                    // before the first drag event can reach our
                                    // own DropArea and down again the moment the
                                    // drop resolves. Neither MouseArea.pressed
                                    // nor Drag.active can stand in for them: the
                                    // platform drag takes the pointer grab, so
                                    // both can fall to false while the drag is
                                    // still in flight.
                                    Drag.onDragStarted: page.dragOutActive = true
                                    Drag.onDragFinished: page.dragOutActive = false
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

                                // Kind badge: bottom-left, clear of the persistent
                                // action strip as well as the checkbox and star.
                                Rectangle {
                                    visible: kind !== "image"
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: strip.height + 4
                                    anchors.left: parent.left
                                    anchors.leftMargin: 6
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
                                        // Already spoken as part of the tile name.
                                        Accessible.ignored: true
                                    }
                                }

                                // Selection checkbox: top-left and always present,
                                // so touch and pointer users see the same affordance.
                                Rectangle {
                                    id: selectBox
                                    z: 2
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.margins: 6
                                    width: 22; height: 22; radius: 6
                                    color: tile.selected ? Theme.accent
                                                         : Theme.alpha(Theme.mediaBase, 0.5)
                                    border.width: 1
                                    border.color: tile.selected ? Theme.accent
                                                                : Theme.alpha(Theme.mediaText, 0.6)
                                    UIcon {
                                        anchors.centerIn: parent
                                        visible: tile.selected
                                        name: "checkmark"
                                        size: 14
                                        color: Theme.textOnAccent
                                    }
                                    function toggle() {
                                        page.toggleSelect(entryId)
                                        page.anchorRow = index
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: selectBox.toggle()
                                    }
                                    // Reachable from assistive tech, but out of
                                    // the tab chain like its tile: the grid's
                                    // own Space already toggles the selection.
                                    Accessible.role: Accessible.CheckBox
                                    Accessible.name: qsTr("Select this capture")
                                    Accessible.checkable: true
                                    Accessible.checked: tile.selected
                                    Accessible.onToggleAction: selectBox.toggle()
                                    Accessible.onPressAction: selectBox.toggle()
                                }

                                // Star: top-right, dark scrim disc so it reads on
                                // any capture.
                                Rectangle {
                                    id: starBox
                                    z: 2
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    anchors.margins: 6
                                    width: 26; height: 26; radius: 13
                                    color: Theme.alpha(Theme.mediaBase, 0.5)
                                    function toggle() { App.history.setFavoriteByIds([entryId], !favorite) }
                                    UIcon {
                                        anchors.centerIn: parent
                                        name: favorite ? "star-filled" : "star"
                                        size: 15
                                        color: favorite ? Theme.accent : Theme.mediaText
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: starBox.toggle()
                                    }
                                    Accessible.role: Accessible.CheckBox
                                    Accessible.name: qsTr("Star this capture")
                                    Accessible.description: qsTr("Starred captures survive Clear all")
                                    Accessible.checkable: true
                                    Accessible.checked: favorite
                                    Accessible.onToggleAction: starBox.toggle()
                                    Accessible.onPressAction: starBox.toggle()
                                }

                                // Action strip: permanently occupies the bottom
                                // edge. Batch-selection mode disables it in place
                                // rather than hiding controls under the pointer.
                                Item {
                                    id: strip
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 38
                                    clip: true
                                    enabled: page.selectionCount === 0
                                    opacity: enabled ? 1 : 0.42
                                    Behavior on opacity { NumberAnimation { duration: Theme.animFast } }

                                    Rectangle {
                                        width: parent.width
                                        height: parent.height
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: Theme.alpha(Theme.mediaBase, 0.0) }
                                            GradientStop { position: 0.45; color: Theme.alpha(Theme.mediaBase, 0.65) }
                                            GradientStop { position: 1.0; color: Theme.alpha(Theme.mediaBase, 0.85) }
                                        }

                                        // NOTHING in this strip is a tab stop.
                                        // The GridView deliberately owns keyboard
                                        // navigation, so per-tile controls must not
                                        // splice dozens of stops into the tab order.
                                        // (Measured: a VISIBLE control with
                                        // activeFocusOnTab true takes the Tab;
                                        // with false the Tab skips straight
                                        // past it.) The keyboard route to these
                                        // actions is the grid itself - Return
                                        // opens, Ctrl+C copies, Delete trashes,
                                        // Space stages a selection - and the
                                        // selection bar that staging reveals,
                                        // which IS in the chain (star, unstar,
                                        // copy paths, upload, export, delete).
                                        // AT-SPI is unaffected: its Press
                                        // action needs no focus.
                                        Row {
                                            anchors.centerIn: parent
                                            anchors.verticalCenterOffset: 3
                                            spacing: 2
                                            UIconButton {
                                                activeFocusOnTab: false
                                                iconName: kind === "image" ? "content-copy" : "document-open"
                                                tooltip: kind === "image" ? qsTr("Copy image") : qsTr("Copy file path")
                                                iconSize: 16; width: 30; height: 30
                                                enabled: filePath !== ""
                                                onClicked: page.copyOne(kind, filePath, url)
                                            }
                                            UIconButton {
                                                activeFocusOnTab: false
                                                iconName: "globe"
                                                tooltip: qsTr("Copy link")
                                                iconSize: 16; width: 30; height: 30
                                                enabled: url !== ""
                                                onClicked: { App.copyText(url); App.showToast(qsTr("Link copied")) }
                                            }
                                            UIconButton {
                                                activeFocusOnTab: false
                                                iconName: "folder-open"
                                                tooltip: qsTr("Open file")
                                                iconSize: 16; width: 30; height: 30
                                                enabled: filePath !== ""
                                                onClicked: App.openFile(filePath)
                                            }
                                            UIconButton {
                                                activeFocusOnTab: false
                                                iconName: "edit"
                                                tooltip: qsTr("Edit (overwrites the file on save)")
                                                iconSize: 16; width: 30; height: 30
                                                visible: kind === "image" && filePath !== ""
                                                onClicked: App.editFromHistory(filePath)
                                            }
                                            UIconButton {
                                                activeFocusOnTab: false
                                                iconName: "cut"
                                                tooltip: qsTr("Trim recording")
                                                iconSize: 16; width: 30; height: 30
                                                visible: (kind === "video" || kind === "gif") && filePath !== ""
                                                onClicked: App.openTrimRecording(filePath)
                                            }
                                            UMenuButton {
                                                id: moreMenu
                                                activeFocusOnTab: false
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
                                                    if (url !== "")
                                                        a.push({ label: qsTr("Show QR code"), iconName: "view-preview", separatorBefore: a.length > 0,
                                                                 trigger: function() { App.showQr(url) } })
                                                    if (kind === "image" && filePath !== "")
                                                        a.push({ label: qsTr("Pin as floating preview"), iconName: "window-pin",
                                                                 trigger: function() { App.previewFromHistory(filePath) } })
                                                    if (kind === "image" && filePath !== "")
                                                        a.push({ label: qsTr("Copy text (OCR)"), iconName: "ocr",
                                                                 trigger: function() { App.ocrFile(filePath) } })
                                                    // Each conversion writes a second file beside the
                                                    // original, so the tile it makes is its own. The
                                                    // format the file already is stays out of the list
                                                    // (it would be a copy under another name), and GIF
                                                    // is greyed with the reason when ffmpeg is absent,
                                                    // because Qt writes no GIF.
                                                    if (kind === "image" && filePath !== "") {
                                                        let own = filePath.substring(filePath.lastIndexOf(".") + 1).toLowerCase()
                                                        if (own === "jpeg")
                                                            own = "jpg"
                                                        const targets = [["png", "PNG"], ["jpg", "JPEG"],
                                                                         ["webp", "WebP"], ["gif", "GIF"]]
                                                        let first = true
                                                        for (let t = 0; t < targets.length; ++t) {
                                                            const code = targets[t][0]
                                                            if (code === own)
                                                                continue
                                                            const needsFfmpeg = code === "gif"
                                                            a.push({ label: qsTr("Convert to %1").arg(targets[t][1]),
                                                                     iconName: "document-save",
                                                                     separatorBefore: first && a.length > 0,
                                                                     enabled: !needsFfmpeg || App.ffmpegAvailable,
                                                                     hint: (needsFfmpeg && !App.ffmpegAvailable) ? qsTr("Needs ffmpeg") : "",
                                                                     trigger: function() { App.convertFileTo(filePath, code) } })
                                                            first = false
                                                        }
                                                    }
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
                            // never push the other one out of the card - and
                            // both are already folded into the tile's
                            // Accessible.name, so they opt out here instead of
                            // being read a second time.
                            Text {
                                width: parent.width
                                height: page.nameLine
                                verticalAlignment: Text.AlignVCenter
                                text: url !== "" ? url
                                     : filePath !== "" ? filePath.split("/").pop() : qsTr("(not saved)")
                                color: url !== "" ? Theme.accent : Theme.textSecondary
                                font.pixelSize: Theme.fontS
                                elide: Text.ElideMiddle
                                Accessible.ignored: true
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
                                Accessible.ignored: true
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
