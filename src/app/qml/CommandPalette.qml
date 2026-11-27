// SPDX-License-Identifier: AGPL-3.0-or-later
// The command palette (ADR-0135): every command of the window, found by typing
// a few letters of its name, and shown with its keys. It lists what the menus
// hold, the entry tools and every note effect, and goes where a measure
// number or a section's name says; "tempo 120" and "3/4" set the tempo and the
// time signature at the caret's measure. The commands used last come first,
// the latest chosen, so its keys and Enter repeat it.
import QtQuick
import QtQuick.Controls
import Elara.App

Popup {
    id: root

    objectName: "commandPalette"

    /// The menus whose commands it offers.
    required property AppMenuBar menus
    required property ScoreDocument document
    // What is at the document's caret, read and edited.
    required property CaretEditor editor
    required property AudioController playback
    /// The score: the caret is brought into view after an edit at it.
    required property ScoreView view
    /// Whether the commands used last are kept for the next start.
    property alias remember: search.remember
    /// How far from the window's top it floats: under the command bar.
    property real top: 64

    /// Every command it offers while it shows: maps of `id`, `name`,
    /// `group`, `words`, `keys`, `enabled`, `chosen`, `needs` (what it waits
    /// for, when it cannot run), `atCaret` and `run`.
    property var commands: []
    /// The rows shown: a heading (`heading`) or a command (`command`, with its
    /// name as rich text in `marked`).
    property var rows: []
    /// The row chosen, which Enter runs.
    property int current: -1

    readonly property bool hasScore: document.pageCount > 0
    readonly property bool hasCaret: document.caretPage >= 0
    readonly property var chosenCommand: current >= 0 && current < rows.length
                                         ? rows[current].command ?? null : null
    // For the checks: the chosen command's id, how many it offers and shows.
    readonly property string chosenId: chosenCommand ? chosenCommand.id : ""
    readonly property int commandCount: commands.length
    readonly property int rowCount: rows.length

    /// Opens it with `text` typed and the best command chosen: with nothing
    /// typed, the one used last.
    function show(text: string) {
        gather();
        field.text = text;
        refresh();
        open();
        field.forceActiveFocus();
        field.selectAll();
    }

    /// Runs the chosen command. It closes first, unless `stay`, and the
    /// command runs once it has: the measure, the chord or the lyrics open at
    /// the caret, the dialogs over the window.
    function runChosen(stay: bool) {
        const command = chosenCommand;
        if (!command || !command.enabled)
            return;
        search.used(command.id);
        if (!stay) {
            close();
            if (hasScore)
                view.forceActiveFocus();
        }
        Qt.callLater(() => {
            command.run();
            if (command.atCaret)
                view.reveal();
            // Kept open, it shows what the command changed.
            if (stay)
                refreshLater.restart();
        });
    }

    /// Moves the choice `step` rows, over the headings.
    function choose(step: int) {
        if (rows.length === 0)
            return;
        let at = current;
        let left = Math.abs(step);
        const direction = step > 0 ? 1 : -1;
        while (left > 0) {
            let next = at + direction;
            while (next >= 0 && next < rows.length && rows[next].heading !== undefined)
                next += direction;
            if (next < 0 || next >= rows.length)
                break;
            at = next;
            --left;
        }
        current = at;
        // A heading over the row chosen shows with it.
        if (current > 0 && rows[current - 1].heading !== undefined)
            list.positionViewAtIndex(current - 1, ListView.Contain);
        list.positionViewAtIndex(current, ListView.Contain);
    }

    // ---- What it offers ----------------------------------------------------

    /// A command, with what is not given left plain.
    function entry(id: string, group: string, name: string, run, options): var {
        const given = options ?? {};
        return {
            id: id,
            group: group,
            name: name,
            run: run,
            // Keys in the platform's words; `shownKeys` as they are.
            keys: given.shownKeys ?? (given.keys ? KeyNames.native(given.keys) : ""),
            words: given.words ?? "",
            enabled: given.enabled ?? true,
            chosen: given.chosen ?? false,
            needs: given.needs ?? "",
            atCaret: given.atCaret ?? false
        };
    }

    /// What a command at the caret waits for, or nothing when it can run.
    function caretNeeds(strings: bool): string {
        if (!hasScore)
            return qsTr("Open or start a song first.");
        if (!hasCaret)
            return qsTr("Click a staff to put the caret there first.");
        if (strings && !editor.caretFretted)
            return qsTr("Needs a track with strings.");
        return "";
    }

    /// A command that edits what is at the caret; `strings` when only a staff
    /// with strings has it.
    function atCaret(id: string, group: string, name: string, run, options): var {
        const given = options ?? {};
        const needs = caretNeeds(given.strings ?? false);
        given.enabled = needs.length === 0;
        given.needs = needs;
        given.atCaret = true;
        return entry(id, group, name, run, given);
    }

    /// The commands of `menu` and of its submenus, under `path`.
    function menuCommands(menu, path: string, enabled: bool, into) {
        for (let at = 0; at < menu.count; ++at) {
            const item = menu.itemAt(at);
            if (!item || item.text === undefined || item.text.length === 0)
                continue;
            if (item.subMenu) {
                menuCommands(item.subMenu, path + " › " + item.subMenu.title.replace("&", ""),
                             enabled && item.enabled, into);
                continue;
            }
            // What the menu leaves out now, and the palette itself.
            if (item.shown === false || item.objectName === "findCommand")
                continue;
            const runs = enabled && item.enabled;
            into.push(entry("menu:" + path + "/" + (item.objectName.length > 0 ? item.objectName
                                                                                : item.text),
                            path, item.text, () => item.triggered(), {
                                shownKeys: item.keys ?? "",
                                enabled: runs,
                                chosen: item.chosen ?? false,
                                needs: runs || hasScore ? "" : qsTr("Open or start a song first.")
                            }));
        }
    }

    /// Puts the caret and playback on `measure` (from 0).
    function goToMeasure(measure: int) {
        document.caretToMeasure(measure);
        const seconds = document.measureStart(measure);
        if (seconds >= 0)
            playback.seek(seconds);
    }

    // The English words that also find the keyed note effects, in the order
    // of core::NoteEffect: a translated name is still found by them.
    readonly property var effectWords: ["tie", "hammer-on pull-off H/P legato", "slide", "bend",
        "vibrato", "palm mute P.M.", "dead note muted x", "let ring", "natural harmonic N.H.",
        "ghost note parentheses", "accent", "staccato", "tenuto", "staccatissimo"]
    readonly property var dynamicWords: ({
            "ppp": "pianississimo", "pp": "pianissimo", "p": "piano", "mp": "mezzo-piano",
            "mf": "mezzo-forte", "f": "forte", "ff": "fortissimo", "fff": "fortississimo"
        })

    /// Every command the palette offers now, in the order it lists them.
    function gather() {
        const all = [];
        for (let at = 0; at < menus.count; ++at) {
            const menu = menus.menuAt(at);
            if (menu && menu.objectName !== "helpMenu")
                menuCommands(menu, menu.title.replace("&", ""), true, all);
        }
        entryCommands(all);
        effectCommands(all);
        placeCommands(all);
        const help = menus.menuAt(menus.count - 1);
        if (help && help.objectName === "helpMenu")
            menuCommands(help, help.title.replace("&", ""), true, all);
        // One command for a name: at the caret the first, which for an effect
        // is the one with its key; elsewhere one in each group.
        const seen = new Set();
        commands = all.filter(command => {
            const key = (command.atCaret ? "" : command.group + "\u0001") + command.name;
            if (seen.has(key))
                return false;
            seen.add(key);
            return true;
        });
        search.setCommands(commands.map(command => ({
                                            id: command.id,
                                            name: command.name,
                                            group: command.group,
                                            words: command.words,
                                            enabled: command.enabled
                                        })));
    }

    /// The entry tools: the note values, the dot, the tuplets, the voices,
    /// rests, deleting and the pitch.
    function entryCommands(into) {
        const group = qsTr("Entry");
        const values = [
            [1, qsTranslate("EntryBar", "Whole note"), "whole semibreve 1"],
            [2, qsTranslate("EntryBar", "Half note"), "half minim 2"],
            [4, qsTranslate("EntryBar", "Quarter note"), "quarter crotchet 4"],
            [8, qsTranslate("EntryBar", "Eighth note"), "eighth quaver 8"],
            [16, qsTranslate("EntryBar", "16th note"), "sixteenth semiquaver 16"],
            [32, qsTranslate("EntryBar", "32nd note"), "thirty-second demisemiquaver 32"],
            [64, qsTranslate("EntryBar", "64th note"), "sixty-fourth hemidemisemiquaver 64"]
        ];
        for (const [value, name, words] of values) {
            into.push(atCaret("entry.value." + value, group, name,
                              () => document.setInputValue(value),
                              { words: words + " duration note value",
                                chosen: document.inputValue === value }));
        }
        into.push(atCaret("entry.longer", group, qsTr("Longer note value"),
                          () => document.longerDuration(), { keys: "+", words: "duration" }));
        into.push(atCaret("entry.shorter", group, qsTr("Shorter note value"),
                          () => document.shorterDuration(), { keys: "-", words: "duration" }));
        into.push(atCaret("entry.dot", group, qsTr("Dotted note"), () => document.toggleDot(),
                          { keys: ".", words: "dot dotted", chosen: document.inputDotted }));
        const tuplets = [
            [2, qsTranslate("EntryBar", "Duplet (2 in the time of 3)")],
            [3, qsTranslate("EntryBar", "Triplet (3 in the time of 2)")],
            [4, qsTranslate("EntryBar", "Quadruplet (4 in the time of 3)")],
            [5, qsTranslate("EntryBar", "Quintuplet (5 in the time of 4)")],
            [6, qsTranslate("EntryBar", "Sextuplet (6 in the time of 4)")],
            [7, qsTranslate("EntryBar", "Septuplet (7 in the time of 4)")],
            [9, qsTranslate("EntryBar", "Nonuplet (9 in the time of 8)")]
        ];
        for (const [actual, name] of tuplets) {
            into.push(atCaret("entry.tuplet." + actual, group, name,
                              () => document.setInputTuplet(actual),
                              { keys: "Ctrl+" + actual, words: "tuplet",
                                chosen: document.inputTuplet === actual }));
        }
        into.push(atCaret("entry.tuplet.none", group, qsTranslate("EntryBar", "No tuplet"),
                          () => document.setInputTuplet(0), { words: "tuplet" }));
        for (let voice = 1; voice <= 4; ++voice) {
            const chosenVoice = voice;
            into.push(atCaret("entry.voice." + voice, group,
                              qsTranslate("EntryBar", "Voice %1").arg(voice),
                              () => document.setCaretVoice(chosenVoice),
                              { keys: "Alt+" + voice, words: "voice layer",
                                chosen: document.caretVoice === voice }));
        }
        into.push(atCaret("entry.rest", group, qsTr("Insert a rest"), () => editor.insertRest(),
                          { keys: "Ins", words: "rest silence" }));
        into.push(atCaret("entry.delete", group, qsTr("Delete the note"),
                          () => editor.deleteAtCaret(), { keys: "Del", words: "remove erase" }));
        into.push(atCaret("entry.deleteBeat", group, qsTr("Delete the beat"),
                          () => editor.deleteBeat(), { keys: "Ctrl+Del", words: "remove erase" }));
        const pitch = semitones => () => {
            if (document.hasSelection)
                document.transposeSelection(semitones);
            else
                editor.shiftPitch(semitones);
        };
        into.push(atCaret("entry.up", group, qsTr("Raise a semitone"), pitch(1),
                          { keys: "Alt+Up", words: "pitch transpose sharp" }));
        into.push(atCaret("entry.down", group, qsTr("Lower a semitone"), pitch(-1),
                          { keys: "Alt+Down", words: "pitch transpose flat" }));
        into.push(atCaret("entry.octaveUp", group, qsTr("Raise an octave"), pitch(12),
                          { keys: "Ctrl+Alt+Up", words: "pitch transpose" }));
        into.push(atCaret("entry.octaveDown", group, qsTr("Lower an octave"), pitch(-12),
                          { keys: "Ctrl+Alt+Down", words: "pitch transpose" }));
        into.push(atCaret("entry.drum", group, qsTr("Drum hit on the lane"),
                          () => editor.toggleDrum(), { keys: "Return", words: "drums percussion" }));
    }

    /// Every note effect of the palette at the left, in its groups.
    function effectCommands(into) {
        const fx = name => qsTranslate("EffectsPalette", name);
        let group = fx("Note effects");
        for (let effect = 0; effect < editor.effectCount; ++effect) {
            const which = effect;
            into.push(atCaret("effect." + effect, group, editor.effectName(effect),
                              () => editor.toggleEffect(which), {
                                  shownKeys: editor.effectKey(effect),
                                  words: effectWords[effect] ?? "",
                                  strings: editor.effectNeedsStrings(effect),
                                  chosen: (editor.caretEffects & (1 << effect)) !== 0
                              }));
        }

        group = fx("Bend");
        const bendForms = [fx("Bend"), fx("Bend and release"), fx("Pre-bend"),
                           fx("Pre-bend and release")];
        bendForms.forEach((name, index) => {
            const form = index + 1;
            const on = editor.caretBendForm === form;
            into.push(atCaret("bend.form." + form, group, name, () => {
                if (on)
                    editor.setBend(0, 0);
                else
                    editor.setBend(form, editor.caretBendCents > 0 ? editor.caretBendCents : 200);
            }, { strings: true, chosen: on, words: "bend" }));
        });
        for (const cents of [50, 100, 200, 300, 400]) {
            const form = editor.caretBendForm;
            into.push(atCaret("bend.amount." + cents, group,
                              qsTr("Bend %1").arg(editor.bendAmount(cents)),
                              () => editor.setBend(form > 0 && form < 5 ? form : 1, cents), {
                                  strings: true, words: "bend amount",
                                  chosen: form > 0 && editor.caretBendCents === cents
                              }));
        }

        group = fx("Slide");
        for (const kind of [1, 2]) {
            const on = editor.caretSlideIn === kind;
            into.push(atCaret("slide.in." + kind, group, editor.slideInName(kind),
                              () => editor.setSlideIn(on ? 0 : kind),
                              { strings: true, chosen: on, words: "slide" }));
        }
        for (const kind of [1, 2, 3, 4]) {
            const on = editor.caretSlideOut === kind;
            into.push(atCaret("slide.out." + kind, group, editor.slideOutName(kind),
                              () => editor.setSlideOut(on ? 0 : kind),
                              { strings: true, chosen: on, words: "slide" }));
        }

        group = fx("Harmonic");
        ["N.H.", "A.H.", "P.H.", "T.H.", "S.H."].forEach((sign, index) => {
            const kind = index + 1;
            const on = editor.caretHarmonic === kind;
            into.push(atCaret("harmonic." + kind, group, editor.harmonicName(kind),
                              () => editor.setHarmonic(on ? 0 : kind),
                              { strings: true, chosen: on, words: sign + " harmonic" }));
        });

        group = fx("Vibrato, accent and ornaments");
        const choice = (id, name, current, value, set, words) => {
            const on = current === value;
            into.push(atCaret(id, group, name, () => set(on ? 0 : value),
                              { chosen: on, words: words }));
        };
        choice("vibrato.slight", fx("Slight vibrato"), editor.caretVibrato, 1,
               value => editor.setVibrato(value), "vibrato");
        choice("vibrato.wide", fx("Wide vibrato"), editor.caretVibrato, 2,
               value => editor.setVibrato(value), "vibrato");
        choice("accent.accent", fx("Accent"), editor.caretAccent, 1,
               value => editor.setAccent(value), "accent >");
        choice("accent.marcato", fx("Marcato"), editor.caretAccent, 2,
               value => editor.setAccent(value), "accent ^ marcato");
        const fretted = editor.caretFretted;
        const finger = editor.caretFingering;
        into.push(atCaret("fingering", group, fx("Fingering: again for the next finger"),
                          () => editor.setFingering(fretted ? [2, 0, 3, 4, 5, 1][finger]
                                                            : (finger + 1) % 6),
                          { chosen: finger !== 0, words: "fingering finger" }));
        into.push(atCaret("pluck", group, fx("Plucking finger: again for the next one"),
                          () => editor.setPluck((editor.caretPluck + 1) % 6), {
                              strings: true, chosen: editor.caretPluck !== 0,
                              words: "plucking finger p i m a"
                          }));
        into.push(atCaret("trill", group, fx("Trill"), () => editor.toggleTrill(),
                          { chosen: editor.caretTrill !== 0, words: "tr trill" }));
        choice("ornament.mordent", fx("Mordent"), editor.caretOrnament, 1,
               value => editor.setOrnament(value), "mordent ornament");
        choice("ornament.upper", fx("Upper mordent"), editor.caretOrnament, 2,
               value => editor.setOrnament(value), "upper mordent ornament");
        choice("ornament.turn", fx("Turn"), editor.caretOrnament, 3,
               value => editor.setOrnament(value), "turn ornament");
        choice("ornament.inverted", fx("Inverted turn"), editor.caretOrnament, 4,
               value => editor.setOrnament(value), "inverted turn ornament");

        group = fx("Dynamics");
        for (let dynamic = 0; dynamic < 8; ++dynamic) {
            const which = dynamic;
            const sign = editor.dynamicName(dynamic);
            into.push(atCaret("dynamic." + sign, group, qsTr("Dynamic %1").arg(sign),
                              () => editor.setDynamic(which), {
                                  chosen: editor.caretDynamic === dynamic,
                                  words: sign + " " + (dynamicWords[sign] ?? "") + " dynamic"
                              }));
        }
        choice("hairpin.crescendo", fx("Crescendo"), editor.caretHairpin, 1,
               value => editor.setHairpin(value), "crescendo cresc hairpin <");
        choice("hairpin.diminuendo", fx("Diminuendo"), editor.caretHairpin, 2,
               value => editor.setHairpin(value), "diminuendo decrescendo dim hairpin >");

        group = fx("Phrasing and lines");
        into.push(atCaret("slur", group, fx("Slur to the next note"),
                          () => editor.setSlur(!editor.caretSlur),
                          { chosen: editor.caretSlur, words: "slur phrase legato" }));
        into.push(atCaret("grace", group, fx("Grace note"), () => editor.toggleGraceNote(),
                          { chosen: editor.caretGraceNotes > 0, words: "grace appoggiatura" }));
        choice("fermata.short", fx("Short fermata"), editor.caretFermata, 1,
               value => editor.setFermata(value), "fermata pause hold");
        choice("fermata.normal", fx("Fermata"), editor.caretFermata, 2,
               value => editor.setFermata(value), "fermata pause hold");
        choice("fermata.long", fx("Long fermata"), editor.caretFermata, 3,
               value => editor.setFermata(value), "fermata pause hold");
        const ottava = editor.caretOttava;
        into.push(atCaret("ottava.up", group, fx("Octave line up: 8va, again for 15ma"),
                          () => editor.setOttava(ottava === 1 ? 3 : (ottava === 1 || ottava === 3
                                                                      ? 0 : 1)),
                          { chosen: ottava === 1 || ottava === 3, words: "8va 15ma ottava octave" }));
        into.push(atCaret("ottava.down", group, fx("Octave line down: 8vb, again for 15mb"),
                          () => editor.setOttava(ottava === 2 ? 4 : (ottava === 2 || ottava === 4
                                                                      ? 0 : 2)),
                          { chosen: ottava === 2 || ottava === 4, words: "8vb 15mb ottava octave" }));
        into.push(atCaret("pedal", group, fx("Sustain pedal: again to change it"),
                          () => editor.setPedal((editor.caretPedal + 1) % 3),
                          { chosen: editor.caretPedal !== 0, words: "sustain pedal ped" }));
        into.push(atCaret("breath", group, fx("Breath mark: again for a caesura"),
                          () => editor.setBreath((editor.caretBreath + 1) % 3),
                          { chosen: editor.caretBreath !== 0, words: "breath caesura comma" }));

        group = fx("Beat");
        choice("strum.down", editor.caretFretted ? fx("Strum down") : fx("Roll upwards"),
               editor.caretStrum, 1, value => editor.setStrum(value), "strum rasgueado arpeggio roll");
        choice("strum.up", editor.caretFretted ? fx("Strum up") : fx("Roll downwards"),
               editor.caretStrum, 2, value => editor.setStrum(value), "strum rasgueado arpeggio roll");
        choice("stroke.down", fx("Down stroke"), editor.caretPickStroke, 1,
               value => editor.setPickStroke(value), "pick stroke downstroke picking");
        choice("stroke.up", fx("Up stroke"), editor.caretPickStroke, 2,
               value => editor.setPickStroke(value), "pick stroke upstroke picking");
        choice("tremolo.8", fx("Tremolo picking in eighths"), editor.caretTremolo, 8,
               value => editor.setTremolo(value), "tremolo picking");
        choice("tremolo.16", fx("Tremolo picking in sixteenths"), editor.caretTremolo, 16,
               value => editor.setTremolo(value), "tremolo picking");
        choice("tremolo.32", fx("Tremolo picking in thirty-seconds"), editor.caretTremolo, 32,
               value => editor.setTremolo(value), "tremolo picking");
        ["tapping T", "slap S", "pop P"].forEach((words, index) => {
            const technique = index + 1;
            const on = editor.caretBassTechnique === technique;
            into.push(atCaret("bass." + technique, group, editor.bassTechniqueName(technique),
                              () => editor.setBassTechnique(on ? 0 : technique),
                              { strings: true, chosen: on, words: words + " bass" }));
        });

        group = fx("Whammy bar");
        const diving = editor.caretWhammyCents < 0;
        const depth = diving ? editor.caretWhammyCents : -200;
        for (const form of [1, 2, 3, 4]) {
            const on = diving && editor.caretWhammyForm === form;
            into.push(atCaret("whammy.form." + form, group, editor.whammyName(form, depth),
                              () => editor.setWhammy(on ? 0 : form, on ? 0 : depth),
                              { strings: true, chosen: on, words: "whammy bar tremolo arm dive" }));
        }
        for (const cents of [-100, -200, -300, -400, -600]) {
            const form = editor.caretWhammyForm;
            into.push(atCaret("whammy.amount." + cents, group,
                              qsTr("Whammy bar %1").arg(editor.whammyAmount(cents)),
                              () => editor.setWhammy(form > 0 && form < 5 ? form : 1, cents), {
                                  strings: true, words: "whammy bar dive depth",
                                  chosen: form > 0 && editor.caretWhammyCents === cents
                              }));
        }
    }

    /// Where to go: the first and the last measure, the measures beside the
    /// caret's, each section, and the tracks to show.
    function placeCommands(into) {
        let group = qsTr("Go to");
        const needs = hasScore ? "" : qsTr("Open or start a song first.");
        const place = { enabled: hasScore, needs: needs, atCaret: true };
        into.push(entry("goto.first", group, qsTr("The first measure"), () => goToMeasure(0),
                        Object.assign({ words: "start beginning go to" }, place)));
        into.push(entry("goto.last", group, qsTr("The last measure"),
                        () => goToMeasure(document.measureCount - 1),
                        Object.assign({ words: "end go to" }, place)));
        into.push(entry("goto.next", group, qsTr("The next measure"),
                        () => document.moveCaret(ScoreDocument.NextMeasure),
                        Object.assign({ keys: "Ctrl+Right", words: "go to" }, place)));
        into.push(entry("goto.previous", group, qsTr("The previous measure"),
                        () => document.moveCaret(ScoreDocument.PreviousMeasure),
                        Object.assign({ keys: "Ctrl+Left", words: "go to" }, place)));
        for (const mark of hasScore ? document.sectionMarks : []) {
            const measure = document.measureAt(mark.seconds);
            into.push(entry("goto.section." + measure, group,
                            qsTr("Section “%1”, measure %2").arg(mark.name).arg(measure + 1),
                            () => goToMeasure(measure),
                            Object.assign({ words: "section rehearsal mark part go to" }, place)));
        }

        if (document.trackNames.length < 2)
            return;
        group = qsTr("Tracks");
        into.push(entry("track.all", group, qsTranslate("Main", "All tracks"),
                        () => document.track = ScoreDocument.kAllTracks,
                        { chosen: document.track < 0, words: "show tracks" }));
        document.trackNames.forEach((name, index) => {
            into.push(entry("track." + index, group, qsTr("Show “%1”").arg(name),
                            () => document.track = index,
                            { chosen: document.track === index, words: "show track" }));
        });
    }

    /// What the text says beyond the commands' names: a measure number, a
    /// tempo, a time signature. Each a command, or none.
    function typedCommands(text: string): var {
        const typed = [];
        const goTo = qsTr("Go to");
        const measure = text.match(/^\s*[mbc]?\s*(\d{1,4})\s*$/i);
        if (measure) {
            const number = parseInt(measure[1]);
            const inSong = number >= 1 && number <= document.measureCount;
            typed.push(entry("typed.measure", goTo, qsTr("Measure %1").arg(number),
                             () => goToMeasure(number - 1), {
                                 enabled: hasScore && inSong, atCaret: true,
                                 needs: !hasScore ? qsTr("Open or start a song first.")
                                        : qsTr("The song has %n measure(s).", "",
                                               document.measureCount)
                             }));
        }
        const tempo = text.match(/^\s*(?:tempo|bpm|q\s*=)\s*(\d{1,3})\s*$/i)
                      ?? text.match(/^\s*(\d{1,3})\s*bpm\s*$/i);
        if (tempo) {
            const bpm = parseInt(tempo[1]);
            const from = document.caretMeasure;
            const needs = caretNeeds(false);
            typed.push(entry("typed.tempo", qsTr("Measure"),
                             qsTr("Tempo %1 from measure %2").arg(bpm).arg(from + 1),
                             () => document.setTempoFrom(from, bpm), {
                                 enabled: needs.length === 0 && bpm >= 10 && bpm <= 999,
                                 needs: needs.length > 0 ? needs
                                        : qsTr("A tempo is 10 to 999 beats a minute."),
                                 atCaret: true
                             }));
        }
        const time = text.match(/^\s*(\d{1,2})\s*\/\s*(\d{1,2})\s*$/);
        if (time) {
            const numerator = parseInt(time[1]);
            const denominator = parseInt(time[2]);
            const from = document.caretMeasure;
            const needs = caretNeeds(false);
            const valid = numerator >= 1 && numerator <= 32
                          && [1, 2, 4, 8, 16, 32].includes(denominator);
            typed.push(entry("typed.time", qsTr("Measure"),
                             qsTr("Time signature %1/%2 from measure %3")
                                 .arg(numerator).arg(denominator).arg(from + 1),
                             () => document.setTimeSignatureFrom(from, numerator, denominator), {
                                 enabled: needs.length === 0 && valid,
                                 needs: needs.length > 0 ? needs
                                        : qsTr("%1/%2 is not a time signature.")
                                              .arg(numerator).arg(denominator),
                                 atCaret: true
                             }));
        }
        return typed;
    }

    /// The rows for the text typed, the best command chosen.
    function refresh() {
        const text = field.text;
        const shown = [];
        for (const command of typedCommands(text))
            shown.push({ command: command, marked: escaped(command.name) });
        const found = search.search(text);
        const empty = text.trim().length === 0;
        let headed = "";
        for (const match of found) {
            if (empty) {
                const heading = match.recent ? qsTr("Used last") : qsTr("All commands");
                if (heading !== headed) {
                    shown.push({ heading: heading });
                    headed = heading;
                }
            }
            shown.push({ command: commands[match.index], marked: match.marked });
        }
        rows = shown;
        current = shown.findIndex(row => row.heading === undefined);
        list.positionViewAtBeginning();
    }

    function escaped(text: string): string {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    // ---- How it looks ------------------------------------------------------

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round(Math.min(top, parent.height * 0.2))
    width: Math.min(620, parent.width - 32)
    padding: 0
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.quick
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: 80
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

    /// Whether it is fading out, closed but still shown.
    property bool leaving: false

    onAboutToShow: leaving = false
    onAboutToHide: leaving = true

    CommandSearch {
        id: search
    }

    // While it shows, the window's shortcuts are held (it closes on Escape),
    // so its own keys are here: they close it, or open it again as it fades.
    Shortcut {
        sequences: ["Ctrl+E", "Ctrl+Shift+P"]
        enabled: root.visible
        onActivated: root.leaving ? root.show("") : root.close()
    }

    // A change at the caret while it shows (Shift+Enter keeps it open) is
    // what the next commands see.
    Connections {
        target: root.document
        enabled: root.opened

        function onCaretChanged() {
            refreshLater.restart();
        }
        function onSongChanged() {
            refreshLater.restart();
        }
    }

    Timer {
        id: refreshLater

        interval: 0
        onTriggered: {
            const kept = root.current;
            root.gather();
            root.refresh();
            root.current = Math.min(kept, root.rows.length - 1);
        }
    }

    contentItem: Column {
        Item {
            width: parent.width
            height: 48

            Icon {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                path: Icons.search
                color: Theme.inkSoft
            }
            Field {
                id: field

                objectName: "commandField"
                x: 40
                width: parent.width - x - 10
                anchors.verticalCenter: parent.verticalCenter
                implicitHeight: 36
                font.pixelSize: Theme.fontTitle
                placeholderText: qsTr("Find a command, or type a measure number")
                background: Item {}
                Accessible.name: qsTr("Find a command")
                onTextEdited: root.refresh()

                Keys.onPressed: event => {
                    const shift = (event.modifiers & Qt.ShiftModifier) !== 0;
                    if (event.key === Qt.Key_Down) {
                        root.choose(1);
                    } else if (event.key === Qt.Key_Up) {
                        root.choose(-1);
                    } else if (event.key === Qt.Key_PageDown) {
                        root.choose(8);
                    } else if (event.key === Qt.Key_PageUp) {
                        root.choose(-8);
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        root.runChosen(shift);
                    } else {
                        return;
                    }
                    event.accepted = true;
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.line
        }

        ListView {
            id: list

            objectName: "commandList"
            width: parent.width
            height: Math.min(contentHeight, 10 * 32 + 26) + (count > 0 ? 12 : 0)
            topMargin: count > 0 ? 6 : 0
            bottomMargin: count > 0 ? 6 : 0
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: Scroll {}

            delegate: Item {
                id: row

                required property var modelData
                required property int index

                readonly property var command: modelData.command ?? null
                readonly property bool isCurrent: index === root.current

                width: ListView.view.width
                height: command ? 32 : 26

                // A heading: "Used last", "All commands".
                Label {
                    x: 16
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    visible: !row.command
                    text: row.modelData.heading ?? ""
                    font.pixelSize: Theme.fontCaption
                    font.weight: Font.DemiBold
                    color: Theme.inkSoft
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    visible: !!row.command
                    radius: Theme.radiusSmall
                    color: row.isCurrent ? Theme.hover : rowArea.containsMouse ? Qt.alpha(Theme.ink, 0.04)
                                                                               : "transparent"
                }

                Loader {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    active: !!row.command && row.command.chosen

                    sourceComponent: Icon {
                        path: Icons.check
                        lineWidth: 2.2
                        color: Theme.accentInk
                    }
                }

                Label {
                    id: nameLabel

                    x: 40
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, row.width - x - detail.implicitWidth - 32)
                    visible: !!row.command
                    text: row.modelData.marked ?? ""
                    textFormat: Text.StyledText
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontBody
                    color: Theme.ink
                    opacity: row.command && row.command.enabled ? 1.0 : 0.38
                }

                // Where it belongs, then its keys.
                Row {
                    id: detail

                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !!row.command
                    spacing: 12
                    opacity: row.command && row.command.enabled ? 1.0 : 0.38

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, 200)
                        text: row.command ? row.command.group : ""
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.fontSmall
                        color: Theme.inkSoft
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !!row.command && row.command.keys.length > 0
                        width: keysLabel.implicitWidth + 12
                        height: 20
                        radius: 4
                        color: "transparent"
                        border.color: Theme.line

                        Label {
                            id: keysLabel

                            anchors.centerIn: parent
                            text: row.command ? row.command.keys : ""
                            font.pixelSize: Theme.fontCaption
                            color: Theme.inkSoft
                        }
                    }
                }

                MouseArea {
                    id: rowArea

                    anchors.fill: parent
                    enabled: !!row.command
                    hoverEnabled: true
                    onClicked: {
                        root.current = row.index;
                        root.runChosen(false);
                    }
                }
            }
        }

        // Nothing found: what it can find.
        Label {
            width: parent.width
            visible: root.rows.length === 0
            topPadding: 14
            bottomPadding: 14
            leftPadding: 16
            rightPadding: 16
            text: qsTr("Nothing is called “%1”. Try another word, a measure number, a section’s name, “tempo 120” or “3/4”.")
                      .arg(field.text)
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontBody
            color: Theme.inkSoft
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.line
        }

        // What the command chosen waits for, or how to use the palette.
        Label {
            id: hint

            objectName: "commandHint"
            width: parent.width
            leftPadding: 16
            rightPadding: 16
            topPadding: 7
            bottomPadding: 8
            text: root.chosenCommand && !root.chosenCommand.enabled && root.chosenCommand.needs.length > 0
                  ? root.chosenCommand.needs
                  : qsTr("↑↓ to choose · Enter to run · Shift+Enter to run and stay · Esc to close")
            elide: Text.ElideRight
            font.pixelSize: Theme.fontCaption
            color: Theme.inkSoft
        }
    }
}
