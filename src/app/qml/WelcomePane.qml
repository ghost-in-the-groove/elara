// SPDX-License-Identifier: AGPL-3.0-or-later
// What the window shows before a score is open.
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

    // Centred when it fits; a short window scrolls it instead of cutting it.
    Flickable {
        id: flick

        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight + 48)
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ScrollBar.vertical: ScrollBar {
            policy: flick.contentHeight > flick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: column

            x: (flick.width - width) / 2
            y: Math.max(24, (flick.height - implicitHeight) / 2)
            spacing: 10
            opacity: 0
            transform: Translate {
                id: rise

                y: 16
            }

            // One entrance animation, then the pane is static (nothing keeps
            // repainting while it is shown).
            Component.onCompleted: entrance.start()

            ParallelAnimation {
                id: entrance

                NumberAnimation {
                    target: column
                    property: "opacity"
                    to: 1
                    duration: 420
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    target: rise
                    property: "y"
                    to: 0
                    duration: 420
                    easing.type: Easing.OutCubic
                }
            }

            // The logo, with the frets on its strings.
            Logo {
                Layout.alignment: Qt.AlignHCenter
                // Smaller in a short window, to leave room for the recent scores.
                Layout.preferredHeight: root.height < 720 ? 120 : 170
                Layout.preferredWidth: Layout.preferredHeight * aspect
                Layout.bottomMargin: 6
                color: Theme.ink
                stringColor: Theme.accentInk
                ground: Theme.desk
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Tablature, score and music studio")
                font.pixelSize: 16
                opacity: 0.7
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 24
                spacing: 12

                Button {
                    text: qsTr("Open a score…")
                    highlighted: true
                    font.pixelSize: 15
                    onClicked: root.openRequested()
                }

                Button {
                    text: qsTr("New song")
                    flat: true
                    font.pixelSize: 15
                    onClicked: root.newRequested()
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 4
                text: qsTr("or drop a file anywhere in this window")
                font.pixelSize: 13
                opacity: 0.55
            }

            Flow {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 18
                Layout.preferredWidth: 420
                spacing: 8

                Repeater {
                    model: ["Guitar Pro 3–7", "MusicXML", "MIDI", "Elara"]

                    delegate: Rectangle {
                        required property string modelData

                        width: chip.implicitWidth + 24
                        height: 26
                        radius: 13
                        color: Theme.hover

                        Label {
                            id: chip

                            anchors.centerIn: parent
                            text: parent.modelData
                            font.pixelSize: 12
                            opacity: 0.75
                        }
                    }
                }
            }

            // The scores opened last, to pick up where the work was left.
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 30
                Layout.preferredWidth: 440
                visible: recentList.count > 0
                leftPadding: 10
                text: qsTr("Recent")
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.8
                opacity: 0.55
            }

            Column {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 440
                spacing: 2

                Repeater {
                    id: recentList

                    model: root.recentScores.slice(0, 6)

                    delegate: Item {
                        id: row

                        required property var modelData
                        required property int index

                        width: parent.width
                        height: 50
                        opacity: 0
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Open %1").arg(modelData.name)

                        // Each row arrives a moment after the one above, once.
                        SequentialAnimation {
                            running: true

                            PauseAnimation {
                                duration: 180 + 45 * row.index
                            }
                            NumberAnimation {
                                target: row
                                property: "opacity"
                                to: 1
                                duration: 260
                                easing.type: Easing.OutCubic
                            }
                        }

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

                            Behavior on color {
                                ColorAnimation {
                                    duration: 120
                                }
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6
                            spacing: 12

                            // The file's format, as a badge.
                            Rectangle {
                                Layout.preferredWidth: 50
                                Layout.preferredHeight: 30
                                radius: 7
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
                                spacing: 1

                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.name
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.fontBody + 1
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.folder
                                    elide: Text.ElideMiddle
                                    font.pixelSize: Theme.fontCaption
                                    opacity: 0.55
                                }
                            }

                            IconButton {
                                implicitWidth: 30
                                implicitHeight: 30
                                padding: 7
                                // A cross.
                                iconPath: "M7 7 L17 17 M17 7 L7 17"
                                tip: qsTr("Remove from the list")
                                opacity: hover.hovered || hovered || visualFocus ? 1.0 : 0.0
                                onClicked: root.forgetRequested(row.modelData.url)

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 120
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
