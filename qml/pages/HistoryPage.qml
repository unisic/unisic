import QtQuick
import QtQuick.Effects
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
        else App.copyAsFromHistory(filePath, url, "file")
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

    // Midnight of the day `daysAgo` days back - the boundary both date labels
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
        // The UI language's standalone month name: Qt.formatDate names months
        // in English whatever the language, and a "MMMM" format gives the
        // genitive in Polish and Russian ("września", not "wrzesień").
        var m = Qt.locale(Qt.uiLanguage).standaloneMonthName(d.getMonth(), Locale.LongFormat)
        m = m.charAt(0).toUpperCase() + m.slice(1)
        return d.getFullYear() === new Date().getFullYear() ? m : m + " " + d.getFullYear()
    }

    // Tile timestamp. A full "yyyy-MM-dd HH:mm" spent most of the tile's one
    // meta line on a date the date heading over the grid already gives away, and
    // pushed the size and dimensions out of the tile - so the date shrinks as
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
            return d.toLocaleDateString(Qt.locale(Qt.uiLanguage), "d MMM") + " " + Qt.formatTime(d, "HH:mm")
        return d.toLocaleDateString(Qt.locale(Qt.uiLanguage), "d MMM yyyy")
    }

    HistoryFilterModel {
        id: filter
        sourceModel: App.history
    }

    // The tile's height is arithmetic over its parts, and a guessed constant for
    // the two footer lines was ~10 px short - so the meta line was cut off by the
    // card's clip. Measure the lines instead: this also survives a font or DPI
    // the guess was never checked against.
    FontMetrics { id: nameMetrics; font.pixelSize: 11; font.family: "monospace" }
    FontMetrics { id: metaMetrics; font.pixelSize: Theme.fontS }
    // Card margins (2×2) + its padding (2×6) + the 8 px gap under the
    // thumbnail + the 1 px between the two lines.
    readonly property int tileChrome: 4 + 12 + 8 + 1
    // Left and right of a thumbnail inside its cell: card margin + padding.
    readonly property int tileInset: 2 + 6
    // kind: the value written to HistoryFilterModel.kindFilter, matched
    // against the entry's category. "Clips" is the instant-replay ring's
    // output - a saved replay is an .mp4 like any recording, so only the
    // history entry's origin separates them.
    readonly property var kinds: [
        { label: qsTr("All"),        kind: "" },
        { label: qsTr("Images"),     kind: "image" },
        { label: qsTr("GIFs"),       kind: "gif" },
        { label: qsTr("Recordings"), kind: "video" },
        { label: qsTr("Clips"),      kind: "replay" },
        { label: qsTr("Sessions"),   kind: "session" }
    ]
    // The kind filter's segments share one width, the widest label's, so a
    // longer translation widens all six instead of eliding one.
    FontMetrics { id: segMetrics; font.pixelSize: Theme.fontS; font.weight: Font.DemiBold }
    readonly property int segWidth: {
        var w = 0
        for (var i = 0; i < page.kinds.length; ++i)
            w = Math.max(w, segMetrics.advanceWidth(page.kinds[i].label))
        return Math.ceil(w) + 28
    }

    // The default 1060x700 window: tighter top gap, as on Capture and Record.
    readonly property bool compact: height < 760
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
        anchors.leftMargin: 40
        anchors.rightMargin: 40
        anchors.topMargin: page.compact ? 20 : 28
        spacing: page.compact ? 18 : 24

        // ---- title + search + clear all ----
        // Title on the left, actions on the right, and NOTHING between them
        // that can grow into the other side: at the 880 px minimum window the
        // history body is narrow and the two blocks measured 598-636 px in
        // Spanish, French, Italian and Polish - the count text was drawn under
        // the search field. The search field gives width back instead (it is
        // the only elastic control here), and the title column is bounded by
        // the actions so the worst case elides rather than overlaps.
        Item {
            id: historyHeader
            width: parent.width
            height: Math.max(titleCol.height, historyActions.height)

            readonly property real leftWidth: Math.max(titleText.implicitWidth, countText.implicitWidth)

            Column {
                id: titleCol
                anchors.left: parent.left
                anchors.right: historyActions.left
                anchors.rightMargin: 24
                anchors.bottom: parent.bottom
                spacing: 6
                PageTitle {
                    id: titleText
                    width: parent.width
                    text: qsTr("History")
                }
                // Kept in the layout when empty (no text), so the header does
                // not change height and shift the page when the last entry goes.
                Text {
                    id: countText
                    width: parent.width
                    elide: Text.ElideRight
                    text: App.history.count === 0 ? ""
                          : filter.filtering ? qsTr("%1 of %2").arg(filter.count).arg(App.history.count)
                          : qsTr("%1 items").arg(App.history.count)
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
            }

            Row {
                id: historyActions
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 8

                UTextField {
                    id: searchField
                    // 260 when the window can afford it, never below 130 - the
                    // placeholder is elided by the field itself, and a search
                    // box narrower than that stops being usable.
                    width: Math.max(130, Math.min(260, historyHeader.width - historyHeader.leftWidth
                                                  - 24 - clearAllButton.width - historyActions.spacing
                                                  - (clearSearchButton.visible
                                                     ? clearSearchButton.width + historyActions.spacing : 0)))
                    height: 36
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
                HeaderButton {
                    id: clearAllButton
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Clear all")
                    iconName: "line-trash"
                    enabled: App.history.count > 0
                    onActivated: clearAllConfirm.open()
                }
            }
        }

        // ---- filters / selection actions ----
        // One row, two states: filtering is pointless while a batch is staged,
        // and stacking both bars would cost a whole tile row of height.
        Item {
            id: filterBar
            width: parent.width
            // Follows the filters when they wrap: at the 880 px minimum window
            // the kind segments and the two toggles run close to the body
            // width in the longer languages, and anything past it would be
            // clipped away, since nothing here scrolls sideways.
            height: Math.max(36, filterFlow.visible ? filterFlow.implicitHeight : 0)
            visible: App.history.count > 0

            Flow {
                id: filterFlow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                visible: page.selectionCount === 0

                SegmentedControl {
                    width: 8 + page.kinds.length * page.segWidth + (page.kinds.length - 1) * 2
                    accessibleName: qsTr("Capture type")
                    labels: page.kinds.map(function (k) { return k.label })
                    currentIndex: page.kinds.findIndex(function (k) { return k.kind === filter.kindFilter })
                    onPicked: (i) => filter.kindFilter = page.kinds[i].kind
                }
                // A wider gap between the pick-one segments and the two
                // independent toggles, so they do not read as one control.
                Item { width: 4; height: 36 }
                FilterToggle {
                    text: qsTr("Starred")
                    iconName: "star-filled"
                    checked: filter.favoritesOnly
                    onToggled: filter.favoritesOnly = !filter.favoritesOnly
                }
                FilterToggle {
                    text: qsTr("Uploaded")
                    iconName: "globe"
                    checked: filter.uploadedOnly
                    onToggled: filter.uploadedOnly = !filter.uploadedOnly
                }
            }

            // The selection bar takes the filters' place, as a raised strip in
            // the header buttons' colours so it reads as a different state.
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: selectionRow.width + 2 * 12
                height: 36
                radius: 9
                color: Theme.surface
                border.width: 1
                border.color: Theme.alpha(Theme.accent, 0.32)
                visible: page.selectionCount > 0

                Row {
                    id: selectionRow
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("%1 selected").arg(page.selectionCount)
                        color: Theme.accent
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Item { width: 8; height: 1 }
                    // Icon-only, not labelled: with seven actions the labelled row
                    // overflowed the window in longer languages (Polish), pushing
                    // Export ZIP and Delete off the right edge. Tooltips keep them
                    // discoverable while the row fits in every language.
                    UIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "star-filled"; tooltip: qsTr("Star"); iconSize: 16; width: 30; height: 30
                        onClicked: App.history.setFavoriteByIds(page.selectedIds(), true)
                    }
                    UIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "star"; tooltip: qsTr("Unstar"); iconSize: 16; width: 30; height: 30
                        onClicked: App.history.setFavoriteByIds(page.selectedIds(), false)
                    }
                    UIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "content-copy"; tooltip: qsTr("Copy paths"); iconSize: 16; width: 30; height: 30
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
                        iconName: "upload-cloud"; tooltip: qsTr("Upload"); iconSize: 16; width: 30; height: 30
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
                        iconName: "document-save"; tooltip: qsTr("Export ZIP"); iconSize: 16; width: 30; height: 30
                        onClicked: App.exportEntriesToZipDialog(page.selectedIds())
                    }
                    UIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "edit-delete"; tooltip: qsTr("Delete"); iconSize: 16; width: 30; height: 30
                        onClicked: deleteSelectedConfirm.open()
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 1; height: 18
                        color: Theme.alpha(Theme.accent, 0.18)
                    }
                    UIconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "close"; tooltip: qsTr("Cancel"); iconSize: 16; width: 30; height: 30
                        onClicked: page.clearSelection()
                    }
                }
            }
        }

        // ---- grid ----
        Item {
            width: parent.width
            height: parent.height - y

            // Sticky date group of whichever row is topmost right now, as the
            // section label over the grid. GridView has no sections, and
            // regrouping the model into per-group rows would re-instantiate
            // every visible tile on each star toggle - this reads the same
            // information off the scroll position instead. The label keeps
            // its line when empty, so the grid never shifts under it.
            SectionLabel {
                id: dateHeading
                width: parent.width
                height: Math.ceil(dateMetrics.height)
                elide: Text.ElideRight
                readonly property int topRow: grid.count > 0
                    ? grid.indexAt(grid.cellWidth / 2, grid.contentY + 4) : -1
                text: topRow >= 0 ? page.dateGroup(filter.timestampAt(topRow)) : ""
                FontMetrics { id: dateMetrics; font: dateHeading.font }
            }

            // One corner mask for every thumbnail (they share a size), as in
            // RecentStrip: an Image cannot round its own corners.
            Rectangle {
                id: thumbMask
                width: grid.cellWidth - 2 * page.tileInset - 2
                height: grid.thumbH - 2
                radius: 7
                visible: false
                layer.enabled: true
            }

            // "No captures yet" and "nothing matches your filter" are different
            // problems and the second one needs a way out.
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                y: Math.round(parent.height * 0.28)
                width: Math.min(parent.width, 520)
                spacing: 14
                // Over the grid: its click-to-deselect area fills the view
                // and would take the Reset button's click.
                z: 1
                visible: App.history.count === 0 || filter.count === 0

                UIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: App.history.count === 0 ? "line-history" : "line-search"
                    size: 32
                    color: Theme.textTertiary
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: App.history.count === 0
                          ? qsTr("Nothing here yet. Captures and recordings will appear here with thumbnails.")
                          : qsTr("No capture matches the current search and filters.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                UButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    // Only the no-match state has filters to reset.
                    visible: App.history.count > 0
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

            GridView {
                id: grid
                // Wider than the column by a tile's inset on each side, so the
                // thumbnails (not their invisible cells) line up with the
                // header above.
                x: -page.tileInset
                width: parent.width + 2 * page.tileInset
                anchors.top: dateHeading.bottom
                anchors.topMargin: 10
                anchors.bottom: parent.bottom
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
                // 220 gives four columns at the default window, the thumbnails
                // about Recent's size on the Capture page, and still leaves
                // the action strip room for all its buttons.
                readonly property int columns: Math.max(1, Math.floor(width / 220))
                cellWidth: Math.floor(width / columns)
                // A 16:10 thumbnail, as in Recent, and the two lines under it.
                readonly property int thumbH: Math.round((cellWidth - 2 * page.tileInset) * 10 / 16)
                cellHeight: thumbH + page.nameLine + page.metaLine + page.tileChrome

                Accessible.role: Accessible.List
                Accessible.name: qsTr("Capture history")
                Accessible.description: qsTr("Arrow keys move, Space selects, Return opens, Delete removes")

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
                    // URL of the thumbnail image, shared by the tile image and the
                    // drag pixmap. Built in C++: "file://" + "C:/..." makes "C" a
                    // host, a UNC share Windows tries to reach over SMB.
                    readonly property url thumbUrl: App.fileDragUri(thumbnail)

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

                    // Recent's tile on the Capture page, plus the History
                    // controls: no card around a tile at rest, only the
                    // bordered thumbnail and the two lines under it. The card
                    // appears as a tint when the tile is selected and as a
                    // ring while the grid's keyboard cursor is on it.
                    Rectangle {
                        id: card
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 10
                        color: tile.selected ? Theme.alpha(Theme.accent, 0.10) : "transparent"
                        border.width: 2
                        border.color: tile.current ? Theme.alpha(Theme.accent, 0.6) : "transparent"
                        clip: true
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                        // HoverHandler (not a MouseArea): child button MouseAreas
                        // don't steal its hover, so the hover colour stays up
                        // while the pointer is over a button.
                        HoverHandler { id: cardHover }

                        // The More menu renders outside the card. Count an open
                        // menu as hover so the card's colour feedback does not
                        // flicker while the pointer moves onto the popup.
                        readonly property bool hovering: cardHover.hovered || moreMenu.menuOpen

                        Column {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 8

                            Rectangle {
                                id: thumb
                                width: parent.width
                                height: grid.thumbH
                                radius: 8
                                color: Theme.background
                                border.width: 1
                                border.color: Theme.alpha(Theme.accent, card.hovering ? 0.45 : 0.12)
                                clip: true
                                Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                                Image {
                                    id: thumbImage
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    source: tile.thumbUrl
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                    sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                                    // Only once loaded: a layer per tile still
                                    // waiting for its file would be an empty
                                    // texture for nothing.
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
                                    anchors.verticalCenterOffset: -strip.height / 2
                                    visible: thumbImage.status !== Image.Ready
                                    name: kind === "image" ? "image" : "camera-video"
                                    size: 24
                                    color: Theme.textTertiary
                                }
                                // The selection ring sits over the picture, not
                                // in the thumbnail's own border: widening that
                                // border would shrink the image inside it and
                                // reload it at the new size on every toggle.
                                Rectangle {
                                    anchors.fill: parent
                                    radius: parent.radius
                                    color: "transparent"
                                    border.width: 2
                                    border.color: tile.selected ? Theme.accent : "transparent"
                                    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
                                }
                                // Drag the capture file out to another app - a file
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
                                            // selection toggles - opening a preview
                                            // mid-batch would be a surprise, and the
                                            // batch is one Escape away.
                                            page.toggleSelect(entryId)
                                            page.anchorRow = index
                                        } else {
                                            page.activate(kind, filePath)
                                        }
                                    }
                                }

                                // One tag at most, Recent's rule: the format for
                                // a recording, the upload for a still. Beside
                                // the checkbox, clear of the star and of the
                                // action strip.
                                Rectangle {
                                    readonly property string badge: kind === "gif" ? "GIF"
                                        : kind === "video" ? (filePath !== "" ? filePath.split(".").pop() : kind)
                                        : url !== "" ? qsTr("Uploaded") : ""
                                    visible: badge !== ""
                                    x: 6 + 22 + 6
                                    y: 6 + Math.round((22 - height) / 2)
                                    width: Math.min(kindText.implicitWidth + 12, thumb.width - x - 6 - 26 - 6)
                                    height: kindText.implicitHeight + 4
                                    radius: 4
                                    color: Theme.alpha(Theme.mediaBase, 0.72)
                                    Text {
                                        id: kindText
                                        anchors.centerIn: parent
                                        width: Math.min(implicitWidth, parent.width - 12)
                                        elide: Text.ElideRight
                                        text: parent.badge
                                        color: Theme.mediaText
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.4
                                        font.capitalization: Font.AllUppercase
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
                                    // Inside the thumbnail's 1 px border.
                                    anchors.margins: 1
                                    height: 38
                                    clip: true
                                    enabled: page.selectionCount === 0
                                    opacity: enabled ? 1 : 0.42
                                    Behavior on opacity { NumberAnimation { duration: Theme.animFast } }

                                    // Rounded like the masked picture under it,
                                    // or its bottom corners would stick out
                                    // square; the top two fall in the fully
                                    // transparent end of the gradient.
                                    Rectangle {
                                        width: parent.width
                                        height: parent.height
                                        radius: 7
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
                                                iconName: "content-copy"
                                                tooltip: kind === "image" ? qsTr("Copy image") : qsTr("Copy file")
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
                                                // Unfilled like the icon buttons beside it: the
                                                // tonal pill read as the strip's one lit control.
                                                flat: true
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
                                                    if (kind === "image" && filePath !== "")
                                                        a.push({ label: qsTr("Print…"), iconName: "document-print",
                                                                 trigger: function() { App.printFromHistory(filePath) } })
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

                            // Recent's two lines: the name in monospace, the
                            // meta line under it.
                            Column {
                                width: parent.width
                                spacing: 1

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
                                    color: url !== "" ? Theme.accent : Theme.textPrimary
                                    font.pixelSize: 11
                                    font.family: "monospace"
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
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontS
                                    elide: Text.ElideRight
                                    Accessible.ignored: true
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
