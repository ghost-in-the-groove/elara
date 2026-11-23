// SPDX-License-Identifier: AGPL-3.0-or-later
// The song panel, docked at the right of the score: what the whole song is
// (title, artist, tempo, signatures, length) and its tracks. Every change is
// one edit of the document, so Ctrl+Z takes it back.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Elara.App

Pane {
    id: root

    required property ScoreDocument document
    property bool dark: false

    signal closeRequested

    readonly property color cardColor: dark ? "#24242c" : "#ffffff"
    readonly property color lineColor: dark ? "#34343e" : "#dfe2e8"

    padding: 0
    background: Rectangle {
        color: root.cardColor

        Rectangle {
            width: 1
            height: parent.height
            color: root.lineColor
        }
    }

    component SectionLabel: Label {
        Layout.fillWidth: true
        Layout.topMargin: 10
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
        opacity: 0.55
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
                                  : toggle.hovered ? Qt.alpha(Material.foreground, 0.12)
                                                   : Qt.alpha(Material.foreground, 0.06)

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
            font.pixelSize: 11
            font.weight: Font.Bold
            color: toggle.checked ? "white" : Material.foreground
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
            color: Qt.alpha(Material.foreground, 0.35)
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
            Layout.bottomMargin: 4

            Label {
                Layout.fillWidth: true
                text: qsTr("Song and tracks")
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            IconButton {
                iconPath: "M7 7 L17 17 M17 7 L7 17"
                tip: qsTr("Close (Ctrl+I)")
                onClicked: root.closeRequested()
            }
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

                SectionLabel {
                    Layout.topMargin: 2
                    text: qsTr("Song")
                }

                TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Title")
                    text: root.document.songTitle
                    selectByMouse: true
                    onEditingFinished: root.document.setSongTitle(text)
                }

                TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Artist")
                    text: root.document.artist
                    selectByMouse: true
                    onEditingFinished: root.document.setArtist(text)
                }

                RowLayout {
                    Layout.fillWidth: true

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

                SectionLabel {
                    text: qsTr("Measures")
                }

                SignatureFields {
                    Layout.fillWidth: true
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
                    text: qsTr("From the first measure until the next change.")
                    font.pixelSize: 12
                    opacity: 0.5
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true

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
                    model: root.document.tracks

                    delegate: Rectangle {
                        id: row

                        required property int index
                        required property var modelData
                        readonly property bool shown: root.document.track === index

                        Layout.fillWidth: true
                        Layout.preferredHeight: 94
                        radius: 12
                        color: shown ? Qt.alpha(Material.accent, root.dark ? 0.22 : 0.12)
                                     : hover.hovered ? Qt.alpha(Material.foreground, 0.06)
                                                     : "transparent"

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
                            height: 56
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
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                radius: 10
                                color: Qt.alpha(Material.accent, root.dark ? 0.25 : 0.14)

                                MusicSymbol {
                                    anchors.centerIn: parent
                                    width: 30
                                    height: 30
                                    symbol: row.modelData.symbol
                                    size: 24
                                    color: Material.accent
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
                                    font.pixelSize: 12
                                    opacity: 0.6
                                    elide: Text.ElideRight
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
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: header.bottom
                            anchors.leftMargin: 8
                            anchors.rightMargin: 10
                            height: 34
                            spacing: 6

                            MixToggle {
                                text: qsTr("M", "mute")
                                tip: qsTr("Mute")
                                checked: row.modelData.mute
                                tint: "#e5484d"
                                onClicked: root.document.setTrackMix(row.index, volume.value, pan.value,
                                                                     !row.modelData.mute, row.modelData.solo)
                            }
                            MixToggle {
                                text: qsTr("S", "solo")
                                tip: qsTr("Solo")
                                checked: row.modelData.solo
                                tint: "#f5a524"
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
