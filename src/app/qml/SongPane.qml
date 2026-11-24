// SPDX-License-Identifier: AGPL-3.0-or-later
// The song panel, docked at the right of the score: what the whole song is
// (title, artist, credits, tempo, signatures, length) and its tracks. Every change is
// one edit of the document, so Ctrl+Z takes it back.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

Pane {
    id: root

    required property ScoreDocument document

    signal closeRequested

    readonly property color cardColor: Theme.surface
    readonly property color lineColor: Theme.line

    padding: 0
    background: Rectangle {
        color: root.cardColor

        Rectangle {
            width: 1
            height: parent.height
            color: root.lineColor
        }
    }

    /// The track whose tuning shows under its row, -1 for none.
    property int tuningTrack: -1

    /// Shows the tuning of `track` under its row.
    function openTuning(track: int) {
        tuningTrack = track;
    }

    /// Scrolls the panel so that all of `item` shows.
    function reveal(item: Item) {
        const flick = scroll.contentItem;
        const top = item.mapToItem(flick.contentItem, 0, 0).y;
        const bottom = top + item.height + 12;
        if (bottom > flick.contentY + flick.height) {
            flick.contentY = Math.min(top - 12, bottom - flick.height);
        } else if (top < flick.contentY) {
            flick.contentY = Math.max(0, top - 12);
        }
    }

    component SectionLabel: Label {
        Layout.fillWidth: true
        Layout.topMargin: 10
        font.pixelSize: Theme.fontCaption
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1.0
        opacity: 0.55
    }

    // The heading of a section that folds: a click hides or shows its fields.
    component SectionHeader: AbstractButton {
        id: heading

        property bool open: true

        Layout.fillWidth: true
        Layout.topMargin: 10
        implicitHeight: 24
        focusPolicy: Qt.TabFocus
        hoverEnabled: true
        Accessible.name: text
        onClicked: open = !open

        background: Rectangle {
            radius: 4
            color: "transparent"
            border.width: heading.visualFocus ? 2 : 0
            border.color: Theme.accentInk
        }

        contentItem: RowLayout {
            spacing: 4
            opacity: heading.hovered ? 0.9 : 0.55

            Label {
                Layout.fillWidth: true
                text: heading.text
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1.0
                verticalAlignment: Text.AlignVCenter
            }
            Icon {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                path: "M7 10 L12 15 L17 10"
                color: Theme.ink
                rotation: heading.open ? 0 : -90

                Behavior on rotation {
                    NumberAnimation {
                        duration: 140
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }

    // Mute or solo: a round letter, lit in its colour when on.
    component MixToggle: AbstractButton {
        id: toggle

        property string tip
        property color tint

        implicitWidth: 26
        implicitHeight: 26
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        Accessible.role: Accessible.CheckBox
        Accessible.name: tip
        Accessible.checked: checked
        ToolTip.visible: hovered
        ToolTip.delay: 500
        ToolTip.text: tip

        background: Rectangle {
            radius: height / 2
            color: toggle.checked ? toggle.tint
                                  : toggle.hovered ? Theme.pressed : Theme.hover

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }
        }
        contentItem: Label {
            text: toggle.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: Theme.fontCaption
            font.weight: Font.Bold
            color: toggle.checked ? "white" : Theme.ink
        }
    }

    // A fader of the mixer: compact, with its value in a tip while it moves.
    // released() follows the drag, and also a click on the groove.
    component MixSlider: Slider {
        id: slider

        property string tip
        /// A pan: a tick marks the centre of the groove.
        property bool centred: false

        signal released

        Layout.preferredHeight: 30
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        onPressedChanged: {
            if (!pressed)
                released();
        }
        Accessible.name: tip
        ToolTip.visible: pressed || hovered
        ToolTip.delay: pressed ? 0 : 500
        ToolTip.text: tip

        Rectangle {
            visible: slider.centred
            x: slider.leftPadding + (slider.availableWidth - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: 10
            radius: 1
            color: Theme.faint
        }
    }

    component FieldLabel: Label {
        Layout.preferredWidth: 104
        opacity: 0.75
        elide: Text.ElideRight
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 10
            Layout.topMargin: 10
            Layout.bottomMargin: 10

            Label {
                Layout.fillWidth: true
                text: qsTr("Song and tracks")
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            IconButton {
                iconPath: "M7 7 L17 17 M17 7 L7 17"
                tip: qsTr("Close (Ctrl+I)")
                onClicked: root.closeRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.lineColor
        }

        ScrollView {
            id: scroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: scroll.availableWidth - 40
                x: 20
                spacing: 8

                SectionHeader {
                    id: songSection

                    Layout.topMargin: 14
                    text: qsTr("Song")
                }

                TextField {
                    Layout.fillWidth: true
                    visible: songSection.open
                    placeholderText: qsTr("Title")
                    text: root.document.songTitle
                    selectByMouse: true
                    onEditingFinished: root.document.setSongTitle(text)
                }

                TextField {
                    Layout.fillWidth: true
                    visible: songSection.open
                    placeholderText: qsTr("Artist")
                    text: root.document.artist
                    selectByMouse: true
                    onEditingFinished: root.document.setArtist(text)
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: songSection.open

                    FieldLabel {
                        text: qsTr("Tempo")
                    }

                    SpinBox {
                        Layout.fillWidth: true
                        from: 10
                        to: 999
                        editable: true
                        value: root.document.tempo
                        textFromValue: (value, locale) => qsTr("♩ = %1").arg(value)
                        valueFromText: (text, locale) => parseInt(text.replace(/[^0-9]/g, ""))
                        onValueModified: root.document.setTempo(value)
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Quarter notes per minute")
                    }
                }

                SectionHeader {
                    id: creditsSection

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

                    delegate: TextField {
                        required property var modelData

                        Layout.fillWidth: true
                        visible: creditsSection.open
                        placeholderText: modelData.name
                        text: root.document.credits[modelData.line]
                        selectByMouse: true
                        Accessible.name: modelData.name
                        onEditingFinished: root.document.setCredit(modelData.line, text)
                    }
                }

                TextArea {
                    Layout.fillWidth: true
                    visible: creditsSection.open
                    placeholderText: qsTr("Notes")
                    text: root.document.credits[8]
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    Accessible.name: qsTr("Notes")
                    onEditingFinished: root.document.setCredit(8, text)
                }

                SectionHeader {
                    id: measuresSection

                    text: qsTr("Measures")
                }

                SignatureFields {
                    Layout.fillWidth: true
                    visible: measuresSection.open
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
                    visible: measuresSection.open
                    text: qsTr("From the first measure until the next change.")
                    font.pixelSize: 12
                    opacity: 0.5
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: measuresSection.open

                    FieldLabel {
                        text: qsTr("Length")
                    }

                    SpinBox {
                        Layout.fillWidth: true
                        from: 1
                        to: 9999
                        editable: true
                        value: root.document.measureCount
                        textFromValue: (value, locale) => qsTr("%n measure(s)", "", value)
                        valueFromText: (text, locale) => parseInt(text.replace(/[^0-9]/g, ""))
                        onValueModified: root.document.setMeasureCount(value)
                    }
                }

                SectionLabel {
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

                        Layout.fillWidth: true
                        Layout.preferredHeight: 80 + (tuningOpen ? tuning.implicitHeight + 14 : 0)
                        radius: Theme.radiusCard
                        clip: true
                        // A card of its own; the track shown wears the accent.
                        border.width: 1
                        border.color: shown ? Qt.alpha(Theme.accentInk, 0.45) : root.lineColor
                        // The tuning stays in sight as it opens.
                        onHeightChanged: {
                            if (tuningOpen) {
                                root.reveal(row);
                            }
                        }

                        Behavior on Layout.preferredHeight {
                            NumberAnimation {
                                duration: 200
                                easing.type: Easing.OutCubic
                            }
                        }
                        color: shown ? Theme.accentWash : hover.hovered ? Theme.hover : "transparent"

                        Behavior on color {
                            ColorAnimation {
                                duration: 140
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
                            height: 46
                            anchors.leftMargin: 8
                            anchors.rightMargin: 4
                            spacing: 10
                            // A track that mute or solo keeps quiet fades.
                            opacity: row.modelData.audible ? 1.0 : 0.45

                            Behavior on opacity {
                                NumberAnimation {
                                    duration: 140
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                radius: Theme.radiusSmall
                                color: Theme.accentWash

                                MusicSymbol {
                                    anchors.centerIn: parent
                                    width: 24
                                    height: 24
                                    symbol: row.modelData.symbol
                                    size: 20
                                    color: Theme.accentInk
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.name
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.detail
                                    font.pixelSize: Theme.fontSmall
                                    opacity: 0.6
                                    elide: Text.ElideRight
                                }
                            }

                            // Strings and a capo bar: the tuning under the row.
                            IconButton {
                                visible: row.modelData.fretted === true
                                opacity: hover.hovered || row.shown || row.tuningOpen ? 1.0 : 0.0
                                checked: row.tuningOpen
                                iconPath: "M7 4 L7 20 M12 4 L12 20 M17 4 L17 20 M4.5 9 L19.5 9"
                                tip: qsTr("Tuning and capo")
                                onClicked: root.tuningTrack = row.tuningOpen ? -1 : row.index

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 120
                                    }
                                }
                            }

                            IconButton {
                                visible: root.document.tracks.length > 1
                                opacity: hover.hovered || row.shown ? 1.0 : 0.0
                                iconPath: "M5 7 L19 7 M10 7 L10 4.5 L14 4.5 L14 7 M7 7 L8 20 L16 20 L17 7"
                                tip: qsTr("Remove track")
                                onClicked: root.document.removeTrack(row.index)

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 120
                                    }
                                }
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
                            anchors.rightMargin: 10
                            height: 30
                            spacing: 6

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
                            MixSlider {
                                id: volume

                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                value: row.modelData.volume
                                tip: qsTr("Volume %1 %").arg(Math.round(value * 100))
                                onMoved: root.document.previewTrackMix(row.index, value, pan.value)
                                onReleased: root.document.setTrackMix(row.index, value, pan.value,
                                                                      row.modelData.mute, row.modelData.solo)
                            }
                            MixSlider {
                                id: pan

                                Layout.preferredWidth: 76
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
                            anchors.topMargin: 8
                            anchors.leftMargin: 8
                            anchors.rightMargin: 10
                            active: row.tuningOpen || row.height > 81
                            opacity: row.tuningOpen ? 1.0 : 0.0

                            sourceComponent: TuningFields {
                                document: root.document
                                track: row.index
                                info: row.modelData
                            }

                            Behavior on opacity {
                                NumberAnimation {
                                    duration: 160
                                }
                            }
                        }
                    }
                }

                Button {
                    id: addButton

                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    flat: true
                    text: qsTr("Add track…")
                    onClicked: addMenu.popup(addButton, 0, addButton.height)

                    Menu {
                        id: addMenu

                        Menu {
                            id: guitarMenu

                            title: qsTr("Guitars")

                            Instantiator {
                                model: root.document.instrumentPresets.filter(p => p.group === "guitar")
                                delegate: MenuItem {
                                    required property var modelData

                                    text: modelData.name
                                    onTriggered: root.document.addTrack(modelData.id)
                                }
                                onObjectAdded: (index, object) => guitarMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => guitarMenu.removeItem(object)
                            }
                        }

                        Menu {
                            id: bassMenu

                            title: qsTr("Basses")

                            Instantiator {
                                model: root.document.instrumentPresets.filter(p => p.group === "bass")
                                delegate: MenuItem {
                                    required property var modelData

                                    text: modelData.name
                                    onTriggered: root.document.addTrack(modelData.id)
                                }
                                onObjectAdded: (index, object) => bassMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => bassMenu.removeItem(object)
                            }
                        }

                        MenuSeparator {}

                        Instantiator {
                            model: root.document.instrumentPresets.filter(p => p.group === "other")
                            delegate: MenuItem {
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
                    Layout.preferredHeight: 12
                }
            }
        }
    }
}
