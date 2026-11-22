import QtQuick
import Unisic
import Unisic.Kit
import "../components"

// Clips: instant replay and what comes out of it. The ring is started,
// stopped and saved here, the saved clips open in the clip editor (the trim
// window), and the inspector holds the clip settings, the same Settings
// properties as Settings > Clips. Laid out like the Record page; the
// inspector is the sanctioned side column (docs/dev/ui-kit.md).
Item {
    id: page

    readonly property bool modalOpen: false
    readonly property bool compact: height < 760 || width - aside.width - 2 * 40 < 3 * 200 + 2 * 14
    readonly property bool canStart: App.recordingAvailable && !App.recording && !App.converting

    signal openPage(int index)
    signal openSettings(int tab, string query)

    // Clips and whole sessions, apart. A game chip in "Recently played"
    // narrows both to the files named after that game.
    property string gameFilter: ""
    HistoryFilterModel {
        id: recentModel
        sourceModel: App.history
        kindFilter: "replay"
        searchText: page.gameFilter
    }
    HistoryFilterModel {
        id: sessionModel
        sourceModel: App.history
        kindFilter: "session"
        searchText: page.gameFilter
    }

    // Settings::recentGames, "<epoch seconds>\t<name>[\t<cover path>]" newest first.
    readonly property var games: App.settings.recentGames.map(function (entry) {
        var f = entry.split("\t")
        return { name: f[1] || "", cover: f[2] || "", when: new Date(parseInt(f[0]) * 1000) }
    })
    // The game's part of a file name, as GifRecorder::replayStem writes it.
    // "Unisic_Session_2026-01-01_20-00-00_Hades II.mp4" -> "Hades II · 20:00";
    // any other name (a renamed or cut clip) stays as it is.
    function tileName(path) {
        var file = path.split("/").pop()
        var m = /^Unisic_(?:Replay|Session)_\d{4}-\d\d-\d\d_(\d\d)-(\d\d)-\d\d(?:-\d+)?(?:_(.+?))?(?:-\d+)?\.\w+$/.exec(file)
        return m && m[3] ? m[3] + " · " + m[1] + ":" + m[2] : file
    }
    function fileTag(name) {
        return name.replace(/[\/\\:*?"<>|\x00-\x1f]/g, "-").trim().slice(0, 60)
    }

    Flickable {
        id: pageFlick
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
            width: Math.max(0, pageFlick.width - 2 * 40)
            spacing: page.compact ? 18 : 30

            Column {
                width: parent.width
                spacing: 6
                PageTitle {
                    width: parent.width
                    text: qsTr("Clips")
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: !App.recordingAvailable
                          ? qsTr("Instant replay is unavailable: this desktop has no ScreenCast portal backend, so nothing can hand Unisic the screen.")
                          : App.instantReplayActive
                          ? qsTr("Instant replay is running and keeps the last %1. Save a clip when something worth keeping happens.").arg(page.lengthText)
                          : qsTr("Instant replay keeps the last moments of the screen in the background. Start it, then save a clip after the moment happened.")
                    color: App.recordingAvailable ? Theme.textSecondary : Theme.danger
                    font.pixelSize: 13
                }
            }

            Column {
                width: parent.width
                spacing: 12

                SectionLabel { text: qsTr("Instant replay") }

                // Titles and places never change; state only enables, disables
                // and recolours, so nothing moves under the pointer.
                Grid {
                    id: grid
                    width: parent.width
                    columns: 3
                    columnSpacing: 14
                    rowSpacing: 10
                    readonly property real cellW: Math.floor((width - 2 * columnSpacing) / 3)

                    ActionTile {
                        width: grid.cellW
                        compact: page.compact
                        iconName: App.instantReplayActive ? "stop" : "media-record"
                        iconColor: App.instantReplayActive ? Theme.danger : Theme.recDot
                        title: App.instantReplayActive ? qsTr("Stop replay") : qsTr("Start replay")
                        sub: App.instantReplayActive ? qsTr("Running") : qsTr("Off")
                        hotkey: App.settings.hotkeyReplayToggle
                        enabled: App.instantReplayActive || page.canStart
                        onActivated: App.instantReplayActive ? App.stopRecording() : App.startInstantReplay()
                    }
                    ActionTile {
                        width: grid.cellW
                        compact: page.compact
                        iconName: "document-save"
                        title: qsTr("Save clip")
                        //: %1 is the replay length, e.g. "30 s" or "2 min".
                        sub: qsTr("The last %1").arg(page.lengthText)
                        hotkey: App.settings.hotkeyInstantReplay
                        enabled: App.instantReplayActive && !App.converting
                        onActivated: App.saveInstantReplay()
                    }
                    ActionTile {
                        width: grid.cellW
                        compact: page.compact
                        iconName: "line-film"
                        title: qsTr("Edit a clip")
                        sub: qsTr("Cut any recording")
                        hotkey: App.settings.hotkeyTrimVideo
                        onActivated: App.openFileForEditing("video")
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 10

                SectionLabel { text: qsTr("Recently played") }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    visible: App.currentGame !== "" || page.games.length === 0
                    text: App.currentGame !== ""
                          //: %1 is the game's name.
                          ? qsTr("Playing now: %1. Clips and sessions are named after it.").arg(App.currentGame)
                          : App.settings.instantReplayAuto === "game" || App.instantReplayActive
                          ? qsTr("No game seen yet. Games from Steam, Proton launchers, GameMode and cloud gaming apps show up here.")
                          : qsTr("Games are looked for while instant replay runs, or all the time with Start automatically set to While a game is running.")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                }
                // One row, as many cards as fit, newest first; the size comes
                // from the width alone, so two games look like six.
                Row {
                    id: gameRow
                    width: parent.width
                    spacing: 10
                    visible: page.games.length > 0
                    readonly property int perRow: Math.max(1, Math.floor((width + spacing) / (150 + spacing)))
                    readonly property real cardW: Math.floor((width - (perRow - 1) * spacing) / perRow)
                    Repeater {
                        model: page.games.slice(0, gameRow.perRow)
                        GameCard {
                            required property var modelData
                            width: gameRow.cardW
                            height: implicitHeight
                            name: modelData.name
                            cover: modelData.cover
                            // App.history.count: look again when a clip lands.
                            fallback: App.history.count >= 0 ? App.history.gameThumbnail(page.fileTag(modelData.name)) : ""
                            checked: page.gameFilter !== "" && page.gameFilter === page.fileTag(modelData.name)
                            //: %1 is a date and time.
                            accessibleDescription: qsTr("Last played %1").arg(modelData.when.toLocaleString(Qt.locale(), Locale.ShortFormat))
                            onActivated: page.gameFilter = checked ? "" : page.fileTag(modelData.name)
                        }
                    }
                }
            }

            RecentStrip {
                id: sessionSection
                width: parent.width
                model: sessionModel
                gridName: "clipsSessionGrid"
                title: qsTr("Recordings")
                // Half of what is left; the clips below take the rest.
                available: (pageFlick.height - 2 * col.y - sessionSection.y - col.spacing) / 2
                listName: qsTr("Session recordings")
                emptyText: App.settings.clipKeepSession
                           ? qsTr("Every instant replay session is saved here when it stops. Click one to cut clips from it.")
                           : qsTr("Turn on Keep the whole session to save every instant replay session here and cut clips from it later.")
                openToEdit: true
                nameOf: page.tileName
                onOpenHistory: page.openPage(3)
            }

            RecentStrip {
                id: recentSection
                width: parent.width
                model: recentModel
                gridName: "clipsRecentGrid"
                title: qsTr("Clips")
                available: pageFlick.height - 2 * col.y - recentSection.y
                listName: qsTr("Recent clips")
                emptyText: qsTr("Saved clips show up here. Click one to cut it.")
                // A clip opens in the clip editor, not the player.
                openToEdit: true
                nameOf: page.tileName
                onOpenHistory: page.openPage(3)
            }
        }
    }

    readonly property string lengthText: {
        var s = App.settings.instantReplaySeconds
        return s < 60 ? qsTranslate("AppContext", "%1 s").arg(s)
                      : qsTranslate("AppContext", "%1 min").arg(Math.round(s / 60))
    }

    Inspector {
        id: aside
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        flickName: "clipsAside"
        accessibleName: qsTr("Clip options")

        Column {
            x: 20
            width: parent.width - 40
            topPadding: page.compact ? 14 : 22
            bottomPadding: page.compact ? 12 : 18
            spacing: page.compact ? 10 : 14

            SectionLabel {
                text: qsTr("Instant replay")
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            InspectorRow {
                id: lengthRow
                label: qsTranslate("RecordPage", "Replay length")
                UValueCombo {
                    width: 104; values: [10, 15, 30, 60, 120, 300, 600, 1800, 3600]
                    from: 10; to: 3600; suffix: " s"
                    accessibleName: lengthRow.label
                    value: App.settings.instantReplaySeconds
                    onChanged: (v) => App.settings.instantReplaySeconds = v
                }
            }
            InspectorRow {
                id: sourceRow
                label: qsTranslate("SettingsPage", "What to record")
                UComboBox {
                    width: 150
                    accessibleName: sourceRow.label
                    readonly property var ids: ["screen", "window"]
                    model: [qsTranslate("SettingsPage", "Whole screen"), qsTranslate("SettingsPage", "Game window")]
                    currentIndex: Math.max(0, ids.indexOf(App.settings.clipSource))
                    onActivated: (i) => App.settings.clipSource = ids[i]
                }
            }
            Column {
                width: parent.width
                spacing: 8
                Text {
                    id: autoLabel
                    text: qsTr("Start automatically")
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    Accessible.ignored: true
                }
                UComboBox {
                    width: parent.width
                    accessibleName: autoLabel.text
                    readonly property var ids: ["off", "launch", "game"]
                    model: [qsTranslate("SettingsPage", "Off"), qsTranslate("SettingsPage", "When Unisic starts"),
                            qsTranslate("SettingsPage", "While a game is running")]
                    currentIndex: Math.max(0, ids.indexOf(App.settings.instantReplayAuto))
                    onActivated: (i) => App.settings.instantReplayAuto = ids[i]
                }
            }
            InspectorRow {
                id: systemAudioRow
                label: qsTranslate("RecordPage", "Record system audio")
                MiniSwitch {
                    checked: App.settings.clipSystemAudio
                    accessibleName: systemAudioRow.label
                    onToggled: (c) => App.settings.clipSystemAudio = c
                }
            }
            InspectorRow {
                id: micRow
                label: qsTranslate("RecordPage", "Record microphone")
                MiniSwitch {
                    checked: App.settings.clipMicrophone
                    accessibleName: micRow.label
                    onToggled: (c) => App.settings.clipMicrophone = c
                }
            }
            InspectorRow {
                id: osdRow
                label: qsTranslate("SettingsPage", "On-screen indicator")
                MiniSwitch {
                    checked: App.settings.clipIndicator
                    accessibleName: osdRow.label
                    onToggled: (c) => App.settings.clipIndicator = c
                }
            }
            InspectorRow {
                id: styleRow
                label: qsTranslate("SettingsPage", "Indicator style")
                enabled: App.settings.clipIndicator
                UComboBox {
                    width: 104
                    accessibleName: styleRow.label
                    readonly property var ids: ["pill", "compact", "dot", "tinted"]
                    model: [qsTranslate("SettingsPage", "Pill"), qsTranslate("SettingsPage", "Compact"),
                            qsTranslate("SettingsPage", "Dot"), qsTranslate("SettingsPage", "Tinted")]
                    currentIndex: Math.max(0, ids.indexOf(App.settings.clipIndicatorStyle))
                    onActivated: (i) => App.settings.clipIndicatorStyle = ids[i]
                }
            }
            InspectorRow {
                id: sessionRow
                label: qsTranslate("SettingsPage", "Keep the whole session")
                MiniSwitch {
                    checked: App.settings.clipKeepSession
                    accessibleName: sessionRow.label
                    onToggled: (c) => App.settings.clipKeepSession = c
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

            SectionLabel { text: qsTranslate("SettingsPage", "Clip editor") }
            InspectorRow {
                id: openRow
                label: qsTr("Open saved clips in the editor")
                MiniSwitch {
                    checked: App.settings.clipOpenReplay
                    accessibleName: openRow.label
                    onToggled: (c) => App.settings.clipOpenReplay = c
                }
            }
            InspectorRow {
                id: frameRow
                label: qsTranslate("SettingsPage", "Default frame")
                UComboBox {
                    width: 130
                    accessibleName: frameRow.label
                    readonly property var ids: ["original", "16:9", "9:16", "1:1"]
                    model: [qsTranslate("SettingsPage", "As recorded"), "16:9", "9:16", "1:1"]
                    currentIndex: Math.max(0, ids.indexOf(App.settings.clipAspect))
                    onActivated: (i) => App.settings.clipAspect = ids[i]
                }
            }
            InspectorRow {
                id: sizeRow
                label: qsTranslate("SettingsPage", "Default clip size")
                UComboBox {
                    width: 130
                    accessibleName: sizeRow.label
                    readonly property var ids: [0, 1080, 720, 480]
                    model: [qsTranslate("SettingsPage", "As recorded"), "1080p", "720p", "480p"]
                    currentIndex: Math.max(0, ids.indexOf(App.settings.clipShortSide))
                    onActivated: (i) => App.settings.clipShortSide = ids[i]
                }
            }
            UButton {
                width: parent.width
                compact: true
                variant: "tonal"
                text: qsTr("All clip settings")
                onClicked: page.openSettings(3, "")
            }
        }
    }
}
