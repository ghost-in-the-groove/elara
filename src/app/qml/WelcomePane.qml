// SPDX-License-Identifier: AGPL-3.0-or-later
// What the window shows before a score is open.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    signal openRequested

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

        MusicSymbol {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 8
            symbol: "\uE050" // G clef
            size: 96
            color: Material.accent
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "Elara"
            font.pixelSize: 44
            font.weight: Font.DemiBold
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Tablature, score and music studio")
            font.pixelSize: 16
            opacity: 0.7
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 24
            text: qsTr("Open a score…")
            highlighted: true
            font.pixelSize: 15
            onClicked: root.openRequested()
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
                    color: "transparent"
                    border.color: Qt.alpha(Material.foreground, 0.2)

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
