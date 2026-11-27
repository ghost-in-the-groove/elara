// SPDX-License-Identifier: AGPL-3.0-or-later
// The pages of a score on a desk, or the strips of one endless page (the
// continuous view), scrolled vertically and zoomed smoothly.
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Elara.App

Item {
    id: root

    required property ScoreDocument document
    // What is at the document's caret, read and edited.
    required property CaretEditor editor

    /// 1 shows pages at their paper size (96 logical pixels per inch).
    property real zoom: 1.0
    readonly property real minZoom: 0.25
    readonly property real maxZoom: 4.0
    /// The page turned dark, its ink light (ADR-0114): for the dark theme.
    property bool darkPage: false
    readonly property color paper: darkPage ? Theme.nightPage : "white"
    readonly property color ink: darkPage ? Theme.nightInk : "black"
    /// Seconds into the song where the playback cursor stands; negative hides it.
    property real cursorSeconds: -1
    /// Scroll so that the cursor stays in view as it moves.
    property bool followCursor: false
    property color cursorColor: Theme.accent
    /// A click on the score asks to move playback there.
    signal seekRequested(real seconds)
    /// Where the cursor stands (ScoreDocument.cursorAt), empty when hidden.
    /// Reading pageCount makes it follow relayouts too.
    readonly property var cursor: cursorSeconds >= 0 && document.pageCount > 0
                                  ? document.cursorAt(cursorSeconds) : ({})
    readonly property bool cursorShown: cursor.page !== undefined
    /// The page under the middle of the view, from 1.
    readonly property int currentPage: Math.max(1, Math.min(document.pageCount,
                                                           Math.floor(centre) + 1))

    // Logical pixels per staff space at zoom 1.
    readonly property real unit: document.staffSpace * 96 / 25.4
    property real shownZoom: zoom
    readonly property real pageWidth: document.pageSize.width * unit * shownZoom
    readonly property real pageHeight: document.pageSize.height * unit * shownZoom
    /// Between pages; the strips of the continuous view touch (ADR-0057).
    readonly property real gap: document.showsStrips ? 0 : 24
    readonly property real sideMargin: 32
    // Where each page starts, in staff spaces from the top of the first, and
    // where the last ends.
    readonly property var tops: document.pageTops
    // Where the middle of the view is, in pages from the top of the first.
    property real centre: 0
    property bool zooming: false

    /// Where page `page` starts, in pixels from the top of the first; for
    /// pageCount, where the last ends and a gap after it.
    function pageY(page: int): real {
        if (tops.length === 0)
            return 0;
        const at = Math.max(0, Math.min(page, tops.length - 1));
        return tops[at] * unit * shownZoom + at * gap;
    }
    /// How high page `page` is in pixels.
    function pageExtent(page: int): real {
        if (page < 0 || page + 1 >= tops.length)
            return 0;
        return (tops[page + 1] - tops[page]) * unit * shownZoom;
    }
    /// The page `y` pixels from the top of the first is on, and how far down
    /// it (with the gap after it), in pages.
    function pagesAt(y: real): real {
        const count = tops.length - 1;
        if (count <= 0)
            return 0;
        let low = 0;
        let high = count - 1;
        while (low < high) {
            const middle = (low + high + 1) >> 1;
            if (pageY(middle) <= y)
                low = middle;
            else
                high = middle - 1;
        }
        const span = pageExtent(low) + gap;
        return low + (span > 0 ? (y - pageY(low)) / span : 0);
    }
    /// The inverse of pagesAt().
    function yAt(pages: real): real {
        const page = Math.max(0, Math.floor(pages));
        return pageY(page) + (pages - page) * (pageExtent(page) + gap);
    }

    /// Whether the view is still moving by itself: scrolling after the caret
    /// or the playback cursor, or zooming to fit.
    readonly property bool settling: scrollY.running || scrollX.running || followY.running
                                     || followX.running || refit.running || shownZoom !== zoom
    /// Whether one of the sheets over the score is open.
    readonly property bool sheetOpen: measurePopup.visible || chordPopup.visible || textPopup.visible
                                      || lyricsPopup.visible || symbolCard.visible
    /// What the zoom follows as the room for the pages changes: the page's
    /// width up to the reading zoom ("page", as a song opens), the page's
    /// width ("width"), or nothing once it was chosen by hand ("").
    property string fit: "page"
    /// The largest a song opens at (ADR-0133): its page across the view, but
    /// no larger than this on a wide screen.
    readonly property real readingZoom: 1.5

    function setZoom(value: real) {
        zoom = Math.max(minZoom, Math.min(maxZoom, value));
    }
    function zoomIn() {
        fit = "";
        setZoom(zoom * 1.25);
    }
    function zoomOut() {
        fit = "";
        setZoom(zoom / 1.25);
    }
    /// The zoom at which a page fills the width of the view.
    function widthZoom(): real {
        return (list.width - 2 * sideMargin) / (document.pageSize.width * unit);
    }
    function fitWidth() {
        fit = "width";
        setZoom(widthZoom());
    }
    /// The page across the view, up to the reading zoom.
    function fitPage() {
        fit = "page";
        setZoom(Math.min(readingZoom, widthZoom()));
    }
    /// Fits again once the view has its new width: a panel that slides in or
    /// out changes it on every frame, and each zoom paints the pages again.
    onWidthChanged: {
        if (fit.length > 0 && document.pageCount > 0)
            refit.restart();
    }

    Timer {
        id: refit

        interval: 90
        onTriggered: {
            if (root.fit === "width")
                root.setZoom(root.widthZoom());
            else if (root.fit === "page")
                root.setZoom(Math.min(root.readingZoom, root.widthZoom()));
        }
    }
    function toTop() {
        list.contentY = list.originY - list.topMargin;
    }

    // Keyboard entry: the caret follows the keys and stays in view.
    property bool revealPending: false

    function reveal() {
        revealPending = true;
        revealCaret();
    }
    function revealCaret() {
        const page = document.caretPage;
        if (page < 0 || pageHeight <= 0)
            return;
        const scale = unit * shownZoom;
        const caret = document.caretRect;
        const margin = 56;
        let y = scrollY.running ? scrollY.to : list.contentY;
        let x = scrollX.running ? scrollX.to : list.contentX;
        const top = list.originY + pageY(page) + caret.y * scale;
        const bottom = top + caret.height * scale;
        if (top - margin < y)
            y = top - margin;
        else if (bottom + margin > y + list.height)
            y = bottom + margin - list.height;
        const left = list.originX + (list.contentWidth - pageWidth) / 2 + caret.x * scale;
        const right = left + caret.width * scale;
        if (left - margin < x)
            x = Math.max(list.originX, left - margin);
        else if (right + margin > x + list.width)
            x = right + margin - list.width;
        if (y !== list.contentY || x !== list.contentX)
            scrollTo(y, x);
    }
    /// Where the caret's bottom left is in the view.
    function caretPoint(): point {
        const scale = unit * shownZoom;
        const caret = document.caretRect;
        const page = document.caretPage;
        const x = list.originX + (list.contentWidth - pageWidth) / 2 + caret.x * scale - list.contentX;
        const y = list.y + list.originY + pageY(page) + (caret.y + caret.height) * scale
                  - list.contentY;
        return Qt.point(x, y);
    }
    /// Opens the measure popup near (x, y), inside the view.
    function openMeasurePopup(x: real, y: real) {
        measurePopup.x = Math.max(8, Math.min(width - measurePopup.width - 8, x));
        measurePopup.y = Math.max(8, Math.min(height - measurePopup.implicitHeight - 8, y));
        measurePopup.open();
    }
    /// Moves `popup` up or left until all of it is inside the view.
    function keepInside(popup) {
        popup.x = Math.max(8, Math.min(width - popup.width - 8, popup.x));
        popup.y = Math.max(8, Math.min(height - popup.height - 8, popup.y));
    }

    function openMeasurePopupAtCaret() {
        if (document.caretPage < 0)
            return;
        const at = caretPoint();
        openMeasurePopup(at.x - 24, at.y + 12);
    }
    /// Opens the chord symbol popup under the caret's beat, so that the
    /// symbol above the staff shows as it changes; over it when there is no
    /// room below (ADR-0080).
    function openChordPopupAtCaret() {
        openWordsPopupAtCaret(chordPopup);
    }
    /// Opens the beat text popup the same way (ADR-0081).
    function openTextPopupAtCaret() {
        openWordsPopupAtCaret(textPopup);
    }
    /// Opens the lyrics popup away from the caret's staff, so that the
    /// lyrics under it show as they change: at the bottom of the view under a
    /// staff in its upper half, and at the top otherwise (ADR-0084).
    function openLyricsPopupAtCaret() {
        if (document.caretPage < 0)
            return;
        const at = caretPoint();
        lyricsPopup.x = Math.max(8, Math.min(width - lyricsPopup.width - 8,
                                             at.x - lyricsPopup.width / 2));
        lyricsPopup.y = at.y < height / 2
                ? Math.max(8, height - lyricsPopup.implicitHeight - 16)
                : 8;
        lyricsPopup.open();
    }
    /// Edits `symbol` (ScoreDocument::symbolAt()) of page `page` where it is
    /// drawn (ADR-0130): the chord symbol or the text of its beat in their
    /// popups, any other in the card of that symbol, under it.
    function editSymbol(page: int, symbol: var) {
        if (symbol.kind === undefined)
            return;
        const scale = unit * shownZoom;
        const box = symbol.rect;
        if (symbol.kind === "chord" || symbol.kind === "text") {
            // The caret goes to the symbol's beat: a chord's name is centred
            // over it, a text starts at it. Both stand above their staff.
            const x = symbol.kind === "chord" ? box.x + box.width / 2 : box.x + 0.1;
            if (!document.placeCaret(page, x, box.y + box.height + 2))
                return;
            if (symbol.kind === "chord")
                openChordPopupAtCaret();
            else
                openTextPopupAtCaret();
            return;
        }
        const left = list.originX + (list.contentWidth - pageWidth) / 2 + box.x * scale
                     - list.contentX;
        const top = list.y + list.originY + pageY(page) + box.y * scale - list.contentY;
        const below = top + box.height * scale + 10;
        symbolCard.show(symbol);
        symbolCard.x = Math.max(8, Math.min(width - symbolCard.width - 8, left - 14));
        symbolCard.y = below + symbolCard.implicitHeight <= height - 8
                ? below : Math.max(8, top - symbolCard.implicitHeight - 10);
    }
    function openWordsPopupAtCaret(popup) {
        if (document.caretPage < 0)
            return;
        const at = caretPoint();
        const top = at.y - document.caretRect.height * unit * shownZoom;
        const below = at.y + 12;
        popup.x = Math.max(8, Math.min(width - popup.width - 8, at.x - 24));
        popup.y = below + popup.implicitHeight <= height - 8
                ? below : Math.max(8, top - popup.implicitHeight - 12);
        popup.open();
    }
    function scrollTo(y: real, x: real) {
        scrollY.stop();
        scrollX.stop();
        scrollY.to = y;
        scrollX.to = x;
        scrollY.start();
        scrollX.start();
    }

    focus: true
    activeFocusOnTab: true

    Keys.onPressed: event => {
        const control = (event.modifiers & Qt.ControlModifier) !== 0;
        const alt = (event.modifiers & Qt.AltModifier) !== 0;
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0;
        if (document.pageCount === 0)
            return;
        // Alt with 1 to 4 picks the voice typing goes into.
        if (alt && !control && event.key >= Qt.Key_1 && event.key <= Qt.Key_4) {
            document.setCaretVoice(event.key - Qt.Key_0);
            event.accepted = true;
            reveal();
            return;
        }
        // Alt with Up or Down moves the pitch a semitone, or transposes the
        // selected measures; with Ctrl an octave.
        if (alt) {
            if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down)
                return;
            const semitones = (event.key === Qt.Key_Up ? 1 : -1) * (control ? 12 : 1);
            if (document.hasSelection)
                document.transposeSelection(semitones);
            else
                editor.shiftPitch(semitones);
            event.accepted = true;
            reveal();
            return;
        }
        // Measures: Shift with Left or Right selects, Ctrl+A selects all,
        // Ctrl+C copies, Ctrl+V pastes over (with Shift, inserts), Escape lets go.
        let measures = true;
        if (control && !shift && event.key === Qt.Key_A)
            document.selectAll();
        else if (control && !shift && event.key === Qt.Key_C)
            document.copy();
        else if (control && event.key === Qt.Key_V)
            document.paste(shift);
        else if (shift && !control && event.key === Qt.Key_Right)
            document.extendSelection(1);
        else if (shift && !control && event.key === Qt.Key_Left)
            document.extendSelection(-1);
        else if (event.key === Qt.Key_Escape && document.hasSelection)
            document.clearSelection();
        else
            measures = false;
        if (measures) {
            event.accepted = true;
            reveal();
            return;
        }
        // A to G type note names; with Shift they join the beat's chord.
        const letter = "CDEFGAB".indexOf(event.text.toUpperCase());
        if (!control && event.text.length === 1 && letter >= 0) {
            editor.typeNoteName(letter, shift);
            event.accepted = true;
            reveal();
            return;
        }
        // Note effects: T tie, H hammer-on or pull-off, S slide, U bend,
        // V vibrato, P palm mute, X dead note, L let ring, N harmonic,
        // O ghost note, > accent, ' staccato, _ tenuto, " staccatissimo.
        const effect = control ? -1 : editor.effectForKey(event.text);
        if (effect >= 0) {
            editor.toggleEffect(effect);
            event.accepted = true;
            reveal();
            return;
        }
        if (control && event.key === Qt.Key_K) {
            openChordPopupAtCaret();
            event.accepted = true;
            return;
        }
        if (control && !shift && event.key === Qt.Key_T) {
            openTextPopupAtCaret();
            event.accepted = true;
            return;
        }
        if (control && shift && event.key === Qt.Key_L) {
            openLyricsPopupAtCaret();
            event.accepted = true;
            return;
        }
        if (control && event.key === Qt.Key_M) {
            openMeasurePopupAtCaret();
            event.accepted = true;
            return;
        }
        if (control && event.key === Qt.Key_Insert) {
            document.insertMeasure(false);
        } else if (control && shift && event.key === Qt.Key_Delete) {
            document.removeMeasure();
        } else if (!control && event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            document.typeDigit(event.key - Qt.Key_0);
        } else if (control && event.key >= Qt.Key_2 && event.key <= Qt.Key_9 && event.key !== Qt.Key_8) {
            // Ctrl and a digit: that tuplet, or none when the input has it (ADR-0050).
            document.setInputTuplet(event.key - Qt.Key_0);
        } else if (event.key === Qt.Key_Right) {
            document.moveCaret(control ? ScoreDocument.NextMeasure : ScoreDocument.NextBeat);
        } else if (event.key === Qt.Key_Left) {
            document.moveCaret(control ? ScoreDocument.PreviousMeasure : ScoreDocument.PreviousBeat);
        } else if (event.key === Qt.Key_Up && !control) {
            document.moveCaret(ScoreDocument.StringUp);
        } else if (event.key === Qt.Key_Down && !control) {
            document.moveCaret(ScoreDocument.StringDown);
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            if (control)
                editor.deleteBeat();
            else
                editor.deleteAtCaret();
        } else if (event.key === Qt.Key_Insert && !control) {
            editor.insertRest();
        } else if (!control && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            editor.toggleDrum();
        } else if (!control && event.key === Qt.Key_Plus) {
            document.longerDuration();
        } else if (!control && event.key === Qt.Key_Minus) {
            document.shorterDuration();
        } else if (!control && event.key === Qt.Key_Period) {
            document.toggleDot();
        } else {
            return;
        }
        event.accepted = true;
        reveal();
    }

    Connections {
        target: root.document

        // A song just opened or started: typing goes to it.
        function onSourceChanged() {
            root.forceActiveFocus();
        }
        function onCaretChanged() {
            if (root.revealPending) {
                root.revealCaret();
                root.revealPending = root.document.busy;
            }
        }
    }

    NumberAnimation {
        id: scrollY

        target: list
        property: "contentY"
        duration: 160
        easing.type: Easing.OutCubic
    }
    NumberAnimation {
        id: scrollX

        target: list
        property: "contentX"
        duration: 160
        easing.type: Easing.OutCubic
    }

    Behavior on shownZoom {
        NumberAnimation {
            duration: 180
            easing.type: Easing.OutCubic
        }
    }

    // Brings the cursor into view when it leaves it: a third of the way down,
    // and horizontally when the page is wider than the view.
    onCursorChanged: {
        if (!followCursor || !cursorShown || zooming)
            return;
        const scale = unit * shownZoom;
        const top = pageY(cursor.page) + (cursor.y - 6) * scale;
        const bottom = top + (cursor.height + 12) * scale;
        if (top < list.contentY || bottom > list.contentY + list.height) {
            const highest = list.originY - list.topMargin;
            const lowest = list.originY + list.contentHeight + list.bottomMargin - list.height;
            followY.to = Math.max(highest, Math.min(lowest, top - list.height / 3));
            followY.restart();
        }
        const x = playhead.x;
        if (list.contentWidth > list.width && (x < list.contentX + 40
                                               || x > list.contentX + list.width - 40)) {
            followX.to = Math.max(0, Math.min(list.contentWidth - list.width,
                                              x - list.width / 3));
            followX.restart();
        }
    }

    NumberAnimation {
        id: followY

        target: list
        property: "contentY"
        duration: 420
        easing.type: Easing.InOutCubic
    }
    NumberAnimation {
        id: followX

        target: list
        property: "contentX"
        duration: 420
        easing.type: Easing.InOutCubic
    }

    // Keep the page under the middle of the view in place while zooming.
    onShownZoomChanged: {
        zooming = true;
        list.contentY = list.originY + yAt(centre) - list.height / 2;
        zooming = false;
    }

    // The pages, each where pageY() puts it. A page is loaded only near the
    // view, so that only those near it hold a texture.
    Flickable {
        id: list

        // How far above and below the view pages stay loaded.
        readonly property real buffer: Math.max(0, root.pageHeight)

        anchors.fill: parent
        topMargin: 24
        bottomMargin: 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        contentWidth: Math.max(width, root.pageWidth + 2 * root.sideMargin)
        contentHeight: Math.max(0, root.pageY(root.document.pageCount) - root.gap)

        onContentYChanged: {
            if (!root.zooming)
                root.centre = root.pagesAt(contentY - originY + height / 2);
        }
        onHeightChanged: root.centre = root.pagesAt(contentY - originY + height / 2)

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        // The continuous view's strips lie on one sheet with one shadow, so
        // that no seam shows between them. The sheet is as dim as the strips
        // in the dark theme, also where a strip is not loaded yet.
        RectangularShadow {
            anchors.fill: scroll
            visible: scroll.visible
            offset.y: 3
            radius: 2
            blur: 18
            spread: 0
            color: Qt.rgba(0, 0, 0, Theme.dark ? 0.55 : 0.2)
        }
        Rectangle {
            id: scroll

            visible: root.document.showsStrips && root.document.pageCount > 0
            x: (list.contentWidth - root.pageWidth) / 2
            width: root.pageWidth
            height: list.contentHeight
            radius: 2
            color: root.darkPage ? root.paper
                                 : Theme.dark ? Qt.rgba(0.851, 0.85, 0.845, 1) : "white"
            border.width: root.darkPage ? 1 : 0
            border.color: Theme.line
        }

        Repeater {
            model: root.document.pageCount

            delegate: Item {
                id: sheet

                required property int index
                /// The symbol under the pointer on this page that a double click
                /// edits (ScoreDocument::symbolAt()); empty while there is none.
                property var symbol: ({})

                y: root.pageY(index)
                width: list.contentWidth
                height: root.pageExtent(index)

                Loader {
                    anchors.fill: parent
                    active: sheet.y + sheet.height >= list.contentY - list.buffer
                            && sheet.y <= list.contentY + list.height + list.buffer
                    sourceComponent: sheetContent
                }

                Component {
                    id: sheetContent

                    Item {
                        // A soft shadow under the paper, drawn analytically on the GPU.
                        RectangularShadow {
                            anchors.fill: paper
                            visible: !root.document.showsStrips
                            offset.y: 3
                            radius: 2
                            blur: 18
                            spread: 0
                            color: Qt.rgba(0, 0, 0, Theme.dark ? 0.55 : 0.2)
                        }

                        Rectangle {
                            id: paper

                            width: root.pageWidth
                            height: sheet.height
                            anchors.horizontalCenter: parent.horizontalCenter
                            radius: root.document.showsStrips ? 0 : 2
                            color: root.paper
                            // A dark page has no shadow to stand out by: a hairline.
                            border.width: root.darkPage && !root.document.showsStrips ? 1 : 0
                            border.color: Theme.line

                            ScorePage {
                                anchors.fill: parent
                                document: root.document
                                index: sheet.index
                                paper: root.paper
                                ink: root.ink
                                opacity: painted ? 1.0 : 0.0

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 160
                                    }
                                }
                            }

                            // In the dark theme a white page is dimmed, so that it does
                            // not glare on the dark desk; the ink stays black.
                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                visible: Theme.dark && !root.darkPage
                                color: Qt.rgba(0.07, 0.06, 0.03, 0.16)
                            }

                            // The selected measures, tinted across their systems.
                            Repeater {
                                model: root.document.selectionRevision >= 0
                                       ? root.document.selectionBoxes(sheet.index) : []

                                delegate: Rectangle {
                                    required property rect modelData
                                    readonly property real scale: root.unit * root.shownZoom

                                    x: modelData.x * scale
                                    y: modelData.y * scale
                                    width: modelData.width * scale
                                    height: modelData.height * scale
                                    radius: 3
                                    color: Qt.alpha(Theme.accent, 0.1)
                                    border.color: Qt.alpha(Theme.accent, 0.35)
                                    border.width: 1

                                    NumberAnimation on opacity {
                                        from: 0
                                        to: 1
                                        duration: 120
                                    }
                                }
                            }

                            // The symbol under the pointer that a double click edits
                            // (ADR-0130): lit over the page, which is not painted again.
                            Rectangle {
                                readonly property real scale: root.unit * root.shownZoom
                                readonly property rect box: sheet.symbol.rect ?? Qt.rect(0, 0, 0, 0)

                                objectName: "symbolHighlight"
                                visible: sheet.symbol.kind !== undefined
                                x: box.x * scale - 3
                                y: box.y * scale - 3
                                width: box.width * scale + 6
                                height: box.height * scale + 6
                                radius: 4
                                color: Qt.alpha(Theme.accent, 0.12)
                                border.color: Qt.alpha(Theme.accent, 0.55)
                                border.width: 1
                            }

                            // The caret: where typing goes. A soft band says its beat
                            // across the staff, the notation and the tablature alike,
                            // and a cell on the band the string (or the drum, or the
                            // whole of a staff of notation). It glides between beats
                            // on a page, and keeps up with zooming without gliding.
                            Item {
                                id: caret

                                readonly property real scale: root.unit * root.shownZoom
                                readonly property rect cell: root.document.caretRect
                                readonly property rect column: root.document.caretColumn
                                // Faint while the keys go elsewhere; stronger on a
                                // dark page, where a wash of the accent hardly shows.
                                readonly property bool lit: root.activeFocus
                                readonly property real strength: root.darkPage ? 1.8 : 1.0
                                // On notation alone the cell is the band: its frame.
                                readonly property bool whole: cell.height >= column.height - 0.01

                                objectName: "caret"
                                visible: root.document.caretPage === sheet.index
                                x: cell.x * scale
                                width: cell.width * scale

                                Behavior on x {
                                    enabled: root.shownZoom === root.zoom
                                    NumberAnimation {
                                        duration: 90
                                        easing.type: Easing.OutCubic
                                    }
                                }

                                Rectangle {
                                    objectName: "caretBand"
                                    y: caret.column.y * caret.scale
                                    width: caret.width
                                    height: caret.column.height * caret.scale
                                    radius: Math.min(width / 2, 0.6 * caret.scale)
                                    color: Qt.alpha(Theme.accent,
                                                    (caret.lit ? 0.12 : 0.05) * caret.strength)
                                }
                                Rectangle {
                                    objectName: "caretCell"
                                    y: caret.cell.y * caret.scale
                                    width: caret.width
                                    height: caret.cell.height * caret.scale
                                    radius: Math.min(width, height) * 0.24
                                    color: caret.whole ? "transparent" : Qt.alpha(
                                                             Theme.accent, (caret.lit ? 0.16 : 0.06)
                                                             * caret.strength)
                                    border.color: Qt.alpha(Theme.accent, caret.lit ? 1.0 : 0.45)
                                    border.width: caret.lit
                                                  ? Math.max(1.5, Math.min(3, 0.3 * caret.scale)) : 1

                                    Behavior on y {
                                        enabled: root.shownZoom === root.zoom
                                        NumberAnimation {
                                            duration: 90
                                            easing.type: Easing.OutCubic
                                        }
                                    }
                                }
                            }

                            // The pointer over the page finds the symbol under it.
                            HoverHandler {
                                cursorShape: sheet.symbol.kind !== undefined ? Qt.PointingHandCursor
                                                                             : Qt.ArrowCursor
                                onPointChanged: {
                                    const scale = root.unit * root.shownZoom;
                                    sheet.symbol = root.document.symbolAt(
                                                sheet.index, point.position.x / scale,
                                                point.position.y / scale);
                                }
                                onHoveredChanged: {
                                    if (!hovered)
                                        sheet.symbol = ({});
                                }
                            }

                            // A right click opens the measure under it.
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: eventPoint => {
                                    const scale = root.unit * root.shownZoom;
                                    root.forceActiveFocus();
                                    if (root.document.placeCaret(sheet.index, eventPoint.position.x / scale,
                                                                 eventPoint.position.y / scale)) {
                                        const at = paper.mapToItem(root, eventPoint.position.x,
                                                                   eventPoint.position.y);
                                        root.openMeasurePopup(at.x + 8, at.y + 8);
                                    }
                                }
                            }

                            // A click puts the caret on the beat and string under it and
                            // moves playback to that note; a drag still scrolls. One
                            // handler for both, so neither takes the click from the other.
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: eventPoint => {
                                    const scale = root.unit * root.shownZoom;
                                    const x = eventPoint.position.x / scale;
                                    const y = eventPoint.position.y / scale;
                                    const seconds = root.document.secondsAt(sheet.index, x, y);
                                    root.forceActiveFocus();
                                    root.document.placeCaret(sheet.index, x, y);
                                    if (seconds >= 0)
                                        root.seekRequested(seconds);
                                }
                                // A second click on a symbol edits it where it is.
                                onDoubleTapped: eventPoint => {
                                    const scale = root.unit * root.shownZoom;
                                    root.editSymbol(sheet.index, root.document.symbolAt(
                                                        sheet.index, eventPoint.position.x / scale,
                                                        eventPoint.position.y / scale));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    MeasurePopup {
        id: measurePopup
        objectName: "measurePopup"

        document: root.document
        editor: root.editor
        onClosed: root.forceActiveFocus()
        // It knows its height once it shows: kept inside the view then.
        onOpened: root.keepInside(measurePopup)
        onHeightChanged: {
            if (visible)
                root.keepInside(measurePopup);
        }
    }

    SymbolCard {
        id: symbolCard
        objectName: "symbolCard"

        document: root.document
        editor: root.editor
        onClosed: root.forceActiveFocus()
    }

    WordsPopup {
        id: chordPopup
        objectName: "chordPopup"

        title: qsTr("Chord symbol")
        placeholder: qsTr("Am7, C/G, N.C.…")
        hint: root.editor.caretFretted ? qsTr("Enter names it; the beat's frets draw its diagram.")
                                         : qsTr("Enter names it; an empty name takes it away.")
        removeTip: qsTr("Remove the chord symbol")
        current: root.editor.caretChord
        onOpening: offered = root.editor.songChords()
        onApplied: words => root.editor.setChordSymbol(words)
        onClosed: root.forceActiveFocus()
    }

    WordsPopup {
        id: textPopup
        objectName: "textPopup"

        title: qsTr("Text")
        placeholder: qsTr("let it ring, ad lib., solo…")
        hint: qsTr("Enter writes it above the beat; an empty text takes it away.")
        removeTip: qsTr("Remove the text")
        current: root.editor.caretText
        onOpening: offered = root.editor.songTexts()
        onApplied: words => root.editor.setBeatText(words)
        onClosed: root.forceActiveFocus()
    }

    LyricsPopup {
        id: lyricsPopup
        objectName: "lyricsPopup"

        document: root.document
        editor: root.editor
        onClosed: root.forceActiveFocus()
    }

    // The playback cursor: a line across the system, over the pages.
    Rectangle {
        id: playhead

        readonly property real scale: root.unit * root.shownZoom

        objectName: "playhead"
        parent: list.contentItem
        z: 2
        visible: root.cursorShown
        width: Math.max(2, 0.28 * scale)
        radius: width / 2
        color: root.cursorColor
        opacity: 0.9
        x: root.cursorShown ? (list.contentWidth - root.pageWidth) / 2 + root.cursor.x * scale
                              - width / 2 : 0
        y: root.cursorShown ? root.pageY(root.cursor.page) + (root.cursor.y - 2) * scale : 0
        height: root.cursorShown ? (root.cursor.height + 4) * scale : 0

        // A soft glow either side.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 5
            height: parent.height
            radius: width / 2
            color: root.cursorColor
            opacity: 0.12
        }
    }

    // Ctrl + wheel (or a pinch on a touchpad) zooms.
    WheelHandler {
        target: null
        acceptedModifiers: Qt.ControlModifier
        onWheel: event => {
            root.fit = "";
            root.setZoom(root.zoom * Math.pow(1.0015, event.angleDelta.y));
        }
    }
    PinchHandler {
        target: null
        property real startZoom: 1.0
        onActiveChanged: {
            if (active)
                startZoom = root.zoom;
        }
        onActiveScaleChanged: {
            root.fit = "";
            root.setZoom(startZoom * activeScale);
        }
    }
}
