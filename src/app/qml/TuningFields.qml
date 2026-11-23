// SPDX-License-Identifier: AGPL-3.0-or-later
// A track's tuning and capo (ADR-0046): a preset or open strings typed from the
// lowest, the capo's fret, and whether a change keeps the sound or the frets.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
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

    spacing: 8

    component FieldLabel: Label {
        Layout.preferredWidth: 76
        font.pixelSize: 13
        opacity: 0.75
        elide: Text.ElideRight
    }

    ComboBox {
        id: presetField

        // A tuning no preset has comes first, as the notes it plays.
        readonly property var choices: (root.preset === "" ? [{
                                                                   id: "",
                                                                   name: qsTr("Custom"),
                                                                   notes: root.notes
                                                               }] : []).concat(root.presets)

        Layout.fillWidth: true
        focusPolicy: Qt.NoFocus
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

        delegate: ItemDelegate {
            id: choice

            required property var modelData
            required property int index

            width: ListView.view.width
            highlighted: presetField.highlightedIndex === index
            contentItem: ColumnLayout {
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: choice.modelData.name
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: choice.modelData.notes
                    font.pixelSize: 11
                    opacity: 0.55
                    elide: Text.ElideRight
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true

        FieldLabel {
            text: qsTr("Strings")
        }
        TextField {
            id: notesField

            Layout.fillWidth: true
            text: root.notes
            selectByMouse: true
            font.pixelSize: 13
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

        FieldLabel {
            text: qsTr("Capo")
        }
        SpinBox {
            id: capoField

            Layout.fillWidth: true
            from: 0
            to: 24
            editable: true
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
        font.pixelSize: 12
        opacity: 0.5
        wrapMode: Text.Wrap
    }
}
