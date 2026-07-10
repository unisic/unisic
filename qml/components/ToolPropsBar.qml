import QtQuick
import Unisic

// Contextual tool-properties strip: renders only the controls relevant to the
// active tool (ToolCatalog `props` — "stroke", "width", "fill", "font").
// Shared by the editor top bar and the overlay pill. The hosts own the color
// pickers (editor: ColorDialog window, overlay: in-scene UColorPopup — a
// dialog window would never get input under the layer-shell keyboard grab),
// so picker requests are surfaced as signals instead of handled here.
Row {
    id: root

    required property var canvas
    property var props: []
    // Editor-only recent stroke colors (the overlay bar stays compact).
    property var recentColors: []
    signal strokePickerRequested()
    signal fillPickerRequested()
    signal textOutlinePickerRequested()
    signal textBackgroundPickerRequested()

    spacing: 6

    function has(p) { return props.indexOf(p) >= 0 }

    // ---- stroke color ----
    Repeater {
        model: root.has("stroke") ? Theme.swatches : []
        delegate: ColorDot {
            dotColor: modelData
            active: Qt.colorEqual(root.canvas.strokeColor, modelData)
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.strokeColor = modelData
        }
    }
    Repeater {
        model: root.has("stroke") ? root.recentColors : []
        delegate: ColorDot {
            dotColor: modelData
            active: Qt.colorEqual(root.canvas.strokeColor, modelData)
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.canvas.strokeColor = modelData
        }
    }
    UIconButton {
        visible: root.has("stroke")
        iconName: "color-picker"
        iconSize: 16
        width: 30; height: 30
        tooltip: qsTr("More colors")
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.strokePickerRequested()
    }

    Rectangle {
        visible: root.has("stroke") && (root.has("width") || root.has("fill"))
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }

    // ---- stroke width ----
    Column {
        visible: root.has("width")
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            text: qsTr("Stroke %1").arg(root.canvas.strokeWidth)
            color: Theme.textTertiary
            font.pixelSize: 10
            anchors.horizontalCenter: parent.horizontalCenter
        }
        USlider {
            width: 100
            from: 1; to: 16
            value: root.canvas.strokeWidth
            onMoved: (v) => root.canvas.strokeWidth = Math.round(v)
        }
    }

    Rectangle {
        visible: root.has("width") && root.has("fill")
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }

    // ---- shape fill ----
    ToolChip {
        visible: root.has("fill")
        iconName: "fill-color"
        label: qsTr("Fill shapes")
        active: root.canvas.shapeFillEnabled
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.shapeFillEnabled = !root.canvas.shapeFillEnabled
    }
    ColorDot {
        visible: root.has("fill")
        dotColor: root.canvas.shapeFillColor
        active: root.canvas.shapeFillEnabled
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.fillPickerRequested()
    }

    // ---- text styling ----
    Rectangle {
        visible: root.has("font") && root.has("stroke")
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }
    UComboBox {
        visible: root.has("font")
        width: 150
        anchors.verticalCenter: parent.verticalCenter
        property var fams: [qsTr("Default font")].concat(Qt.fontFamilies())
        model: fams
        currentIndex: {
            if (root.canvas.fontFamily === "") return 0
            var i = fams.indexOf(root.canvas.fontFamily)
            return i < 0 ? 0 : i
        }
        onActivated: (i) => root.canvas.fontFamily = (i === 0 ? "" : fams[i])
    }
    USpinBox {
        visible: root.has("font")
        anchors.verticalCenter: parent.verticalCenter
        from: 8; to: 144; step: 2
        value: root.canvas.fontSize
        onChanged: (v) => root.canvas.fontSize = v
    }
    ToolChip {
        visible: root.has("font")
        icon: "B"; label: qsTr("Bold")
        active: root.canvas.fontBold
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.fontBold = !root.canvas.fontBold
    }
    ToolChip {
        visible: root.has("font")
        icon: "I"; label: qsTr("Italic")
        active: root.canvas.fontItalic
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.fontItalic = !root.canvas.fontItalic
    }
    ToolChip {
        visible: root.has("font")
        icon: "U"; label: qsTr("Underline")
        active: root.canvas.fontUnderline
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.fontUnderline = !root.canvas.fontUnderline
    }
    Rectangle {
        visible: root.has("font")
        width: 1; height: 28; color: Theme.divider
        anchors.verticalCenter: parent.verticalCenter
    }
    ToolChip {
        visible: root.has("font")
        iconName: "text-outline"
        label: qsTr("Text outline")
        active: root.canvas.textOutline
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.textOutline = !root.canvas.textOutline
    }
    ColorDot {
        visible: root.has("font")
        dotColor: root.canvas.textOutlineColor
        active: root.canvas.textOutline
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.textOutlinePickerRequested()
    }
    ToolChip {
        visible: root.has("font")
        iconName: "text-background"
        label: qsTr("Text background")
        active: root.canvas.textBackground
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.canvas.textBackground = !root.canvas.textBackground
    }
    ColorDot {
        visible: root.has("font")
        dotColor: root.canvas.textBackgroundColor
        active: root.canvas.textBackground
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.textBackgroundPickerRequested()
    }
}
