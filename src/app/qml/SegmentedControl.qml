// SPDX-License-Identifier: AGPL-3.0-or-later
// A row of mutually exclusive choices with a highlight that slides to the
// chosen one.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Rectangle {
    id: root

    /// The labels of the choices.
    property list<string> labels
    /// Which choices can be picked (all when shorter than labels).
    property list<bool> enabledChoices
    property int currentIndex: 0

    signal activated(int index)

    implicitWidth: row.implicitWidth + 6
    implicitHeight: 32
    radius: height / 2
    color: Qt.alpha(Material.foreground, 0.07)
    opacity: enabled ? 1.0 : 0.45

    Rectangle {
        id: highlight

        readonly property Item target: repeater.count > root.currentIndex && root.currentIndex >= 0
                                       ? repeater.itemAt(root.currentIndex) : null

        visible: target !== null
        x: row.x + (target ? target.x : 0)
        y: 3
        width: target ? target.width : 0
        height: root.height - 6
        radius: height / 2
        color: Material.accent

        Behavior on x {
            NumberAnimation {
                duration: 180
                easing.type: Easing.OutCubic
            }
        }
        Behavior on width {
            NumberAnimation {
                duration: 180
                easing.type: Easing.OutCubic
            }
        }
    }

    Row {
        id: row

        anchors.centerIn: parent

        Repeater {
            id: repeater

            model: root.labels

            delegate: Item {
                id: choice

                required property int index
                required property string modelData
                readonly property bool choosable: root.enabledChoices.length <= index
                                                  || root.enabledChoices[index]
                readonly property bool current: index === root.currentIndex

                width: label.implicitWidth + 28
                height: root.height

                Label {
                    id: label

                    anchors.centerIn: parent
                    text: choice.modelData
                    font.pixelSize: 13
                    font.weight: choice.current ? Font.DemiBold : Font.Normal
                    color: choice.current ? "white" : Material.foreground
                    opacity: choice.choosable ? 1.0 : 0.35

                    Behavior on color {
                        ColorAnimation {
                            duration: 180
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: choice.choosable && root.enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!choice.current)
                            root.activated(choice.index);
                    }
                }
            }
        }
    }
}
