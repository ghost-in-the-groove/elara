// SPDX-License-Identifier: AGPL-3.0-or-later
// The lyrics of the caret's staff, typed a verse at a time (ADR-0084): a
// syllable for each note of its first voice, from a measure on.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import Elara.App

Sheet {
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

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: qsTr("Lyrics")
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                color: Theme.ink
            }
            IconButton {
                implicitWidth: 28
                implicitHeight: 28
                padding: 6
                visible: root.verse < root.verseCount
                iconPath: Icons.trash
                tip: qsTr("Remove this verse")
                onClicked: {
                    root.document.setVerse(root.verse, 0, "");
                    root.verseCount = root.document.caretVerseCount();
                    root.pickVerse(Math.min(root.verse, root.verseCount));
                }
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

        // The verses the staff sings, then one more to start a new verse.
        RowLayout {
            Layout.fillWidth: true
            spacing: 5

            Label {
                rightPadding: 3
                text: qsTr("Verse")
                font.pixelSize: Theme.fontSmall
                color: Theme.inkSoft
            }
            Repeater {
                model: Math.min(root.verseCount + 1, root.maxVerses)

                delegate: Chip {
                    required property int index
                    readonly property bool added: index === root.verseCount

                    implicitWidth: Math.max(28, implicitContentWidth + 18)
                    // The verse being added wears its number once it is the
                    // one written in.
                    text: added && !on ? "+" : String(index + 1)
                    on: index === root.verse
                    tip: added ? qsTr("New verse") : ""
                    onClicked: root.pickVerse(index)
                    Accessible.name: added ? qsTr("New verse") : qsTr("Verse %1").arg(index + 1)
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }

        // The verse, several lines of it.
        Flickable {
            id: verseBox

            Layout.fillWidth: true
            Layout.preferredHeight: 116
            contentWidth: width
            contentHeight: verseField.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ScrollBar.vertical: Scroll {}

            // Keeps the line being typed in sight.
            function follow(caret: rect) {
                if (caret.y < contentY)
                    contentY = caret.y;
                else if (caret.y + caret.height > contentY + height)
                    contentY = caret.y + caret.height - height;
            }

            Rectangle {
                parent: verseBox
                anchors.fill: parent
                z: -1
                radius: Theme.radiusSmall
                color: Theme.surface
                border.width: verseField.activeFocus ? 2 : 1
                border.color: verseField.activeFocus ? Theme.accentInk : Theme.line
            }

            T.TextArea {
                id: verseField

                width: verseBox.width
                implicitHeight: Math.max(verseBox.height, contentHeight + topPadding + bottomPadding)
                padding: 9
                placeholderText: qsTr("Verse text")
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                font.pixelSize: Theme.fontTitle
                color: Theme.ink
                selectionColor: Qt.alpha(Theme.accent, 0.4)
                selectedTextColor: Theme.ink
                placeholderTextColor: Theme.inkSoft
                Accessible.name: qsTr("Verse text")
                onTextChanged: root.leftover = 0
                onCursorRectangleChanged: verseBox.follow(cursorRectangle)
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

                Text {
                    x: verseField.leftPadding
                    y: verseField.topPadding
                    visible: verseField.length === 0 && verseField.preeditText.length === 0
                    text: verseField.placeholderText
                    font: verseField.font
                    color: verseField.placeholderTextColor
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("A syllable for each note. Spaces and hyphens part syllables, one more space skips a note, _ holds a syllable and + joins two words.")
            font.pixelSize: Theme.fontCaption
            color: Theme.inkSoft
            wrapMode: Text.Wrap
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
                font.pixelSize: Theme.fontSmall
                color: Theme.inkSoft
            }
            Stepper {
                id: startField

                Layout.preferredWidth: 120
                from: 1
                to: Math.max(1, root.document.measureCount)
                Accessible.name: qsTr("First measure of the verse")
            }
            Item {
                Layout.fillWidth: true
            }
            TextButton {
                kind: "main"
                text: qsTr("Apply")
                onClicked: root.apply()
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Ctrl+Enter")
            }
        }
    }
}
