import QtQuick
import QtQuick.Effects
import QtQuick.Window
import Unisic.Kit

// "Recent" at the foot of the Capture, Record and Edit pages: the newest history
// entries of `model` (App.history or a HistoryFilterModel over it), newest
// first, in as many whole rows as `available` leaves room for, never so many
// that the page scrolls. `available` is the height from this strip's top to
// the bottom of the page's viewport.
//
// Strings keep the CapturePage context: they moved out of that file together
// with their translations.
Column {
    id: root

    property var model
    property real available: 0
    property string listName
    property string emptyText
    // capturePageCheck() (F8) finds the Capture page's grid by this name.
    property string gridName: ""
    // The Edit page opens an entry to work on it: through AppContext::openPath,
    // the one router, so a still lands in the editor and a recording in the
    // trim window. Off on Capture and Record, where a still opens the preview.
    property bool openToEdit: false
    signal openHistory()

    spacing: 12

    // Refreshes the "2 min ago" lines while the page stays open.
    property date now: new Date()
    Timer {
        interval: 60000
        repeat: true
        running: root.visible && root.Window.window !== null && root.Window.window.visible
        onTriggered: root.now = new Date()
    }

    function ago(ts) {
        if (!ts || isNaN(ts.getTime()))
            return ""
        var mins = Math.floor((root.now.getTime() - ts.getTime()) / 60000)
        var startOfToday = new Date(root.now.getFullYear(), root.now.getMonth(), root.now.getDate())
        if (mins < 1)
            return qsTranslate("CapturePage", "Just now")
        if (mins < 60)
            //: Age of a capture in Recent, minutes. Short form, no plural.
            return qsTranslate("CapturePage", "%1 min ago").arg(mins)
        if (ts >= startOfToday)
            //: Age of a capture in Recent, hours. Short form, no plural.
            return qsTranslate("CapturePage", "%1 h ago").arg(Math.floor(mins / 60))
        if (ts >= new Date(startOfToday.getTime() - 86400000))
            return qsTranslate("CapturePage", "Yesterday")
        return ts.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    Item {
        id: recentHead
        width: parent.width
        height: Math.max(recentLabel.height, historyLink.height)

        SectionLabel {
            id: recentLabel
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTranslate("CapturePage", "Recent")
        }
        Link {
            id: historyLink
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: qsTranslate("CapturePage", "Open History")
            onActivated: root.openHistory()
        }
    }

    // Not interactive and no cache buffer, so the view builds the shown rows
    // and part of the next however long the history is.
    Item {
        width: parent.width
        height: recentList.rows * (recentList.baseH + recentList.spacing) - recentList.spacing

        GridView {
            id: recentList
            objectName: root.gridName
            // Wider by one gap: a cell is a tile plus the gap after it, so the
            // last tile ends on the column's edge.
            width: parent.width + spacing
            height: parent.height
            interactive: false
            cacheBuffer: 0
            model: root.model
            readonly property int spacing: 14
            // Narrower tiles, up to six to a row, rather than one row cut by
            // the bottom of the window.
            readonly property int fitCellW: Math.floor((room - 8 - 40) * 16 / 10)
            readonly property int perRow: Math.max(1, Math.floor(width / (150 + spacing)),
                fitCellW > 80 ? Math.min(6, Math.ceil(width / (fitCellW + spacing))) : 0)
            readonly property int cellW: Math.floor(width / perRow) - spacing
            // A 16:10 thumbnail and the two lines under it.
            readonly property int thumbH: Math.round(cellW * 10 / 16)

            // One mask for every thumbnail (they share a size): an Image
            // cannot round its own corners.
            Rectangle {
                id: thumbMask
                width: recentList.cellW - 2
                height: recentList.thumbH - 2
                radius: 7
                visible: false
                layer.enabled: true
            }
            readonly property int baseH: thumbH + 8 + 40
            readonly property real room: root.available - recentHead.height - root.spacing
            readonly property int fitRows: Math.max(1, Math.floor((room + spacing) / (baseH + spacing)))
            readonly property int rows: Math.max(1, Math.min(fitRows, Math.ceil(count / perRow)))
            cellWidth: width / perRow
            cellHeight: baseH + spacing
            Accessible.role: Accessible.List
            Accessible.name: root.listName

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
                                                 : url !== "" ? url : qsTranslate("CapturePage", "(not saved)")
                readonly property string meta: {
                    var parts = [root.ago(timestamp), dimensions !== "" ? dimensions : fileSizeText]
                    return parts.filter(function (p) { return p !== "" }).join(" · ")
                }
                // One tag at most: the kind for a recording, the upload for a
                // still.
                readonly property string badge: kind === "gif" ? "GIF"
                    : kind === "video" ? filePath.split(".").pop().toUpperCase()
                    : url !== "" ? qsTranslate("CapturePage", "Uploaded") : ""
                // The view also builds part of the row after the last whole
                // one (GridView looks ahead by up to a row whatever the cache
                // buffer); it stays hidden and never loads its thumbnail.
                readonly property bool shown: index < recentList.perRow * recentList.rows

                function activate() {
                    if (filePath === "") return
                    if (root.openToEdit) App.openPath(filePath)
                    else if (kind === "image") App.previewFromHistory(filePath)
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
            visible: root.model ? root.model.count === 0 : true
            text: root.emptyText
            color: Theme.textTertiary
            font.pixelSize: Theme.fontM
        }
    }
}
