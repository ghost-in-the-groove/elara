// SPDX-License-Identifier: AGPL-3.0-or-later
// A row of an AppMenu: a check mark when it is chosen, its name, and the keys
// that do the same at the right.
import QtQuick
import QtQuick.Controls
import Elara.App

MenuItem {
    id: root

    /// The keys that do the same, as the platform writes them.
    property string keys
    /// Marked as the one chosen, or as on; a click does not change it by
    /// itself, as `checkable` would.
    property bool chosen: false

    readonly property color ink: Theme.ink

    implicitWidth: leftPadding + name.implicitWidth + rightPadding
                   + (keys.length > 0 ? keysLabel.implicitWidth + 28 : 0)
                   + (subMenu ? 16 : 0)
    implicitHeight: visible ? 30 : 0
    Accessible.checked: chosen || checked
    leftPadding: 30
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0
    spacing: 0

    // The check mark and the arrow are made only for the items that show
    // one: a window has some sixty items, and a shape for each adds up.
    indicator: Loader {
        x: 8
        y: (root.height - height) / 2
        width: 16
        height: 16
        active: root.chosen || (root.checkable && root.checked)

        sourceComponent: Icon {
            path: Icons.check
            lineWidth: 2.2
            color: Theme.accentInk
        }
    }

    arrow: Loader {
        x: root.width - width - 8
        y: (root.height - height) / 2
        width: 14
        height: 14
        active: root.subMenu !== null

        sourceComponent: Icon {
            path: Icons.chevronRight
            color: Theme.inkSoft
            opacity: root.enabled ? 1.0 : 0.38
        }
    }

    contentItem: Item {
        Label {
            id: name

            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width)
            text: root.text
            elide: Text.ElideRight
            font.pixelSize: Theme.fontBody
            color: root.ink
            opacity: root.enabled ? 1.0 : 0.38
        }
        Label {
            id: keysLabel

            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            visible: root.keys.length > 0
            text: root.keys
            font.pixelSize: Theme.fontSmall
            color: Theme.inkSoft
            opacity: root.enabled ? 1.0 : 0.38
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall - 1
        color: root.highlighted && root.enabled ? Theme.hover : "transparent"
    }
}
