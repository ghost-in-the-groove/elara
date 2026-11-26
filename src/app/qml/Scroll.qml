// SPDX-License-Identifier: AGPL-3.0-or-later
// A slim scroll bar that shows whenever there is more than fits, so that a
// list cut by the edge of its panel says so.
import QtQuick
import QtQuick.Controls
import Elara.App

ScrollBar {
    id: root

    implicitWidth: 8
    implicitHeight: 8
    padding: 2
    minimumSize: 0.1
    policy: ScrollBar.AsNeeded

    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: 2
        visible: root.size < 1.0
        color: root.pressed ? Theme.inkSoft : root.hovered ? Qt.alpha(Theme.inkSoft, 0.7) : Theme.faint
    }

    background: Item {}
}
