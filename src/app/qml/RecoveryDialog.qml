// SPDX-License-Identifier: AGPL-3.0-or-later
// Offers back the songs that were left with unsaved changes when Elara last
// closed without saving them, as after a crash (ADR-0106).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Elara.App

AppDialog {
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

    /// Opens copy `index` as the song it was, in a tab of its own (ADR-0160);
    /// the others stay on offer.
    function recover(index: int) {
        const file = copies[index].file;
        copies = copies.filter((copy, i) => i !== index);
        if (count === 0)
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
    title: qsTr("Recover unsaved changes?")
    // Closing keeps the copies for the next start.
    standardButtons: Dialog.Close

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: qsTr("Elara closed before these changes were saved. Recover a song to go on with it, or discard it.")
            wrapMode: Text.Wrap
            color: Theme.inkSoft
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
                    spacing: 1

                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.title
                        font.weight: Font.DemiBold
                        color: Theme.ink
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
                        color: Theme.inkSoft
                        font.pixelSize: Theme.fontSmall
                    }
                }
                TextButton {
                    kind: "danger"
                    text: qsTr("Discard")
                    onClicked: root.discard(row.index)
                }
                TextButton {
                    kind: "main"
                    text: qsTr("Recover")
                    onClicked: root.recover(row.index)
                }
            }
        }
    }
}
