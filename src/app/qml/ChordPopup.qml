// SPDX-License-Identifier: AGPL-3.0-or-later
// The chord symbol of the caret's beat (ADR-0080): typed as a name, or picked
// from the chords the song already names; on a fretted staff the beat's frets
// give it its diagram.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Elara.App

Popup {
    id: root

    required property ScoreDocument document

    readonly property color ink: Theme.ink
    /// The song's chord names, read as the popup opens.
    property var offered: []

    function apply(name: string) {
        root.document.setChordSymbol(name);
        root.close();
    }

    width: 320
    padding: 16
    topPadding: 10
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    onAboutToShow: {
        offered = root.document.songChords();
        nameField.text = root.document.caretChord;
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
                text: qsTr("Chord symbol")
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                color: root.ink
            }
            IconButton {
                visible: root.document.caretChord.length > 0
                iconPath: "M6 7 L18 7 M9 7 L9 19 L15 19 L15 7 M10 4 L14 4"
                tip: qsTr("Remove the chord symbol")
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
            placeholderText: qsTr("Am7, C/G, N.C.…")
            selectByMouse: true
            font.pixelSize: Theme.fontTitle
            onAccepted: root.apply(text)
        }

        // The chords the song names already, one click away.
        Flow {
            Layout.fillWidth: true
            visible: root.offered.length > 0
            spacing: 6

            Repeater {
                model: root.offered

                delegate: AbstractButton {
                    id: chip

                    required property string modelData
                    readonly property bool current: modelData === root.document.caretChord

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
            text: root.document.caretFretted ? qsTr("Enter names it; the beat's frets draw its diagram.")
                                             : qsTr("Enter names it; an empty name takes it away.")
            font.pixelSize: Theme.fontSmall
            opacity: 0.5
            wrapMode: Text.Wrap
            color: root.ink
        }
    }
}
