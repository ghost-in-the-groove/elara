// SPDX-License-Identifier: AGPL-3.0-or-later
// A line icon drawn from an SVG path on a 24 x 24 grid (Icons): crisp at any
// size and in any colour, no image files.
import QtQuick
import QtQuick.Shapes
import Elara.App

Item {
    id: root

    property string path
    property color color: Theme.ink
    property real lineWidth: 1.8
    /// Fill the shape with the colour too (play and pause symbols).
    property bool filled: false

    implicitWidth: Theme.icon
    implicitHeight: Theme.icon

    Shape {
        width: 24
        height: 24
        scale: Math.min(root.width, root.height) / 24
        anchors.centerIn: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.lineWidth
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: root.path
            }
        }
    }
}
