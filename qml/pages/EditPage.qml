import QtQuick
import Unisic
import Unisic.Kit
import "../components"

// Files you already have, not captures: an image opens in the same editor a
// screenshot does, a recording in the same trim window History's Trim opens.
// Its own page rather than more tiles on Capture: nothing here captures
// anything. Below them, the same kind of tiles for sharing what you have: a
// file, the clipboard, a QR code.
//
// Atelier layout, the Capture and Record pages': the work on the left, the
// editor's defaults in an inspector column on the right. That column is the
// third sanctioned exception to "one full-width grid per page"
// (docs/dev/ui-kit.md): it holds settings, the same App.settings properties
// as Settings > Editor, not content.
//
// The settings rows reuse Settings > Editor's strings in that page's context
// (qsTranslate("SettingsPage", ...)) and the header and Recent reuse the
// Capture page's, so they share one translation each instead of seven new
// copies that could drift.
Item {
    id: page

    // Nothing modal on this page - the flag exists on every page so Main.qml
    // can ask the active one without a per-page special case.
    readonly property bool modalOpen: false

    // Main.qml owns the page index and the Settings tab.
    signal openPage(int index)
    signal openSettings(int tab)

    // Same rule as the Capture page, for two tiles instead of four.
    readonly property bool compact: height < 760 || width - aside.width - 2 * 40 < 2 * 200 + 14

    // ------------------------------------------------------------------ main --

    Flickable {
        id: pageFlick
        objectName: "editPageFlick"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: aside.left
        contentHeight: col.height + 2 * col.y
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        MiddleScroll { flickable: pageFlick }
        WheelBoost { flickable: pageFlick }

        Column {
            id: col
            x: 40
            y: page.compact ? 20 : 28
            // Clamped for the Recent GridView, as on the Capture page.
            width: Math.max(0, pageFlick.width - 2 * 40)
            spacing: page.compact ? 18 : 30

            Item {
                width: parent.width
                height: Math.max(titleCol.height, pasteBtn.height)

                Column {
                    id: titleCol
                    anchors.left: parent.left
                    anchors.right: pasteBtn.left
                    anchors.rightMargin: 24
                    anchors.bottom: parent.bottom
                    spacing: 6
                    PageTitle {
                        width: parent.width
                        text: qsTr("Edit")
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Open a picture or a video you already have - the same editor and trim window your captures use.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                    }
                }

                // The clipboard is the other way in besides a file; it goes
                // through the same router (AppContext::openPath) as the
                // tiles, the drop area and the window's Ctrl+V.
                HeaderButton {
                    id: pasteBtn
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    iconName: "line-clipboard"
                    text: qsTranslate("CapturePage", "Paste")
                    kbd: "Ctrl V"
                    accessibleDescription: qsTranslate("CapturePage", "Open the image on the clipboard in the editor, Ctrl+V")
                    onActivated: App.pasteFromClipboard()
                }
            }

            Column {
                width: parent.width
                spacing: 12

                SectionLabel { text: qsTr("Open") }

                // One dialog either way (AppContext::openFileForEditing): the
                // tile only picks which filter comes first, and what the file
                // IS decides where it opens.
                Flow {
                    id: openFlow
                    objectName: "editOpenFlow"
                    width: parent.width
                    spacing: page.compact ? 12 : 14
                    readonly property int minCard: page.compact ? 130 : 200
                    readonly property int perRow: width >= 2 * minCard + spacing ? 2 : 1
                    readonly property real cardW: Math.floor((width - (perRow - 1) * spacing) / perRow)

                    SourceTile {
                        width: openFlow.cardW
                        compact: page.compact
                        art: editArt
                        title: qsTr("Edit an image")
                        sub: qsTr("Annotate, crop, blur")
                        // No hotkey action opens a file: no hotkey line at
                        // all, and a subtitle that fits on one line.
                        bindable: false
                        hotkeyRow: false
                        subLines: 1
                        onActivated: App.openFileForEditing("image")
                    }
                    SourceTile {
                        width: openFlow.cardW
                        compact: page.compact
                        art: trimArt
                        title: qsTr("Trim a video")
                        sub: qsTr("Cut a start and an end")
                        bindable: false
                        hotkeyRow: false
                        subLines: 1
                        onActivated: App.openFileForEditing("video")
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 12

                SectionLabel { text: qsTr("Share") }

                // Not captures either: these go through the same servers,
                // failure chain and after-upload actions a capture upload does.
                Grid {
                    id: shareGrid
                    objectName: "editShareGrid"
                    width: parent.width
                    // Only a column count that divides the three tiles.
                    columns: width >= 3 * openFlow.minCard + 2 * columnSpacing ? 3 : 1
                    columnSpacing: openFlow.spacing
                    rowSpacing: openFlow.spacing
                    readonly property real cellW: Math.floor((width - (columns - 1) * columnSpacing) / columns)

                    ActionTile {
                        width: shareGrid.cellW
                        compact: page.compact
                        iconName: "line-upload"
                        title: qsTr("Upload a file")
                        sub: qsTr("Any file, to your server")
                        onActivated: App.uploadAnyFile()
                    }
                    ActionTile {
                        width: shareGrid.cellW
                        compact: page.compact
                        iconName: "line-send"
                        title: qsTr("Upload clipboard")
                        sub: qsTr("A copied image, file or text")
                        onActivated: App.uploadClipboard()
                    }
                    ActionTile {
                        width: shareGrid.cellW
                        compact: page.compact
                        // The kit's QR glyph; the line set has none.
                        iconName: "view-preview"
                        title: qsTr("QR code")
                        sub: qsTr("Of the text you copied")
                        onActivated: App.qrFromClipboard()
                    }
                }
            }

            // Every kind, since this page opens both: a still lands in the
            // editor, a recording in the trim window (openToEdit).
            RecentStrip {
                id: recentSection
                width: parent.width
                model: App.history
                gridName: "editRecentGrid"
                openToEdit: true
                available: pageFlick.height - 2 * col.y - recentSection.y
                listName: qsTranslate("CapturePage", "Recent captures")
                emptyText: qsTranslate("CapturePage", "Your captures will show up here.")
                onOpenHistory: page.openPage(3)
            }
        }
    }

    // ------------------------------------------------------------- inspector --

    Inspector {
        id: aside
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        flickName: "editAside"
        accessibleName: qsTranslate("SettingsPage", "Editor defaults")

        // A subset of Settings > Editor, the rows that change what the
        // editor looks like when it opens; the rest stays behind Manage.
        Column {
            x: 20
            width: parent.width - 40
            topPadding: page.compact ? 14 : 22
            bottomPadding: page.compact ? 12 : 18
            spacing: page.compact ? 10 : 14

            Item {
                width: parent.width
                height: Math.max(defaultsLabel.height, manageLink.height)
                SectionLabel {
                    id: defaultsLabel
                    anchors.left: parent.left
                    anchors.right: manageLink.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    // Wraps rather than elides: in capitals with letter
                    // spacing the Polish heading runs past the column.
                    wrapMode: Text.WordWrap
                    text: qsTranslate("SettingsPage", "Editor defaults")
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Link {
                    id: manageLink
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("CapturePage", "Manage")
                    Accessible.description: qsTr("Open Settings at Editor")
                    onActivated: page.openSettings(3)
                }
            }

            // Stacked like the frame rate on the Record page: two segments
            // beside the label would cut the German "Bedienfelder" short.
            Column {
                width: parent.width
                spacing: 8
                Text {
                    id: layoutLabel
                    text: qsTranslate("SettingsPage", "Layout")
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    Accessible.ignored: true
                }
                SegmentedControl {
                    width: parent.width
                    accessibleName: layoutLabel.text
                    labels: [qsTranslate("SettingsPage", "Panels"), qsTranslate("SettingsPage", "Floating")]
                    currentIndex: App.settings.editorLayout === "floating" ? 1 : 0
                    onPicked: (i) => App.settings.editorLayout = (i === 1 ? "floating" : "panels")
                }
            }

            InspectorRow {
                id: colorRow
                label: qsTranslate("SettingsPage", "Stroke color")
                // The swatches name themselves by colour, as in Settings;
                // the group carries the row's caption.
                Row {
                    spacing: 6
                    Accessible.role: Accessible.Grouping
                    Accessible.name: colorRow.label
                    Repeater {
                        model: Theme.swatches.slice(0, 5)
                        delegate: ColorDot {
                            required property var modelData
                            dotColor: modelData
                            active: Qt.colorEqual(App.settings.editorStrokeColor, modelData)
                            onClicked: App.settings.editorStrokeColor = modelData
                        }
                    }
                }
            }

            InspectorRow {
                id: widthRow
                label: qsTranslate("SettingsPage", "Stroke width")
                UValueCombo {
                    width: 104
                    values: [1, 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32, 48, 64]
                    from: 1; to: 64
                    suffix: " px"
                    accessibleName: widthRow.label
                    value: App.settings.editorStrokeWidth
                    onChanged: (v) => App.settings.editorStrokeWidth = v
                }
            }

            InspectorRow {
                id: textSizeRow
                label: qsTranslate("SettingsPage", "Text size")
                UValueCombo {
                    width: 104
                    values: [8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 56, 64, 72, 96, 144]
                    from: 8; to: 144
                    suffix: " px"
                    accessibleName: textSizeRow.label
                    value: App.settings.editorFontSize
                    onChanged: (v) => App.settings.editorFontSize = v
                }
            }

            InspectorRow {
                id: resetColorsRow
                label: qsTranslate("SettingsPage", "Always start with the default colors")
                MiniSwitch {
                    checked: App.settings.editorResetColors
                    accessibleName: resetColorsRow.label
                    accessibleDescription: qsTranslate("SettingsPage", "Color picks made while annotating last for that session only.")
                    onToggled: (c) => App.settings.editorResetColors = c
                }
            }

            InspectorRow {
                id: resetToolsRow
                label: qsTranslate("SettingsPage", "Always start with the default tool options")
                MiniSwitch {
                    checked: App.settings.editorResetTools
                    accessibleName: resetToolsRow.label
                    accessibleDescription: qsTranslate("SettingsPage", "Stroke width, text style and fill toggles last for that session only.")
                    onToggled: (c) => App.settings.editorResetTools = c
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.alpha(Theme.accent, 0.10) }

        Column {
            x: 20
            width: parent.width - 40
            topPadding: page.compact ? 12 : 18
            bottomPadding: page.compact ? 12 : 18
            spacing: page.compact ? 10 : 14

            SectionLabel { text: qsTr("Good to know") }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Editing an image never touches the original: the editor saves where your captures go, under a new name, unless you overwrite it yourself. Trimming always writes a new file next to the source.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontS
            }
            // A build-time fact (QtMultimedia present or not), never a
            // state that flips while the page is open.
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: !App.capVideoPlayback
                text: qsTr("Install qt6-qtmultimedia for a video preview while trimming; without it the trim window falls back to a slider-only range picker.")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontS
            }
        }
    }

    Component { id: editArt; TileArt.EditImageArt {} }
    Component { id: trimArt; TileArt.TrimVideoArt {} }
}
