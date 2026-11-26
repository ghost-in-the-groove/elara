// SPDX-License-Identifier: AGPL-3.0-or-later
// A hairline between groups of an AppMenu.
import QtQuick
import QtQuick.Controls
import Elara.App

MenuSeparator {
    topPadding: 4
    bottomPadding: 4
    leftPadding: 6
    rightPadding: 6

    contentItem: Rectangle {
        implicitHeight: 1
        color: Theme.line
    }
}
