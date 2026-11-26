// SPDX-License-Identifier: AGPL-3.0-or-later
// The caret's measure: its time and key signatures and its staff's clef, which
// change from it until the next change, its tempo, the section it starts, its
// signs and jumps, its repeats, and measures inserted before or after it, or it
// deleted. In a window too short for all of it, it scrolls.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

Sheet {
    id: root

    required property ScoreDocument document
    // What is at the document's caret, read and edited.
    required property CaretEditor editor

    readonly property var marks: document.caretNavigation
    readonly property var repeats: document.caretRepeats
    readonly property var endings: Array.from(repeats.endings ?? [])
    readonly property var tempo: document.caretTempo
    // The beat as a metronome mark of the music font, dotted when it is.
    readonly property string beatSymbol: {
        const notes = {
            "1": "",
            "2": "",
            "4": "",
            "8": "",
            "16": "",
            "32": ""
        };
        return (notes[tempo.beat] ?? "") + (tempo.dotted === true ? "" : "");
    }
    readonly property real labelWidth: 92
    /// The room the view leaves it, top to bottom.
    readonly property real room: (parent ? parent.height : 640) - 16 - topPadding - bottomPadding

    // The name before a field.
    component FieldLabel: Label {
        Layout.preferredWidth: root.labelWidth
        font.pixelSize: Theme.fontSmall
        color: Theme.inkSoft
        elide: Text.ElideRight
    }

    // The heading of a part of the popup, after a hairline.
    component Heading: ColumnLayout {
        property alias text: headingLabel.text

        Layout.fillWidth: true
        Layout.topMargin: 2
        spacing: 7

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.line
        }
        Label {
            id: headingLabel

            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            color: Theme.inkSoft
        }
    }

    width: 376
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    contentItem: Flickable {
        implicitHeight: Math.min(fields.implicitHeight, root.room)
        contentWidth: width
        contentHeight: fields.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: contentHeight > height

        ScrollBar.vertical: Scroll {}

        ColumnLayout {
            id: fields

            width: parent.width
            spacing: 6

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Measure %1").arg(root.document.caretMeasure + 1)
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                    color: Theme.ink
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

            SignatureFields {
                Layout.fillWidth: true
                labelWidth: root.labelWidth
                numerator: root.document.caretTimeNumerator
                denominator: root.document.caretTimeDenominator
                fifths: root.document.caretKeyFifths
                minor: root.document.caretKeyMinor
                onTimeChosen: (numerator, denominator) => root.document.setTimeSignatureFrom(
                                  root.document.caretMeasure, numerator, denominator)
                onKeyChosen: (fifths, minor) => root.document.setKeySignatureFrom(
                                 root.document.caretMeasure, fifths, minor)
            }

            // The clef of the caret's staff (ADR-0087); a guitar keeps its octave
            // clefs, drums their own.
            RowLayout {
                Layout.fillWidth: true
                visible: root.editor.caretClef >= 0
                spacing: 6

                FieldLabel {
                    text: qsTr("Clef")
                }
                Picker {
                    Layout.fillWidth: true
                    takesFocus: false
                    model: [qsTr("Treble"), qsTr("Bass"), qsTr("Alto"), qsTr("Tenor")]
                    currentIndex: root.editor.caretClef
                    onActivated: index => root.editor.setClef(index)
                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("The clef of this staff from this measure on")
                }
            }

            // A measure repeat sign (ADR-0102): the measure plays the one, or the
            // two, before it; taking the sign away writes out what it played.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                FieldLabel {
                    text: qsTr("Repeat")
                }
                Picker {
                    Layout.fillWidth: true
                    takesFocus: false
                    model: [qsTr("None"), qsTr("The measure before"), qsTr("The two measures before")]
                    currentIndex: root.editor.caretRepeat
                    onActivated: index => root.editor.setMeasureRepeat(index)
                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Play the measure, or the two measures, before this one: the sign takes the place of the music here")
                }
            }

            // The tempo the measure starts with (ADR-0042), in the beat the song
            // counts in there: a new one starts a tempo change, which the cross
            // takes away again. On the first measure it is the song's tempo. A
            // gradual change speeds up or slows down to the next tempo (ADR-0082).
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                FieldLabel {
                    Layout.preferredWidth: root.labelWidth - beat.width - equals.width - 12
                    text: qsTr("Tempo")
                }
                MusicSymbol {
                    id: beat

                    symbol: root.beatSymbol
                    size: 20
                    color: Theme.ink
                }
                Label {
                    id: equals

                    text: "="
                    color: Theme.inkSoft
                }
                Stepper {
                    Layout.fillWidth: true
                    from: 10
                    to: 999
                    value: root.tempo.bpm ?? 120
                    onValueModified: root.document.setTempoFrom(root.document.caretMeasure, value)
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Beats per minute from this measure on")
                }
                Chip {
                    visible: root.tempo.first !== true
                    text: qsTr("Gradual")
                    on: root.tempo.gradual === true
                    tip: qsTr("Speed up or slow down to the next tempo change")
                    onClicked: root.document.setTempoGradual(root.document.caretMeasure, !on)
                }
                IconButton {
                    implicitWidth: 28
                    implicitHeight: 28
                    padding: 6
                    takesFocus: false
                    visible: root.tempo.own === true && root.tempo.first !== true
                    iconPath: Icons.close
                    tip: qsTr("Remove the tempo change")
                    onClicked: root.document.setTempoFrom(root.document.caretMeasure, 0)
                }
            }

            // The triplet feel (ADR-0104): how the measure plays its pairs of
            // eighths or sixteenths, drawn above the staff where it changes.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                FieldLabel {
                    text: qsTr("Swing")
                }
                Picker {
                    Layout.fillWidth: true
                    takesFocus: false
                    model: [qsTr("Straight"), qsTr("Triplet eighths"), qsTr("Triplet sixteenths"),
                        qsTr("Dotted eighths"), qsTr("Dotted sixteenths"), qsTr("Scottish eighths"),
                        qsTr("Scottish sixteenths")]
                    currentIndex: root.document.caretTripletFeel
                    onActivated: index => root.document.setTripletFeelFrom(root.document.caretMeasure,
                                                                           index)
                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Play pairs of eighths or sixteenths long and short, as in a shuffle")
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("From this measure until the next change.")
                font.pixelSize: Theme.fontCaption
                color: Theme.inkSoft
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                FieldLabel {
                    text: qsTr("Section")
                }
                Field {
                    id: sectionField

                    Layout.fillWidth: true
                    text: root.document.caretSection
                    placeholderText: qsTr("Intro, Verse, Chorus…")
                    onEditingFinished: root.document.setSection(root.document.caretMeasure, text)
                }
            }

            Heading {
                text: qsTr("Signs and jumps")
            }

            Flow {
                Layout.fillWidth: true
                spacing: 5

                Repeater {
                    model: [
                        {
                            key: "segno",
                            label: qsTr("Segno")
                        },
                        {
                            key: "segnoSegno",
                            label: qsTr("Segno segno")
                        },
                        {
                            key: "coda",
                            label: qsTr("Coda")
                        },
                        {
                            key: "doubleCoda",
                            label: qsTr("Double coda")
                        },
                        {
                            key: "fine",
                            label: qsTr("Fine")
                        },
                        {
                            key: "toCoda",
                            label: qsTr("To coda")
                        },
                        {
                            key: "toDoubleCoda",
                            label: qsTr("To double coda")
                        }
                    ]

                    delegate: Chip {
                        required property var modelData

                        text: modelData.label
                        on: root.marks[modelData.key] === true
                        onClicked: {
                            const change = {};
                            change[modelData.key] = !on;
                            root.document.setNavigation(root.document.caretMeasure, change);
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Picker {
                    id: jumpField

                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    takesFocus: false
                    model: [qsTr("No jump"), qsTr("D.C."), qsTr("D.S."), qsTr("D.S.S.")]
                    currentIndex: root.marks.jump ?? 0
                    onActivated: index => root.document.setNavigation(root.document.caretMeasure, {
                                                                          jump: index
                                                                      })
                }
                Picker {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    takesFocus: false
                    enabled: jumpField.currentIndex > 0
                    model: [qsTr("to the end"), qsTr("al Fine"), qsTr("al Coda"), qsTr("al Double Coda")]
                    currentIndex: root.marks.jumpThen ?? 0
                    onActivated: index => root.document.setNavigation(root.document.caretMeasure, {
                                                                          jumpThen: index
                                                                      })
                }
            }

            Heading {
                text: qsTr("Repeats")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 5

                Chip {
                    text: qsTr("Start")
                    on: root.repeats.start === true
                    onClicked: root.document.setRepeats(root.document.caretMeasure, {
                                                            start: !on
                                                        })
                }
                Chip {
                    text: qsTr("End")
                    on: (root.repeats.times ?? 0) >= 2
                    onClicked: root.document.setRepeats(root.document.caretMeasure, {
                                                            times: on ? 0 : 2
                                                        })
                }
                Item {
                    Layout.fillWidth: true
                }
                Stepper {
                    id: timesField

                    readonly property int times: Math.max(2, root.repeats.times ?? 2)

                    Layout.preferredWidth: 140
                    focusPolicy: Qt.NoFocus
                    enabled: (root.repeats.times ?? 0) >= 2
                    from: 2
                    to: 255
                    value: times
                    textFromValue: (value, locale) => qsTr("%1 times").arg(value)
                    onValueModified: {
                        root.document.setRepeats(root.document.caretMeasure, {
                                                     times: value
                                                 });
                        value = Qt.binding(() => timesField.times);
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 3

                FieldLabel {
                    Layout.preferredWidth: implicitWidth + 6
                    text: qsTr("Endings")
                }
                Repeater {
                    model: 8

                    delegate: Chip {
                        required property int index

                        readonly property int time: index + 1

                        implicitWidth: 28
                        text: qsTr("%1.").arg(time)
                        on: root.endings.indexOf(time) >= 0
                        onClicked: {
                            const list = root.endings.filter(t => t !== time);
                            if (!on) {
                                list.push(time);
                            }
                            root.document.setRepeats(root.document.caretMeasure, {
                                                         endings: list
                                                     });
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
            }

            // The barline that closes the measure: a double one ends a section
            // (ADR-0103).
            RowLayout {
                Layout.fillWidth: true
                spacing: 3

                FieldLabel {
                    Layout.preferredWidth: implicitWidth + 6
                    text: qsTr("Barline")
                }
                Chip {
                    text: qsTr("Double")
                    on: root.repeats.doubleBar === true
                    tip: qsTr("Close this measure with a double barline, as at the end of a section")
                    onClicked: root.document.setRepeats(root.document.caretMeasure, {
                                                            doubleBar: !on
                                                        })
                }
                Item {
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 2
                Layout.preferredHeight: 1
                color: Theme.line
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 6

                TextButton {
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Insert before")
                    onClicked: root.document.insertMeasure(false)
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("An empty measure before this one (Ctrl+Insert)")
                }
                TextButton {
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Insert after")
                    onClicked: root.document.insertMeasure(true)
                }
                Item {
                    Layout.fillWidth: true
                }
                TextButton {
                    focusPolicy: Qt.NoFocus
                    enabled: root.document.measureCount > 1
                    kind: "danger"
                    text: qsTr("Delete")
                    onClicked: root.document.removeMeasure()
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Delete this measure from every track (Ctrl+Shift+Delete)")
                }
            }
        }
    }
}
