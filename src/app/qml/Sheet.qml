// SPDX-License-Identifier: AGPL-3.0-or-later
// What floats over the score for the caret: a card with the interface's
// surface, hairline and shadow, that fades in and out.
import QtQuick
import QtQuick.Controls
import Elara.App

Popup {
    id: root

    padding: 14
    topPadding: 10

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
        radius: Theme.radiusSheet
        color: Theme.surface
        border.color: Theme.line

        Elevation {
            radius: parent.radius
        }
    }
}
