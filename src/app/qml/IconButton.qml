// SPDX-License-Identifier: AGPL-3.0-or-later
// A flat tool button showing an Icon, with a tooltip.
import QtQuick
import QtQuick.Controls

ToolButton {
    id: root

    property string iconPath
    property string tip

    display: AbstractButton.IconOnly
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    contentItem: Icon {
        path: root.iconPath
        color: root.palette.buttonText
        opacity: root.enabled ? 1.0 : 0.35
    }

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip
    Accessible.name: tip
}
