// SPDX-License-Identifier: AGPL-3.0-or-later
// The note effects palette, docked at the left of the score: every technique,
// articulation, dynamic and line the caret's note or beat can take, in named
// groups. A tile is lit while the caret has what it sets, and a click sets it
// or takes it off; the tiles with a key show it in their corner. It never
// takes the keyboard from the score, and never covers it.
import QtQuick
import QtQuick.Controls
import Elara.App

Rectangle {
    id: root

    required property ScoreDocument document

    /// Whether the palette shows; the window slides it in and out.
    property bool opened: true
    /// Its width while it shows: six tiles and their margins.
    readonly property real fullWidth: 236
    /// A tile is as wide as a tool and a little lower, so that the groups a
    /// player uses most show without scrolling.
    readonly property real tileHeight: 30

    /// Whether its groups have been made: once it has shown.
    property bool made: false

    onVisibleChanged: {
        if (visible)
            made = true;
    }
    Component.onCompleted: made = visible

    function open() {
        opened = true;
    }
    function close() {
        opened = false;
    }

    /// How far the next bend goes when the caret's note has none, in cents.
    property int bendCents: 200
    /// How far the next dive goes when the caret's beat has none (ADR-0060).
    property int whammyCents: -200

    // A bend or dive amount as printed: quarter steps as fraction characters,
    // drawn at the size of the digits beside them ("1 1/2" becomes a 1 and a
    // half sign, which the font's fraction feature would shrink along with
    // them), and a minus sign for a dive.
    function amountText(text) {
        return text.replace(/ ?1\/4/g, "¼").replace(/ ?1\/2/g, "½")
                   .replace(/ ?3\/4/g, "¾").replace(/^-/, "−");
    }

    // The keyed effects, in the order of core::NoteEffect: each drawn as the
    // score writes it, lettered as it does, or its sign in the music font.
    readonly property var keyed: [
        { path: Icons.tie },
        { label: "H/P" },
        { path: Icons.slide },
        { path: Icons.bend },
        { path: Icons.vibrato },
        { label: "P.M." },
        { path: Icons.deadNote },
        { path: Icons.letRing },
        { path: Icons.harmonic },
        { path: Icons.ghostNote },
        { symbol: "\uE4A0", size: 36 },
        { symbol: "\uE4A2", size: 64 },
        { symbol: "\uE4A4", size: 36 },
        { symbol: "\uE4A6", size: 48 }
    ]

    color: Theme.bar
    clip: true

    // A tile of the palette: lit in the accent while the caret has it.
    component Tile: AbstractButton {
        id: tile

        property bool active: false
        property string tip
        readonly property color ink: active ? Theme.accentInk : Theme.ink

        implicitWidth: Theme.tool
        implicitHeight: root.tileHeight
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        opacity: enabled ? 1.0 : 0.38
        Accessible.role: Accessible.CheckBox
        Accessible.name: tip
        Accessible.checked: active

        background: Rectangle {
            radius: Theme.radiusSmall
            color: tile.pressed ? Theme.pressed : tile.active ? Theme.accentWash
                                                               : tile.hovered ? Theme.hover : "transparent"
            border.width: tile.active ? 1 : 0
            border.color: Qt.alpha(Theme.accentInk, 0.4)
        }

        ToolTip.visible: hovered && tip.length > 0
        ToolTip.delay: 500
        ToolTip.text: tip
    }

    // A tile drawn as a line icon.
    component IconTile: Tile {
        id: iconTile

        property string path

        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                path: iconTile.path
                color: iconTile.ink
            }
        }
    }

    // A tile lettered as the score letters it; as wide as its letters need.
    component LabelTile: Tile {
        id: labelTile

        property string label
        property bool bold: true

        implicitWidth: Math.max(Theme.tool, Math.ceil(letters.implicitWidth) + 14)

        contentItem: Label {
            id: letters

            text: labelTile.label
            font.pixelSize: labelTile.bold ? Theme.fontCaption : Theme.fontSmall
            font.weight: labelTile.bold ? Font.Bold : Font.Normal
            color: labelTile.ink
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // A tile showing a glyph of the music font.
    component SymbolTile: Tile {
        id: symbolTile

        property string symbol
        property real size: 24

        contentItem: Item {
            MusicSymbol {
                anchors.centerIn: parent
                symbol: symbolTile.symbol
                size: symbolTile.size
                color: symbolTile.ink
            }
        }
    }

    // A row of tiles that wraps.
    component Tiles: Flow {
        width: parent ? parent.width : 0
        spacing: 4
    }

    // A hairline between two kinds of tile in a row.
    component Gap: Item {
        width: 5
        height: root.tileHeight

        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: 18
            color: Theme.line
        }
    }

    // A named group that folds: its heading hides or shows its rows.
    component Group: Column {
        id: group

        property string title
        property bool open: true
        default property alias rows: body.data

        width: parent ? parent.width : 0
        spacing: 2

        AbstractButton {
            id: heading

            width: parent.width
            height: 24
            focusPolicy: Qt.NoFocus
            hoverEnabled: true
            Accessible.name: group.title
            onClicked: group.open = !group.open

            contentItem: Item {
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 18
                    text: group.title
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    color: heading.hovered ? Theme.ink : Theme.inkSoft
                    elide: Text.ElideRight
                }
                Icon {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    path: group.open ? Icons.chevronDown : Icons.chevronRight
                    color: heading.hovered ? Theme.ink : Theme.inkSoft
                }
            }
        }

        Column {
            id: body

            width: parent.width
            visible: group.open
            spacing: 4
        }
    }

    // A bend form (ADR-0055), numbered as core::BendForm: on the caret's notes
    // at the bend's amount, or off again when they have it.
    component BendForm: IconTile {
        required property int form

        active: root.document.caretBendForm === form
        Accessible.role: Accessible.RadioButton
        onClicked: {
            if (active) {
                root.document.setBend(0, 0);
            } else {
                const cents = root.document.caretBendCents;
                root.document.setBend(form, cents > 0 ? cents : root.bendCents);
            }
        }
    }

    // How far a bend goes: the caret's bend, or a bend up if it has none.
    component BendAmount: LabelTile {
        required property int cents

        bold: false
        label: root.amountText(root.document.bendAmount(cents))
        tip: root.document.bendAmount(cents)
        active: root.document.caretBendForm > 0 && root.document.caretBendCents === cents
        Accessible.role: Accessible.RadioButton
        onClicked: {
            const form = root.document.caretBendForm;
            root.bendCents = cents;
            root.document.setBend(form > 0 && form < 5 ? form : 1, cents);
        }
    }

    // A whammy-bar form (ADR-0060), numbered as core::BendForm and drawn going
    // down: on the caret's beat at the dive's depth, or off when it has it.
    component WhammyForm: IconTile {
        required property int form

        readonly property bool diving: root.document.caretWhammyCents < 0

        active: diving && root.document.caretWhammyForm === form
        tip: root.document.whammyName(form, diving ? root.document.caretWhammyCents : root.whammyCents)
        Accessible.role: Accessible.RadioButton
        onClicked: {
            if (active) {
                root.document.setWhammy(0, 0);
            } else {
                root.document.setWhammy(form, diving ? root.document.caretWhammyCents
                                                     : root.whammyCents);
            }
        }
    }

    // How deep a dive goes: the caret's whammy bar, or a dive if it has none.
    component WhammyAmount: LabelTile {
        required property int cents

        readonly property int form: root.document.caretWhammyForm

        bold: false
        label: root.amountText(root.document.whammyAmount(cents))
        tip: root.document.whammyAmount(cents)
        active: form > 0 && form < 5 && root.document.caretWhammyCents === cents
        Accessible.role: Accessible.RadioButton
        onClicked: {
            root.whammyCents = cents;
            root.document.setWhammy(form > 0 && form < 5 ? form : 1, cents);
        }
    }

    // A dynamic (ADR-0047), numbered as core::Dynamic and written as the score
    // writes it: lit while it is the caret's; a click sets it from the caret's
    // notes to the next change.
    component DynamicKind: SymbolTile {
        required property int dynamic

        size: 24
        active: root.document.caretDynamic === dynamic
        tip: root.document.dynamicName(dynamic)
        Accessible.role: Accessible.RadioButton
        onClicked: root.document.setDynamic(dynamic)
    }

    // The groups are made when the palette first shows: a window without a
    // score has no use for a hundred tiles.
    Loader {
        anchors.fill: parent
        anchors.rightMargin: 1
        active: root.made

        sourceComponent: Flickable {
            id: flick

            contentWidth: width
            contentHeight: groups.implicitHeight + 24
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ScrollBar.vertical: Scroll {}

            Column {
                id: groups

                x: 12
                y: 8
                width: root.fullWidth - 24
                spacing: 6
                // Tools for the caret: dimmed while there is none.
                enabled: root.document.caretPage >= 0
                opacity: enabled ? 1.0 : 0.38

                Group {
                    title: qsTr("Note effects")

                    Tiles {
                        Repeater {
                            model: root.document.effectCount

                            delegate: Tile {
                                id: keyedTile

                                required property int index
                                readonly property var drawn: root.keyed[index]
                                readonly property string key: root.document.effectKey(index)

                                // Techniques of strings show only on a fretted staff.
                                visible: root.document.caretFretted
                                         || !root.document.effectNeedsStrings(index)
                                active: (root.document.caretEffects & (1 << index)) !== 0
                                tip: qsTr("%1 (%2)").arg(root.document.effectName(index)).arg(key)
                                onClicked: root.document.toggleEffect(index)

                                contentItem: Item {
                                    Icon {
                                        anchors.centerIn: parent
                                        visible: keyedTile.drawn.path !== undefined
                                        path: keyedTile.drawn.path ?? ""
                                        color: keyedTile.ink
                                    }
                                    Label {
                                        anchors.centerIn: parent
                                        anchors.verticalCenterOffset: 2
                                        visible: keyedTile.drawn.label !== undefined
                                        text: keyedTile.drawn.label ?? ""
                                        font.pixelSize: 10
                                        font.weight: Font.Bold
                                        color: keyedTile.ink
                                    }
                                    MusicSymbol {
                                        anchors.centerIn: parent
                                        visible: keyedTile.drawn.symbol !== undefined
                                        symbol: keyedTile.drawn.symbol ?? ""
                                        size: keyedTile.drawn.size ?? 24
                                        color: keyedTile.ink
                                    }
                                    // The key that does the same, when it is a letter: a comma or
                                    // a quote in a corner is a speck, and the tooltip names it.
                                    Label {
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.rightMargin: 3
                                        visible: /^[A-Za-z0-9]$/.test(keyedTile.key)
                                        text: keyedTile.key
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        color: keyedTile.active ? Theme.accentInk : Theme.inkSoft
                                    }
                                }
                            }
                        }
                    }
                }

                Group {
                    title: qsTr("Bend")
                    visible: root.document.caretFretted

                    // The bend's form, then how far it goes.
                    Tiles {
                        BendForm {
                            form: 1
                            tip: qsTr("Bend")
                            path: Icons.bend
                        }
                        BendForm {
                            form: 2
                            tip: qsTr("Bend and release")
                            path: Icons.bendRelease
                        }
                        BendForm {
                            form: 3
                            tip: qsTr("Pre-bend")
                            path: Icons.prebend
                        }
                        BendForm {
                            form: 4
                            tip: qsTr("Pre-bend and release")
                            path: Icons.prebendRelease
                        }
                    }
                    Tiles {
                        Repeater {
                            model: [50, 100, 200, 300, 400]

                            delegate: BendAmount {
                                required property int modelData

                                cents: modelData
                            }
                        }
                    }
                }

                Group {
                    title: qsTr("Slide")
                    visible: root.document.caretFretted

                    // How the caret's notes slide in, then out (ADR-0058).
                    Tiles {
                        IconTile {
                            tip: root.document.slideInName(1)
                            path: Icons.slideInBelow
                            active: root.document.caretSlideIn === 1
                            onClicked: root.document.setSlideIn(active ? 0 : 1)
                        }
                        IconTile {
                            tip: root.document.slideInName(2)
                            path: Icons.slideInAbove
                            active: root.document.caretSlideIn === 2
                            onClicked: root.document.setSlideIn(active ? 0 : 2)
                        }
                        IconTile {
                            tip: root.document.slideOutName(1)
                            path: Icons.slide
                            active: root.document.caretSlideOut === 1
                            onClicked: root.document.setSlideOut(active ? 0 : 1)
                        }
                        IconTile {
                            tip: root.document.slideOutName(2)
                            path: Icons.slideLegato
                            active: root.document.caretSlideOut === 2
                            onClicked: root.document.setSlideOut(active ? 0 : 2)
                        }
                        IconTile {
                            tip: root.document.slideOutName(3)
                            path: Icons.slideOutDown
                            active: root.document.caretSlideOut === 3
                            onClicked: root.document.setSlideOut(active ? 0 : 3)
                        }
                        IconTile {
                            tip: root.document.slideOutName(4)
                            path: Icons.slideOutUp
                            active: root.document.caretSlideOut === 4
                            onClicked: root.document.setSlideOut(active ? 0 : 4)
                        }
                    }
                }

                Group {
                    title: qsTr("Harmonic")
                    visible: root.document.caretFretted

                    // The harmonic's kind, lettered as the score letters it.
                    Tiles {
                        Repeater {
                            model: ["N.H.", "A.H.", "P.H.", "T.H.", "S.H."]

                            delegate: LabelTile {
                                required property int index
                                required property string modelData

                                tip: root.document.harmonicName(index + 1)
                                label: modelData
                                active: root.document.caretHarmonic === index + 1
                                onClicked: root.document.setHarmonic(active ? 0 : index + 1)
                            }
                        }
                    }
                }

                Group {
                    title: qsTr("Vibrato, accent and ornaments")

                    // The vibrato's width, the accent's strength and the fingers.
                    Tiles {
                        IconTile {
                            tip: qsTr("Slight vibrato")
                            path: Icons.vibrato
                            active: root.document.caretVibrato === 1
                            onClicked: root.document.setVibrato(active ? 0 : 1)
                        }
                        IconTile {
                            tip: qsTr("Wide vibrato")
                            path: Icons.vibratoWide
                            active: root.document.caretVibrato === 2
                            onClicked: root.document.setVibrato(active ? 0 : 2)
                        }
                        SymbolTile {
                            tip: qsTr("Accent")
                            symbol: "\uE4A0"
                            size: 36
                            active: root.document.caretAccent === 1
                            onClicked: root.document.setAccent(active ? 0 : 1)
                        }
                        SymbolTile {
                            tip: qsTr("Marcato")
                            symbol: "\uE4AC"
                            size: 36
                            active: root.document.caretAccent === 2
                            onClicked: root.document.setAccent(active ? 0 : 2)
                        }
                        // The finger that plays the caret's notes, numbered as
                        // core::Finger: each click the next one, then none. On a
                        // fretted staff 1 is the index and T the thumb, which
                        // comes last (ADR-0093).
                        LabelTile {
                            readonly property bool fretted: root.document.caretFretted
                            readonly property int finger: root.document.caretFingering

                            tip: qsTr("Fingering: again for the next finger")
                            label: (fretted ? ["1", "T", "1", "2", "3", "4"]
                                            : ["1", "1", "2", "3", "4", "5"])[finger]
                            active: finger !== 0
                            onClicked: root.document.setFingering(
                                           fretted ? [2, 0, 3, 4, 5, 1][finger] : (finger + 1) % 6)
                        }
                        // The finger that plucks them on a fretted staff: p, i, m,
                        // a, c, then none.
                        LabelTile {
                            visible: root.document.caretFretted
                            tip: qsTr("Plucking finger: again for the next one")
                            label: ["p", "p", "i", "m", "a", "c"][root.document.caretPluck]
                            active: root.document.caretPluck !== 0
                            onClicked: root.document.setPluck((root.document.caretPluck + 1) % 6)
                        }
                    }
                    Tiles {
                        // To the next step of the key (ADR-0090).
                        SymbolTile {
                            tip: qsTr("Trill")
                            symbol: "\uE566"
                            size: 28
                            active: root.document.caretTrill !== 0
                            onClicked: root.document.toggleTrill()
                        }
                        // Mordents and turns, numbered as core::Ornament (ADR-0091).
                        SymbolTile {
                            tip: qsTr("Mordent")
                            symbol: "\uE56D"
                            size: 28
                            active: root.document.caretOrnament === 1
                            onClicked: root.document.setOrnament(active ? 0 : 1)
                        }
                        SymbolTile {
                            tip: qsTr("Upper mordent")
                            symbol: "\uE56C"
                            size: 28
                            active: root.document.caretOrnament === 2
                            onClicked: root.document.setOrnament(active ? 0 : 2)
                        }
                        SymbolTile {
                            tip: qsTr("Turn")
                            symbol: "\uE567"
                            size: 28
                            active: root.document.caretOrnament === 3
                            onClicked: root.document.setOrnament(active ? 0 : 3)
                        }
                        SymbolTile {
                            tip: qsTr("Inverted turn")
                            symbol: "\uE568"
                            size: 28
                            active: root.document.caretOrnament === 4
                            onClicked: root.document.setOrnament(active ? 0 : 4)
                        }
                    }
                }

                Group {
                    title: qsTr("Dynamics")

                    // From the caret's notes to the next change (ADR-0047).
                    Tiles {
                        Repeater {
                            model: ["", "", "", "", "",
                                "", "", ""]

                            delegate: DynamicKind {
                                required property int index
                                required property string modelData

                                dynamic: index
                                symbol: modelData
                            }
                        }
                        // The caret's beat under a hairpin (ADR-0078).
                        IconTile {
                            tip: qsTr("Crescendo")
                            path: Icons.crescendo
                            active: root.document.caretHairpin === 1
                            onClicked: root.document.setHairpin(active ? 0 : 1)
                        }
                        IconTile {
                            tip: qsTr("Diminuendo")
                            path: Icons.diminuendo
                            active: root.document.caretHairpin === 2
                            onClicked: root.document.setHairpin(active ? 0 : 2)
                        }
                    }
                }

                Group {
                    title: qsTr("Phrasing and lines")

                    Tiles {
                        // The caret's beat slurred to the next (ADR-0086).
                        IconTile {
                            tip: qsTr("Slur to the next note")
                            path: Icons.slur
                            active: root.document.caretSlur
                            onClicked: root.document.setSlur(!active)
                        }
                        // A small slashed note leading into the caret's beat (ADR-0079).
                        IconTile {
                            tip: qsTr("Grace note")
                            path: Icons.graceNote
                            active: root.document.caretGraceNotes > 0
                            onClicked: root.document.toggleGraceNote()
                        }
                        Gap {}
                        // The caret's beat held under a fermata, numbered as
                        // core::Fermata: short, normal and long (ADR-0085).
                        SymbolTile {
                            tip: qsTr("Short fermata")
                            symbol: "\uE4C4"
                            size: 28
                            active: root.document.caretFermata === 1
                            onClicked: root.document.setFermata(active ? 0 : 1)
                        }
                        SymbolTile {
                            tip: qsTr("Fermata")
                            symbol: "\uE4C0"
                            size: 28
                            active: root.document.caretFermata === 2
                            onClicked: root.document.setFermata(active ? 0 : 2)
                        }
                        SymbolTile {
                            tip: qsTr("Long fermata")
                            symbol: "\uE4C6"
                            size: 28
                            active: root.document.caretFermata === 3
                            onClicked: root.document.setFermata(active ? 0 : 3)
                        }
                    }
                    Tiles {
                        // The caret's beat under an octave line, numbered as
                        // core::Ottava: a second click makes 8va 15ma and 8vb
                        // 15mb, a third takes the line off (ADR-0089).
                        SymbolTile {
                            tip: qsTr("Octave line up: 8va, again for 15ma")
                            symbol: "\uE511"
                            size: 22
                            active: root.document.caretOttava === 1 || root.document.caretOttava === 3
                            onClicked: root.document.setOttava(root.document.caretOttava === 1
                                                               ? 3 : (active ? 0 : 1))
                        }
                        SymbolTile {
                            tip: qsTr("Octave line down: 8vb, again for 15mb")
                            symbol: "\uE51C"
                            size: 22
                            active: root.document.caretOttava === 2 || root.document.caretOttava === 4
                            onClicked: root.document.setOttava(root.document.caretOttava === 2
                                                               ? 4 : (active ? 0 : 2))
                        }
                        // The sustain pedal under the caret's beat, numbered as
                        // core::Pedal: down, a second click changes it there, a
                        // third lifts it (ADR-0092).
                        SymbolTile {
                            tip: qsTr("Sustain pedal: again to change it")
                            symbol: "\uE650"
                            size: 18
                            active: root.document.caretPedal !== 0
                            onClicked: root.document.setPedal((root.document.caretPedal + 1) % 3)
                        }
                        // A break after the caret's beat, numbered as core::Breath:
                        // a breath mark, a second click a caesura, a third none
                        // (ADR-0095).
                        SymbolTile {
                            tip: qsTr("Breath mark: again for a caesura")
                            symbol: root.document.caretBreath === 2 ? "\uE4D1" : "\uE4CE"
                            size: root.document.caretBreath === 2 ? 24 : 44
                            active: root.document.caretBreath !== 0
                            onClicked: root.document.setBreath((root.document.caretBreath + 1) % 3)
                        }
                    }
                }

                Group {
                    title: qsTr("Beat")

                    // The beat's strum, pick stroke and tremolo picking.
                    Tiles {
                        IconTile {
                            tip: root.document.caretFretted ? qsTr("Strum down") : qsTr("Roll upwards")
                            path: Icons.strumDown
                            active: root.document.caretStrum === 1
                            onClicked: root.document.setStrum(active ? 0 : 1)
                        }
                        IconTile {
                            tip: root.document.caretFretted ? qsTr("Strum up") : qsTr("Roll downwards")
                            path: Icons.strumUp
                            active: root.document.caretStrum === 2
                            onClicked: root.document.setStrum(active ? 0 : 2)
                        }
                        Gap {}
                        SymbolTile {
                            tip: qsTr("Down stroke")
                            symbol: "\uE610"
                            size: 32
                            active: root.document.caretPickStroke === 1
                            onClicked: root.document.setPickStroke(active ? 0 : 1)
                        }
                        SymbolTile {
                            tip: qsTr("Up stroke")
                            symbol: "\uE612"
                            size: 30
                            active: root.document.caretPickStroke === 2
                            onClicked: root.document.setPickStroke(active ? 0 : 2)
                        }
                    }
                    Tiles {
                        IconTile {
                            tip: qsTr("Tremolo picking in eighths")
                            path: Icons.tremolo8
                            active: root.document.caretTremolo === 8
                            onClicked: root.document.setTremolo(active ? 0 : 8)
                        }
                        IconTile {
                            tip: qsTr("Tremolo picking in sixteenths")
                            path: Icons.tremolo16
                            active: root.document.caretTremolo === 16
                            onClicked: root.document.setTremolo(active ? 0 : 16)
                        }
                        IconTile {
                            tip: qsTr("Tremolo picking in thirty-seconds")
                            path: Icons.tremolo32
                            active: root.document.caretTremolo === 32
                            onClicked: root.document.setTremolo(active ? 0 : 32)
                        }
                        Gap {
                            visible: root.document.caretFretted
                        }
                        // Tapping, slap and pop (ADR-0060), lettered as the
                        // score writes them; techniques of strings.
                        Repeater {
                            model: ["T", "S", "P"]

                            delegate: LabelTile {
                                required property int index
                                required property string modelData

                                tip: root.document.bassTechniqueName(index + 1)
                                label: modelData
                                visible: root.document.caretFretted
                                active: root.document.caretBassTechnique === index + 1
                                onClicked: root.document.setBassTechnique(active ? 0 : index + 1)
                            }
                        }
                    }
                }

                Group {
                    title: qsTr("Whammy bar")
                    // A guitar's, so only on a fretted staff.
                    visible: root.document.caretFretted

                    // The whammy bar's form, then how deep it dives.
                    Tiles {
                        WhammyForm {
                            form: 1
                            path: Icons.whammyDive
                        }
                        WhammyForm {
                            form: 2
                            path: Icons.whammyDip
                        }
                        WhammyForm {
                            form: 3
                            path: Icons.whammyPredive
                        }
                        WhammyForm {
                            form: 4
                            path: Icons.whammyPrediveRelease
                        }
                    }
                    Tiles {
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
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.line
    }
}
