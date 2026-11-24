// SPDX-License-Identifier: AGPL-3.0-or-later
// The colours, radii and type sizes the interface shares, in the light and in
// the dark theme. The window sets `dark` from the Material theme it follows.
pragma Singleton
import QtQuick

QtObject {
    property bool dark: false

    // Surfaces, from the furthest back to the nearest.
    /// The desk the pages lie on.
    readonly property color desk: dark ? "#141418" : "#eceef2"
    /// The toolbar and the status bar.
    readonly property color bar: dark ? "#1e1e24" : "#fafafc"
    /// Panels, cards and whatever floats over the score.
    readonly property color surface: dark ? "#26262e" : "#ffffff"
    /// Hairlines between and around surfaces.
    readonly property color line: dark ? "#363641" : "#dfe2e8"

    /// Text and icons.
    readonly property color ink: dark ? "#ececf1" : "#1d1d24"
    /// Tracks, ticks and outlines that only hint.
    readonly property color faint: dark ? "#4a4a55" : "#d3d6de"
    /// What a control shows under the pointer, and pressed.
    readonly property color hover: Qt.alpha(ink, 0.07)
    readonly property color pressed: Qt.alpha(ink, 0.14)

    /// The accent as text or an icon: lighter on dark surfaces, where the
    /// accent of qtquickcontrols2.conf would be hard to read.
    readonly property color accentInk: dark ? "#a99bff" : "#6c4cf5"
    /// A wash of the accent behind what is chosen.
    readonly property color accentWash: Qt.alpha(accentInk, dark ? 0.2 : 0.12)

    readonly property color danger: "#e5484d"
    readonly property color dangerInk: dark ? "#ffb4b4" : "#a12622"
    readonly property color dangerSurface: dark ? "#3a2328" : "#fdecee"
    readonly property color success: dark ? "#5fd38d" : "#1e9e57"
    readonly property color warning: dark ? "#ffb454" : "#d9822b"
    readonly property color solo: "#f5a524"

    /// The shadow under what floats.
    readonly property color shadow: Qt.rgba(0, 0, 0, dark ? 0.5 : 0.16)

    readonly property real radiusSmall: 8
    readonly property real radiusCard: 12
    readonly property real radiusSheet: 16

    readonly property int fontCaption: 11
    readonly property int fontSmall: 12
    readonly property int fontBody: 13
    readonly property int fontTitle: 16
}
