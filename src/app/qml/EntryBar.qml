// SPDX-License-Identifier: AGPL-3.0-or-later
// The keyboard entry tools, at the left of the tool row: the note value typing
// uses, the dot, the tuplet, the voice, a rest, delete, the chord symbol, the
// text, the lyrics and the measure. The note effects have a palette of their
// own (EffectsPalette).
import QtQuick
import QtQuick.Controls
import Elara.App

Item {
    id: root

    required property ScoreDocument document

    /// The measure button: the caret's measure, its signatures and length.
    signal measureRequested
    /// The chord button: the chord symbol of the caret's beat (ADR-0080).
    signal chordRequested
    /// The text button: the text above the caret's beat (ADR-0081).
    signal textRequested
    /// The lyrics button: the verses of the caret's staff (ADR-0084).
    signal lyricsRequested

    // Note values from the music font's metronome marks, longest first.
    readonly property var values: [
        { value: 1, symbol: "", tip: qsTr("Whole note") },
        { value: 2, symbol: "", tip: qsTr("Half note") },
        { value: 4, symbol: "", tip: qsTr("Quarter note") },
        { value: 8, symbol: "", tip: qsTr("Eighth note") },
        { value: 16, symbol: "", tip: qsTr("16th note") },
        { value: 32, symbol: "", tip: qsTr("32nd note") },
        { value: 64, symbol: "", tip: qsTr("64th note") }
    ]
    readonly property int currentIndex: values.findIndex(entry => entry.value === document.inputValue)
    readonly property color ink: Theme.ink

    implicitWidth: row.implicitWidth
    implicitHeight: Theme.tool

    // A tool of the row: never takes the keyboard from the score.
    component Tool: AbstractButton {
        id: tool

        property string tip
        /// Lit in the accent: what typing uses now.
        property bool lit: false

        implicitWidth: Theme.tool
        implicitHeight: Theme.tool
        focusPolicy: Qt.NoFocus
        hoverEnabled: true

        background: Rectangle {
            radius: Theme.radiusSmall
            color: tool.lit ? Theme.accent
                            : tool.pressed ? Theme.pressed : tool.hovered ? Theme.hover : "transparent"
        }

        ToolTip.visible: hovered && tip.length > 0
        ToolTip.delay: 500
        ToolTip.text: tip
        Accessible.name: tip
    }

    // A tool drawn as a line icon, with a dot while the caret's beat has what
    // it sets.
    component IconTool: Tool {
        id: iconTool

        property string path
        property bool marked: false

        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                path: iconTool.path
                color: root.ink
            }
            Rectangle {
                width: 6
                height: 6
                radius: 3
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                color: Theme.accent
                visible: iconTool.marked
            }
        }
    }

    // A tuplet in the tuplet menu, checked when the input has it.
    component TupletItem: AppMenuItem {
        required property int actual

        chosen: root.document.inputTuplet === actual
        onTriggered: root.document.setInputTuplet(actual)
    }

    // A voice in the voice menu, checked when the caret is in it.
    component VoiceItem: AppMenuItem {
        required property int voice

        text: qsTr("Voice %1").arg(voice)
        chosen: root.document.caretVoice === voice
        onTriggered: root.document.setCaretVoice(voice)
    }

    component Separator: Item {
        width: 9
        height: Theme.tool

        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: 20
            color: Theme.line
        }
    }

    Row {
        id: row

        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Item {
            width: valueRow.width
            height: Theme.tool

            // The chosen value, gliding from one to the next.
            Rectangle {
                readonly property Item target: valueRepeater.count > root.currentIndex
                                               && root.currentIndex >= 0
                                               ? valueRepeater.itemAt(root.currentIndex) : null

                visible: target !== null
                x: target ? target.x : 0
                width: Theme.tool
                height: Theme.tool
                radius: Theme.radiusSmall
                color: Theme.accent

                Behavior on x {
                    NumberAnimation {
                        duration: Theme.quick
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
                            color: valueTool.index === root.currentIndex ? Theme.accentText : root.ink
                        }
                    }
                }
            }
        }

        Tool {
            tip: qsTr("Dotted (.)")
            lit: root.document.inputDotted
            onClicked: root.document.toggleDot()

            contentItem: MusicSymbol {
                symbol: ""
                size: 30
                color: root.document.inputDotted ? Theme.accentText : root.ink
            }
        }

        // The tuplet (ADR-0050): a click toggles a triplet, a right click or a
        // long press picks another.
        Tool {
            id: tuplet

            readonly property bool on: root.document.inputTuplet > 0

            tip: qsTr("Triplet (Ctrl+3); a right click for other tuplets (Ctrl+2 to Ctrl+9)")
            lit: on
            onClicked: root.document.setInputTuplet(on ? 0 : 3)
            onPressAndHold: tupletMenu.popup(tuplet, 0, tuplet.height + 6)

            contentItem: MusicSymbol {
                // The tuplet's number in the music font's tuplet digits.
                symbol: String.fromCharCode(0xE880 + (tuplet.on ? root.document.inputTuplet : 3))
                size: 30
                color: tuplet.on ? Theme.accentText : root.ink
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: tupletMenu.popup(tuplet, 0, tuplet.height + 6)
            }

            AppMenu {
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
                AppMenuSeparator {}
                AppMenuItem {
                    text: qsTr("No tuplet")
                    enabled: tuplet.on
                    onTriggered: root.document.setInputTuplet(0)
                }
            }
        }

        // The voice typing goes into (Alt+1 to Alt+4): a click goes on to the
        // next one, a right click picks one. Lit outside the first voice.
        Tool {
            id: voiceTool

            readonly property int voice: root.document.caretVoice

            tip: qsTr("Voice %1 (Alt+1 to Alt+4); a right click picks one").arg(voice)
            lit: voice > 1
            onClicked: root.document.setCaretVoice(voice % 4 + 1)
            onPressAndHold: voiceMenu.popup(voiceTool, 0, voiceTool.height + 6)

            contentItem: Label {
                text: qsTr("V%1").arg(voiceTool.voice)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                color: voiceTool.voice > 1 ? Theme.accentText : root.ink
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: voiceMenu.popup(voiceTool, 0, voiceTool.height + 6)
            }

            AppMenu {
                id: voiceMenu

                VoiceItem {
                    voice: 1
                }
                VoiceItem {
                    voice: 2
                }
                VoiceItem {
                    voice: 3
                }
                VoiceItem {
                    voice: 4
                }
            }
        }

        Separator {}

        Tool {
            tip: qsTr("Insert a rest (Insert)")
            onClicked: root.document.insertRest()

            contentItem: MusicSymbol {
                symbol: ""
                size: 20
                color: root.ink
            }
        }
        IconTool {
            tip: qsTr("Delete the note (Delete); the beat with Ctrl+Delete")
            path: Icons.trash
            onClicked: root.document.deleteAtCaret()
        }

        Separator {}

        IconTool {
            tip: qsTr("Chord symbol (Ctrl+K)")
            path: Icons.chord
            marked: root.document.caretChord.length > 0
            onClicked: root.chordRequested()
        }
        IconTool {
            tip: qsTr("Text (Ctrl+T)")
            path: Icons.text
            marked: root.document.caretText.length > 0
            onClicked: root.textRequested()
        }
        IconTool {
            tip: qsTr("Lyrics (Ctrl+Shift+L)")
            path: Icons.lyrics
            onClicked: root.lyricsRequested()
        }
        IconTool {
            tip: qsTr("Measure: signatures, insert, delete (Ctrl+M)")
            path: Icons.measure
            onClicked: root.measureRequested()
        }
    }
}
