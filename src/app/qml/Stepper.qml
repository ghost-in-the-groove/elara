// SPDX-License-Identifier: AGPL-3.0-or-later
// A number to step or type, as high as the other controls: less at the left,
// more at the right, the value between them.
import QtQuick
import QtQuick.Templates as T
import Elara.App

T.SpinBox {
    id: root

    implicitWidth: 140
    implicitHeight: Theme.control
    leftPadding: Theme.control
    rightPadding: Theme.control
    topPadding: 0
    bottomPadding: 0
    editable: true
    hoverEnabled: true
    font.pixelSize: Theme.fontBody
    opacity: enabled ? 1.0 : 0.38

    validator: IntValidator {
        locale: root.locale.name
        bottom: Math.min(root.from, root.to)
        top: Math.max(root.from, root.to)
    }

    // One of the two ends: a sign that answers the pointer.
    component End: Item {
        id: end

        required property string path
        required property bool held
        required property bool hot
        required property bool usable

        implicitWidth: Theme.control
        implicitHeight: Theme.control

        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            radius: Theme.radiusSmall - 2
            color: end.held ? Theme.pressed : end.hot ? Theme.hover : "transparent"
        }
        Icon {
            anchors.centerIn: parent
            width: 14
            height: 14
            path: end.path
            lineWidth: 2.2
            color: Theme.ink
            opacity: end.usable ? 1.0 : 0.38
        }
    }

    contentItem: TextInput {
        text: root.displayText
        font: root.font
        color: Theme.ink
        selectionColor: Qt.alpha(Theme.accent, 0.4)
        selectedTextColor: Theme.ink
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !root.editable
        validator: root.validator
        inputMethodHints: root.inputMethodHints
        selectByMouse: true
        clip: width < implicitWidth
    }

    down.indicator: End {
        x: root.mirrored ? root.width - width : 0
        height: root.height
        path: Icons.minus
        held: root.down.pressed
        hot: root.down.hovered
        usable: root.value > Math.min(root.from, root.to)
    }

    up.indicator: End {
        x: root.mirrored ? 0 : root.width - width
        height: root.height
        path: Icons.plus
        held: root.up.pressed
        hot: root.up.hovered
        usable: root.value < Math.max(root.from, root.to)
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surface
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.accentInk : root.hovered ? Theme.faint : Theme.line
    }
}
