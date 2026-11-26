// SPDX-License-Identifier: AGPL-3.0-or-later
// A button with words on it. `kind` says what it is among its neighbours:
// "main" is the one a dialog or a pane is there for (filled with the accent),
// "danger" takes something away, and a plain one does anything else.
import QtQuick
import QtQuick.Controls
import Elara.App

AbstractButton {
    id: root

    property string kind: "plain"
    /// A line icon before the words.
    property string iconPath

    readonly property bool main: kind === "main"
    readonly property color ink: main ? Theme.accentText : kind === "danger" ? Theme.dangerInk : Theme.ink

    implicitHeight: Theme.control
    implicitWidth: Math.ceil(row.implicitWidth) + 28
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? 1.0 : 0.38
    Accessible.role: Accessible.Button
    Accessible.name: text

    contentItem: Item {
        Row {
            id: row

            anchors.centerIn: parent
            spacing: 7

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.iconPath.length > 0
                width: 16
                height: 16
                path: root.iconPath
                color: root.ink
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                font.pixelSize: Theme.fontBody
                font.weight: root.main ? Font.DemiBold : Font.Normal
                color: root.ink
            }
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: root.main ? (root.down ? Qt.darker(Theme.accent, 1.18)
                                      : root.hovered ? Qt.lighter(Theme.accent, 1.12) : Theme.accent)
                         : root.down ? Theme.pressed : root.hovered ? Theme.hover : "transparent"
        border.width: root.visualFocus ? 2 : root.main ? 0 : 1
        border.color: root.visualFocus ? (root.main ? Theme.ink : Theme.accentInk) : Theme.line
    }
}
