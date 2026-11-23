// SPDX-License-Identifier: AGPL-3.0-or-later
// The pages of a score on a desk, scrolled vertically and zoomed smoothly.
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Elara.App

Item {
    id: root

    required property ScoreDocument document

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
