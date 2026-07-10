import QtQuick
import QtQuick.Dialogs
import Unisic
import "../components"

Item {
    id: page

    property var editing: null   // destination map being edited, or null

    FileDialog {
        id: sxcuDialog
        title: qsTr("Import ShareX uploader (.sxcu)")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("ShareX custom uploader (*.sxcu *.json)"), qsTr("All files (*)")]
        onAccepted: {
            var name = App.uploads.importSxcu(selectedFile)
            if (name !== "") {
                App.settings.activeDestination = name
                App.showToast(qsTr("Imported “%1”").arg(name))
            } else {
                App.showToast(App.uploads.lastImportError())
            }
        }
    }

    function destNames() {
        var names = []
        for (var i = 0; i < App.uploads.destinations.length; ++i)
            names.push(App.uploads.destinations[i].name)
        return names
    }

    Flickable {
        id: pageFlick
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        MiddleScroll { flickable: pageFlick }

        Column {
            id: col
            width: parent.width
            spacing: Theme.spacingL

            Text {
                text: qsTr("Destinations")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Modular uploaders, ShareX-style: custom HTTP APIs plus FTP/SFTP via curl. After every upload the link is copied to your clipboard.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontM
            }

            UCard {
                width: Math.min(parent.width, 694)
                Column {
                    width: parent.width
                    spacing: Theme.spacingM

                    Item {
                        width: parent.width; height: 40
                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Active destination")
                            color: Theme.textPrimary; font.pixelSize: Theme.fontM
                        }
                        UComboBox {
                            id: activeCombo
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            model: page.destNames()
                            currentIndex: Math.max(0, page.destNames().indexOf(App.settings.activeDestination))
                            onActivated: (i) => App.settings.activeDestination = model[i]
                        }
                    }
                }
            }

            Repeater {
                model: App.uploads.destinations
                delegate: Rectangle {
                    width: Math.min(col.width, 694)
                    height: 64
                    radius: Theme.radiusL
                    color: Theme.surface
                    border.width: 1
                    border.color: App.settings.activeDestination === modelData.name ? Theme.accent : Theme.divider

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingL
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingM
                        UIcon { name: modelData.type === "curl" ? "lock" : "globe"; size: 22; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text { text: modelData.name; color: Theme.textPrimary; font.pixelSize: Theme.fontM; font.weight: Font.DemiBold }
                            Text { text: modelData.requestUrl || ""; color: Theme.textTertiary; font.pixelSize: Theme.fontS }
                        }
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingM
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        UButton {
                            compact: true; variant: "ghost"; text: qsTr("Use")
                            visible: App.settings.activeDestination !== modelData.name
                            onClicked: App.settings.activeDestination = modelData.name
                        }
                        UIconButton {
                            iconName: "edit"; iconSize: 16
                            onClicked: { page.editing = JSON.parse(JSON.stringify(modelData)); editSheet.open() }
                        }
                        UIconButton {
                            iconName: "edit-delete"; iconSize: 16
                            visible: !modelData.builtin
                            onClicked: App.uploads.removeDestination(modelData.name)
                        }
                    }
                }
            }

            Row {
                spacing: Theme.spacingM
                UButton {
                    iconName: "list-add"; text: qsTr("Add custom destination")
                    onClicked: {
                        page.editing = { name: "", type: "http", requestUrl: "", method: "POST",
                                         fileFormName: "file", responseType: "json", urlPath: "$json:url$" }
                        editSheet.open()
                    }
                }
                UButton {
                    iconName: "folder-open"; variant: "tonal"; text: qsTr("Import .sxcu")
                    onClicked: sxcuDialog.open()
                }
            }
        }
    }

    // ------- edit sheet (modal card) -------
    Rectangle {
        id: editSheet
        // Populate imperatively: `text:` bindings on the fields would die on
        // the first keystroke, so reopening for another destination would
        // show the previously typed values.
        function open() {
            var e = page.editing || {}
            fName.text = e.name || ""
            fType.currentIndex = e.type === "curl" ? 1 : 0
            fUrl.text = e.requestUrl || ""
            fBody.currentIndex = e.body === "json" ? 1 : 0
            fFormName.text = e.fileFormName || "file"
            fData.text = e.data || ""
            fArgs.text = e.arguments ? JSON.stringify(e.arguments) : ""
            fUrlPath.text = e.urlPath || ""
            fHeaders.text = e.headers ? JSON.stringify(e.headers) : ""
            fUser.text = e.user || ""
            fPublicBase.text = e.publicUrlBase || ""
            visible = true
        }
        function close() { visible = false; page.editing = null }
        visible: false
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)
        z: 200

        MouseArea { anchors.fill: parent; onClicked: editSheet.close() }

        Rectangle {
            anchors.centerIn: parent
            width: 520
            height: sheetCol.height + 2 * Theme.spacingXL
            radius: Theme.radiusXL
            color: Theme.surfaceHi
            border.width: 1
            border.color: Theme.divider

            MouseArea { anchors.fill: parent } // swallow clicks

            Column {
                id: sheetCol
                anchors.top: parent.top
                anchors.topMargin: Theme.spacingXL
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Theme.spacingXL
                spacing: Theme.spacingM

                Text {
                    text: page.editing && page.editing.name !== "" ? qsTr("Edit destination") : qsTr("New destination")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontXL
                    font.weight: Font.Bold
                }

                UTextField {
                    id: fName; width: parent.width
                    placeholder: qsTr("Name")
                }
                UComboBox {
                    id: fType; width: parent.width
                    model: ["http", "curl"]
                    onActivated: (i) => fType.currentIndex = i
                }
                UTextField {
                    id: fUrl; width: parent.width
                    placeholder: fType.currentIndex === 1
                                 ? qsTr("sftp://host/path/  or  ftp://host/path/")
                                 : qsTr("Request URL (https://…)")
                }
                UComboBox {
                    id: fBody; width: parent.width
                    visible: fType.currentIndex === 0
                    model: [qsTr("Multipart form-data (upload the file)"), qsTr("Custom JSON body")]
                    onActivated: (i) => fBody.currentIndex = i
                }
                UTextField {
                    id: fFormName; width: parent.width
                    visible: fType.currentIndex === 0 && fBody.currentIndex === 0
                    placeholder: qsTr("File form field name (e.g. file)")
                }
                UTextField {
                    id: fData; width: parent.width
                    visible: fType.currentIndex === 0 && fBody.currentIndex === 1
                    placeholder: qsTr("JSON body — tokens $base64$, $filename$, $mime$")
                }
                UTextField {
                    id: fArgs; width: parent.width
                    visible: fType.currentIndex === 0 && fBody.currentIndex === 0
                    placeholder: qsTr("Extra form fields as JSON, e.g. {\"reqtype\":\"fileupload\"}")
                }
                UTextField {
                    id: fUrlPath; width: parent.width
                    visible: fType.currentIndex === 0
                    placeholder: qsTr("URL extractor: $text$, $json:files[0].url$ or $regex:…$")
                }
                UTextField {
                    id: fHeaders; width: parent.width
                    visible: fType.currentIndex === 0
                    placeholder: qsTr("Headers as JSON, e.g. {\"Authorization\":\"Bearer x\"}")
                }
                UTextField {
                    id: fUser; width: parent.width
                    visible: fType.currentIndex === 1
                    placeholder: qsTr("user:password (curl -u)")
                }
                UTextField {
                    id: fPublicBase; width: parent.width
                    visible: fType.currentIndex === 1
                    placeholder: qsTr("Public URL base (optional, for the copied link)")
                }

                Row {
                    spacing: Theme.spacingM
                    anchors.right: parent.right
                    UButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: editSheet.close() }
                    UButton {
                        text: qsTr("Save")
                        enabled: fName.text.trim() !== "" && fUrl.text.trim() !== ""
                        onClicked: {
                            // Deep-copy HERE, not an alias of page.editing: the
                            // validation early-returns below must leave the sheet
                            // state untouched, or a failed Save corrupts `orig`
                            // for the retry (rename cleanup then misses). Keys the
                            // form does not own (method, deletionUrlPath, builtin,
                            // urlReplace, …) still survive the round-trip.
                            var d = JSON.parse(JSON.stringify(page.editing || {}))
                            var orig = (page.editing && page.editing.name) || ""
                            d.name = fName.text.trim()
                            d.type = fType.currentIndex === 1 ? "curl" : "http"
                            d.requestUrl = fUrl.text.trim()
                            if (!d.method) d.method = "POST"
                            if (d.type === "http") {
                                d.urlPath = fUrlPath.text.trim() || "$text$"
                                d.responseType = d.urlPath.indexOf("$json:") === 0 ? "json" : "text"
                                if (fHeaders.text.trim() !== "") {
                                    try { d.headers = JSON.parse(fHeaders.text) }
                                    catch (e) {
                                        // Silently dropping the auth header would be worse.
                                        App.showToast(qsTr("Headers are not valid JSON — fix or clear the field"))
                                        return
                                    }
                                } else {
                                    delete d.headers
                                }
                                if (fBody.currentIndex === 1) {
                                    delete d.fileFormName
                                    delete d.arguments
                                    d.body = "json"
                                    d.data = fData.text
                                } else {
                                    delete d.body
                                    delete d.data
                                    d.fileFormName = fFormName.text.trim() || "file"
                                    if (fArgs.text.trim() !== "") {
                                        try { d.arguments = JSON.parse(fArgs.text) }
                                        catch (e) {
                                            App.showToast(qsTr("Extra form fields are not valid JSON — fix or clear the field"))
                                            return
                                        }
                                    } else {
                                        delete d.arguments
                                    }
                                }
                            } else {
                                if (fUser.text.trim() !== "") d.user = fUser.text.trim()
                                else delete d.user
                                if (fPublicBase.text.trim() !== "") d.publicUrlBase = fPublicBase.text.trim()
                                else delete d.publicUrlBase
                            }
                            App.uploads.saveDestination(d)
                            // Renaming: drop the old entry so it doesn't linger as a
                            // duplicate, and keep the active-destination pointer valid.
                            if (orig !== "" && orig !== d.name) {
                                App.uploads.removeDestination(orig)
                                if (App.settings.activeDestination === orig)
                                    App.settings.activeDestination = d.name
                            }
                            editSheet.close()
                        }
                    }
                }
            }
        }
    }
}
