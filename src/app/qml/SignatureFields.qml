// SPDX-License-Identifier: AGPL-3.0-or-later
// A time signature and a key signature to pick: beats per measure, beat unit,
// key by its tonic, major or minor. Shared by the song panel and the measure
// popup; whoever uses it applies the choice.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

ColumnLayout {
    id: root

    property int numerator: 4
    property int denominator: 4
    property int fifths: 0
    property bool minor: false
    property real labelWidth: 92

    signal timeChosen(int numerator, int denominator)
    signal keyChosen(int fifths, bool minor)

    // Key signatures from seven flats to seven sharps, by their tonic.
    readonly property list<string> majorTonics: ["C♭", "G♭", "D♭", "A♭", "E♭", "B♭", "F", "C",
        "G", "D", "A", "E", "B", "F♯", "C♯"]
    readonly property list<string> minorTonics: ["A♭", "E♭", "B♭", "F", "C", "G", "D", "A",
        "E", "B", "F♯", "C♯", "G♯", "D♯", "A♯"]
    readonly property list<int> denominators: [1, 2, 4, 8, 16, 32]
    /// Major or minor beside the key where there is room, under it otherwise.
    readonly property bool modeBeside: width >= 336

    function keyLabel(index: int, minorKey: bool): string {
        const tonic = minorKey ? minorTonics[index] : majorTonics[index];
        const count = index - 7;
        if (count === 0)
            return tonic;
        return tonic + "  ·  " + Math.abs(count) + (count > 0 ? "♯" : "♭");
    }

    spacing: 6

    component FieldLabel: Label {
        Layout.preferredWidth: root.labelWidth
        font.pixelSize: Theme.fontSmall
        color: Theme.inkSoft
        elide: Text.ElideRight
    }

    component ModeSwitch: SegmentedControl {
        labels: [qsTr("Major"), qsTr("Minor")]
        currentIndex: root.minor ? 1 : 0
        onActivated: index => root.keyChosen(root.fifths, index === 1)
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        FieldLabel {
            text: qsTr("Time signature")
        }

        // Beats per measure, 1 to 32.
        Picker {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            takesFocus: false
            model: Array.from({ length: 32 }, (_, index) => String(index + 1))
            currentIndex: root.numerator - 1
            onActivated: index => root.timeChosen(index + 1, root.denominator)
            Accessible.name: qsTr("Beats in a measure")
        }

        Label {
            text: "/"
            font.pixelSize: Theme.fontTitle
            color: Theme.inkSoft
        }

        Picker {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            takesFocus: false
            model: root.denominators
            currentIndex: root.denominators.indexOf(root.denominator)
            onActivated: index => root.timeChosen(root.numerator, root.denominators[index])
            Accessible.name: qsTr("Beat unit")
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        FieldLabel {
            text: qsTr("Key signature")
        }

        Picker {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            takesFocus: false
            model: root.majorTonics.map((tonic, index) => root.keyLabel(index, root.minor))
            currentIndex: root.fifths + 7
            onActivated: index => root.keyChosen(index - 7, root.minor)
        }

        ModeSwitch {
            visible: root.modeBeside
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: !root.modeBeside
        spacing: 6

        Item {
            Layout.preferredWidth: root.labelWidth
        }
        ModeSwitch {}
        Item {
            Layout.fillWidth: true
        }
    }
}
