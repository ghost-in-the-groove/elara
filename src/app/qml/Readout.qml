// SPDX-License-Identifier: AGPL-3.0-or-later
// A reading of the transport in a cell of fixed width: its figures change, the
// cell and everything beside it stay where they are.
import QtQuick
import QtQuick.Controls
import Elara.App

Item {
    id: root

    /// What it reads now, and what follows it more quietly (" / 64").
    property string value
    property string rest
    /// The widest text it will ever read: the cell is sized for it.
    property string widest
    /// A line icon before the figures, or a glyph of the music font.
    property string iconPath
    property string symbol
    property string tip

    implicitWidth: Math.ceil(metrics.advanceWidth) + lead.width + (lead.width > 0 ? 6 : 0) + 20
    implicitHeight: Theme.control
    Accessible.role: Accessible.StaticText
    Accessible.name: tip + ": " + value + rest

    TextMetrics {
        id: metrics

        font: figures.font
        text: root.widest
    }

    Row {
        anchors.centerIn: parent
        spacing: lead.width > 0 ? 6 : 0

        Item {
            id: lead

            width: root.iconPath.length > 0 ? 14 : root.symbol.length > 0 ? 9 : 0
            height: 14
            anchors.verticalCenter: parent.verticalCenter

            Icon {
                anchors.fill: parent
                visible: root.iconPath.length > 0
                path: root.iconPath
                color: Theme.pearlSoft
            }
            MusicSymbol {
                anchors.centerIn: parent
                visible: root.symbol.length > 0
                symbol: root.symbol
                size: 16
                color: Theme.pearlSoft
            }
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter

            Label {
                id: figures

                text: root.value
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                font.features: {
                    "tnum": 1
                }
                color: Theme.pearl
            }
            Label {
                text: root.rest
                font: figures.font
                color: Theme.pearlSoft
            }
        }
    }

    HoverHandler {
        id: hover
    }

    ToolTip.visible: hover.hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip
}
