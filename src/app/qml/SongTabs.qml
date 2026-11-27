// SPDX-License-Identifier: AGPL-3.0-or-later
// The songs open, a tab each (ADR-0160), under the tool row while two or more
// are open. A click shows a song; its cross or a middle click closes it.
import QtQuick
import QtQuick.Controls
import Elara.App

Rectangle {
    id: root

    /// The songs open.
    required property DocumentSet songs

    /// Song `index` is to close, once its changes are saved or discarded.
    signal closeRequested(int index)

    implicitHeight: 34
    color: Theme.bar

    ListView {
        id: tabs

        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.bottomMargin: 1
        orientation: ListView.Horizontal
        spacing: 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.songs
        currentIndex: root.songs.currentIndex
        highlightFollowsCurrentItem: false
        // The song shown stays in view.
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

        delegate: AbstractButton {
            id: tab

            required property int index
            required property string title
            required property bool modified
            required property bool busy
            readonly property bool chosen: index === root.songs.currentIndex

            y: 4
            height: tabs.height - 8
            width: Math.min(220, Math.max(96, name.implicitWidth + 46))
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            onClicked: root.songs.currentIndex = index
            Accessible.role: Accessible.PageTab
            Accessible.name: title
            ToolTip.visible: hovered && name.truncated
            ToolTip.delay: 600
            ToolTip.text: title

            background: Rectangle {
                radius: Theme.radiusSmall
                color: tab.chosen ? Theme.accentWash
                                  : tab.down ? Theme.pressed : tab.hovered ? Theme.hover : "transparent"
                border.width: tab.chosen ? 1 : 0
                border.color: Theme.accentInk
            }

            contentItem: Item {
                Label {
                    id: name

                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: closeButton.left
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    // A dot for unsaved changes, as in the window's title.
                    text: (tab.modified ? "• " : "") + tab.title
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontBody
                    color: tab.chosen ? Theme.accentInk : tab.busy ? Theme.inkSoft : Theme.ink
                }

                AbstractButton {
                    id: closeButton

                    objectName: "closeSong"
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: root.closeRequested(tab.index)
                    Accessible.name: qsTr("Close song")
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Close song (%1)").arg(KeyNames.native("Ctrl+W"))

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: closeButton.down ? Theme.pressed
                                                : closeButton.hovered ? Theme.hover : "transparent"
                    }
                    contentItem: Icon {
                        path: Icons.close
                        lineWidth: 1.6
                        color: tab.chosen ? Theme.accentInk : Theme.inkSoft
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.MiddleButton
                onClicked: root.closeRequested(tab.index)
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.line
    }
}
