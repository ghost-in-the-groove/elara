// SPDX-License-Identifier: AGPL-3.0-or-later
// The lyrics of the caret's staff, typed a verse at a time (ADR-0084): a
// syllable for each note of its first voice, from a measure on.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Elara.App

Popup {
    id: root

    required property ScoreDocument document
    /// The verse shown, 0 for the first.
    property int verse: 0
    /// The verses the caret's staff sings.
    property int verseCount: 0
    /// The verse's text as typed in the field.
    readonly property alias text: verseField.text
    /// Syllables the last apply left out after the last note.
    property int leftover: 0
    /// The verse's text as last loaded into the field.
    property string loaded

    readonly property int maxVerses: document.maxVerses
    readonly property color ink: Theme.ink

    /// Shows verse `verse` as the staff sings it: its text and first measure,
    /// the caret's measure for a verse not typed yet.
    function load() {
        const typed = root.document.caretVerse(root.verse);
        root.loaded = typed.text;
        verseField.text = typed.text;
        startField.value = (typed.text.length > 0 ? typed.first : root.document.caretMeasure) + 1;
        root.leftover = 0;
    }
    /// Follows the caret's staff when it or its lyrics change while the
    /// popup is open (another track, an undo), unless the field was typed in.
    function refresh() {
        root.verseCount = root.document.caretVerseCount();
        root.verse = Math.max(0, Math.min(root.verse, root.verseCount, root.maxVerses - 1));
        if (verseField.text === root.loaded && root.document.caretVerse(root.verse).text !== root.loaded)
            load();
    }
    function pickVerse(verse: int) {
        if (root.verse === verse)
            load();
        else
            root.verse = verse;
        verseField.forceActiveFocus();
    }
    function apply() {
        root.leftover = root.document.setVerse(root.verse, startField.value - 1, verseField.text);
        root.verseCount = root.document.caretVerseCount();
        if (root.leftover === 0)
            root.close();
    }

    width: 420
    padding: 16
    topPadding: 10
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    onVerseChanged: load()
    onAboutToShow: {
        root.verseCount = root.document.caretVerseCount();
        root.verse = Math.max(0, Math.min(root.verse, root.verseCount, root.maxVerses - 1));
        load();
    }
    onOpened: verseField.forceActiveFocus()

    Connections {
        target: root.document
        enabled: root.visible

        function onCaretChanged() {
            root.refresh();
        }
        function onSongChanged() {
            root.refresh();
        }
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
                text: qsTr("Lyrics")
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                color: root.ink
            }
            IconButton {
                visible: root.verse < root.verseCount
                iconPath: "M6 7 L18 7 M9 7 L9 19 L15 19 L15 7 M10 4 L14 4"
                tip: qsTr("Remove this verse")
                onClicked: {
                    root.document.setVerse(root.verse, 0, "");
                    root.verseCount = root.document.caretVerseCount();
                    root.pickVerse(Math.min(root.verse, root.verseCount));
                }
            }
            IconButton {
                iconPath: "M7 7 L17 17 M17 7 L7 17"
                tip: qsTr("Close")
                onClicked: root.close()
            }
        }

        // The verses the staff sings, then one more to start a new verse.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: qsTr("Verse")
                font.pixelSize: Theme.fontBody
                color: root.ink
                opacity: 0.7
            }
            Repeater {
                model: Math.min(root.verseCount + 1, root.maxVerses)

                delegate: AbstractButton {
                    id: chip

                    required property int index
                    readonly property bool current: index === root.verse
                    readonly property bool added: index === root.verseCount

                    text: added ? "+" : String(index + 1)
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: true
                    implicitHeight: 30
                    implicitWidth: Math.max(30, chipLabel.implicitWidth + 18)
                    onClicked: root.pickVerse(index)
                    Accessible.name: added ? qsTr("New verse") : qsTr("Verse %1").arg(index + 1)
                    ToolTip.visible: added && hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("New verse")

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
            Item {
                Layout.fillWidth: true
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 116

            TextArea {
                id: verseField

                placeholderText: qsTr("Verse text")
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                font.pixelSize: Theme.fontTitle
                Accessible.name: qsTr("Verse text")
                onTextChanged: root.leftover = 0
                Keys.onReturnPressed: event => {
                    if (event.modifiers & Qt.ControlModifier)
                        root.apply();
                    else
                        event.accepted = false;
                }
                Keys.onEnterPressed: event => {
                    if (event.modifiers & Qt.ControlModifier)
                        root.apply();
                    else
                        event.accepted = false;
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("A syllable for each note. Spaces and hyphens part syllables, one more space skips a note, _ holds a syllable and + joins two words.")
            font.pixelSize: Theme.fontSmall
            opacity: 0.5
            wrapMode: Text.Wrap
            color: root.ink
        }

        Label {
            Layout.fillWidth: true
            visible: root.leftover > 0
            text: qsTr("Syllables after the last note left out: %1").arg(root.leftover)
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            color: Theme.warning
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("From measure")
                font.pixelSize: Theme.fontBody
                color: root.ink
                opacity: 0.7
            }
            SpinBox {
                id: startField

                Layout.preferredWidth: 130
                from: 1
                to: Math.max(1, root.document.measureCount)
                editable: true
                Accessible.name: qsTr("First measure of the verse")
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Apply")
                highlighted: true
                onClicked: root.apply()
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Ctrl+Enter")
            }
        }
    }
}
