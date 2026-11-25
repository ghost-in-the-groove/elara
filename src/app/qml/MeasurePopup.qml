// SPDX-License-Identifier: AGPL-3.0-or-later
// The caret's measure: its time and key signatures and its staff's clef, which
// change from it until the next change, its tempo, the section it starts, its
// signs and jumps, its repeats, and measures inserted before or after it, or it
// deleted.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Elara.App

Popup {
    id: root

    required property ScoreDocument document

    readonly property color ink: Theme.ink
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

    // A mark that is on or off, as a rounded chip.
    component Chip: AbstractButton {
        id: chip

        property bool on: false

        focusPolicy: Qt.NoFocus
        implicitHeight: 30
        implicitWidth: chipLabel.implicitWidth + 24
        contentItem: Label {
            id: chipLabel

            text: chip.text
            font.pixelSize: Theme.fontBody
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: chip.on ? "white" : root.ink
        }
        background: Rectangle {
            radius: height / 2
            color: chip.on ? chip.Material.accent
                           : (chip.hovered ? Theme.hover : "transparent")
            border.color: chip.on ? chip.Material.accent : Theme.line

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }
        }
    }

    width: 360
    padding: 18
    topPadding: 12
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

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
                text: qsTr("Measure %1").arg(root.document.caretMeasure + 1)
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                color: root.ink
            }
            IconButton {
                iconPath: "M7 7 L17 17 M17 7 L7 17"
                tip: qsTr("Close")
                onClicked: root.close()
            }
        }

        SignatureFields {
            Layout.fillWidth: true
            labelWidth: 112
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
            visible: root.document.caretClef >= 0
            spacing: 6

            Label {
                Layout.preferredWidth: 112
                text: qsTr("Clef")
                opacity: 0.75
                elide: Text.ElideRight
            }
            ComboBox {
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                model: [qsTr("Treble"), qsTr("Bass"), qsTr("Alto"), qsTr("Tenor")]
                currentIndex: root.document.caretClef
                onActivated: index => root.document.setClef(index)
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("The clef of this staff from this measure on")
            }
        }

        // The tempo the measure starts with (ADR-0042), in the beat the song
        // counts in there: a new one starts a tempo change, which the cross
        // takes away again. On the first measure it is the song's tempo.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                Layout.preferredWidth: 112
                text: qsTr("Tempo")
                opacity: 0.75
                elide: Text.ElideRight
            }
            MusicSymbol {
                symbol: root.beatSymbol
                size: 22
                color: root.ink
            }
            Label {
                text: "="
                opacity: 0.75
            }
            SpinBox {
                Layout.fillWidth: true
                from: 10
                to: 999
                editable: true
                value: root.tempo.bpm ?? 120
                onValueModified: root.document.setTempoFrom(root.document.caretMeasure, value)
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Beats per minute from this measure on")
            }
            IconButton {
                visible: root.tempo.own === true && root.tempo.first !== true
                iconPath: "M7 7 L17 17 M17 7 L7 17"
                tip: qsTr("Remove the tempo change")
                onClicked: root.document.setTempoFrom(root.document.caretMeasure, 0)
            }
        }

        // A gradual change speeds up or slows down to the next tempo
        // (ADR-0082).
        RowLayout {
            Layout.fillWidth: true
            visible: root.tempo.first !== true
            spacing: 6

            Item {
                Layout.preferredWidth: 112
            }
            Chip {
                text: qsTr("Gradual")
                on: root.tempo.gradual === true
                onClicked: root.document.setTempoGradual(root.document.caretMeasure, !on)
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Speed up or slow down to the next tempo change")
            }
            Item {
                Layout.fillWidth: true
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("From this measure until the next change.")
            font.pixelSize: Theme.fontSmall
            opacity: 0.5
            wrapMode: Text.Wrap
            color: root.ink
        }

        // The section of the song that starts here (ADR-0062), drawn boxed
        // above the staff; an empty name takes it off.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                Layout.preferredWidth: 112
                text: qsTr("Section")
                opacity: 0.75
                elide: Text.ElideRight
            }
            TextField {
                id: sectionField

                Layout.fillWidth: true
                text: root.document.caretSection
                placeholderText: qsTr("Intro, Verse, Chorus…")
                selectByMouse: true
                onEditingFinished: root.document.setSection(root.document.caretMeasure, text)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 2
            height: 1
            color: Theme.line
        }

        Label {
            text: qsTr("Signs and jumps")
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: root.ink
        }

        // The signs that open the measure and the marks that close it.
        Flow {
            Layout.fillWidth: true
            spacing: 6

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

        // The jump at the end of the measure and what ends the music after it.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ComboBox {
                id: jumpField

                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                model: [qsTr("No jump"), qsTr("D.C."), qsTr("D.S."), qsTr("D.S.S.")]
                currentIndex: root.marks.jump ?? 0
                onActivated: index => root.document.setNavigation(root.document.caretMeasure, {
                                                                      jump: index
                                                                  })
            }
            ComboBox {
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                enabled: jumpField.currentIndex > 0
                model: [qsTr("to the end"), qsTr("al Fine"), qsTr("al Coda"), qsTr("al Double Coda")]
                currentIndex: root.marks.jumpThen ?? 0
                onActivated: index => root.document.setNavigation(root.document.caretMeasure, {
                                                                      jumpThen: index
                                                                  })
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 2
            height: 1
            color: Theme.line
        }

        Label {
            text: qsTr("Repeats")
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: root.ink
        }

        // The barlines that open and close a repeated section, and how many
        // times the section plays.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

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
            SpinBox {
                id: timesField

                readonly property int times: Math.max(2, root.repeats.times ?? 2)

                Layout.preferredWidth: 140
                focusPolicy: Qt.NoFocus
                enabled: (root.repeats.times ?? 0) >= 2
                opacity: enabled ? 1 : 0.4
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

        // The times through the repeat that play this measure, as alternate
        // endings; none lit, it plays every time.
        Flow {
            Layout.fillWidth: true
            spacing: 3

            Label {
                height: 30
                rightPadding: 5
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Endings")
                font.pixelSize: 13
                color: Qt.alpha(root.ink, 0.7)
            }
            Repeater {
                model: 8

                delegate: Chip {
                    required property int index

                    readonly property int time: index + 1

                    // Round, so that the eight fit beside their label.
                    implicitWidth: 30
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
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 2
            height: 1
            color: Qt.alpha(root.ink, 0.1)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Button {
                flat: true
                focusPolicy: Qt.NoFocus
                text: qsTr("Insert before")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("An empty measure before this one (Ctrl+Insert)")
                onClicked: root.document.insertMeasure(false)
            }
            Button {
                flat: true
                focusPolicy: Qt.NoFocus
                text: qsTr("Insert after")
                onClicked: root.document.insertMeasure(true)
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                flat: true
                focusPolicy: Qt.NoFocus
                enabled: root.document.measureCount > 1
                text: qsTr("Delete")
                Material.foreground: Theme.danger
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Delete this measure from every track (Ctrl+Shift+Delete)")
                onClicked: root.document.removeMeasure()
            }
        }
    }
}
