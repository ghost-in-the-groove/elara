// SPDX-License-Identifier: AGPL-3.0-or-later
// A time signature and a key signature to pick: beats per measure, beat unit,
// key by its tonic, major or minor. Shared by the song panel and the measure
// popup; whoever uses it applies the choice.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
    id: root

    property int numerator: 4
    property int denominator: 4
    property int fifths: 0
    property bool minor: false
    property real labelWidth: 104

    signal timeChosen(int numerator, int denominator)
    signal keyChosen(int fifths, bool minor)

    // Key signatures from seven flats to seven sharps, by their tonic.
    readonly property list<string> majorTonics: ["C♭", "G♭", "D♭", "A♭", "E♭", "B♭", "F", "C",
        "G", "D", "A", "E", "B", "F♯", "C♯"]
    readonly property list<string> minorTonics: ["A♭", "E♭", "B♭", "F", "C", "G", "D", "A",
        "E", "B", "F♯", "C♯", "G♯", "D♯", "A♯"]
    readonly property list<int> denominators: [1, 2, 4, 8, 16, 32]

    function keyLabel(index: int, minorKey: bool): string {
        const tonic = minorKey ? minorTonics[index] : majorTonics[index];
        const count = index - 7;
        if (count === 0)
            return tonic;
        return tonic + "  ·  " + Math.abs(count) + (count > 0 ? "♯" : "♭");
    }

    spacing: 8

    component FieldLabel: Label {
        Layout.preferredWidth: root.labelWidth
        opacity: 0.75
        elide: Text.ElideRight
    }

    RowLayout {
        Layout.fillWidth: true

        FieldLabel {
            text: qsTr("Time signature")
        }

        // Beats per measure, 1 to 32.
        ComboBox {
            Layout.fillWidth: true
            model: 32
            displayText: String(currentIndex + 1)
            delegate: ItemDelegate {
                required property int index

                width: ListView.view.width
                text: String(index + 1)
            }
            currentIndex: root.numerator - 1
            onActivated: index => root.timeChosen(index + 1, root.denominator)
        }

        Label {
            text: "/"
            font.pixelSize: 18
            opacity: 0.6
        }

        ComboBox {
            Layout.fillWidth: true
            model: root.denominators
            currentIndex: root.denominators.indexOf(root.denominator)
            onActivated: index => root.timeChosen(root.numerator, root.denominators[index])
        }
    }

    RowLayout {
        Layout.fillWidth: true

        FieldLabel {
            text: qsTr("Key signature")
        }

        ComboBox {
            Layout.fillWidth: true
            model: root.majorTonics.map((tonic, index) => root.keyLabel(index, root.minor))
            currentIndex: root.fifths + 7
            onActivated: index => root.keyChosen(index - 7, root.minor)
        }
    }

    SegmentedControl {
        Layout.alignment: Qt.AlignRight
        labels: [qsTr("Major"), qsTr("Minor")]
        currentIndex: root.minor ? 1 : 0
        onActivated: index => root.keyChosen(root.fifths, index === 1)
    }
}
