// SPDX-License-Identifier: AGPL-3.0-or-later
// A choice from a list, as high as the other controls: the chosen one, and a
// list that opens under it with a check mark on it.
import QtQuick
import QtQuick.Controls
import Elara.App

ComboBox {
    id: root

    /// A click never takes the keyboard from the score when off.
    property bool takesFocus: true

    implicitWidth: 160
    implicitHeight: Theme.control
    leftPadding: 10
    rightPadding: 28
    topPadding: 0
    bottomPadding: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    hoverEnabled: true
    focusPolicy: takesFocus ? Qt.StrongFocus : Qt.NoFocus
    font.pixelSize: Theme.fontBody

    background: Rectangle {
        radius: Theme.radiusSmall
        color: root.down || root.popup.visible ? Theme.pressed : root.hovered ? Theme.hover : "transparent"
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus ? Theme.accentInk : Theme.line
    }

    contentItem: Label {
        text: root.displayText
        font: root.font
        color: Theme.ink
        opacity: root.enabled ? 1.0 : 0.38
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    indicator: Icon {
        x: root.width - width - 8
        y: (root.height - height) / 2
        width: 14
        height: 14
        path: Icons.chevronDown
        color: Theme.inkSoft
        opacity: root.enabled ? 1.0 : 0.38
    }

    delegate: ItemDelegate {
        id: option

        required property int index

        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: 30
        leftPadding: 30
        rightPadding: 12
        topPadding: 0
        bottomPadding: 0
        highlighted: root.highlightedIndex === index

        contentItem: Label {
            text: root.textAt(option.index)
            font.pixelSize: Theme.fontBody
            color: Theme.ink
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: Theme.radiusSmall - 1
            color: option.highlighted ? Theme.hover : "transparent"

            Icon {
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                visible: option.index === root.currentIndex
                path: Icons.check
                lineWidth: 2.2
                color: Theme.accentInk
            }
        }
    }

    popup: Popup {
        y: root.height + 4
        width: Math.max(root.width, 200)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         root.Window.height - topMargin - bottomMargin)
        topMargin: 8
        bottomMargin: 8
        padding: 5

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

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds

            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: Theme.radiusCard
            color: Theme.surface
            border.color: Theme.line

            Elevation {
                radius: parent.radius
            }
        }
    }
}
