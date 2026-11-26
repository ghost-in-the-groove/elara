// SPDX-License-Identifier: AGPL-3.0-or-later
// A flat tool button showing an Icon, with a tooltip. Checked, it shows in the
// accent; on the command bar it takes the bar's colours.
import QtQuick
import QtQuick.Controls
import Elara.App

ToolButton {
    id: root

    property string iconPath
    property string tip
    /// On the command bar, which is dark in both themes.
    property bool nightBar: false
    /// Whether a click takes the keyboard: off for the tools of the score,
    /// which must leave it there.
    property bool takesFocus: true

    readonly property color ink: nightBar ? Theme.pearl : Theme.ink
    readonly property color lit: nightBar ? Theme.lilac : Theme.accentInk

    display: AbstractButton.IconOnly
    hoverEnabled: true
    focusPolicy: takesFocus ? Qt.TabFocus : Qt.NoFocus
    implicitWidth: Theme.tool
    implicitHeight: Theme.tool
    padding: (Theme.tool - Theme.icon) / 2
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0

    background: Rectangle {
        radius: Theme.radiusSmall
        color: !root.enabled ? "transparent"
                             : root.down ? (root.nightBar ? Theme.nightPressed : Theme.pressed)
                                         : root.checked ? Qt.alpha(root.lit, root.nightBar ? 0.22 : 0.15)
                                                        : root.hovered ? (root.nightBar ? Theme.nightHover
                                                                                        : Theme.hover)
                                                                       : "transparent"
        border.width: root.visualFocus ? 2 : 0
        border.color: root.lit
    }

    contentItem: Icon {
        path: root.iconPath
        color: root.checked ? root.lit : root.ink
        opacity: root.enabled ? 1.0 : 0.38
    }

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip
    Accessible.name: tip
}
