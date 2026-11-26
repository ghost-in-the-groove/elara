// SPDX-License-Identifier: AGPL-3.0-or-later
// A line of text to type, as high as the other controls: its outline takes the
// accent while it has the keyboard, and its placeholder shows while it is empty.
import QtQuick
import QtQuick.Templates as T
import Elara.App

T.TextField {
    id: root

    implicitWidth: 160
    implicitHeight: Theme.control
    leftPadding: 9
    rightPadding: 9
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: Theme.fontBody
    color: Theme.ink
    selectionColor: Qt.alpha(Theme.accent, 0.4)
    selectedTextColor: Theme.ink
    placeholderTextColor: Theme.inkSoft
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.38

    cursorDelegate: Rectangle {
        id: cursor

        width: 1.5
        color: Theme.accentInk
        visible: root.activeFocus && !root.readOnly && root.selectionStart === root.selectionEnd

        // Blinks at the pace the platform asks for, only while it shows.
        Timer {
            running: cursor.visible && Application.styleHints.cursorFlashTime > 0
            interval: Application.styleHints.cursorFlashTime / 2
            repeat: true
            onTriggered: cursor.opacity = cursor.opacity > 0 ? 0 : 1
            onRunningChanged: cursor.opacity = 1
        }
        Connections {
            target: root

            function onCursorPositionChanged() {
                cursor.opacity = 1;
            }
        }
    }

    Text {
        x: root.leftPadding
        width: root.width - root.leftPadding - root.rightPadding
        height: root.height
        visible: root.length === 0 && root.preeditText.length === 0
        text: root.placeholderText
        font: root.font
        color: root.placeholderTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surface
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.accentInk : root.hovered ? Theme.faint : Theme.line
    }
}
