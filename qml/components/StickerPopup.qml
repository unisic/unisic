import QtQuick
import QtQuick.Controls
import Unisic
import Unisic.Kit

// The Sticker tool's library: every Twemoji emoji (third_party/twemoji), one
// Unicode group at a time or every group at once through the search, in the
// skin tone picked under the grid. An in-scene Popup for the same reason
// UColorPopup is one. A pick goes to the canvas and to the front of the quick
// row in the tool's options (EditorWindow.qml).
Popup {
    id: root

    // For AnnotationCanvas::stickerArt (the art a cell shows is the file the
    // canvas stamps) and stickerCatalog.
    property var canvas: null
    signal picked(string sticker)

    // The quick row before anything has been picked from here: pointing
    // hands, verdicts and attention marks, what a screenshot gets stamped with.
    readonly property var quickDefaults: ["\u{1F449}", "\u{1F446}", "\u{1F447}", "\u{1F448}",
                                          "✅", "❌", "⚠️", "❓",
                                          "\u{1F4A1}", "⭐", "\u{1F44D}", "\u{1F600}"]

    // AnnotationCanvas::stickerCatalog, read on the first open: about 0.5 MB
    // of names and search words held for the editor window's life.
    property var catalog: null
    // Every sticker (each tone too) to its catalogue index, for openFor.
    property var lookup: ({})
    // Each group's catalogue indices.
    property var byGroup: []
    function load() {
        if (catalog || !canvas)
            return
        const c = canvas.stickerCatalog(Qt.uiLanguage)
        var at = {}
        var groups = c.groups.map(function () { return [] })
        for (var i = 0; i < c.emoji.length; ++i) {
            at[c.emoji[i]] = i
            const t = c.tones[i]
            if (t)
                for (var k = 0; k < t.length; ++k)
                    at[t[k]] = i
            groups[c.group[i]].push(i)
        }
        lookup = at
        byGroup = groups
        catalog = c
    }

    property int category: 0
    readonly property string query: search.text.trim().toLowerCase()
    readonly property int tone: App.settings.stickerTone
    // The grid's model: catalogue indices, every word of the query starting
    // a word of the name or keywords (the UI language's, then English), else
    // one group. A word start, not anywhere: "kot" (a cat) left alone brings
    // the disco ball too, its "dyskotekowa" holds it mid-word.
    readonly property var shown: {
        if (!catalog)
            return []
        if (query === "")
            return byGroup[category] || []
        const words = query.split(/\s+/)
        var out = []
        for (var i = 0; i < catalog.search.length; ++i) {
            const hay = " " + catalog.search[i]
            if (words.every(function (w) { return hay.indexOf(" " + w) >= 0 }))
                out.push(i)
        }
        return out
    }
    function stickerAt(i) {
        const t = catalog.tones[i]
        return tone > 0 && t ? t[tone - 1] : catalog.emoji[i]
    }
    function groupName(g) {
        switch (g) {
        case "Smileys & Emotion": return qsTr("Smileys")
        case "People & Body": return qsTr("People")
        case "Animals & Nature": return qsTr("Nature")
        case "Food & Drink": return qsTr("Food")
        case "Travel & Places": return qsTr("Travel")
        case "Activities": return qsTr("Activities")
        case "Objects": return qsTr("Objects")
        case "Symbols": return qsTr("Symbols")
        default: return qsTr("Flags")
        }
    }
    readonly property var toneNames: [qsTr("No skin tone"), qsTr("Light skin tone"),
                                      qsTr("Medium-light skin tone"), qsTr("Medium skin tone"),
                                      qsTr("Medium-dark skin tone"), qsTr("Dark skin tone")]

    readonly property int columns: 9
    readonly property real cell: 44
    // Fixed, so a short group or search never resizes the popup and moves
    // the chips under the pointer; the grid scrolls the rest.
    readonly property int rows: 7

    // Opens on the group that holds `sticker` (in any tone), with it current.
    function openFor(sticker) {
        load()
        search.text = ""
        const i = sticker in lookup ? lookup[sticker] : -1
        if (i >= 0)
            category = catalog.group[i]
        open()
        grid.currentIndex = Math.max(0, shown.indexOf(i))
    }
    function pick(i) {
        picked(stickerAt(i))
        close()
    }

    onOpened: search.forceFocus()

    parent: Overlay.overlay
    anchors.centerIn: parent
    // UFlyout rule 3, as in UColorPopup: `margins` keeps it inside the window
    // and the grid scrolls when the window is too short for all its rows.
    margins: UFlyout.margin
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.spacingL

    Overlay.modal: Rectangle { color: Theme.alpha(Theme.mediaBase, 0.25) }
    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
    }

    contentItem: Column {
        id: col
        spacing: Theme.spacingM
        // On the contentItem: the Accessible attached type binds to an Item,
        // and a Popup is not one.
        Accessible.role: Accessible.Grouping
        Accessible.name: qsTr("Stickers")

        // Return picks the current cell, Down walks into the grid.
        UTextField {
            id: search
            objectName: "stickerSearch"
            width: grid.width
            iconName: "line-search"
            placeholder: qsTr("Search stickers…")
            onTextChanged: grid.currentIndex = 0
            onAccepted: if (grid.count > 0) root.pick(root.shown[grid.currentIndex])
            Keys.onDownPressed: grid.forceActiveFocus()
        }

        Flow {
            id: tabs
            width: grid.width
            spacing: 6
            Repeater {
                model: root.catalog ? root.catalog.groups : []
                delegate: UFilterChip {
                    required property string modelData
                    required property int index
                    text: root.groupName(modelData)
                    checked: root.query === "" && root.category === index
                    onClicked: {
                        search.text = ""
                        root.category = index
                        grid.currentIndex = 0
                    }
                }
            }
        }

        GridView {
            id: grid
            objectName: "stickerGrid"
            width: root.columns * cellWidth
            readonly property real others: search.height + tabs.height + toneRow.height + 3 * col.spacing
            height: UFlyout.fitHeight(root.parent, root.rows * cellHeight + others
                                      + root.topPadding + root.bottomPadding)
                    - root.topPadding - root.bottomPadding - others
            cellWidth: root.cell
            cellHeight: root.cell
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.shown
            // One tab stop; the arrows walk the cells and Space or Enter picks
            // (the current cell holds the focus, ToolChip's own keys).
            activeFocusOnTab: true
            keyNavigationEnabled: true
            Accessible.role: Accessible.Grouping
            Accessible.name: root.query !== "" ? search.text
                             : root.catalog ? root.groupName(root.catalog.groups[root.category]) : ""
            Accessible.description: qsTr("Arrow keys move between the stickers, Space picks one")

            MiddleScroll { flickable: grid }
            WheelBoost { flickable: grid }

            delegate: ToolChip {
                id: chip
                required property int modelData
                readonly property string sticker: root.stickerAt(modelData)
                readonly property string art: root.canvas ? root.canvas.stickerArt(sticker) : ""
                activeFocusOnTab: false
                Accessible.focusable: true
                // The hover tip, and the spoken name in the user's language.
                label: root.catalog.names[modelData]
                // No art: the emoji font draws it, as the canvas would.
                icon: art === "" ? sticker : ""
                active: root.canvas !== null && root.canvas.sticker === sticker
                onClicked: root.pick(modelData)
                Image {
                    visible: chip.art !== ""
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    // Twice the drawn size, sharp on a 2x screen.
                    sourceSize: Qt.size(56, 56)
                    // Loaded in place, about 0.1 ms a cell: an asynchronous
                    // Image drops its picture while the next one loads, so a
                    // page flashed empty on open and on every skin-tone switch.
                    source: chip.art
                }
            }

            Text {
                anchors.centerIn: parent
                visible: grid.count === 0 && root.query !== ""
                text: qsTr("No sticker matches \"%1\"").arg(search.text.trim())
                color: Theme.textSecondary
                font.pixelSize: Theme.fontS
            }
        }

        Row {
            id: toneRow
            spacing: 4
            Text {
                anchors.verticalCenter: parent.verticalCenter
                rightPadding: Theme.spacingS
                text: qsTr("Skin tone")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontS
            }
            Repeater {
                // The raised hand in each tone.
                model: 6
                delegate: ToolChip {
                    id: toneChip
                    required property int index
                    readonly property var hand: root.catalog ? root.catalog.tones[root.lookup["✋"]] : null
                    readonly property string art: root.canvas && hand
                                                  ? root.canvas.stickerArt(index > 0 ? hand[index - 1] : "✋") : ""
                    objectName: "stickerTone" + index
                    width: 32
                    height: 32
                    label: root.toneNames[index]
                    active: root.tone === index
                    onClicked: App.settings.stickerTone = index
                    Image {
                        anchors.centerIn: parent
                        width: 20
                        height: 20
                        sourceSize: Qt.size(40, 40)
                        source: toneChip.art
                    }
                }
            }
        }
    }
}
