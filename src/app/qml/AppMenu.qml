// SPDX-License-Identifier: AGPL-3.0-or-later
// A menu on the interface's own surface, as wide as its widest item.
import QtQuick
import QtQuick.Controls
import Elara.App

Menu {
    id: root

    /// As wide as the widest item needs, measured when the menu opens: items
    /// arrive and change their text while it is closed.
    function fitWidth() {
        let widest = 180;
        for (let at = 0; at < count; ++at) {
            const item = itemAt(at);
            if (item && item.visible)
                widest = Math.max(widest, item.implicitWidth);
        }
        width = widest + leftPadding + rightPadding;
    }

    topPadding: 5
    bottomPadding: 5
    leftPadding: 5
    rightPadding: 5
    overlap: 4
    onAboutToShow: fitWidth()

    delegate: AppMenuItem {}

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.quick
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: 80
        }
    }

    background: Rectangle {
        implicitWidth: 180
        radius: Theme.radiusCard
        color: Theme.surface
        border.color: Theme.line

        Elevation {
            radius: parent.radius
        }
    }
}
