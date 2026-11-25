// SPDX-License-Identifier: AGPL-3.0-or-later
// Offers back the songs that were left with unsaved changes when Elara last
// closed without saving them, as after a crash (ADR-0106).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

Dialog {
    id: root

    /// The copies on offer (ScoreDocument.recoverable()), the newest first.
    property var copies: []
    readonly property int count: copies.length

    /// A copy to open as the song it was.
    signal recoverRequested(string file)
    /// A copy to remove for good.
    signal discardRequested(string file)

    /// Shows `left` when there is any.
    function offer(left) {
        copies = left;
        if (count > 0)
            open();
    }

    function recover(index: int) {
        const file = copies[index].file;
        close();
        recoverRequested(file);
    }

    function discard(index: int) {
        discardRequested(copies[index].file);
        copies = copies.filter((copy, i) => i !== index);
        if (count === 0)
            close();
    }

    anchors.centerIn: parent
    width: Math.min(520, parent.width - 48)
    modal: true
    title: qsTr("Recover unsaved changes?")
    // Closing keeps the copies for the next start.
    standardButtons: Dialog.Close

    ColumnLayout {
        width: root.availableWidth
        spacing: 14

        Label {
            Layout.fillWidth: true
            text: qsTr("Elara closed before these changes were saved. Recover a song to go on with it, or discard it.")
            wrapMode: Text.Wrap
            opacity: 0.85
        }

        Repeater {
            model: root.copies

            delegate: RowLayout {
                id: row

                required property var modelData
                required property int index

                Layout.fillWidth: true
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.title
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: {
                            const source = row.modelData.source.toString();
                            const file = source.length > 0
                                    ? decodeURIComponent(source.substring(source.lastIndexOf("/") + 1))
                                    : qsTr("Never saved");
                            return file + " · " + row.modelData.written.toLocaleString(Qt.locale(), Locale.ShortFormat);
                        }
                        elide: Text.ElideRight
                        opacity: 0.6
                        font.pixelSize: 12
                    }
                }
                Button {
                    text: qsTr("Discard")
                    flat: true
                    onClicked: root.discard(row.index)
                }
                Button {
                    text: qsTr("Recover")
                    highlighted: true
                    onClicked: root.recover(row.index)
                }
            }
        }
    }
}
