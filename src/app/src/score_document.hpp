// SPDX-License-Identifier: AGPL-3.0-or-later
// The score shown in the window: opened and laid out on worker threads
// (ADR-0015).
#pragma once

#include <elara/core/document.hpp>
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>

#include "score_painter.hpp"
#include <QObject>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <memory>
#include <optional>
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

public:
    enum class Status : quint8 { Empty, Loading, Ready, Error };
    Q_ENUM(Status)
    /// How the shown tracks are drawn (notation::TrackView).
    enum class View : quint8 { Standard, Tablature, Both };
    Q_ENUM(View)

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
    /// kind of instrument) and symbol (a clef of the music font).
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
    /// Adds or removes measures at the end of the song.
    Q_INVOKABLE void setMeasureCount(int count);
    /// Adds a track of an instrument preset at the end and shows it.
    Q_INVOKABLE void addTrack(const QString& presetId);
    /// Removes a track; the last track of a song cannot be removed.
    Q_INVOKABLE void removeTrack(int index);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    /// Saves the song as a .elara file (the extension is added when missing).
    /// Returns false, and reports why through editFailed(), when it cannot.
    Q_INVOKABLE bool save(const QUrl& file);

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
    /// An edit or a save that failed, with the reason to show.
    void editFailed(const QString& message);

private:
    void execute(std::unique_ptr<core::Command> command);
    void edited();
    void apply(const ScoreLayout& layout);
    void fail(const QString& message);
    void setPending(int delta);

    QUrl source_;
    Status status_ = Status::Empty;
    QString errorMessage_;
    std::shared_ptr<const core::Song> song_;          ///< what the pages show
    std::shared_ptr<const audio::Sequence> sequence_; ///< song_ as playback plays it
    std::optional<core::Document> document_;          ///< the song being edited
    std::shared_ptr<const std::vector<notation::DisplayList>> pages_;
    int track_ = 0;
    View view_ = View::Both;
    bool hasTablature_ = false;
    int layoutMilliseconds_ = 0;
    int pending_ = 0;
    std::uint64_t generation_ = 0;
};

} // namespace elara::app
