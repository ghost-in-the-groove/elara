// SPDX-License-Identifier: AGPL-3.0-or-later
// The keyboard entry bar floating over the score: the note value typing uses,
// the dot, the tuplet, a rest, delete, and where the caret is.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Elara.App

Rectangle {
    id: root

    required property ScoreDocument document
    property bool dark: false

    /// The measure button: the caret's measure, its signatures and length.
    signal measureRequested

    // Note values from the music font's metronome marks, longest first.
    readonly property var values: [
        { value: 1, symbol: "\uECA2", tip: qsTr("Whole note") },
        { value: 2, symbol: "\uECA3", tip: qsTr("Half note") },
        { value: 4, symbol: "\uECA5", tip: qsTr("Quarter note") },
        { value: 8, symbol: "\uECA7", tip: qsTr("Eighth note") },
        { value: 16, symbol: "\uECA9", tip: qsTr("16th note") },
        { value: 32, symbol: "\uECAB", tip: qsTr("32nd note") },
        { value: 64, symbol: "\uECAD", tip: qsTr("64th note") }
    ]
    readonly property int currentIndex: values.findIndex(entry => entry.value === document.inputValue)
    readonly property color ink: dark ? "#e8e8ee" : "#23232b"

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 44
    radius: height / 2
    color: dark ? "#2a2a32" : "white"
    border.color: Qt.alpha(ink, 0.12)

    // A tool of the bar: never takes the keyboard from the score.
    component Tool: AbstractButton {
        id: tool

        property string tip

        implicitWidth: 34
        implicitHeight: 34
        focusPolicy: Qt.NoFocus
        hoverEnabled: true

        background: Rectangle {
            radius: height / 2
            color: tool.pressed ? Qt.alpha(root.ink, 0.16)
                                : tool.hovered ? Qt.alpha(root.ink, 0.08) : "transparent"

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }
        }

        ToolTip.visible: hovered && tip.length > 0
        ToolTip.delay: 500
        ToolTip.text: tip
        Accessible.name: tip
    }

    // A tuplet in the tuplet menu, checked when the input has it.
    component TupletItem: MenuItem {
        required property int actual

        checkable: true
        checked: root.document.inputTuplet === actual
        onTriggered: root.document.setInputTuplet(actual)
    }

    component Separator: Rectangle {
        width: 1
        height: 22
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        color: Qt.alpha(root.ink, 0.14)
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: 2

        Item {
            width: valueRow.width
            height: 34

            // The chosen value, gliding from one to the next.
            Rectangle {
                readonly property Item target: valueRepeater.count > root.currentIndex
                                               && root.currentIndex >= 0
                                               ? valueRepeater.itemAt(root.currentIndex) : null

                visible: target !== null
                x: target ? target.x : 0
                width: 34
                height: 34
                radius: 17
                color: Material.accent

                Behavior on x {
                    NumberAnimation {
                        duration: 160
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Row {
                id: valueRow

                Repeater {
                    id: valueRepeater

                    model: root.values

                    delegate: Tool {
                        id: valueTool

                        required property var modelData
                        required property int index

                        tip: qsTr("%1 (+ longer, - shorter)").arg(modelData.tip)
                        onClicked: root.document.setInputValue(modelData.value)

                        contentItem: MusicSymbol {
                            symbol: valueTool.modelData.symbol
                            size: 22
                            color: valueTool.index === root.currentIndex ? "white" : root.ink
                        }
                    }
                }
            }
        }

        Tool {
            id: dot

            tip: qsTr("Dotted (.)")
            onClicked: root.document.toggleDot()

            background: Rectangle {
                radius: height / 2
                color: root.document.inputDotted ? Material.accent
                                                 : dot.hovered ? Qt.alpha(root.ink, 0.08)
                                                               : "transparent"
            }
            contentItem: MusicSymbol {
                symbol: "\uECB7"
                size: 30
                color: root.document.inputDotted ? "white" : root.ink
            }
        }

        // The tuplet (ADR-0050): a click toggles a triplet, a right click or a
        // long press picks another.
        Tool {
            id: tuplet

            readonly property bool on: root.document.inputTuplet > 0

            tip: qsTr("Triplet (Ctrl+3); a right click for other tuplets (Ctrl+2 to Ctrl+9)")
            onClicked: root.document.setInputTuplet(on ? 0 : 3)
            onPressAndHold: tupletMenu.popup(tuplet, 0, tuplet.height + 6)

            background: Rectangle {
                radius: height / 2
                color: tuplet.on ? Material.accent
                                 : tuplet.hovered ? Qt.alpha(root.ink, 0.08) : "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }
                }
            }
            contentItem: MusicSymbol {
                // The tuplet's number in the music font's tuplet digits.
                symbol: String.fromCharCode(0xE880 + (tuplet.on ? root.document.inputTuplet : 3))
                size: 22
                color: tuplet.on ? "white" : root.ink
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: tupletMenu.popup(tuplet, 0, tuplet.height + 6)
            }

            Menu {
                id: tupletMenu

                TupletItem {
                    actual: 2
                    text: qsTr("Duplet (2 in the time of 3)")
                }
                TupletItem {
                    actual: 3
                    text: qsTr("Triplet (3 in the time of 2)")
                }
                TupletItem {
                    actual: 4
                    text: qsTr("Quadruplet (4 in the time of 3)")
                }
                TupletItem {
                    actual: 5
                    text: qsTr("Quintuplet (5 in the time of 4)")
                }
                TupletItem {
                    actual: 6
                    text: qsTr("Sextuplet (6 in the time of 4)")
                }
                TupletItem {
                    actual: 7
                    text: qsTr("Septuplet (7 in the time of 4)")
                }
                TupletItem {
                    actual: 9
                    text: qsTr("Nonuplet (9 in the time of 8)")
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("No tuplet")
                    enabled: tuplet.on
                    onTriggered: root.document.setInputTuplet(0)
                }
            }
        }

        Item {
            width: 8
            height: 34

            Separator {
                anchors.centerIn: parent
            }
        }

        Tool {
            tip: qsTr("Insert a rest (Insert)")
            onClicked: root.document.insertRest()

            contentItem: MusicSymbol {
                symbol: "\uE4E5"
                size: 20
                color: root.ink
            }
        }
        Tool {
            tip: qsTr("Delete the note (Delete); the beat with Ctrl+Delete")
            onClicked: root.document.deleteAtCaret()

            contentItem: Icon {
                path: "M5 7 L19 7 M10 7 L10 4.5 L14 4.5 L14 7 M7 7 L8 20 L16 20 L17 7"
                color: root.ink
            }
        }

        Tool {
            tip: qsTr("Measure: signatures, insert, delete (Ctrl+M)")
            onClicked: root.measureRequested()

            contentItem: Icon {
                path: "M4 7 L4 17 M20 7 L20 17 M4 9 L20 9 M4 12 L20 12 M4 15 L20 15 M12 7 L12 17"
                color: root.ink
            }
        }

        Item {
            width: 8
            height: 34

            Separator {
                anchors.centerIn: parent
            }
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 4
            rightPadding: 10
            text: root.document.caretStatus
            font.pixelSize: 12
            font.features: { "tnum": 1 }
            color: root.ink
            opacity: 0.8
        }
    }
}
