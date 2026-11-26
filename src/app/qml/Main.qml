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
    /// Open the lyrics of the caret's staff once the score is laid out (from
    /// the command line).
    property bool showLyrics: false
    // Open the note effects panel once there is a caret (`--effects`).
    property bool showEffects: false
    /// Open the first track's tuning in the song panel once there is a score
    /// (from the command line).
    property bool showTuning: false
    /// A SoundFont to play through (from the command line).
    property string initialSoundFont
    /// SFZ instruments for the guitars and the basses (from the command line).
    property string initialGuitarSfz
    property string initialBassSfz
    /// Whether choices such as the SoundFont are kept for the next start; off
    /// for checks and screenshots.
    property bool rememberSettings: true
    /// Where unsaved songs are copied while the app runs (ADR-0106).
    property string recoveryFolder
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
    /// What waits for the saves being written, asked for again once they are
    /// all on the disk.
    property var savedAction: null
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
        recent.remember = rememberSettings;
        if (initialFile.toString().length > 0)
            score.open(initialFile);
        if (showTuner)
            tunerPane.open();
        audio.rememberSound = rememberSettings;
        if (initialSoundFont.length > 0 || initialGuitarSfz.length > 0
                || initialBassSfz.length > 0) {
            if (initialSoundFont.length > 0)
                audio.setSoundFont(initialSoundFont);
            if (initialGuitarSfz.length > 0)
                audio.setSfz("guitars", initialGuitarSfz);
            if (initialBassSfz.length > 0)
                audio.setSfz("basses", initialBassSfz);
        } else if (rememberSettings) {
            audio.restoreSound();
        }
        recoveryDialog.offer(score.recoverable());
    }

    onClosing: close => {
        if (closing || (!score.saving && !score.modified))
            return;
        close.accepted = false;
        whenSaved(() => {
            window.closing = true;
            window.close();
        });
    }

    function toggleTuner() {
        if (tunerPane.visible)
            tunerPane.close();
        else
            tunerPane.open();
    }

    /// Runs `action` at once, or after asking whether to save the changes.
    /// While a save is written, decides once it is on the disk.
    function whenSaved(action) {
        if (score.saving) {
            savedAction = action;
            return;
        }
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

    /// Runs an edit asked for from a menu and brings the caret into view, as
    /// the keys do.
    function edit(action) {
        action();
        scoreView.reveal();
    }

    /// Saves, then runs `action` once the song is on the disk.
    function saveThen(action) {
        savedAction = action;
        save();
    }

    /// Saves to the song's own file, or asks where when it has none.
    function save() {
        if (score.savesInPlace)
            score.save(score.source);
        else
            saveDialog.open();
    }

    /// Asks where to export the score as PDF, suggesting a file beside the song's.
    function exportPdf() {
        pdfDialog.selectedFile = score.exportFile("pdf");
        pdfDialog.open();
    }

    /// Asks where to export the song as MusicXML, to take it to another
    /// notation program.
    function exportMusicXml() {
        musicXmlDialog.selectedFile = score.exportFile("musicxml");
        musicXmlDialog.open();
    }

    /// Asks where to export the song as a MIDI file, as it plays.
    function exportMidi() {
        midiDialog.selectedFile = score.exportFile("mid");
        midiDialog.open();
    }

    ScoreDocument {
        id: score

        recoveryFolder: window.recoveryFolder
        // A new song starts at its top, at paper size when it fits. A file
        // opened or saved goes to the top of the recent scores.
        onSourceChanged: {
            scoreView.fitPaper();
            scoreView.toTop();
            if (source.toString().length > 0)
                recent.add(source);
        }
        onStatusChanged: {
            if (status === ScoreDocument.Error)
                toast.show(errorMessage);
        }
        onEditFailed: message => toast.show(message)
        // What waited for the saves runs once the last one is written,
        // after asking again if the song changed meanwhile. A failed save
        // drops it.
        onSaveFinished: (file, saved) => {
            if (!saved)
                window.savedAction = null;
            if (score.saving || !window.savedAction)
                return;
            const action = window.savedAction;
            window.savedAction = null;
            window.whenSaved(action);
        }
        onNotice: message => toast.inform(message, [])
        onReport: (message, details) => toast.inform(message, details)
    }

    RecentScores {
        id: recent
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
        enabled: window.showMeasure || window.showLyrics || window.showEffects

        function onCaretChanged() {
            if (score.caretPage < 0) {
                return;
            }
            if (window.showMeasure) {
                window.showMeasure = false;
                scoreView.openMeasurePopupAtCaret();
            }
            if (window.showLyrics) {
                window.showLyrics = false;
                scoreView.openLyricsPopupAtCaret();
            }
            if (window.showEffects) {
                window.showEffects = false;
                entryBar.openEffects();
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
        id: soundFontDialog

        title: qsTr("Choose a SoundFont")
        nameFilters: [qsTr("SoundFonts (*.sf2 *.sf3)")]
        onAccepted: audio.setSoundFont(selectedFile)
    }

    FileDialog {
        id: sfzDialog

        /// "guitars" or "basses": the tracks the instrument chosen plays.
        property string tracks: "guitars"

        title: tracks === "guitars" ? qsTr("Choose an SFZ instrument for the guitars")
                                    : qsTr("Choose an SFZ instrument for the basses")
        nameFilters: [qsTr("SFZ instruments (*.sfz)")]
        onAccepted: audio.setSfz(tracks, selectedFile)
    }

    FileDialog {
        id: saveDialog

        title: qsTr("Save the song")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Elara (*.elara)")]
        defaultSuffix: "elara"
        onAccepted: score.save(selectedFile)
        onRejected: window.savedAction = null
    }

    FileDialog {
        id: pdfDialog

        title: qsTr("Export the score as PDF")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PDF documents (*.pdf)")]
        defaultSuffix: "pdf"
        onAccepted: score.exportPdf(selectedFile)
    }

    FileDialog {
        id: musicXmlDialog

        title: qsTr("Export the song as MusicXML")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("MusicXML (*.musicxml *.xml)"), qsTr("Compressed MusicXML (*.mxl)")]
        defaultSuffix: "musicxml"
        onAccepted: score.exportMusicXml(selectedFile)
    }

    FileDialog {
        id: midiDialog

        title: qsTr("Export the song as MIDI")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("MIDI files (*.mid *.midi)")]
        defaultSuffix: "mid"
        onAccepted: score.exportMidi(selectedFile)
    }

    // What an export left out, one line each.
    Dialog {
        id: detailsDialog

        property var lines: []

        anchors.centerIn: parent
        width: Math.min(560, window.width - 48)
        modal: true
        standardButtons: Dialog.Close

        ListView {
            implicitHeight: Math.min(contentHeight, window.height * 0.5)
            width: detailsDialog.availableWidth
            clip: true
            spacing: 6
            model: detailsDialog.lines
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Label {
                required property string modelData

                width: ListView.view.width
                text: "• " + modelData
                wrapMode: Text.Wrap
                opacity: 0.85
            }
        }
    }

    Dialog {
        id: unsavedDialog

        objectName: "unsavedDialog"
        anchors.centerIn: parent
        // A fixed width: three buttons and a label would size each other.
        width: Math.min(460, window.width - 48)
        modal: true
        title: qsTr("Save the changes?")
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        onAccepted: {
            const action = window.pendingAction;
            window.pendingAction = null;
            window.saveThen(action);
        }
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

    RecoveryDialog {
        id: recoveryDialog

        objectName: "recoveryDialog"
        onRecoverRequested: file => window.whenSaved(() => score.recover(file))
        onDiscardRequested: file => score.discardRecovery(file)
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
    // The command bar with the menus and the transport, the song string along
    // its lower edge, and the tool row under it while a score is open.
    header: Item {
        implicitHeight: commandBar.height + (window.hasScore ? toolRow.height : 0)

        CommandBar {
            id: commandBar

            width: parent.width
            document: score
            playback: audio
            hasScore: window.hasScore
            songPaneShown: window.showSongPane
            onOpenRequested: window.openScore()
            onSaveRequested: window.save()
            onSongPaneToggled: window.showSongPane = !window.showSongPane
            onTunerRequested: window.toggleTuner()

            menus: AppMenuBar {
                AppMenu {
                    title: qsTr("&File")

                    AppMenuItem {
                        text: qsTr("New song")
                        keys: KeyNames.native("Ctrl+N")
                        onTriggered: window.newSong()
                    }
                    AppMenuItem {
                        text: qsTr("Open…")
                        keys: KeyNames.native("Ctrl+O")
                        onTriggered: window.openScore()
                    }
                    AppMenu {
                        id: recentMenu

                        title: qsTr("Open recent")
                        enabled: recent.scores.length > 0

                        Instantiator {
                            model: recent.scores

                            delegate: AppMenuItem {
                                required property var modelData

                                text: modelData.name
                                onTriggered: window.whenSaved(() => score.open(modelData.url))
                            }
                            onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                        }
                        AppMenuSeparator {}
                        AppMenuItem {
                            text: qsTr("Clear the list")
                            onTriggered: recent.clear()
                        }
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: qsTr("Save")
                        keys: KeyNames.native("Ctrl+S")
                        enabled: window.hasScore && (score.modified || !score.savesInPlace)
                        onTriggered: window.save()
                    }
                    AppMenuItem {
                        text: qsTr("Save as…")
                        keys: KeyNames.native("Ctrl+Shift+S")
                        enabled: window.hasScore
                        onTriggered: saveDialog.open()
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        // The score as shown, on A4 pages (ADR-0074).
                        text: qsTr("Export as PDF…")
                        keys: KeyNames.native("Ctrl+P")
                        enabled: window.hasScore
                        onTriggered: window.exportPdf()
                    }
                    AppMenuItem {
                        text: qsTr("Export as MusicXML…")
                        enabled: window.hasScore
                        onTriggered: window.exportMusicXml()
                    }
                    AppMenuItem {
                        text: qsTr("Export as MIDI…")
                        enabled: window.hasScore
                        onTriggered: window.exportMidi()
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: qsTr("Quit")
                        keys: KeyNames.native("Ctrl+Q")
                        onTriggered: window.close()
                    }
                }

                AppMenu {
                    title: qsTr("&Edit")

                    AppMenuItem {
                        text: score.canUndo ? qsTr("Undo %1").arg(score.undoName) : qsTr("Undo")
                        keys: KeyNames.native("Ctrl+Z")
                        enabled: score.canUndo
                        onTriggered: score.undo()
                    }
                    AppMenuItem {
                        text: score.canRedo ? qsTr("Redo %1").arg(score.redoName) : qsTr("Redo")
                        keys: KeyNames.native("Ctrl+Y")
                        enabled: score.canRedo
                        onTriggered: score.redo()
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: qsTr("Copy the measures")
                        keys: KeyNames.native("Ctrl+C")
                        enabled: window.hasScore
                        onTriggered: score.copy()
                    }
                    AppMenuItem {
                        text: qsTr("Paste over")
                        keys: KeyNames.native("Ctrl+V")
                        enabled: window.hasScore && score.canPaste
                        onTriggered: window.edit(() => score.paste(false))
                    }
                    AppMenuItem {
                        text: qsTr("Paste as new measures")
                        keys: KeyNames.native("Ctrl+Shift+V")
                        enabled: window.hasScore && score.canPaste
                        onTriggered: window.edit(() => score.paste(true))
                    }
                    AppMenuItem {
                        text: qsTr("Select every measure")
                        keys: KeyNames.native("Ctrl+A")
                        enabled: window.hasScore
                        onTriggered: score.selectAll()
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: qsTr("Insert a measure")
                        keys: KeyNames.native("Ctrl+Ins")
                        enabled: score.caretPage >= 0
                        onTriggered: window.edit(() => score.insertMeasure(false))
                    }
                    AppMenuItem {
                        text: qsTr("Delete the measure")
                        keys: KeyNames.native("Ctrl+Shift+Del")
                        enabled: score.caretPage >= 0
                        onTriggered: window.edit(() => score.removeMeasure())
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: qsTr("Measure…")
                        keys: KeyNames.native("Ctrl+M")
                        enabled: score.caretPage >= 0
                        onTriggered: scoreView.openMeasurePopupAtCaret()
                    }
                    AppMenuItem {
                        text: qsTr("Chord symbol…")
                        keys: KeyNames.native("Ctrl+K")
                        enabled: score.caretPage >= 0
                        onTriggered: scoreView.openChordPopupAtCaret()
                    }
                    AppMenuItem {
                        text: qsTr("Text…")
                        keys: KeyNames.native("Ctrl+T")
                        enabled: score.caretPage >= 0
                        onTriggered: scoreView.openTextPopupAtCaret()
                    }
                    AppMenuItem {
                        text: qsTr("Lyrics…")
                        keys: KeyNames.native("Ctrl+Shift+L")
                        enabled: score.caretPage >= 0
                        onTriggered: scoreView.openLyricsPopupAtCaret()
                    }
                }

                AppMenu {
                    title: qsTr("&View")

                    AppMenuItem {
                        text: qsTr("Notation")
                        enabled: window.hasScore
                        chosen: window.hasScore && (score.view === ScoreDocument.Standard
                                                    || !score.hasTablature)
                        onTriggered: score.view = ScoreDocument.Standard
                    }
                    AppMenuItem {
                        text: qsTr("Tablature")
                        enabled: window.hasScore && score.hasTablature
                        chosen: enabled && score.view === ScoreDocument.Tablature
                        onTriggered: score.view = ScoreDocument.Tablature
                    }
                    AppMenuItem {
                        text: qsTr("Both")
                        enabled: window.hasScore && score.hasTablature
                        chosen: enabled && score.view === ScoreDocument.Both
                        onTriggered: score.view = ScoreDocument.Both
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: qsTr("Page view")
                        enabled: window.hasScore
                        chosen: window.hasScore && !score.continuous
                        onTriggered: score.continuous = false
                    }
                    AppMenuItem {
                        text: qsTr("Continuous view")
                        enabled: window.hasScore
                        chosen: window.hasScore && score.continuous
                        onTriggered: score.continuous = true
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: qsTr("Zoom in")
                        keys: KeyNames.native("Ctrl++")
                        enabled: window.hasScore && scoreView.zoom < scoreView.maxZoom
                        onTriggered: scoreView.zoomIn()
                    }
                    AppMenuItem {
                        text: qsTr("Zoom out")
                        keys: KeyNames.native("Ctrl+-")
                        enabled: window.hasScore && scoreView.zoom > scoreView.minZoom
                        onTriggered: scoreView.zoomOut()
                    }
                    AppMenuItem {
                        text: qsTr("Fit the page width")
                        keys: KeyNames.native("Ctrl+0")
                        enabled: window.hasScore
                        onTriggered: scoreView.fitWidth()
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: qsTr("Song and tracks")
                        keys: KeyNames.native("Ctrl+I")
                        enabled: window.hasScore
                        chosen: window.hasScore && window.showSongPane
                        onTriggered: window.showSongPane = !window.showSongPane
                    }
                    AppMenuItem {
                        text: qsTr("Tuner")
                        keys: KeyNames.native("Ctrl+Shift+T")
                        chosen: tunerPane.visible
                        onTriggered: window.toggleTuner()
                    }
                }

                AppMenu {
                    title: qsTr("&Sound")

                    AppMenuItem {
                        text: audio.playingSong ? qsTr("Pause") : qsTr("Play")
                        keys: KeyNames.native("Space")
                        enabled: window.hasScore
                        onTriggered: audio.togglePlayback()
                    }
                    AppMenuItem {
                        text: qsTr("Back to the start")
                        keys: KeyNames.native("Home")
                        enabled: window.hasScore && (audio.playingSong || audio.position > 0)
                        onTriggered: audio.stopPlayback()
                    }
                    AppMenuItem {
                        text: audio.looping ? qsTr("End the A-B loop")
                                            : audio.loopMarked ? qsTr("Mark B of the loop")
                                                               : qsTr("Mark A of the loop")
                        keys: KeyNames.native("Ctrl+L")
                        enabled: window.hasScore
                        onTriggered: audio.markLoop()
                    }
                    AppMenuItem {
                        text: qsTr("Metronome")
                        chosen: score.metronome
                        onTriggered: score.metronome = !score.metronome
                    }
                    AppMenuItem {
                        text: qsTr("Count-in")
                        chosen: audio.countIn
                        onTriggered: audio.countIn = !audio.countIn
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: audio.loadingSoundFont ? qsTr("Opening the SoundFont…")
                                                     : audio.soundFontName.length > 0
                                                       ? qsTr("SoundFont: %1").arg(audio.soundFontName)
                                                       : qsTr("Choose a SoundFont…")
                        enabled: !audio.loadingSoundFont
                        onTriggered: soundFontDialog.open()
                    }
                    AppMenuItem {
                        // The SoundFont that comes with Elara (ADR-0069), back
                        // after another sound was chosen.
                        visible: audio.includedSoundFont.length > 0
                        text: qsTr("Use the included SoundFont")
                        enabled: audio.soundFont !== audio.includedSoundFont
                                 && !audio.loadingSoundFont
                        onTriggered: audio.setSoundFont(audio.includedSoundFont)
                    }
                    AppMenuItem {
                        text: audio.guitarSfzName.length > 0
                              ? qsTr("Guitars: %1").arg(audio.guitarSfzName)
                              : qsTr("Choose an SFZ instrument for the guitars…")
                        enabled: !audio.loadingSoundFont
                        onTriggered: {
                            sfzDialog.tracks = "guitars";
                            sfzDialog.open();
                        }
                    }
                    AppMenuItem {
                        // The guitar SFZ instrument that comes with Elara
                        // (ADR-0072).
                        visible: audio.includedGuitarSfz.length > 0
                        text: qsTr("Use the included guitar")
                        enabled: audio.guitarSfz !== audio.includedGuitarSfz
                                 && !audio.loadingSoundFont
                        onTriggered: audio.setSfz("guitars", audio.includedGuitarSfz)
                    }
                    AppMenuItem {
                        text: audio.bassSfzName.length > 0
                              ? qsTr("Basses: %1").arg(audio.bassSfzName)
                              : qsTr("Choose an SFZ instrument for the basses…")
                        enabled: !audio.loadingSoundFont
                        onTriggered: {
                            sfzDialog.tracks = "basses";
                            sfzDialog.open();
                        }
                    }
                    AppMenuItem {
                        text: qsTr("Use the built-in synthesizer")
                        enabled: (audio.soundFont.length > 0 || audio.guitarSfz.length > 0
                                  || audio.bassSfz.length > 0) && !audio.loadingSoundFont
                        onTriggered: audio.useBuiltinSound()
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        text: audio.playing ? qsTr("Playing…") : qsTr("Play a test tone")
                        enabled: !audio.playing
                        onTriggered: audio.playTestTone(440, 1.5)
                    }
                }

                AppMenu {
                    title: qsTr("&Help")

                    AppMenuItem {
                        text: qsTr("About Elara")
                        onTriggered: about.open()
                    }
                }
            }
        }

        // The tool row: what typing uses at the left, what the score shows at
        // the right.
        Rectangle {
            id: toolRow

            /// What is left for the track picker once the tools and the view
            /// switch have their room.
            readonly property real spare: width - entryBar.implicitWidth - 44
                                          - (viewSwitch.visible ? viewSwitch.implicitWidth + 8 : 0)

            y: commandBar.height
            width: parent.width
            height: 46
            visible: window.hasScore
            color: Theme.bar

            EntryBar {
                id: entryBar

                objectName: "entryBar"
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 1
                document: score
                // Tools for the caret: there is none before a staff is shown.
                enabled: score.caretPage >= 0
                opacity: enabled ? 1.0 : 0.4
                onMeasureRequested: scoreView.openMeasurePopupAtCaret()
                onChordRequested: scoreView.openChordPopupAtCaret()
                onTextRequested: scoreView.openTextPopupAtCaret()
                onLyricsRequested: scoreView.openLyricsPopupAtCaret()
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 1
                spacing: 8

                Picker {
                    id: trackPicker

                    objectName: "trackPicker"
                    width: Math.max(112, Math.min(200, toolRow.spare))
                    visible: score.trackNames.length > 1
                    takesFocus: false
                    model: [qsTr("All tracks")].concat(score.trackNames)
                    onActivated: index => score.track = index - 1
                    // Set after the model has been replaced: a new model resets
                    // the index, which would undo a binding.
                    onModelChanged: Qt.callLater(showTrack)
                    Accessible.name: qsTr("Track")

                    function showTrack() {
                        currentIndex = score.track + 1;
                    }

                    Connections {
                        target: score

                        function onTrackChanged() {
                            trackPicker.showTrack();
                        }
                    }
                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Track")
                }

                SegmentedControl {
                    id: viewSwitch

                    objectName: "viewSwitch"
                    // In a narrow window the View menu has the same choices.
                    visible: toolRow.width >= entryBar.implicitWidth + implicitWidth + 44
                                              + (trackPicker.visible ? 120 : 0)
                    labels: [qsTr("Notation"), qsTr("Tablature"), qsTr("Both")]
                    enabledChoices: [true, score.hasTablature, score.hasTablature]
                    currentIndex: !score.hasTablature ? 0 : score.view === ScoreDocument.Standard ? 0
                                  : score.view === ScoreDocument.Tablature ? 1 : 2
                    onActivated: index => score.view = [ScoreDocument.Standard,
                                                        ScoreDocument.Tablature,
                                                        ScoreDocument.Both][index]
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.line
            }
        }

        SongString {
            id: songString

            objectName: "songString"
            width: parent.width
            // Its line is the last two pixels of the command bar.
            y: commandBar.height - height / 2 - 1
            enabled: window.hasScore
            position: audio.position
            duration: score.duration
            loopMarked: audio.loopMarked
            looping: audio.looping
            loopFrom: audio.loopFrom
            loopTo: audio.loopTo
            sections: score.sectionMarks
            marker: window.hasScore ? commandBar.playingMeasure : ""
            onSeekRequested: seconds => audio.seek(seconds)
        }
    }

    Item {
        id: content

        anchors.fill: parent

        WelcomePane {
            anchors.fill: parent
            opacity: window.hasScore ? 0.0 : 1.0
            visible: opacity > 0
            recentScores: recent.scores
            onOpenRequested: window.openScore()
            onNewRequested: window.newSong()
            onRecentRequested: file => window.whenSaved(() => score.open(file))
            onForgetRequested: file => recent.remove(file)

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.calm
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
            opacity: window.hasScore ? 1.0 : 0.0
            visible: opacity > 0

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.calm
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
                    duration: Theme.calm
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
                    duration: Theme.quick
                }
            }

            Label {
                anchors.centerIn: parent
                text: qsTr("Drop to open")
                font.pixelSize: 20
                font.weight: Font.DemiBold
                color: Theme.accentInk
            }
        }

        TunerPane {
            id: tunerPane

            document: score
            x: parent.width - width - 16
            y: 12
        }

        // Errors and notices slide up from the bottom and go away by
        // themselves; a notice with details offers them.
        Rectangle {
            id: toast

            property string message
            property bool error: true
            property var details: []

            /// An error, in the danger colours.
            function show(text: string) {
                message = text;
                error = true;
                details = [];
                shown = true;
                hideTimer.restart();
            }

            /// Something done, with `lines` to read on demand.
            function inform(text: string, lines: var) {
                message = text;
                error = false;
                details = lines;
                shown = true;
                hideTimer.restart();
            }

            property bool shown: false

            anchors.horizontalCenter: parent.horizontalCenter
            y: shown ? parent.height - height - 20 : parent.height + 8
            width: Math.min(parent.width - 48, toastRow.implicitWidth + 36)
            height: toastRow.implicitHeight + 20
            radius: Theme.radiusCard
            color: error ? Theme.dangerSurface : Theme.surface
            border.color: error ? Theme.danger : Theme.line
            visible: y < parent.height

            Elevation {
                radius: parent.radius
            }

            Behavior on y {
                NumberAnimation {
                    duration: Theme.calm
                    easing.type: Easing.OutCubic
                }
            }

            Timer {
                id: hideTimer

                interval: toast.details.length > 0 ? 10000 : 6000
                onTriggered: toast.shown = false
            }

            MouseArea {
                anchors.fill: parent
                onClicked: toast.shown = false
            }

            RowLayout {
                id: toastRow

                anchors.centerIn: parent
                spacing: 12

                Label {
                    id: toastText

                    Layout.maximumWidth: toast.parent.width - 88 - (detailsButton.visible
                                                                    ? detailsButton.implicitWidth + 12 : 0)
                    text: toast.message
                    wrapMode: Text.Wrap
                    color: toast.error ? Theme.dangerInk : Theme.ink
                }
                Button {
                    id: detailsButton

                    visible: toast.details.length > 0
                    flat: true
                    text: qsTr("Details")
                    Material.foreground: Theme.accentInk
                    onClicked: {
                        detailsDialog.title = toast.message;
                        detailsDialog.lines = toast.details;
                        toast.shown = false;
                        detailsDialog.open();
                    }
                }
            }
        }
    }

    footer: StatusBar {
        visible: window.hasScore
        document: score
        view: scoreView
        onImportWarningsRequested: {
            detailsDialog.title = qsTr("What opening the file changed or left out");
            detailsDialog.lines = score.importWarnings;
            detailsDialog.open();
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
        sequence: "Ctrl+Q"
        onActivated: window.close()
    }
    Shortcut {
        sequences: [StandardKey.Print]
        enabled: window.hasScore
        onActivated: window.exportPdf()
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
        sequence: "Ctrl+L"
        enabled: window.hasScore
        onActivated: audio.markLoop()
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
