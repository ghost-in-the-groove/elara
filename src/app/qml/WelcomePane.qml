// SPDX-License-Identifier: AGPL-3.0-or-later
// What the window shows before a score is open: what to do at the left (open
// a score, start a song), the scores opened last beside it. In a narrow window
// the two stand one over the other.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

Item {
    id: root

    /// The scores opened last (RecentScores.scores), the latest first.
    property var recentScores: []

    signal openRequested
    signal newRequested
    signal recentRequested(url file)
    signal forgetRequested(url file)

    /// Side by side when there is room for both columns.
    readonly property bool wide: width >= 760
    readonly property real startWidth: 300
    readonly property real recentWidth: Math.max(280, Math.min(440, width - startWidth - 150))

    // Centred when it fits; a short window scrolls it instead of cutting it.
    Flickable {
        id: flick

        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, home.implicitHeight + 64)
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ScrollBar.vertical: Scroll {}

        GridLayout {
            id: home

            x: Math.round((flick.width - width) / 2)
            y: Math.round(Math.max(32, (flick.height - implicitHeight) / 2))
            width: root.wide ? root.startWidth + 57 + root.recentWidth : Math.min(420, flick.width - 48)
            columns: root.wide ? 3 : 1
            columnSpacing: 28
            rowSpacing: 28

            // What to do.
            ColumnLayout {
                Layout.preferredWidth: root.wide ? root.startWidth : -1
                Layout.fillWidth: !root.wide
                Layout.alignment: Qt.AlignTop
                spacing: 8

                // The logo, with the frets on its strings.
                Logo {
                    Layout.preferredHeight: 92
                    Layout.preferredWidth: 92 * aspect
                    Layout.leftMargin: -4
                    color: Theme.ink
                    stringColor: Theme.accentInk
                    ground: Theme.desk
                }

                Label {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 14
                    text: qsTr("Tablature and score editor")
                    font.pixelSize: Theme.fontTitle
                    color: Theme.inkSoft
                    wrapMode: Text.Wrap
                }

                TextButton {
                    Layout.fillWidth: true
                    implicitHeight: 36
                    kind: "main"
                    iconPath: Icons.open
                    text: qsTr("Open a score…")
                    onClicked: root.openRequested()
                }

                TextButton {
                    Layout.fillWidth: true
                    implicitHeight: 36
                    iconPath: Icons.newSong
                    text: qsTr("New song")
                    onClicked: root.newRequested()
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    text: qsTr("or drop a file anywhere in this window")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.inkSoft
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Guitar Pro 3 to 8 · MusicXML · MIDI · Elara")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.inkSoft
                    wrapMode: Text.Wrap
                }
            }

            Rectangle {
                visible: root.wide
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: Theme.line
            }

            // The scores opened last, to pick up where the work was left.
            ColumnLayout {
                Layout.preferredWidth: root.wide ? root.recentWidth : -1
                Layout.fillWidth: !root.wide
                Layout.alignment: Qt.AlignTop
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 6
                    leftPadding: 8
                    text: qsTr("Recent")
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    color: Theme.inkSoft
                }

                // Before any score was opened: what the list is for.
                Label {
                    Layout.fillWidth: true
                    visible: recentList.count === 0
                    leftPadding: 8
                    rightPadding: 8
                    text: qsTr("The scores you open show here, the latest first, to pick up where you left them.")
                    font.pixelSize: Theme.fontBody
                    color: Theme.inkSoft
                    wrapMode: Text.Wrap
                }

                Repeater {
                    id: recentList

                    model: root.recentScores.slice(0, 8)

                    delegate: Item {
                        id: row

                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        implicitHeight: 46
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Open %1").arg(modelData.name)

                        HoverHandler {
                            id: hover

                            cursorShape: Qt.PointingHandCursor
                        }

                        TapHandler {
                            onTapped: root.recentRequested(row.modelData.url)
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSmall
                            color: hover.hovered ? Theme.hover : "transparent"
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 4
                            spacing: 11

                            // The file's format, as a badge.
                            Rectangle {
                                Layout.preferredWidth: 46
                                Layout.preferredHeight: 26
                                radius: Theme.radiusSmall
                                color: Theme.accentWash

                                Label {
                                    anchors.centerIn: parent
                                    width: parent.width - 6
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    text: row.modelData.format
                                    color: Theme.accentInk
                                    font.pixelSize: Theme.fontCaption
                                    font.weight: Font.DemiBold
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.name
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.fontBody
                                    color: Theme.ink
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.folder
                                    elide: Text.ElideMiddle
                                    font.pixelSize: Theme.fontCaption
                                    color: Theme.inkSoft
                                }
                            }

                            IconButton {
                                implicitWidth: 28
                                implicitHeight: 28
                                padding: 6
                                iconPath: Icons.close
                                tip: qsTr("Remove from the list")
                                opacity: hover.hovered || hovered || visualFocus ? 1.0 : 0.0
                                onClicked: root.forgetRequested(row.modelData.url)
                            }
                        }
                    }
                }
            }
        }
    }
}
