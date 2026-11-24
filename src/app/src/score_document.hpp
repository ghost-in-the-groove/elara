// SPDX-License-Identifier: AGPL-3.0-or-later
// The score shown in the window: opened and laid out on worker threads
// (ADR-0015).
#pragma once

#include <elara/audio/sequence.hpp>
#include <elara/core/clipboard.hpp>
#include <elara/core/document.hpp>
#include <elara/core/duration.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>

#include "score_painter.hpp"
#include "track_list.hpp"
#include <QElapsedTimer>
#include <QObject>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace elara::app {

struct ScoreLayout;

/// A song opened from a file, or new, and laid out on A4 pages. Opening and
/// laying out run on worker threads; the window keeps showing the previous
/// pages until the new ones are ready. Pages are immutable once laid out, so
/// page views paint them on worker threads too.
///
/// The song is edited through core::Document's commands, so every edit can be
/// undone; each edit lays the score out again. Saving writes the native format.
class ScoreDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString title READ title NOTIFY songChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY songChanged)
    Q_PROPERTY(QStringList trackNames READ trackNames NOTIFY songChanged)
    Q_PROPERTY(int measureCount READ measureCount NOTIFY songChanged)
    Q_PROPERTY(double duration READ duration NOTIFY sequenceChanged)
    Q_PROPERTY(int track READ track WRITE setTrack NOTIFY trackChanged)
    Q_PROPERTY(View view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(bool hasTablature READ hasTablature NOTIFY layoutChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY layoutChanged)
    Q_PROPERTY(QSizeF pageSize READ pageSize NOTIFY layoutChanged)
    Q_PROPERTY(double staffSpace READ staffSpace CONSTANT)
    Q_PROPERTY(int layoutMilliseconds READ layoutMilliseconds NOTIFY layoutChanged)
    Q_PROPERTY(QStringList nameFilters READ nameFilters CONSTANT)
    // Editing.
    Q_PROPERTY(QString songTitle READ songTitle NOTIFY songChanged)
    Q_PROPERTY(int tempo READ tempo NOTIFY songChanged)
    Q_PROPERTY(int timeNumerator READ timeNumerator NOTIFY songChanged)
    Q_PROPERTY(int timeDenominator READ timeDenominator NOTIFY songChanged)
    Q_PROPERTY(int keyFifths READ keyFifths NOTIFY songChanged)
    Q_PROPERTY(bool keyMinor READ keyMinor NOTIFY songChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY songChanged)
    Q_PROPERTY(QAbstractItemModel* trackList READ trackList CONSTANT)
    Q_PROPERTY(QVariantList instrumentPresets READ instrumentPresets CONSTANT)
    Q_PROPERTY(QVariantList tuningPresets READ tuningPresets CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoName READ undoName NOTIFY historyChanged)
    Q_PROPERTY(QString redoName READ redoName NOTIFY historyChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY historyChanged)
    Q_PROPERTY(bool savesInPlace READ savesInPlace NOTIFY sourceChanged)
    // Keyboard entry (ADR-0021).
    Q_PROPERTY(int caretPage READ caretPage NOTIFY caretChanged)
    Q_PROPERTY(QRectF caretRect READ caretRect NOTIFY caretChanged)
    Q_PROPERTY(QString caretStatus READ caretStatus NOTIFY caretChanged)
    Q_PROPERTY(bool caretFretted READ caretFretted NOTIFY caretChanged)
    Q_PROPERTY(int caretEffects READ caretEffects NOTIFY caretChanged)
    Q_PROPERTY(int effectCount READ effectCount CONSTANT)
    Q_PROPERTY(int inputValue READ inputValue NOTIFY inputChanged)
    Q_PROPERTY(bool inputDotted READ inputDotted NOTIFY inputChanged)
    Q_PROPERTY(int inputTuplet READ inputTuplet NOTIFY inputChanged)
    // The caret's measure.
    Q_PROPERTY(int caretMeasure READ caretMeasure NOTIFY caretChanged)
    Q_PROPERTY(int caretVoice READ caretVoice NOTIFY caretChanged)
    Q_PROPERTY(int caretTimeNumerator READ caretTimeNumerator NOTIFY caretChanged)
    Q_PROPERTY(int caretTimeDenominator READ caretTimeDenominator NOTIFY caretChanged)
    Q_PROPERTY(int caretKeyFifths READ caretKeyFifths NOTIFY caretChanged)
    Q_PROPERTY(bool caretKeyMinor READ caretKeyMinor NOTIFY caretChanged)
    Q_PROPERTY(QVariantMap caretNavigation READ caretNavigation NOTIFY caretChanged)
    Q_PROPERTY(QVariantMap caretRepeats READ caretRepeats NOTIFY caretChanged)
    Q_PROPERTY(QVariantMap caretTempo READ caretTempo NOTIFY caretChanged)
    // Selected measures (ADR-0036).
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(int selectionFirst READ selectionFirst NOTIFY selectionChanged)
    Q_PROPERTY(int selectionLast READ selectionLast NOTIFY selectionChanged)
    Q_PROPERTY(int selectionRevision READ selectionRevision NOTIFY selectionChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    // Practice (ADR-0024): how playback plays the song, whatever song it is.
    Q_PROPERTY(int speed READ speed WRITE setSpeed NOTIFY practiceChanged)
    Q_PROPERTY(bool metronome READ metronome WRITE setMetronome NOTIFY practiceChanged)

public:
    enum class Status : quint8 { Empty, Loading, Ready, Error };
    Q_ENUM(Status)
    /// How the shown tracks are drawn (notation::TrackView).
    enum class View : quint8 { Standard, Tablature, Both };
    Q_ENUM(View)
    /// The caret's moves (core::CaretMove): the arrow keys.
    enum class CaretMove : quint8 {
        NextBeat,
        PreviousBeat,
        NextMeasure,
        PreviousMeasure,
        StringUp,
        StringDown
    };
    Q_ENUM(CaretMove)

    /// How long after a digit a second one makes a two-digit fret.
    static constexpr int kTwoDigitMilliseconds = 700;

    /// `track` for every track of the song at once.
    static constexpr int kAllTracks = -1;

    explicit ScoreDocument(QObject* parent = nullptr);

    [[nodiscard]] QUrl source() const { return source_; }
    [[nodiscard]] Status status() const { return status_; }
    [[nodiscard]] QString errorMessage() const { return errorMessage_; }
    [[nodiscard]] bool busy() const { return pending_ > 0; }
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString artist() const;
    [[nodiscard]] QStringList trackNames() const;
    [[nodiscard]] int measureCount() const;
    /// Seconds the song lasts when played, repeats included.
    [[nodiscard]] double duration() const;
    [[nodiscard]] int track() const { return track_; }
    void setTrack(int track);
    [[nodiscard]] View view() const { return view_; }
    void setView(View view);
    [[nodiscard]] bool hasTablature() const { return hasTablature_; }
    [[nodiscard]] int pageCount() const;
    /// The page size in staff spaces.
    [[nodiscard]] QSizeF pageSize() const;
    /// Millimetres per staff space on paper.
    [[nodiscard]] static double staffSpace();
    [[nodiscard]] int layoutMilliseconds() const { return layoutMilliseconds_; }
    /// File dialog filters for the files open() reads.
    [[nodiscard]] static QStringList nameFilters();

    /// Opens a file (a file: URL) and lays out its first track.
    Q_INVOKABLE void open(const QUrl& file);
    /// Lays out the score again (after a track or view change).
    Q_INVOKABLE void relayout();

    /// Where the playback cursor stands `seconds` into the song: "page", and
    /// "x", "y" and "height" in staff spaces on it; empty when no page shows
    /// the measure played then.
    Q_INVOKABLE QVariantMap cursorAt(double seconds) const;
    /// When playback would start for a click at (`x`, `y`) staff spaces on
    /// page `page`: on the column of notes there, the first time its measure
    /// plays; negative away from every measure.
    Q_INVOKABLE double secondsAt(int page, double x, double y) const;

    /// The song and the sequence it plays as; null before a song is open.
    /// After an edit the sequence follows when the edit's layout is done: it
    /// is built on the same worker thread, so typing never waits for it.
    [[nodiscard]] std::shared_ptr<const core::Song> song() const { return song_; }
    [[nodiscard]] std::shared_ptr<const audio::Sequence> sequence() const { return sequence_; }
    /// The song sequence() was built from: what playback loads with it.
    [[nodiscard]] std::shared_ptr<const core::Song> sequenceSong() const { return sequenceSong_; }

    /// Playback speed, in percent of the written tempo (25 to 200), and the
    /// metronome. Kept when another song is opened; changing either builds the
    /// sequence again, and playback keeps its place in the music.
    [[nodiscard]] int speed() const noexcept;
    void setSpeed(int percent);
    [[nodiscard]] bool metronome() const noexcept { return practice_.metronome; }
    void setMetronome(bool on);
    /// What every sequence is built with.
    [[nodiscard]] audio::SequenceOptions practice() const noexcept { return practice_; }

    // ---- Editing ------------------------------------------------------------

    /// The title as written in the song; empty when it has none.
    [[nodiscard]] QString songTitle() const;
    /// The tempo at the start, in quarter notes per minute.
    [[nodiscard]] int tempo() const;
    /// The first measure's time and key signatures.
    [[nodiscard]] int timeNumerator() const;
    [[nodiscard]] int timeDenominator() const;
    [[nodiscard]] int keyFifths() const;
    [[nodiscard]] bool keyMinor() const;
    /// One map per track: name, instrument, detail (strings and tuning, or the
    /// kind of instrument), symbol (a clef of the music font) and its mix:
    /// volume (0 to 1), pan (-1 to 1), mute, solo, and audible when it sounds
    /// (ADR-0039).
    /// The tracks: name, detail, symbol, mix ("volume", "pan", "mute", "solo",
    /// "audible") and tuning ("fretted", "tuning", "tuningPreset", "capo", as
    /// trackTuning() names them "fretted", "notes", "preset" and "capo").
    [[nodiscard]] QVariantList tracks() const;
    /// tracks() as a model the song panel keeps its rows in: what changes
    /// changes only its rows. Follows songChanged.
    [[nodiscard]] QAbstractItemModel* trackList() { return &trackList_; }
    /// The instruments a track can be added for: id, name and group (guitar,
    /// bass or other), in a menu's order.
    [[nodiscard]] static QVariantList instrumentPresets();
    /// The tuning presets (ADR-0046): id, name and notes, the open strings
    /// from the lowest ("E2 A2 D3 G3 B3 E4").
    [[nodiscard]] static QVariantList tuningPresets();
    [[nodiscard]] bool canUndo() const { return document_ && document_->canUndo(); }
    [[nodiscard]] bool canRedo() const { return document_ && document_->canRedo(); }
    /// What undo() or redo() would change, e.g. "Add track"; empty when nothing.
    [[nodiscard]] QString undoName() const;
    [[nodiscard]] QString redoName() const;
    /// Whether the song changed since it was opened or saved.
    [[nodiscard]] bool modified() const { return document_ && document_->isModified(); }
    /// Whether save() can write back to the file the song came from (.elara).
    [[nodiscard]] bool savesInPlace() const;

    /// Starts a new song: one track of the instrument preset `presetId`, eight
    /// empty measures of 4/4 in C major at 120 quarter notes per minute.
    Q_INVOKABLE void newSong(const QString& presetId);
    Q_INVOKABLE void setSongTitle(const QString& title);
    Q_INVOKABLE void setArtist(const QString& artist);
    Q_INVOKABLE void setTempo(int bpm);
    /// Change the signature from the first measure until its next change.
    Q_INVOKABLE void setTimeSignature(int numerator, int denominator);
    Q_INVOKABLE void setKeySignature(int fifths, bool minor);
    /// Change the signature from `measure` (0-based) until its next change.
    Q_INVOKABLE void setTimeSignatureFrom(int measure, int numerator, int denominator);
    Q_INVOKABLE void setKeySignatureFrom(int measure, int fifths, bool minor);
    /// The signs and jumps of `measure` (ADR-0049): the flags "segno",
    /// "segnoSegno", "coda", "doubleCoda", "fine", "toCoda" and "toDoubleCoda",
    /// "jump" (0 none, 1 da capo, 2 dal segno, 3 dal segno segno) and
    /// "jumpThen" (0 to the end, 1 al Fine, 2 al Coda, 3 al Double Coda).
    Q_INVOKABLE QVariantMap navigationAt(int measure) const;
    /// Changes the marks of `measure` named in `marks`, keyed as navigationAt()
    /// gives them, as one undo step ("Change navigation marks").
    Q_INVOKABLE void setNavigation(int measure, const QVariantMap& marks);
    /// The repeats of `measure` (ADR-0016): "start" (it opens a repeated
    /// section), "times" (how often the section it closes plays, 2 to 255, or
    /// 0 when it closes none) and "endings" (the times through the repeat that
    /// play it, 1 to 8, in order).
    Q_INVOKABLE QVariantMap repeatsAt(int measure) const;
    /// Changes the repeats of `measure` named in `repeats`, keyed as
    /// repeatsAt() gives them, as one undo step ("Change repeats").
    Q_INVOKABLE void setRepeats(int measure, const QVariantMap& repeats);
    /// The tempo `measure` starts with (ADR-0042): "bpm", in beats per minute of
    /// the beat the song counts in there, "beat" (1 whole, 2 half, 4 quarter,
    /// 8 eighth...) and "dotted"; "own" when the measure starts a tempo
    /// change, and "first" for the first measure, whose tempo is the song's.
    Q_INVOKABLE QVariantMap tempoAt(int measure) const;
    /// Sets the tempo from `measure` until the next change as one undo step
    /// ("Change tempo"), or with a bpm of 0 takes the measure's change away. On
    /// the first measure it sets the song's tempo.
    Q_INVOKABLE void setTempoFrom(int measure, int bpm);
    /// Adds or removes measures at the end of the song.
    Q_INVOKABLE void setMeasureCount(int count);
    /// Adds a track of an instrument preset at the end and shows it.
    Q_INVOKABLE void addTrack(const QString& presetId);
    /// Removes a track; the last track of a song cannot be removed.
    Q_INVOKABLE void removeTrack(int index);
    /// Sets a track's mix as one undo step ("Change mix"); playback carries on
    /// with it (ADR-0039).
    Q_INVOKABLE void setTrackMix(int track, double volume, double pan, bool mute, bool solo);
    /// Lets playback hear a track's volume and pan while a fader moves, without
    /// changing the song: setTrackMix() keeps it when the fader is let go.
    Q_INVOKABLE void previewTrackMix(int track, double volume, double pan);
    /// The tuning of a track's first staff: "fretted" (false without strings),
    /// "notes" (its open strings from the lowest), "preset" (the id of the
    /// preset it matches, else empty) and "capo".
    Q_INVOKABLE [[nodiscard]] QVariantMap trackTuning(int track) const;
    /// Retunes a track's first staff (ADR-0046) to `tuning`, a preset id or open
    /// strings from the lowest ("D2 A2 D3 G3 B3 E4"), with a capo at `capo`,
    /// keeping its pitches or, with `keepFrets`, its frets: one undo step,
    /// "Change tuning". A tuning that cannot play every note is refused through
    /// editFailed().
    Q_INVOKABLE void setTrackTuning(int track, const QString& tuning, int capo, bool keepFrets);
    /// The mixes previewTrackMix() last made, one per track.
    [[nodiscard]] std::span<const core::TrackMix> previewMixes() const { return previewMixes_; }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// Saves the song as a .elara file (the extension is added when missing).
    /// Returns false, and reports why through editFailed(), when it cannot.
    Q_INVOKABLE bool save(const QUrl& file);

    // ---- Keyboard entry -----------------------------------------------------

    /// The page showing the caret, or -1 when no page shows it (its track is
    /// hidden, or there is no song).
    [[nodiscard]] int caretPage() const { return caretPage_; }
    /// The caret on its page, in staff spaces from the page's top-left corner.
    [[nodiscard]] QRectF caretRect() const { return caretRect_; }
    /// Where the caret is, for a status line: "Measure 3 · Beat 2 · String 4 ·
    /// Fret 5 · A3", the beat's notes on a staff without strings, or the lane's
    /// kit piece on a drum staff ("Measure 1 · Beat 1 · Snare").
    [[nodiscard]] QString caretStatus() const;
    /// Whether the caret's staff has strings, so that digits type frets.
    [[nodiscard]] bool caretFretted() const;
    /// The duration typing uses: 1 a whole note, 2 a half note... 64.
    [[nodiscard]] int inputValue() const;
    [[nodiscard]] bool inputDotted() const { return input_.dots() > 0; }
    /// The actual notes of the input duration's tuplet (3 for a triplet), 0
    /// when it has none.
    [[nodiscard]] int inputTuplet() const {
        return input_.tuplet().isNone() ? 0 : input_.tuplet().actual();
    }
    /// The caret in the song being edited.
    [[nodiscard]] core::Caret caret() const { return caret_; }
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

    Q_INVOKABLE void moveCaret(CaretMove move);
    /// The voice typing goes into, 1 to 4 (the core's voice plus one).
    [[nodiscard]] int caretVoice() const { return static_cast<int>(caret_.voice) + 1; }
    /// Moves the caret to voice `voice` (1 to 4) of its measure, at its beat or
    /// the nearest one that voice has; typing then goes there.
    Q_INVOKABLE void setCaretVoice(int voice);
    /// Puts the caret where a click at (x, y) on page `index` points, in staff
    /// spaces from the page's top-left corner. Returns whether it moved there.
    Q_INVOKABLE bool placeCaret(int index, double x, double y);
    /// Types a digit: a fret on the caret's string. A second digit within
    /// kTwoDigitMilliseconds on the same caret makes a two-digit fret, as one
    /// undo step.
    Q_INVOKABLE void typeDigit(int digit);
    /// The input duration (1, 2, 4... 64), also given to the caret's beat.
    Q_INVOKABLE void setInputValue(int value);
    Q_INVOKABLE void toggleDot();
    /// Makes the input duration a tuplet of `actual` notes (ADR-0050): 2, 3,
    /// 4, 5, 6, 7 or 9, in the time of 3, 2, 3, 4, 4, 4 and 8. It goes back to
    /// no tuplet when it already is that one or `actual` is 0. The caret's
    /// beat takes it, as setInputValue().
    Q_INVOKABLE void setInputTuplet(int actual);
    /// The next longer or shorter note value, as setInputValue().
    Q_INVOKABLE void longerDuration();
    Q_INVOKABLE void shorterDuration();
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
    static constexpr int kEffectCount = 12;
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

    /// The caret's measure, 0-based, and its signatures.
    [[nodiscard]] int caretMeasure() const { return static_cast<int>(caret_.measure); }
    [[nodiscard]] int caretTimeNumerator() const;
    [[nodiscard]] int caretTimeDenominator() const;
    [[nodiscard]] int caretKeyFifths() const;
    [[nodiscard]] bool caretKeyMinor() const;
    [[nodiscard]] QVariantMap caretNavigation() const {
        return navigationAt(static_cast<int>(caret_.measure));
    }
    [[nodiscard]] QVariantMap caretRepeats() const {
        return repeatsAt(static_cast<int>(caret_.measure));
    }
    [[nodiscard]] QVariantMap caretTempo() const {
        return tempoAt(static_cast<int>(caret_.measure));
    }
    /// Inserts an empty measure before the caret's measure, or after it, with
    /// the signatures of the measure before it; the caret goes to it.
    Q_INVOKABLE void insertMeasure(bool after);
    /// Removes the caret's measure; the last measure of a song stays.
    Q_INVOKABLE void removeMeasure();

    /// Selected measures (ADR-0036): from an anchor, where the selection
    /// started, to the caret's measure. 0-based; -1 without a selection.
    [[nodiscard]] bool hasSelection() const { return anchor_.has_value(); }
    [[nodiscard]] int selectionFirst() const;
    [[nodiscard]] int selectionLast() const;
    /// Changes whenever the selection or its place on the pages does.
    [[nodiscard]] int selectionRevision() const { return selectionRevision_; }
    /// Where the selected measures are on page `index`, in staff spaces from the
    /// page's top-left corner: one rectangle per measure, across its system.
    Q_INVOKABLE QVariantList selectionBoxes(int index) const;
    /// Moves the caret `measures` measures on (back when negative), selecting
    /// from where it was.
    Q_INVOKABLE void extendSelection(int measures);
    /// Selects every measure, with the caret on the first.
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    /// Copies the selected measures, or the caret's, of the shown track (of
    /// every track when all are shown).
    Q_INVOKABLE void copy();
    [[nodiscard]] bool canPaste() const { return clip_.has_value(); }
    /// Pastes the copied measures at the first selected measure, or the
    /// caret's, over the measures there or into new ones before it; the pasted
    /// measures are selected.
    Q_INVOKABLE void paste(bool insert);
    /// Transposes the selected measures, or the caret's, of the shown track (of
    /// every track when all are shown) by `semitones`.
    Q_INVOKABLE void transposeSelection(int semitones);

    /// Page `index`, shared with the painting threads; null when out of range.
    [[nodiscard]] std::shared_ptr<const notation::DisplayList> page(int index) const;
    /// The painter of every page.
    [[nodiscard]] static const ScorePainter& painter();

signals:
    void sourceChanged();
    void statusChanged();
    void busyChanged();
    void songChanged();
    /// Another song replaced the one being edited: a file was opened, or a new
    /// song made. Emitted before songChanged() and sequenceChanged(), with the
    /// new song and sequence in place; playback goes back to the start.
    void songReplaced();
    /// The sequence playback plays changed: a song was opened, or an edit
    /// sounds different and its layout is done.
    void sequenceChanged();
    void trackChanged();
    void viewChanged();
    void layoutChanged();
    void historyChanged();
    void caretChanged();
    void inputChanged();
    void selectionChanged();
    void clipboardChanged();
    /// Something done that the window shows for a moment ("Copied measure 3.").
    void notice(const QString& message);
    /// An edit or a save that failed, with the reason to show.
    void editFailed(const QString& message);
    /// previewTrackMix() moved a fader: previewMixes() holds the new mixes.
    void mixPreviewed();
    void practiceChanged();

private:
    bool execute(std::unique_ptr<core::Command> command);
    /// Executes an entry's command, if any, and moves the caret. Returns
    /// whether a command ran.
    bool enter(core::Entry entry);
    /// Makes an entry from the song being edited and enters it, reporting
    /// what it throws through editFailed().
    void runEntry(const std::function<core::Entry(const core::Song&)>& make);
    /// The input duration takes the caret beat's, when it has one typing can use.
    void followBeat();
    void setInput(core::Duration duration);
    /// Clamps the caret to the song being edited.
    void clampCaret();
    /// Finds the caret on the pages shown.
    void placeCaretBox();
    void edited();
    void apply(const ScoreLayout& layout);
    void fail(const QString& message);
    void setPending(int delta);
    void resequence();
    /// The selected measures, or the caret's: first and count.
    [[nodiscard]] std::pair<std::size_t, std::size_t> selectedRange() const;
    /// The selection changed, or its place on the pages.
    void selectionMoved();

    QUrl source_;
    Status status_ = Status::Empty;
    QString errorMessage_;
    std::shared_ptr<const core::Song> song_;          ///< what the pages show
    std::shared_ptr<const audio::Sequence> sequence_; ///< song_ as playback plays it
    std::shared_ptr<const core::Song> sequenceSong_;  ///< the song sequence_ was built from
    std::shared_ptr<const core::Song> sequencing_;    ///< the song a sequence is being built for
    audio::SequenceOptions practice_;                 ///< speed() and metronome()
    std::vector<core::TrackMix> previewMixes_;        // previewTrackMix()
    TrackList trackList_{this};                       ///< tracks(), kept up to date
    std::optional<core::Document> document_;          ///< the song being edited
    std::shared_ptr<const std::vector<notation::DisplayList>> pages_;
    std::shared_ptr<const core::Song> pagesSong_; ///< the song the pages show
    /// What the last layout laid out, so an edit lays out only what it changed
    /// (ADR-0052); shared with the layouts running on workers.
    std::shared_ptr<notation::PageLayoutCache> layoutCache_ =
        std::make_shared<notation::PageLayoutCache>();
    core::Caret caret_;
    core::Duration input_;
    int caretPage_ = -1;
    QRectF caretRect_;
    std::optional<std::size_t> anchor_; ///< where the selection started
    int selectionRevision_ = 0;
    std::optional<core::MeasureClip> clip_; ///< the measures copied
    int pendingDigit_ = -1;                 ///< a digit just typed, waiting for a second
    core::Caret digitCaret_;                ///< where it was typed
    std::uint64_t digitEdit_ = 0;           ///< edits_ after it
    bool digitChanged_ = false;             ///< whether it changed the song (and can be undone)
    QElapsedTimer digitClock_;
    std::uint64_t edits_ = 0; ///< edits, undos and redos so far
    int track_ = 0;
    View view_ = View::Both;
    bool hasTablature_ = false;
    int layoutMilliseconds_ = 0;
    int pending_ = 0;
    std::uint64_t generation_ = 0;         ///< of layouts: only the latest is shown
    std::uint64_t sequenceGeneration_ = 0; ///< of sequences: only the latest is played
};

} // namespace elara::app
