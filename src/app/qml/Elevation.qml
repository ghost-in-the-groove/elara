// SPDX-License-Identifier: AGPL-3.0-or-later
// The soft shadow under a surface that floats (the transport, the entry bar,
// popups). A child of that surface: it fills it, follows its corners and is
// drawn behind it.
import QtQuick
import QtQuick.Effects
import Elara.App

RectangularShadow {
    anchors.fill: parent
    z: -1
    offset.y: 6
    blur: 24
    color: Theme.shadow
}
