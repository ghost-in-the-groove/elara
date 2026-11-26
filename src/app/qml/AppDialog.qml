// SPDX-License-Identifier: AGPL-3.0-or-later
// A dialog on the interface's own surface: its title, what it says, and its
// buttons at the right, the one that goes on filled with the accent and the
// one that discards in the danger colour.
import QtQuick
import QtQuick.Controls
import Elara.App

Dialog {
    id: root

    padding: 20
    topPadding: 4
    bottomPadding: 12
    modal: true

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

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, Theme.dark ? 0.5 : 0.3)
    }

    background: Rectangle {
        radius: Theme.radiusSheet
        color: Theme.surface
        border.color: Theme.line

        Elevation {
            radius: parent.radius
        }
    }

    header: Label {
        visible: root.title.length > 0
        text: root.title
        font.pixelSize: Theme.fontTitle
        font.weight: Font.DemiBold
        color: Theme.ink
        wrapMode: Text.Wrap
        padding: 20
        topPadding: 18
        bottomPadding: 8
    }

    footer: DialogButtonBox {
        visible: count > 0
        alignment: Qt.AlignRight
        spacing: 8
        padding: 14
        topPadding: 4

        background: Item {}

        delegate: TextButton {
            kind: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
                  || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
                  ? "main" : DialogButtonBox.buttonRole === DialogButtonBox.DestructiveRole
                    ? "danger" : "plain"
        }
    }
}
