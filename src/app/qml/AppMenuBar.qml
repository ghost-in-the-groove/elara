// SPDX-License-Identifier: AGPL-3.0-or-later
// The menus, on the command bar: every command by name, with its keys.
import QtQuick
import QtQuick.Controls
import Elara.App

MenuBar {
    id: root

    spacing: 0
    padding: 0

    background: Item {}

    delegate: MenuBarItem {
        id: barItem

        implicitHeight: 28
        leftPadding: 9
        rightPadding: 9
        topPadding: 0
        bottomPadding: 0

        // The name without the mark of its Alt key.
        contentItem: Label {
            text: barItem.text.replace("&", "")
            font.pixelSize: Theme.fontBody
            color: Theme.pearl
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }

        background: Rectangle {
            radius: Theme.radiusSmall
            color: barItem.down || barItem.highlighted ? Theme.nightPressed
                                                       : barItem.hovered ? Theme.nightHover
                                                                         : "transparent"
            border.width: barItem.visualFocus ? 2 : 0
            border.color: Theme.lilac
        }
    }
}
