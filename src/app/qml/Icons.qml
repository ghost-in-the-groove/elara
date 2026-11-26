// SPDX-License-Identifier: AGPL-3.0-or-later
// The interface's icons: one family of line drawings on a 24 x 24 grid, drawn
// by Icon with round ends at one width. Each is an SVG path; `play` and
// `pause` are filled.
pragma Singleton
import QtQuick

QtObject {
    // Files and history.
    readonly property string newSong: "M6.5 3.5 L13.5 3.5 L18.5 8.5 L18.5 19 Q18.5 20.5 17 20.5 L7 20.5 Q5.5 20.5 5.5 19 L5.5 4.5 Q5.5 3.5 6.5 3.5 Z M13.5 3.5 L13.5 8.5 L18.5 8.5 M12 11.5 L12 17 M9.25 14.25 L14.75 14.25"
    readonly property string open: "M3.5 7.5 Q3.5 6 5 6 L9.2 6 L11.2 8 L19 8 Q20.5 8 20.5 9.5 L20.5 17 Q20.5 18.5 19 18.5 L5 18.5 Q3.5 18.5 3.5 17 Z"
    readonly property string save: "M5.5 4.5 L15.5 4.5 L19.5 8.5 L19.5 18 Q19.5 19.5 18 19.5 L6 19.5 Q4.5 19.5 4.5 18 L4.5 5.5 Q4.5 4.5 5.5 4.5 Z M8 4.5 L8 9 L14.5 9 L14.5 4.5 M8 19.5 L8 14 L16 14 L16 19.5"
    readonly property string undo: "M8.5 13.5 L4 9 L8.5 4.5 M4 9 L14 9 Q20 9 20 14.5 Q20 19.5 14.5 19.5 L11 19.5"
    readonly property string redo: "M15.5 13.5 L20 9 L15.5 4.5 M20 9 L10 9 Q4 9 4 14.5 Q4 19.5 9.5 19.5 L13 19.5"

    // The transport and practice.
    readonly property string toStart: "M6.5 6 L6.5 18 M18 6.5 L9.5 12 L18 17.5 Z"
    readonly property string play: "M8.5 5.5 L18.5 12 L8.5 18.5 Z"
    readonly property string pause: "M7.5 6 L10 6 L10 18 L7.5 18 Z M14 6 L16.5 6 L16.5 18 L14 18 Z"
    readonly property string loop: "M4.5 12 A5.5 5.5 0 0 1 10 6.5 L17.5 6.5 M15 4 L17.5 6.5 L15 9 M19.5 12 A5.5 5.5 0 0 1 14 17.5 L6.5 17.5 M9 15 L6.5 17.5 L9 20"
    readonly property string metronome: "M8.5 20 L10.8 4 L13.2 4 L15.5 20 Z M6.5 20 L17.5 20 M12 16 L16.5 8"
    readonly property string countIn: "M12 21 A7.5 7.5 0 1 0 12 6 A7.5 7.5 0 1 0 12 21 Z M12 9.5 L12 13.5 L14.5 15 M9.5 3 L14.5 3 M12 3 L12 6 M18.5 6.5 L19.8 5.2"
    readonly property string tuner: "M9 3 L9 10 Q9 14 12 14 Q15 14 15 10 L15 3 M12 14 L12 21"

    // Panels and views.
    readonly property string panelLeft: "M4 6 Q4 4.5 5.5 4.5 L18.5 4.5 Q20 4.5 20 6 L20 18 Q20 19.5 18.5 19.5 L5.5 19.5 Q4 19.5 4 18 Z M9.5 4.5 L9.5 19.5 M6.25 8 L7.25 8 M6.25 11 L7.25 11"
    readonly property string panelRight: "M4 6 Q4 4.5 5.5 4.5 L18.5 4.5 Q20 4.5 20 6 L20 18 Q20 19.5 18.5 19.5 L5.5 19.5 Q4 19.5 4 18 Z M14.5 4.5 L14.5 19.5 M16.75 8 L17.75 8 M16.75 11 L17.75 11"
    readonly property string pageView: "M7 3.5 L17 3.5 L17 10.5 L7 10.5 Z M7 13.5 L17 13.5 L17 20.5 L7 20.5 Z"
    readonly property string continuousView: "M7 3.5 L17 3.5 L17 20.5 L7 20.5 Z M10 8 L14 8 M10 12 L14 12 M10 16 L14 16"
    readonly property string zoomOut: "M15.5 10 A5.5 5.5 0 1 1 4.5 10 A5.5 5.5 0 1 1 15.5 10 M14 14 L20 20 M7.5 10 L12.5 10"
    readonly property string zoomIn: "M15.5 10 A5.5 5.5 0 1 1 4.5 10 A5.5 5.5 0 1 1 15.5 10 M14 14 L20 20 M7.5 10 L12.5 10 M10 7.5 L10 12.5"

    // Entry.
    readonly property string trash: "M5 7 L19 7 M10 7 L10 4.5 L14 4.5 L14 7 M7 7 L8 19.5 L16 19.5 L17 7 M10.5 10.5 L10.5 16 M13.5 10.5 L13.5 16"
    readonly property string effects: "M3 12 C5 8 7 8 9 12 C11 16 13 16 15 12 C17 8 19 8 21 12"
    readonly property string chord: "M7 6 L7 20 M12 6 L12 20 M17 6 L17 20 M5 6 L19 6 M5 11 L19 11 M5 16 L19 16 M5 4 L19 4"
    readonly property string text: "M6 7 L6 5.5 L18 5.5 L18 7 M12 5.5 L12 18.5 M9.5 18.5 L14.5 18.5"
    readonly property string lyrics: "M9.5 6.5 A2.5 2.5 0 0 1 14.5 6.5 L14.5 11 A2.5 2.5 0 0 1 9.5 11 Z M6.5 11 A5.5 5.5 0 0 0 17.5 11 M12 16.5 L12 20 M9 20 L15 20"
    readonly property string measure: "M4 7 L4 17 M20 7 L20 17 M4 9.5 L20 9.5 M4 12 L20 12 M4 14.5 L20 14.5 M12 7 L12 17"

    // Small signs.
    readonly property string close: "M7 7 L17 17 M17 7 L7 17"
    readonly property string plus: "M12 6 L12 18 M6 12 L18 12"
    readonly property string minus: "M6 12 L18 12"
    readonly property string check: "M5.5 12.5 L10 17 L18.5 7.5"
    readonly property string chevronDown: "M7 10 L12 15 L17 10"
    readonly property string chevronRight: "M10 7 L15 12 L10 17"
}
