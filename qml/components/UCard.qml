import QtQuick
import QtQuick.Effects
import Unisic

// Soft rounded surface card with depth: vertical gradient, top hairline
// highlight and a drop shadow — the basic SwiftUI-like grouping element.
Rectangle {
    id: card
    default property alias content: inner.data
    property alias padding: inner.anchors.margins

    radius: Theme.radiusL
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.surfaceTop }
        GradientStop { position: 1.0; color: Theme.surfaceBottom }
    }
    border.width: 1
    border.color: Theme.divider

    implicitHeight: inner.childrenRect.height + inner.anchors.margins * 2
    implicitWidth: inner.childrenRect.width + inner.anchors.margins * 2

    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Theme.shadow
        shadowBlur: 0.9
        shadowVerticalOffset: 5
        shadowOpacity: 0.55
    }

    // lit top edge
    Rectangle {
        x: card.radius / 2
        width: parent.width - card.radius
        height: 1
        y: 1
        color: Theme.edgeLight
    }

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: Theme.spacingL
    }
}
