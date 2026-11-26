// SPDX-License-Identifier: AGPL-3.0-or-later
// The tuner, docked at the right of the score where the song panel also
// docks: the note heard, a needle for how far it is from in tune and the
// strings of the instrument. It listens only while it is open.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import Elara.App

Rectangle {
    id: root

    /// The score whose shown track the tuner follows; none without one.
    property ScoreDocument document: null
    /// Whether it shows: it listens only then.
    property bool opened: false
    /// Its width while it shows.
    readonly property real fullWidth: 320

    signal closeRequested

    // With a tuning, the nearest string and how far it is from its pitch;
    // chromatic, the nearest note.
    readonly property bool byString: tuner.hasReading && tuner.stringIndex >= 0
                                     && tuner.stringIndex < tuner.stringNames.length
    readonly property string target: byString ? tuner.stringNames[tuner.stringIndex] : ""
    readonly property string shownName: byString ? target.replace(/-?[0-9]+$/, "")
                                                 : tuner.hasReading ? tuner.noteName : ""
    readonly property string shownOctave: byString ? target.replace(/^[^0-9-]+/, "")
                                                   : tuner.hasReading ? String(tuner.octave) : ""
    readonly property real offset: byString ? tuner.stringCents : tuner.cents
    readonly property bool inTune: tuner.hasReading && Math.abs(offset) < 5
    /// The arc and its ticks: clear of the panel in both themes.
    readonly property color scale: Qt.alpha(Theme.inkSoft, 0.55)

    // The track and tuning followed last, so that a tuning picked by hand
    // stays until the track or its tuning changes.
    property string followed
    // Its body is made the first time it opens.
    property bool made: false

    // Tunes to the shown track's strings, when it has strings and they
    // changed since the last time.
    function followTrack() {
        if (!document || document.track < 0)
            return;
        const shown = document.trackTuning(document.track);
        if (!shown.fretted)
            return;
        const key = document.track + " " + shown.notes;
        if (key === followed)
            return;
        followed = key;
        tuner.showTuning(shown.notes, document.trackNames[document.track]);
    }

    function signedCents(cents: real): string {
        const rounded = Math.round(cents * 10) / 10;
        return (rounded > 0 ? "+" : rounded < 0 ? "−" : "±") + Math.abs(rounded).toFixed(1);
    }

    function wake() {
        if (!opened)
            return;
        made = true;
        followTrack();
    }

    color: Theme.bar
    clip: true
    onOpenedChanged: wake()
    Component.onCompleted: wake()

    TunerController {
        id: tuner

        active: root.opened
    }

    Connections {
        target: root.document
        enabled: root.opened

        function onTrackChanged() {
            root.followTrack();
        }
        function onSongChanged() {
            root.followTrack();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 6
            Layout.preferredHeight: 40
            spacing: 8

            Label {
                text: qsTr("Tuner")
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                color: Theme.ink
            }

            // Whether the input is heard.
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                visible: tuner.listening
                width: 8
                height: 8
                radius: 4
                color: Theme.success
            }
            Label {
                Layout.fillWidth: true
                text: tuner.listening ? qsTr("Listening") : ""
                font.pixelSize: Theme.fontCaption
                color: Theme.inkSoft
                elide: Text.ElideRight
            }

            IconButton {
                implicitWidth: 28
                implicitHeight: 28
                padding: 6
                iconPath: Icons.close
                tip: qsTr("Close (Ctrl+Shift+T)")
                onClicked: root.closeRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.line
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.made
            sourceComponent: body
        }
    }

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.line
    }

    Component {
        id: body

        Flickable {
            contentWidth: width
            contentHeight: parts.implicitHeight + 28
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ScrollBar.vertical: Scroll {}

            ColumnLayout {
                id: parts

                x: 14
                y: 12
                width: root.fullWidth - 29
                spacing: 10

                // The gauge: -50 to +50 cents over 120 degrees.
                Item {
                    id: gauge

                    readonly property real centerY: 158
                    readonly property real radius: 136

                    Layout.fillWidth: true
                    Layout.preferredHeight: 114

                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer

                        ShapePath {
                            strokeColor: root.scale
                            strokeWidth: 5
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap

                            PathAngleArc {
                                centerX: gauge.width / 2
                                centerY: gauge.centerY
                                radiusX: gauge.radius
                                radiusY: gauge.radius
                                startAngle: -150
                                sweepAngle: 120
                            }
                        }
                        ShapePath {
                            strokeColor: Theme.success
                            strokeWidth: 5
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap

                            PathAngleArc {
                                centerX: gauge.width / 2
                                centerY: gauge.centerY
                                radiusX: gauge.radius
                                radiusY: gauge.radius
                                startAngle: -96
                                sweepAngle: 12
                            }
                        }
                    }

                    Repeater {
                        model: 21

                        Rectangle {
                            required property int index

                            x: gauge.width / 2 - width / 2
                            y: gauge.centerY - gauge.radius + 10
                            width: 2
                            height: index % 5 === 0 ? 11 : 6
                            radius: 1
                            color: root.scale
                            antialiasing: true
                            transform: Rotation {
                                origin.x: 1
                                origin.y: gauge.radius - 10
                                angle: (index - 10) * 6
                            }
                        }
                    }

                    Label {
                        x: gauge.width / 2 - gauge.radius * 0.87 - width / 2
                        y: gauge.centerY - gauge.radius * 0.5 + 6
                        text: "−50"
                        font.pixelSize: Theme.fontCaption
                        color: Theme.inkSoft
                    }
                    Label {
                        x: gauge.width / 2 + gauge.radius * 0.87 - width / 2
                        y: gauge.centerY - gauge.radius * 0.5 + 6
                        text: "+50"
                        font.pixelSize: Theme.fontCaption
                        color: Theme.inkSoft
                    }

                    // The note, inside the arc; before one is heard, what to do.
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 44
                        visible: tuner.hasReading
                        spacing: 2

                        Text {
                            text: root.shownName
                            font.pixelSize: 58
                            font.weight: Font.Bold
                            color: root.inTune ? Theme.success : Theme.ink

                            Behavior on color {
                                ColorAnimation {
                                    duration: Theme.calm
                                }
                            }
                        }
                        Text {
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 10
                            text: root.shownOctave
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                            color: Theme.inkSoft
                        }
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 78
                        visible: !tuner.hasReading
                        text: qsTr("Play a note")
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.DemiBold
                        color: Theme.inkSoft
                    }

                    // The marker slides along the arc.
                    Item {
                        id: marker

                        readonly property color tint: root.inTune ? Theme.success
                                                                  : tuner.hasReading ? Theme.warning
                                                                                     : root.scale

                        x: gauge.width / 2
                        y: gauge.centerY - gauge.radius
                        width: 0
                        height: gauge.radius
                        transformOrigin: Item.Bottom
                        rotation: tuner.hasReading ? Math.max(-50, Math.min(50, root.offset)) * 1.2 : 0

                        Behavior on rotation {
                            SpringAnimation {
                                spring: 3.5
                                damping: 0.32
                                epsilon: 0.05
                            }
                        }

                        Rectangle {
                            x: -width / 2
                            y: -height / 2
                            width: 22
                            height: 22
                            radius: 11
                            color: marker.tint
                            border.color: Theme.bar
                            border.width: 4

                            Behavior on color {
                                ColorAnimation {
                                    duration: Theme.calm
                                }
                            }
                        }
                        Rectangle {
                            x: -width / 2
                            y: 16
                            width: 2
                            height: 18
                            radius: 1
                            color: marker.tint
                        }
                    }
                }

                // How far, and which way to turn; its two lines keep their
                // room while nothing is heard.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        visible: tuner.hasReading
                        text: root.inTune ? qsTr("In tune")
                              : root.offset < 0 ? qsTr("%1 cents · tune up").arg(root.signedCents(root.offset))
                              : qsTr("%1 cents · tune down").arg(root.signedCents(root.offset))
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.DemiBold
                        color: root.inTune ? Theme.success : Theme.ink
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        visible: tuner.hasReading
                        text: root.byString
                              ? qsTr("Heard %1%2 · %3 Hz").arg(tuner.noteName).arg(tuner.octave)
                                .arg(tuner.frequency.toFixed(1))
                              : qsTr("%1 cents · %2 Hz").arg(root.signedCents(tuner.cents))
                                .arg(tuner.frequency.toFixed(1))
                        font.pixelSize: Theme.fontSmall
                        color: Theme.inkSoft
                    }
                    Item {
                        Layout.fillHeight: true
                    }
                }

                // The strings, lowest first: the one heard wears the chosen
                // state, green once it is in tune.
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 4
                    visible: tuner.stringNames.length > 0
                    spacing: 8

                    Repeater {
                        model: tuner.stringNames

                        Rectangle {
                            id: chip

                            required property int index
                            required property string modelData
                            readonly property bool current: root.byString && index === tuner.stringIndex
                            readonly property bool tuned: current && root.inTune

                            width: 34
                            height: 34
                            radius: 17
                            color: tuned ? Qt.alpha(Theme.success, 0.2)
                                         : current ? Theme.accentWash : "transparent"
                            border.color: tuned ? Theme.success
                                                : current ? Qt.alpha(Theme.accentInk, 0.5) : root.scale
                            border.width: current ? 1.5 : 1

                            Label {
                                anchors.centerIn: parent
                                text: chip.modelData.replace(/-?[0-9]+$/, "")
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: chip.current && !chip.tuned ? Theme.accentInk : Theme.ink
                            }

                            ToolTip.visible: chipArea.containsMouse
                            ToolTip.delay: 500
                            ToolTip.text: qsTr("String %1: %2").arg(tuner.stringNames.length - index)
                                          .arg(modelData)

                            MouseArea {
                                id: chipArea

                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }
                    }
                }

                // The whole width, so that long names ("Guitarra, un semitono
                // abajo") are not cut.
                Picker {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    model: tuner.tuningLabels
                    currentIndex: tuner.tuning
                    onActivated: index => tuner.tuning = index
                    Accessible.name: qsTr("Instrument and tuning")
                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Instrument and tuning")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Reference A4")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.inkSoft
                        elide: Text.ElideRight
                    }

                    Stepper {
                        Layout.preferredWidth: 140
                        from: 400
                        to: 480
                        value: tuner.a4
                        textFromValue: (value, locale) => qsTr("%1 Hz").arg(value)
                        valueFromText: (text, locale) => parseInt(text.replace(/[^0-9]/g, ""))
                        onValueModified: tuner.a4 = value
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Reference pitch of A4")
                    }
                }

                // How loud the input is.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    spacing: 8

                    Label {
                        text: qsTr("Input")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.inkSoft
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                        radius: 2
                        color: root.scale

                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, tuner.level))
                            height: parent.height
                            radius: 2
                            color: tuner.level > 0.97 ? Theme.danger : Theme.accentInk
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: tuner.errorMessage.length > 0 ? tuner.errorMessage : tuner.deviceDescription
                    color: tuner.errorMessage.length > 0 ? Theme.dangerInk : Theme.inkSoft
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    maximumLineCount: 3
                }
            }
        }
    }
}
