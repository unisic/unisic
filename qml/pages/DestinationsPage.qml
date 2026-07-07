import QtQuick
import Unisic
import "../components"

Item {
    id: page

    property var editing: null   // destination map being edited, or null

    function destNames() {
        var names = []
        for (var i = 0; i < App.uploads.destinations.length; ++i)
            names.push(App.uploads.destinations[i].name)
        return names
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        contentHeight: col.height
        clip: true

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

            UButton {
                iconName: "list-add"; text: qsTr("Add custom destination")
                onClicked: {
                    page.editing = { name: "", type: "http", requestUrl: "", method: "POST",
                                     fileFormName: "file", responseType: "json", urlPath: "$json:url$" }
                    editSheet.open()
                }
            }
        }
    }

    // ------- edit sheet (modal card) -------
    Rectangle {
        id: editSheet
        function open() { visible = true }
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
                    text: page.editing ? (page.editing.name || "") : ""
                }
                UComboBox {
                    id: fType; width: parent.width
                    model: ["http", "curl"]
                    currentIndex: page.editing && page.editing.type === "curl" ? 1 : 0
                }
                UTextField {
                    id: fUrl; width: parent.width
                    placeholder: fType.currentIndex === 1
                                 ? qsTr("sftp://host/path/  or  ftp://host/path/")
                                 : qsTr("Request URL (https://…)")
                    text: page.editing ? (page.editing.requestUrl || "") : ""
                }
                UTextField {
                    id: fFormName; width: parent.width
                    visible: fType.currentIndex === 0
                    placeholder: qsTr("File form field name (e.g. file)")
                    text: page.editing ? (page.editing.fileFormName || "file") : "file"
                }
                UTextField {
                    id: fUrlPath; width: parent.width
                    visible: fType.currentIndex === 0
                    placeholder: qsTr("URL extractor: $text$, $json:files[0].url$ or $regex:…$")
                    text: page.editing ? (page.editing.urlPath || "") : ""
                }
                UTextField {
                    id: fHeaders; width: parent.width
                    visible: fType.currentIndex === 0
                    placeholder: qsTr("Headers as JSON, e.g. {\"Authorization\":\"Bearer x\"}")
                    text: page.editing && page.editing.headers ? JSON.stringify(page.editing.headers) : ""
                }
                UTextField {
                    id: fUser; width: parent.width
                    visible: fType.currentIndex === 1
                    placeholder: qsTr("user:password (curl -u)")
                    text: page.editing ? (page.editing.user || "") : ""
                }
                UTextField {
                    id: fPublicBase; width: parent.width
                    visible: fType.currentIndex === 1
                    placeholder: qsTr("Public URL base (optional, for the copied link)")
                    text: page.editing ? (page.editing.publicUrlBase || "") : ""
                }

                Row {
                    spacing: Theme.spacingM
                    anchors.right: parent.right
                    UButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: editSheet.close() }
                    UButton {
                        text: qsTr("Save")
                        enabled: fName.text.trim() !== "" && fUrl.text.trim() !== ""
                        onClicked: {
                            var d = {
                                name: fName.text.trim(),
                                type: fType.currentIndex === 1 ? "curl" : "http",
                                requestUrl: fUrl.text.trim(),
                                method: "POST"
                            }
                            if (d.type === "http") {
                                d.fileFormName = fFormName.text.trim() || "file"
                                d.urlPath = fUrlPath.text.trim() || "$text$"
                                d.responseType = d.urlPath.indexOf("$json:") === 0 ? "json" : "text"
                                if (fHeaders.text.trim() !== "") {
                                    try { d.headers = JSON.parse(fHeaders.text) } catch (e) {}
                                }
                            } else {
                                if (fUser.text.trim() !== "") d.user = fUser.text.trim()
                                if (fPublicBase.text.trim() !== "") d.publicUrlBase = fPublicBase.text.trim()
                            }
                            App.uploads.saveDestination(d)
                            editSheet.close()
                        }
                    }
                }
            }
        }
    }
}
