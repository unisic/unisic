import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Unisic
import "../components"

Item {
    id: page
    // paneArea is already inset by spacingXL on both sides, so don't subtract it
    // again here (that left the cards mis-centered with a big right-hand gap).
    readonly property int cardWidth: Math.min(paneArea.width, 694)
    property int tab: 0

    // Local absolute path -> file URL. Per-segment encode: encodeURI leaves
    // '#'/'?' unescaped, so a filename containing them would be parsed as a URL
    // fragment/query and the thumbnail would fail to load.
    function fileUrl(p) {
        return "file://" + p.split("/").map(encodeURIComponent).join("/")
    }
    // Tooltip label from a path: basename without the image extension.
    function iconLabel(p) {
        return p.split("/").pop().replace(/\.(svg|svgz|png|jpe?g|webp|xpm|ico)$/i, "")
    }

    // FPS dropdown options (15/30/45/60); snap a stored value to the nearest.
    readonly property var fpsOpts: [15, 30, 45, 60]
    function nearestFps(v) {
        var best = 0, bd = 1e9
        for (var i = 0; i < fpsOpts.length; ++i) {
            var d = Math.abs(fpsOpts[i] - v)
            if (d < bd) { bd = d; best = i }
        }
        return best
    }

    // The Developer tab (index 5) only exists in a dev build.
    readonly property var tabNames: {
        var t = [qsTr("General"), qsTr("Appearance"), qsTr("Editor"),
                 qsTr("Recording"), qsTr("Hotkeys")]
        if (App.devBuild)
            t.push(qsTr("Developer"))
        return t
    }

    readonly property var themeIds: ["system", "unisic", "dark", "light",
                                     "catppuccin-mocha", "catppuccin-latte", "dracula", "nord", "gruvbox"]
    readonly property var themeNames: [qsTr("System Theme"), "Unisic", qsTr("Dark"), qsTr("Light"),
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
    // Per-tool freedesktop icon-name overrides (JSON map in editorToolIcons).
    function iconOverride(id) {
        var j = App.settings.editorToolIcons
        if (!j) return ""
        try { var m = JSON.parse(j); return m[id] || "" } catch (e) { return "" }
    }
    function setIconOverride(id, name) {
        var j = App.settings.editorToolIcons
        var m = {}
        if (j) { try { m = JSON.parse(j) } catch (e) { m = {} } }
        if (name && name !== "") m[id] = name; else delete m[id]
        App.settings.editorToolIcons = JSON.stringify(m)
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
            width: slot.x - Theme.spacingM
            elide: Text.ElideRight
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

    // A scrollable settings pane: give it cards as default content.
    component ScrollPane: Flickable {
        id: fl
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: paneCol.height + Theme.spacingXL
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        default property alias content: paneCol.data
        Column {
            id: paneCol
            width: fl.width
            spacing: Theme.spacingL
        }
        MiddleScroll { flickable: fl }
    }

    // Import/Export use the DESKTOP's native file picker (C++ QFileDialog via
    // the platform theme), not a QML FileDialog — the latter fell back to the
    // Basic-styled Qt Quick dialog here, which looked out of place.

    // ---------------- header ----------------
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingXL
        anchors.bottomMargin: 0
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
            UButton { compact: true; variant: "tonal"; iconName: "folder-open"; text: qsTr("Import"); onClicked: App.importSettingsDialog() }
            UButton { compact: true; variant: "tonal"; iconName: "document-save"; text: qsTr("Export"); onClicked: App.exportSettingsDialog() }
        }
    }

    // ---------------- tab bar ----------------
    Row {
        id: tabBar
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingXL
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingXL
        anchors.topMargin: Theme.spacingM
        height: 38
        spacing: Theme.spacingS
        Repeater {
            model: page.tabNames
            delegate: Rectangle {
                required property int index
                required property string modelData
                width: tabLabel.implicitWidth + 28
                height: 34
                radius: Theme.radiusM
                color: page.tab === index ? Theme.accent
                     : tabMouse.containsMouse ? Theme.alpha(Theme.accent, 0.16)
                     : Theme.surface
                border.width: 1
                border.color: page.tab === index ? Theme.accent : Theme.divider
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
                Text {
                    id: tabLabel
                    anchors.centerIn: parent
                    text: modelData
                    color: page.tab === index ? Theme.textOnAccent : Theme.textSecondary
                    font.pixelSize: Theme.fontM
                    font.weight: page.tab === index ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    id: tabMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.tab = index
                }
            }
        }
    }

    // Persistence warning: the config dir isn't writable, so nothing sticks
    // across launches. Tells the user exactly why + where to fix it.
    Rectangle {
        id: persistWarn
        visible: !App.settings.persistent
        anchors.top: tabBar.bottom
        anchors.topMargin: Theme.spacingM
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingXL
        anchors.rightMargin: Theme.spacingXL
        height: visible ? warnText.implicitHeight + 2 * Theme.spacingM : 0
        radius: Theme.radiusM
        color: Theme.alpha(Theme.danger, 0.15)
        border.width: 1
        border.color: Theme.danger
        Text {
            id: warnText
            anchors.left: parent.left; anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Theme.spacingM
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            font.pixelSize: Theme.fontS + 1
            text: qsTr("⚠ Settings can't be saved — your config file is not writable, so changes reset every launch. Fix its permissions:\n    sudo chown -R $USER ~/.config/Unisic")
        }
    }

    // ---------------- panes ----------------
    Item {
        id: paneArea
        anchors.top: persistWarn.visible ? persistWarn.bottom : tabBar.bottom
        anchors.topMargin: Theme.spacingM
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spacingXL
        anchors.rightMargin: Theme.spacingXL
        anchors.bottomMargin: Theme.spacingL
        clip: true

        // ===== General =====
        ScrollPane {
            visible: page.tab === 0
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
                        label: qsTr("Show capture notification")
                        USwitch { checked: App.settings.showCapturePopup; onToggled: (c) => App.settings.showCapturePopup = c }
                    }
                    SettingRow {
                        // Corner only matters for the layer-shell card we position
                        // ourselves; a native notification is placed by the server.
                        visible: App.settings.showCapturePopup && App.layerShellActive
                        height: (App.settings.showCapturePopup && App.layerShellActive) ? 44 : 0
                        label: qsTr("Notification position")
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
                        label: qsTr("Notification auto-hide (0 = keep open)")
                        USpinBox { from: 0; to: 60; value: App.settings.capturePopupDurationSec; suffix: " s"; onChanged: (v) => App.settings.capturePopupDurationSec = v }
                    }
                    SettingRow {
                        visible: App.settings.showCapturePopup
                        height: App.settings.showCapturePopup ? 44 : 0
                        label: qsTr("Hide it during fullscreen / Do Not Disturb")
                        USwitch { checked: App.settings.muteOnFullscreen; onToggled: (c) => App.settings.muteOnFullscreen = c }
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
                        label: qsTr("Start at login (minimized to tray)")
                        USwitch { checked: App.autostartEnabled; onToggled: (c) => App.autostartEnabled = c }
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
                            // filenamePreview() is a plain Q_INVOKABLE with no
                            // dependency tracking — reference the settings it
                            // uses so the preview updates while typing.
                            text: {
                                App.settings.filenameTemplate
                                App.settings.imageFormat
                                return qsTr("Preview: %1").arg(App.filenamePreview())
                            }
                            color: Theme.accent
                            font.pixelSize: Theme.fontS
                            font.family: "monospace"
                        }
                    }
                }
            }

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

        }

        // ===== Appearance =====
        ScrollPane {
            visible: page.tab === 1
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

            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Window decoration") }
                    SettingRow {
                        label: qsTr("Use system window decoration")
                        USwitch { checked: App.settings.useSystemDecoration; onToggled: (c) => App.settings.useSystemDecoration = c }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Off = Unisic draws its own title bar with themed minimize/maximize/close buttons.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }

            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingM
                    SectionTitle { text: qsTr("System tray icon") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Click an icon to use it in the system tray. Drop your own .png/.svg files into the icons folder and they appear here automatically.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }

                    Flow {
                        width: parent.width
                        spacing: Theme.spacingS

                        Repeater {
                            // Rebuilds whenever the drop-in folder or the current
                            // selection changes (both referenced below).
                            model: {
                                // Referenced so the thumbnails re-render when the
                                // OS light/dark scheme flips.
                                var color = App.trayContrastColor
                                var bundled = App.bundledTrayIcons
                                var presets = App.trayIconPresets
                                var cur = App.settings.trayIconPath
                                var arr = [{ path: "", label: qsTr("Default"),
                                             src: "qrc:/resources/icons/unisic.svg" }]
                                // App-shipped presets are monochrome — recolor the
                                // preview to contrast the scheme, like the tray.
                                for (var b = 0; b < bundled.length; b++)
                                    arr.push({ path: bundled[b],
                                               label: page.iconLabel(bundled[b]),
                                               src: App.trayIconThumb(bundled[b], color) })
                                for (var i = 0; i < presets.length; i++)
                                    arr.push({ path: presets[i],
                                               label: page.iconLabel(presets[i]),
                                               src: page.fileUrl(presets[i]) })
                                // A file picked from elsewhere still gets a tile so
                                // the current choice is always visible/selected.
                                if (cur !== "" && presets.indexOf(cur) === -1
                                        && bundled.indexOf(cur) === -1)
                                    arr.push({ path: cur, label: page.iconLabel(cur),
                                               src: page.fileUrl(cur) })
                                return arr
                            }
                            delegate: Rectangle {
                                id: tile
                                required property var modelData
                                readonly property bool sel: App.settings.trayIconPath === modelData.path
                                width: 62; height: 62
                                radius: Theme.radiusM
                                color: sel ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                                            : Theme.surfaceHi
                                border.width: sel ? 2 : 1
                                border.color: sel ? Theme.accent : Theme.divider
                                Image {
                                    anchors.centerIn: parent
                                    width: 38; height: 38
                                    source: tile.modelData.src
                                    fillMode: Image.PreserveAspectFit
                                    sourceSize.width: 76; sourceSize.height: 76
                                    smooth: true
                                    asynchronous: true
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: App.selectTrayIcon(tile.modelData.path)
                                    ToolTip.text: tile.modelData.label
                                    ToolTip.visible: containsMouse && tile.modelData.path !== ""
                                    ToolTip.delay: 500
                                }
                            }
                        }

                        // "+" tile — pick an image; it is copied into the icons
                        // folder and selected.
                        Rectangle {
                            width: 62; height: 62
                            radius: Theme.radiusM
                            color: Theme.surfaceHi
                            border.width: 1
                            border.color: Theme.divider
                            Text {
                                anchors.centerIn: parent
                                text: "+"
                                color: Theme.textSecondary
                                font.pixelSize: 30
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: App.addTrayIcon()
                                ToolTip.text: qsTr("Add an icon (copies it here)")
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 500
                            }
                        }
                    }

                    Row {
                        spacing: Theme.spacingS
                        UButton { compact: true; variant: "tonal"; iconName: "folder-open"; text: qsTr("Open folder with icons"); onClicked: App.openDirectory(App.trayIconsDir()) }
                        UButton {
                            compact: true; variant: "ghost"; text: qsTr("Reset")
                            visible: App.settings.trayIconPath !== ""
                            onClicked: App.clearTrayIcon()
                        }
                    }
                }
            }

            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Editor tool icons") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Choose the icon set for the drawing tools only — the main app icons stay fixed.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Icon style")
                        UComboBox {
                            width: 220
                            model: [qsTr("Custom (bundled)"), qsTr("System (desktop theme)")]
                            currentIndex: App.settings.editorIconStyle === "system" ? 1 : 0
                            onActivated: (i) => App.settings.editorIconStyle = (i === 1 ? "system" : "custom")
                        }
                    }
                    Text {
                        visible: App.settings.editorIconStyle === "system"
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Optional: override individual tools with a freedesktop icon name.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    Repeater {
                        model: App.settings.editorIconStyle === "system"
                               ? ToolCatalog.tools.filter(function (t) { return t.editor || t.overlay })
                               : []
                        delegate: Item {
                            width: parent.width
                            height: 44
                            Row {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                UIcon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: page.iconOverride(modelData.id) || modelData.iconName
                                    iconStyle: "system"
                                    size: 18
                                    color: Theme.textSecondary
                                }
                                Text { text: modelData.label; color: Theme.textPrimary; font.pixelSize: Theme.fontM; anchors.verticalCenter: parent.verticalCenter }
                            }
                            UTextField {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 200
                                text: page.iconOverride(modelData.id)
                                placeholder: modelData.iconName
                                onEdited: (t) => page.setIconOverride(modelData.id, t.trim())
                            }
                        }
                    }
                }
            }

        }

        // ===== Editor =====
        ScrollPane {
            visible: page.tab === 2
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
        }

        // ===== Recording =====
        ScrollPane {
            visible: page.tab === 3
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Recording") }
                    SettingRow {
                        label: qsTr("GIF frame rate")
                        UComboBox { width: 130; model: ["15 FPS", "30 FPS", "45 FPS", "60 FPS"]; readonly property var opts: [15,30,45,60]; currentIndex: page.nearestFps(App.settings.gifFps); onActivated: (i) => App.settings.gifFps = opts[i] }
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
                        UComboBox { width: 130; model: ["15 FPS", "30 FPS", "45 FPS", "60 FPS"]; readonly property var opts: [15,30,45,60]; currentIndex: page.nearestFps(App.settings.videoFps); onActivated: (i) => App.settings.videoFps = opts[i] }
                    }
                }
            }

            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Audio") }
                    SettingRow {
                        label: qsTr("Record system audio")
                        USwitch { checked: App.settings.recordSystemAudio; onToggled: (c) => App.settings.recordSystemAudio = c }
                    }
                    SettingRow {
                        label: qsTr("Record microphone")
                        USwitch { checked: App.settings.recordMicrophone; onToggled: (c) => App.settings.recordMicrophone = c }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Applies to video recordings (MP4/WebM) — GIFs have no audio.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }
        }

        // ===== Hotkeys =====
        ScrollPane {
            visible: page.tab === 4

            // KGlobalAccel missing (niri/sway/GNOME…): the recorders below
            // would be dead — explain the compositor-bind route instead.
            UCard {
                visible: !App.hotkeysAvailable
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Global hotkeys unavailable on this desktop") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        textFormat: Text.MarkdownText
                        text: qsTr("This desktop has no KGlobalAccel service, so Unisic cannot register global shortcuts itself. Bind keys in your compositor instead — a running Unisic instance picks the command up:\n\n" +
                                   "```\nunisic --region | --fullscreen | --window | --gif\n```\n\n" +
                                   "niri (`config.kdl`):\n\n" +
                                   "```\nbinds {\n    Mod+Shift+S { spawn \"unisic\" \"--region\"; }\n    Print { spawn \"unisic\" \"--fullscreen\"; }\n}\n```")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontS + 1
                    }
                }
            }

            UCard {
                visible: App.hotkeysAvailable
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Global hotkeys") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: App.hotkeyBackend === "portal"
                              ? qsTr("Registered through the system GlobalShortcuts portal. Your desktop may show a one-time confirmation dialog; the binding it decides on is final (on Hyprland bind the ids in hyprland.conf).")
                              : qsTr("Registered through KDE global shortcuts (KGlobalAccel). Use Qt key notation, e.g. Meta+Shift+2.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Full screen")
                        UShortcutRecorder { width: 220; shortcut: App.settings.hotkeyFullScreen; onRecorded: (t) => { App.settings.hotkeyFullScreen = t; App.applyHotkey("capture-fullscreen") } }
                    }
                    SettingRow {
                        label: qsTr("Region")
                        UShortcutRecorder { width: 220; shortcut: App.settings.hotkeyRegion; onRecorded: (t) => { App.settings.hotkeyRegion = t; App.applyHotkey("capture-region") } }
                    }
                    SettingRow {
                        label: qsTr("Window")
                        UShortcutRecorder { width: 220; shortcut: App.settings.hotkeyWindow; onRecorded: (t) => { App.settings.hotkeyWindow = t; App.applyHotkey("capture-window") } }
                    }
                    SettingRow {
                        label: qsTr("Video start/stop")
                        UShortcutRecorder { width: 220; shortcut: App.settings.hotkeyRecord; onRecorded: (t) => { App.settings.hotkeyRecord = t; App.applyHotkey("record-video") } }
                    }
                    SettingRow {
                        label: qsTr("GIF start/stop")
                        UShortcutRecorder { width: 220; shortcut: App.settings.hotkeyGif; onRecorded: (t) => { App.settings.hotkeyGif = t; App.applyHotkey("record-gif") } }
                    }
                    UButton {
                        anchors.right: parent.right
                        text: qsTr("Apply hotkeys")
                        compact: true
                        onClicked: { App.applyHotkeys(); App.showToast(qsTr("Hotkeys re-registered")) }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: App.hotkeyBackend === "portal"
                              ? qsTr("Keys recorded here are suggestions passed to the portal — the system dialog confirms or adjusts them.")
                              : qsTr("Shortcuts apply immediately and stay in sync with KDE System Settings — an edit made there shows up here too.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                }
            }
        }

        // ===== Developer (dev build only, tab 5) =====
        ScrollPane {
            visible: page.tab === 5
            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Developer") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Dev build. Compositor capabilities detected on this system. F8 (or the button) runs the full smoke test.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    SettingRow {
                        label: qsTr("Native notifications")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capNativeNotification ? "✓" : "—"
                               color: App.capNativeNotification ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    SettingRow {
                        label: qsTr("Custom card (layer-shell)")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capCustomNotification ? "✓" : "—"
                               color: App.capCustomNotification ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    SettingRow {
                        label: qsTr("Recording border")
                        Text { anchors.verticalCenter: parent.verticalCenter; text: App.capRecordBorder ? "✓" : "—"
                               color: App.capRecordBorder ? Theme.accent : Theme.textTertiary; font.pixelSize: Theme.fontL }
                    }
                    UButton {
                        compact: true; variant: "tonal"
                        text: App.smokeTestRunning ? qsTr("Running…") : qsTr("Run full smoke test (F8)")
                        enabled: !App.smokeTestRunning
                        onClicked: App.runSmokeTest()
                    }
                    Rectangle {
                        visible: App.smokeTestLog !== ""
                        width: parent.width
                        height: 200
                        radius: Theme.radiusM
                        color: Theme.background
                        border.width: 1
                        border.color: Theme.divider
                        clip: true
                        Flickable {
                            anchors.fill: parent
                            anchors.margins: 8
                            contentWidth: width
                            contentHeight: logText.height
                            boundsBehavior: Flickable.StopAtBounds
                            Text {
                                id: logText
                                width: parent.width
                                text: App.smokeTestLog
                                color: Theme.textSecondary
                                font.family: "monospace"
                                font.pixelSize: Theme.fontS
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                }
            }

            UCard {
                width: page.cardWidth
                Column {
                    width: parent.width
                    spacing: Theme.spacingS
                    SectionTitle { text: qsTr("Run a single action") }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("Trigger each path on its own to verify it by hand. Every new feature must add its trigger here and to the smoke test.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    Flow {
                        width: parent.width
                        spacing: Theme.spacingS
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture fullscreen"); onClicked: App.captureFullScreen() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture region"); onClicked: App.captureRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Capture window"); onClicked: App.captureWindow() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec GIF (screen)"); onClicked: App.startGifFullScreen() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec GIF (region)"); onClicked: App.startGifRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec MP4 (screen)"); onClicked: App.startVideoScreen() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec MP4 (region)"); onClicked: App.startVideoRegion() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Rec MP4 (window)"); onClicked: App.startVideoWindow() }
                        UButton { compact: true; variant: "danger"; text: qsTr("Stop recording"); onClicked: App.stopRecording() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Test notification"); onClicked: App.devTestNotification() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Open editor"); onClicked: App.devTestEditor() }
                        UButton { compact: true; variant: "tonal"; text: qsTr("Add history entry"); onClicked: App.devTestHistory() }
                    }
                }
            }
        }
    }
}

