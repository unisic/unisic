import QtQuick
import QtQuick.Controls as C
import Unisic
import Unisic.Kit

// The header's search: capture actions, pages, Settings categories and past
// captures from one field. Ctrl+K (Main.qml) focuses it. The results are a
// flyout under the field, but the caret stays in the field: typing goes on
// while Up/Down pick a row and Return runs it.
Rectangle {
    id: root
    objectName: "headerSearch"

    // Main.qml owns the page index and the Settings page.
    signal navigate(int page)
    // tab -1 opens Settings with `query` in its own search box.
    signal openSettings(int tab, string query)

    readonly property string query: input.text.trim()
    // Highlighted row: an index into `matches` first, then into the captures.
    property int current: 0
    readonly property int captureRows: Math.min(captures.count, 4)
    readonly property int total: matches.length + captureRows
    readonly property bool resultsOpen: popup.visible

    function focusField() {
        input.forceActiveFocus()
        input.selectAll()
    }
    function dismiss() {
        input.text = ""
        input.focus = false
        popup.close()
    }

    readonly property var everything: [
        { title: qsTr("Full screen"), sub: qsTr("Screenshot"), icon: "monitor", run: function () { App.captureFullScreen() } },
        { title: qsTr("Region"), sub: qsTr("Screenshot"), icon: "region", run: function () { App.captureRegion() } },
        { title: qsTr("Window"), sub: qsTr("Screenshot"), icon: "window", run: function () { App.captureWindow() } },
        { title: qsTr("Scrolling"), sub: qsTr("Screenshot"), icon: "view-more", run: function () { App.captureScroll() } },
        { title: qsTr("Copy text"), sub: qsTr("OCR from a region"), icon: "line-text-scan", run: function () { App.captureRegionOcr() } },
        { title: qsTr("Measure"), sub: qsTr("Distances and angles"), icon: "line-ruler", run: function () { App.captureMeasure() } },
        { title: qsTr("Record GIF"), sub: qsTr("Recording"), icon: "line-film", run: function () { App.startGifRegion() } },
        { title: qsTr("Record video"), sub: qsTr("Recording"), icon: "line-record", run: function () { App.startVideoRegion() } },
        { title: qsTr("Open image"), sub: qsTr("Editor"), icon: "line-image", run: function () { App.openFileForEditing("image") } },
        { title: qsTr("Paste from clipboard"), sub: qsTr("Editor"), icon: "line-clipboard", run: function () { App.pasteFromClipboard() } },
        { title: qsTr("Capture"), sub: qsTr("Page"), icon: "line-camera", run: function () { root.navigate(0) } },
        { title: qsTr("Record"), sub: qsTr("Page"), icon: "line-record", run: function () { root.navigate(1) } },
        { title: qsTr("Edit"), sub: qsTr("Page"), icon: "line-pencil", run: function () { root.navigate(2) } },
        { title: qsTr("History"), sub: qsTr("Page"), icon: "line-history", run: function () { root.navigate(3) } },
        { title: qsTr("Servers"), sub: qsTr("Page"), icon: "line-upload", run: function () { root.navigate(4) } },
        { title: qsTr("Settings"), sub: qsTr("Page"), icon: "line-sliders", run: function () { root.navigate(5) } },
    ]
    // Same order and gate as SettingsPage.tabNames: the index is the tab. The
    // disambiguation keeps "Capture" and "Recording" apart from the page and
    // the action group above, which translate differently.
    readonly property var settingsTabs: {
        var t = [qsTr("General", "Settings category"), qsTr("Capture", "Settings category"),
                 qsTr("Recording", "Settings category"), qsTr("Editor", "Settings category"),
                 qsTr("Saving", "Settings category"), qsTr("Notifications", "Settings category"),
                 qsTr("Appearance", "Settings category"), qsTr("Hotkeys", "Settings category")]
        if (App.devBuild)
            t.push(qsTr("Developer", "Settings category"))
        return t
    }

    // At most six actions and every matching Settings category, then one row
    // that hands the words to the Settings search, which looks inside the panes.
    readonly property var matches: {
        var q = root.query.toLowerCase()
        if (q === "")
            return []
        var out = []
        for (var i = 0; i < everything.length && out.length < 6; ++i) {
            var a = everything[i]
            if ((a.title + " " + a.sub).toLowerCase().indexOf(q) >= 0)
                out.push(a)
        }
        for (var t = 0; t < settingsTabs.length; ++t) {
            if (settingsTabs[t].toLowerCase().indexOf(q) >= 0)
                out.push({ title: settingsTabs[t], sub: qsTr("Settings"), icon: "line-sliders",
                           run: (function (tab) { return function () { root.openSettings(tab, "") } })(t) })
        }
        out.push({ title: qsTr("Search settings for \"%1\"").arg(root.query), sub: qsTr("Settings"), icon: "line-search",
                   run: function () { root.openSettings(-1, root.query) } })
        return out
    }
    onQueryChanged: current = 0

    function runCurrent() {
        if (current < matches.length) {
            var m = matches[current]
            dismiss()
            m.run()
            return
        }
        var row = captureList.itemAtIndex(current - matches.length)
        if (row)
            row.activate()
    }

    // One result row. Not a tab stop: the caret stays in the field and the
    // arrows move `highlighted`.
    component ResultRow: Rectangle {
        id: rowItem
        property string title
        property string sub
        property string icon
        property string thumb: ""
        property bool highlighted: false
        signal activated()
        signal hovered()
        height: 40
        radius: 7
        color: highlighted ? Theme.secondary : rowMouse.containsMouse ? Theme.surfaceTop : "transparent"
        Rectangle {
            id: rowTile
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 28; height: 28
            radius: 7
            color: Theme.secondary
            clip: true
            UIcon {
                anchors.centerIn: parent
                visible: rowThumb.status !== Image.Ready
                name: rowItem.icon
                size: 16
                color: Theme.accent
            }
            Image {
                id: rowThumb
                anchors.fill: parent
                source: rowItem.thumb
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize: Qt.size(56, 56)
            }
        }
        Text {
            anchors.left: rowTile.right
            anchors.leftMargin: 10
            anchors.right: rowSub.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: rowItem.title
            color: Theme.textPrimary
            font.pixelSize: 13
            font.weight: Font.Medium
            Accessible.ignored: true
        }
        Text {
            id: rowSub
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width * 0.4)
            elide: Text.ElideRight
            text: rowItem.sub
            color: Theme.textSecondary
            font.pixelSize: Theme.fontS
            Accessible.ignored: true
        }
        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: rowItem.hovered()
            onClicked: rowItem.activated()
        }
        Accessible.role: Accessible.ListItem
        Accessible.name: title + ", " + sub
        Accessible.selectable: true
        Accessible.selected: highlighted
        Accessible.onPressAction: rowItem.activated()
    }

    width: 440
    height: 30
    radius: 8
    color: Theme.surface
    border.width: 1
    border.color: Theme.alpha(Theme.accent, input.activeFocus ? 0.45 : fieldHover.hovered ? 0.24 : 0.10)
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
    HoverHandler { id: fieldHover; cursorShape: Qt.IBeamCursor }

    UIcon {
        id: glass
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        name: "line-search"
        size: 15
        color: Theme.textSecondary
    }
    TextInput {
        id: input
        objectName: "headerSearchInput"
        anchors.left: glass.right
        anchors.leftMargin: 8
        anchors.right: shortcutCap.visible ? shortcutCap.left : parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        clip: true
        color: Theme.textPrimary
        selectionColor: Theme.alpha(Theme.accent, 0.4)
        selectedTextColor: Theme.textPrimary
        font.pixelSize: 13
        selectByMouse: true
        maximumLength: 200
        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Search actions, settings and captures")
        Accessible.description: "Ctrl+K"

        // `text`, not root.query: that binding may not have caught up yet
        // inside this handler, and the first keystroke would close the list.
        onTextChanged: {
            const filled = text.trim() !== ""
            if (filled && activeFocus && !popup.opened)
                popup.open()
            else if (!filled)
                popup.close()
        }
        onActiveFocusChanged: if (!activeFocus) popup.close()

        Keys.onEscapePressed: root.dismiss()
        Keys.onDownPressed: if (root.total > 0) root.current = (root.current + 1) % root.total
        Keys.onUpPressed: if (root.total > 0) root.current = (root.current - 1 + root.total) % root.total
        Keys.onReturnPressed: if (root.total > 0) root.runCurrent()
        Keys.onEnterPressed: if (root.total > 0) root.runCurrent()

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            elide: Text.ElideRight
            visible: input.text === ""
            text: input.Accessible.name
            color: Theme.textSecondary
            font.pixelSize: 13
            Accessible.ignored: true
        }
    }
    Kbd {
        id: shortcutCap
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        visible: !input.activeFocus
        text: "Ctrl K"
    }

    // Live only while the flyout is up: closed, the proxy filters nothing.
    HistoryFilterModel {
        id: captures
        sourceModel: App.history
        searchText: popup.visible ? root.query : ""
    }

    C.Popup {
        id: popup
        // Anchored to the field (UFlyout rule 2). The header is the top of
        // the window, so the list always opens below it.
        parent: root
        margins: UFlyout.margin
        y: root.height + UFlyout.gap
        width: UFlyout.fitWidth(C.Overlay.overlay, Math.max(root.width, 360))
        readonly property real want: resultCol.implicitHeight + 2 * padding
        height: UFlyout.fitHeight(C.Overlay.overlay, want,
                                  UFlyout.roomOn(visible ? UFlyout.rooms(root, C.Overlay.overlay, want) : null, false))
        padding: 6
        focus: false
        closePolicy: C.Popup.CloseOnPressOutsideParent
        // A press outside closes the list; with words still in the field that
        // is a cancel, the same as Escape.
        onClosed: if (root.query !== "") root.dismiss()

        background: Rectangle {
            color: Theme.surface
            radius: 10
            border.width: 1
            border.color: Theme.alpha(Theme.accent, 0.18)
        }

        contentItem: Flickable {
            clip: true
            contentHeight: resultCol.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: resultCol
                width: popup.availableWidth

                Repeater {
                    model: root.matches
                    delegate: ResultRow {
                        required property var modelData
                        required property int index
                        width: resultCol.width
                        highlighted: root.current === index
                        onHovered: root.current = index
                        title: modelData.title
                        sub: modelData.sub
                        icon: modelData.icon
                        onActivated: { root.current = index; root.runCurrent() }
                    }
                }

                Text {
                    visible: root.captureRows > 0
                    leftPadding: 8
                    topPadding: 8
                    bottomPadding: 4
                    text: qsTr("Captures")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.88
                }

                // Four rows at most, and not interactive: the view builds those
                // four however long the history is.
                ListView {
                    id: captureList
                    width: parent.width
                    height: root.captureRows * 40
                    interactive: false
                    model: root.captureRows > 0 ? captures : null
                    delegate: ResultRow {
                        required property int index
                        required property string filePath
                        required property string thumbnail
                        required property string url
                        required property string kind
                        function activate() {
                            root.dismiss()
                            if (filePath === "") return
                            if (kind === "image") App.previewFromHistory(filePath)
                            else App.openFile(filePath)
                        }
                        visible: index < root.captureRows
                        width: captureList.width
                        highlighted: root.current === root.matches.length + index
                        onHovered: root.current = root.matches.length + index
                        title: filePath !== "" ? filePath.split("/").pop() : url
                        sub: kind === "gif" ? "GIF" : kind === "video" ? qsTr("Video") : qsTr("Screenshot")
                        icon: kind === "image" ? "image" : "camera-video"
                        thumb: thumbnail !== "" ? App.fileDragUri(thumbnail) : ""
                        onActivated: activate()
                    }
                }
            }
        }
    }
}
