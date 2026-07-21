import QtQuick
import QtQuick.Controls
import Unisic

// "What's new" sheet shown when the version label is clicked. Renders the
// running version's section of the bundled CHANGELOG.md (App.changelog(lang),
// markdown) with an English/Polish toggle. Modelled on UShortcutsHelp: a modal
// Popup on the window Overlay.
//
// The markdown is parsed into structure here: a bare `**Section**` line starts
// a group (New/Fixed/…, already localized in the changelog itself), each
// `- **Title**: body` bullet becomes a card under it. Anything that doesn't
// parse into at least one group falls back to plain markdown rendering, so an
// unconventional changelog entry can never render blank.
Popup {
    id: root

    property string version: ""
    // Default to the UI language, then the user can switch.
    property string lang: Qt.locale().name.slice(0, 2) === "pl" ? "pl" : "en"
    readonly property string notes: App.changelog(lang)
    readonly property var parsed: parseNotes(notes)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(560, parent ? parent.width - 2 * Theme.spacingXL : 560)
    readonly property real maxBodyHeight: (parent ? parent.height : 600) * 0.62
    padding: Theme.spacingXL
    topPadding: Theme.spacingL

    // ---- changelog parsing ----

    // `**Section**` lines group the bullets that follow; `- **Title**: body`
    // bullets carry a bold lead-in that becomes the card title. Returns
    // { pre, sections: [{ name, kind, items: [{title, body}] }] }.
    function parseNotes(md) {
        const sections = []
        const pre = []
        let cur = null
        let curItem = null
        const lines = md.split('\n')
        for (let i = 0; i < lines.length; ++i) {
            const line = lines[i].trim()
            if (line === "") { curItem = null; continue }
            let m = line.match(/^\*\*([^*]+)\*\*:?$/)
            if (m) {
                cur = { name: m[1], kind: sectionKind(m[1]), items: [] }
                sections.push(cur)
                curItem = null
                continue
            }
            m = line.match(/^[-*]\s+(.*)$/)
            if (m) {
                let body = m[1]
                let title = ""
                const tm = body.match(/^\*\*(.+?)\*\*:?\s*(.*)$/)
                if (tm) { title = tm[1]; body = tm[2] }
                curItem = { title: title, body: body }
                if (!cur) {
                    cur = { name: "", kind: "other", items: [] }
                    sections.push(cur)
                }
                cur.items.push(curItem)
                continue
            }
            // Wrapped continuation of the previous bullet, or free preamble text.
            if (curItem)
                curItem.body += ' ' + line
            else
                pre.push(line)
        }
        return { pre: pre.join('\n'), sections: sections }
    }

    // Canonical section id from the (localized) heading, for color assignment.
    function sectionKind(name) {
        const n = name.toLowerCase()
        if (n === "new" || n.startsWith("now")) return "new"                       // Nowe / Nowości
        if (n === "fixed" || n.startsWith("napraw") || n.startsWith("popraw")) return "fixed"
        if (n === "improved" || n.startsWith("ulepsz")) return "improved"
        if (n === "changed" || n.startsWith("zmien") || n.startsWith("zmian")) return "changed"
        if (n === "removed" || n.startsWith("usun")) return "removed"
        return "other"
    }

    // Deliberately not palette-derived (like the recording-overlay tokens):
    // these are category hues and must stay distinguishable in every theme,
    // so only the light/dark variant follows the palette.
    function kindColor(kind) {
        const dark = Theme.isDark
        switch (kind) {
        case "new":      return dark ? "#3fb950" : "#1a7f37"
        case "fixed":    return dark ? "#58a6ff" : "#0969da"
        case "improved": return dark ? "#a371f7" : "#8250df"
        case "changed":  return dark ? "#d29922" : "#9a6700"
        case "removed":  return dark ? "#f85149" : "#cf222e"
        }
        return Theme.textSecondary
    }

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animFast; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.animMed; easing.type: Easing.OutBack }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast; easing.type: Easing.InCubic }
    }

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.divider
    }

    contentItem: Column {
        spacing: Theme.spacingM

        // Header: icon tile + title + version pill on the left, language toggle right.
        Item {
            width: parent.width
            height: 40

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingM

                Rectangle {
                    width: 36; height: 36; radius: Theme.radiusM
                    color: Theme.alpha(Theme.accent, 0.16)
                    anchors.verticalCenter: parent.verticalCenter
                    UIcon {
                        anchors.centerIn: parent
                        name: "star-filled"
                        size: 20
                        color: Theme.accent
                    }
                }
                Text {
                    text: qsTr("What's new")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontL
                    font.weight: Font.DemiBold
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    height: 22
                    width: versionText.implicitWidth + 16
                    radius: 11
                    color: Theme.alpha(Theme.accent, 0.14)
                    border.width: 1
                    border.color: Theme.alpha(Theme.accent, 0.35)
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        id: versionText
                        anchors.centerIn: parent
                        text: qsTr("v%1").arg(root.version)
                        color: Theme.accent
                        font.pixelSize: Theme.fontS
                        font.weight: Font.DemiBold
                    }
                }
            }

            // English / Polish segmented toggle: one pill container, the active
            // segment filled with the accent.
            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: langRow.width + 6
                height: 28
                radius: 14
                color: Theme.surfaceHi
                border.width: 1
                border.color: Theme.divider

                Row {
                    id: langRow
                    anchors.centerIn: parent
                    Repeater {
                        model: [{ code: "en", label: qsTr("EN") },
                                { code: "pl", label: qsTr("PL") }]
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool selected: root.lang === modelData.code
                            width: 40; height: 22; radius: 11
                            color: selected ? Theme.accent : "transparent"
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }
                            Text {
                                anchors.centerIn: parent
                                text: parent.modelData.label
                                color: parent.selected ? Theme.textOnAccent : Theme.textSecondary
                                font.pixelSize: Theme.fontS
                                font.weight: Font.DemiBold
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.lang = parent.modelData.code
                            }
                        }
                    }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        // Empty state.
        Column {
            visible: root.notes === ""
            width: parent.width
            spacing: Theme.spacingS
            topPadding: Theme.spacingL
            bottomPadding: Theme.spacingL
            UIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "star"
                size: 28
                color: Theme.textTertiary
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("No release notes for this version.")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontM
            }
        }

        ScrollView {
            visible: root.notes !== ""
            width: parent.width
            height: Math.min(notesColumn.implicitHeight, root.maxBodyHeight)
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                id: notesColumn
                width: root.width - 2 * root.padding
                spacing: Theme.spacingL

                // Free text before the first section (also rare, kept as markdown).
                Text {
                    visible: root.parsed.pre !== ""
                    width: parent.width
                    text: root.parsed.pre
                    textFormat: Text.MarkdownText
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    linkColor: Theme.accent
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                }

                // Structured view: one group per changelog section.
                Repeater {
                    model: root.parsed.sections
                    delegate: Column {
                        id: sectionCol
                        required property var modelData
                        readonly property color tint: root.kindColor(modelData.kind)
                        width: parent ? parent.width : 0
                        spacing: Theme.spacingS

                        // Section chip: colored dot + the changelog's own heading.
                        Rectangle {
                            visible: sectionCol.modelData.name !== ""
                            height: 24
                            width: chipRow.width + 20
                            radius: 12
                            color: Theme.alpha(sectionCol.tint, 0.13)
                            Row {
                                id: chipRow
                                anchors.centerIn: parent
                                spacing: 6
                                Rectangle {
                                    width: 7; height: 7; radius: 3.5
                                    color: sectionCol.tint
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: sectionCol.modelData.name
                                    color: sectionCol.tint
                                    font.pixelSize: Theme.fontS
                                    font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }

                        Repeater {
                            model: sectionCol.modelData.items
                            delegate: Rectangle {
                                id: itemCard
                                required property var modelData
                                width: parent ? parent.width : 0
                                height: itemCol.implicitHeight + 2 * Theme.spacingM
                                radius: Theme.radiusM
                                color: Theme.surfaceHi
                                border.width: 1
                                border.color: Theme.divider

                                // Category-colored rail down the card's left edge.
                                Rectangle {
                                    x: 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 3
                                    height: parent.height - 14
                                    radius: 1.5
                                    color: Theme.alpha(sectionCol.tint, 0.65)
                                }

                                Column {
                                    id: itemCol
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: Theme.spacingM + 4
                                    anchors.rightMargin: Theme.spacingM
                                    spacing: 3
                                    Text {
                                        visible: itemCard.modelData.title !== ""
                                        width: parent.width
                                        text: itemCard.modelData.title
                                        wrapMode: Text.WordWrap
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontM
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        visible: itemCard.modelData.body !== ""
                                        width: parent.width
                                        text: itemCard.modelData.body
                                        textFormat: Text.MarkdownText
                                        wrapMode: Text.WordWrap
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontM
                                        linkColor: Theme.accent
                                        onLinkActivated: (link) => Qt.openUrlExternally(link)
                                    }
                                }
                            }
                        }
                    }
                }

                // Fallback: nothing parsed into sections — render the raw markdown.
                Text {
                    visible: root.parsed.sections.length === 0 && root.notes !== ""
                    width: parent.width
                    text: root.notes
                    textFormat: Text.MarkdownText
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    linkColor: Theme.accent
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.divider }

        // Footer: release-history link left, dismiss right.
        Item {
            width: parent.width
            height: 32

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Full release history →")
                color: Theme.accent
                font.pixelSize: Theme.fontS
                font.underline: historyMouse.containsMouse
                MouseArea {
                    id: historyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Qt.openUrlExternally("https://github.com/unisic/unisic/releases")
                }
            }

            UButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Got it")
                variant: "filled"
                compact: true
                onClicked: root.close()
            }
        }
    }
}
