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
    /// The song panel at the right of the score.
    property bool showSongPane: false
    /// Open the caret's measure once the score is laid out (from the command line).
    property bool showMeasure: false
    /// Open the first track's tuning in the song panel once there is a score
    /// (from the command line).
    property bool showTuning: false
    readonly property bool hasScore: score.pageCount > 0
    readonly property bool dark: Material.theme === Material.Dark

    onHasScoreChanged: {
        if (hasScore && showTuning) {
            showTuning = false;
            showSongPane = true;
            songPane.openTuning(0);
        }
    }
    /// What to do once unsaved changes are saved or discarded.
    property var pendingAction: null
    property bool closing: false

    width: 1280
    height: 860
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: hasScore ? qsTr("%1 — Elara").arg((score.modified ? "• " : "") + score.title) : "Elara"
    color: Theme.desk

    // Every surface takes its colours from the theme.
    Binding {
        target: Theme
        property: "dark"
        value: window.dark
    }

    Component.onCompleted: {
        if (initialFile.toString().length > 0)
            score.open(initialFile);
        if (showTuner)
            tunerPane.open();
    }

    onClosing: close => {
        if (score.modified && !closing) {
            close.accepted = false;
            whenSaved(() => {
                window.closing = true;
                window.close();
            });
        }
    }

    function toggleTuner() {
        if (tunerPane.visible)
            tunerPane.close();
        else
            tunerPane.open();
    }

    /// Runs `action` at once, or after asking whether to save the changes.
    function whenSaved(action) {
        if (!score.modified) {
            action();
            return;
        }
        pendingAction = action;
        unsavedDialog.open();
    }

    function runPendingAction() {
        const action = pendingAction;
        pendingAction = null;
        if (action)
            action();
    }

    function openScore() {
        whenSaved(() => openDialog.open());
    }

    function newSong() {
        whenSaved(() => {
            score.newSong("electric-guitar");
            window.showSongPane = true;
        });
    }

    /// Saves to the song's own file, or asks where when it has none.
    function save() {
        if (score.savesInPlace) {
            if (score.save(score.source))
                runPendingAction();
        } else {
            saveDialog.open();
        }
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
        onEditFailed: message => toast.show(message)
        onNotice: message => toast.show(message)
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

    Connections {
        target: score
        enabled: window.showMeasure

        function onCaretChanged() {
            if (score.caretPage >= 0) {
                window.showMeasure = false;
                scoreView.openMeasurePopupAtCaret();
            }
        }
    }

    FileDialog {
        id: openDialog

        title: qsTr("Open a score")
        nameFilters: score.nameFilters
        onAccepted: score.open(selectedFile)
    }

    FileDialog {
        id: saveDialog

        title: qsTr("Save the song")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Elara (*.elara)")]
        defaultSuffix: "elara"
        onAccepted: {
            if (score.save(selectedFile))
                window.runPendingAction();
        }
        onRejected: window.pendingAction = null
    }

    Dialog {
        id: unsavedDialog

        anchors.centerIn: parent
        // A fixed width: three buttons and a label would size each other.
        width: Math.min(460, window.width - 48)
        modal: true
        title: qsTr("Save the changes?")
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        onAccepted: window.save()
        onDiscarded: {
            close();
            window.runPendingAction();
        }
        onRejected: window.pendingAction = null

        Label {
            width: unsavedDialog.availableWidth
            text: qsTr("“%1” has changes that are not saved.").arg(score.title)
            wrapMode: Text.Wrap
        }
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

    // Flat, with a hairline under it: the score is what has depth.
    header: ToolBar {
        Material.background: Theme.bar
        Material.foreground: Theme.ink
        Material.elevation: 0
        height: 56

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.line
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 10
            spacing: 4

            Logo {
                Layout.preferredHeight: 32
                Layout.preferredWidth: 32 * aspect
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: 10
                detailed: false
                // Only the e in a narrow window: the song's title needs the room.
                named: window.width >= 1000
                color: Theme.ink
                stringColor: Theme.accentInk
                ground: Theme.bar
            }

            IconButton {
                // A sheet with a plus.
                iconPath: "M6 3 L14 3 L19 8 L19 21 L6 21 Z M14 3 L14 8 L19 8 M12.5 11 L12.5 17 M9.5 14 L15.5 14"
                tip: qsTr("New song (Ctrl+N)")
                onClicked: window.newSong()
            }

            IconButton {
                iconPath: "M3 7 L3 18 Q3 19 4 19 L20 19 Q21 19 21 18 L21 9 Q21 8 20 8 L11.5 8 L9.5 6 L4 6 Q3 6 3 7 Z"
                tip: qsTr("Open a score (Ctrl+O)")
                onClicked: window.openScore()
            }

            IconButton {
                visible: window.hasScore
                enabled: score.modified || !score.savesInPlace
                // A floppy disk.
                iconPath: "M5 4 L16 4 L20 8 L20 20 L4 20 L4 5 Q4 4 5 4 Z M8 4 L8 9 L15 9 L15 4 M8 20 L8 14 L16 14 L16 20"
                tip: qsTr("Save (Ctrl+S)")
                onClicked: window.save()
            }

            IconButton {
                visible: window.hasScore
                enabled: score.canUndo
                iconPath: "M9 14 L4 9 L9 4 M4 9 L14 9 Q20 9 20 15 Q20 20 14 20 L10 20"
                tip: score.canUndo ? qsTr("Undo %1 (Ctrl+Z)").arg(score.undoName) : qsTr("Undo (Ctrl+Z)")
                onClicked: score.undo()
            }

            IconButton {
                visible: window.hasScore
                enabled: score.canRedo
                iconPath: "M15 14 L20 9 L15 4 M20 9 L10 9 Q4 9 4 15 Q4 20 10 20 L14 20"
                tip: score.canRedo ? qsTr("Redo %1 (Ctrl+Y)").arg(score.redoName) : qsTr("Redo (Ctrl+Y)")
                onClicked: score.redo()
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
                    text: score.title + (score.modified ? "  •" : "")
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: score.artist
                    font.pixelSize: Theme.fontSmall
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

            IconButton {
                visible: window.hasScore
                // Three sliders.
                iconPath: "M4 7 L20 7 M4 12 L20 12 M4 17 L20 17 M9 5 L9 9 M15 10 L15 14 M7 15 L7 19"
                tip: qsTr("Song and tracks (Ctrl+I)")
                // Bound, not checkable: the panel also closes by itself.
                checked: window.showSongPane
                onClicked: window.showSongPane = !window.showSongPane
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
            onOpenRequested: window.openScore()
            onNewRequested: window.newSong()

            Behavior on opacity {
                NumberAnimation {
                    duration: 220
                    easing.type: Easing.OutCubic
                }
            }
        }

        ScoreView {
            id: scoreView

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: songPane.left
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

        // Slides in from the right; the score makes room for it.
        SongPane {
            id: songPane

            readonly property bool shown: window.showSongPane && window.hasScore

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: shown ? 340 : 0
            visible: width > 0
            clip: true
            document: score
            onCloseRequested: window.showSongPane = false

            Behavior on width {
                NumberAnimation {
                    duration: 220
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
                if (drop.hasUrls) {
                    const file = drop.urls[0];
                    window.whenSaved(() => score.open(file));
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 16
            radius: Theme.radiusSheet
            color: Theme.accentWash
            border.color: Theme.accentInk
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
                color: Theme.accentInk
            }
        }

        // The transport: floats over the bottom of the score, and slides in
        // once there is one.
        Rectangle {
            id: transport

            objectName: "transport"
            anchors.horizontalCenter: parent.horizontalCenter
            y: window.hasScore ? parent.height - height - 20 : parent.height + 12
            width: transportRow.implicitWidth + 28
            height: 56
            radius: 28
            color: Theme.surface
            border.color: Theme.line
            opacity: window.hasScore ? 1.0 : 0.0
            visible: opacity > 0

            Elevation {
                radius: parent.radius
            }

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
                    font.pixelSize: Theme.fontBody
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

        TunerPane {
            id: tunerPane

            x: parent.width - width - 16
            y: 12
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
            radius: Theme.radiusCard
            color: Theme.dangerSurface
            border.color: Theme.danger
            visible: y < parent.height

            Elevation {
                radius: parent.radius
            }

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
                color: Theme.dangerInk
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
        Material.background: Theme.bar
        Material.foreground: Theme.ink
        Material.elevation: 0
        height: 32
        visible: window.hasScore

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.line
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 10
            spacing: 10

            Label {
                text: qsTr("Page %1 of %2").arg(scoreView.currentPage).arg(score.pageCount)
                font.pixelSize: Theme.fontSmall
                font.features: {
                    "tnum": 1
                }
                opacity: 0.75
            }
            Rectangle {
                implicitWidth: 3
                implicitHeight: 3
                radius: 1.5
                color: Theme.ink
                opacity: 0.35
            }
            Label {
                text: qsTr("%n measure(s)", "", score.measureCount)
                font.pixelSize: Theme.fontSmall
                font.features: {
                    "tnum": 1
                }
                opacity: 0.75
            }
            Item {
                Layout.fillWidth: true
            }

            // Zoom, as one group: out, the level (a click fits the width), in.
            Rectangle {
                implicitWidth: zoomRow.implicitWidth
                implicitHeight: 24
                radius: height / 2
                color: Theme.hover

                Row {
                    id: zoomRow

                    anchors.centerIn: parent

                    IconButton {
                        implicitWidth: 24
                        implicitHeight: 24
                        padding: 4
                        enabled: scoreView.zoom > scoreView.minZoom
                        iconPath: "M15.5 10 A5.5 5.5 0 1 1 4.5 10 A5.5 5.5 0 1 1 15.5 10 M14 14 L20 20 M7.5 10 L12.5 10"
                        tip: qsTr("Zoom out (Ctrl+-)")
                        onClicked: scoreView.zoomOut()
                    }

                    AbstractButton {
                        id: zoomLevel

                        implicitWidth: 48
                        implicitHeight: 24
                        hoverEnabled: true
                        focusPolicy: Qt.TabFocus
                        text: Math.round(scoreView.zoom * 100) + "%"
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Fit the page width (Ctrl+0)")
                        onClicked: scoreView.fitWidth()

                        background: Rectangle {
                            radius: height / 2
                            color: zoomLevel.down ? Theme.pressed
                                                  : zoomLevel.hovered ? Theme.hover : "transparent"
                            border.width: zoomLevel.visualFocus ? 2 : 0
                            border.color: Theme.accentInk
                        }

                        contentItem: Label {
                            text: zoomLevel.text
                            font.pixelSize: Theme.fontSmall
                            font.features: {
                                "tnum": 1
                            }
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    IconButton {
                        implicitWidth: 24
                        implicitHeight: 24
                        padding: 4
                        enabled: scoreView.zoom < scoreView.maxZoom
                        iconPath: "M15.5 10 A5.5 5.5 0 1 1 4.5 10 A5.5 5.5 0 1 1 15.5 10 M14 14 L20 20 M7.5 10 L12.5 10 M10 7.5 L10 12.5"
                        tip: qsTr("Zoom in (Ctrl++)")
                        onClicked: scoreView.zoomIn()
                    }
                }
            }
        }
    }

    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: window.openScore()
    }
    Shortcut {
        sequences: [StandardKey.New]
        onActivated: window.newSong()
    }
    Shortcut {
        sequences: [StandardKey.Save]
        enabled: window.hasScore
        onActivated: window.save()
    }
    Shortcut {
        sequences: [StandardKey.SaveAs]
        enabled: window.hasScore
        onActivated: saveDialog.open()
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: score.canUndo
        onActivated: score.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo, "Ctrl+Y"]
        enabled: score.canRedo
        onActivated: score.redo()
    }
    Shortcut {
        sequence: "Ctrl+I"
        enabled: window.hasScore
        onActivated: window.showSongPane = !window.showSongPane
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
