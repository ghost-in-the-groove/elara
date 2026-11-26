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
    readonly property string tuning: "M7 4 L7 20 M12 4 L12 20 M17 4 L17 20 M4.5 9 L19.5 9"
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
    readonly property string chord: "M7 6 L7 20 M12 6 L12 20 M17 6 L17 20 M5 6 L19 6 M5 11 L19 11 M5 16 L19 16 M5 4 L19 4"
    readonly property string text: "M6 7 L6 5.5 L18 5.5 L18 7 M12 5.5 L12 18.5 M9.5 18.5 L14.5 18.5"
    readonly property string lyrics: "M9.5 6.5 A2.5 2.5 0 0 1 14.5 6.5 L14.5 11 A2.5 2.5 0 0 1 9.5 11 Z M6.5 11 A5.5 5.5 0 0 0 17.5 11 M12 16.5 L12 20 M9 20 L15 20"
    readonly property string measure: "M4 7 L4 17 M20 7 L20 17 M4 9.5 L20 9.5 M4 12 L20 12 M4 14.5 L20 14.5 M12 7 L12 17"

    // Note effects, each drawn as the score draws it.
    readonly property string tie: "M4.5 9.5 A2.3 1.8 0 1 0 9.1 9.5 A2.3 1.8 0 1 0 4.5 9.5 M14.9 9.5 A2.3 1.8 0 1 0 19.5 9.5 A2.3 1.8 0 1 0 14.9 9.5 M6.5 14.5 Q12 19.5 17.5 14.5"
    readonly property string deadNote: "M7.5 7.5 L16.5 16.5 M16.5 7.5 L7.5 16.5"
    readonly property string letRing: "M3.5 12 A2.6 2 0 1 0 8.7 12 A2.6 2 0 1 0 3.5 12 M11.5 12 L13.5 12 M15.5 12 L17.5 12 M19.5 12 L20.5 12 L20.5 15.5"
    readonly property string harmonic: "M12 5.5 L18.5 12 L12 18.5 L5.5 12 Z"
    readonly property string ghostNote: "M8 5.5 Q4.5 12 8 18.5 M16 5.5 Q19.5 12 16 18.5 M9.8 12 A2.2 1.8 0 1 0 14.2 12 A2.2 1.8 0 1 0 9.8 12"
    readonly property string accent: "M7 7 L17 12 L7 17"
    readonly property string marcato: "M7 17 L12 7 L17 17"
    readonly property string staccato: "M10.6 12 A1.4 1.4 0 1 0 13.4 12 A1.4 1.4 0 1 0 10.6 12"
    readonly property string tenuto: "M7 12 L17 12"
    readonly property string staccatissimo: "M12 16.5 L9.5 8 L14.5 8 Z"

    // Bends and the whammy bar.
    readonly property string bend: "M3 19 L7 19 C11 19 12 15 12 6 M9 9 L12 6 L15 9"
    readonly property string bendRelease: "M2 19 L4 19 C7 19 8 14 8 6 M5.5 8.5 L8 6 L10.5 8.5 M8 6 L12 6 C15 6 16 12 16 19 M13.5 16.5 L16 19 L18.5 16.5"
    readonly property string prebend: "M12 20 L12 5 M8.5 8.5 L12 5 L15.5 8.5"
    readonly property string prebendRelease: "M7 20 L7 5 M4 8 L7 5 L10 8 M7 5 L10 5 C13 5 15 11 15 19 M12.5 16.5 L15 19 L17.5 16.5"
    readonly property string whammyDive: "M3 7 L8 7 L13 17 L21 17"
    readonly property string whammyDip: "M3 7 L8 7 L12 17 L16 7 L21 7"
    readonly property string whammyPredive: "M5 5 L5 17 L21 17"
    readonly property string whammyPrediveRelease: "M5 5 L5 17 L10 17 L16 7 L21 7"

    // Slides: into the note, to the next one, and out of it.
    readonly property string slideInBelow: "M3 19 L11 12 M13 12 A2.5 2.5 0 1 0 18 12 A2.5 2.5 0 1 0 13 12"
    readonly property string slideInAbove: "M3 5 L11 12 M13 12 A2.5 2.5 0 1 0 18 12 A2.5 2.5 0 1 0 13 12"
    readonly property string slide: "M2 16 A2.5 2.5 0 1 0 7 16 A2.5 2.5 0 1 0 2 16 M8.5 14.5 L15.5 9.5 M17 8 A2.5 2.5 0 1 0 22 8 A2.5 2.5 0 1 0 17 8"
    readonly property string slideLegato: "M2 18 A2.5 2.5 0 1 0 7 18 A2.5 2.5 0 1 0 2 18 M8.5 16.5 L15.5 11.5 M17 10 A2.5 2.5 0 1 0 22 10 A2.5 2.5 0 1 0 17 10 M4.5 13 Q11 3 19.5 5.5"
    readonly property string slideOutDown: "M6 12 A2.5 2.5 0 1 0 11 12 A2.5 2.5 0 1 0 6 12 M13 12 L21 19"
    readonly property string slideOutUp: "M6 12 A2.5 2.5 0 1 0 11 12 A2.5 2.5 0 1 0 6 12 M13 12 L21 5"

    // Vibrato and ornaments.
    readonly property string vibrato: "M3 12 C5 9 7 9 9 12 C11 15 13 15 15 12 C17 9 19 9 21 12"
    readonly property string vibratoWide: "M3 12 C5 5 7 5 9 12 C11 19 13 19 15 12 C17 5 19 5 21 12"
    readonly property string mordent: "M3 14 L7 10 L10 14 L14 10 L17 14 L21 10 M12 6 L12 18"
    readonly property string upperMordent: "M3 14 L7 10 L10 14 L14 10 L17 14 L21 10"
    readonly property string turn: "M4 13 C4 8 10 8 12 12 C14 16 20 16 20 11"
    readonly property string invertedTurn: "M4 11 C4 16 10 16 12 12 C14 8 20 8 20 13"

    // Dynamics, phrasing and lines.
    readonly property string crescendo: "M19 7 L5 12 L19 17"
    readonly property string diminuendo: "M5 7 L19 12 L5 17"
    readonly property string slur: "M4 13 Q12 4 20 13 M5 18 A1.6 1.2 0 1 0 8.2 18 A1.6 1.2 0 1 0 5 18 M15.8 18 A1.6 1.2 0 1 0 19 18 A1.6 1.2 0 1 0 15.8 18"
    readonly property string graceNote: "M8 18 A2.5 2 0 1 0 13 18 A2.5 2 0 1 0 8 18 M13 18 L13 5 Q16 7 17 10 M9 12 L17 7"
    readonly property string fermataShort: "M5 16 L12 8 L19 16 M11 15 A1 1 0 1 0 13 15 A1 1 0 1 0 11 15"
    readonly property string fermata: "M5 16 A7 7 0 0 1 19 16 M11 15 A1 1 0 1 0 13 15 A1 1 0 1 0 11 15"
    readonly property string fermataLong: "M5 16 L5 9 L19 9 L19 16 M11 15 A1 1 0 1 0 13 15 A1 1 0 1 0 11 15"
    readonly property string ottavaUp: "M4.3 5.5 A1.7 1.7 0 1 0 7.7 5.5 A1.7 1.7 0 1 0 4.3 5.5 M3.9 9.4 A2.1 2.1 0 1 0 8.1 9.4 A2.1 2.1 0 1 0 3.9 9.4 M10.5 7.5 L12.5 7.5 M14.5 7.5 L16.5 7.5 M18.5 7.5 L20 7.5 L20 11.5"
    readonly property string ottavaDown: "M4.3 14.6 A1.7 1.7 0 1 0 7.7 14.6 A1.7 1.7 0 1 0 4.3 14.6 M3.9 18.5 A2.1 2.1 0 1 0 8.1 18.5 A2.1 2.1 0 1 0 3.9 18.5 M10.5 16.5 L12.5 16.5 M14.5 16.5 L16.5 16.5 M18.5 16.5 L20 16.5 L20 12.5"
    readonly property string pedal: "M3 9 L3 16 L9.5 16 L12 11.5 L14.5 16 L21 16 L21 10"
    readonly property string breath: "M11 8 A1 1 0 1 1 13 8 A1 1 0 1 1 11 8 M13 8 C13 11.5 12 14 9.5 16"
    readonly property string caesura: "M7 19 L12 5 M12 19 L17 5"

    // The beat: strums, pick strokes and tremolo picking.
    readonly property string strumDown: "M12 4 L12 20 M7 15 L12 20 L17 15"
    readonly property string strumUp: "M12 20 L12 4 M7 9 L12 4 L17 9"
    readonly property string pickDown: "M6 18 L6 7 L18 7 L18 18"
    readonly property string pickUp: "M7 6 L12 18 L17 6"
    readonly property string tremolo8: "M7 14 L17 10"
    readonly property string tremolo16: "M7 11 L17 7 M7 17 L17 13"
    readonly property string tremolo32: "M7 8 L17 4 M7 13 L17 9 M7 18 L17 14"

    // Small signs.
    readonly property string close: "M7 7 L17 17 M17 7 L7 17"
    readonly property string plus: "M12 6 L12 18 M6 12 L18 12"
    readonly property string minus: "M6 12 L18 12"
    readonly property string check: "M5.5 12.5 L10 17 L18.5 7.5"
    readonly property string chevronDown: "M7 10 L12 15 L17 10"
    readonly property string chevronRight: "M10 7 L15 12 L10 17"
}
