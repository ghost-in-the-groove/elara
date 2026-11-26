// SPDX-License-Identifier: AGPL-3.0-or-later
// The colours, sizes and timings the interface shares, in the light and in
// the dark theme (docs/interface.md). The window sets `dark` from the Material
// theme it follows.
pragma Singleton
import QtQuick

QtObject {
    property bool dark: false

    // The command bar, dark in both themes: the night of the app icon, with
    // pearl for what is written on it and lilac for the song string.
    readonly property color night: dark ? "#191531" : "#1e1940"
    readonly property color pearl: "#f1eef8"
    readonly property color pearlSoft: Qt.alpha(pearl, 0.62)
    readonly property color lilac: "#b3a4ff"
    readonly property color nightHover: Qt.alpha(pearl, 0.1)
    readonly property color nightPressed: Qt.alpha(pearl, 0.18)
    readonly property color nightLine: Qt.alpha(pearl, 0.14)
    /// The well the transport's readouts sit in.
    readonly property color nightWell: Qt.rgba(0, 0, 0, 0.26)

    // Surfaces, from the furthest back to the nearest.
    /// The desk the pages lie on.
    readonly property color desk: dark ? "#111015" : "#e5e2dc"
    /// The tool row, the panels and the status bar.
    readonly property color bar: dark ? "#1b1a20" : "#f5f3ef"
    /// Fields, cards and whatever floats over the score: a step under the
    /// paper, which stays the brightest surface in both themes.
    readonly property color surface: dark ? "#222127" : "#fbfaf7"
    /// The page of the score in the dark theme, and the ink on it (ADR-0114).
    readonly property color nightPage: "#27262e"
    readonly property color nightInk: "#e7e5ec"
    /// Hairlines between and around surfaces.
    readonly property color line: dark ? "#35333d" : "#d8d4cc"

    /// Text and icons.
    readonly property color ink: dark ? "#edebf2" : "#1f1c24"
    /// Text that explains or counts: captions, units, hints.
    readonly property color inkSoft: dark ? "#aaa6b4" : "#5c5864"
    /// Tracks, ticks and outlines that only hint.
    readonly property color faint: dark ? "#4b4854" : "#cdc8bf"
    /// What a control shows under the pointer, and pressed.
    readonly property color hover: Qt.alpha(ink, 0.07)
    readonly property color pressed: Qt.alpha(ink, 0.14)

    /// The accent as a fill: what plays, what is chosen, the one main action.
    readonly property color accent: "#6c4cf5"
    /// The accent as text or an icon: lighter on dark surfaces, where the
    /// fill would be hard to read.
    readonly property color accentInk: dark ? "#a99bff" : "#5a3de6"
    /// A wash of the accent behind what is chosen.
    readonly property color accentWash: Qt.alpha(accentInk, dark ? 0.22 : 0.13)
    /// Text and icons on the accent.
    readonly property color accentText: "#ffffff"

    readonly property color danger: "#e5484d"
    readonly property color dangerInk: dark ? "#ffb4b4" : "#a12622"
    readonly property color dangerSurface: dark ? "#3a2328" : "#fdecee"
    readonly property color success: dark ? "#5fd38d" : "#1e9e57"
    readonly property color warning: dark ? "#ffb454" : "#925408"
    readonly property color solo: "#f5a524"

    /// The shadow under what floats.
    readonly property color shadow: Qt.rgba(0, 0, 0, dark ? 0.5 : 0.18)

    /// A hue for each track, the same wherever the track shows. None is the
    /// accent's violet, which says chosen or playing.
    readonly property list<color> trackHues: dark
        ? ["#5fc9c0", "#f0a35e", "#e98bb0", "#8fc56a", "#6fb1ee", "#e0c35a", "#e58f7f", "#a9b6c8"]
        : ["#178f86", "#c26a1a", "#c2457d", "#4f8a26", "#2374c4", "#9a7b10", "#b0483a", "#5f6f86"]

    function trackHue(index: int): color {
        return trackHues[((index % trackHues.length) + trackHues.length) % trackHues.length];
    }

    /// Corners: controls, cards and panels, sheets that float.
    readonly property real radiusSmall: 6
    readonly property real radiusCard: 8
    readonly property real radiusSheet: 10

    /// A tool button and the icon in it; a field or a button with a label.
    readonly property int tool: 32
    readonly property int icon: 20
    readonly property int control: 30

    readonly property int fontCaption: 11
    readonly property int fontSmall: 12
    readonly property int fontBody: 13
    readonly property int fontTitle: 15

    /// State changes: under the pointer, and what slides or opens.
    readonly property int quick: 120
    readonly property int calm: 180
}
