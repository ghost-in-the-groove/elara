// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import Elara.App

ApplicationWindow {
    id: window

    /// A file to open at start (from the command line).
    property url initialFile
    readonly property bool hasScore: score.pageCount > 0
    readonly property bool dark: Material.theme === Material.Dark

    width: 1280
    height: 860
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: hasScore ? qsTr("%1 — Elara").arg(score.title) : "Elara"
    color: dark ? "#17171c" : "#e9ebf0"

    Component.onCompleted: {
        if (initialFile.toString().length > 0)
            score.open(initialFile);
    }

    ScoreDocument {
        id: score

        // A new song starts at its top, at paper size when it fits.
        onSourceChanged: {
            scoreView.fitPaper();
            scoreView.toTop();
        }
        onStatusChanged: {
            if (status === ScoreDocument.Error)
                toast.show(errorMessage);
        }
    }

    AudioController {
        id: audio

        document: score
        onErrorMessageChanged: {
            if (errorMessage.length > 0)
                toast.show(errorMessage);
        }
    }

    /// "m:ss" for a number of seconds.
    function clock(seconds: real): string {
        const whole = Math.max(0, Math.floor(seconds));
        const rest = whole % 60;
        return Math.floor(whole / 60) + ":" + (rest < 10 ? "0" : "") + rest;
    }

    FileDialog {
        id: openDialog

        title: qsTr("Open a score")
        nameFilters: score.nameFilters
        onAccepted: score.open(selectedFile)
    }

    Dialog {
        id: about

        anchors.centerIn: parent
        modal: true
        title: qsTr("About Elara")
        standardButtons: Dialog.Close

        ColumnLayout {
            spacing: 8

            Label {
                text: qsTr("Version %1 (alpha)").arg(Qt.application.version)
            }
            Label {
                text: qsTr("Free software under the GNU AGPL 3.0 or later.")
                opacity: 0.7
            }
            Label {
                text: qsTr("Music font: Bravura (SIL OFL). Text font: Arimo (SIL OFL).")
                opacity: 0.7
            }
        }
    }

    header: ToolBar {
        Material.background: window.dark ? "#202027" : "#fbfbfd"
        Material.elevation: 2
        height: 56

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 8
            spacing: 6

            MusicSymbol {
                symbol: "\uE050" // G clef
                size: 28
                color: Material.accent
                Layout.alignment: Qt.AlignVCenter
            }

            Label {
                text: "Elara"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                Layout.rightMargin: 8
            }

            IconButton {
                iconPath: "M3 7 L3 18 Q3 19 4 19 L20 19 Q21 19 21 18 L21 9 Q21 8 20 8 L11.5 8 L9.5 6 L4 6 Q3 6 3 7 Z"
                tip: qsTr("Open a score (Ctrl+O)")
                onClicked: openDialog.open()
            }

            ToolSeparator {
                visible: window.hasScore
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                spacing: 0
                opacity: window.hasScore ? 1.0 : 0.0

                Behavior on opacity {
                    NumberAnimation {
                        duration: 200
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: score.title
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: score.artist
                    font.pixelSize: 12
                    opacity: 0.65
                    elide: Text.ElideRight
                }
            }

                        SegmentedControl {
                visible: window.hasScore
                labels: [qsTr("Notation"), qsTr("Tablature"), qsTr("Both")]
                enabledChoices: [true, score.hasTablature, score.hasTablature]
                currentIndex: !score.hasTablature ? 0 : score.view === ScoreDocument.Standard ? 0
                              : score.view === ScoreDocument.Tablature ? 1 : 2
                onActivated: index => score.view = [ScoreDocument.Standard,
                                                    ScoreDocument.Tablature,
                                                    ScoreDocument.Both][index]
            }

            ComboBox {
                id: trackPicker

                Layout.preferredWidth: 200
                Layout.leftMargin: 6
                visible: score.trackNames.length > 1
                model: [qsTr("All tracks")].concat(score.trackNames)
                                                onActivated: index => score.track = index - 1
                // Set after the model has been replaced: a new model resets
                // the index, which would undo a binding.
                onModelChanged: Qt.callLater(showTrack)

                function showTrack() {
                    currentIndex = score.track + 1;
                }

                Connections {
                    target: score

                    function onTrackChanged() {
                        trackPicker.showTrack();
                    }
                }
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Track")
            }

                        ToolSeparator {
                visible: window.hasScore
            }

            IconButton {
                visible: window.hasScore
                enabled: scoreView.zoom > scoreView.minZoom
                iconPath: "M15.5 10 A5.5 5.5 0 1 1 4.5 10 A5.5 5.5 0 1 1 15.5 10 M14 14 L20 20 M7.5 10 L12.5 10"
                tip: qsTr("Zoom out (Ctrl+-)")
                onClicked: scoreView.zoomOut()
            }

                        ToolButton {
                visible: window.hasScore
                text: Math.round(scoreView.zoom * 100) + "%"
                font.pixelSize: 13
                Layout.preferredWidth: 64
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Fit the page width (Ctrl+0)")
                onClicked: scoreView.fitWidth()
            }

                        IconButton {
                visible: window.hasScore
                enabled: scoreView.zoom < scoreView.maxZoom
                iconPath: "M15.5 10 A5.5 5.5 0 1 1 4.5 10 A5.5 5.5 0 1 1 15.5 10 M14 14 L20 20 M7.5 10 L12.5 10 M10 7.5 L10 12.5"
                tip: qsTr("Zoom in (Ctrl++)")
                onClicked: scoreView.zoomIn()
            }

            IconButton {
                id: moreButton

                iconPath: "M12 5.5 L12 5.6 M12 12 L12 12.1 M12 18.5 L12 18.6"
                tip: qsTr("More")
                onClicked: moreMenu.open()

                Menu {
                    id: moreMenu

                    y: moreButton.height

                    MenuItem {
                        text: audio.playing ? qsTr("Playing…") : qsTr("Play a test tone")
                        enabled: !audio.playing
                        onTriggered: audio.playTestTone(440, 1.5)
                    }
                    MenuItem {
                        text: qsTr("About Elara")
                        onTriggered: about.open()
                    }
                }
            }
        }
    }

    Item {
        id: content

        anchors.fill: parent

        WelcomePane {
            anchors.fill: parent
            opacity: window.hasScore ? 0.0 : 1.0
            visible: opacity > 0
            onOpenRequested: openDialog.open()

            Behavior on opacity {
                NumberAnimation {
                    duration: 220
                    easing.type: Easing.OutCubic
                }
            }
        }

        ScoreView {
            id: scoreView

            anchors.fill: parent
            document: score
            cursorSeconds: audio.playingSong || audio.position > 0 ? audio.position : -1
            followCursor: true
            onSeekRequested: seconds => audio.seek(seconds)
            cursorColor: Material.accent
            opacity: window.hasScore ? 1.0 : 0.0
            visible: opacity > 0

            Behavior on opacity {
                NumberAnimation {
                    duration: 260
                    easing.type: Easing.OutCubic
                }
            }
        }

        ProgressBar {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            indeterminate: true
            visible: score.busy
        }

        // Drop a file anywhere to open it.
        DropArea {
            id: drop

            anchors.fill: parent
            onEntered: drag => drag.accepted = drag.hasUrls
            onDropped: drop => {
                if (drop.hasUrls)
                    score.open(drop.urls[0]);
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 16
            radius: 16
            color: Qt.alpha(Material.accent, 0.08)
            border.color: Material.accent
            border.width: 2
            opacity: drop.containsDrag ? 1.0 : 0.0
            visible: opacity > 0

            Behavior on opacity {
                NumberAnimation {
                    duration: 140
                }
            }

            Label {
                anchors.centerIn: parent
                text: qsTr("Drop to open")
                font.pixelSize: 22
                font.weight: Font.DemiBold
                color: Material.accent
            }
        }

        // The transport: floats over the bottom of the score, and slides in
        // once there is one.
        RectangularShadow {
            anchors.fill: transport
            offset.y: 4
            radius: transport.radius
            blur: 22
            color: Qt.rgba(0, 0, 0, window.dark ? 0.5 : 0.18)
            visible: transport.visible
            opacity: transport.opacity
        }

        Rectangle {
            id: transport

            anchors.horizontalCenter: parent.horizontalCenter
            y: window.hasScore ? parent.height - height - 20 : parent.height + 12
            width: transportRow.implicitWidth + 28
            height: 56
            radius: 28
            color: window.dark ? "#26262e" : "#ffffff"
            border.color: window.dark ? "#34343e" : "#e1e4ea"
            opacity: window.hasScore ? 1.0 : 0.0
            visible: opacity > 0

            Behavior on y {
                NumberAnimation {
                    duration: 300
                    easing.type: Easing.OutCubic
                }
            }
            Behavior on opacity {
                NumberAnimation {
                    duration: 220
                }
            }

            RowLayout {
                id: transportRow

                anchors.centerIn: parent
                spacing: 10

                IconButton {
                    iconPath: "M6.5 6 L6.5 18 M18 6.5 L9.5 12 L18 17.5 Z"
                    tip: qsTr("Back to the start (Home)")
                    enabled: audio.playingSong || audio.position > 0
                    onClicked: audio.stopPlayback()
                }

                RoundButton {
                    id: playButton

                    implicitWidth: 44
                    implicitHeight: 44
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    padding: 0
                    focusPolicy: Qt.TabFocus
                    Accessible.name: audio.playingSong ? qsTr("Pause") : qsTr("Play")
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: (audio.playingSong ? qsTr("Pause") : qsTr("Play"))
                                  + qsTr(" (Space)")
                    onClicked: audio.togglePlayback()

                    background: Rectangle {
                        radius: width / 2
                        color: playButton.down ? Qt.darker(window.Material.accent, 1.15)
                                               : playButton.hovered ? Qt.lighter(window.Material.accent, 1.12)
                                                                    : window.Material.accent

                        Behavior on color {
                            ColorAnimation {
                                duration: 120
                            }
                        }
                    }

                    contentItem: Item {
                        Icon {
                            anchors.centerIn: parent
                            width: 22
                            height: 22
                            color: "white"
                            filled: true
                            lineWidth: 1.5
                            path: audio.playingSong ? "M7 5 L10 5 L10 19 L7 19 Z M14 5 L17 5 L17 19 L14 19 Z"
                                                    : "M8 5 L19 12 L8 19 Z"
                        }
                    }

                    // Gives a little under the pointer.
                    scale: pressed ? 0.94 : 1.0

                    Behavior on scale {
                        NumberAnimation {
                            duration: 120
                        }
                    }
                }

                Label {
                    text: window.clock(audio.position) + " / " + window.clock(score.duration)
                    font.pixelSize: 13
                    font.features: {
                        "tnum": 1
                    }
                    opacity: 0.8
                    Layout.preferredWidth: 86
                    horizontalAlignment: Text.AlignHCenter
                }

                Slider {
                    id: seekBar

                    Layout.preferredWidth: 240
                    from: 0
                    to: Math.max(0.001, score.duration)
                    focusPolicy: Qt.NoFocus
                    onMoved: audio.seek(value)

                    Binding {
                        target: seekBar
                        property: "value"
                        value: audio.position
                        when: !seekBar.pressed
                        restoreMode: Binding.RestoreNone
                    }
                }
            }
        }

        // Errors slide up from the bottom and go away by themselves.
        Rectangle {
            id: toast

            property string message

            function show(text: string) {
                message = text;
                shown = true;
                hideTimer.restart();
            }

            property bool shown: false

            anchors.horizontalCenter: parent.horizontalCenter
            y: shown ? parent.height - height - (window.hasScore ? transport.height + 36 : 24)
                     : parent.height + 8
            width: Math.min(parent.width - 48, toastText.implicitWidth + 40)
            height: toastText.implicitHeight + 24
            radius: 10
            color: window.dark ? "#3a2328" : "#fdecee"
            border.color: "#d9534f"
            visible: y < parent.height

            Behavior on y {
                NumberAnimation {
                    duration: 220
                    easing.type: Easing.OutCubic
                }
            }

            Label {
                id: toastText

                anchors.centerIn: parent
                width: Math.min(implicitWidth, toast.parent.width - 88)
                text: toast.message
                wrapMode: Text.Wrap
                color: window.dark ? "#ffb4b4" : "#a12622"
            }

            Timer {
                id: hideTimer

                interval: 6000
                onTriggered: toast.shown = false
            }

            MouseArea {
                anchors.fill: parent
                onClicked: toast.shown = false
            }
        }
    }

    footer: ToolBar {
        Material.background: window.dark ? "#1d1d23" : "#f4f5f8"
        height: 30
        visible: window.hasScore

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 18

            Label {
                text: qsTr("Page %1 of %2").arg(scoreView.currentPage).arg(score.pageCount)
                font.pixelSize: 12
                opacity: 0.75
            }
            Label {
                text: qsTr("%n measure(s)", "", score.measureCount)
                font.pixelSize: 12
                opacity: 0.75
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("Laid out in %1 ms").arg(score.layoutMilliseconds)
                font.pixelSize: 12
                opacity: 0.5
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: openDialog.open()
    }
    Shortcut {
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        enabled: window.hasScore
        onActivated: scoreView.zoomIn()
    }
    Shortcut {
        sequences: [StandardKey.ZoomOut]
        enabled: window.hasScore
        onActivated: scoreView.zoomOut()
    }
    Shortcut {
        sequence: "Space"
        enabled: window.hasScore
        onActivated: audio.togglePlayback()
    }
    Shortcut {
        sequence: "Home"
        enabled: window.hasScore
        onActivated: audio.stopPlayback()
    }
    Shortcut {
        sequence: "Ctrl+0"
        enabled: window.hasScore
        onActivated: scoreView.fitWidth()
    }
}
