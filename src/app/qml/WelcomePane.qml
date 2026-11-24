// SPDX-License-Identifier: AGPL-3.0-or-later
// What the window shows before a score is open.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

Item {
    id: root

    signal openRequested
    signal newRequested

    ColumnLayout {
        id: column

        anchors.centerIn: parent
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
            Layout.preferredHeight: 170
            Layout.preferredWidth: 170 * aspect
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
    }
}
