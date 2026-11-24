// SPDX-License-Identifier: AGPL-3.0-or-later
// A flat round tool button showing an Icon, with a tooltip. Checked, it shows
// in the accent.
import QtQuick
import QtQuick.Controls
import Elara.App

ToolButton {
    id: root

    property string iconPath
    property string tip

    display: AbstractButton.IconOnly
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    implicitWidth: 36
    implicitHeight: 36
    padding: 7
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0

    background: Rectangle {
        radius: height / 2
        color: root.checked ? Theme.accentWash
                            : !root.enabled ? "transparent"
                                            : root.down ? Theme.pressed
                                                        : root.hovered ? Theme.hover : "transparent"
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.accentInk

        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }
    }

    contentItem: Icon {
        path: root.iconPath
        color: root.checked ? Theme.accentInk : Theme.ink
        opacity: root.enabled ? 1.0 : 0.35
    }

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip
    Accessible.name: tip
}
