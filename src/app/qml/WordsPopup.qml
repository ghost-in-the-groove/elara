// SPDX-License-Identifier: AGPL-3.0-or-later
// Words for the caret's beat, its chord symbol (ADR-0080) or its text
// (ADR-0081): typed, or picked from the ones the song already has. The owner
// says what they are and applies them.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

Sheet {
    id: root

    /// What the popup is for: "Chord symbol", "Text".
    property string title
    property string placeholder
    property string hint
    property string removeTip
    /// The caret's beat's words now.
    property string current
    /// The song's words to pick from, set as the popup opens (`opening`).
    property var offered: []

    /// The popup is about to show: time to set `offered`.
    signal opening
    /// Words to apply to the caret's beat; empty takes them away.
    signal applied(string words)

    function apply(words: string) {
        root.applied(words);
        root.close();
    }

    width: 320
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    onAboutToShow: {
        root.opening();
        nameField.text = root.current;
    }
    onOpened: {
        nameField.selectAll();
        nameField.forceActiveFocus();
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: root.title
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                color: Theme.ink
            }
            IconButton {
                implicitWidth: 28
                implicitHeight: 28
                padding: 6
                visible: root.current.length > 0
                iconPath: Icons.trash
                tip: root.removeTip
                onClicked: root.apply("")
            }
            IconButton {
                implicitWidth: 28
                implicitHeight: 28
                padding: 6
                iconPath: Icons.close
                tip: qsTr("Close")
                onClicked: root.close()
            }
        }

        Field {
            id: nameField

            Layout.fillWidth: true
            implicitHeight: 34
            placeholderText: root.placeholder
            font.pixelSize: Theme.fontTitle
            onAccepted: root.apply(text)
        }

        // The words the song has already, one click away.
        Flow {
            Layout.fillWidth: true
            visible: root.offered.length > 0
            spacing: 5

            Repeater {
                model: root.offered

                delegate: Chip {
                    required property string modelData

                    text: modelData
                    on: modelData === root.current
                    onClicked: root.apply(modelData)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.hint
            font.pixelSize: Theme.fontCaption
            color: Theme.inkSoft
            wrapMode: Text.Wrap
        }
    }
}
