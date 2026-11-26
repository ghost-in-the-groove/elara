// SPDX-License-Identifier: AGPL-3.0-or-later
// Practice controls of the transport, on the command bar: the A-B loop, the
// metronome, a measure of clicks before playing, and the playback speed as a
// button that opens a slider with the usual practice speeds (ADR-0024).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

RowLayout {
    id: root

    required property ScoreDocument document
    required property AudioController playback
    /// Without the metronome and the count-in, which the Sound menu also has.
    property bool compact: false
    /// The width those two take, and the width of the rest.
    readonly property real toggles: 2 * (Theme.tool + spacing)
    readonly property real coreWidth: loopButton.implicitWidth + spacing + speedButton.Layout.leftMargin
                                      + speedButton.implicitWidth

    spacing: 2

    AbstractButton {
        id: loopButton

        objectName: "loopButton"
        readonly property bool on: root.playback.looping || root.playback.loopMarked

        implicitHeight: Theme.tool
        implicitWidth: on ? loopRow.implicitWidth + 16 : Theme.tool
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
                duration: Theme.quick
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
                    path: Icons.loop
                    color: loopButton.on ? Theme.lilac : Theme.pearl
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: loopButton.on
                    text: root.playback.looping ? qsTr("A–B") : qsTr("A")
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                    color: Theme.lilac
                }
            }
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: loopButton.down ? Theme.nightPressed
                                   : loopButton.on ? Qt.alpha(Theme.lilac, 0.22)
                                                   : loopButton.hovered ? Theme.nightHover : "transparent"
            border.width: loopButton.visualFocus ? 2 : 0
            border.color: Theme.lilac
        }
    }

    IconButton {
        objectName: "metronomeButton"
        visible: !root.compact
        nightBar: true
        iconPath: Icons.metronome
        tip: qsTr("Metronome")
        checked: root.document.metronome
        onClicked: root.document.metronome = !root.document.metronome
    }

    IconButton {
        objectName: "countInButton"
        visible: !root.compact
        nightBar: true
        iconPath: Icons.countIn
        tip: qsTr("Count-in: a measure of clicks before playing")
        checked: root.playback.countIn
        onClicked: root.playback.countIn = !root.playback.countIn
    }

    AbstractButton {
        id: speedButton

        objectName: "speedButton"
        readonly property bool changed: root.document.speed !== 100

        Layout.leftMargin: 4
        implicitHeight: Theme.control - 2
        implicitWidth: speedMetrics.advanceWidth + 20
        focusPolicy: Qt.TabFocus
        hoverEnabled: true
        Accessible.name: qsTr("Playback speed: %1 %").arg(root.document.speed)
        ToolTip.visible: hovered && !speedPopup.visible
        ToolTip.delay: 600
        ToolTip.text: qsTr("Playback speed")
        onClicked: speedPopup.opened ? speedPopup.close() : speedPopup.open()

        // As wide as the widest speed, so that a change moves nothing.
        TextMetrics {
            id: speedMetrics

            font: speedLabel.font
            text: qsTr("%1 %").arg(200)
        }

        contentItem: Label {
            id: speedLabel

            text: qsTr("%1 %").arg(root.document.speed)
            font.pixelSize: Theme.fontBody
            font.features: {
                "tnum": 1
            }
            color: speedButton.changed ? Theme.lilac : Theme.pearl
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: speedButton.down ? Theme.nightPressed
                                    : speedButton.changed ? Qt.alpha(Theme.lilac, 0.22)
                                                          : speedButton.hovered ? Theme.nightHover
                                                                                : "transparent"
            border.width: speedButton.visualFocus ? 2 : 1
            border.color: speedButton.visualFocus ? Theme.lilac : Theme.nightLine
        }

        Popup {
            id: speedPopup

            objectName: "speedPopup"
            // Under the command bar, its right edge on the button's.
            x: speedButton.width - width
            y: speedButton.height + 12
            width: 280
            padding: 14
            topPadding: 12
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

            enter: Transition {
                NumberAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: Theme.quick
                }
            }
            exit: Transition {
                NumberAnimation {
                    property: "opacity"
                    to: 0
                    duration: 80
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
                        color: Theme.ink
                    }
                    Label {
                        // Follows the slider while it is dragged.
                        text: qsTr("%1 %").arg(Math.round(speedSlider.value))
                        font.pixelSize: Theme.fontBody
                        font.features: {
                            "tnum": 1
                        }
                        color: Theme.inkSoft
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
                            implicitHeight: Theme.control - 2
                            focusPolicy: Qt.TabFocus
                            hoverEnabled: true
                            text: qsTr("%1 %").arg(modelData)
                            onClicked: root.document.speed = modelData

                            contentItem: Label {
                                text: preset.text
                                font.pixelSize: Theme.fontBody
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: preset.on ? Theme.accentInk : Theme.ink
                            }
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: preset.down ? Theme.pressed
                                                   : preset.on ? Theme.accentWash
                                                               : preset.hovered ? Theme.hover : "transparent"
                                border.width: preset.visualFocus ? 2 : 1
                                border.color: preset.visualFocus || preset.on
                                              ? Qt.alpha(Theme.accentInk, preset.visualFocus ? 1.0 : 0.5)
                                              : Theme.line
                            }
                        }
                    }
                }
            }
        }
    }
}
