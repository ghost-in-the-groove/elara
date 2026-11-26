// SPDX-License-Identifier: AGPL-3.0-or-later
#include "caret_editor.hpp"

#include <elara/core/commands.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/lyrics.hpp>
#include <elara/core/measure.hpp>

#include "score_common.hpp"
#include <QChar>
#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace elara::app {

namespace {

// The context the editor's names are translated in: its own.
constexpr char kContext[] = "elara::app::CaretEditor";

// The note effects, in core::NoteEffect order, and their keys (ADR-0038):
// letters A to G type note names, so the effects take others.
struct EffectKey {
    const char* name;
    char16_t key;
};
constexpr std::array<EffectKey, CaretEditor::kEffectCount> kEffects{{
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Tie"), u'T'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Hammer-on or pull-off"), u'H'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Slide"), u'S'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Bend"), u'U'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Vibrato"), u'V'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Palm mute"), u'P'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Dead note"), u'X'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Let ring"), u'L'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Natural harmonic"), u'N'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Ghost note"), u'O'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Accent"), u'>'},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Staccato"), u'\''},
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Tenuto"), u'_'},        // ADR-0088
    {QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Staccatissimo"), u'"'}, // ADR-0094
}};
static_assert(std::ranges::none_of(kEffects,
                                   [](const EffectKey& effect) { return effect.name == nullptr; }),
              "every core::NoteEffect needs a name and a key here");

// The kit pieces of drum entry, by their names in elara_core.
constexpr std::array kDrumPieceNames{
    Translation{"Crash cymbal", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Crash cymbal")},
    Translation{"Closed hi-hat", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Closed hi-hat")},
    Translation{"Open hi-hat", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Open hi-hat")},
    Translation{"Ride cymbal", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Ride cymbal")},
    Translation{"Ride bell", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Ride bell")},
    Translation{"High tom", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "High tom")},
    Translation{"Mid tom", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Mid tom")},
    Translation{"Snare", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Snare")},
    Translation{"Side stick", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Side stick")},
    Translation{"Floor tom", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Floor tom")},
    Translation{"Bass drum", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Bass drum")},
    Translation{"Hi-hat pedal", QT_TRANSLATE_NOOP("elara::app::CaretEditor", "Hi-hat pedal")},
};

// A pitch with music signs and its octave: "C♯4", "B♭3".
QString pitchName(const core::Pitch& pitch) {
    constexpr std::string_view kLetters = "CDEFGAB";
    QString text(QChar::fromLatin1(kLetters[static_cast<std::size_t>(pitch.step())]));
    for (int i = 0; i < pitch.alter(); ++i) {
        text += QChar(0x266F);
    }
    for (int i = 0; i > pitch.alter(); --i) {
        text += QChar(0x266D);
    }
    return text + QString::number(pitch.octave());
}

} // namespace

CaretEditor::CaretEditor(QObject* parent) : QObject(parent) {}

void CaretEditor::setDocument(ScoreDocument* document) {
    if (document_ == document) {
        return;
    }
    if (document_) {
        disconnect(document_, nullptr, this, nullptr);
    }
    document_ = document;
    if (document_) {
        // The document says so when the caret moves and when the song under
        // it changes: either way, what is at the caret may be something else.
        connect(document_, &ScoreDocument::caretChanged, this, &CaretEditor::caretChanged);
    }
    emit documentChanged();
    emit caretChanged();
}

QString CaretEditor::caretStatus() const {
    if (!editing()) {
        return {};
    }
    const core::Song& song = edited();
    QStringList parts;
    if (document_->session().hasSelection()) {
        const int first = document_->selectionFirst() + 1;
        const int last = document_->selectionLast() + 1;
        parts.append(first == last ? tr("Measure %1 selected").arg(first)
                                   : tr("Measures %1-%2 selected").arg(first).arg(last));
    }
    parts.append(tr("Measure %1").arg(where().measure + 1));
    if (where().voice > 0) {
        parts.append(tr("Voice %1").arg(where().voice + 1));
    }
    parts.append(core::onFreeSlot(song, where()) ? tr("New beat")
                                                 : tr("Beat %1").arg(where().beat + 1));
    if (const QString chord = caretChord(); !chord.isEmpty()) {
        parts.append(tr("Chord %1").arg(chord)); // the beat's, before its notes
    }
    if (const QString text = caretText(); !text.isEmpty()) {
        parts.append(tr("\u201C%1\u201D").arg(text));
    }
    if (caretFretted()) {
        parts.append(tr("String %1").arg(where().string));
        if (const auto fret = fretAtCaret()) {
            parts.append(tr("Fret %1").arg(*fret));
        }
    }
    if (caretOnDrums()) {
        parts.append(laneName(where().string));
    }
    QStringList names;
    for (const core::Pitch& pitch : pitchesAtCaret()) {
        names.append(pitchName(pitch));
    }
    if (!names.isEmpty() && caretPitched()) {
        parts.append(names.join(QLatin1Char(' ')));
    }
    const int effects = caretEffects();
    const core::NoteEffects fx = noteEffectsAtCaret();
    if (fx.slideIn != core::SlideIn::None) {
        parts.append(slideInName(static_cast<int>(fx.slideIn)));
    }
    for (int effect = 0; effect < kEffectCount; ++effect) {
        if ((effects & (1 << effect)) == 0) {
            continue;
        }
        // Effects of several kinds say which (ADR-0055, ADR-0058).
        switch (static_cast<core::NoteEffect>(effect)) {
        case core::NoteEffect::Bend:
            parts.append(bendName(caretBendForm(), caretBendCents()));
            break;
        case core::NoteEffect::Slide:
            parts.append(slideOutName(static_cast<int>(fx.slideOut)));
            break;
        case core::NoteEffect::Harmonic:
            parts.append(harmonicName(static_cast<int>(fx.harmonic.kind)));
            break;
        case core::NoteEffect::Vibrato:
            parts.append(fx.vibrato == core::Vibrato::Wide ? tr("Wide vibrato")
                                                           : effectName(effect));
            break;
        case core::NoteEffect::Accent:
            parts.append(fx.accent == core::Accent::Marcato ? tr("Marcato") : effectName(effect));
            break;
        default:
            parts.append(effectName(effect));
            break;
        }
    }
    if (fx.trill.isTrill()) {
        parts.append(tr("Trill"));
    }
    switch (fx.ornament) {
    case core::Ornament::Mordent:
        parts.append(tr("Mordent"));
        break;
    case core::Ornament::UpperMordent:
        parts.append(tr("Upper mordent"));
        break;
    case core::Ornament::Turn:
        parts.append(tr("Turn"));
        break;
    case core::Ornament::InvertedTurn:
        parts.append(tr("Inverted turn"));
        break;
    case core::Ornament::None:
        break;
    }
    const auto latin = [](std::string_view text) {
        return QLatin1StringView(text.data(), static_cast<qsizetype>(text.size()));
    };
    if (fx.fingering != core::Finger::None) {
        parts.append(tr("Finger %1").arg(latin(core::fingeringText(fx.fingering, caretFretted()))));
    }
    if (fx.pluck != core::Finger::None) {
        parts.append(tr("Plucked with %1").arg(latin(core::pluckText(fx.pluck))));
    }
    // Then the beat's own effects.
    if (const int strum = caretStrum(); strum != 0) {
        parts.append(strum == 1 ? tr("Strum down") : tr("Strum up"));
    }
    if (const int stroke = caretPickStroke(); stroke != 0) {
        parts.append(stroke == 1 ? tr("Down stroke") : tr("Up stroke"));
    }
    if (const int tremolo = caretTremolo(); tremolo != 0) {
        parts.append(tr("Tremolo picking 1/%1").arg(tremolo));
    }
    if (const int whammy = caretWhammyForm(); whammy != 0) {
        parts.append(whammyName(whammy, caretWhammyCents()));
    }
    if (const int technique = caretBassTechnique(); technique != 0) {
        parts.append(bassTechniqueName(technique));
    }
    if (const int hairpin = caretHairpin(); hairpin != 0) {
        parts.append(hairpin == static_cast<int>(core::Hairpin::Crescendo) ? tr("Crescendo")
                                                                           : tr("Diminuendo"));
    }
    switch (static_cast<core::Fermata>(caretFermata())) {
    case core::Fermata::Short:
        parts.append(tr("Short fermata"));
        break;
    case core::Fermata::Normal:
        parts.append(tr("Fermata"));
        break;
    case core::Fermata::Long:
        parts.append(tr("Long fermata"));
        break;
    case core::Fermata::None:
        break;
    }
    if (const auto ottava = core::enumFromIndex<core::Ottava>(caretOttava());
        ottava && *ottava != core::Ottava::None) {
        parts.append(tr("Octave line %1").arg(latin(core::nameOf(*ottava)))); // "8va"...
    }
    if (caretSlur()) {
        parts.append(tr("Slur"));
    }
    if (const int pedal = caretPedal(); pedal != 0) {
        parts.append(pedal == static_cast<int>(core::Pedal::Change) ? tr("Pedal change")
                                                                    : tr("Sustain pedal"));
    }
    if (const int breath = caretBreath(); breath != 0) {
        parts.append(breath == static_cast<int>(core::Breath::Caesura) ? tr("Caesura")
                                                                       : tr("Breath mark"));
    }
    if (caretGraceNotes() > 0) {
        parts.append(tr("Grace notes"));
    }
    return parts.join(QStringLiteral(" · "));
}

bool CaretEditor::caretFretted() const {
    return editedSong() != nullptr && onStrings(edited(), where());
}

std::optional<int> CaretEditor::fretAtCaret() const {
    if (!editing() || core::onFreeSlot(edited(), where())) {
        return std::nullopt;
    }
    const core::Beat& beat = edited()
                                 .track(where().track)
                                 .staff(where().staff)
                                 .measure(where().measure)
                                 .voices.at(where().voice)
                                 .beats.at(where().beat);
    for (const core::Note& note : beat.notes) {
        if (note.position && note.position->string == where().string) {
            return note.position->fret;
        }
    }
    return std::nullopt;
}

std::vector<core::Pitch> CaretEditor::pitchesAtCaret() const {
    std::vector<core::Pitch> pitches;
    if (!editing() || core::onFreeSlot(edited(), where())) {
        return pitches;
    }
    const bool fretted = caretFretted();
    const core::Beat& beat = edited()
                                 .track(where().track)
                                 .staff(where().staff)
                                 .measure(where().measure)
                                 .voices.at(where().voice)
                                 .beats.at(where().beat);
    for (const core::Note& note : beat.notes) {
        if (!fretted || (note.position && note.position->string == where().string)) {
            pitches.push_back(note.pitch);
        }
    }
    std::ranges::sort(pitches, {}, &core::Pitch::midiNumber);
    return pitches;
}

bool CaretEditor::caretPitched() const {
    if (editedSong() == nullptr || where().track >= edited().trackCount()) {
        return false;
    }
    const core::Track& track = edited().track(where().track);
    return track.instrument().kind != core::InstrumentKind::Percussion &&
           where().staff < track.staffCount() &&
           track.staff(where().staff).clef() != core::Clef::Percussion;
}

bool CaretEditor::caretOnDrums() const {
    return editedSong() != nullptr && onDrumStaff(edited(), where());
}

QString CaretEditor::laneName(int lane) {
    const auto pieces = core::drumPieces();
    if (lane < 1 || lane > static_cast<int>(pieces.size())) {
        return {};
    }
    const std::string_view name = pieces[static_cast<std::size_t>(lane - 1)].name;
    return translated(kContext, kDrumPieceNames, name, name);
}

void CaretEditor::deleteAtCaret() {
    runEntry([this](const core::Song& song) { return core::deleteAtCaret(song, where()); });
}

void CaretEditor::deleteBeat() {
    runEntry([this](const core::Song& song) { return core::deleteBeat(song, where()); });
}

void CaretEditor::insertRest() {
    runEntry([this](const core::Song& song) {
        return core::insertRest(song, where(), inputDuration());
    });
}

void CaretEditor::typeNoteName(int step, bool addToChord) {
    if (!editing() || step < 0 || step > 6) {
        return;
    }
    if (!caretPitched()) {
        document_->refuse(tr("Drums take no note names."));
        return;
    }
    document_->endTyping();
    try {
        document_->enter(
            core::enterPitch(edited(), where(), static_cast<core::Step>(step), inputDuration(),
                             addToChord ? core::PitchMode::AddToChord : core::PitchMode::Replace));
    } catch (const std::out_of_range&) {
        document_->refuse(caretFretted() ? tr("No string can play that note.")
                                         : tr("That note is out of range."));
    }
}

void CaretEditor::shiftPitch(int semitones) {
    if (!editing()) {
        return;
    }
    document_->endTyping();
    try {
        document_->enter(core::shiftPitch(edited(), where(), semitones));
    } catch (const std::out_of_range&) {
        document_->refuse(caretFretted() ? tr("The note would leave the neck.")
                                         : tr("That note is out of range."));
    }
}

void CaretEditor::toggleDrum() {
    if (!editing()) {
        return;
    }
    if (!caretOnDrums()) {
        document_->refuse(tr("Hits are typed on a drum track."));
        return;
    }
    runEntry([this](const core::Song& song) {
        return core::toggleDrum(song, where(), inputDuration());
    });
}

void CaretEditor::toggleEffect(int effect) {
    if (!editing() || effect < 0 || effect >= kEffectCount) {
        return;
    }
    document_->endTyping();
    const auto which = static_cast<core::NoteEffect>(effect);
    try {
        core::Entry entry = core::toggleEffect(edited(), where(), which);
        if (!entry.command) {
            document_->refuse(tr("Effects go on notes: put the caret on one."));
            return;
        }
        document_->enter(std::move(entry));
    } catch (const std::out_of_range&) {
        document_->refuse(tr("That fret has no natural harmonic."));
    } catch (const std::logic_error&) {
        document_->refuse(which == core::NoteEffect::Tie
                              ? tr("There is no note to tie to.")
                              : tr("%1 needs strings.").arg(effectName(effect)));
    }
}

int CaretEditor::caretEffects() const {
    if (!editing()) {
        return 0;
    }
    int effects = 0;
    for (int effect = 0; effect < kEffectCount; ++effect) {
        if (core::hasEffectAtCaret(edited(), where(), static_cast<core::NoteEffect>(effect))) {
            effects |= 1 << effect;
        }
    }
    return effects;
}

QString CaretEditor::effectName(int effect) const {
    if (effect < 0 || effect >= kEffectCount) {
        return {};
    }
    return tr(kEffects.at(static_cast<std::size_t>(effect)).name);
}

QString CaretEditor::effectKey(int effect) const {
    if (effect < 0 || effect >= kEffectCount) {
        return {};
    }
    return QString(QChar(kEffects.at(static_cast<std::size_t>(effect)).key));
}

bool CaretEditor::effectNeedsStrings(int effect) const {
    return effect >= 0 && effect < kEffectCount &&
           core::needsStrings(static_cast<core::NoteEffect>(effect));
}

int CaretEditor::effectForKey(const QString& text) const {
    if (text.size() != 1) {
        return -1;
    }
    const char16_t key = text.front().toUpper().unicode();
    const auto found =
        std::ranges::find_if(kEffects, [key](const EffectKey& entry) { return entry.key == key; });
    return found == kEffects.end() ? -1 : static_cast<int>(found - kEffects.begin());
}

void CaretEditor::setBend(int form, int cents) {
    if (!editing() || form < 0 || form >= static_cast<int>(core::BendForm::Custom)) {
        return;
    }
    document_->endTyping();
    core::BendCurve curve;
    try {
        curve = core::bendCurve(static_cast<core::BendForm>(form), cents);
    } catch (const std::invalid_argument&) {
        return;
    }
    core::Entry entry = core::setBend(edited(), where(), curve);
    if (!entry.command) {
        // Silent when the notes have that bend already.
        if (entry.missed) {
            document_->refuse(tr("Effects go on notes: put the caret on one."));
        }
        return;
    }
    document_->enter(std::move(entry));
}

core::BeatEffects CaretEditor::beatEffectsAtCaret() const {
    if (!editing()) {
        return {};
    }
    return core::beatEffectsAt(edited(), where());
}

template <typename Value>
void CaretEditor::setBeatEffect(Value core::BeatEffects::* field,
                                std::type_identity_t<Value> value) {
    if (!editing()) {
        return;
    }
    document_->endTyping();
    core::Entry entry = core::setBeatEffect(edited(), where(), field, std::move(value));
    if (!entry.command) {
        // Silent when the beat has it already.
        if (entry.missed) {
            document_->refuse(tr("Effects go on notes: put the caret on one."));
        }
        return;
    }
    document_->enter(std::move(entry));
}

void CaretEditor::setStrum(int direction) {
    if (const std::optional<core::Strum> strum = core::enumFromIndex<core::Strum>(direction)) {
        setBeatEffect(&core::BeatEffects::strum, *strum);
    }
}

void CaretEditor::setHairpin(int hairpin) {
    if (const std::optional<core::Hairpin> wedge = core::enumFromIndex<core::Hairpin>(hairpin)) {
        setBeatEffect(&core::BeatEffects::hairpin, *wedge);
    }
}

void CaretEditor::setSlur(bool slurred) {
    setBeatEffect(&core::BeatEffects::slur, slurred);
}

void CaretEditor::setFermata(int fermata) {
    if (const std::optional<core::Fermata> pause = core::enumFromIndex<core::Fermata>(fermata)) {
        setBeatEffect(&core::BeatEffects::fermata, *pause);
    }
}

void CaretEditor::setOttava(int ottava) {
    if (const std::optional<core::Ottava> line = core::enumFromIndex<core::Ottava>(ottava)) {
        setBeatEffect(&core::BeatEffects::ottava, *line);
    }
}

void CaretEditor::setPedal(int pedal) {
    if (const std::optional<core::Pedal> mark = core::enumFromIndex<core::Pedal>(pedal)) {
        setBeatEffect(&core::BeatEffects::pedal, *mark);
    }
}

void CaretEditor::setBreath(int breath) {
    if (const std::optional<core::Breath> mark = core::enumFromIndex<core::Breath>(breath)) {
        setBeatEffect(&core::BeatEffects::breath, *mark);
    }
}

void CaretEditor::toggleGraceNote() {
    if (caretGraceNotes() > 0) {
        runEntry([this](const core::Song& song) { return core::removeGraceNotes(song, where()); });
        return;
    }
    runEntry([this](const core::Song& song) {
        const core::Duration eighth(core::NoteValue::Eighth);
        try {
            return core::addGraceNote(song, where(), -2, eighth, false);
        } catch (const std::out_of_range&) {
            return core::addGraceNote(song, where(), 2, eighth, false); // from above
        }
    });
}

void CaretEditor::setChordSymbol(const QString& name) {
    runEntry([this, text = name.toStdString()](const core::Song& song) {
        return core::setChordSymbol(song, where(), text);
    });
}

namespace {

constexpr qsizetype kOffered = 12; // words a popup offers

/// The different words `pick` finds on the song's beats (a pointer, null for
/// none) in the order they first appear, at most kOffered.
template <typename Pick>
QStringList wordsOf(const core::Song& song, Pick pick) {
    QStringList found;
    for (std::size_t m = 0; m < song.measureCount() && found.size() < kOffered; ++m) {
        for (std::size_t t = 0; t < song.trackCount(); ++t) {
            const core::Track& track = song.track(t);
            for (std::size_t s = 0; s < track.staffCount(); ++s) {
                for (const core::Voice& voice : track.staff(s).measure(m).voices) {
                    for (const core::Beat& beat : voice.beats) {
                        const std::string* words = pick(beat);
                        if (words == nullptr || words->empty()) {
                            continue;
                        }
                        const QString text = qstring(*words);
                        if (!found.contains(text) && found.size() < kOffered) {
                            found.append(text);
                        }
                    }
                }
            }
        }
    }
    return found;
}

} // namespace

QStringList CaretEditor::songChords() const {
    if (editedSong() == nullptr) {
        return {};
    }
    return wordsOf(edited(), [](const core::Beat& beat) -> const std::string* {
        return beat.chord ? &beat.chord->name : nullptr;
    });
}

void CaretEditor::setBeatText(const QString& text) {
    runEntry([this, words = text.toStdString()](const core::Song& song) {
        return core::setBeatText(song, where(), words);
    });
}

QStringList CaretEditor::songTexts() const {
    if (editedSong() == nullptr) {
        return {};
    }
    return wordsOf(edited(), [](const core::Beat& beat) -> const std::string* {
        return beat.text ? &*beat.text : nullptr;
    });
}

int CaretEditor::caretVerseCount() const {
    if (!editing()) {
        return 0;
    }
    const core::Song& song = edited();
    const core::Caret caret = core::clampCaret(song, where());
    return static_cast<int>(core::verseCount(song.track(caret.track).staff(caret.staff)));
}

QVariantMap CaretEditor::caretVerse(int verse) const {
    core::VerseText typed;
    if (editing() && verse >= 0) {
        typed = core::verseAt(edited(), where(), static_cast<std::size_t>(verse));
    }
    return {{QStringLiteral("first"), static_cast<int>(typed.first)},
            {QStringLiteral("text"), qstring(typed.text)}};
}

int CaretEditor::setVerse(int verse, int first, const QString& text) {
    if (verse < 0 || verse >= ScoreDocument::maxVerses() || first < 0) {
        return 0;
    }
    std::size_t leftover = 0;
    runEntry([&](const core::Song& song) {
        core::VerseEntry typed =
            core::setVerse(song, where(), static_cast<std::size_t>(verse),
                           static_cast<std::size_t>(first), text.toStdString());
        leftover = typed.leftover;
        return std::move(typed.entry);
    });
    return static_cast<int>(leftover);
}

void CaretEditor::setPickStroke(int direction) {
    if (const std::optional<core::PickStroke> stroke =
            core::enumFromIndex<core::PickStroke>(direction)) {
        setBeatEffect(&core::BeatEffects::pickStroke, *stroke);
    }
}

void CaretEditor::setTremolo(int value) {
    std::optional<core::NoteValue> speed;
    for (const core::NoteValue candidate :
         {core::NoteValue::Eighth, core::NoteValue::Sixteenth, core::NoteValue::ThirtySecond}) {
        if (denominatorOf(candidate) == value) {
            speed = candidate;
        }
    }
    if (value != 0 && !speed) {
        return;
    }
    setBeatEffect(&core::BeatEffects::tremoloPicking, speed);
}

void CaretEditor::setWhammy(int form, int cents) {
    if (form < 0 || form >= static_cast<int>(core::BendForm::Custom)) {
        return;
    }
    core::BendCurve curve;
    try {
        curve = core::whammyCurve(static_cast<core::BendForm>(form), cents);
    } catch (const std::invalid_argument&) {
        return;
    }
    setBeatEffect(&core::BeatEffects::whammyBar, std::move(curve));
}

void CaretEditor::setBassTechnique(int technique) {
    if (const std::optional<core::BassTechnique> bass =
            core::enumFromIndex<core::BassTechnique>(technique)) {
        setBeatEffect(&core::BeatEffects::bassTechnique, *bass);
    }
}

QString CaretEditor::whammyAmount(int cents) const {
    constexpr int kStep = 200;
    const int depth = std::abs(cents);
    const QString steps = depth == kStep ? QStringLiteral("1") : bendAmount(depth);
    return (cents < 0 ? QStringLiteral("-") : QStringLiteral("+")) + steps;
}

QString CaretEditor::whammyName(int form, int cents) const {
    const QString amount = whammyAmount(cents);
    const bool dive = cents < 0;
    switch (static_cast<core::BendForm>(form)) {
    case core::BendForm::None:
        return {};
    case core::BendForm::Bend:
        return (dive ? tr("Dive %1") : tr("Pull-up %1")).arg(amount);
    case core::BendForm::BendRelease:
        return (dive ? tr("Dip %1") : tr("Inverted dip %1")).arg(amount);
    case core::BendForm::PreBend:
        return (dive ? tr("Pre-dive %1") : tr("Pre-pull-up %1")).arg(amount);
    case core::BendForm::PreBendRelease:
        return (dive ? tr("Pre-dive and return %1") : tr("Pre-pull-up and return %1")).arg(amount);
    case core::BendForm::Custom:
        break;
    }
    return tr("Whammy bar (custom curve)");
}

QString CaretEditor::bassTechniqueName(int technique) const {
    switch (static_cast<core::BassTechnique>(technique)) {
    case core::BassTechnique::None:
        return {};
    case core::BassTechnique::Tap:
        return tr("Tapping");
    case core::BassTechnique::Slap:
        return tr("Slap");
    case core::BassTechnique::Pop:
        return tr("Pop");
    }
    return {};
}

core::NoteEffects CaretEditor::noteEffectsAtCaret() const {
    if (!editing()) {
        return {};
    }
    return core::noteEffectsAt(edited(), where());
}

void CaretEditor::changeNoteEffects(
    const std::function<core::Entry(const core::Song&, const core::Caret&)>& make) {
    if (!editing()) {
        return;
    }
    document_->endTyping();
    core::Entry entry = make(edited(), where());
    if (!entry.command) {
        // Silent when the notes have it already.
        if (entry.missed) {
            document_->refuse(tr("Effects go on notes: put the caret on one."));
        }
        return;
    }
    document_->enter(std::move(entry));
}

void CaretEditor::setSlideIn(int kind) {
    if (const std::optional<core::SlideIn> in = core::enumFromIndex<core::SlideIn>(kind)) {
        changeNoteEffects([in = *in](const core::Song& song, const core::Caret& caret) {
            return core::setSlideIn(song, caret, in);
        });
    }
}

void CaretEditor::setSlideOut(int kind) {
    if (const std::optional<core::SlideOut> out = core::enumFromIndex<core::SlideOut>(kind)) {
        changeNoteEffects([out = *out](const core::Song& song, const core::Caret& caret) {
            return core::setSlideOut(song, caret, out);
        });
    }
}

void CaretEditor::setVibrato(int kind) {
    if (const std::optional<core::Vibrato> vibrato = core::enumFromIndex<core::Vibrato>(kind)) {
        changeNoteEffects([vibrato = *vibrato](const core::Song& song, const core::Caret& caret) {
            return core::setVibrato(song, caret, vibrato);
        });
    }
}

void CaretEditor::setAccent(int kind) {
    if (const std::optional<core::Accent> accent = core::enumFromIndex<core::Accent>(kind)) {
        changeNoteEffects([accent = *accent](const core::Song& song, const core::Caret& caret) {
            return core::setAccent(song, caret, accent);
        });
    }
}

void CaretEditor::setHarmonic(int kind) {
    const std::optional<core::HarmonicKind> wanted = core::enumFromIndex<core::HarmonicKind>(kind);
    if (!wanted) {
        return;
    }
    // What setHarmonic() throws, as it documents it.
    try {
        changeNoteEffects([harmonic = *wanted](const core::Song& song, const core::Caret& caret) {
            return core::setHarmonic(song, caret, harmonic);
        });
    } catch (const std::out_of_range&) {
        document_->refuse(tr("That fret has no natural harmonic."));
    } catch (const std::logic_error&) {
        document_->refuse(tr("Harmonics need strings."));
    }
}

int CaretEditor::caretSlideIn() const {
    return static_cast<int>(noteEffectsAtCaret().slideIn);
}

int CaretEditor::caretSlideOut() const {
    return static_cast<int>(noteEffectsAtCaret().slideOut);
}

int CaretEditor::caretVibrato() const {
    return static_cast<int>(noteEffectsAtCaret().vibrato);
}

int CaretEditor::caretAccent() const {
    return static_cast<int>(noteEffectsAtCaret().accent);
}

int CaretEditor::caretTrill() const {
    return noteEffectsAtCaret().trill.semitones;
}

int CaretEditor::caretOrnament() const {
    return static_cast<int>(noteEffectsAtCaret().ornament);
}

void CaretEditor::setOrnament(int kind) {
    if (const std::optional<core::Ornament> ornament = core::enumFromIndex<core::Ornament>(kind)) {
        changeNoteEffects([ornament = *ornament](const core::Song& song, const core::Caret& caret) {
            return core::setOrnament(song, caret, ornament);
        });
    }
}

int CaretEditor::caretFingering() const {
    return static_cast<int>(noteEffectsAtCaret().fingering);
}

int CaretEditor::caretPluck() const {
    return static_cast<int>(noteEffectsAtCaret().pluck);
}

void CaretEditor::setFingering(int finger) {
    if (const std::optional<core::Finger> chosen = core::enumFromIndex<core::Finger>(finger)) {
        changeNoteEffects([chosen = *chosen](const core::Song& song, const core::Caret& caret) {
            return core::setFingering(song, caret, std::span(&chosen, 1));
        });
    }
}

void CaretEditor::setPluck(int finger) {
    const std::optional<core::Finger> chosen = core::enumFromIndex<core::Finger>(finger);
    if (!chosen || !caretFretted()) { // setPluck() needs strings
        return;
    }
    changeNoteEffects([chosen = *chosen](const core::Song& song, const core::Caret& caret) {
        return core::setPluck(song, caret, chosen);
    });
}

void CaretEditor::toggleTrill() {
    const bool on = caretTrill() == 0;
    changeNoteEffects([on](const core::Song& song, const core::Caret& caret) {
        return on ? core::setTrillToNextStep(song, caret, core::NoteValue::ThirtySecond)
                  : core::setTrill(song, caret, core::Trill{});
    });
}

int CaretEditor::caretDynamic() const {
    if (!editing()) {
        return static_cast<int>(core::Dynamic::MF);
    }
    return static_cast<int>(core::dynamicAt(edited(), where()));
}

void CaretEditor::setDynamic(int dynamic) {
    const std::optional<core::Dynamic> wanted = core::enumFromIndex<core::Dynamic>(dynamic);
    if (!editing() || !wanted) {
        return;
    }
    document_->endTyping();
    core::Entry entry = core::setDynamic(edited(), where(), *wanted);
    if (!entry.command) {
        if (core::dynamicAt(edited(), where()) != *wanted) {
            document_->refuse(tr("Dynamics go on notes: put the caret on one."));
        }
        return;
    }
    document_->enter(std::move(entry));
}

QString CaretEditor::dynamicName(int dynamic) const {
    const std::optional<core::Dynamic> value = core::enumFromIndex<core::Dynamic>(dynamic);
    if (!value) {
        return {};
    }
    const std::string_view name = core::nameOf(*value); // as it is written: "ppp" to "fff"
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

int CaretEditor::caretHarmonic() const {
    return static_cast<int>(noteEffectsAtCaret().harmonic.kind);
}

QString CaretEditor::slideInName(int kind) const {
    switch (static_cast<core::SlideIn>(kind)) {
    case core::SlideIn::FromBelow:
        return tr("Slide in from below");
    case core::SlideIn::FromAbove:
        return tr("Slide in from above");
    case core::SlideIn::None:
        break;
    }
    return {};
}

QString CaretEditor::slideOutName(int kind) const {
    switch (static_cast<core::SlideOut>(kind)) {
    case core::SlideOut::Shift:
        return tr("Shift slide");
    case core::SlideOut::Legato:
        return tr("Legato slide");
    case core::SlideOut::Downwards:
        return tr("Slide out downwards");
    case core::SlideOut::Upwards:
        return tr("Slide out upwards");
    case core::SlideOut::None:
        break;
    }
    return {};
}

QString CaretEditor::harmonicName(int kind) const {
    switch (static_cast<core::HarmonicKind>(kind)) {
    case core::HarmonicKind::Natural:
        return tr("Natural harmonic");
    case core::HarmonicKind::Artificial:
        return tr("Artificial harmonic");
    case core::HarmonicKind::Pinch:
        return tr("Pinch harmonic");
    case core::HarmonicKind::Tapped:
        return tr("Tapped harmonic");
    case core::HarmonicKind::Semi:
        return tr("Semi-harmonic");
    case core::HarmonicKind::None:
        break;
    }
    return {};
}

int CaretEditor::caretStrum() const {
    return static_cast<int>(beatEffectsAtCaret().strum);
}

int CaretEditor::caretHairpin() const {
    return static_cast<int>(beatEffectsAtCaret().hairpin);
}

int CaretEditor::caretFermata() const {
    return static_cast<int>(beatEffectsAtCaret().fermata);
}

int CaretEditor::caretOttava() const {
    return static_cast<int>(beatEffectsAtCaret().ottava);
}

int CaretEditor::caretPedal() const {
    return static_cast<int>(beatEffectsAtCaret().pedal);
}

int CaretEditor::caretBreath() const {
    return static_cast<int>(beatEffectsAtCaret().breath);
}

bool CaretEditor::caretSlur() const {
    return beatEffectsAtCaret().slur;
}

namespace {

// The clefs the measure popup offers, in its order (ADR-0087).
constexpr std::array kOfferedClefs{core::Clef::Treble, core::Clef::Bass, core::Clef::Alto,
                                   core::Clef::Tenor};

} // namespace

int CaretEditor::caretClef() const {
    if (!editing() || caretOnDrums()) {
        return -1;
    }
    const core::Caret caret = core::clampCaret(edited(), where());
    switch (edited().track(caret.track).staff(caret.staff).clefAt(caret.measure)) {
    case core::Clef::Treble:
    case core::Clef::Treble8vb:
        return 0;
    case core::Clef::Bass:
    case core::Clef::Bass8vb:
        return 1;
    case core::Clef::Alto:
        return 2;
    case core::Clef::Tenor:
        return 3;
    case core::Clef::Percussion:
        break;
    }
    return -1;
}

void CaretEditor::setClef(int clef) {
    if (clef < 0 || std::cmp_greater_equal(clef, kOfferedClefs.size())) {
        return;
    }
    runEntry([this, chosen = kOfferedClefs.at(static_cast<std::size_t>(clef))](
                 const core::Song& song) { return core::setClef(song, where(), chosen); });
}

int CaretEditor::caretRepeat() const {
    if (!editing()) {
        return 0;
    }
    return static_cast<int>(core::measureRepeatAt(edited(), where()));
}

void CaretEditor::setMeasureRepeat(int measures) {
    if (measures < 0 || measures > 2) {
        return;
    }
    runEntry([this, measures](const core::Song& song) {
        return core::setMeasureRepeat(song, where(), static_cast<std::size_t>(measures));
    });
}

QString CaretEditor::caretText() const {
    if (!editing()) {
        return {};
    }
    return qstring(core::beatTextAt(edited(), where()));
}

QString CaretEditor::caretChord() const {
    if (!editing()) {
        return {};
    }
    return qstring(core::chordNameAt(edited(), where()));
}

int CaretEditor::caretGraceNotes() const {
    if (!editing()) {
        return 0;
    }
    return static_cast<int>(core::graceNotesAt(edited(), where()));
}

int CaretEditor::caretPickStroke() const {
    return static_cast<int>(beatEffectsAtCaret().pickStroke);
}

int CaretEditor::caretTremolo() const {
    const auto speed = beatEffectsAtCaret().tremoloPicking;
    return speed ? denominatorOf(*speed) : 0;
}

int CaretEditor::caretWhammyForm() const {
    return static_cast<int>(core::whammyForm(beatEffectsAtCaret().whammyBar));
}

int CaretEditor::caretWhammyCents() const {
    return core::whammyCents(beatEffectsAtCaret().whammyBar);
}

int CaretEditor::caretBassTechnique() const {
    return static_cast<int>(beatEffectsAtCaret().bassTechnique);
}

int CaretEditor::caretBendForm() const {
    if (!editing()) {
        return 0;
    }
    return static_cast<int>(core::bendForm(core::bendAtCaret(edited(), where())));
}

int CaretEditor::caretBendCents() const {
    if (!editing()) {
        return 0;
    }
    return core::bendAtCaret(edited(), where()).maxCents();
}

QString CaretEditor::bendAmount(int cents) const {
    constexpr int kStep = 200;   // cents in a full step
    constexpr int kQuarter = 50; // cents in a quarter step
    if (cents == kStep) {
        return tr("full");
    }
    if (cents <= 0 || cents % kQuarter != 0) {
        return tr("%1 cents").arg(cents);
    }
    static constexpr std::array<const char*, 4> kQuarters{"", "1/4", "1/2", "3/4"};
    const int steps = cents / kStep;
    const QString part =
        QLatin1StringView(kQuarters.at(static_cast<std::size_t>((cents % kStep) / kQuarter)));
    if (steps == 0) {
        return part;
    }
    return part.isEmpty() ? QString::number(steps) : QStringLiteral("%1 %2").arg(steps).arg(part);
}

QString CaretEditor::bendName(int form, int cents) const {
    switch (static_cast<core::BendForm>(form)) {
    case core::BendForm::None:
        return {};
    case core::BendForm::Bend:
        return tr("Bend %1").arg(bendAmount(cents));
    case core::BendForm::BendRelease:
        return tr("Bend and release %1").arg(bendAmount(cents));
    case core::BendForm::PreBend:
        return tr("Pre-bend %1").arg(bendAmount(cents));
    case core::BendForm::PreBendRelease:
        return tr("Pre-bend and release %1").arg(bendAmount(cents));
    case core::BendForm::Custom:
        break;
    }
    return tr("Bend (custom curve)");
}

void CaretEditor::runEntry(const std::function<core::Entry(const core::Song&)>& make) {
    if (!editing()) {
        return;
    }
    document_->endTyping();
    try {
        document_->enter(make(edited()));
    } catch (const std::exception& error) {
        document_->refuse(qstring(error.what()));
    }
}

} // namespace elara::app
