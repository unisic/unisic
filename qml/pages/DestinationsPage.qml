import QtQuick
import QtQuick.Dialogs
import Unisic
import Unisic.Kit
import "../components"

Item {
    id: page

    property var editing: null   // destination map being edited, or null

    // The page's half of the window-wide "is a modal up?" question (Main.qml
    // reads it off the active page). Drag-and-drop and Ctrl+V are off while
    // this is true, or a dropped file would open an editor BEHIND the sheet.
    readonly property bool modalOpen: editSheet.visible || sxcuDialog.visible
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

    // A placeholder vanishes the moment the field has a value, so every control
    // in the edit sheet carries a persistent caption above it instead. The
    // placeholder stays as the example. Inline component: no CMakeLists entry.
    component Labeled: Column {
        id: labeled
        property alias label: labelText.text
        property alias hint: hintText.text
        default property alias content: slot.data
        width: parent ? parent.width : 0
        spacing: Theme.spacingXS

        // The caption speaks for the field under it - UNameBridge owns that
        // rule and explains it. Held by a property, not written as a child:
        // this component's DEFAULT property is redirected to `slot`, so a plain
        // child object would land in the caller's content slot instead.
        readonly property UNameBridge nameBridge: UNameBridge {
            targets: [slot]
            name: labelText.text
            description: hintText.text
        }

        Text {
            id: labelText
            color: Theme.textSecondary
            font.pixelSize: Theme.fontS
            font.weight: Font.DemiBold
        }
        Item {
            id: slot
            width: parent.width
            height: childrenRect.height
            onChildrenChanged: labeled.nameBridge.refresh()
        }
        Text {
            id: hintText
            width: parent.width
            visible: text !== ""
            wrapMode: Text.WordWrap
            color: Theme.textTertiary
            font.pixelSize: Theme.fontS
        }
    }

    // A text field for a value that can carry template variables. It draws the
    // variables already in the text as pills; the chips that type new ones are
    // NOT here, they are one floating UVarBar in the sheet that follows
    // whichever of these has focus. Both halves read the same answer from
    // UploadManager, the only thing that performs the substitutions - a chip
    // for a token the sender ignores would look like it worked.
    //
    // Why chips at all: %file% and $json:data.link$ were discoverable only from
    // a placeholder that vanishes the moment anything is typed, so the syntax
    // had to be remembered or re-derived. Clicking a chip types it correctly,
    // and a pill around it afterwards is the proof it IS a variable and not six
    // characters of punctuation that happen to look like one.
    component VarField: Item {
        id: varField
        // { pattern: <regex for the pills>, vars: [{token,label,description,caretBack}] }
        property var help: null
        property alias text: varInput.text
        property alias placeholder: varInput.placeholder
        // Both halves of the identity are forwarded, so UNameBridge treats this
        // whole unit as the row's one control and the caption lands on the
        // field. The chips are a pointer shortcut that sits outside this
        // field's place in the tab chain, so what each variable does is spelled
        // out in the row's visible hint as well, where a keyboard or
        // screen-reader user meets it.
        property alias accessibleName: varInput.accessibleName
        property alias accessibleDescription: varInput.accessibleDescription
        // Read by the sheet to decide whose variables the bar is showing.
        readonly property alias focused: varInput.inputActiveFocus
        readonly property var vars: help && help.vars ? help.vars : []

        width: parent ? parent.width : 0
        height: varInput.height

        // Forwarded rather than aliased: UVarBar drives the focused field
        // through this name whether it is a UTextField or a wrapper like this.
        function insertToken(token, caretBack) { varInput.insertToken(token, caretBack) }

        UTextField {
            id: varInput
            width: parent.width
            tokenPattern: varField.help ? (varField.help.pattern || "") : ""
        }
    }

    // The variables a field understands, asked of the thing that substitutes
    // them. `type` is the destination kind: the two senders do not substitute
    // the same set, and the request URL's one token means something slightly
    // different in each.
    function templateHelp(field, curl) {
        // Read for its dependency only: engine.retranslate() re-evaluates qsTr
        // in QML, but these labels are tr()'d in C++, so without this a live
        // language switch would leave the chips in the old language.
        void App.settings.uiLanguage
        return App.uploads.templateHelp(field, curl ? "curl" : "http")
    }

    // Carried by BOTH halves of the page below (the pinned header and the
    // scrolling list): `enabled` propagates to children, so this is what takes
    // the page out of the tab chain while the modal edit sheet is up. Without
    // it, Tab past Save walked onto the controls behind the scrim - including
    // the Delete button of a row the user cannot even see. (Verified with QTest
    // key events: a disabled subtree is skipped by Qt Quick's tab navigation,
    // an enabled one is not.) The header needs it as much as the list does -
    // "Add custom server" behind the scrim would open a second sheet.
    readonly property bool interactive: !editSheet.visible

    // Pinned, like the Settings header: the two ways to ADD a server must not
    // depend on how many servers you already have. Measured at the default
    // 1060x700 with the five shipped destinations, they used to be the last row
    // of the scrolled column and landed at y=698 in a 700 px window - 2 px of a
    // 42 px button, and the only route to a new server at all. Only the list
    // below scrolls now, so they stay put however long it gets.
    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingXL
        anchors.bottomMargin: 0
        height: titleRow.height + Theme.spacingS + intro.implicitHeight
        enabled: page.interactive

        Item {
            id: titleRow
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Math.max(44, addRow.height)

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Servers")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
            }
            Row {
                id: addRow
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingS
                UButton {
                    id: addBtn
                    compact: true
                    iconName: "list-add"; text: qsTr("Add custom server")
                    onClicked: {
                        page.editing = { name: "", type: "http", requestUrl: "", method: "POST",
                                         fileFormName: "file", responseType: "json", urlPath: "$json:url$" }
                        editSheet.open(addBtn)
                    }
                }
                UButton {
                    compact: true
                    iconName: "folder-open"; variant: "tonal"; text: qsTr("Import .sxcu")
                    onClicked: sxcuDialog.open()
                }
            }
        }
        Text {
            id: intro
            anchors.top: titleRow.bottom
            anchors.topMargin: Theme.spacingS
            anchors.left: parent.left
            anchors.right: parent.right
            wrapMode: Text.WordWrap
            text: qsTr("Modular uploaders: custom HTTP APIs plus FTP/SFTP via curl. After every upload the link is copied to your clipboard.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontM
        }
    }

    Flickable {
        id: pageFlick
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingL
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spacingXL
        anchors.rightMargin: Theme.spacingXL
        anchors.bottomMargin: Theme.spacingXL
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        enabled: page.interactive

        MiddleScroll { flickable: pageFlick }
        WheelBoost { flickable: pageFlick }
        // Tab must not walk off the bottom of the list once there are more
        // servers than fit - one implementation, in the kit (FocusScroll.qml).
        FocusScroll { flickable: pageFlick }

        Column {
            id: col
            width: parent.width
            spacing: Theme.spacingL

            USettingRow {
                width: parent.width
                label: qsTr("Active server")
                UComboBox {
                    id: activeCombo
                    model: page.destNames()
                    currentIndex: Math.max(0, page.destNames().indexOf(App.settings.activeDestination))
                    onActivated: (i) => App.settings.activeDestination = model[i]
                }
            }

            // The server list is ONE group of cards, so it uses the same tight
            // card rhythm Settings uses inside a section (spacingS) rather than
            // the between-sections spacingL. That is also what buys the fit: at
            // 1060x700 the five shipped destinations came to 476 px of content
            // in a 462 px viewport, and their four 20 px gaps were part of it -
            // now 420 px of content with 42 px to spare.
            Column {
                id: listCol
                width: parent.width
                spacing: Theme.spacingS

                // Defensive empty state: ensureBuiltins() normally keeps at least the
                // built-in services here, so this shows only if every one is removed.
                Text {
                    visible: App.uploads.destinations.length === 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("No upload destinations yet. Add one to send captures straight to your own server or a public host.")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontM
                }

                Repeater {
                    model: App.uploads.destinations
                    delegate: Rectangle {
                        width: listCol.width
                        height: 64
                        radius: Theme.radiusM
                        color: Theme.surface
                        border.width: 1
                        border.color: App.settings.activeDestination === modelData.name ? Theme.accent : Theme.divider

                        // The row is a list entry, not a control (its buttons are
                        // the controls); "in use" is only shown as an accent border
                        // today, so it has to be spoken here.
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.name
                        Accessible.description: modelData.requestUrl || ""
                        Accessible.selectable: true
                        Accessible.selected: App.settings.activeDestination === modelData.name

                        Row {
                            id: infoRow
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingL
                            // Stop before the action buttons so a long request URL
                            // elides instead of sliding under Use/Edit/Delete.
                            anchors.right: actionRow.left
                            anchors.rightMargin: Theme.spacingM
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spacingM
                            UIcon { name: modelData.type === "curl" ? "lock" : "globe"; size: 22; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                            Column {
                                id: infoCol
                                width: infoRow.width - 22 - Theme.spacingM
                                anchors.verticalCenter: parent.verticalCenter
                                // Imgur ships without a Client-ID (it is per-user), so
                                // say so on the row instead of letting every upload fail.
                                readonly property bool needsClientId:
                                    App.uploads.isImgurDestination(modelData)
                                    && App.uploads.imgurClientIdOf(modelData) === ""
                                Row {
                                    width: parent.width
                                    spacing: Theme.spacingS
                                    Text {
                                        text: modelData.name; color: Theme.textPrimary
                                        font.pixelSize: Theme.fontM; font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        width: Math.min(implicitWidth, parent.width - (setupChip.visible ? setupChip.width + Theme.spacingS : 0))
                                    }
                                    Rectangle {
                                        id: setupChip
                                        visible: infoCol.needsClientId
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: setupChipText.implicitWidth + 12
                                        height: 18
                                        radius: 9
                                        color: Theme.alpha(Theme.danger, 0.18)
                                        border.width: 1
                                        border.color: Theme.alpha(Theme.danger, 0.5)
                                        Text {
                                            id: setupChipText
                                            anchors.centerIn: parent
                                            text: qsTr("Needs a Client-ID")
                                            color: Theme.danger
                                            font.pixelSize: Theme.fontS - 2
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                }
                                Text { width: parent.width; text: modelData.requestUrl || ""; color: Theme.textTertiary; font.pixelSize: Theme.fontS; elide: Text.ElideMiddle }
                            }
                        }
                        Row {
                            id: actionRow
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingM
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            UButton {
                                compact: true; variant: "ghost"; text: qsTr("Use")
                                accessibleName: qsTr("Use %1 for uploads").arg(modelData.name)
                                // Disabled, never hidden: flipping `visible` here
                                // reflowed Edit/Delete sideways under the pointer
                                // the moment the row was activated. The row already
                                // says it is the active one (accent border, and
                                // Accessible.selected on the row itself).
                                enabled: App.settings.activeDestination !== modelData.name
                                onClicked: App.settings.activeDestination = modelData.name
                            }
                            UIconButton {
                                id: editBtn
                                iconName: "edit"; iconSize: 16
                                accessibleName: qsTr("Edit %1").arg(modelData.name)
                                onClicked: {
                                    page.editing = JSON.parse(JSON.stringify(modelData))
                                    editSheet.open(editBtn)
                                }
                            }
                            UIconButton {
                                iconName: "edit-delete"; iconSize: 16
                                accessibleName: qsTr("Delete %1").arg(modelData.name)
                                visible: !modelData.builtin
                                onClicked: {
                                    // Clear the active pointer first, or it dangles at a
                                    // removed name (combo and list then disagree).
                                    if (App.settings.activeDestination === modelData.name)
                                        App.settings.activeDestination = ""
                                    App.uploads.removeDestination(modelData.name)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ------- edit sheet (modal card) -------
    Rectangle {
        id: editSheet
        // The button that opened the sheet: closing hands the keyboard back to
        // it. It is an Item property, so QML nulls it by itself if that row is
        // gone by then (a rename rebuilds the whole list) - the fallback in
        // close() then keeps focus on the page instead of letting it drop to
        // the sidebar behind.
        property Item returnFocus: null
        // Populate imperatively: `text:` bindings on the fields would die on
        // the first keystroke, so reopening for another destination would
        // show the previously typed values.
        function open(from) {
            returnFocus = from || null
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
            fClientId.text = App.uploads.imgurClientIdOf(e)
            fUser.text = e.user || ""
            fPublicBase.text = e.publicUrlBase || ""
            testing = false
            testDone = false
            testUrl = ""
            testError = ""
            sheetFlick.contentY = 0
            visible = true
            // Land the caret in the first field, so the sheet is usable from
            // the keyboard the moment it appears (and a screen reader has
            // something named to announce instead of a bare dialog).
            focusFirst()
        }
        // The sheet's first tab stop. Also where Tab wraps to from Save, so
        // there is exactly one definition of "the top of the form".
        function focusFirst() { fName.forceFocus() }
        function close() {
            visible = false
            page.editing = null
            // A late testFinished() must not repaint a sheet that has moved on.
            testing = false
            testDone = false
            if (returnFocus)
                returnFocus.forceActiveFocus(Qt.OtherFocusReason)
            else
                pageFlick.forceActiveFocus(Qt.OtherFocusReason)
            returnFocus = null
        }
        // Compose the destination object out of the CURRENT form state. Returns
        // null (after the explaining toast) when a JSON field does not parse, so
        // Save and Test always agree on what is being sent.
        function buildDest() {
            // Deep-copy HERE, not an alias of page.editing: the validation
            // early-returns below must leave the sheet state untouched, or a
            // failed Save corrupts `orig` for the retry (rename cleanup then
            // misses). Keys the form does not own (method, deletionUrlPath,
            // builtin, urlReplace, …) still survive the round-trip.
            var d = JSON.parse(JSON.stringify(page.editing || {}))
            d.name = fName.text.trim()
            d.type = fType.currentIndex === 1 ? "curl" : "http"
            d.requestUrl = fUrl.text.trim()
            if (!d.method) d.method = "POST"
            if (d.type === "http") {
                d.urlPath = fUrlPath.text.trim() || "$text$"
                d.responseType = d.urlPath.indexOf("$json:") === 0 ? "json" : "text"
                if (editSheet.imgurMode) {
                    // Client-ID field owns the Authorization header;
                    // any other header the destination carries survives.
                    var h = d.headers || {}
                    if (fClientId.text.trim() !== "")
                        h["Authorization"] = "Client-ID " + fClientId.text.trim()
                    else
                        delete h["Authorization"]
                    if (Object.keys(h).length > 0) d.headers = h
                    else delete d.headers
                } else if (fHeaders.text.trim() !== "") {
                    try { d.headers = JSON.parse(fHeaders.text) }
                    catch (e) {
                        // Silently dropping the auth header would be worse.
                        App.showToast(qsTr("Headers are not valid JSON. Fix or clear the field"))
                        return null
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
                            App.showToast(qsTr("Extra form fields are not valid JSON. Fix or clear the field"))
                            return null
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
                // An empty extractor MEANS something here, so it is not
                // defaulted to $text$ the way the http branch does it: no
                // extractor is how a plain FTP or SFTP folder works, and the
                // link comes from the public URL base instead.
                if (fUrlPath.text.trim() !== "") d.urlPath = fUrlPath.text.trim()
                else delete d.urlPath
                if (fHeaders.text.trim() !== "") {
                    try { d.headers = JSON.parse(fHeaders.text) }
                    catch (e) {
                        App.showToast(qsTr("Headers are not valid JSON. Fix or clear the field"))
                        return null
                    }
                } else {
                    delete d.headers
                }
            }
            return d
        }
        // Live off the URL field (not page.editing) so retyping the URL swaps the
        // credential field immediately. The host test lives in C++ - one rule for
        // the editor, the list badge and the upload precheck.
        readonly property bool imgurMode: App.uploads.isImgurDestination({ requestUrl: fUrl.text })
        // Test-upload state. Local on purpose: App.uploads.busy is global and a
        // real capture upload running in parallel would grey the Test button out.
        property bool testing: false
        property bool testDone: false
        property bool testOk: false
        property string testUrl: ""
        property string testError: ""
        visible: false
        anchors.fill: parent
        color: Theme.alpha(Theme.mediaBase, 0.55)
        z: 200

        Connections {
            target: App.uploads
            function onTestFinished(ok, url, error) {
                // The result can land after the sheet was closed or reopened for
                // another server, and the callback can even fire synchronously
                // inside testDestination() - only the sheet that asked shows it.
                if (!editSheet.visible || !editSheet.testing)
                    return
                editSheet.testing = false
                editSheet.testDone = true
                editSheet.testOk = ok
                editSheet.testUrl = url
                editSheet.testError = error
                sheetFlick.contentY = 0 // the banner sits at the top of the body
            }
        }

        // The dim backdrop is a dismiss target, not content.
        MouseArea {
            anchors.fill: parent
            onClicked: editSheet.close()
            Accessible.ignored: true
        }

        Rectangle {
            id: sheetCard
            anchors.centerIn: parent
            width: 520
            // Grow with the content, but never past the page: the sheet lives
            // inside the clipped content card, so an oversized card would have
            // its Cancel/Save cut off instead of just overlapping.
            height: Math.max(0, Math.min(2 * Theme.spacingXL + sheetTitle.height + Theme.spacingM
                                         + sheetCol.height + Theme.spacingM + sheetFooter.height,
                                         editSheet.height - 2 * Theme.spacingL))
            radius: Theme.radiusXL
            color: Theme.surfaceHi
            border.width: 1
            border.color: Theme.divider

            // Dismiss with Escape like every other overlay in the app. The
            // handler must stay on this card (an ancestor of the fields): a
            // TextInput does not consume Escape, so the key bubbles up here.
            focus: editSheet.visible
            Keys.onEscapePressed: editSheet.close()

            Accessible.role: Accessible.Dialog
            Accessible.name: sheetTitle.text

            MouseArea {
                anchors.fill: parent   // swallow clicks
                Accessible.ignored: true
            }

            // Backward half of the focus trap. It CANNOT be a key handler:
            // measured with real key events, Qt performs Tab/Backtab focus
            // navigation on the item that holds focus and never propagates the
            // key to an ancestor, so a Keys.onBacktabPressed on the first
            // field's UTextField root (its focus lives on the inner TextInput)
            // or on this card never fires at all - one Shift+Tab off the Name
            // field landed on the sidebar behind the scrim, from where Escape
            // no longer closed the sheet and Return destroyed the unsaved form.
            // So the wrap is positional instead: this zero-size stop is the
            // first item of the sheet's tab chain, and anything that backs into
            // it is bounced to the last control. Forward Tab never reaches it -
            // Save/Cancel wrap through focusFirst() straight to the Name field.
            Item {
                id: sheetTabStart
                width: 0
                height: 0
                activeFocusOnTab: editSheet.visible
                Accessible.ignored: true
                onActiveFocusChanged: {
                    if (!activeFocus || !editSheet.visible)
                        return
                    // Save drops out of the chain while the form is
                    // incomplete; Cancel is always there.
                    var last = saveBtn.enabled ? saveBtn : cancelBtn
                    last.forceActiveFocus(Qt.BacktabFocusReason)
                }
            }

            Text {
                id: sheetTitle
                anchors.top: parent.top
                anchors.topMargin: Theme.spacingXL
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingXL
                anchors.rightMargin: Theme.spacingXL
                text: page.editing && page.editing.name !== "" ? qsTr("Edit server") : qsTr("New server")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontXL
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            // Only the fields scroll: title and actions stay pinned, so Save is
            // reachable even at the 560 px minimum window height.
            Flickable {
                id: sheetFlick
                anchors.top: sheetTitle.bottom
                anchors.topMargin: Theme.spacingM
                anchors.bottom: sheetFooter.top
                anchors.bottomMargin: Theme.spacingM
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingXL
                anchors.rightMargin: Theme.spacingXL
                contentHeight: sheetCol.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                MiddleScroll { flickable: sheetFlick }
                WheelBoost { flickable: sheetFlick }
                // Tab walks the form top to bottom; at the 560 px minimum
                // window height the last fields are past the sheet's fold.
                FocusScroll { flickable: sheetFlick }

                Column {
                    id: sheetCol
                    width: sheetFlick.width
                    spacing: Theme.spacingM

                    // Test result banner. Lives inside the scrolled body (an
                    // error can be several lines) and the handler scrolls back
                    // to the top so it is always what you see first.
                    Rectangle {
                        id: testBanner
                        width: parent.width
                        visible: editSheet.testing || editSheet.testDone
                        height: bannerCol.height + 2 * Theme.spacingM
                        radius: Theme.radiusM
                        color: Theme.surface
                        border.width: 1
                        border.color: editSheet.testing ? Theme.divider
                                                        : (editSheet.testOk ? Theme.success : Theme.danger)

                        Column {
                            id: bannerCol
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: Theme.spacingM
                            spacing: Theme.spacingXS

                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                font.pixelSize: Theme.fontS
                                font.weight: Font.DemiBold
                                color: editSheet.testing ? Theme.textSecondary
                                                         : (editSheet.testOk ? Theme.success : Theme.danger)
                                text: editSheet.testing
                                      ? qsTr("Testing the upload…")
                                      : (editSheet.testOk
                                         ? (editSheet.testUrl !== ""
                                            ? qsTr("Upload worked. The server answered with:")
                                            : qsTr("Upload worked, but no link came back. Set a public URL base so the copied link points at the uploaded file."))
                                         : qsTr("Upload failed"))
                            }
                            // A TextEdit only so the returned link can be
                            // selected and copied - it is a result, never an
                            // input. Left alone it announces as an unnamed
                            // EDITABLE field, which invites typing into it, so
                            // it says what it is and that it is read-only. The
                            // link itself stays the node's value, which is what
                            // assistive tech actually reads out.
                            TextEdit {
                                width: parent.width
                                visible: editSheet.testOk && editSheet.testUrl !== ""
                                text: editSheet.testUrl
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.WrapAnywhere
                                color: Theme.accent
                                font.pixelSize: Theme.fontM
                                selectionColor: Theme.tertiary
                                Accessible.role: Accessible.EditableText
                                Accessible.name: qsTr("Upload test result")
                                Accessible.editable: false
                                Accessible.readOnly: true
                            }
                            Text {
                                width: parent.width
                                visible: !editSheet.testing && !editSheet.testOk && editSheet.testError !== ""
                                text: editSheet.testError
                                wrapMode: Text.WordWrap
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontS
                            }
                        }
                    }

                    Labeled {
                        label: qsTr("Name")
                        // First tab stop of the form; the backward wrap that
                        // guards it is sheetTabStart, above.
                        UTextField {
                            id: fName; width: parent.width
                            placeholder: qsTr("e.g. my-server")
                        }
                    }
                    Labeled {
                        label: qsTr("Type")
                        hint: qsTr("http for web APIs and public hosts, curl for FTP/SFTP.")
                        UComboBox {
                            id: fType; width: parent.width
                            model: ["http", "curl"]
                            onActivated: (i) => fType.currentIndex = i
                        }
                    }
                    Labeled {
                        label: qsTr("Request URL")
                        hint: fType.currentIndex === 1
                              ? qsTr("%file% is replaced by the file name. Without it the name is added at the end, which is what FTP and SFTP folders want.")
                              : qsTr("%file% is replaced by the file name. Without it the address is sent exactly as typed.")
                        VarField {
                            id: fUrl
                            help: page.templateHelp("requestUrl", fType.currentIndex === 1)
                            placeholder: fType.currentIndex === 1
                                         ? qsTr("sftp://host/path/  or  https://host/upload/%file%")
                                         : qsTr("https://host/api/upload  or  https://host/put/%file%")
                        }
                    }
                    Labeled {
                        visible: fType.currentIndex === 0
                        label: qsTr("Request body")
                        UComboBox {
                            id: fBody; width: parent.width
                            model: [qsTr("Multipart form-data (upload the file)"), qsTr("Custom JSON body")]
                            onActivated: (i) => fBody.currentIndex = i
                        }
                    }
                    Labeled {
                        visible: fType.currentIndex === 0 && fBody.currentIndex === 0
                        label: qsTr("File form field name")
                        UTextField {
                            id: fFormName; width: parent.width
                            placeholder: qsTr("file")
                        }
                    }
                    Labeled {
                        visible: fType.currentIndex === 0 && fBody.currentIndex === 1
                        label: qsTr("JSON body")
                        hint: qsTr("$base64$ is the file itself, $filename$ its name, $mime$ its type.")
                        VarField {
                            id: fData
                            help: page.templateHelp("data", false)
                            placeholder: qsTr("e.g. {\"image\":\"$base64$\",\"name\":\"$filename$\"}")
                        }
                    }
                    Labeled {
                        visible: fType.currentIndex === 0 && fBody.currentIndex === 0
                        label: qsTr("Extra form fields (JSON)")
                        UTextField {
                            id: fArgs; width: parent.width
                            placeholder: qsTr("e.g. {\"reqtype\":\"fileupload\"}")
                        }
                    }
                    Labeled {
                        label: qsTr("URL extractor")
                        hint: fType.currentIndex === 1
                              ? qsTr("Reads the link out of what the server answers. Leave it empty for a plain file server that answers nothing, and fill in the public URL base below instead.")
                              : ""
                        VarField {
                            id: fUrlPath
                            help: page.templateHelp("urlPath", fType.currentIndex === 1)
                            placeholder: qsTr("$text$, $json:files[0].url$ or $regex:…$")
                        }
                    }
                    // Imgur's only credential is the Client-ID, so ask for it plainly
                    // instead of making the user hand-write an Authorization header.
                    // Keyed off the live URL field, so a hand-made Imgur destination
                    // gets the same field.
                    Labeled {
                        id: rClientId
                        visible: fType.currentIndex === 0 && editSheet.imgurMode
                        label: qsTr("Imgur Client-ID")
                        UTextField {
                            id: fClientId; width: parent.width
                            placeholder: qsTr("e.g. 1a2b3c4d5e6f7g8")
                        }
                    }
                    Text {
                        width: parent.width
                        visible: rClientId.visible
                        wrapMode: Text.WordWrap
                        text: qsTr("Unisic ships no Client-ID: it identifies the application, so a shared one would put every user on one daily cap. Register a free application at https://api.imgur.com/oauth2/addclient - pick “Anonymous usage without user authorisation” - and paste its Client-ID here. Uploads stay anonymous; they never appear in your Imgur gallery.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontS
                    }
                    Labeled {
                        // The Client-ID field replaces this one for Imgur, which
                        // is an http destination; a curl destination always gets
                        // the raw header field.
                        visible: fType.currentIndex === 1 || !editSheet.imgurMode
                        label: qsTr("Headers (JSON)")
                        UTextField {
                            id: fHeaders; width: parent.width
                            placeholder: qsTr("e.g. {\"Authorization\":\"Bearer x\"}")
                        }
                    }
                    Labeled {
                        visible: fType.currentIndex === 1
                        label: qsTr("Credentials")
                        UTextField {
                            id: fUser; width: parent.width
                            placeholder: qsTr("user:password (curl -u)")
                        }
                    }
                    Labeled {
                        visible: fType.currentIndex === 1
                        label: qsTr("Public URL base (optional)")
                        hint: qsTr("Where the uploaded file is reachable from. With neither this nor a URL extractor, a curl upload succeeds but no link can be copied.")
                        UTextField {
                            id: fPublicBase; width: parent.width
                            placeholder: qsTr("https://host/dir/")
                        }
                    }
                }
            }

            // The variables of whichever field is being edited, floating
            // over the form right under that field.
            //
            // ONE bar for the sheet, parented to the card rather than to the
            // fields, and that is the whole point: a chip row that appeared and
            // disappeared inside the scrolled Column would shove every field
            // below it down and back up each time focus moved, which is the
            // form rearranging itself under the pointer that is trying to use
            // it. Out here it costs the layout nothing and covers what is
            // behind it instead.
            UVarBar {
                // Not wired up at each field: which one is focused is asked of
                // the fields themselves.
                field: fUrl.focused ? fUrl
                     : (fData.focused ? fData
                     : (fUrlPath.focused ? fUrlPath : null))
                vars: field ? field.vars : []
                viewport: sheetFlick
            }

            Column {
                id: sheetFooter
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottomMargin: Theme.spacingXL
                anchors.leftMargin: Theme.spacingXL
                anchors.rightMargin: Theme.spacingXL
                spacing: Theme.spacingS

                // A curl test really writes the file to the user's own server,
                // and there is no delete path - say so before they press it.
                Text {
                    width: parent.width
                    visible: fType.currentIndex === 1
                    wrapMode: Text.WordWrap
                    text: qsTr("A test upload really writes unisic-test.png to this server. Unisic cannot remove it again.")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontS
                }
                Item {
                    width: parent.width
                    height: Math.max(testBtn.height, sheetActions.height)

                    UButton {
                        id: testBtn
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        variant: "tonal"
                        iconName: "upload-cloud"
                        text: editSheet.testing ? qsTr("Testing…") : qsTr("Test upload")
                        enabled: !editSheet.testing && fUrl.text.trim() !== ""
                        onClicked: {
                            var d = editSheet.buildDest()
                            if (!d)
                                return
                            // Pending flag BEFORE the call: the result can come
                            // back synchronously (missing URL, Imgur guard) and
                            // would otherwise be overwritten right after.
                            editSheet.testing = true
                            editSheet.testDone = false
                            editSheet.testUrl = ""
                            editSheet.testError = ""
                            App.uploads.testDestination(d)
                        }
                    }
                    Row {
                        id: sheetActions
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingM
                        UButton {
                            id: cancelBtn
                            text: qsTr("Cancel"); variant: "ghost"
                            onClicked: editSheet.close()
                            // Save is the last stop only while it is enabled;
                            // with an empty name or URL it is skipped by the
                            // tab chain and Cancel is the end of the sheet, so
                            // the wrap has to happen here too. A Keys handler
                            // consumes its key by default - the pass-through
                            // branch has to un-accept it explicitly.
                            Keys.onTabPressed: (e) => {
                                if (saveBtn.enabled) {
                                    e.accepted = false
                                    return
                                }
                                editSheet.focusFirst()
                                e.accepted = true
                            }
                        }
                        UButton {
                            id: saveBtn
                            text: qsTr("Save")
                            enabled: fName.text.trim() !== "" && fUrl.text.trim() !== ""
                            // Last control of a modal sheet: Tab wraps back to
                            // the top of the form. The page behind is disabled
                            // (see pageFlick), so this closes the loop.
                            Keys.onTabPressed: (e) => {
                                editSheet.focusFirst()
                                e.accepted = true
                            }
                            onClicked: {
                                var d = editSheet.buildDest()
                                if (!d)
                                    return
                                var orig = (page.editing && page.editing.name) || ""
                                // Renaming onto an existing server would make
                                // saveDestination() silently overwrite its config.
                                if (orig !== d.name
                                        && Object.keys(App.uploads.destination(d.name)).length > 0) {
                                    App.showToast(qsTr("A server with this name already exists"))
                                    return
                                }
                                // A renamed copy of a builtin becomes a fresh custom
                                // server - drop the builtin flag, or the clone can never
                                // be deleted (delete button is hidden for builtins) and
                                // the original resurrects as a duplicate next launch.
                                if (orig !== "" && orig !== d.name)
                                    delete d.builtin
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
}
