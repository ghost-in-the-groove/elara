// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/effects.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/names.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/song.hpp>

#include "score_document.hpp"
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>
#include <type_traits>
#include <vector>

namespace elara::app {

/// What is at the caret of a document, read and edited (ADR-0128): the notes
/// of its beat, their effects, and the chord, text and lyrics over it.
///
/// The document has the caret; this is what the effects palette, the lyrics
/// popup, the entry bar and the keys ask of the place it is on. Every edit
/// here is one of core's edits at a caret (entry.hpp), entered through the
/// document, so it is undoable and lays the score out again. Without a
/// document, or before a song is open, the readers answer as on an empty
/// beat and the edits do nothing.
class CaretEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        elara::app::ScoreDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString caretStatus READ caretStatus NOTIFY caretChanged)
    Q_PROPERTY(bool caretFretted READ caretFretted NOTIFY caretChanged)
    Q_PROPERTY(int caretEffects READ caretEffects NOTIFY caretChanged)
    Q_PROPERTY(int effectCount READ effectCount CONSTANT)
    Q_PROPERTY(int caretBendForm READ caretBendForm NOTIFY caretChanged)
    Q_PROPERTY(int caretBendCents READ caretBendCents NOTIFY caretChanged)
    Q_PROPERTY(int caretStrum READ caretStrum NOTIFY caretChanged)
    Q_PROPERTY(int caretHairpin READ caretHairpin NOTIFY caretChanged)
    Q_PROPERTY(int caretFermata READ caretFermata NOTIFY caretChanged)
    Q_PROPERTY(int caretOttava READ caretOttava NOTIFY caretChanged)
    Q_PROPERTY(int caretPedal READ caretPedal NOTIFY caretChanged)
    Q_PROPERTY(int caretBreath READ caretBreath NOTIFY caretChanged)
    Q_PROPERTY(bool caretSlur READ caretSlur NOTIFY caretChanged)
    Q_PROPERTY(int caretGraceNotes READ caretGraceNotes NOTIFY caretChanged)
    Q_PROPERTY(QString caretChord READ caretChord NOTIFY caretChanged)
    Q_PROPERTY(QString caretText READ caretText NOTIFY caretChanged)
    Q_PROPERTY(int caretPickStroke READ caretPickStroke NOTIFY caretChanged)
    Q_PROPERTY(int caretSlideIn READ caretSlideIn NOTIFY caretChanged)
    Q_PROPERTY(int caretSlideOut READ caretSlideOut NOTIFY caretChanged)
    Q_PROPERTY(int caretVibrato READ caretVibrato NOTIFY caretChanged)
    Q_PROPERTY(int caretAccent READ caretAccent NOTIFY caretChanged)
    Q_PROPERTY(int caretTrill READ caretTrill NOTIFY caretChanged)
    Q_PROPERTY(int caretOrnament READ caretOrnament NOTIFY caretChanged)
    Q_PROPERTY(int caretFingering READ caretFingering NOTIFY caretChanged)
    Q_PROPERTY(int caretPluck READ caretPluck NOTIFY caretChanged)
    Q_PROPERTY(QString caretFingeringLabel READ caretFingeringLabel NOTIFY caretChanged)
    Q_PROPERTY(QString caretPluckLabel READ caretPluckLabel NOTIFY caretChanged)
    Q_PROPERTY(int caretDynamic READ caretDynamic NOTIFY caretChanged)
    Q_PROPERTY(int caretHarmonic READ caretHarmonic NOTIFY caretChanged)
    Q_PROPERTY(int caretTremolo READ caretTremolo NOTIFY caretChanged)
    // The beat's whammy bar, as a core::BendForm and cents (negative below the
    // written pitch), and its bass technique (ADR-0060).
    Q_PROPERTY(int caretWhammyForm READ caretWhammyForm NOTIFY caretChanged)
    Q_PROPERTY(int caretWhammyCents READ caretWhammyCents NOTIFY caretChanged)
    Q_PROPERTY(int caretBassTechnique READ caretBassTechnique NOTIFY caretChanged)
    Q_PROPERTY(int caretClef READ caretClef NOTIFY caretChanged)
    Q_PROPERTY(int caretRepeat READ caretRepeat NOTIFY caretChanged)

public:
    explicit CaretEditor(QObject* parent = nullptr);

    [[nodiscard]] ScoreDocument* document() const { return document_; }
    void setDocument(ScoreDocument* document);

    /// Where the caret is, for a status line: "Measure 3 · Beat 2 · String 4 ·
    /// Fret 5 · A3", the beat's notes on a staff without strings, or the lane's
    /// kit piece on a drum staff ("Measure 1 · Beat 1 · Snare").
    [[nodiscard]] QString caretStatus() const;
    /// Whether the caret's staff has strings, so that digits type frets.
    [[nodiscard]] bool caretFretted() const;
    /// The fret of the note on the caret's string at its beat, if there is one.
    [[nodiscard]] std::optional<int> fretAtCaret() const;
    /// The notes the caret is on, low to high: the one on its string on a
    /// fretted staff, every note of the beat elsewhere.
    [[nodiscard]] std::vector<core::Pitch> pitchesAtCaret() const;
    /// Whether note names can be typed at the caret (its staff is not percussion).
    [[nodiscard]] bool caretPitched() const;
    /// Whether the caret is on a drum staff, where its string is a lane (ADR-0035).
    [[nodiscard]] bool caretOnDrums() const;
    /// The translated name of drum lane `lane` (1 at the top of the staff).
    [[nodiscard]] static QString laneName(int lane);

    Q_INVOKABLE void deleteAtCaret();
    Q_INVOKABLE void deleteBeat();
    Q_INVOKABLE void insertRest();
    /// Types a note name, 0 for C to 6 for B (ADR-0034): in the octave nearest
    /// the previous note, replacing the beat's notes or added above them.
    Q_INVOKABLE void typeNoteName(int step, bool addToChord);
    /// Moves the caret's note by `semitones` (12 for an octave).
    Q_INVOKABLE void shiftPitch(int semitones);
    /// On a drum staff, adds the lane's kit piece to the caret's beat or removes
    /// it when it is there (ADR-0035).
    Q_INVOKABLE void toggleDrum();
    /// The note effects typing toggles, numbered as core::NoteEffect
    /// (ADR-0037, ADR-0038).
    static constexpr int kEffectCount = static_cast<int>(core::kEnumCount<core::NoteEffect>);
    /// Toggles note effect `effect` on the caret's notes.
    Q_INVOKABLE void toggleEffect(int effect);
    /// The effects the caret's notes all have: bit `effect` for each.
    [[nodiscard]] int caretEffects() const;
    [[nodiscard]] int effectCount() const { return kEffectCount; }
    /// An effect's name, translated, and the key that toggles it.
    Q_INVOKABLE QString effectName(int effect) const;
    Q_INVOKABLE QString effectKey(int effect) const;
    /// The effect a typed character toggles, or -1.
    Q_INVOKABLE int effectForKey(const QString& text) const;
    /// Whether an effect is a technique of strings, which goes only on a
    /// fretted staff (core::needsStrings): the effects panel shows it there only.
    Q_INVOKABLE bool effectNeedsStrings(int effect) const;
    /// Sets the bend of the caret's notes (ADR-0055): `form` numbered as
    /// core::BendForm (0 takes the bend off), going up by `cents`.
    Q_INVOKABLE void setBend(int form, int cents);
    /// The form of the bend of the caret's first note, as core::BendForm, and
    /// how far it goes up in cents (0 without a bend).
    [[nodiscard]] int caretBendForm() const;
    [[nodiscard]] int caretBendCents() const;
    /// A bend as the status line names it, translated: "Bend and release 1/2".
    Q_INVOKABLE QString bendName(int form, int cents) const;
    /// How far a bend goes, as tablature writes it: "1/4", "1/2", "full", "1 1/2".
    Q_INVOKABLE QString bendAmount(int cents) const;
    /// The effects of the caret's beat (ADR-0056): strum and pick stroke as
    /// 0 none, 1 down, 2 up; tremolo picking as the repeated notes' value
    /// (8, 16, 32), 0 without it.
    Q_INVOKABLE void setStrum(int direction);
    Q_INVOKABLE void setPickStroke(int direction);
    Q_INVOKABLE void setTremolo(int value);
    /// Gives the caret's beat the whammy bar of `form` (a core::BendForm, 0
    /// takes it off) going `cents` from the written pitch, negative for a dive
    /// (ADR-0060).
    Q_INVOKABLE void setWhammy(int form, int cents);
    /// Taps, slaps or pops the caret's beat (a core::BassTechnique; 0 for none).
    Q_INVOKABLE void setBassTechnique(int technique);
    /// Puts the caret's beat under a crescendo or diminuendo (a core::Hairpin;
    /// 0 for none); neighbouring beats with the same one share a wedge (ADR-0078).
    Q_INVOKABLE void setHairpin(int hairpin);
    /// Holds the caret's beat, a note or a rest, under a fermata (a
    /// core::Fermata; 0 for none) (ADR-0085).
    Q_INVOKABLE void setFermata(int fermata);
    /// Puts the caret's beat under an octave line (a core::Ottava; 0 for
    /// none): its notes are written one or two octaves nearer the staff
    /// and sound where they did (ADR-0089).
    Q_INVOKABLE void setOttava(int ottava);
    /// Puts the caret's beat, a note or a rest, under the sustain pedal (a
    /// core::Pedal: 1 down, 2 changed there; 0 up) (ADR-0092).
    Q_INVOKABLE void setPedal(int pedal);
    /// The break after the caret's beat (ADR-0095), numbered as core::Breath:
    /// 0 none, 1 a breath mark, 2 a caesura.
    Q_INVOKABLE void setBreath(int breath);
    /// Slurs the caret's beat to the next beat of its voice, or takes the slur
    /// off; beats slurred in a row share one slur (ADR-0086).
    Q_INVOKABLE void setSlur(bool slurred);
    /// Adds a grace note an eighth before the caret's beat, a whole tone below
    /// its note (above an open string or the first fret), or takes the beat's
    /// grace notes off when it has some (ADR-0079).
    Q_INVOKABLE void toggleGraceNote();
    /// Names the caret's beat with a chord symbol, its diagram from the beat's
    /// frets on a fretted staff; an empty name takes it away (ADR-0080).
    Q_INVOKABLE void setChordSymbol(const QString& name);
    /// The names of the song's chord symbols in the order they first appear,
    /// at most 12: what the chord popup offers.
    Q_INVOKABLE QStringList songChords() const;
    /// Writes a text above the caret's beat; an empty one takes it away
    /// (ADR-0081).
    Q_INVOKABLE void setBeatText(const QString& text);
    /// The song's beat texts in the order they first appear, at most 12: what
    /// the text popup offers.
    Q_INVOKABLE QStringList songTexts() const;
    /// How many verses the caret's staff sings (ADR-0084).
    Q_INVOKABLE int caretVerseCount() const;
    /// Verse `verse` (0 for the first) of the caret's staff as typed
    /// (ADR-0084): "first", the measure it starts at, and "text".
    Q_INVOKABLE QVariantMap caretVerse(int verse) const;
    /// Types verse `verse` of the caret's staff from measure `first` on, a
    /// syllable for each note of its first voice, as one undo step; an empty
    /// text takes the verse away (ADR-0084). Returns how many syllables after
    /// the last note were left out.
    Q_INVOKABLE int setVerse(int verse, int first, const QString& text);
    /// "Dive -1", "Dip -1/2", "Inverted dip +1/2"...
    Q_INVOKABLE QString whammyName(int form, int cents) const;
    /// How far the whammy bar goes, in steps with its direction: "-1 1/2", "+1/2".
    Q_INVOKABLE QString whammyAmount(int cents) const;
    /// "Tapping", "Slap" or "Pop"; empty for none.
    Q_INVOKABLE QString bassTechniqueName(int technique) const;
    [[nodiscard]] int caretWhammyForm() const;
    [[nodiscard]] int caretWhammyCents() const;
    [[nodiscard]] int caretBassTechnique() const;
    [[nodiscard]] int caretStrum() const;
    [[nodiscard]] int caretHairpin() const;
    [[nodiscard]] int caretFermata() const;
    [[nodiscard]] int caretOttava() const;
    [[nodiscard]] int caretPedal() const;
    [[nodiscard]] int caretBreath() const;
    [[nodiscard]] bool caretSlur() const;
    [[nodiscard]] int caretGraceNotes() const;
    [[nodiscard]] QString caretChord() const;
    [[nodiscard]] QString caretText() const;
    [[nodiscard]] int caretPickStroke() const;
    [[nodiscard]] int caretTremolo() const;
    /// The kinds of the caret's note effects (ADR-0058), numbered as their
    /// enums in elara_core (0 none): core::SlideIn, core::SlideOut,
    /// core::Vibrato, core::Accent and core::HarmonicKind.
    Q_INVOKABLE void setSlideIn(int kind);
    Q_INVOKABLE void setSlideOut(int kind);
    Q_INVOKABLE void setVibrato(int kind);
    Q_INVOKABLE void setAccent(int kind);
    Q_INVOKABLE void setHarmonic(int kind);
    [[nodiscard]] int caretSlideIn() const;
    [[nodiscard]] int caretSlideOut() const;
    [[nodiscard]] int caretVibrato() const;
    [[nodiscard]] int caretAccent() const;
    /// How many semitones the caret's notes trill to (ADR-0090), 0 without a
    /// trill. toggleTrill() takes it off, or trills each note to the next step
    /// up in the key at thirty-seconds.
    [[nodiscard]] int caretTrill() const;
    Q_INVOKABLE void toggleTrill();
    /// The mordent or turn of the caret's notes (ADR-0091), numbered as
    /// core::Ornament (0 none), and the one setOrnament() gives them.
    [[nodiscard]] int caretOrnament() const;
    Q_INVOKABLE void setOrnament(int kind);
    /// The finger that plays the caret's notes and the one that plucks them
    /// (ADR-0093), numbered as core::Finger (0 none, 1 the thumb to 5 the
    /// little finger); setFingering() and setPluck() give one to all of them.
    /// Only a fretted staff has plucking fingers.
    [[nodiscard]] int caretFingering() const;
    [[nodiscard]] int caretPluck() const;
    Q_INVOKABLE void setFingering(int finger);
    Q_INVOKABLE void setPluck(int finger);
    /// What the fingering and the plucking finger buttons show: the caret's
    /// finger as the staff writes it, or with none the one a press gives.
    [[nodiscard]] QString caretFingeringLabel() const;
    [[nodiscard]] QString caretPluckLabel() const;
    /// Give the caret's notes the finger after theirs (core::nextFingering()),
    /// or the plucking finger after theirs (core::nextPluck()): a button pressed
    /// again and again steps through them.
    Q_INVOKABLE void nextFingering();
    Q_INVOKABLE void nextPluck();
    /// The dynamic at the caret (ADR-0047), numbered as core::Dynamic (0 ppp
    /// to 7 fff): the notes there, or the last ones struck before them.
    [[nodiscard]] int caretDynamic() const;
    /// Sets a dynamic from the caret's notes to the next change of dynamic, as
    /// one undo step ("Change dynamic").
    Q_INVOKABLE void setDynamic(int dynamic);
    /// "ppp" to "fff", as the score writes them.
    Q_INVOKABLE QString dynamicName(int dynamic) const;
    [[nodiscard]] int caretHarmonic() const;
    /// The kinds as the status line names them, translated: "Slide in from
    /// below", "Shift slide", "Artificial harmonic".
    Q_INVOKABLE QString slideInName(int kind) const;
    Q_INVOKABLE QString slideOutName(int kind) const;
    Q_INVOKABLE QString harmonicName(int kind) const;

    /// The clef in force at the caret (ADR-0087) as the measure popup offers
    /// it: 0 treble, 1 bass, 2 alto, 3 tenor, the octave forms as the plain
    /// ones; -1 on a drum staff or without a caret.
    [[nodiscard]] int caretClef() const;
    /// Changes the clef of the caret's staff from the caret's measure on, `clef`
    /// numbered as caretClef(); the staff keeps its octave (ADR-0087).
    Q_INVOKABLE void setClef(int clef);
    /// The clef in force on staff `staff` of track `track` at `measure`,
    /// numbered as caretClef(); -1 on a drum staff or for a place the song
    /// does not have.
    Q_INVOKABLE [[nodiscard]] int clefAt(int track, int staff, int measure) const;
    /// Changes the clef of that staff from `measure` on, as setClef() does at
    /// the caret, the caret staying where it is: for a clef picked on the
    /// page (ADR-0130).
    Q_INVOKABLE void setClefFrom(int track, int staff, int measure, int clef);
    /// How many measures the sign on the caret's measure repeats: 0 without
    /// one, 1 or 2 (ADR-0102).
    [[nodiscard]] int caretRepeat() const;
    /// Makes the caret's measure repeat the measure before it (1), or it and
    /// the next the two before them (2); 0 takes the sign away, writing out
    /// what it played (ADR-0102).
    Q_INVOKABLE void setMeasureRepeat(int measures);
signals:
    void documentChanged();
    /// The caret moved, or what is at it changed.
    void caretChanged();

private:
    /// The song being edited; null without a document or before a song is open.
    [[nodiscard]] const core::Song* editedSong() const {
        return document_ ? document_->editedSong() : nullptr;
    }
    /// Whether there is a song with a place for the caret: what an edit at
    /// the caret needs.
    [[nodiscard]] bool editing() const {
        const core::Song* song = editedSong();
        return song != nullptr && core::hasCaret(*song);
    }
    /// The song being edited, its caret and the duration typing uses; only
    /// where editedSong() is not null.
    [[nodiscard]] const core::Song& edited() const { return *editedSong(); }
    [[nodiscard]] const core::Caret& where() const { return document_->session().caret(); }
    [[nodiscard]] core::Duration inputDuration() const { return document_->session().input(); }

    /// The effects of the caret's beat, none without a caret.
    [[nodiscard]] core::BeatEffects beatEffectsAtCaret() const;
    /// Gives the caret's beat `value` in `field` (core::setBeatEffect()), or
    /// says why it cannot when nothing there takes it.
    template <typename Value>
    void setBeatEffect(Value core::BeatEffects::* field, std::type_identity_t<Value> value);
    /// The effects of the caret's first note, none without a caret.
    [[nodiscard]] core::NoteEffects noteEffectsAtCaret() const;
    /// Enters what `make` makes for the caret's notes, or says why it cannot
    /// when no note there takes it (core::Entry::missed); silent when they
    /// have it already. What `make` throws goes on to the caller.
    void changeNoteEffects(
        const std::function<core::Entry(const core::Song&, const core::Caret&)>& make);
    /// Makes an entry from the song being edited and enters it, reporting
    /// what it throws through editFailed().
    void runEntry(const std::function<core::Entry(const core::Song&)>& make);
    QPointer<ScoreDocument> document_;
};

} // namespace elara::app
