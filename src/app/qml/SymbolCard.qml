// SPDX-License-Identifier: AGPL-3.0-or-later
// A symbol of the score edited where it is drawn (ADR-0130): the card a double
// click on it opens, with the one thing that symbol is. A time or a key
// signature, a clef, a tempo, or the name of a section; each changes the song
// from the measure the symbol's value starts at until the next change.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

Sheet {
    id: root

    required property ScoreDocument document
    // What is at the document's caret, read and edited; it changes a clef.
    required property CaretEditor editor

    /// "time", "key", "clef", "tempo" or "section"; "" while it shows nothing.
    property string kind: ""
    /// The measure the symbol's value starts at: the one that is changed.
    property int measure: 0
    /// The staff of a clef.
    property int track: 0
    property int staff: 0

    // What the measure has now, read again whenever the song changes.
    property var signature: ({})
    property var tempo: ({})
    property int clef: -1
    property string section: ""

    readonly property string heading: {
        switch (kind) {
        case "time":
            return qsTr("Time signature");
        case "key":
            return qsTr("Key signature");
        case "clef":
            return qsTr("Clef");
        case "tempo":
            return qsTr("Tempo");
        case "section":
            return qsTr("Section");
        }
        return "";
    }
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

    /// Shows the card for `symbol`, as ScoreDocument::symbolAt() gives it.
    function show(symbol) {
        kind = symbol.kind;
        measure = symbol.measure;
        track = symbol.track;
        staff = symbol.staff;
        refresh();
        open();
        if (kind === "section")
            sectionField.forceActiveFocus();
    }
    function refresh() {
        signature = document.signatureAt(measure);
        tempo = document.tempoAt(measure);
        clef = editor.clefAt(track, staff, measure);
        section = document.sectionAt(measure);
    }

    width: 336
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    Connections {
        target: root.document
        enabled: root.opened

        function onSongChanged() {
            root.refresh();
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: root.heading
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
            visible: root.kind === "time" || root.kind === "key"
            showTime: root.kind === "time"
            showKey: root.kind === "key"
            labelWidth: 0
            numerator: root.signature.numerator ?? 4
            denominator: root.signature.denominator ?? 4
            fifths: root.signature.fifths ?? 0
            minor: root.signature.minor === true
            onTimeChosen: (numerator, denominator) => root.document.setTimeSignatureFrom(
                              root.measure, numerator, denominator)
            onKeyChosen: (fifths, minor) => root.document.setKeySignatureFrom(root.measure, fifths,
                                                                             minor)
        }

        Picker {
            Layout.fillWidth: true
            visible: root.kind === "clef"
            takesFocus: false
            model: [qsTr("Treble"), qsTr("Bass"), qsTr("Alto"), qsTr("Tenor")]
            currentIndex: root.clef
            onActivated: index => root.editor.setClefFrom(root.track, root.staff, root.measure, index)
            Accessible.name: qsTr("Clef")
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.kind === "tempo"
            spacing: 6

            MusicSymbol {
                symbol: root.beatSymbol
                size: 20
                color: Theme.ink
            }
            Label {
                text: "="
                color: Theme.inkSoft
            }
            Stepper {
                objectName: "symbolTempo"
                Layout.fillWidth: true
                from: 10
                to: 999
                value: root.tempo.bpm ?? 120
                onValueModified: root.document.setTempoFrom(root.measure, value)
                Accessible.name: qsTr("Beats per minute")
            }
            Chip {
                visible: root.tempo.first !== true
                text: qsTr("Gradual")
                on: root.tempo.gradual === true
                tip: qsTr("Speed up or slow down to the next tempo change")
                onClicked: root.document.setTempoGradual(root.measure, !on)
            }
            IconButton {
                implicitWidth: 28
                implicitHeight: 28
                padding: 6
                takesFocus: false
                visible: root.tempo.own === true && root.tempo.first !== true
                iconPath: Icons.close
                tip: qsTr("Remove the tempo change")
                onClicked: {
                    root.document.setTempoFrom(root.measure, 0);
                    root.close();
                }
            }
        }

        Field {
            id: sectionField

            objectName: "symbolSection"
            Layout.fillWidth: true
            visible: root.kind === "section"
            text: root.section
            placeholderText: qsTr("Intro, Verse, Chorus…")
            Accessible.name: qsTr("Section")
            onEditingFinished: {
                if (root.opened && root.kind === "section")
                    root.document.setSection(root.measure, text);
            }
            Keys.onReturnPressed: {
                root.document.setSection(root.measure, text);
                root.close();
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.kind !== "section"
            text: root.measure === 0 ? qsTr("From the first measure until the next change.")
                                     : qsTr("From measure %1 until the next change.").arg(
                                           root.measure + 1)
            font.pixelSize: Theme.fontCaption
            color: Theme.inkSoft
            wrapMode: Text.Wrap
        }
    }
}
