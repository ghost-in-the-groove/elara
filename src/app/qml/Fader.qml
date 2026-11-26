// SPDX-License-Identifier: AGPL-3.0-or-later
// A slider with a thin groove and a small round handle: the groove is filled
// from its start, or from its middle for a value that goes both ways (a pan).
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Elara.App

T.Slider {
    id: root

    /// What the fill and the handle are drawn in.
    property color tint: Theme.accent
    /// Filled from the middle, with a tick there.
    property bool centred: false
    property string tip

    /// The handle was let go, or the groove clicked.
    signal released

    implicitWidth: 120
    implicitHeight: 24
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1.0 : 0.38
    onPressedChanged: {
        if (!pressed)
            released();
    }
    Accessible.name: tip

    background: Item {
        x: root.leftPadding
        y: root.topPadding
        width: root.availableWidth
        height: root.availableHeight

        Rectangle {
            id: groove

            x: handle.width / 2
            width: parent.width - handle.width
            y: (parent.height - height) / 2
            height: 3
            radius: 1.5
            color: Theme.faint
        }
        // The middle of a pan.
        Rectangle {
            visible: root.centred
            x: groove.x + groove.width / 2 - 1
            y: (parent.height - height) / 2
            width: 2
            height: 9
            radius: 1
            color: Theme.faint
        }
        Rectangle {
            readonly property real at: groove.x + root.visualPosition * groove.width
            readonly property real from: root.centred ? groove.x + groove.width / 2 : groove.x

            x: Math.min(from, at)
            width: Math.abs(at - from)
            y: groove.y
            height: groove.height
            radius: groove.radius
            color: root.tint
        }
    }

    handle: Rectangle {
        id: handle

        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + (root.availableHeight - height) / 2
        width: 12
        height: 12
        radius: 6
        color: root.tint
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.ink
        scale: root.pressed ? 1.2 : root.hovered ? 1.1 : 1.0
    }

    ToolTip.visible: tip.length > 0 && (pressed || hovered)
    ToolTip.delay: pressed ? 0 : 500
    ToolTip.text: tip
}
