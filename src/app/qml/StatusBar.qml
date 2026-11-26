// SPDX-License-Identifier: AGPL-3.0-or-later
// The status bar along the bottom of the window: where the caret is and what
// the score holds at the left; the layout and the zoom at the right.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

Rectangle {
    id: root

    required property ScoreDocument document
    /// The view of the score: its page and its zoom.
    required property ScoreView view

    /// A click on the changes made on import asks to read them.
    signal importWarningsRequested

    component Dot: Rectangle {
        implicitWidth: 3
        implicitHeight: 3
        radius: 1.5
        color: Theme.faint
    }

    component Note: Label {
        font.pixelSize: Theme.fontSmall
        font.features: {
            "tnum": 1
        }
        color: Theme.inkSoft
    }

    // A group of small tools on one ground.
    component Group: Rectangle {
        default property alias tools: groupRow.data

        implicitWidth: groupRow.implicitWidth
        implicitHeight: 22
        radius: Theme.radiusSmall
        color: Theme.hover

        Row {
            id: groupRow

            anchors.centerIn: parent
        }
    }

    component SmallTool: IconButton {
        implicitWidth: 26
        implicitHeight: 22
        padding: 3
        leftPadding: 5
        rightPadding: 5
        takesFocus: false
    }

    implicitHeight: 28
    color: Theme.bar

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.line
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 9

        // Where typing goes.
        Label {
            objectName: "caretStatus"
            visible: text.length > 0
            text: root.document.caretPage >= 0 ? root.document.caretStatus : ""
            font.pixelSize: Theme.fontSmall
            font.features: {
                "tnum": 1
            }
            color: Theme.ink
            elide: Text.ElideRight
            Layout.maximumWidth: root.width * 0.4
        }
        Dot {
            visible: root.document.caretPage >= 0
        }
        // Strips have no page numbers.
        Note {
            visible: !root.document.showsStrips
            text: qsTr("Page %1 of %2").arg(root.view.currentPage).arg(root.document.pageCount)
        }
        Dot {
            visible: !root.document.showsStrips
        }
        Note {
            text: qsTr("%n measure(s)", "", root.document.measureCount)
        }
        // What opening the file changed or left out, to read again.
        Dot {
            visible: root.document.importWarnings.length > 0
        }
        Label {
            objectName: "importWarningsLabel"
            visible: root.document.importWarnings.length > 0
            text: qsTr("%n change(s) on import", "", root.document.importWarnings.length)
            font.pixelSize: Theme.fontSmall
            font.underline: importWarningsArea.containsMouse
            color: Theme.warning

            MouseArea {
                id: importWarningsArea

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.importWarningsRequested()
            }
        }
        Item {
            Layout.fillWidth: true
        }

        // Pages or one continuous scroll (ADR-0057).
        Group {
            SmallTool {
                objectName: "pageViewButton"
                iconPath: Icons.pageView
                tip: qsTr("Page view")
                checked: !root.document.continuous
                onClicked: root.document.continuous = false
            }
            SmallTool {
                objectName: "continuousViewButton"
                iconPath: Icons.continuousView
                tip: qsTr("Continuous view")
                checked: root.document.continuous
                onClicked: root.document.continuous = true
            }
        }

        // Zoom: out, the level (a click fits the width), in.
        Group {
            SmallTool {
                enabled: root.view.zoom > root.view.minZoom
                iconPath: Icons.zoomOut
                tip: qsTr("Zoom out (Ctrl+-)")
                onClicked: root.view.zoomOut()
            }

            AbstractButton {
                id: zoomLevel

                implicitWidth: 48
                implicitHeight: 22
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                text: Math.round(root.view.zoom * 100) + "%"
                Accessible.name: qsTr("Zoom: %1").arg(text)
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Fit the page width (Ctrl+0)")
                onClicked: root.view.fitWidth()

                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: zoomLevel.down ? Theme.pressed : zoomLevel.hovered ? Theme.hover : "transparent"
                }

                contentItem: Label {
                    text: zoomLevel.text
                    font.pixelSize: Theme.fontSmall
                    font.features: {
                        "tnum": 1
                    }
                    color: Theme.ink
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            SmallTool {
                enabled: root.view.zoom < root.view.maxZoom
                iconPath: Icons.zoomIn
                tip: qsTr("Zoom in (Ctrl++)")
                onClicked: root.view.zoomIn()
            }
        }
    }
}
