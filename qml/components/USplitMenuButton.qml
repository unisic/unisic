import QtQuick
import QtQuick.Controls as C
import Unisic.Kit

// A split dropdown pill: left half activates the default action (e.g. Save),
// right half opens a flyout menu with related options (e.g. Save as…, Save as GIF).
// Both share a single tonal pill background with a subtle divider between them.
Rectangle {
    id: root

    readonly property Item _overlay: C.Overlay.overlay

    property string text: ""
    property string iconName: ""
    property string tooltip: ""
    property string dropdownTooltip: ""
    property bool compact: false
    property var actions: []
    readonly property bool menuOpen: popup.opened

    signal clicked()

    implicitHeight: compact ? 34 : 42
    implicitWidth: leftPart.implicitWidth + 1 + rightPart.implicitWidth
    radius: height / 2
    color: Theme.tertiary
    border.width: 1
    border.color: popup.opened ? Theme.accent : Theme.divider
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

    function _openMenu(fromKeyboard) {
        if (!root.enabled)
            return
        popup._kb = fromKeyboard === true
        popup.open()
    }

    function _toggleMenu() {
        if (!root.enabled)
            return
        popup.opened ? popup.close() : root._openMenu(false)
    }

    function _keyOpen(e, toggle) {
        if (!UKeys.claim(e))
            return
        if (!popup.opened)
            root._openMenu(true)
        else if (toggle)
            popup._dismiss()
        else
            popup._focusFirst()
    }

    // Measure the widest label+hint row for popup width.
    TextMetrics { id: _tmLabel; font.pixelSize: Theme.fontM; font.weight: Font.DemiBold }
    TextMetrics { id: _tmHint;  font.pixelSize: Theme.fontS }
    function measureContentWidth() {
        var w = 180
        for (var i = 0; i < actions.length; ++i) {
            var a = actions[i]
            _tmLabel.text = a.label || ""
            var lw = _tmLabel.width
            var hw = 0
            if (a.hint) { _tmHint.text = a.hint; hw = _tmHint.width + 24 }
            var iconw = (a.iconName !== undefined && a.iconName !== "") ? 27 : 0
            w = Math.max(w, 10 + iconw + lw + hw + 10 + 12)
        }
        return Math.ceil(w)
    }

    // LEFT half - primary action
    Rectangle {
        id: leftPart
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        implicitWidth: leftRow.implicitWidth + (root.compact ? 24 : 32)
        topLeftRadius: root.radius
        bottomLeftRadius: root.radius
        topRightRadius: 0
        bottomRightRadius: 0
        color: lm.pressed && root.enabled ? Qt.darker(Theme.tertiary, 1.08)
             : lm.containsMouse && root.enabled ? Qt.lighter(Theme.tertiary, 1.12)
             : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animFast } }

        Row {
            id: leftRow
            anchors.centerIn: parent
            spacing: 7
            UIcon {
                visible: root.iconName !== ""
                name: root.iconName
                size: 18
                color: Theme.textPrimary
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: root.text !== ""
                text: root.text
                font.pixelSize: Theme.fontM
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        MouseArea {
            id: lm
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                if (root.enabled)
                    root.clicked()
            }
        }

        UHoverTip {
            anchor: leftPart
            text: root.tooltip
            show: lm.containsMouse && root.tooltip !== ""
        }

        activeFocusOnTab: root.enabled
        Keys.onSpacePressed: (e) => UKeys.activate(e, function() { if (root.enabled) root.clicked() })
        Keys.onReturnPressed: (e) => UKeys.activate(e, function() { if (root.enabled) root.clicked() })
        Keys.onEnterPressed: (e) => UKeys.activate(e, function() { if (root.enabled) root.clicked() })

        Accessible.role: Accessible.Button
        Accessible.name: root.text !== "" ? root.text : root.tooltip
        Accessible.focusable: activeFocusOnTab
        Accessible.onPressAction: if (root.enabled) root.clicked()

        UFocusRing { inset: 1; hostRadius: root.radius }
    }

    // Divider line
    Rectangle {
        id: divider
        anchors.left: leftPart.right
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: parent.height - (root.compact ? 12 : 16)
        color: Theme.divider
    }

    // RIGHT half - dropdown menu trigger
    Rectangle {
        id: rightPart
        anchors.left: divider.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        implicitWidth: root.compact ? 28 : 34
        topRightRadius: root.radius
        bottomRightRadius: root.radius
        topLeftRadius: 0
        bottomLeftRadius: 0
        color: (popup.opened || (rm.containsMouse && !rm.pressed)) && root.enabled
             ? Qt.lighter(Theme.tertiary, 1.12)
             : (rm.pressed && root.enabled ? Qt.darker(Theme.tertiary, 1.08) : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.animFast } }

        UIcon {
            anchors.centerIn: parent
            name: "chevron-down"
            size: 14
            color: Theme.textSecondary
            rotation: popup.opened ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: Theme.animFast } }
        }

        MouseArea {
            id: rm
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: root._toggleMenu()
        }

        UHoverTip {
            anchor: rightPart
            text: root.dropdownTooltip !== "" ? root.dropdownTooltip : qsTr("More save options")
            show: rm.containsMouse
        }

        activeFocusOnTab: root.enabled
        Keys.onSpacePressed: (e) => root._keyOpen(e, true)
        Keys.onReturnPressed: (e) => root._keyOpen(e, true)
        Keys.onEnterPressed: (e) => root._keyOpen(e, true)
        Keys.onDownPressed: (e) => root._keyOpen(e, false)

        Accessible.role: Accessible.ButtonMenu
        Accessible.name: root.dropdownTooltip !== "" ? root.dropdownTooltip : qsTr("More save options")
        Accessible.focusable: activeFocusOnTab
        Accessible.onPressAction: root._toggleMenu()

        UFocusRing { inset: 1; hostRadius: root.radius }
    }

    // Click-away catcher
    Item {
        id: catcher
        parent: root._overlay
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0
        visible: popup.opened && parent !== null
        z: 999
        MouseArea { anchors.fill: parent; onClicked: popup.close(); onWheel: (w) => { popup.close(); w.accepted = false } }
    }

    // Dropdown popup
    C.Popup {
        id: popup
        parent: root
        margins: UFlyout.margin
        property real measuredWidth: 180
        width: UFlyout.fitWidth(root._overlay, Math.max(root.width, measuredWidth))

        property bool flyUp: true
        readonly property real flyWant: col.implicitHeight + 12
        readonly property var flyFit: popup.visible
                                      ? UFlyout.rooms(root, root._overlay, popup.flyWant)
                                      : null
        onFlyFitChanged: popup.flyUp = UFlyout.sideNow(popup.flyUp, popup.flyFit,
                                                       popup.opened, true)
        height: UFlyout.fitHeight(root._overlay, popup.flyWant,
                                  UFlyout.roomOn(popup.flyFit, popup.flyUp))
        y: UFlyout.offsetY(popup, root, popup.flyUp)
        z: catcher.z + 1
        padding: 6
        focus: true
        closePolicy: C.Popup.CloseOnEscape

        onAboutToShow: {
            measuredWidth = root.measureContentWidth()
            popup.flyUp = UFlyout.sideAtOpen(root, root._overlay, popup.flyWant, true)
        }

        property bool _kb: false
        onOpened: if (popup._kb) popup._focusFirst()
        onAboutToHide: popup._kb = false

        function _rows() {
            var out = []
            for (var i = 0; i < col.children.length; ++i) {
                var wrap = col.children[i]
                if (!wrap || !wrap.children)
                    continue
                for (var j = 0; j < wrap.children.length; ++j) {
                    var it = wrap.children[j]
                    if (it && it.activeFocusOnTab === true && it.visible === true)
                        out.push(it)
                }
            }
            return out
        }
        function _walk(from, delta) {
            const rows = popup._rows()
            if (rows.length === 0)
                return
            const i = rows.indexOf(from)
            const next = i < 0 ? (delta > 0 ? 0 : rows.length - 1)
                               : (i + delta + rows.length) % rows.length
            rows[next].forceActiveFocus(delta > 0 ? Qt.TabFocusReason : Qt.BacktabFocusReason)
            popup._reveal(rows[next])
        }
        function _focusFirst() { popup._walk(null, 1) }
        function _dismiss() {
            popup.close()
            rightPart.forceActiveFocus(Qt.OtherFocusReason)
        }

        function _reveal(item) {
            if (!item || menuFlick.contentHeight <= menuFlick.height)
                return
            let p = item
            while (p && p !== col)
                p = p.parent
            if (p !== col)
                return
            const top = item.mapToItem(col, 0, 0).y
            const bottom = top + item.height
            if (top < menuFlick.contentY)
                menuFlick.contentY = top
            else if (bottom > menuFlick.contentY + menuFlick.height)
                menuFlick.contentY = bottom - menuFlick.height
        }

        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animFast } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }

        background: Rectangle {
            radius: Theme.radiusM
            color: Theme.surfaceHi
            border.width: 1
            border.color: Theme.divider
        }

        contentItem: Flickable {
            id: menuFlick
            contentWidth: width
            contentHeight: col.implicitHeight
            clip: true
            focus: true
            Keys.onDownPressed: (e) => { if (UKeys.claim(e)) popup._walk(null, 1) }
            Keys.onUpPressed: (e) => { if (UKeys.claim(e)) popup._walk(null, -1) }
            Keys.onEscapePressed: (e) => { if (UKeys.claim(e)) popup._dismiss() }
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: col
                width: menuFlick.width
                spacing: 2
                Repeater {
                    model: root.actions
                    delegate: Column {
                        width: col.width
                        spacing: 2
                        Rectangle {
                            visible: modelData.separatorBefore === true
                            x: 6; width: parent.width - 12; height: 1; color: Theme.divider
                        }
                        Rectangle {
                            id: mRow
                            width: parent.width; height: 36; radius: Theme.radiusS
                            readonly property bool _on: modelData.enabled !== false
                            opacity: _on ? 1 : 0.4
                            color: rMouse.containsMouse && _on ? Theme.tertiary : "transparent"

                            function _fire() {
                                if (!mRow._on)
                                    return
                                const byKey = mRow.activeFocus
                                popup.close()
                                if (byKey)
                                    rightPart.forceActiveFocus(Qt.OtherFocusReason)
                                modelData.trigger()
                            }
                            function _keyWalk(e, forward) {
                                if (!UKeys.claim(e))
                                    return
                                popup._walk(mRow, forward ? 1 : -1)
                            }
                            activeFocusOnTab: mRow._on
                            Keys.onSpacePressed: (e) => UKeys.activate(e, mRow._fire)
                            Keys.onReturnPressed: (e) => UKeys.activate(e, mRow._fire)
                            Keys.onEnterPressed: (e) => UKeys.activate(e, mRow._fire)
                            Keys.onDownPressed: (e) => mRow._keyWalk(e, true)
                            Keys.onUpPressed: (e) => mRow._keyWalk(e, false)
                            Keys.onEscapePressed: (e) => UKeys.activate(e, popup._dismiss)

                            Accessible.role: Accessible.MenuItem
                            Accessible.name: modelData.label || ""
                            Accessible.description: modelData.hint || ""
                            Accessible.focusable: mRow.activeFocusOnTab
                            Accessible.onPressAction: mRow._fire()

                            UFocusRing { inset: 1 }

                            UIcon {
                                id: rowIcon
                                visible: modelData.iconName !== undefined && modelData.iconName !== ""
                                name: modelData.iconName || ""; size: 17; color: Theme.textPrimary
                                anchors.left: parent.left; anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                id: rowHint
                                visible: !!modelData.hint
                                anchors.right: parent.right; anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.hint || ""; color: Theme.textTertiary
                                font.pixelSize: Theme.fontS
                            }
                            Text {
                                text: modelData.label; color: Theme.textPrimary
                                font.pixelSize: Theme.fontM
                                elide: Text.ElideRight
                                anchors.left: rowIcon.visible ? rowIcon.right : parent.left
                                anchors.leftMargin: 10
                                anchors.right: rowHint.visible ? rowHint.left : parent.right
                                anchors.rightMargin: rowHint.visible ? 12 : 10
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            MouseArea {
                                id: rMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: mRow._on ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: mRow._fire()
                            }
                        }
                    }
                }
            }
        }
    }
}
