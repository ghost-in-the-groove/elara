// SPDX-License-Identifier: AGPL-3.0-or-later
// Words for the caret's beat, its chord symbol (ADR-0080) or its text
// (ADR-0081): typed, or picked from the ones the song already has. The owner
// says what they are and applies them.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Elara.App

Popup {
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

    readonly property color ink: Theme.ink

    /// The popup is about to show: time to set `offered`.
    signal opening
    /// Words to apply to the caret's beat; empty takes them away.
    signal applied(string words)

    function apply(words: string) {
        root.applied(words);
        root.close();
    }

    width: 320
    padding: 16
    topPadding: 10
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

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 140
        }
        NumberAnimation {
            property: "scale"
            from: 0.96
            to: 1
            duration: 140
            easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: 100
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

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: root.title
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                color: root.ink
            }
            IconButton {
                visible: root.current.length > 0
                iconPath: "M6 7 L18 7 M9 7 L9 19 L15 19 L15 7 M10 4 L14 4"
                tip: root.removeTip
                onClicked: root.apply("")
            }
            IconButton {
                iconPath: "M7 7 L17 17 M17 7 L7 17"
                tip: qsTr("Close")
                onClicked: root.close()
            }
        }

        TextField {
            id: nameField

            Layout.fillWidth: true
            placeholderText: root.placeholder
            selectByMouse: true
            font.pixelSize: Theme.fontTitle
            onAccepted: root.apply(text)
        }

        // The words the song has already, one click away.
        Flow {
            Layout.fillWidth: true
            visible: root.offered.length > 0
            spacing: 6

            Repeater {
                model: root.offered

                delegate: AbstractButton {
                    id: chip

                    required property string modelData
                    readonly property bool current: modelData === root.current

                    text: modelData
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: true
                    implicitHeight: 30
                    implicitWidth: chipLabel.implicitWidth + 22
                    onClicked: root.apply(modelData)
                    Accessible.name: modelData

                    contentItem: Label {
                        id: chipLabel

                        text: chip.text
                        font.pixelSize: Theme.fontBody
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: chip.current ? "white" : root.ink
                    }
                    background: Rectangle {
                        radius: height / 2
                        color: chip.current ? chip.Material.accent
                                            : chip.pressed ? Theme.pressed
                                                           : chip.hovered ? Theme.hover : "transparent"
                        border.color: chip.current ? chip.Material.accent : Theme.line

                        Behavior on color {
                            ColorAnimation {
                                duration: 120
                            }
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.hint
            font.pixelSize: Theme.fontSmall
            opacity: 0.5
            wrapMode: Text.Wrap
            color: root.ink
        }
    }
}
