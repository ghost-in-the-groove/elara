// SPDX-License-Identifier: AGPL-3.0-or-later
// The pages of a score on a desk, scrolled vertically and zoomed smoothly.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
import Elara.App

Item {
    id: root

    required property ScoreDocument document
    property bool dark: false

    /// 1 shows pages at their paper size (96 logical pixels per inch).
    property real zoom: 1.0
    readonly property real minZoom: 0.25
    readonly property real maxZoom: 4.0
    /// The page under the middle of the view, from 1.
    readonly property int currentPage: Math.max(1, Math.min(document.pageCount,
                                                           Math.floor(centre) + 1))

    // Logical pixels per staff space at zoom 1.
    readonly property real unit: document.staffSpace * 96 / 25.4
    property real shownZoom: zoom
    readonly property real pageWidth: document.pageSize.width * unit * shownZoom
    readonly property real pageHeight: document.pageSize.height * unit * shownZoom
    readonly property real gap: 24
    readonly property real sideMargin: 32
    // Where the middle of the view is, in pages from the top of the first.
    property real centre: 0
    property bool zooming: false

    function setZoom(value: real) {
        zoom = Math.max(minZoom, Math.min(maxZoom, value));
    }
    function zoomIn() {
        setZoom(zoom * 1.25);
    }
    function zoomOut() {
        setZoom(zoom / 1.25);
    }
    /// The zoom at which a page fills the width of the view.
    function widthZoom(): real {
        return (list.width - 2 * sideMargin) / (document.pageSize.width * unit);
    }
    function fitWidth() {
        setZoom(widthZoom());
    }
    /// Paper size, or smaller when the view is narrower.
    function fitPaper() {
        setZoom(Math.min(1.0, widthZoom()));
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
        const top = list.originY + page * (pageHeight + gap) + caret.y * scale;
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
        if (alt || document.pageCount === 0)
            return;
        if (!control && event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            document.typeDigit(event.key - Qt.Key_0);
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
                document.deleteBeat();
            else
                document.deleteAtCaret();
        } else if (event.key === Qt.Key_Insert && !control) {
            document.insertRest();
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

    // Keep the page under the middle of the view in place while zooming.
    onShownZoomChanged: {
        zooming = true;
        list.contentY = list.originY + centre * (pageHeight + gap) - list.height / 2;
        zooming = false;
    }

    ListView {
        id: list

        anchors.fill: parent
        model: root.document.pageCount
        spacing: root.gap
        topMargin: root.gap
        bottomMargin: root.gap
        clip: true
        reuseItems: true
        keyNavigationEnabled: false
        cacheBuffer: Math.max(0, Math.round(root.pageHeight))
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        contentWidth: Math.max(width, root.pageWidth + 2 * root.sideMargin)

        onContentYChanged: {
            if (!root.zooming && root.pageHeight > 0)
                root.centre = (contentY - originY + height / 2) / (root.pageHeight + root.gap);
        }
        onHeightChanged: {
            if (root.pageHeight > 0)
                root.centre = (contentY - originY + height / 2) / (root.pageHeight + root.gap);
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        delegate: Item {
            id: sheet

            required property int index

            width: list.contentWidth
            height: root.pageHeight

            // A soft shadow under the paper, drawn analytically on the GPU.
            RectangularShadow {
                anchors.fill: paper
                offset.y: 3
                radius: 2
                blur: 16
                spread: 0
                color: Qt.rgba(0, 0, 0, 0.22)
            }

            Rectangle {
                id: paper

                width: root.pageWidth
                height: root.pageHeight
                anchors.horizontalCenter: parent.horizontalCenter
                radius: 2
                color: "white"

                ScorePage {
                    anchors.fill: parent
                    document: root.document
                    index: sheet.index
                    opacity: painted ? 1.0 : 0.0

                    Behavior on opacity {
                        NumberAnimation {
                            duration: 160
                        }
                    }
                }

                // The caret: where typing goes. It glides between beats on a
                // page, and keeps up with zooming without gliding.
                Rectangle {
                    readonly property real scale: root.unit * root.shownZoom
                    readonly property rect box: root.document.caretRect

                    visible: root.document.caretPage === sheet.index
                    x: box.x * scale
                    y: box.y * scale
                    width: box.width * scale
                    height: box.height * scale
                    radius: Math.min(width, height) * 0.22
                    color: Qt.alpha(Material.accent, root.activeFocus ? 0.22 : 0.1)
                    border.color: Qt.alpha(Material.accent, root.activeFocus ? 0.9 : 0.4)
                    border.width: 1.5

                    Behavior on x {
                        enabled: root.shownZoom === root.zoom
                        NumberAnimation {
                            duration: 90
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on y {
                        enabled: root.shownZoom === root.zoom
                        NumberAnimation {
                            duration: 90
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                // A click puts the caret on the beat and string under it.
                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: eventPoint => {
                        const scale = root.unit * root.shownZoom;
                        root.forceActiveFocus();
                        root.document.placeCaret(sheet.index, eventPoint.position.x / scale,
                                                 eventPoint.position.y / scale);
                    }
                }
            }
        }
    }

    EntryBar {
        id: entryBar

        readonly property bool shown: root.document.caretPage >= 0

        anchors.horizontalCenter: parent.horizontalCenter
        y: shown ? parent.height - height - 18 : parent.height + 8
        document: root.document
        dark: root.dark
        opacity: shown ? 1.0 : 0.0
        visible: opacity > 0

        Behavior on y {
            NumberAnimation {
                duration: 220
                easing.type: Easing.OutCubic
            }
        }
        Behavior on opacity {
            NumberAnimation {
                duration: 180
            }
        }
    }

    // Ctrl + wheel (or a pinch on a touchpad) zooms.
    WheelHandler {
        target: null
        acceptedModifiers: Qt.ControlModifier
        onWheel: event => root.setZoom(root.zoom * Math.pow(1.0015, event.angleDelta.y))
    }
    PinchHandler {
        target: null
        property real startZoom: 1.0
        onActiveChanged: {
            if (active)
                startZoom = root.zoom;
        }
        onActiveScaleChanged: root.setZoom(startZoom * activeScale)
    }
}
