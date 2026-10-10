import QtQuick
import Unisic
import Unisic.Kit

// "Application audio only": one chip per application playing audio, any
// number ticked at once. Shared by Settings > Recording and the Record page.
//
// Ticks are stored by application (App.settings.recordAppAudioApps), not by
// stream, and the recorder resolves them to live streams when it starts - so
// a choice survives the app restarting, and a browser with three tabs playing
// is one chip that records all three.
//
// The list refreshes itself every 3 s while visible (pw-dump, off the GUI
// thread, ~10 ms). Chips never reorder or vanish under the pointer: an app is
// appended the first time it is seen and stays, dimmed, after it goes quiet,
// until the picker is hidden.
Flow {
    id: root
    width: parent ? parent.width : 0
    spacing: Theme.spacingS

    // {id, label, icon, streams} in first-seen order; streams 0 = gone quiet.
    property var seen: []
    readonly property var selected: App.settings.recordAppAudioApps
    readonly property var entries: {
        const out = root.seen.slice()
        const ids = out.map(e => e.id)
        // A ticked app that has not played since the picker opened still
        // needs a chip, or there would be no way to untick it.
        for (const id of root.selected)
            if (ids.indexOf(id) < 0)
                out.push({ id: id, label: id, icon: "", streams: 0 })
        return out
    }

    function toggle(id) {
        const s = root.selected.slice()
        const i = s.indexOf(id)
        if (i >= 0)
            s.splice(i, 1)
        else
            s.push(id)
        App.settings.recordAppAudioApps = s
    }

    function merge(nodes) {
        const live = {}
        for (const n of nodes)
            live[n.id] = n
        const out = root.seen.map(e => live[e.id] !== undefined ? live[e.id]
                                       : Object.assign({}, e, { streams: 0 }))
        const ids = out.map(e => e.id)
        for (const n of nodes)
            if (ids.indexOf(n.id) < 0)
                out.push(n)
        root.seen = out
    }

    onVisibleChanged: if (!visible) root.seen = []

    Connections {
        target: App
        function onAudioApplicationNodesReady(nodes) { if (root.visible) root.merge(nodes) }
    }
    Timer {
        interval: 3000
        repeat: true
        triggeredOnStart: true
        running: root.visible && App.perAppAudioAvailable
        onTriggered: App.requestAudioApplicationNodes()
    }

    Repeater {
        model: root.entries
        delegate: UFilterChip {
            required property var modelData
            enabled: App.perAppAudioAvailable
            iconName: modelData.icon
            text: modelData.streams > 1 ? qsTr("%1 (%2 streams)").arg(modelData.label).arg(modelData.streams)
                                        : modelData.label
            checked: root.selected.indexOf(modelData.id) >= 0
            opacity: modelData.streams > 0 ? 1 : 0.6
            accessibleDescription: modelData.streams > 0 ? "" : qsTr("Not playing audio right now")
            onClicked: root.toggle(modelData.id)
        }
    }

    Text {
        visible: root.entries.length === 0
        width: root.width
        text: App.perAppAudioAvailable
              ? qsTr("No application is playing audio. Start playback and it appears here.")
              : qsTr("Requires the pw-dump and pw-record helpers.")
        color: Theme.textTertiary
        font.pixelSize: Theme.fontS
        wrapMode: Text.WordWrap
    }
}
