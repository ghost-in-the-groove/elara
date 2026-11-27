// SPDX-License-Identifier: AGPL-3.0-or-later
// The score shown in the window: opened and laid out on worker threads
// (ADR-0015).
#pragma once

#include <elara/audio/sequence.hpp>
#include <elara/core/clipboard.hpp>
#include <elara/core/document.hpp>
#include <elara/core/duration.hpp>
#include <elara/core/edit_session.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/song.hpp>
#include <elara/io/result.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>

#include "job_lane.hpp"
#include "recovery_store.hpp"
#include "score_painter.hpp"
#include "track_list.hpp"
#include <QObject>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace elara::app {

struct ScoreLayout;
struct SequenceResult;

/// A song opened from a file, or new, and laid out on A4 pages. Opening and
/// laying out run on worker threads; the window keeps showing the previous
/// pages until the new ones are ready. Pages are immutable once laid out, so
/// page views paint them on worker threads too.
///
/// The song is edited through core::Document's commands, so every edit can be
/// undone; each edit lays the score out again. Saving writes the native format.
///
/// It has the editing session (ADR-0127): the caret, the input duration, the
/// selection and the measures copied, and what moves them. What is at the
/// caret, the notes and their effects, is CaretEditor's to read and edit
/// (ADR-0128).
class ScoreDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString title READ title NOTIFY songChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY songChanged)
    // Every line of the credits, in the order of elara::core::Credit (ADR-0059).
    Q_PROPERTY(QStringList credits READ credits NOTIFY songChanged)
    Q_PROPERTY(QStringList trackNames READ trackNames NOTIFY songChanged)
    Q_PROPERTY(QStringList importWarnings READ importWarnings NOTIFY importWarningsChanged)
    Q_PROPERTY(int measureCount READ measureCount NOTIFY songChanged)
    /// The verses a staff sings at most (ADR-0084).
    Q_PROPERTY(int maxVerses READ maxVerses CONSTANT)
    Q_PROPERTY(double duration READ duration NOTIFY sequenceChanged)
    /// Where each named section first plays (ADR-0062), in song order: maps
    /// with "name" and "seconds". A section that never plays is left out.
    Q_PROPERTY(QVariantList sectionMarks READ sectionMarks NOTIFY sequenceChanged)
    /// When each measure played starts, in seconds, in the order played. It
    /// changes when the measures played or their times do, not with every
    /// edit of the notes.
    Q_PROPERTY(QVariantList measureStarts READ measureStarts NOTIFY measureStartsChanged)
    Q_PROPERTY(int track READ track WRITE setTrack NOTIFY trackChanged)
    Q_PROPERTY(View view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(bool hasTablature READ hasTablature NOTIFY layoutChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY layoutChanged)
    Q_PROPERTY(QSizeF pageSize READ pageSize NOTIFY layoutChanged)
    Q_PROPERTY(bool continuous READ continuous WRITE setContinuous NOTIFY continuousChanged)
    Q_PROPERTY(bool showsStrips READ showsStrips NOTIFY layoutChanged)
    Q_PROPERTY(QList<qreal> pageTops READ pageTops NOTIFY layoutChanged)
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
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    /// Where the song is copied while it has unsaved changes (ADR-0106); no
    /// copies are written while it is empty.
    Q_PROPERTY(QString recoveryFolder READ recoveryFolder WRITE setRecoveryFolder NOTIFY
                   recoveryFolderChanged)
    // Keyboard entry (ADR-0021).
    Q_PROPERTY(int caretPage READ caretPage NOTIFY caretChanged)
    Q_PROPERTY(QRectF caretRect READ caretRect NOTIFY caretChanged)
    Q_PROPERTY(QRectF caretColumn READ caretColumn NOTIFY caretChanged)
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
    Q_PROPERTY(int caretTripletFeel READ caretTripletFeel NOTIFY caretChanged)
    Q_PROPERTY(QString caretSection READ caretSection NOTIFY caretChanged)
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
    static constexpr int kTwoDigitMilliseconds =
        static_cast<int>(core::EditSession::kTwoDigitWindow.count());

    /// `track` for every track of the song at once.
    static constexpr int kAllTracks = -1;

    /// How long after the first unsaved change the recovery copy is written,
    /// and how often while the song goes on changing: what a crash can lose.
    static constexpr int kRecoveryMilliseconds = 5000;

    explicit ScoreDocument(QObject* parent = nullptr);
    /// Removes the recovery copy: the window closes once the changes are
    /// saved or discarded.
    ~ScoreDocument() override;
    ScoreDocument(const ScoreDocument&) = delete;
    ScoreDocument& operator=(const ScoreDocument&) = delete;
    ScoreDocument(ScoreDocument&&) = delete;
    ScoreDocument& operator=(ScoreDocument&&) = delete;

    [[nodiscard]] QUrl source() const { return source_; }
    [[nodiscard]] Status status() const { return status_; }
    [[nodiscard]] QString errorMessage() const { return errorMessage_; }
    /// Whether a file is being opened, or the score laid out or sequenced.
    [[nodiscard]] bool busy() const;
    /// What laying out and sequencing did so far (ADR-0110): the smoke test
    /// prints it.
    [[nodiscard]] LaneWork layoutWork() const;
    [[nodiscard]] LaneWork sequenceWork() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString artist() const;
    /// The song's credits, one line per elara::core::Credit; empty lines when
    /// no song is open.
    [[nodiscard]] QStringList credits() const;
    [[nodiscard]] QStringList trackNames() const;
    /// What opening the song's file approximated or dropped, one line each
    /// ("Drums: volume changes dropped (measure 2)"); empty for a new song
    /// and once another file is opened without any.
    [[nodiscard]] QStringList importWarnings() const { return importWarnings_; }
    [[nodiscard]] int measureCount() const;
    [[nodiscard]] static int maxVerses() { return static_cast<int>(core::kMaxVerses); }
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
    /// Whether the music is laid out on one endless page cut into strips
    /// between systems, shown one under the other, instead of on pages
    /// (ADR-0057).
    [[nodiscard]] bool continuous() const { return continuous_; }
    void setContinuous(bool continuous);
    /// Whether the pages shown are such strips: the layout asked for by
    /// setContinuous() once it is done.
    [[nodiscard]] bool showsStrips() const { return showsStrips_; }
    /// Where each page (or strip) starts, in staff spaces from the top of the
    /// first, stacked without gaps, and where the last one ends.
    [[nodiscard]] QList<qreal> pageTops() const { return pageTops_; }
    /// Millimetres per staff space on paper.
    [[nodiscard]] static double staffSpace();
    [[nodiscard]] int layoutMilliseconds() const { return layoutMilliseconds_; }
    /// File dialog filters for the files open() reads.
    [[nodiscard]] static QStringList nameFilters();

    /// Opens a file (a file: URL) and lays out its first track.
    Q_INVOKABLE void open(const QUrl& file);

    // ---- Recovery copies (ADR-0106) -------------------------------------------

    [[nodiscard]] QString recoveryFolder() const;
    void setRecoveryFolder(const QString& folder);
    /// The copies in recoveryFolder(); null while it is empty.
    [[nodiscard]] const RecoveryStore* recovery() const { return recovery_.get(); }
    /// Writes the recovery copy of the song on a worker now, when it has
    /// unsaved changes the copy does not hold yet; recoveryWritten() follows.
    /// The copy is otherwise written kRecoveryMilliseconds after a change.
    void writeRecovery();
    /// The copies an earlier run of the app left behind, the newest first:
    /// maps of `file`, `title`, `source` (the song's own file, an empty URL
    /// for a song never saved) and `written` (a date).
    Q_INVOKABLE QVariantList recoverable();
    /// Opens a copy recoverable() offered as the song it was, with unsaved
    /// changes and its own file, if any, to save to. The copy goes once this
    /// run's own copy of the song is written.
    Q_INVOKABLE void recover(const QString& file);
    /// Removes a copy recoverable() offered.
    Q_INVOKABLE void discardRecovery(const QString& file);
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
    /// The measure playing `seconds` into the song, from 0; negative before a
    /// song is sequenced and past its end.
    Q_INVOKABLE int measureAt(double seconds) const;
    /// When measure `measure`, from 0, starts the first time it plays, in
    /// seconds; negative before a song is sequenced and for a measure that
    /// never plays.
    Q_INVOKABLE double measureStart(int measure) const;
    [[nodiscard]] QVariantList sectionMarks() const;
    [[nodiscard]] QVariantList measureStarts() const { return measureStarts_; }

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
    /// Sets line `line` (an elara::core::Credit) of the credits to `text`, as
    /// one edit; empty text clears it.
    Q_INVOKABLE void setCredit(int line, const QString& text);
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
    /// gives them, as one undo step ("Change navigation marks",
    /// core::changeNavigation()); taking the jump away also resets how it ends.
    Q_INVOKABLE void setNavigation(int measure, const QVariantMap& marks);
    /// The repeats of `measure` (ADR-0016): "start" (it opens a repeated
    /// section), "times" (how often the section it closes plays, 2 to 255, or
    /// 0 when it closes none) and "endings" (the times through the repeat that
    /// play it, 1 to 8, in order), and "doubleBar" (a double barline closes
    /// it, ADR-0103).
    Q_INVOKABLE QVariantMap repeatsAt(int measure) const;
    /// Changes the repeats of `measure` named in `repeats`, keyed as
    /// repeatsAt() gives them: the repeats and endings as one undo step
    /// ("Change repeats", core::changeRepeats()), the double barline as
    /// another ("Change barline", core::changeBarline()). Times or endings out
    /// of range change nothing and say so through editFailed().
    Q_INVOKABLE void setRepeats(int measure, const QVariantMap& repeats);
    /// The tempo `measure` starts with (ADR-0042): "bpm", in beats per minute of
    /// the beat the song counts in there, "beat" (1 whole, 2 half, 4 quarter,
    /// 8 eighth...) and "dotted"; "own" when the measure starts a tempo
    /// change, "gradual" when that change moves to the next one's tempo
    /// (ADR-0082), and "first" for the first measure, whose tempo is the song's.
    Q_INVOKABLE QVariantMap tempoAt(int measure) const;
    /// Sets the tempo from `measure` until the next change as one undo step
    /// ("Change tempo"), or with a bpm of 0 takes the measure's change away. On
    /// the first measure it sets the song's tempo.
    Q_INVOKABLE void setTempoFrom(int measure, int bpm);
    /// Makes the tempo change that starts `measure` gradual or sudden again, as
    /// one undo step ("Change tempo"); not on the first measure (ADR-0082).
    Q_INVOKABLE void setTempoGradual(int measure, bool gradual);
    /// The triplet feel of `measure` (ADR-0104), numbered as the measure popup
    /// offers it: 0 none, 1 triplet eighths, 2 triplet sixteenths, 3 dotted
    /// eighths, 4 dotted sixteenths, 5 Scottish eighths, 6 Scottish sixteenths
    /// (core::TripletFeel); 0 for a missing measure.
    Q_INVOKABLE int tripletFeelAt(int measure) const;
    /// Changes the triplet feel from `measure` until the next change of it,
    /// numbered as tripletFeelAt(), as one undo step ("Change triplet feel").
    Q_INVOKABLE void setTripletFeelFrom(int measure, int feel);
    /// The name of the section that starts at `measure` (ADR-0062), empty when
    /// none does.
    Q_INVOKABLE QString sectionAt(int measure) const;
    /// Names the section that starts at `measure`, trimmed, as one undo step
    /// ("Change section"); an empty name takes it off.
    Q_INVOKABLE void setSection(int measure, const QString& name);
    /// Adds or removes measures at the end of the song.
    Q_INVOKABLE void setMeasureCount(int count);
    /// Adds a track of an instrument preset at the end and shows it.
    Q_INVOKABLE void addTrack(const QString& presetId);
    /// Removes a track; the last track of a song cannot be removed.
    Q_INVOKABLE void removeTrack(int index);
    /// Gives a track another name, with the spaces round it taken off, as one
    /// undo step ("Rename track"). An empty name leaves the track without one.
    Q_INVOKABLE void setTrackName(int track, const QString& name);
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
    /// Saves the song as a .elara file (the extension is added when missing)
    /// on a worker, so that the window goes on while it is written
    /// (ADR-0107). The state saved is the one the song is in now: edits made
    /// while it is written stay unsaved. saveFinished() follows, after
    /// editFailed() with the reason when it could not be saved. Saves are
    /// written one after the other, in the order asked.
    Q_INVOKABLE void save(const QUrl& file);
    /// Whether a save is being written.
    [[nodiscard]] bool saving() const noexcept { return saving_ > 0; }
    /// Writes the song as the score shows it (its track or all of them, in its
    /// view) on A4 pages to `file`, a PDF document (".pdf" is added when
    /// missing; ADR-0074). The pages are laid out again, so the continuous
    /// view prints as pages too. Laid out and written on a worker (ADR-0107);
    /// exportFinished() follows, after notice() or editFailed().
    Q_INVOKABLE void exportPdf(const QUrl& file);
    /// Writes the song as MusicXML to `file`: compressed for ".mxl", text for
    /// ".musicxml" or ".xml" (".musicxml" is added when it has none of them).
    /// Then report() tells what MusicXML left out. Returns false, and reports
    /// why through editFailed(), when it cannot.
    Q_INVOKABLE bool exportMusicXml(const QUrl& file);
    /// Writes the song as a Standard MIDI File to `file` (".mid" is added when
    /// it ends in neither ".mid" nor ".midi"), as it plays. Then report()
    /// tells what MIDI left out. Returns false, and reports why through
    /// editFailed(), when it cannot.
    Q_INVOKABLE bool exportMidi(const QUrl& file);
    /// The file an export suggests for `extension` ("pdf", "musicxml",
    /// "mid"): the song's file with that extension, or the song's title (or
    /// "Untitled") in the documents folder for a song without a file.
    Q_INVOKABLE [[nodiscard]] QUrl exportFile(const QString& extension) const;

    // ---- Keyboard entry -----------------------------------------------------

    /// The page showing the caret, or -1 when no page shows it (its track is
    /// hidden, or there is no song).
    [[nodiscard]] int caretPage() const { return caretPage_; }
    /// The caret on its page, in staff spaces from the page's top-left corner.
    [[nodiscard]] QRectF caretRect() const { return caretRect_; }
    /// The caret's beat across its staff on that page, the notation and the
    /// tablature when both are shown (notation::caretColumn): the band the
    /// caret's box stands in.
    [[nodiscard]] QRectF caretColumn() const { return caretColumn_; }
    /// The duration typing uses: 1 a whole note, 2 a half note... 64.
    [[nodiscard]] int inputValue() const;
    [[nodiscard]] bool inputDotted() const { return session_.input().dots() > 0; }
    /// The actual notes of the input duration's tuplet (3 for a triplet), 0
    /// when it has none.
    [[nodiscard]] int inputTuplet() const {
        return session_.input().tuplet().isNone() ? 0 : session_.input().tuplet().actual();
    }
    /// The caret in the song being edited.
    [[nodiscard]] core::Caret caret() const { return session_.caret(); }

    Q_INVOKABLE void moveCaret(CaretMove move);
    /// The voice typing goes into, 1 to 4 (the core's voice plus one).
    [[nodiscard]] int caretVoice() const { return static_cast<int>(session_.caret().voice) + 1; }
    /// Moves the caret to voice `voice` (1 to 4) of its measure, at its beat or
    /// the nearest one that voice has; typing then goes there.
    Q_INVOKABLE void setCaretVoice(int voice);
    /// Puts the caret where a click at (x, y) on page `index` points, in staff
    /// spaces from the page's top-left corner. Returns whether it moved there.
    Q_INVOKABLE bool placeCaret(int index, double x, double y);
    /// Puts the caret on the first beat of `measure` (from 0), on its track,
    /// staff, voice and string: a measure past the song's end is its last.
    Q_INVOKABLE void caretToMeasure(int measure);
    /// Types a digit: a fret on the caret's string. A second digit within
    /// kTwoDigitMilliseconds on the same caret makes a two-digit fret, as one
    /// undo step.
    Q_INVOKABLE void typeDigit(int digit);
    /// typeDigit() as if the key were pressed at `at`: what the window between
    /// two digits measures, whatever time the first one's layout took.
    void typeDigit(int digit, core::EditSession::Clock::time_point at);
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

    /// The caret's measure, 0-based, and its signatures.
    [[nodiscard]] int caretMeasure() const { return static_cast<int>(session_.caret().measure); }
    [[nodiscard]] int caretTimeNumerator() const;
    [[nodiscard]] int caretTimeDenominator() const;
    [[nodiscard]] int caretKeyFifths() const;
    [[nodiscard]] bool caretKeyMinor() const;
    [[nodiscard]] QVariantMap caretNavigation() const {
        return navigationAt(static_cast<int>(session_.caret().measure));
    }
    [[nodiscard]] QVariantMap caretRepeats() const {
        return repeatsAt(static_cast<int>(session_.caret().measure));
    }
    [[nodiscard]] QVariantMap caretTempo() const {
        return tempoAt(static_cast<int>(session_.caret().measure));
    }
    [[nodiscard]] int caretTripletFeel() const {
        return tripletFeelAt(static_cast<int>(session_.caret().measure));
    }
    [[nodiscard]] QString caretSection() const {
        return sectionAt(static_cast<int>(session_.caret().measure));
    }
    /// Inserts an empty measure before the caret's measure, or after it, with
    /// the signatures of the measure before it; the caret goes to it.
    Q_INVOKABLE void insertMeasure(bool after);
    /// Removes the caret's measure; the last measure of a song stays.
    Q_INVOKABLE void removeMeasure();

    /// Selected measures (ADR-0036): from an anchor, where the selection
    /// started, to the caret's measure. 0-based; -1 without a selection.
    [[nodiscard]] bool hasSelection() const { return session_.hasSelection(); }
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
    [[nodiscard]] bool canPaste() const { return session_.canPaste(); }
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

    // ---- Symbols edited where they are drawn (ADR-0130) -----------------------

    /// The symbol drawn at (x, y) on page `index`, in staff spaces from the
    /// page's top-left corner, among those a double click edits; an empty map
    /// where there is none. "kind" is "clef", "key", "time", "tempo",
    /// "section", "chord" or "text"; "rect" is the box round it on the page;
    /// "shown" is the measure it is drawn in and "measure" the one its value
    /// starts at, which is the one to change: a key signature written again at
    /// the start of a system is the one set some measures before. "track" and
    /// "staff" are the staff it is on.
    Q_INVOKABLE [[nodiscard]] QVariantMap symbolAt(int index, double x, double y) const;
    /// A measure's time and key signatures: "numerator", "denominator",
    /// "fifths" and "minor"; empty for a measure the song lacks.
    Q_INVOKABLE [[nodiscard]] QVariantMap signatureAt(int measure) const;

    // ---- For CaretEditor (ADR-0128) -------------------------------------------
    // What the editor of what is at the caret needs of the document. QML does
    // not call these.

    /// The song being edited; null before one is open.
    [[nodiscard]] const core::Song* editedSong() const {
        return document_ ? &document_->song() : nullptr;
    }
    /// The caret, the input duration and the selection.
    [[nodiscard]] const core::EditSession& session() const noexcept { return session_; }
    /// Ends the wait for a second digit, as an edit at the caret does before
    /// it is made (core::EditSession::endTyping()).
    void endTyping() noexcept { session_.endTyping(); }
    /// Enters an edit at the caret and shows what it did: the edited song, or
    /// only the caret. Reports through editFailed() what the song refuses.
    /// Returns whether the song changed. Only with a song open.
    bool enter(core::Entry entry);
    /// Reports through editFailed() an edit that cannot be made.
    void refuse(const QString& message) { emit editFailed(message); }

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
    void measureStartsChanged();
    void trackChanged();
    void viewChanged();
    void continuousChanged();
    void layoutChanged();
    void historyChanged();
    void caretChanged();
    /// A layout an edit, an undo or a redo caused moved the caret's staff
    /// `spaces` staff spaces down the pages, and `pages` pages on, while the
    /// caret stayed in the system it was in: the view scrolls as far, so that
    /// the staff stays where it was on screen. Comes just before caretChanged.
    void caretHeld(double spaces, int pages);
    void inputChanged();
    void selectionChanged();
    void clipboardChanged();
    /// Something done that the window shows for a moment ("Copied measure 3.").
    void notice(const QString& message);
    /// Something done with details to read on demand, one per line: what an
    /// export left out ("Exported song.mid. MIDI left out 3 things.").
    void report(const QString& message, const QStringList& details);
    void importWarningsChanged();
    /// An edit or a save that failed, with the reason to show.
    void editFailed(const QString& message);
    /// previewTrackMix() moved a fader: previewMixes() holds the new mixes.
    void mixPreviewed();
    void practiceChanged();
    void recoveryFolderChanged();
    /// The recovery copy of the song is on the disk.
    void recoveryWritten();
    void savingChanged();
    /// A save() is done: `saved` tells whether `file` holds the song now.
    void saveFinished(const QUrl& file, bool saved);
    /// An export on a worker is done: `written` tells whether `file` was.
    void exportFinished(const QUrl& file, bool written);

private:
    /// Opens `file`, recovered from `recovered` when it is a recovery copy.
    void load(const QUrl& file, std::optional<RecoveryStore::Copy> recovered);
    /// Reports work on `file` that could not start: editFailed() with
    /// `problem`, then `finished` saying it failed, once the caller returns.
    void failLater(void (ScoreDocument::*finished)(const QUrl&, bool), const QString& file,
                   const QString& problem);
    /// Keeps the recovery copy in step with the history: written a while
    /// after an unsaved change, removed once there are none.
    void keepRecovery();
    bool execute(std::unique_ptr<core::Command> command);
    bool writeExport(const QString& local, const io::ExportResult& result, const QString& format);
    /// Tells that `local` was exported, and what its format left out.
    void reportExport(const QString& local, const std::vector<std::string>& warnings,
                      const QString& format);
    void setImportWarnings(const QStringList& warnings);
    /// Another song: the session starts on its track `track`.
    void startSession(std::size_t track);
    /// Tells inputChanged() when the input duration is no longer `before`.
    void inputMaybeChanged(core::Duration before);
    /// Shows what an edit of the session did, the edited song or only the
    /// caret, and returns `changed`.
    bool showEdit(bool changed);
    /// The duration typing uses from now on, which the caret's beat takes too;
    /// reports through editFailed() a beat that cannot take it.
    void giveInput(core::Duration duration);
    /// Puts the caret on `caret`, or the nearest place the song has.
    void placeCaretAt(const core::Caret& caret);
    /// The caret back on the track shown and inside the song being edited.
    void clampCaret();
    /// Finds the caret on the pages shown.
    void placeCaretBox();
    void edited();
    /// Lays out the score again; `edit` when an edit, an undo or a redo asks.
    void requestLayout(bool edit);
    void apply(const ScoreLayout& layout, bool edit);
    void fail(const QString& message);
    void updateBusy();
    void resequence();
    /// The selection changed, or its place on the pages.
    void selectionMoved();

    QUrl source_;
    QStringList importWarnings_;
    Status status_ = Status::Empty;
    QString errorMessage_;
    std::shared_ptr<const core::Song> song_;          ///< what the pages show
    std::shared_ptr<const audio::Sequence> sequence_; ///< song_ as playback plays it
    std::shared_ptr<const core::Song> sequenceSong_;  ///< the song sequence_ was built from
    std::vector<audio::PlayedMeasure> played_;        ///< the measures of sequence_
    QVariantList measureStarts_;                      ///< when each of played_ starts
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
    /// The caret, the input duration, the digit waiting for a second one, the
    /// selection and the measures copied (ADR-0127).
    core::EditSession session_;
    int caretPage_ = -1;
    QRectF caretRect_;
    QRectF caretColumn_;
    std::size_t caretBoxMeasure_ = 0; ///< the caret's measure when caretRect_ was found
    /// Where the caret's box was before a layout an edit caused: in staff
    /// spaces from the top of the first page, on which page, in which measure.
    struct CaretAnchor {
        double top = 0.0;
        int page = 0;
        std::size_t measure = 0;
    };
    std::optional<CaretAnchor> anchor_;
    /// A layout asked for since the last one shown was not an edit's (a track,
    /// view or page change): the next layout holds nothing still.
    bool reshaped_ = false;
    int selectionRevision_ = 0;
    int track_ = 0;
    View view_ = View::Both;
    bool continuous_ = false;
    bool showsStrips_ = false;
    QList<qreal> pageTops_;
    bool hasTablature_ = false;
    int layoutMilliseconds_ = 0;
    bool busy_ = false;
    /// Opening and laying out: only the newest is shown (ADR-0110).
    std::unique_ptr<JobLane<ScoreLayout>> layouts_;
    /// Sequencing: only the newest is played.
    std::unique_ptr<JobLane<SequenceResult>> sequences_;
    std::shared_ptr<RecoveryStore> recovery_;        ///< shared with the write running
    QTimer recoveryTimer_;                           ///< writes the copy when it fires
    std::shared_ptr<const core::Song> recoverySong_; ///< the song the copy holds or will
    bool recoveryWriting_ = false;                   ///< a write is running on a worker
    QString recoveredFrom_;           ///< a recovered copy, to discard once this run's is written
    std::uint64_t documentsMade_ = 0; ///< documents opened or made so far: a save knows its own
    int saving_ = 0;                  ///< saves being written
    /// Saves run here, one at a time and in order; exports, one at a time, in
    /// the other. Destroyed with the document after the jobs left in them.
    QThreadPool saveQueue_;
    QThreadPool exportQueue_;
};

} // namespace elara::app
