// SPDX-License-Identifier: AGPL-3.0-or-later
// A track's tuning and capo (ADR-0046): a preset or open strings typed from the
// lowest, the capo's fret, and whether a change keeps the sound or the frets.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

ColumnLayout {
    id: root

    required property ScoreDocument document
    required property int track
    /// The track as ScoreDocument.tracks gives it.
    required property var info
    /// A change keeps the tablature's frets instead of the sound.
    property bool keepFrets: false

    readonly property var presets: document.tuningPresets
    readonly property string preset: info.tuningPreset ?? ""
    readonly property string notes: info.tuning ?? ""
    readonly property int capo: info.capo ?? 0

    function apply(tuning: string, capo: int) {
        root.document.setTrackTuning(root.track, tuning, capo, root.keepFrets);
    }

    spacing: 6

    component FieldLabel: Label {
        Layout.preferredWidth: 70
        font.pixelSize: Theme.fontSmall
        color: Theme.inkSoft
        elide: Text.ElideRight
    }

    Picker {
        id: presetField

        // A tuning no preset has comes first, as the notes it plays.
        readonly property var choices: (root.preset === "" ? [{
                                                                   id: "",
                                                                   name: qsTr("Custom"),
                                                                   notes: root.notes
                                                               }] : []).concat(root.presets)

        Layout.fillWidth: true
        takesFocus: false
        model: choices
        textRole: "name"
        currentIndex: choices.findIndex(choice => choice.id === root.preset)
        onActivated: index => {
            const id = choices[index].id;
            if (id !== "" && id !== root.preset) {
                root.apply(id, root.capo);
            }
            currentIndex = Qt.binding(() => choices.findIndex(choice => choice.id === root.preset));
        }
        Accessible.name: qsTr("Tuning")

        // Each tuning with the notes of its strings under its name.
        delegate: ItemDelegate {
            id: choice

            required property var modelData
            required property int index

            width: ListView.view ? ListView.view.width : implicitWidth
            implicitHeight: 42
            leftPadding: 30
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            highlighted: presetField.highlightedIndex === index

            contentItem: Column {
                topPadding: 5
                spacing: 0

                Label {
                    width: parent.width
                    text: choice.modelData.name
                    font.pixelSize: Theme.fontBody
                    color: Theme.ink
                    elide: Text.ElideRight
                }
                Label {
                    width: parent.width
                    text: choice.modelData.notes
                    font.pixelSize: Theme.fontCaption
                    color: Theme.inkSoft
                    elide: Text.ElideRight
                }
            }

            background: Rectangle {
                radius: Theme.radiusSmall - 1
                color: choice.highlighted ? Theme.hover : "transparent"

                Icon {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    visible: choice.index === presetField.currentIndex
                    path: Icons.check
                    lineWidth: 2.2
                    color: Theme.accentInk
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        FieldLabel {
            text: qsTr("Strings")
        }
        Field {
            id: notesField

            Layout.fillWidth: true
            text: root.notes
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Open strings from the lowest; Enter applies them")
            onAccepted: {
                if (text.trim() !== root.notes) {
                    root.apply(text, root.capo);
                }
                text = Qt.binding(() => root.notes);
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        FieldLabel {
            text: qsTr("Capo")
        }
        Stepper {
            id: capoField

            Layout.fillWidth: true
            from: 0
            to: 24
            value: root.capo
            textFromValue: (value, locale) => value === 0 ? qsTr("None") : qsTr("Fret %1").arg(value)
            valueFromText: (text, locale) => {
                const digits = text.replace(/[^0-9]/g, "");
                return digits === "" ? 0 : parseInt(digits);
            }
            onValueModified: {
                root.apply(root.notes, value);
                value = Qt.binding(() => root.capo);
            }
        }
    }

    // What a change keeps: the sound or the frets.
    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        FieldLabel {
            text: qsTr("Keep")
        }
        SegmentedControl {
            labels: [qsTr("the sound"), qsTr("the frets")]
            currentIndex: root.keepFrets ? 1 : 0
            onActivated: index => root.keepFrets = index === 1
        }
        Item {
            Layout.fillWidth: true
        }
    }

    Label {
        Layout.fillWidth: true
        text: root.keepFrets ? qsTr("The tablature stays; each note sounds what its fret plays now.")
                             : qsTr("The music sounds the same; its notes move to other frets.")
        font.pixelSize: Theme.fontCaption
        color: Theme.inkSoft
        wrapMode: Text.Wrap
    }
}
