// SPDX-License-Identifier: AGPL-3.0-or-later
// The command bar across the top of the window, dark in both themes: the mark
// and the menus, the file tools, the transport with its readings and the
// practice controls, and the panels. Its lower edge is the song string. In a
// narrow window the file tools go first, then the readings one by one.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

Rectangle {
    id: root

    required property ScoreDocument document
    required property AudioController playback
    /// Whether a score is open.
    property bool hasScore: false
    /// The menus: an AppMenuBar, placed beside the mark.
    property alias menus: menuSlot.data
    /// Whether the song and tracks panel shows.
    property bool songPaneShown: false

    signal openRequested
    signal saveRequested
    signal songPaneToggled
    signal tunerRequested

    /// The measure playing, from 1, or the caret's while nothing plays.
    readonly property int playingMeasure: {
        const played = hasScore ? document.measureAt(playback.position) : -1;
        return played >= 0 ? played + 1 : Math.max(1, document.caretMeasure + 1);
    }
    /// The tempo written there, at the speed it plays.
    readonly property int playingTempo: {
        if (!hasScore)
            return 0;
        const written = document.tempoAt(playingMeasure - 1).bpm;
        return Math.round((written > 0 ? written : document.tempo) * document.speed / 100);
    }

    /// "m:ss" for a number of seconds.
    function clock(seconds: real): string {
        const whole = Math.max(0, Math.floor(seconds));
        const rest = whole % 60;
        return Math.floor(whole / 60) + ":" + (rest < 10 ? "0" : "") + rest;
    }

    // What always shows, and what goes when the window is too narrow for it.
    readonly property real needed: 10 + mark.width + 8 + menuSlot.implicitWidth + 24
                                   + (hasScore ? transportCore.implicitWidth + 12 + practice.implicitWidth
                                                 + 24 : 0)
                                   + panels.implicitWidth + 8
    readonly property real spare: width - needed
    readonly property real wellEdge: 2
    readonly property bool showMeasure: spare >= measureCell.implicitWidth + wellEdge + 10
    readonly property bool showTime: spare >= measureCell.implicitWidth + timeCell.implicitWidth
                                              + wellEdge + 11
    readonly property bool showTempo: spare >= measureCell.implicitWidth + timeCell.implicitWidth
                                               + tempoCell.implicitWidth + wellEdge + 12
    readonly property bool showFiles: spare >= measureCell.implicitWidth + timeCell.implicitWidth
                                               + tempoCell.implicitWidth + wellEdge + 12
                                               + files.implicitWidth + 12

    implicitHeight: 48
    color: Theme.night

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 8
        spacing: 0

        Logo {
            id: mark

            Layout.preferredHeight: 26
            Layout.preferredWidth: 26 * aspect
            Layout.rightMargin: 8
            detailed: false
            named: false
            color: Theme.pearl
            stringColor: Theme.lilac
            ground: Theme.night
        }

        Item {
            id: menuSlot

            objectName: "menus"
            implicitWidth: children.length > 0 ? children[0].implicitWidth : 0
            implicitHeight: children.length > 0 ? children[0].implicitHeight : 0
        }

        Row {
            id: files

            objectName: "fileTools"
            Layout.leftMargin: 12
            visible: root.showFiles
            spacing: 2

            IconButton {
                nightBar: true
                iconPath: Icons.open
                tip: qsTr("Open a score (Ctrl+O)")
                onClicked: root.openRequested()
            }
            IconButton {
                nightBar: true
                visible: root.hasScore
                enabled: root.document.modified || !root.document.savesInPlace
                iconPath: Icons.save
                tip: qsTr("Save (Ctrl+S)")
                onClicked: root.saveRequested()
            }
            IconButton {
                nightBar: true
                visible: root.hasScore
                enabled: root.document.canUndo
                iconPath: Icons.undo
                tip: root.document.canUndo ? qsTr("Undo %1 (Ctrl+Z)").arg(root.document.undoName)
                                           : qsTr("Undo (Ctrl+Z)")
                onClicked: root.document.undo()
            }
            IconButton {
                nightBar: true
                visible: root.hasScore
                enabled: root.document.canRedo
                iconPath: Icons.redo
                tip: root.document.canRedo ? qsTr("Redo %1 (Ctrl+Y)").arg(root.document.redoName)
                                           : qsTr("Redo (Ctrl+Y)")
                onClicked: root.document.redo()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 12
        }

        // The transport: to the start, play, and where the song is.
        Row {
            id: transport

            objectName: "transport"
            visible: root.hasScore
            spacing: 12

            Row {
                id: transportCore

                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    nightBar: true
                    iconPath: Icons.toStart
                    tip: qsTr("Back to the start (Home)")
                    enabled: root.playback.playingSong || root.playback.position > 0
                    onClicked: root.playback.stopPlayback()
                }

                // The one filled control of the window, and the one round one.
                AbstractButton {
                    id: playButton

                    objectName: "playButton"
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: 36
                    implicitHeight: 36
                    hoverEnabled: true
                    focusPolicy: Qt.TabFocus
                    Accessible.name: root.playback.playingSong ? qsTr("Pause") : qsTr("Play")
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: (root.playback.playingSong ? qsTr("Pause") : qsTr("Play"))
                                  + qsTr(" (Space)")
                    onClicked: root.playback.togglePlayback()

                    background: Rectangle {
                        radius: width / 2
                        color: playButton.down ? Qt.darker(Theme.accent, 1.18)
                                               : playButton.hovered ? Qt.lighter(Theme.accent, 1.14)
                                                                    : Theme.accent
                        border.width: playButton.visualFocus ? 2 : 0
                        border.color: Theme.pearl
                    }

                    contentItem: Item {
                        Icon {
                            anchors.centerIn: parent
                            // The triangle looks centred a little to the right.
                            anchors.horizontalCenterOffset: root.playback.playingSong ? 0 : 1
                            color: Theme.accentText
                            filled: true
                            lineWidth: 1.4
                            path: root.playback.playingSong ? Icons.pause : Icons.play
                        }
                    }
                }
            }

            // The readings, each in its cell.
            Rectangle {
                id: well

                anchors.verticalCenter: parent.verticalCenter
                visible: root.showMeasure
                width: cells.implicitWidth + root.wellEdge
                height: Theme.control
                radius: Theme.radiusSmall
                color: Theme.nightWell
                border.color: Theme.nightLine

                Row {
                    id: cells

                    anchors.centerIn: parent

                    Readout {
                        id: measureCell

                        objectName: "measureReadout"
                        iconPath: Icons.measure
                        value: root.playingMeasure
                        rest: " / " + root.document.measureCount
                        widest: "888 / 888"
                        tip: qsTr("Measure")
                    }
                    Rectangle {
                        visible: timeCell.visible
                        width: 1
                        height: Theme.control - 12
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.nightLine
                    }
                    Readout {
                        id: timeCell

                        objectName: "timeReadout"
                        visible: root.showTime
                        value: root.clock(root.playback.position)
                        rest: " / " + root.clock(root.document.duration)
                        widest: "88:88 / 88:88"
                        tip: qsTr("Time")
                    }
                    Rectangle {
                        visible: tempoCell.visible
                        width: 1
                        height: Theme.control - 12
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.nightLine
                    }
                    Readout {
                        id: tempoCell

                        objectName: "tempoReadout"
                        visible: root.showTempo
                        symbol: ""
                        value: "= " + root.playingTempo
                        widest: "= 888"
                        tip: root.document.speed === 100
                             ? qsTr("Tempo")
                             : qsTr("Tempo at %1 % of the speed written").arg(root.document.speed)
                    }
                }
            }

            PracticeControls {
                id: practice

                anchors.verticalCenter: parent.verticalCenter
                document: root.document
                playback: root.playback
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 12
        }

        Row {
            id: panels

            objectName: "panelTools"
            spacing: 2

            IconButton {
                nightBar: true
                visible: root.hasScore
                iconPath: Icons.panelRight
                tip: qsTr("Song and tracks (Ctrl+I)")
                // Bound, not checkable: the panel also closes by itself.
                checked: root.songPaneShown
                onClicked: root.songPaneToggled()
            }
            IconButton {
                nightBar: true
                iconPath: Icons.tuner
                tip: qsTr("Tuner (Ctrl+Shift+T)")
                onClicked: root.tunerRequested()
            }
        }
    }
}
