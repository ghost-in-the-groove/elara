// SPDX-License-Identifier: AGPL-3.0-or-later
// A row of mutually exclusive choices with a highlight that slides to the
// chosen one. The labels keep one weight, so that choosing never moves them.
import QtQuick
import QtQuick.Controls
import Elara.App

Rectangle {
    id: root

    /// The labels of the choices.
    property list<string> labels
    /// Which choices can be picked (all when shorter than labels).
    property list<bool> enabledChoices
    property int currentIndex: 0
    /// Room either side of each label.
    property real sidePadding: 12

    signal activated(int index)

    implicitWidth: row.implicitWidth + 4
    implicitHeight: Theme.control
    radius: Theme.radiusSmall
    color: Theme.hover
    opacity: enabled ? 1.0 : 0.38

    Rectangle {
        id: highlight

        readonly property Item target: repeater.count > root.currentIndex && root.currentIndex >= 0
                                       ? repeater.itemAt(root.currentIndex) : null

        visible: target !== null
        x: row.x + (target ? target.x : 0)
        y: 2
        width: target ? target.width : 0
        height: root.height - 4
        radius: Theme.radiusSmall - 2
        color: Theme.accentWash
        border.color: Qt.alpha(Theme.accentInk, 0.4)

        Behavior on x {
            NumberAnimation {
                duration: Theme.calm
                easing.type: Easing.OutCubic
            }
        }
        Behavior on width {
            NumberAnimation {
                duration: Theme.calm
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

                width: label.implicitWidth + 2 * root.sidePadding
                height: root.height
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData
                Accessible.checked: current

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radiusSmall - 2
                    color: Theme.hover
                    visible: area.containsMouse && !choice.current && choice.choosable
                }

                Label {
                    id: label

                    anchors.centerIn: parent
                    text: choice.modelData
                    font.pixelSize: Theme.fontBody
                    color: choice.current ? Theme.accentInk : Theme.ink
                    opacity: choice.choosable ? 1.0 : 0.38

                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.calm
                        }
                    }
                }

                MouseArea {
                    id: area

                    anchors.fill: parent
                    enabled: choice.choosable && root.enabled
                    hoverEnabled: true
                    onClicked: {
                        if (!choice.current)
                            root.activated(choice.index);
                    }
                }
            }
        }
    }
}
