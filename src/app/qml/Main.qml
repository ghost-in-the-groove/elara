// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import Elara.App

ApplicationWindow {
    id: window

    /// A file to open at start (from the command line).
    property url initialFile
    /// Open the tuner at start (from the command line).
    property bool showTuner: false
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
        if (showTuner)
            tunerPane.open();
    }

    function toggleTuner() {
        if (tunerPane.visible)
            tunerPane.close();
        else
            tunerPane.open();
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
                // A tuning fork.
                iconPath: "M9 3 L9 10 Q9 14 12 14 Q15 14 15 10 L15 3 M12 14 L12 21"
                tip: qsTr("Tuner (Ctrl+Shift+T)")
                onClicked: window.toggleTuner()
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

        TunerPane {
            id: tunerPane

            x: parent.width - width - 16
            y: 12
            dark: window.dark
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
            y: shown ? parent.height - height - 24 : parent.height + 8
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
        sequence: "Ctrl+Shift+T"
        onActivated: window.toggleTuner()
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
        sequence: "Ctrl+0"
        enabled: window.hasScore
        onActivated: scoreView.fitWidth()
    }
}
