// SPDX-License-Identifier: AGPL-3.0-or-later
// The song as one string along the lower edge of the command bar: what has
// played is lit, the measure playing rides it as a fret number rides a string
// of tablature, the measures are ticks, the sections inlays and the A-B loop
// a band; a taller tick stands at the caret's measure. A click or a drag
// moves playback there.
import QtQuick
import QtQuick.Controls
import Elara.App

Item {
    id: root

    /// Where playback stands and how long the song lasts, in seconds.
    property real position: 0
    property real duration: 0
    /// The A-B loop: A marked, or A to B looping.
    property bool loopMarked: false
    property bool looping: false
    property real loopFrom: 0
    property real loopTo: 0
    /// Where each section starts: a list of { name, seconds }.
    property var sections: []
    /// What rides the string at the position: the number of the measure there.
    property string marker
    /// Where the caret's measure starts, in seconds: a taller tick; negative
    /// for none.
    property real caretSeconds: -1
    /// Where the measures played start, in seconds.
    property var measureStarts: []
    /// A tick every so many measures, so that they stand apart.
    readonly property int tickStep: {
        const room = Math.max(1, Math.floor(width / 12));
        let step = 1;
        while (measureStarts.length / step > room)
            step *= 2;
        return step;
    }
    readonly property var ticks: measureStarts.filter((start, index) => index > 0
                                                                        && index % tickStep === 0)
    readonly property int tickCount: ticks.length
    /// Whether playback has left the start: the string is lit up to it then.
    readonly property bool moved: position > 0

    signal seekRequested(real seconds)

    readonly property real span: Math.max(0.001, duration)
    /// Room at both ends, so that the marker never leaves the window.
    readonly property real inset: 14
    readonly property real reach: Math.max(1, width - 2 * inset)
    /// The middle of the string, from the top of this item.
    readonly property real lineY: height / 2

    function xAt(seconds: real): real {
        return inset + reach * Math.max(0, Math.min(1, seconds / span));
    }
    function secondsAt(x: real): real {
        return span * Math.max(0, Math.min(1, (x - inset) / reach));
    }
    /// "m:ss" for a number of seconds.
    function clock(seconds: real): string {
        const whole = Math.max(0, Math.floor(seconds));
        const rest = whole % 60;
        return Math.floor(whole / 60) + ":" + (rest < 10 ? "0" : "") + rest;
    }
    /// The section playing at `seconds`, or nothing.
    function sectionAt(seconds: real): string {
        let name = "";
        for (const section of sections) {
            if (section.seconds <= seconds + 1e-6)
                name = section.name;
        }
        return name;
    }

    implicitHeight: 16
    Accessible.role: Accessible.Slider
    Accessible.name: qsTr("Position in the song")

    // The string, thicker under the pointer.
    Rectangle {
        id: string

        property real thickness: area.containsMouse || area.pressed ? 4 : 2

        width: parent.width
        y: root.lineY - height / 2
        height: thickness
        color: Qt.alpha(Theme.lilac, 0.5)

        Behavior on thickness {
            NumberAnimation {
                duration: Theme.quick
                easing.type: Easing.OutCubic
            }
        }
    }

    // A tick where each measure starts.
    Repeater {
        model: root.enabled ? root.ticks : []

        delegate: Rectangle {
            required property real modelData

            x: Math.round(root.xAt(modelData))
            y: root.lineY - 5
            width: 1
            height: 4
            color: Qt.alpha(Theme.lilac, 0.55)
        }
    }

    // Where the caret's measure starts.
    Rectangle {
        objectName: "caretTick"

        visible: root.enabled && root.caretSeconds >= 0
        x: Math.round(root.xAt(root.caretSeconds)) - 1
        y: root.lineY - 8
        width: 2
        height: 7
        radius: 1
        color: Theme.pearl
    }

    // What has played.
    Rectangle {
        visible: root.enabled && root.moved
        width: root.xAt(root.position)
        y: string.y
        height: string.height
        color: Theme.lilac
    }

    // The A-B loop: A to B, or a mark at A while B is still to come.
    Rectangle {
        objectName: "loopBand"

        visible: root.enabled && (root.looping || root.loopMarked)
        x: root.xAt(root.loopFrom) - (root.looping ? 0 : 1)
        width: root.looping ? Math.max(2, root.xAt(root.loopTo) - root.xAt(root.loopFrom)) : 2
        y: root.lineY - height / 2
        height: 8
        radius: root.looping ? 2 : 1
        color: Qt.alpha(Theme.pearl, root.looping ? 0.34 : 0.8)
        border.width: root.looping ? 1 : 0
        border.color: Qt.alpha(Theme.pearl, 0.8)
    }

    // The sections, inlaid where each starts.
    Repeater {
        model: root.enabled ? root.sections : []

        delegate: Rectangle {
            required property var modelData

            x: root.xAt(modelData.seconds) - width / 2
            y: root.lineY - height / 2
            width: 6
            height: 6
            radius: 3
            color: Theme.pearl
        }
    }

    // The measure at the position, on the string: outlined while the song
    // stands at its start, filled once playback has moved.
    Rectangle {
        readonly property bool lit: root.moved || area.pressed

        visible: root.enabled && root.marker.length > 0
        x: Math.round(root.xAt(root.position) - width / 2)
        y: Math.round(root.lineY - height / 2)
        width: Math.max(22, markerLabel.implicitWidth + 10)
        height: 14
        radius: 4
        color: lit ? Theme.accent : Theme.night
        border.width: lit ? 0 : 1
        border.color: Theme.lilac

        Label {
            id: markerLabel

            anchors.centerIn: parent
            text: root.marker
            font.pixelSize: 10
            font.weight: Font.Bold
            font.features: {
                "tnum": 1
            }
            color: parent.lit ? Theme.accentText : Theme.lilac
        }
    }

    // Under the pointer: the time there and the section it belongs to.
    Rectangle {
        id: hint

        readonly property real seconds: root.secondsAt(area.mouseX)
        readonly property string section: root.sectionAt(seconds)

        visible: root.enabled && (area.containsMouse || area.pressed)
        x: Math.max(4, Math.min(root.width - width - 4, area.mouseX - width / 2))
        y: root.lineY + 12
        width: hintLabel.implicitWidth + 14
        height: 22
        radius: Theme.radiusSmall
        color: Theme.night
        border.color: Theme.nightLine

        Label {
            id: hintLabel

            anchors.centerIn: parent
            text: root.clock(hint.seconds) + (hint.section.length > 0 ? "  ·  " + hint.section : "")
            font.pixelSize: Theme.fontCaption
            font.features: {
                "tnum": 1
            }
            color: Theme.pearl
        }
    }

    MouseArea {
        id: area

        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        preventStealing: true
        onPressed: mouse => root.seekRequested(root.secondsAt(mouse.x))
        onPositionChanged: mouse => {
            if (pressed)
                root.seekRequested(root.secondsAt(mouse.x));
        }
    }
}
