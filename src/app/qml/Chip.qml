// SPDX-License-Identifier: AGPL-3.0-or-later
// A mark that is on or off, or one of a few words to pick: washed with the
// accent while it is on.
import QtQuick
import QtQuick.Controls
import Elara.App

AbstractButton {
    id: root

    property bool on: false
    property string tip

    implicitHeight: 26
    implicitWidth: Math.ceil(label.implicitWidth) + 20
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.38
    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checked: on

    contentItem: Label {
        id: label

        text: root.text
        font.pixelSize: Theme.fontBody
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: root.on ? Theme.accentInk : Theme.ink
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: root.pressed ? Theme.pressed : root.on ? Theme.accentWash
                                                      : root.hovered ? Theme.hover : "transparent"
        border.color: root.on ? Qt.alpha(Theme.accentInk, 0.5) : Theme.line
    }

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip
}
