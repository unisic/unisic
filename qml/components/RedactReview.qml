import QtQuick
import QtQuick.Controls
import Unisic.Kit

// Auto-redact: the kinds of sensitive text the scan found, each with a count
// and a switch, and under a kind that is on, its findings one per row. A
// kind's switch covers every match of it (or takes the bars off again); a
// finding's switch takes that one out of the saved image (the editor keeps
// it outlined, so it stays findable) and puts it back. Each flip is one undo
// step. Anchored to the button that opened it, contained by UFlyout's rule.
Popup {
    id: root

    property var canvas: null
    // The scan's outcome while it is not running: EditorSession's status line.
    property string status: ""
    property bool busy: false
    property Item anchor: null
    property bool preferAbove: false

    // SensitiveText.h names the kinds. The three people reach for first lead.
    readonly property var kinds: ["ip", "email", "number", "secret", "card", "iban", "mac"]
    readonly property int kindHeight: 44
    readonly property int findingHeight: 36

    function openFrom(item, above) {
        anchor = item
        preferAbove = above
        refresh()
        open()
    }

    // In place: kind rows never go away, findings are inserted and removed
    // around them, so a flip keeps the scroll position and the focused switch.
    property real listWant: 0
    function refresh() {
        const counts = canvas ? canvas.sensitiveCounts() : ({})
        const found = canvas ? canvas.redactions() : []
        var want = []
        for (var k = 0; k < kinds.length; ++k) {
            const kind = kinds[k]
            const mine = found.filter(function (f) { return f.kind === kind })
            want.push({ rowType: "kind", kind: kind, found: counts[kind] || 0, on: mine.length > 0,
                        group: -1, text: "", covered: false })
            for (var f = 0; f < mine.length; ++f)
                want.push({ rowType: "finding", kind: kind, found: 0, on: false,
                            group: mine[f].group, text: mine[f].text, covered: mine[f].covered })
        }
        const key = function (r) { return r.rowType === "kind" ? "k" + r.kind : "f" + r.group }
        var h = 0
        var at = 0
        for (var i = 0; i < want.length; ++i) {
            const w = want[i]
            while (at < rows.count && rows.get(at).rowType === "finding" && key(rows.get(at)) !== key(w))
                rows.remove(at)
            if (at < rows.count && key(rows.get(at)) === key(w)) {
                rows.setProperty(at, "found", w.found)
                rows.setProperty(at, "on", w.on)
                rows.setProperty(at, "text", w.text)
                rows.setProperty(at, "covered", w.covered)
            } else {
                rows.insert(at, w)
            }
            h += w.rowType === "kind" ? kindHeight : findingHeight
            ++at
        }
        while (at < rows.count)
            rows.remove(at)
        listWant = h
    }

    function kindTitle(kind) {
        switch (kind) {
        case "secret": return qsTr("Passwords and keys")
        case "email": return qsTr("E-mail addresses")
        case "iban": return qsTr("IBANs")
        case "card": return qsTr("Card numbers")
        case "mac": return qsTr("MAC addresses")
        case "ip": return qsTr("IP addresses")
        default: return qsTr("Long numbers")
        }
    }
    function kindName(kind) {
        switch (kind) {
        case "secret": return qsTr("Password or key")
        case "email": return qsTr("E-mail")
        case "iban": return qsTr("IBAN")
        case "card": return qsTr("Card number")
        case "mac": return qsTr("MAC address")
        case "ip": return qsTr("IP address")
        default: return qsTr("Long number")
        }
    }

    ListModel { id: rows }

    // Deferred: the canvas announces the undo snapshot before it changes the
    // shapes, as the editor's shape list also knows.
    Connections {
        target: root.canvas
        enabled: root.visible
        function onHistoryChanged() { Qt.callLater(root.refresh) }
        function onOcrChanged() { Qt.callLater(root.refresh) }
    }

    readonly property Item _overlay: Overlay.overlay
    parent: anchor
    margins: UFlyout.margin
    width: UFlyout.fitWidth(_overlay, 360)
    // Right edges aligned; Qt's clamp (margins) keeps it inside the window.
    x: anchor ? anchor.width - width : 0

    property bool flyUp: false
    readonly property real flyWant: head.implicitHeight + col.spacing + listWant
                                    + topPadding + bottomPadding
    readonly property var flyFit: visible ? UFlyout.rooms(anchor, _overlay, flyWant) : null
    // Once open, UFlyout holds the side until less than minRoom is left. The
    // panel button moves down under an open list once the new bars reach the
    // inspector's shape list, so this list also changes sides when only the
    // other one shows every row.
    onFlyFitChanged: {
        flyUp = UFlyout.sideNow(flyUp, flyFit, opened, preferAbove)
        if (flyFit && flyFit.want > UFlyout.roomOn(flyFit, flyUp)
                && flyFit.want <= UFlyout.roomOn(flyFit, !flyUp))
            flyUp = !flyUp
    }
    height: UFlyout.fitHeight(_overlay, flyWant, UFlyout.roomOn(flyFit, flyUp))
    y: UFlyout.offsetY(root, anchor, flyUp)
    onAboutToShow: flyUp = UFlyout.sideAtOpen(anchor, _overlay, flyWant, preferAbove)

    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.spacingM

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
    }

    contentItem: Column {
        id: col
        spacing: Theme.spacingS
        Accessible.role: Accessible.Grouping
        Accessible.name: qsTr("Auto-redact")

        Column {
            id: head
            width: parent.width
            spacing: Theme.spacingXS
            Text {
                text: qsTr("Auto-redact")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontM
                font.weight: Font.DemiBold
            }
            Text {
                objectName: "redactReviewStatus"
                width: parent.width
                visible: text !== ""
                text: root.busy ? qsTr("Recognizing…") : root.status
                color: Theme.textSecondary
                font.pixelSize: Theme.fontS
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                visible: !root.busy
                text: qsTr("Switch on what to cover. A finding switched off stays visible in the saved image.")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontS
                wrapMode: Text.Wrap
            }
        }

        ListView {
            id: list
            objectName: "redactReviewList"
            width: parent.width
            height: Math.max(0, root.availableHeight - head.height - col.spacing)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: rows

            MiddleScroll { flickable: list }
            WheelBoost { flickable: list }

            delegate: Item {
                id: row
                required property int index
                required property string rowType
                required property string kind
                required property int found
                required property bool on
                required property int group
                required property string text
                required property bool covered
                readonly property bool isKind: rowType === "kind"
                objectName: isKind ? "redactKind_" + kind : "redactFinding"
                width: ListView.view.width
                height: isKind ? root.kindHeight : root.findingHeight

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: row.isKind ? 0 : Theme.spacingL
                    anchors.right: countText.left
                    anchors.rightMargin: Theme.spacingS
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.isKind ? root.kindTitle(row.kind) : row.text
                    color: row.isKind ? (row.found > 0 || row.on ? Theme.textPrimary : Theme.textTertiary)
                                      : (row.covered ? Theme.textPrimary : Theme.textTertiary)
                    font.pixelSize: row.isKind ? Theme.fontM : Theme.fontS
                    elide: row.isKind ? Text.ElideRight : Text.ElideMiddle
                }
                Text {
                    id: countText
                    anchors.right: toggle.left
                    anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.isKind && !root.busy
                    width: visible ? implicitWidth : 0
                    text: row.found > 0 ? qsTr("%n found", "", row.found) : qsTr("None found")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontS
                }
                USwitch {
                    id: toggle
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: row.isKind ? row.on : row.covered
                    enabled: !row.isKind || row.on || (row.found > 0 && !root.busy)
                    accessibleName: row.isKind ? root.kindTitle(row.kind)
                                               : root.kindName(row.kind) + ": " + row.text
                    accessibleDescription: row.isKind ? countText.text : ""
                    onToggled: (checked) => {
                        if (!row.isKind)
                            root.canvas.setRedactionCovered(row.group, checked)
                        else if (checked)
                            root.canvas.autoRedact([row.kind])
                        else
                            root.canvas.removeRedactions(row.kind)
                        Qt.callLater(root.refresh)
                    }
                }
                Rectangle {
                    visible: row.isKind && row.index > 0
                    width: parent.width
                    height: 1
                    color: Theme.divider
                }
            }
        }
    }
}
