// SPDX-License-Identifier: AGPL-3.0-or-later
// A glyph of the music font, sized and centred by its ink rather than by the
// font's (very tall) line height.
import QtQuick

Item {
    id: root

    property string symbol
    property real size: 32
    property color color: "black"

    implicitWidth: metrics.tightBoundingRect.width
    implicitHeight: metrics.tightBoundingRect.height

    TextMetrics {
        id: metrics

        font: glyph.font
        text: root.symbol
    }

    Text {
        id: glyph

        x: (root.width - metrics.tightBoundingRect.width) / 2 - metrics.tightBoundingRect.x
        y: (root.height - metrics.tightBoundingRect.height) / 2 - metrics.tightBoundingRect.y
           - baselineOffset
        text: root.symbol
        font.family: "Bravura"
        font.pixelSize: root.size
        color: root.color
    }
}
