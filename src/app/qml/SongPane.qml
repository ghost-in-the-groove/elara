// SPDX-License-Identifier: AGPL-3.0-or-later
// The song panel, docked at the right of the score: what the whole song is
// (title, artist, credits, tempo, signatures, length) and its tracks, each in
// its own hue with its mix and its tuning. Every change is one edit of the
// document, so Ctrl+Z takes it back.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import Elara.App

Rectangle {
    id: root

    required property ScoreDocument document

    signal closeRequested

    /// Its width while it shows.
    readonly property real fullWidth: 320
    /// The width of the names before the fields.
    readonly property real labelWidth: 92

    /// The track whose tuning shows under its row, -1 for none.
    property int tuningTrack: -1

    /// Shows the tuning of `track` under its row.
    function openTuning(track: int) {
        tuningTrack = track;
    }

    /// Scrolls the panel so that all of `item` shows.
    function reveal(item: Item) {
        const top = item.mapToItem(flick.contentItem, 0, 0).y;
        const bottom = top + item.height + 12;
        if (bottom > flick.contentY + flick.height) {
            flick.contentY = Math.min(top - 12, bottom - flick.height);
        } else if (top < flick.contentY) {
            flick.contentY = Math.max(0, top - 12);
        }
    }

    color: Theme.bar
    clip: true

    // The heading of a group of fields; one that folds hides or shows them.
    component GroupHeading: AbstractButton {
        id: heading

        property bool open: true
        property bool folds: true

        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: 24
        focusPolicy: folds ? Qt.TabFocus : Qt.NoFocus
        hoverEnabled: folds
        Accessible.name: text
        onClicked: {
            if (folds)
                open = !open;
        }

        background: Rectangle {
            radius: 4
            color: "transparent"
            border.width: heading.visualFocus ? 2 : 0
            border.color: Theme.accentInk
        }

        contentItem: Item {
            Label {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 18
                text: heading.text
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                color: heading.hovered ? Theme.ink : Theme.inkSoft
                elide: Text.ElideRight
            }
            Icon {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: heading.folds
                width: 14
                height: 14
                path: heading.open ? Icons.chevronDown : Icons.chevronRight
                color: heading.hovered ? Theme.ink : Theme.inkSoft
            }
        }
    }

    // The name before a field.
    component FieldLabel: Label {
        Layout.preferredWidth: root.labelWidth
        font.pixelSize: Theme.fontSmall
        color: Theme.inkSoft
        elide: Text.ElideRight
    }

    // A named line of the credits.
    component CreditField: RowLayout {
        id: credit

        required property int line
        required property string name

        Layout.fillWidth: true

        FieldLabel {
            text: credit.name
        }
        Field {
            Layout.fillWidth: true
            text: root.document.credits[credit.line]
            Accessible.name: credit.name
            onEditingFinished: root.document.setCredit(credit.line, text)
        }
    }

    // Mute or solo: a letter, lit in its colour when on.
    component MixToggle: AbstractButton {
        id: toggle

        property string tip
        property color tint

        implicitWidth: 24
        implicitHeight: 22
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        Accessible.role: Accessible.CheckBox
        Accessible.name: tip
        Accessible.checked: checked
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.text: tip

        background: Rectangle {
            radius: Theme.radiusSmall - 1
            color: toggle.checked ? toggle.tint : toggle.hovered ? Theme.pressed : Theme.hover
        }
        contentItem: Label {
            text: toggle.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: Theme.fontCaption
            font.weight: Font.Bold
            color: toggle.checked ? "#1a1408" : Theme.ink
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 6
            Layout.preferredHeight: 40

            Label {
                Layout.fillWidth: true
                text: qsTr("Song and tracks")
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                color: Theme.ink
                elide: Text.ElideRight
            }

            IconButton {
                implicitWidth: 28
                implicitHeight: 28
                padding: 6
                iconPath: Icons.close
                tip: qsTr("Close (Ctrl+I)")
                onClicked: root.closeRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.line
        }

        Flickable {
            id: flick

            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: fields.implicitHeight + 16
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ScrollBar.vertical: Scroll {}

            ColumnLayout {
                id: fields

                x: 14
                y: 2
                width: root.fullWidth - 29
                spacing: 6

                GroupHeading {
                    id: songGroup

                    text: qsTr("Song")
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: songGroup.open

                    FieldLabel {
                        text: qsTr("Title")
                    }
                    Field {
                        Layout.fillWidth: true
                        text: root.document.songTitle
                        Accessible.name: qsTr("Title")
                        onEditingFinished: root.document.setSongTitle(text)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: songGroup.open

                    FieldLabel {
                        text: qsTr("Artist")
                    }
                    Field {
                        Layout.fillWidth: true
                        text: root.document.artist
                        Accessible.name: qsTr("Artist")
                        onEditingFinished: root.document.setArtist(text)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: songGroup.open

                    FieldLabel {
                        text: qsTr("Tempo")
                    }

                    Stepper {
                        Layout.fillWidth: true
                        from: 10
                        to: 999
                        value: root.document.tempo
                        textFromValue: (value, locale) => qsTr("♩ = %1").arg(value)
                        valueFromText: (text, locale) => parseInt(text.replace(/[^0-9]/g, ""))
                        onValueModified: root.document.setTempo(value)
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Quarter notes per minute")
                    }
                }

                GroupHeading {
                    id: creditsGroup

                    open: false
                    text: qsTr("Credits")
                }

                // The other lines of the credits (ADR-0059), in the order of
                // elara::core::Credit; the score shows the subtitle, the words
                // and music and the copyright.
                Repeater {
                    model: [
                        { line: 1, name: qsTr("Subtitle") },
                        { line: 3, name: qsTr("Album") },
                        { line: 4, name: qsTr("Words by") },
                        { line: 5, name: qsTr("Music by") },
                        { line: 6, name: qsTr("Copyright") },
                        { line: 7, name: qsTr("Transcribed by") }
                    ]

                    delegate: CreditField {
                        required property var modelData

                        visible: creditsGroup.open
                        line: modelData.line
                        name: modelData.name
                    }
                }

                // The notes: several lines.
                T.TextArea {
                    id: notes

                    Layout.fillWidth: true
                    visible: creditsGroup.open
                    implicitHeight: Math.max(64, contentHeight + 16)
                    padding: 8
                    leftPadding: 9
                    rightPadding: 9
                    placeholderText: qsTr("Notes")
                    text: root.document.credits[8]
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    hoverEnabled: true
                    font.pixelSize: Theme.fontBody
                    color: Theme.ink
                    selectionColor: Qt.alpha(Theme.accent, 0.4)
                    selectedTextColor: Theme.ink
                    placeholderTextColor: Theme.inkSoft
                    Accessible.name: qsTr("Notes")
                    onEditingFinished: root.document.setCredit(8, text)

                    Text {
                        x: notes.leftPadding
                        y: notes.topPadding
                        visible: notes.length === 0 && notes.preeditText.length === 0
                        text: notes.placeholderText
                        font: notes.font
                        color: notes.placeholderTextColor
                    }

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surface
                        border.width: notes.activeFocus ? 2 : 1
                        border.color: notes.activeFocus ? Theme.accentInk
                                                        : notes.hovered ? Theme.faint : Theme.line
                    }
                }

                GroupHeading {
                    id: measuresGroup

                    text: qsTr("Measures")
                }

                SignatureFields {
                    Layout.fillWidth: true
                    visible: measuresGroup.open
                    labelWidth: root.labelWidth
                    numerator: root.document.timeNumerator
                    denominator: root.document.timeDenominator
                    fifths: root.document.keyFifths
                    minor: root.document.keyMinor
                    onTimeChosen: (numerator, denominator) => root.document.setTimeSignature(
                                      numerator, denominator)
                    onKeyChosen: (fifths, minor) => root.document.setKeySignature(fifths, minor)
                }

                Label {
                    Layout.fillWidth: true
                    visible: measuresGroup.open
                    text: qsTr("From the first measure until the next change.")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.inkSoft
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: measuresGroup.open

                    FieldLabel {
                        text: qsTr("Length")
                    }

                    Stepper {
                        Layout.fillWidth: true
                        from: 1
                        to: 9999
                        value: root.document.measureCount
                        textFromValue: (value, locale) => qsTr("%n measure(s)", "", value)
                        valueFromText: (text, locale) => parseInt(text.replace(/[^0-9]/g, ""))
                        onValueModified: root.document.setMeasureCount(value)
                    }
                }

                GroupHeading {
                    folds: false
                    text: qsTr("Tracks")
                }

                Repeater {
                    // A model rather than the list itself: a mute or an added
                    // track changes its rows instead of making every row again.
                    model: root.document.trackList

                    delegate: Rectangle {
                        id: row

                        objectName: "trackRow"

                        required property int index
                        required property var modelData
                        readonly property bool shown: root.document.track === index
                        readonly property bool tuningOpen: root.tuningTrack === index
                                                           && modelData.fretted === true
                        /// The track's hue, the same wherever it shows.
                        readonly property color hue: Theme.trackHue(index)

                        Layout.fillWidth: true
                        Layout.preferredHeight: 76 + (tuningOpen ? tuning.implicitHeight + 14 : 0)
                        radius: Theme.radiusCard
                        clip: true
                        // A card of its own; the track shown wears its hue.
                        border.width: 1
                        border.color: shown ? Qt.alpha(hue, 0.6) : Theme.line
                        color: shown ? Qt.alpha(hue, Theme.dark ? 0.14 : 0.09)
                                     : hover.hovered ? Theme.hover : "transparent"
                        // The tuning stays in sight as it opens.
                        onHeightChanged: {
                            if (tuningOpen) {
                                root.reveal(row);
                            }
                        }

                        Behavior on Layout.preferredHeight {
                            NumberAnimation {
                                duration: Theme.calm
                                easing.type: Easing.OutCubic
                            }
                        }

                        HoverHandler {
                            id: hover
                        }

                        TapHandler {
                            onTapped: root.document.track = row.index
                        }

                        RowLayout {
                            id: header

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 44
                            anchors.leftMargin: 8
                            anchors.rightMargin: 4
                            spacing: 9
                            // A track that mute or solo keeps quiet fades.
                            opacity: row.modelData.audible ? 1.0 : 0.45

                            Rectangle {
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28
                                radius: Theme.radiusSmall
                                color: Qt.alpha(row.hue, Theme.dark ? 0.24 : 0.16)

                                MusicSymbol {
                                    anchors.centerIn: parent
                                    width: 22
                                    height: 22
                                    symbol: row.modelData.symbol
                                    size: 18
                                    color: row.hue
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.name
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    color: Theme.ink
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.detail
                                    font.pixelSize: Theme.fontCaption
                                    color: Theme.inkSoft
                                    elide: Text.ElideRight
                                }
                            }

                            // Strings and a capo bar: the tuning under the row.
                            IconButton {
                                implicitWidth: 28
                                implicitHeight: 28
                                padding: 5
                                visible: row.modelData.fretted === true
                                opacity: hover.hovered || row.shown || row.tuningOpen ? 1.0 : 0.0
                                checked: row.tuningOpen
                                iconPath: Icons.tuning
                                tip: qsTr("Tuning and capo")
                                onClicked: root.tuningTrack = row.tuningOpen ? -1 : row.index
                            }

                            IconButton {
                                implicitWidth: 28
                                implicitHeight: 28
                                padding: 5
                                visible: root.document.tracks.length > 1
                                opacity: hover.hovered || row.shown ? 1.0 : 0.0
                                iconPath: Icons.trash
                                tip: qsTr("Remove track")
                                onClicked: root.document.removeTrack(row.index)
                            }
                        }

                        // The track's mix (ADR-0039): mute, solo, volume and
                        // pan. Playback hears a fader while it moves; letting go
                        // keeps it as one undo step.
                        RowLayout {
                            id: mixRow

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: header.bottom
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            height: 24
                            spacing: 5

                            MixToggle {
                                text: qsTr("M", "mute")
                                tip: qsTr("Mute")
                                checked: row.modelData.mute
                                tint: Theme.danger
                                onClicked: root.document.setTrackMix(row.index, volume.value, pan.value,
                                                                     !row.modelData.mute, row.modelData.solo)
                            }
                            MixToggle {
                                text: qsTr("S", "solo")
                                tip: qsTr("Solo")
                                checked: row.modelData.solo
                                tint: Theme.solo
                                onClicked: root.document.setTrackMix(row.index, volume.value, pan.value,
                                                                     row.modelData.mute, !row.modelData.solo)
                            }
                            Fader {
                                id: volume

                                Layout.fillWidth: true
                                Layout.leftMargin: 4
                                focusPolicy: Qt.NoFocus
                                tint: row.hue
                                from: 0
                                to: 1
                                value: row.modelData.volume
                                tip: qsTr("Volume %1 %").arg(Math.round(value * 100))
                                onMoved: root.document.previewTrackMix(row.index, value, pan.value)
                                onReleased: root.document.setTrackMix(row.index, value, pan.value,
                                                                      row.modelData.mute, row.modelData.solo)
                            }
                            Fader {
                                id: pan

                                Layout.preferredWidth: 68
                                focusPolicy: Qt.NoFocus
                                tint: row.hue
                                centred: true
                                from: -1
                                to: 1
                                value: row.modelData.pan
                                tip: value < -0.005 ? qsTr("Pan %1 % left").arg(Math.round(-value * 100))
                                                    : value > 0.005 ? qsTr("Pan %1 % right").arg(Math.round(value * 100))
                                                                    : qsTr("Pan centre")
                                onMoved: root.document.previewTrackMix(row.index, volume.value, value)
                                onReleased: root.document.setTrackMix(row.index, volume.value, value,
                                                                      row.modelData.mute, row.modelData.solo)
                            }
                        }

                        // Made only while the tuning shows or closes: a song with many
                        // tracks would otherwise build every row's fields again
                        // whenever a track is added or removed.
                        Loader {
                            id: tuning

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: mixRow.bottom
                            anchors.topMargin: 10
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            active: row.tuningOpen || row.height > 77
                            opacity: row.tuningOpen ? 1.0 : 0.0

                            sourceComponent: TuningFields {
                                document: root.document
                                track: row.index
                                info: row.modelData
                            }

                            Behavior on opacity {
                                NumberAnimation {
                                    duration: Theme.calm
                                }
                            }
                        }
                    }
                }

                TextButton {
                    id: addButton

                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    iconPath: Icons.plus
                    text: qsTr("Add track…")
                    onClicked: addMenu.popup(addButton, 0, addButton.height + 4)

                    AppMenu {
                        id: addMenu

                        AppMenu {
                            id: guitarMenu

                            title: qsTr("Guitars")

                            Instantiator {
                                model: root.document.instrumentPresets.filter(p => p.group === "guitar")
                                delegate: AppMenuItem {
                                    required property var modelData

                                    text: modelData.name
                                    onTriggered: root.document.addTrack(modelData.id)
                                }
                                onObjectAdded: (index, object) => guitarMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => guitarMenu.removeItem(object)
                            }
                        }

                        AppMenu {
                            id: bassMenu

                            title: qsTr("Basses")

                            Instantiator {
                                model: root.document.instrumentPresets.filter(p => p.group === "bass")
                                delegate: AppMenuItem {
                                    required property var modelData

                                    text: modelData.name
                                    onTriggered: root.document.addTrack(modelData.id)
                                }
                                onObjectAdded: (index, object) => bassMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => bassMenu.removeItem(object)
                            }
                        }

                        AppMenuSeparator {}

                        Instantiator {
                            model: root.document.instrumentPresets.filter(p => p.group === "other")
                            delegate: AppMenuItem {
                                required property var modelData

                                text: modelData.name
                                onTriggered: root.document.addTrack(modelData.id)
                            }
                            // After the two submenus and the separator.
                            onObjectAdded: (index, object) => addMenu.insertItem(index + 3, object)
                            onObjectRemoved: (index, object) => addMenu.removeItem(object)
                        }
                    }
                }

                Item {
                    Layout.preferredHeight: 8
                }
            }
        }
    }

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.line
    }
}
