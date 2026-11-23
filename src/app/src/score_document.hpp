// SPDX-License-Identifier: AGPL-3.0-or-later
// The score shown in the window: opened and laid out on worker threads
// (ADR-0015).
#pragma once

#include <elara/core/document.hpp>
#include <elara/core/duration.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>

#include "score_painter.hpp"
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
#include <vector>

namespace elara::audio {
class Sequence;
}

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
    Q_PROPERTY(double duration READ duration NOTIFY songChanged)
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
    Q_PROPERTY(QVariantList instrumentPresets READ instrumentPresets CONSTANT)
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
    Q_PROPERTY(int inputValue READ inputValue NOTIFY inputChanged)
    Q_PROPERTY(bool inputDotted READ inputDotted NOTIFY inputChanged)
    // The caret's measure.
    Q_PROPERTY(int caretMeasure READ caretMeasure NOTIFY caretChanged)
    Q_PROPERTY(int caretTimeNumerator READ caretTimeNumerator NOTIFY caretChanged)
    Q_PROPERTY(int caretTimeDenominator READ caretTimeDenominator NOTIFY caretChanged)
    Q_PROPERTY(int caretKeyFifths READ caretKeyFifths NOTIFY caretChanged)
    Q_PROPERTY(bool caretKeyMinor READ caretKeyMinor NOTIFY caretChanged)

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

    /// The song and the sequence it plays as, built when it is opened; null
    /// before.
    [[nodiscard]] std::shared_ptr<const core::Song> song() const { return song_; }
    [[nodiscard]] std::shared_ptr<const audio::Sequence> sequence() const { return sequence_; }

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
    [[nodiscard]] QVariantList tracks() const;
    /// The instruments a track can be added for: id, name and group (guitar,
    /// bass or other), in a menu's order.
    [[nodiscard]] static QVariantList instrumentPresets();
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
    /// Fret 5".
    [[nodiscard]] QString caretStatus() const;
    /// Whether the caret's staff has strings, so that digits type frets.
    [[nodiscard]] bool caretFretted() const;
    /// The duration typing uses: 1 a whole note, 2 a half note... 64.
    [[nodiscard]] int inputValue() const;
    [[nodiscard]] bool inputDotted() const { return input_.dots() > 0; }
    /// The caret in the song being edited.
    [[nodiscard]] core::Caret caret() const { return caret_; }
    /// The fret of the note on the caret's string at its beat, if there is one.
    [[nodiscard]] std::optional<int> fretAtCaret() const;

    Q_INVOKABLE void moveCaret(CaretMove move);
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
    /// The next longer or shorter note value, as setInputValue().
    Q_INVOKABLE void longerDuration();
    Q_INVOKABLE void shorterDuration();
    Q_INVOKABLE void deleteAtCaret();
    Q_INVOKABLE void deleteBeat();
    Q_INVOKABLE void insertRest();

    /// The caret's measure, 0-based, and its signatures.
    [[nodiscard]] int caretMeasure() const { return static_cast<int>(caret_.measure); }
    [[nodiscard]] int caretTimeNumerator() const;
    [[nodiscard]] int caretTimeDenominator() const;
    [[nodiscard]] int caretKeyFifths() const;
    [[nodiscard]] bool caretKeyMinor() const;
    /// Inserts an empty measure before the caret's measure, or after it, with
    /// the signatures of the measure before it; the caret goes to it.
    Q_INVOKABLE void insertMeasure(bool after);
    /// Removes the caret's measure; the last measure of a song stays.
    Q_INVOKABLE void removeMeasure();

    /// Page `index`, shared with the painting threads; null when out of range.
    [[nodiscard]] std::shared_ptr<const notation::DisplayList> page(int index) const;
    /// The painter of every page.
    [[nodiscard]] static const ScorePainter& painter();

signals:
    void sourceChanged();
    void statusChanged();
    void busyChanged();
    void songChanged();
    void trackChanged();
    void viewChanged();
    void layoutChanged();
    void historyChanged();
    void caretChanged();
    void inputChanged();
    /// An edit or a save that failed, with the reason to show.
    void editFailed(const QString& message);
    /// previewTrackMix() moved a fader: previewMixes() holds the new mixes.
    void mixPreviewed();

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

    QUrl source_;
    Status status_ = Status::Empty;
    QString errorMessage_;
    std::shared_ptr<const core::Song> song_;          ///< what the pages show
    std::shared_ptr<const audio::Sequence> sequence_; ///< song_ as playback plays it
    std::vector<core::TrackMix> previewMixes_;        // previewTrackMix()
    std::optional<core::Document> document_;          ///< the song being edited
    std::shared_ptr<const std::vector<notation::DisplayList>> pages_;
    std::shared_ptr<const core::Song> pagesSong_; ///< the song the pages show
    core::Caret caret_;
    core::Duration input_;
    int caretPage_ = -1;
    QRectF caretRect_;
    int pendingDigit_ = -1;       ///< a digit just typed, waiting for a second
    core::Caret digitCaret_;      ///< where it was typed
    std::uint64_t digitEdit_ = 0; ///< edits_ after it
    bool digitChanged_ = false;   ///< whether it changed the song (and can be undone)
    QElapsedTimer digitClock_;
    std::uint64_t edits_ = 0; ///< edits, undos and redos so far
    int track_ = 0;
    View view_ = View::Both;
    bool hasTablature_ = false;
    int layoutMilliseconds_ = 0;
    int pending_ = 0;
    std::uint64_t generation_ = 0;
};

} // namespace elara::app
