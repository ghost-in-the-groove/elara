// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 960
    height: 600
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    title: "Elara"

    AudioController {
        id: audio
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "Elara"
            font.pixelSize: 56
            font.weight: Font.DemiBold
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Tablature, score and music studio")
            opacity: 0.7
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 24
            text: audio.playing ? qsTr("Playing…") : qsTr("Play test tone (A4, 440 Hz)")
            enabled: !audio.playing
            onClicked: audio.playTestTone(440, 1.5)
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            visible: text.length > 0
            text: audio.errorMessage.length > 0 ? audio.errorMessage : audio.deviceDescription
            color: audio.errorMessage.length > 0 ? "#d9534f" : palette.windowText
            opacity: audio.errorMessage.length > 0 ? 1.0 : 0.6
        }
    }

    footer: Label {
        padding: 8
        horizontalAlignment: Text.AlignRight
        text: qsTr("Milestone 0 · version %1").arg(Qt.application.version)
        opacity: 0.5
    }
}
