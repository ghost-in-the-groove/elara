// SPDX-License-Identifier: AGPL-3.0-or-later
// The keyboard entry bar, in a strip over the top of the pages: the note value
// typing uses, the dot, the tuplet, a rest, delete, the note effects, and
// where the caret is.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Elara.App

Rectangle {
    id: root

    required property ScoreDocument document

    /// The measure button: the caret's measure, its signatures and length.
    signal measureRequested

    // Note values from the music font's metronome marks, longest first.
    readonly property var values: [
        { value: 1, symbol: "\uECA2", tip: qsTr("Whole note") },
        { value: 2, symbol: "\uECA3", tip: qsTr("Half note") },
        { value: 4, symbol: "\uECA5", tip: qsTr("Quarter note") },
        { value: 8, symbol: "\uECA7", tip: qsTr("Eighth note") },
        { value: 16, symbol: "\uECA9", tip: qsTr("16th note") },
        { value: 32, symbol: "\uECAB", tip: qsTr("32nd note") },
        { value: 64, symbol: "\uECAD", tip: qsTr("64th note") }
    ]
    readonly property int currentIndex: values.findIndex(entry => entry.value === document.inputValue)
    readonly property color ink: Theme.ink
    /// How far the next bend goes when the caret's note has none, in cents.
    property int bendCents: 200
    /// How far the next dive goes when the caret's beat has none (ADR-0060).
    property int whammyCents: -200

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 44
    color: "transparent"

    // A tool of the bar: never takes the keyboard from the score.
    component Tool: AbstractButton {
        id: tool

        property string tip

        implicitWidth: 34
        implicitHeight: 34
        focusPolicy: Qt.NoFocus
        hoverEnabled: true

        background: Rectangle {
            radius: height / 2
            color: tool.pressed ? Theme.pressed : tool.hovered ? Theme.hover : "transparent"

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }
        }

        ToolTip.visible: hovered && tip.length > 0
        ToolTip.delay: 500
        ToolTip.text: tip
        Accessible.name: tip
    }

    // A tuplet in the tuplet menu, checked when the input has it.
    component TupletItem: MenuItem {
        required property int actual

        checkable: true
        checked: root.document.inputTuplet === actual
        onTriggered: root.document.setInputTuplet(actual)
    }

    // A pill of the effects panel: accent-filled while it is on.
    component Pill: AbstractButton {
        id: pill

        property bool active

        implicitHeight: 30
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        Accessible.checked: active

        background: Rectangle {
            radius: height / 2
            color: pill.active ? Material.accent
                               : pill.pressed ? Qt.alpha(root.ink, 0.16)
                                              : pill.hovered ? Qt.alpha(root.ink, 0.08) : "transparent"

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }
        }

        ToolTip.visible: hovered && Accessible.name.length > 0
        ToolTip.delay: 500
        ToolTip.text: Accessible.name
    }

    // A bend form (ADR-0055), numbered as core::BendForm: on the caret's notes
    // at the bend's amount, or off again when they have it.
    component BendForm: Pill {
        id: bendForm

        required property int form
        required property string tip
        required property string path

        implicitWidth: 34
        active: root.document.caretBendForm === form
        Accessible.role: Accessible.RadioButton
        Accessible.name: tip
        onClicked: {
            if (active) {
                root.document.setBend(0, 0)
            } else {
                const cents = root.document.caretBendCents
                root.document.setBend(form, cents > 0 ? cents : root.bendCents)
            }
        }

        contentItem: Icon {
            path: bendForm.path
            lineWidth: 1.8
            color: bendForm.active ? "white" : root.ink
        }
    }

    // A beat effect drawn as an icon (ADR-0056): lit while the caret's beat
    // has it; a click on a lit one takes it off.
    component BeatEffect: Pill {
        id: beatEffect

        required property string tip
        required property string path

        implicitWidth: 34
        Accessible.role: Accessible.CheckBox
        Accessible.name: tip

        contentItem: Icon {
            path: beatEffect.path
            lineWidth: 1.8
            color: beatEffect.active ? "white" : root.ink
        }
    }

    // A kind of note effect (ADR-0058), drawn as an icon or a short label:
    // lit while the caret's notes have it; a click on a lit one takes it off.
    component EffectKind: Pill {
        id: effectKind

        required property string tip
        property string path
        property string label

        implicitWidth: label.length > 0 ? Math.max(34, kindLabel.implicitWidth + 14) : 34
        opacity: enabled ? 1.0 : 0.35
        Accessible.role: Accessible.CheckBox
        Accessible.name: tip

        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                visible: effectKind.label.length === 0
                path: effectKind.path
                lineWidth: 1.8
                color: effectKind.active ? "white" : root.ink
            }
            Label {
                id: kindLabel

                anchors.centerIn: parent
                visible: effectKind.label.length > 0
                text: effectKind.label
                font.pixelSize: 11
                font.bold: true
                color: effectKind.active ? "white" : root.ink
            }
        }
    }

    // A whammy-bar form (ADR-0060), numbered as core::BendForm and drawn going
    // down: on the caret's beat at the dive's depth, or off when it has it.
    component WhammyForm: Pill {
        id: whammyForm

        required property int form
        required property string path

        readonly property bool diving: root.document.caretWhammyCents < 0

        implicitWidth: 34
        active: diving && root.document.caretWhammyForm === form
        Accessible.role: Accessible.RadioButton
        Accessible.name: root.document.whammyName(form, diving ? root.document.caretWhammyCents
                                                                : root.whammyCents)
        onClicked: {
            if (active) {
                root.document.setWhammy(0, 0)
            } else {
                root.document.setWhammy(form, diving ? root.document.caretWhammyCents
                                                     : root.whammyCents)
            }
        }

        contentItem: Icon {
            path: whammyForm.path
            lineWidth: 1.8
            color: whammyForm.active ? "white" : root.ink
        }
    }

    // How deep a dive goes: the caret's whammy bar, or a dive if it has none.
    component WhammyAmount: Pill {
        id: whammyAmount

        required property int cents

        readonly property int form: root.document.caretWhammyForm

        implicitWidth: Math.max(34, whammyLabel.implicitWidth + 16)
        active: form > 0 && form < 5 && root.document.caretWhammyCents === cents
        Accessible.role: Accessible.RadioButton
        Accessible.name: root.document.whammyAmount(cents)
        onClicked: {
            root.whammyCents = cents
            root.document.setWhammy(form > 0 && form < 5 ? form : 1, cents)
        }

        contentItem: Label {
            id: whammyLabel

            text: root.document.whammyAmount(whammyAmount.cents)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 12
            font.features: { "frac": 1 }
            color: whammyAmount.active ? "white" : root.ink
        }
    }

    // How far a bend goes: the caret's bend, or a bend up if it has none.
    component BendAmount: Pill {
        id: bendAmount

        required property int cents

        implicitWidth: Math.max(34, amountLabel.implicitWidth + 16)
        active: root.document.caretBendForm > 0 && root.document.caretBendCents === cents
        Accessible.role: Accessible.RadioButton
        Accessible.name: root.document.bendAmount(cents)
        onClicked: {
            const form = root.document.caretBendForm
            root.bendCents = cents
            root.document.setBend(form > 0 && form < 5 ? form : 1, cents)
        }

        contentItem: Label {
            id: amountLabel

            text: root.document.bendAmount(bendAmount.cents)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 12
            font.features: { "frac": 1 }
            color: bendAmount.active ? "white" : root.ink
        }
    }

    // A voice in the voice menu, checked when the caret is in it.
    component VoiceItem: MenuItem {
        required property int voice

        text: qsTr("Voice %1").arg(voice)
        checkable: true
        checked: root.document.caretVoice === voice
        onTriggered: root.document.setCaretVoice(voice)
    }

    component Separator: Rectangle {
        width: 1
        height: 22
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        color: Theme.line
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: 2

        Item {
            width: valueRow.width
            height: 34

            // The chosen value, gliding from one to the next.
            Rectangle {
                readonly property Item target: valueRepeater.count > root.currentIndex
                                               && root.currentIndex >= 0
                                               ? valueRepeater.itemAt(root.currentIndex) : null

                visible: target !== null
                x: target ? target.x : 0
                width: 34
                height: 34
                radius: 17
                color: Material.accent

                Behavior on x {
                    NumberAnimation {
                        duration: 160
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Row {
                id: valueRow

                Repeater {
                    id: valueRepeater

                    model: root.values

                    delegate: Tool {
                        id: valueTool

                        required property var modelData
                        required property int index

                        tip: qsTr("%1 (+ longer, - shorter)").arg(modelData.tip)
                        onClicked: root.document.setInputValue(modelData.value)

                        contentItem: MusicSymbol {
                            symbol: valueTool.modelData.symbol
                            size: 22
                            color: valueTool.index === root.currentIndex ? "white" : root.ink
                        }
                    }
                }
            }
        }

        Tool {
            id: dot

            tip: qsTr("Dotted (.)")
            onClicked: root.document.toggleDot()

            background: Rectangle {
                radius: height / 2
                color: root.document.inputDotted ? Material.accent
                                                 : dot.hovered ? Theme.hover : "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }
                }
            }
            contentItem: MusicSymbol {
                symbol: "\uECB7"
                size: 30
                color: root.document.inputDotted ? "white" : root.ink
            }
        }

        // The tuplet (ADR-0050): a click toggles a triplet, a right click or a
        // long press picks another.
        Tool {
            id: tuplet

            readonly property bool on: root.document.inputTuplet > 0

            tip: qsTr("Triplet (Ctrl+3); a right click for other tuplets (Ctrl+2 to Ctrl+9)")
            onClicked: root.document.setInputTuplet(on ? 0 : 3)
            onPressAndHold: tupletMenu.popup(tuplet, 0, tuplet.height + 6)

            background: Rectangle {
                radius: height / 2
                color: tuplet.on ? Material.accent
                                 : tuplet.hovered ? Theme.hover : "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }
                }
            }
            contentItem: MusicSymbol {
                // The tuplet's number in the music font's tuplet digits.
                symbol: String.fromCharCode(0xE880 + (tuplet.on ? root.document.inputTuplet : 3))
                size: 30
                color: tuplet.on ? "white" : root.ink
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: tupletMenu.popup(tuplet, 0, tuplet.height + 6)
            }

            Menu {
                id: tupletMenu

                TupletItem {
                    actual: 2
                    text: qsTr("Duplet (2 in the time of 3)")
                }
                TupletItem {
                    actual: 3
                    text: qsTr("Triplet (3 in the time of 2)")
                }
                TupletItem {
                    actual: 4
                    text: qsTr("Quadruplet (4 in the time of 3)")
                }
                TupletItem {
                    actual: 5
                    text: qsTr("Quintuplet (5 in the time of 4)")
                }
                TupletItem {
                    actual: 6
                    text: qsTr("Sextuplet (6 in the time of 4)")
                }
                TupletItem {
                    actual: 7
                    text: qsTr("Septuplet (7 in the time of 4)")
                }
                TupletItem {
                    actual: 9
                    text: qsTr("Nonuplet (9 in the time of 8)")
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("No tuplet")
                    enabled: tuplet.on
                    onTriggered: root.document.setInputTuplet(0)
                }
            }
        }

        // The voice typing goes into (Alt+1 to Alt+4): a click goes on to the
        // next one, a right click picks one. Lit outside the first voice.
        Tool {
            id: voiceTool

            readonly property int voice: root.document.caretVoice

            tip: qsTr("Voice %1 (Alt+1 to Alt+4); a right click picks one").arg(voice)
            onClicked: root.document.setCaretVoice(voice % 4 + 1)
            onPressAndHold: voiceMenu.popup(voiceTool, 0, voiceTool.height + 6)

            background: Rectangle {
                radius: height / 2
                color: voiceTool.voice > 1 ? Material.accent
                                           : voiceTool.hovered ? Theme.hover : "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }
                }
            }
            contentItem: Label {
                text: qsTr("V%1").arg(voiceTool.voice)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                color: voiceTool.voice > 1 ? "white" : root.ink
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: voiceMenu.popup(voiceTool, 0, voiceTool.height + 6)
            }

            Menu {
                id: voiceMenu

                VoiceItem {
                    voice: 1
                }
                VoiceItem {
                    voice: 2
                }
                VoiceItem {
                    voice: 3
                }
                VoiceItem {
                    voice: 4
                }
            }
        }

        Item {
            width: 8
            height: 34

            Separator {
                anchors.centerIn: parent
            }
        }

        Tool {
            tip: qsTr("Insert a rest (Insert)")
            onClicked: root.document.insertRest()

            contentItem: MusicSymbol {
                symbol: "\uE4E5"
                size: 20
                color: root.ink
            }
        }
        Tool {
            tip: qsTr("Delete the note (Delete); the beat with Ctrl+Delete")
            onClicked: root.document.deleteAtCaret()

            contentItem: Icon {
                path: "M5 7 L19 7 M10 7 L10 4.5 L14 4.5 L14 7 M7 7 L8 20 L16 20 L17 7"
                color: root.ink
            }
        }

        Tool {
            id: effectsTool

            tip: qsTr("Note effects")
            onClicked: effects.opened ? effects.close() : effects.open()

            contentItem: Item {
                Icon {
                    anchors.centerIn: parent
                    path: "M3 12 C5 8 7 8 9 12 C11 16 13 16 15 12 C17 8 19 8 21 12"
                    color: root.ink
                }
                // A dot while the caret's note has an effect.
                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 5
                    color: Material.accent
                    scale: root.document.caretEffects !== 0 ? 1 : 0

                    Behavior on scale {
                        NumberAnimation {
                            duration: 140
                            easing.type: Easing.OutBack
                        }
                    }
                }
            }

            // The effects of the caret's note, toggled by a click or their key;
            // it stays open so that several can be set, and never takes the
            // keyboard from the score.
            Popup {
                id: effects

                x: (effectsTool.width - width) / 2
                y: effectsTool.height + 12
                padding: 10
                closePolicy: Popup.CloseOnPressOutsideParent

                enter: Transition {
                    NumberAnimation {
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 140
                    }
                    NumberAnimation {
                        property: "scale"
                        from: 0.94
                        to: 1
                        duration: 160
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
                    radius: 16
                    color: root.color
                    border.color: Qt.alpha(root.ink, 0.12)
                }

                contentItem: Column {
                    spacing: 6

                    Label {
                        leftPadding: 4
                        text: qsTr("Note effects")
                        font.pixelSize: 11
                        color: root.ink
                        opacity: 0.6
                    }

                    Grid {
                        columns: 3
                        spacing: 4

                        Repeater {
                            model: root.document.effectCount

                            delegate: AbstractButton {
                                id: chip

                                required property int index
                                readonly property bool active: (root.document.caretEffects & (1 << index)) !== 0

                                implicitWidth: chipRow.implicitWidth + 16
                                implicitHeight: 30
                                focusPolicy: Qt.NoFocus
                                hoverEnabled: true
                                onClicked: root.document.toggleEffect(index)
                                Accessible.role: Accessible.CheckBox
                                Accessible.name: root.document.effectName(index)
                                Accessible.checked: active

                                background: Rectangle {
                                    radius: height / 2
                                    color: chip.active ? Material.accent
                                                   : chip.pressed ? Qt.alpha(root.ink, 0.16)
                                                                  : chip.hovered ? Qt.alpha(root.ink, 0.08)
                                                                                 : "transparent"

                                    Behavior on color {
                                        ColorAnimation {
                                            duration: 120
                                        }
                                    }
                                }

                                contentItem: Item {
                                    Row {
                                        id: chipRow

                                        x: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 8

                                        // The key, as on a keycap.
                                        Rectangle {
                                            width: 20
                                            height: 20
                                            radius: 5
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: chip.active ? Qt.alpha("white", 0.24) : Qt.alpha(root.ink, 0.08)

                                            Label {
                                                anchors.centerIn: parent
                                                text: root.document.effectKey(chip.index)
                                                font.pixelSize: 11
                                                font.bold: true
                                                color: chip.active ? "white" : root.ink
                                            }
                                        }
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: root.document.effectName(chip.index)
                                            font.pixelSize: 12
                                            color: chip.active ? "white" : root.ink
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Label {
                        leftPadding: 4
                        topPadding: 4
                        text: qsTr("Bend")
                        font.pixelSize: 11
                        color: root.ink
                        opacity: 0.6
                    }

                    // The bend's form, then how far it goes.
                    Row {
                        spacing: 4

                        BendForm {
                            form: 1
                            tip: qsTr("Bend")
                            path: "M3 19 L7 19 C11 19 12 15 12 6 M9 9 L12 6 L15 9"
                        }
                        BendForm {
                            form: 2
                            tip: qsTr("Bend and release")
                            path: "M2 19 L4 19 C7 19 8 14 8 6 M5.5 8.5 L8 6 L10.5 8.5 M8 6 L12 6 C15 6 16 12 16 19 M13.5 16.5 L16 19 L18.5 16.5"
                        }
                        BendForm {
                            form: 3
                            tip: qsTr("Pre-bend")
                            path: "M12 20 L12 5 M8.5 8.5 L12 5 L15.5 8.5"
                        }
                        BendForm {
                            form: 4
                            tip: qsTr("Pre-bend and release")
                            path: "M7 20 L7 5 M4 8 L7 5 L10 8 M7 5 L10 5 C13 5 15 11 15 19 M12.5 16.5 L15 19 L17.5 16.5"
                        }

                        Item {
                            width: 9
                            height: 30

                            Separator {
                                anchors.centerIn: parent
                                height: 18
                            }
                        }

                        Repeater {
                            model: [50, 100, 200, 300, 400]

                            delegate: BendAmount {
                                required property int modelData

                                cents: modelData
                            }
                        }
                    }

                    Label {
                        leftPadding: 4
                        topPadding: 4
                        text: qsTr("Slide")
                        font.pixelSize: 11
                        color: root.ink
                        opacity: 0.6
                    }

                    // How the caret's notes slide in, then out (ADR-0058).
                    Row {
                        spacing: 4

                        EffectKind {
                            tip: root.document.slideInName(1)
                            path: "M3 19 L11 12 M13 12 A2.5 2.5 0 1 0 18 12 A2.5 2.5 0 1 0 13 12"
                            active: root.document.caretSlideIn === 1
                            onClicked: root.document.setSlideIn(active ? 0 : 1)
                        }
                        EffectKind {
                            tip: root.document.slideInName(2)
                            path: "M3 5 L11 12 M13 12 A2.5 2.5 0 1 0 18 12 A2.5 2.5 0 1 0 13 12"
                            active: root.document.caretSlideIn === 2
                            onClicked: root.document.setSlideIn(active ? 0 : 2)
                        }

                        Item {
                            width: 9
                            height: 30

                            Separator {
                                anchors.centerIn: parent
                                height: 18
                            }
                        }

                        EffectKind {
                            tip: root.document.slideOutName(1)
                            path: "M2 16 A2.5 2.5 0 1 0 7 16 A2.5 2.5 0 1 0 2 16 M8.5 14.5 L15.5 9.5 M17 8 A2.5 2.5 0 1 0 22 8 A2.5 2.5 0 1 0 17 8"
                            active: root.document.caretSlideOut === 1
                            onClicked: root.document.setSlideOut(active ? 0 : 1)
                        }
                        EffectKind {
                            tip: root.document.slideOutName(2)
                            path: "M2 18 A2.5 2.5 0 1 0 7 18 A2.5 2.5 0 1 0 2 18 M8.5 16.5 L15.5 11.5 M17 10 A2.5 2.5 0 1 0 22 10 A2.5 2.5 0 1 0 17 10 M4.5 13 Q11 3 19.5 5.5"
                            active: root.document.caretSlideOut === 2
                            onClicked: root.document.setSlideOut(active ? 0 : 2)
                        }
                        EffectKind {
                            tip: root.document.slideOutName(3)
                            path: "M6 12 A2.5 2.5 0 1 0 11 12 A2.5 2.5 0 1 0 6 12 M13 12 L21 19"
                            active: root.document.caretSlideOut === 3
                            onClicked: root.document.setSlideOut(active ? 0 : 3)
                        }
                        EffectKind {
                            tip: root.document.slideOutName(4)
                            path: "M6 12 A2.5 2.5 0 1 0 11 12 A2.5 2.5 0 1 0 6 12 M13 12 L21 5"
                            active: root.document.caretSlideOut === 4
                            onClicked: root.document.setSlideOut(active ? 0 : 4)
                        }
                    }

                    Label {
                        leftPadding: 4
                        topPadding: 4
                        text: qsTr("Harmonic, vibrato and accent")
                        font.pixelSize: 11
                        color: root.ink
                        opacity: 0.6
                    }

                    // The harmonic's kind (only with strings), the vibrato's
                    // width and the accent's strength.
                    Row {
                        spacing: 4

                        Repeater {
                            model: ["N.H.", "A.H.", "P.H.", "T.H.", "S.H."]

                            delegate: EffectKind {
                                required property int index
                                required property string modelData

                                tip: root.document.harmonicName(index + 1)
                                label: modelData
                                enabled: root.document.caretFretted
                                active: root.document.caretHarmonic === index + 1
                                onClicked: root.document.setHarmonic(active ? 0 : index + 1)
                            }
                        }

                        Item {
                            width: 9
                            height: 30

                            Separator {
                                anchors.centerIn: parent
                                height: 18
                            }
                        }

                        EffectKind {
                            tip: qsTr("Slight vibrato")
                            path: "M3 12 C5 9 7 9 9 12 C11 15 13 15 15 12 C17 9 19 9 21 12"
                            active: root.document.caretVibrato === 1
                            onClicked: root.document.setVibrato(active ? 0 : 1)
                        }
                        EffectKind {
                            tip: qsTr("Wide vibrato")
                            path: "M3 12 C5 5 7 5 9 12 C11 19 13 19 15 12 C17 5 19 5 21 12"
                            active: root.document.caretVibrato === 2
                            onClicked: root.document.setVibrato(active ? 0 : 2)
                        }

                        Item {
                            width: 9
                            height: 30

                            Separator {
                                anchors.centerIn: parent
                                height: 18
                            }
                        }

                        EffectKind {
                            tip: qsTr("Accent")
                            path: "M7 7 L17 12 L7 17"
                            active: root.document.caretAccent === 1
                            onClicked: root.document.setAccent(active ? 0 : 1)
                        }
                        EffectKind {
                            tip: qsTr("Marcato")
                            path: "M7 17 L12 7 L17 17"
                            active: root.document.caretAccent === 2
                            onClicked: root.document.setAccent(active ? 0 : 2)
                        }
                    }

                    Label {
                        leftPadding: 4
                        topPadding: 4
                        text: qsTr("Beat")
                        font.pixelSize: 11
                        color: root.ink
                        opacity: 0.6
                    }

                    // The beat's strum, pick stroke and tremolo picking.
                    Row {
                        spacing: 4

                        BeatEffect {
                            tip: qsTr("Strum down")
                            path: "M12 4 L12 20 M7 15 L12 20 L17 15"
                            active: root.document.caretStrum === 1
                            onClicked: root.document.setStrum(active ? 0 : 1)
                        }
                        BeatEffect {
                            tip: qsTr("Strum up")
                            path: "M12 20 L12 4 M7 9 L12 4 L17 9"
                            active: root.document.caretStrum === 2
                            onClicked: root.document.setStrum(active ? 0 : 2)
                        }

                        Item {
                            width: 9
                            height: 30

                            Separator {
                                anchors.centerIn: parent
                                height: 18
                            }
                        }

                        BeatEffect {
                            tip: qsTr("Down stroke")
                            path: "M6 18 L6 7 L18 7 L18 18"
                            active: root.document.caretPickStroke === 1
                            onClicked: root.document.setPickStroke(active ? 0 : 1)
                        }
                        BeatEffect {
                            tip: qsTr("Up stroke")
                            path: "M7 6 L12 18 L17 6"
                            active: root.document.caretPickStroke === 2
                            onClicked: root.document.setPickStroke(active ? 0 : 2)
                        }

                        Item {
                            width: 9
                            height: 30

                            Separator {
                                anchors.centerIn: parent
                                height: 18
                            }
                        }

                        BeatEffect {
                            tip: qsTr("Tremolo picking in eighths")
                            path: "M7 14 L17 10"
                            active: root.document.caretTremolo === 8
                            onClicked: root.document.setTremolo(active ? 0 : 8)
                        }
                        BeatEffect {
                            tip: qsTr("Tremolo picking in sixteenths")
                            path: "M7 11 L17 7 M7 17 L17 13"
                            active: root.document.caretTremolo === 16
                            onClicked: root.document.setTremolo(active ? 0 : 16)
                        }
                        BeatEffect {
                            tip: qsTr("Tremolo picking in thirty-seconds")
                            path: "M7 8 L17 4 M7 13 L17 9 M7 18 L17 14"
                            active: root.document.caretTremolo === 32
                            onClicked: root.document.setTremolo(active ? 0 : 32)
                        }

                        Item {
                            width: 9
                            height: 30

                            Separator {
                                anchors.centerIn: parent
                                height: 18
                            }
                        }

                        // Tapping, slap and pop (ADR-0060), lettered as the
                        // score writes them.
                        Repeater {
                            model: ["T", "S", "P"]

                            delegate: EffectKind {
                                required property int index
                                required property string modelData

                                tip: root.document.bassTechniqueName(index + 1)
                                label: modelData
                                active: root.document.caretBassTechnique === index + 1
                                onClicked: root.document.setBassTechnique(active ? 0 : index + 1)
                            }
                        }
                    }

                    Label {
                        leftPadding: 4
                        topPadding: 4
                        text: qsTr("Whammy bar")
                        font.pixelSize: 11
                        color: root.ink
                        opacity: 0.6
                    }

                    // The whammy bar's form, then how deep it dives.
                    Row {
                        spacing: 4

                        WhammyForm {
                            form: 1
                            path: "M3 7 L8 7 L13 17 L21 17"
                        }
                        WhammyForm {
                            form: 2
                            path: "M3 7 L8 7 L12 17 L16 7 L21 7"
                        }
                        WhammyForm {
                            form: 3
                            path: "M5 5 L5 17 L21 17"
                        }
                        WhammyForm {
                            form: 4
                            path: "M5 5 L5 17 L10 17 L16 7 L21 7"
                        }

                        Item {
                            width: 9
                            height: 30

                            Separator {
                                anchors.centerIn: parent
                                height: 18
                            }
                        }

                        Repeater {
                            model: [-100, -200, -300, -400, -600]

                            delegate: WhammyAmount {
                                required property int modelData

                                cents: modelData
                            }
                        }
                    }
                }
            }
        }

        Tool {
            tip: qsTr("Measure: signatures, insert, delete (Ctrl+M)")
            onClicked: root.measureRequested()

            contentItem: Icon {
                path: "M4 7 L4 17 M20 7 L20 17 M4 9 L20 9 M4 12 L20 12 M4 15 L20 15 M12 7 L12 17"
                color: root.ink
            }
        }

        Item {
            width: 8
            height: 34

            Separator {
                anchors.centerIn: parent
            }
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 4
            rightPadding: 10
            text: root.document.caretStatus
            font.pixelSize: Theme.fontSmall
            font.features: { "tnum": 1 }
            color: root.ink
            opacity: 0.8
        }
    }
}
