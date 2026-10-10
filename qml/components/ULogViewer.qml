import QtQuick
import QtQuick.Controls
import Unisic
import Unisic.Kit

// The activity log, read in place (General -> Activity log). Same Popup shell
// as USystemCheck. Shows the in-memory ring (already redacted, newest last),
// filters it, and copies what is shown - so a bug report no longer means
// digging the file out of ~/.local/state in a file manager.
//
// Live by polling, not by signal: DiagLog's message handler runs on whatever
// thread logged, and a signal per line would wake the GUI for every qDebug.
// While open this reads one counter a second and re-fetches the ring (at most
// 500 lines) only when it moved. Closed, the timer stops and nothing runs.
Popup {
    id: root

    parent: Overlay.overlay
    anchors.centerIn: parent
    margins: UFlyout.margin
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(860, parent ? parent.width - 2 * Theme.spacingXL : 860)
    height: Math.min(640, parent ? parent.height - 2 * Theme.spacingXL : 640)
    padding: Theme.spacingXL

    property var lines: []
    property string query: ""
    property bool problemsOnly: false
    property double revision: -1
    // Following the tail. Scrolled up, the view holds still: a ring that is
    // full drops its oldest line for every new one, so refreshing under a
    // reader would slide the line they are reading away from them.
    property bool following: true

    // "HH:mm:ss.zzz L ..." - the level letter is column 13 (DiagLog::record).
    function isProblem(line) {
        const l = line.charAt(13)
        return l === "W" || l === "C" || l === "F"
    }
    readonly property var shown: {
        const q = root.query.toLowerCase()
        if (q === "" && !root.problemsOnly)
            return root.lines
        return root.lines.filter(l => (!root.problemsOnly || root.isProblem(l))
                                      && (q === "" || l.toLowerCase().indexOf(q) >= 0))
    }

    function reload() {
        const rev = App.logRevision()
        if (rev === root.revision)
            return
        root.revision = rev
        const text = App.recentLog()
        root.lines = text === "" ? [] : text.split("\n")
    }

    onAboutToShow: { root.revision = -1; root.following = true; reload() }
    onShownChanged: if (root.following) Qt.callLater(() => logList.positionViewAtEnd())

    Timer {
        interval: 1000
        repeat: true
        running: root.visible && root.following
        onTriggered: root.reload()
    }

    Overlay.modal: Rectangle { color: Theme.modalScrim }

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
    }

    contentItem: Item {
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Activity log")

        Column {
            id: head
            width: parent.width
            spacing: Theme.spacingM

            Text {
                width: parent.width
                text: qsTr("Activity log")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontL
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: qsTr("The last few hundred lines from this run, with passwords, tokens and your home folder already removed. Nothing here is sent anywhere.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
                wrapMode: Text.WordWrap
            }
            Row {
                width: parent.width
                spacing: Theme.spacingS
                UTextField {
                    id: filterField
                    width: parent.width - problemsChip.width - countText.width - 2 * Theme.spacingS
                    iconName: "magnify"
                    placeholder: qsTr("Filter lines")
                    onEdited: (t) => root.query = t
                }
                UFilterChip {
                    id: problemsChip
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Warnings and errors")
                    checked: root.problemsOnly
                    accessibleDescription: qsTr("Show only warning, critical and fatal lines")
                    onClicked: root.problemsOnly = !root.problemsOnly
                }
                Text {
                    id: countText
                    anchors.verticalCenter: parent.verticalCenter
                    // Fixed width so the count ticking up never resizes the
                    // filter field under the pointer.
                    width: countMetrics.width
                    horizontalAlignment: Text.AlignRight
                    text: qsTr("%1 of %2").arg(root.shown.length).arg(root.lines.length)
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontS
                    TextMetrics { id: countMetrics; font: countText.font; text: qsTr("%1 of %2").arg(500).arg(500) }
                }
            }
        }

        Rectangle {
            id: listBox
            anchors.top: head.bottom
            anchors.topMargin: Theme.spacingM
            anchors.bottom: foot.top
            anchors.bottomMargin: Theme.spacingM
            width: parent.width
            radius: Theme.radiusM
            color: Theme.surfaceHi
            border.width: 1
            border.color: Theme.divider
            clip: true

            ListView {
                id: logList
                anchors.fill: parent
                anchors.margins: Theme.spacingS
                model: root.shown
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Log lines")
                ScrollBar.vertical: ScrollBar {}
                onMovementEnded: root.following = atYEnd
                onAtYEndChanged: if (atYEnd && !moving) root.following = true
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_End) { logList.positionViewAtEnd(); root.following = true; e.accepted = true }
                    else if (e.key === Qt.Key_Home) { logList.positionViewAtBeginning(); root.following = false; e.accepted = true }
                }

                MiddleScroll { flickable: logList }
                WheelBoost { flickable: logList }

                delegate: Text {
                    required property string modelData
                    width: logList.width - Theme.spacingS
                    text: modelData
                    wrapMode: Text.WrapAnywhere
                    color: root.isProblem(modelData) ? Theme.danger : Theme.textPrimary
                    font.family: "monospace"
                    font.pixelSize: Theme.fontS
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData
                }

                UFocusRing { hostRadius: listBox.radius }
            }

            Text {
                anchors.centerIn: parent
                visible: root.shown.length === 0
                text: root.lines.length === 0 ? qsTr("Nothing logged yet.") : qsTr("No line matches the filter.")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontM
            }
        }

        Item {
            id: foot
            anchors.bottom: parent.bottom
            width: parent.width
            height: footRow.height

            Text {
                anchors.left: parent.left
                anchors.right: footRow.left
                anchors.rightMargin: Theme.spacingM
                anchors.verticalCenter: parent.verticalCenter
                text: root.following ? qsTr("Live") : qsTr("Paused while scrolled up. Press End to follow.")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontS
                elide: Text.ElideRight
            }
            Row {
                id: footRow
                anchors.right: parent.right
                spacing: Theme.spacingS
                UButton {
                    text: qsTr("Show log file")
                    iconName: "folder-open"
                    variant: "ghost"
                    compact: true
                    onClicked: App.showLogInFileManager()
                }
                UButton {
                    text: qsTr("Copy shown lines")
                    iconName: "edit-copy"
                    variant: "tonal"
                    compact: true
                    enabled: root.shown.length > 0
                    onClicked: { App.copyText(root.shown.join("\n")); App.showToast(qsTr("%n log line(s) copied", "", root.shown.length)) }
                }
                UButton {
                    text: qsTr("Close")
                    variant: "filled"
                    compact: true
                    onClicked: root.close()
                }
            }
        }
    }
}
