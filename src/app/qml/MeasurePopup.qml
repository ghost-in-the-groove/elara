// SPDX-License-Identifier: AGPL-3.0-or-later
// The caret's measure: its time and key signatures, which change from it until
// the next change, and measures inserted before or after it, or it deleted.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Elara.App

Popup {
    id: root

    required property ScoreDocument document

    readonly property color ink: Theme.ink

    width: 360
    padding: 18
    topPadding: 12
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 140
        }
        NumberAnimation {
            property: "scale"
            from: 0.96
            to: 1
            duration: 140
            easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: 100
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

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: qsTr("Measure %1").arg(root.document.caretMeasure + 1)
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                color: root.ink
            }
            IconButton {
                iconPath: "M7 7 L17 17 M17 7 L7 17"
                tip: qsTr("Close")
                onClicked: root.close()
            }
        }

        SignatureFields {
            Layout.fillWidth: true
            labelWidth: 112
            numerator: root.document.caretTimeNumerator
            denominator: root.document.caretTimeDenominator
            fifths: root.document.caretKeyFifths
            minor: root.document.caretKeyMinor
            onTimeChosen: (numerator, denominator) => root.document.setTimeSignatureFrom(
                              root.document.caretMeasure, numerator, denominator)
            onKeyChosen: (fifths, minor) => root.document.setKeySignatureFrom(
                             root.document.caretMeasure, fifths, minor)
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("From this measure until the next change.")
            font.pixelSize: Theme.fontSmall
            opacity: 0.5
            wrapMode: Text.Wrap
            color: root.ink
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 2
            height: 1
            color: Theme.line
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Button {
                flat: true
                focusPolicy: Qt.NoFocus
                text: qsTr("Insert before")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("An empty measure before this one (Ctrl+Insert)")
                onClicked: root.document.insertMeasure(false)
            }
            Button {
                flat: true
                focusPolicy: Qt.NoFocus
                text: qsTr("Insert after")
                onClicked: root.document.insertMeasure(true)
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                flat: true
                focusPolicy: Qt.NoFocus
                enabled: root.document.measureCount > 1
                text: qsTr("Delete")
                Material.foreground: Theme.danger
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Delete this measure from every track (Ctrl+Shift+Delete)")
                onClicked: root.document.removeMeasure()
            }
        }
    }
}
