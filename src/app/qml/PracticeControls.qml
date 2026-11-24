// SPDX-License-Identifier: AGPL-3.0-or-later
// Practice controls in the transport: the metronome, a measure of clicks before
// playing, the A-B loop, and the playback speed as a pill that opens a slider
// with the usual practice speeds (ADR-0024).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Elara.App

RowLayout {
    id: root

    required property ScoreDocument document
    required property AudioController playback

    spacing: 4

    IconButton {
        objectName: "metronomeButton"
        // A metronome: its body, its base and the pendulum.
        iconPath: "M8.5 20 L10.8 4 L13.2 4 L15.5 20 Z M6.5 20 L17.5 20 M12 16 L16.5 8"
        tip: qsTr("Metronome")
        checked: root.document.metronome
        onClicked: root.document.metronome = !root.document.metronome
    }

    IconButton {
        objectName: "countInButton"
        // Four clicks climbing: one, two, three, four.
        iconPath: "M5 18 L5 15.5 M9.7 18 L9.7 12.5 M14.3 18 L14.3 9.5 M19 18 L19 6.5"
        tip: qsTr("Count-in: a measure of clicks before playing")
        checked: root.playback.countIn
        onClicked: root.playback.countIn = !root.playback.countIn
    }

    AbstractButton {
        id: loopButton

        objectName: "loopButton"
        readonly property bool on: root.playback.looping || root.playback.loopMarked

        implicitHeight: 32
        implicitWidth: loopRow.implicitWidth + (on ? 20 : 12)
        focusPolicy: Qt.TabFocus
        hoverEnabled: true
        Accessible.name: root.playback.looping ? qsTr("End the A-B loop")
                                            : root.playback.loopMarked ? qsTr("Mark B of the loop")
                                                                    : qsTr("Mark A of the loop")
        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: qsTr("Loop: mark A, then B, at the playback position; again to end it (Ctrl+L)")
        onClicked: root.playback.markLoop()

        Behavior on implicitWidth {
            NumberAnimation {
                duration: 140
                easing.type: Easing.OutCubic
            }
        }

        contentItem: Item {
            implicitWidth: loopRow.implicitWidth
            implicitHeight: loopRow.implicitHeight

            Row {
                id: loopRow

                anchors.centerIn: parent
                spacing: 4

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    // Two arrows chasing each other round.
                    path: "M4.5 12 A5.5 5.5 0 0 1 10 6.5 L17.5 6.5 M15 4 L17.5 6.5 L15 9 "
                          + "M19.5 12 A5.5 5.5 0 0 1 14 17.5 L6.5 17.5 M9 15 L6.5 17.5 L9 20"
                    color: loopButton.on ? Theme.accentInk : Theme.ink
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: loopButton.on
                    text: root.playback.looping ? qsTr("A–B") : qsTr("A")
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                    color: Theme.accentInk
                }
            }
        }
        background: Rectangle {
            radius: height / 2
            color: loopButton.on ? Theme.accentWash
                                 : loopButton.down ? Theme.pressed
                                                   : loopButton.hovered ? Theme.hover : "transparent"
            border.width: loopButton.visualFocus ? 2 : 0
            border.color: Theme.accentInk

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }
        }
    }

    AbstractButton {
        id: speedButton

        objectName: "speedButton"
        readonly property bool changed: root.document.speed !== 100

        implicitHeight: 32
        implicitWidth: speedLabel.implicitWidth + 22
        focusPolicy: Qt.TabFocus
        hoverEnabled: true
        Accessible.name: qsTr("Playback speed: %1 %").arg(root.document.speed)
        ToolTip.visible: hovered && !speedPopup.visible
        ToolTip.delay: 600
        ToolTip.text: qsTr("Playback speed")
        onClicked: speedPopup.opened ? speedPopup.close() : speedPopup.open()

        contentItem: Label {
            id: speedLabel

            text: qsTr("%1 %").arg(root.document.speed)
            font.pixelSize: Theme.fontBody
            font.features: {
                "tnum": 1
            }
            color: speedButton.changed ? Theme.accentInk : Theme.ink
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: height / 2
            color: speedButton.changed ? Theme.accentWash
                                       : speedButton.down ? Theme.pressed
                                                          : speedButton.hovered ? Theme.hover
                                                                                : "transparent"
            border.width: speedButton.visualFocus ? 2 : 1
            border.color: speedButton.visualFocus ? Theme.accentInk : Theme.line

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }
        }

        Popup {
            id: speedPopup

            objectName: "speedPopup"
            // Above the transport, centred on the pill.
            x: (speedButton.width - width) / 2
            y: -height - 14
            width: 280
            padding: 16
            topPadding: 12
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

            enter: Transition {
                NumberAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 140
                }
                NumberAnimation {
                    property: "scale"
                    from: 0.96
                    to: 1
                    duration: 140
                    easing.type: Easing.OutCubic
                }
            }
            exit: Transition {
                NumberAnimation {
                    property: "opacity"
                    to: 0
                    duration: 100
                }
            }

            background: Rectangle {
                radius: Theme.radiusSheet
                color: Theme.surface
                border.color: Theme.line

                Elevation {
                    radius: parent.radius
                }
            }

            contentItem: ColumnLayout {
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Speed")
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }
                    Label {
                        // Follows the slider while it is dragged.
                        text: qsTr("%1 %").arg(Math.round(speedSlider.value))
                        font.pixelSize: Theme.fontBody
                        font.features: {
                            "tnum": 1
                        }
                        opacity: 0.8
                    }
                }

                Slider {
                    id: speedSlider

                    objectName: "speedSlider"
                    Layout.fillWidth: true
                    from: 25
                    to: 200
                    stepSize: 5
                    snapMode: Slider.SnapAlways
                    // The song is sequenced again once the slider is let go,
                    // not at every step of a drag.
                    onPressedChanged: {
                        if (!pressed) {
                            root.document.speed = Math.round(value);
                        }
                    }
                    onMoved: {
                        if (!pressed) {
                            root.document.speed = Math.round(value);
                        }
                    }

                    Binding {
                        target: speedSlider
                        property: "value"
                        value: root.document.speed
                        when: !speedSlider.pressed
                        restoreMode: Binding.RestoreNone
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: [50, 75, 100]

                        delegate: AbstractButton {
                            id: preset

                            required property int modelData
                            readonly property bool on: root.document.speed === modelData

                            Layout.fillWidth: true
                            implicitHeight: 30
                            focusPolicy: Qt.TabFocus
                            text: qsTr("%1 %").arg(modelData)
                            onClicked: root.document.speed = modelData

                            contentItem: Label {
                                text: preset.text
                                font.pixelSize: Theme.fontBody
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: preset.on ? "white" : Theme.ink
                            }
                            background: Rectangle {
                                radius: height / 2
                                color: preset.on ? preset.Material.accent
                                                 : (preset.hovered ? Theme.hover : "transparent")
                                border.color: preset.on ? preset.Material.accent : Theme.line

                                Behavior on color {
                                    ColorAnimation {
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
