// SPDX-License-Identifier: AGPL-3.0-or-later
// The tuner: a card with the note heard, a needle for how far it is from in
// tune and the strings of the instrument. It listens only while it is open.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Shapes
import Elara.App

Popup {
    id: root

    // With a tuning, the nearest string and how far it is from its pitch;
    // chromatic, the nearest note.
    readonly property bool byString: tuner.hasReading && tuner.stringIndex >= 0
                                     && tuner.stringIndex < tuner.stringNames.length
    readonly property string target: byString ? tuner.stringNames[tuner.stringIndex] : ""
    readonly property string shownName: byString ? target.replace(/-?[0-9]+$/, "")
                                                 : tuner.hasReading ? tuner.noteName : "–"
    readonly property string shownOctave: byString ? target.replace(/^[^0-9-]+/, "")
                                                   : tuner.hasReading ? String(tuner.octave) : ""
    readonly property real offset: byString ? tuner.stringCents : tuner.cents
    readonly property bool inTune: tuner.hasReading && Math.abs(offset) < 5
    readonly property color tuneColor: Theme.success
    readonly property color offColor: Theme.warning
    readonly property color textColor: Theme.ink
    readonly property color faintColor: Theme.faint

    /// The score whose shown track the tuner follows; none without one.
    property ScoreDocument document: null
    // The track and tuning followed last, so that a tuning picked by hand
    // stays until the track or its tuning changes.
    property string followed

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

    width: 360
    padding: 20
    topPadding: 14
    focus: true
    closePolicy: Popup.CloseOnEscape
    transformOrigin: Popup.TopRight

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 160
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                property: "scale"
                from: 0.94
                to: 1
                duration: 220
                easing.type: Easing.OutBack
            }
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: 120
            easing.type: Easing.InCubic
        }
    }

    background: Rectangle {
        radius: Theme.radiusSheet
        color: Theme.surface
        border.color: Theme.line

        Elevation {
            radius: parent.radius
        }
    }

    TunerController {
        id: tuner

        active: root.visible
    }

    onAboutToShow: followTrack()

    Connections {
        target: root.document
        enabled: root.visible

        function onTrackChanged() {
            root.followTrack();
        }
        function onSongChanged() {
            root.followTrack();
        }
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Tuner")
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
            }

            // Pulses while the input is heard.
            Rectangle {
                id: dot

                width: 8
                height: 8
                radius: 4
                color: tuner.listening ? root.tuneColor : root.faintColor
                Layout.alignment: Qt.AlignVCenter

                SequentialAnimation {
                    running: tuner.listening && root.visible
                    loops: Animation.Infinite
                    onRunningChanged: {
                        if (!running)
                            dot.opacity = 1;
                    }

                    NumberAnimation {
                        target: dot
                        property: "opacity"
                        to: 0.25
                        duration: 700
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation {
                        target: dot
                        property: "opacity"
                        to: 1
                        duration: 700
                        easing.type: Easing.InOutSine
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }

            IconButton {
                iconPath: "M7 7 L17 17 M17 7 L7 17"
                tip: qsTr("Close (Esc)")
                onClicked: root.close()
            }
        }

        // The gauge: -50 to +50 cents over 120 degrees.
        Item {
            id: gauge

            readonly property real centerY: 158
            readonly property real radius: 136

            Layout.fillWidth: true
            Layout.preferredHeight: 132

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: root.faintColor
                    strokeWidth: 6
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
                    strokeColor: root.tuneColor
                    strokeWidth: 6
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
                    color: root.faintColor
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
                font.pixelSize: 11
                opacity: 0.5
            }
            Label {
                x: gauge.width / 2 + gauge.radius * 0.87 - width / 2
                y: gauge.centerY - gauge.radius * 0.5 + 6
                text: "+50"
                font.pixelSize: 11
                opacity: 0.5
            }

            // The note, inside the arc.
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 44
                spacing: 2

                Text {
                    text: root.shownName
                    font.pixelSize: 58
                    font.weight: Font.Bold
                    color: root.inTune ? root.tuneColor : root.textColor
                    opacity: tuner.hasReading ? 1.0 : 0.35

                    Behavior on color {
                        ColorAnimation {
                            duration: 180
                        }
                    }
                }
                Text {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    text: root.shownOctave
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    color: root.textColor
                    opacity: 0.55
                }
            }

            // The marker slides along the arc.
            Item {
                id: marker

                readonly property color tint: root.inTune ? root.tuneColor
                                                          : tuner.hasReading ? root.offColor
                                                                             : root.faintColor

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
                    border.color: root.background.color
                    border.width: 4
                    scale: root.inTune ? 1.15 : 1.0

                    Behavior on color {
                        ColorAnimation {
                            duration: 180
                        }
                    }
                    Behavior on scale {
                        NumberAnimation {
                            duration: 180
                            easing.type: Easing.OutBack
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
                    opacity: 0.6
                }
            }
        }

        // How far, and which way to turn.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: !tuner.hasReading ? qsTr("Play a note")
                      : root.inTune ? qsTr("In tune")
                      : root.offset < 0 ? qsTr("%1 cents · tune up").arg(root.signedCents(root.offset))
                      : qsTr("%1 cents · tune down").arg(root.signedCents(root.offset))
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: root.inTune ? root.tuneColor : root.textColor
                opacity: tuner.hasReading ? 1.0 : 0.6
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
                font.pixelSize: 12
                opacity: 0.6
            }
        }

        // The strings, lowest first.
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

                    width: 36
                    height: 36
                    radius: 18
                    color: !current ? "transparent" : root.inTune ? root.tuneColor : Material.accent
                    border.color: current ? color : root.faintColor
                    border.width: 1.5

                    Behavior on color {
                        ColorAnimation {
                            duration: 150
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        text: chip.modelData.replace(/-?[0-9]+$/, "")
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: chip.current ? "white" : root.textColor
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

        // The whole width, so that long names ("Guitarra, un semitono abajo")
        // are not cut.
        ComboBox {
            Layout.fillWidth: true
            Layout.topMargin: 6
            model: tuner.tuningLabels
            currentIndex: tuner.tuning
            onActivated: index => tuner.tuning = index
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Instrument and tuning")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: qsTr("Reference A4")
                opacity: 0.7
                elide: Text.ElideRight
            }

            SpinBox {
                Layout.preferredWidth: 140
                from: 400
                to: 480
                value: tuner.a4
                editable: true
                textFromValue: (value, locale) => qsTr("%1 Hz").arg(value)
                valueFromText: (text, locale) => parseInt(text.replace(/[^0-9]/g, ""))
                onValueModified: tuner.a4 = value
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Reference pitch of A4")
            }
        }

        // Input level.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 4
            radius: 2
            color: root.faintColor

            Rectangle {
                width: parent.width * tuner.level
                height: parent.height
                radius: 2
                color: tuner.level > 0.97 ? Theme.danger : Material.accent

                Behavior on width {
                    NumberAnimation {
                        duration: 90
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: tuner.errorMessage.length > 0 ? tuner.errorMessage : tuner.deviceDescription
            color: tuner.errorMessage.length > 0 ? Theme.danger : root.textColor
            opacity: tuner.errorMessage.length > 0 ? 1.0 : 0.5
            font.pixelSize: 11
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 2
        }
    }
}
