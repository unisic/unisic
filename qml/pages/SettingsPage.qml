import QtQuick
import QtQuick.Dialogs
import Unisic
import "../components"

Item {
    id: page
    readonly property int cardWidth: Math.min(width - 2 * Theme.spacingXL, 694)

    readonly property var themeIds: ["system", "unisic", "dark", "light",
                                     "catppuccin-mocha", "catppuccin-latte", "dracula", "nord", "gruvbox"]
    readonly property var themeNames: [qsTr("System (match KDE)"), "Unisic", qsTr("Dark"), qsTr("Light"),
                                       "Catppuccin Mocha", "Catppuccin Latte", "Dracula", "Nord", "Gruvbox"]
    readonly property var toolbarPosIds: ["follow", "top-left", "top-center", "top-right",
                                          "middle-left", "middle-center", "middle-right",
                                          "bottom-left", "bottom-center", "bottom-right"]
    readonly property var toolbarPosNames: [qsTr("Follow selection"), qsTr("Top left"), qsTr("Top center"), qsTr("Top right"),
                                            qsTr("Middle left"), qsTr("Middle center"), qsTr("Middle right"),
                                            qsTr("Bottom left"), qsTr("Bottom center"), qsTr("Bottom right")]
    readonly property var popupPosIds: ["top-left", "top-center", "top-right",
                                        "bottom-left", "bottom-center", "bottom-right"]
    readonly property var popupPosNames: [qsTr("Top left"), qsTr("Top center"), qsTr("Top right"),
                                          qsTr("Bottom left"), qsTr("Bottom center"), qsTr("Bottom right")]

    function toolHidden(id) {
        var csv = App.settings.hiddenTools
        return csv ? ("," + csv + ",").indexOf("," + id + ",") >= 0 : false
    }
    function setToolHidden(id, hidden) {
        var csv = App.settings.hiddenTools
        var list = csv ? csv.split(",").filter(function (x) { return x.length > 0 }) : []
        list = list.filter(function (x) { return x !== id })
        if (hidden) list.push(id)
        App.settings.hiddenTools = list.join(",")
    }

    component SettingRow: Item {
        property alias label: labelText.text
        default property alias control: slot.data
        width: parent.width
        height: 44
        Text {
            id: labelText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.textPrimary
            font.pixelSize: Theme.fontM
        }
        Item {
            id: slot
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: childrenRect.width
            height: parent.height
        }
    }

    component SectionTitle: Text {
        color: Theme.textPrimary
        font.pixelSize: Theme.fontL
        font.weight: Font.DemiBold
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export Unisic settings")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Unisic settings (*.json)")]
        defaultSuffix: "json"
        onAccepted: {
            var err = App.exportSettings(selectedFile)
            if (err !== "") App.showToast(err)
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import Unisic settings")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Unisic settings (*.json)")]
        onAccepted: {
            var err = App.importSettings(selectedFile)
            if (err !== "") App.showToast(err)
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        contentHeight: col.height + Theme.spacingXL
        clip: true

        Column {
            id: col
            width: parent.width
            spacing: Theme.spacingL

            Item {
                width: parent.width
                height: 44
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Settings")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.Bold
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingS
                    UButton { compact: true; variant: "tonal"; iconName: "folder-open"; text: qsTr("Import"); onClicked: importDialog.open() }
                    UButton { compact: true; variant: "tonal"; iconName: "document-save"; text: qsTr("Export"); onClicked: exportDialog.open() }
                }
            }

            // ---------------- Appearance / theme ----------------
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Appearance") }
                    SettingRow {
                        label: qsTr("Theme")
                        UComboBox {
                            width: 220
                            model: page.themeNames
                            currentIndex: Math.max(0, page.themeIds.indexOf(ThemeController.themeName))
                            onActivated: (i) => ThemeController.themeName = page.themeIds[i]
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("The System theme follows your desktop's light/dark mode and accent color.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }

            // ---------------- General ----------------
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("General") }
                    SettingRow {
                        label: qsTr("Show notifications")
                        USwitch { checked: App.settings.showNotifications; onToggled: (c) => App.settings.showNotifications = c }
                    }
                    SettingRow {
                        label: qsTr("Show capture preview popup")
                        USwitch { checked: App.settings.showCapturePopup; onToggled: (c) => App.settings.showCapturePopup = c }
                    }
                    SettingRow {
                        visible: App.settings.showCapturePopup
                        height: App.settings.showCapturePopup ? 44 : 0
                        label: qsTr("Popup position")
                        UComboBox {
                            width: 180
                            model: page.popupPosNames
                            currentIndex: Math.max(0, page.popupPosIds.indexOf(App.settings.capturePopupPosition))
                            onActivated: (i) => App.settings.capturePopupPosition = page.popupPosIds[i]
                        }
                    }
                    SettingRow {
                        visible: App.settings.showCapturePopup
                        height: App.settings.showCapturePopup ? 44 : 0
                        label: qsTr("Popup auto-hide (0 = keep open)")
                        USpinBox { from: 0; to: 60; value: App.settings.capturePopupDurationSec; suffix: " s"; onChanged: (v) => App.settings.capturePopupDurationSec = v }
                    }
                    SettingRow {
                        visible: App.ocrAvailable
                        height: App.ocrAvailable ? 44 : 0
                        label: qsTr("OCR languages")
                        UTextField {
                            width: 150
                            text: App.settings.ocrLanguages
                            placeholder: "pol+eng"
                            onEdited: (t) => App.settings.ocrLanguages = t
                        }
                    }
                    SettingRow {
                        label: qsTr("Closing the window minimizes to tray")
                        USwitch { checked: App.settings.minimizeToTrayOnClose; onToggled: (c) => App.settings.minimizeToTrayOnClose = c }
                    }
                    SettingRow {
                        label: qsTr("Open file after saving")
                        USwitch { checked: App.settings.openAfterSave; onToggled: (c) => App.settings.openAfterSave = c }
                    }
                    SettingRow {
                        label: qsTr("Capture delay")
                        USpinBox { from: 0; to: 5000; value: App.settings.captureDelayMs; suffix: " ms"; onChanged: (v) => App.settings.captureDelayMs = v }
                    }
                    SettingRow {
                        label: qsTr("Include mouse cursor")
                        USwitch { checked: App.settings.includeCursor; onToggled: (c) => App.settings.includeCursor = c }
                    }
                }
            }

            // ---------------- Storage & file naming ----------------
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Storage & file naming") }
                    Row {
                        width: parent.width
                        spacing: Theme.spacingM
                        UTextField {
                            width: parent.width - 110 - Theme.spacingM
                            text: App.settings.saveDirectory
                            onEdited: (t) => App.settings.saveDirectory = t
                        }
                        UButton { width: 110; text: qsTr("Open"); variant: "tonal"; onClicked: App.openDirectory("") }
                    }
                    SettingRow {
                        label: qsTr("Image format")
                        UComboBox {
                            width: 150
                            model: ["png", "jpg", "webp"]
                            currentIndex: Math.max(0, model.indexOf(App.settings.imageFormat))
                            onActivated: (i) => App.settings.imageFormat = model[i]
                        }
                    }
                    SettingRow {
                        label: qsTr("Quality (JPEG/WebP): %1").arg(App.settings.imageQuality)
                        USlider {
                            width: 200
                            from: 10; to: 100
                            value: App.settings.imageQuality
                            onMoved: (v) => App.settings.imageQuality = Math.round(v)
                        }
                    }
                    Column {
                        width: parent.width
                        spacing: 4
                        Text {
                            text: qsTr("Filename template — tokens: %date%, %time%, %datetime%, %unix%, %rand%")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontS
                        }
                        UTextField {
                            id: templateField
                            width: parent.width
                            text: App.settings.filenameTemplate
                            onEdited: (t) => App.settings.filenameTemplate = t
                        }
                        Text {
                            text: qsTr("Preview: %1").arg(App.filenamePreview())
                            color: Theme.accent
                            font.pixelSize: Theme.fontS
                            font.family: "monospace"
                        }
                    }
                }
            }

            // ---------------- After capture ----------------
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("After capture") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Each enabled action runs immediately when the region is dropped — the editor opens alongside them.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Copy image to clipboard")
                        USwitch { checked: App.settings.copyToClipboard; onToggled: (c) => App.settings.copyToClipboard = c }
                    }
                    SettingRow {
                        label: qsTr("Save to disk automatically")
                        USwitch { checked: App.settings.autoSave; onToggled: (c) => App.settings.autoSave = c }
                    }
                    SettingRow {
                        label: qsTr("Upload to the active destination")
                        USwitch { checked: App.settings.uploadAfterCapture; onToggled: (c) => App.settings.uploadAfterCapture = c }
                    }
                    SettingRow {
                        label: qsTr("Open the editor")
                        USwitch { checked: App.settings.openEditor; onToggled: (c) => App.settings.openEditor = c }
                    }
                }
            }

            // ---------------- After upload ----------------
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("After upload") }
                    SettingRow {
                        label: qsTr("Copy link to clipboard")
                        USwitch { checked: App.settings.afterUploadCopyLink; onToggled: (c) => App.settings.afterUploadCopyLink = c }
                    }
                    SettingRow {
                        label: qsTr("Open link in browser")
                        USwitch { checked: App.settings.afterUploadOpenInBrowser; onToggled: (c) => App.settings.afterUploadOpenInBrowser = c }
                    }
                }
            }

            // ---------------- Editor defaults ----------------
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Editor defaults") }
                    SettingRow {
                        label: qsTr("Stroke color")
                        Row {
                            spacing: 6
                            ColorDot { dotColor: "#FF4757"; active: App.settings.editorStrokeColor.toLowerCase() === "#ff4757"; onClicked: App.settings.editorStrokeColor = "#FF4757" }
                            ColorDot { dotColor: "#FFD84D"; active: App.settings.editorStrokeColor.toLowerCase() === "#ffd84d"; onClicked: App.settings.editorStrokeColor = "#FFD84D" }
                            ColorDot { dotColor: "#2ED573"; active: App.settings.editorStrokeColor.toLowerCase() === "#2ed573"; onClicked: App.settings.editorStrokeColor = "#2ED573" }
                            ColorDot { dotColor: "#1E90FF"; active: App.settings.editorStrokeColor.toLowerCase() === "#1e90ff"; onClicked: App.settings.editorStrokeColor = "#1E90FF" }
                            ColorDot { dotColor: "#C8ACD6"; active: App.settings.editorStrokeColor.toLowerCase() === "#c8acd6"; onClicked: App.settings.editorStrokeColor = "#C8ACD6" }
                        }
                    }
                    SettingRow {
                        label: qsTr("Stroke width")
                        USpinBox { from: 1; to: 16; value: App.settings.editorStrokeWidth; suffix: " px"; onChanged: (v) => App.settings.editorStrokeWidth = v }
                    }
                    SettingRow {
                        label: qsTr("Text size")
                        USpinBox { from: 10; to: 72; value: App.settings.editorFontSize; suffix: " px"; onChanged: (v) => App.settings.editorFontSize = v }
                    }
                }
            }

            // ---------------- Capture overlay ----------------
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Capture overlay") }
                    SettingRow {
                        label: qsTr("Toolbar position")
                        UComboBox {
                            width: 200
                            model: page.toolbarPosNames
                            currentIndex: Math.max(0, page.toolbarPosIds.indexOf(App.settings.overlayToolbarPosition))
                            onActivated: (i) => App.settings.overlayToolbarPosition = page.toolbarPosIds[i]
                        }
                    }
                }
            }

            // ---------------- Editor tools (show/hide) ----------------
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Editor tools") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Hide tools you don't use — they disappear from the editor and the capture overlay.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    Repeater {
                        model: ToolCatalog.tools.filter(function (t) { return t.hideable })
                        delegate: Item {
                            width: parent.width
                            height: 40
                            Row {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                UIcon { name: modelData.iconName; size: 18; color: Theme.textSecondary; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: modelData.label; color: Theme.textPrimary; font.pixelSize: Theme.fontM; anchors.verticalCenter: parent.verticalCenter }
                            }
                            USwitch {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !page.toolHidden(modelData.id)
                                onToggled: (c) => page.setToolHidden(modelData.id, !c)
                            }
                        }
                    }
                }
            }

            // ---------------- Recording ----------------
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Recording") }
                    SettingRow {
                        label: qsTr("GIF frame rate")
                        USpinBox { from: 1; to: 60; value: App.settings.gifFps; suffix: " fps"; onChanged: (v) => App.settings.gifFps = v }
                    }
                    SettingRow {
                        label: qsTr("GIF max duration (0 = unlimited)")
                        USpinBox { from: 0; to: 600; value: App.settings.gifMaxDurationSec; suffix: " s"; onChanged: (v) => App.settings.gifMaxDurationSec = v }
                    }
                    SettingRow {
                        label: qsTr("GIF quality")
                        UComboBox {
                            width: 180
                            model: [qsTr("Fast / small"), qsTr("Balanced"), qsTr("Best")]
                            currentIndex: App.settings.gifQuality
                            onActivated: (i) => App.settings.gifQuality = i
                        }
                    }
                    SettingRow {
                        label: qsTr("MP4 frame rate")
                        USpinBox { from: 5; to: 60; value: App.settings.videoFps; suffix: " fps"; onChanged: (v) => App.settings.videoFps = v }
                    }
                }
            }

            // ---------------- Hotkeys ----------------
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Global hotkeys") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Registered through KDE global shortcuts (KGlobalAccel). Use Qt key notation, e.g. Meta+Shift+2.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Full screen")
                        UShortcutRecorder { width: 220; shortcut: App.settings.hotkeyFullScreen; onRecorded: (t) => App.settings.hotkeyFullScreen = t }
                    }
                    SettingRow {
                        label: qsTr("Region")
                        UShortcutRecorder { width: 220; shortcut: App.settings.hotkeyRegion; onRecorded: (t) => App.settings.hotkeyRegion = t }
                    }
                    SettingRow {
                        label: qsTr("Window")
                        UShortcutRecorder { width: 220; shortcut: App.settings.hotkeyWindow; onRecorded: (t) => App.settings.hotkeyWindow = t }
                    }
                    SettingRow {
                        label: qsTr("Video start/stop")
                        UShortcutRecorder { width: 220; shortcut: App.settings.hotkeyRecord; onRecorded: (t) => App.settings.hotkeyRecord = t }
                    }
                    SettingRow {
                        label: qsTr("GIF start/stop")
                        UShortcutRecorder { width: 220; shortcut: App.settings.hotkeyGif; onRecorded: (t) => App.settings.hotkeyGif = t }
                    }
                    UButton {
                        anchors.right: parent.right
                        text: qsTr("Apply hotkeys")
                        compact: true
                        onClicked: { App.applyHotkeys(); App.showToast(qsTr("Hotkeys re-registered")) }
                    }
                }
            }

            Text {
                text: qsTr("Unisic 0.1 — screenshots · annotations · uploads · GIF, native on Wayland")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontS
            }
        }
    }
}
