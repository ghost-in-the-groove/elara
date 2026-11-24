// SPDX-License-Identifier: AGPL-3.0-or-later
#include "score_document.hpp"

#include <elara/audio/sequence.hpp>
#include <elara/core/commands.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/retune.hpp>
#include <elara/io/native_format.hpp>
#include <elara/io/open.hpp>
#include <elara/notation/caret.hpp>
#include <elara/notation/cursor.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace elara::app {

/// What a worker hands back: the laid-out pages of a song, or an error.
struct ScoreLayout {
    QUrl source;
    std::shared_ptr<const core::Song> song;
    /// Built on the worker for a song just opened.
    std::shared_ptr<const audio::Sequence> sequence;
    std::shared_ptr<const std::vector<notation::DisplayList>> pages;
    int track = 0;
    ScoreDocument::View view = ScoreDocument::View::Both;
    bool hasTablature = false;
    int milliseconds = 0;
    QString error;
    bool opened = false; ///< a song just read from `source`, to edit from now on
};

namespace {

constexpr double kStaffSpaceMm = 1.75;

QString qstring(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Whether a song has somewhere to put the caret.
bool hasCaret(const core::Song& song) {
    return song.trackCount() > 0 && song.measureCount() > 0;
}

const notation::PageLayoutOptions& pageOptions() {
    static const notation::PageLayoutOptions options =
        notation::pageOptions(notation::kA4, kStaffSpaceMm);
    return options;
}

// The music font's metrics, read once from the app's resources.
const notation::EngravingFont* musicMetrics() {
    static const std::optional<notation::EngravingFont> font =
        []() -> std::optional<notation::EngravingFont> {
        QFile file(QStringLiteral(":/fonts/Bravura.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        const QByteArray json = file.readAll();
        auto loaded = notation::EngravingFont::fromMetadata(
            std::string_view(json.constData(), static_cast<std::size_t>(json.size())));
        if (!loaded) {
            return std::nullopt;
        }
        return *std::move(loaded);
    }();
    return font ? &*font : nullptr;
}

notation::TrackView trackView(ScoreDocument::View view) {
    switch (view) {
    case ScoreDocument::View::Standard:
        return notation::TrackView::Standard;
    case ScoreDocument::View::Tablature:
        return notation::TrackView::Tablature;
    case ScoreDocument::View::Both:
        break;
    }
    return notation::TrackView::Both;
}

std::vector<std::size_t> shownTracks(const core::Song& song, int track) {
    std::vector<std::size_t> tracks;
    if (track == ScoreDocument::kAllTracks) {
        for (std::size_t t = 0; t < song.trackCount(); ++t) {
            tracks.push_back(t);
        }
    } else {
        tracks.push_back(static_cast<std::size_t>(track));
    }
    return tracks;
}

// The sequence playback plays for `song`: `heard` when the song sounds the
// same with it (the mix or the song's info changed), so playback carries on
// (ADR-0039); a new one otherwise. Runs on a worker thread.
std::shared_ptr<const audio::Sequence> sequenceFor(const core::Song& song,
                                                   std::shared_ptr<const audio::Sequence> heard) {
    auto sequence = std::make_shared<const audio::Sequence>(song);
    if (heard && *sequence == *heard) {
        return heard;
    }
    return sequence;
}

// An edited song's sequence, or why it could not be built.
struct SequenceResult {
    std::shared_ptr<const audio::Sequence> sequence;
    QString error;
};

// Lays out `track` (or every track) of `song` as `view` on A4 pages. Runs on a
// worker thread and never throws.
ScoreLayout layOut(std::shared_ptr<const core::Song> song, int track, ScoreDocument::View view) {
    ScoreLayout result;
    result.song = song;
    result.track = track;
    result.view = view;
    const notation::EngravingFont* font = musicMetrics();
    if (font == nullptr) {
        result.error = QObject::tr("The music font is missing.");
        return result;
    }
    try {
        const auto tracks = shownTracks(*song, track);
        result.hasTablature = notation::hasTablature(*song, tracks);
        const auto staves = notation::selectTracks(*song, tracks, trackView(view));
        const auto start = std::chrono::steady_clock::now();
        auto pages = notation::layoutPages(*song, staves, *font, pageOptions());
        const auto elapsed = std::chrono::steady_clock::now() - start;
        result.milliseconds = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        result.pages = std::make_shared<const std::vector<notation::DisplayList>>(std::move(pages));
    } catch (const std::exception& error) {
        result.error =
            QObject::tr("The score could not be laid out: %1").arg(qstring(error.what()));
    }
    return result;
}

// Opens `file` and lays out its first track: as notation and tablature when it
// is fretted. Runs on a worker thread and never throws.
ScoreLayout openAndLayOut(const QUrl& file) {
    ScoreLayout result;
    result.source = file;
    try {
        const QString local = file.isLocalFile() ? file.toLocalFile() : file.toString();
        auto opened = io::openFile(std::filesystem::path(local.toStdU16String()));
        if (!opened) {
            result.error = qstring(opened.error().message);
            return result;
        }
        auto song = std::make_shared<const core::Song>(std::move(opened->song));
        if (song->trackCount() == 0 || song->measureCount() == 0) {
            result.error = QObject::tr("The file has no music.");
            return result;
        }
        const std::array<std::size_t, 1> first{0};
        const auto view = notation::hasTablature(*song, first) ? ScoreDocument::View::Both
                                                               : ScoreDocument::View::Standard;
        auto sequence = std::make_shared<const audio::Sequence>(*song);
        result = layOut(std::move(song), 0, view);
        result.source = file;
        result.opened = true;
        result.sequence = std::move(sequence);
    } catch (const std::exception& error) {
        result.error = qstring(error.what());
    }
    return result;
}

constexpr char kContext[] = "elara::app::ScoreDocument";

// The names of the instrument presets and of the edits, translated in the
// context of this class. Unknown ones are shown as the core writes them.
struct Translation {
    std::string_view key;
    const char* text;
};

constexpr std::array kPresetNames{
    Translation{"electric-guitar",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Electric guitar")},
    Translation{"distortion-guitar",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Distortion guitar")},
    Translation{"acoustic-guitar",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Acoustic guitar")},
    Translation{"classical-guitar",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Classical guitar")},
    Translation{"seven-string-guitar",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "7-string guitar")},
    Translation{"electric-bass", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Electric bass")},
    Translation{"five-string-bass",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "5-string bass")},
    Translation{"fretless-bass", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Fretless bass")},
    Translation{"ukulele", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Ukulele")},
    Translation{"mandolin", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Mandolin")},
    Translation{"banjo", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Banjo")},
    Translation{"drum-kit", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Drums")},
    Translation{"piano", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Piano")},
    Translation{"voice", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Voice")},
    Translation{"violin", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Violin")},
    Translation{"flute", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Flute")},
    Translation{"trumpet", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Trumpet in B-flat")},
    Translation{"alto-sax", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Alto saxophone")},
    Translation{"tenor-sax", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Tenor saxophone")},
};

constexpr std::array kEditNames{
    Translation{"Insert measures",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Insert measures")},
    Translation{"Remove measures",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Remove measures")},
    Translation{"Add track", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Add track")},
    Translation{"Remove track", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Remove track")},
    Translation{"Edit measure", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Edit measure")},
    Translation{"Edit song info", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Edit song info")},
    Translation{"Change tempo", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change tempo")},
    Translation{"Change mix", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change mix")},
    Translation{"Change navigation marks",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change navigation marks")},
    Translation{"Change repeats", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change repeats")},
    Translation{"Change time signature",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change time signature")},
    Translation{"Change key signature",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change key signature")},
    Translation{"Enter note", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Enter note")},
    Translation{"Change duration",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change duration")},
    Translation{"Delete", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Delete")},
    Translation{"Delete beat", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Delete beat")},
    Translation{"Insert rest", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Insert rest")},
    Translation{"Change pitch", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change pitch")},
    Translation{"Change tuning", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change tuning")},
};

// The names of Tuning::presetNames().
constexpr std::array kTuningNames{
    Translation{"guitar", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Guitar, standard")},
    Translation{"guitar-drop-d", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Guitar, drop D")},
    Translation{"guitar-half-step-down",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Guitar, half a step down")},
    Translation{"guitar-dadgad", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Guitar, DADGAD")},
    Translation{"guitar-open-g", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Guitar, open G")},
    Translation{"guitar-open-d", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Guitar, open D")},
    Translation{"guitar-7", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Seven-string guitar")},
    Translation{"guitar-8", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Eight-string guitar")},
    Translation{"bass", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Bass, standard")},
    Translation{"bass-drop-d", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Bass, drop D")},
    Translation{"bass-5", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Five-string bass")},
    Translation{"bass-6", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Six-string bass")},
    Translation{"ukulele", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Ukulele")},
    Translation{"mandolin", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Mandolin")},
    Translation{"banjo", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Banjo")},
};

template <std::size_t N>
QString translated(const std::array<Translation, N>& table, std::string_view key,
                   std::string_view fallback) {
    const auto found = std::ranges::find(table, key, &Translation::key);
    return found == table.end() ? qstring(fallback)
                                : QCoreApplication::translate(kContext, found->text);
}

// The preset menu groups guitars, basses (whose tuning presets start with
// "bass") and the other instruments.
QString presetGroup(const core::InstrumentPreset& preset) {
    if (preset.kind != core::InstrumentKind::Fretted) {
        return QStringLiteral("other");
    }
    return preset.tuningPreset.starts_with("bass") ? QStringLiteral("bass")
                                                   : QStringLiteral("guitar");
}

// A clef of the music font that stands for the track's instrument.
QString trackSymbol(const core::Track& track) {
    const core::Clef clef = track.staff(0).clef();
    const bool bassClef = clef == core::Clef::Bass || clef == core::Clef::Bass8vb;
    switch (track.instrument().kind) {
    case core::InstrumentKind::Percussion:
        return QString(QChar(0xE069)); // unpitched percussion clef
    case core::InstrumentKind::Fretted:
        return QString(QChar(bassClef ? 0xE062 : 0xE06D)); // F clef, tablature clef
    default:
        break;
    }
    return QString(QChar(bassClef ? 0xE062 : 0xE050)); // F clef, G clef
}

// "6 strings · E A D G B E", "Percussion", "2 staves".
QString trackDetail(const core::Track& track) {
    const core::Staff& staff = track.staff(0);
    if (staff.tuning()) {
        constexpr std::string_view kLetters = "CDEFGAB";
        QStringList names;
        for (int string = staff.tuning()->stringCount(); string >= 1; --string) {
            const core::Pitch open = staff.tuning()->openString(string);
            QString name(QChar::fromLatin1(kLetters[static_cast<std::size_t>(open.step())]));
            for (int i = 0; i < open.alter(); ++i) {
                name += QChar(0x266F);
            }
            for (int i = 0; i > open.alter(); --i) {
                name += QChar(0x266D);
            }
            names << name;
        }
        QString detail = QCoreApplication::translate("elara::app::ScoreDocument", "%n strings", "",
                                                     staff.tuning()->stringCount()) +
                         QStringLiteral(" · ") + names.join(QLatin1Char(' '));
        if (staff.capo() > 0) {
            detail += QStringLiteral(" · ") +
                      QCoreApplication::translate("elara::app::ScoreDocument", "capo %1")
                          .arg(staff.capo());
        }
        return detail;
    }
    if (track.instrument().kind == core::InstrumentKind::Percussion) {
        return QCoreApplication::translate("elara::app::ScoreDocument", "Percussion");
    }
    return QCoreApplication::translate("elara::app::ScoreDocument", "%n staves", "",
                                       static_cast<int>(track.staffCount()));
}

} // namespace

ScoreDocument::ScoreDocument(QObject* parent) : QObject(parent) {}

QString ScoreDocument::title() const {
    if (!song_) {
        return {};
    }
    const std::string& title = song_->info().title;
    if (!title.empty()) {
        return qstring(title);
    }
    return source_.isEmpty() ? tr("Untitled") : QFileInfo(source_.toLocalFile()).completeBaseName();
}

QString ScoreDocument::artist() const {
    return song_ ? qstring(song_->info().artist) : QString();
}

QStringList ScoreDocument::trackNames() const {
    QStringList names;
    if (!song_) {
        return names;
    }
    for (std::size_t t = 0; t < song_->trackCount(); ++t) {
        const std::string& name = song_->track(t).name();
        names.append(name.empty() ? tr("Track %1").arg(t + 1) : qstring(name));
    }
    return names;
}

int ScoreDocument::measureCount() const {
    return song_ ? static_cast<int>(song_->measureCount()) : 0;
}

double ScoreDocument::duration() const {
    return sequence_ ? sequence_->duration() : 0.0;
}

QVariantMap ScoreDocument::cursorAt(double seconds) const {
    if (!sequence_ || !pages_) {
        return {};
    }
    const std::size_t played = sequence_->playedMeasureAt(seconds);
    if (played >= sequence_->measures().size()) {
        return {};
    }
    const audio::PlayedMeasure& measure = sequence_->measures()[played];
    const double length = measure.end - measure.start;
    const auto cursor = notation::cursorAt(*pages_, measure.measure,
                                           length > 0.0 ? (seconds - measure.start) / length : 0.0);
    if (!cursor) {
        return {};
    }
    return {{QStringLiteral("page"), static_cast<int>(cursor->page)},
            {QStringLiteral("x"), cursor->x},
            {QStringLiteral("y"), cursor->y},
            {QStringLiteral("height"), cursor->height}};
}

double ScoreDocument::secondsAt(int page, double x, double y) const {
    if (!sequence_ || !pages_ || page < 0 || static_cast<std::size_t>(page) >= pages_->size()) {
        return -1.0;
    }
    const auto position =
        notation::measurePositionAt((*pages_)[static_cast<std::size_t>(page)], x, y);
    if (!position) {
        return -1.0;
    }
    return sequence_->secondsAt(position->measure, position->fraction).value_or(-1.0);
}

void ScoreDocument::setTrack(int track) {
    if (track == track_ || !song_ || track < kAllTracks ||
        track >= static_cast<int>(song_->trackCount())) {
        return;
    }
    track_ = track;
    emit trackChanged();
    clampCaret();
    relayout();
}

void ScoreDocument::setView(View view) {
    if (view == view_) {
        return;
    }
    view_ = view;
    emit viewChanged();
    relayout();
}

int ScoreDocument::pageCount() const {
    return pages_ ? static_cast<int>(pages_->size()) : 0;
}

QSizeF ScoreDocument::pageSize() const {
    return {pageOptions().width, pageOptions().height};
}

double ScoreDocument::staffSpace() {
    return kStaffSpaceMm;
}

QStringList ScoreDocument::nameFilters() {
    QString all;
    for (const std::string_view extension : io::openExtensions()) {
        all += (all.isEmpty() ? QStringLiteral("*") : QStringLiteral(" *")) + qstring(extension);
    }
    return {tr("Scores (%1)").arg(all),
            tr("Guitar Pro (*.gp *.gpx *.gp5 *.gp4 *.gp3)"),
            tr("MusicXML (*.musicxml *.xml *.mxl)"),
            tr("MIDI (*.mid *.midi *.smf)"),
            tr("Elara (*.elara)"),
            tr("All files (*)")};
}

void ScoreDocument::open(const QUrl& file) {
    const std::uint64_t generation = ++generation_;
    status_ = Status::Loading;
    errorMessage_.clear();
    emit statusChanged();
    setPending(+1);
    QtConcurrent::run([file] {
        return openAndLayOut(file);
    }).then(this, [this, generation](const ScoreLayout& result) {
        setPending(-1);
        if (generation != generation_) {
            return;
        }
        if (!result.error.isEmpty()) {
            fail(result.error);
            return;
        }
        apply(result);
    });
}

void ScoreDocument::relayout() {
    if (!song_) {
        return;
    }
    const std::uint64_t generation = ++generation_;
    setPending(+1);
    QtConcurrent::run([song = song_, track = track_, view = view_] {
        return layOut(song, track, view);
    }).then(this, [this, generation](const ScoreLayout& result) {
        setPending(-1);
        if (generation != generation_) {
            return;
        }
        if (!result.error.isEmpty()) {
            fail(result.error);
            return;
        }
        apply(result);
    });
    // An edit's sequence is built beside its layout, on another worker: the
    // pages show as soon as they are laid out instead of waiting for it.
    if (sequenceSong_ != song_ && sequencing_ != song_) {
        resequence();
    }
}

// Builds the edited song's sequence off the interface thread: copying the song
// is all an edit costs it. Until it arrives, playback plays the previous one
// with the new mix (ADR-0031).
void ScoreDocument::resequence() {
    const std::uint64_t generation = ++sequenceGeneration_;
    sequencing_ = song_;
    setPending(+1);
    QtConcurrent::run([song = song_, heard = sequence_] {
        SequenceResult result;
        try {
            result.sequence = sequenceFor(*song, heard);
        } catch (const std::exception& error) {
            result.error = qstring(error.what());
        }
        return result;
    }).then(this, [this, generation, song = song_](const SequenceResult& result) {
        setPending(-1);
        if (generation != sequenceGeneration_) {
            return;
        }
        sequencing_.reset();
        if (!result.error.isEmpty()) {
            fail(result.error);
            return;
        }
        // The song and its sequence change together, so whoever hears
        // sequenceChanged sees them as a pair.
        sequenceSong_ = song;
        if (result.sequence != sequence_) {
            sequence_ = result.sequence;
            emit sequenceChanged();
        }
    });
}

std::shared_ptr<const notation::DisplayList> ScoreDocument::page(int index) const {
    if (!pages_ || index < 0 || index >= static_cast<int>(pages_->size())) {
        return nullptr;
    }
    // Shares ownership of every page, so a relayout cannot free it mid-paint.
    return {pages_, &(*pages_)[static_cast<std::size_t>(index)]};
}

const ScorePainter& ScoreDocument::painter() {
    static const ScorePainter painter(ScoreFonts::fromDirectory(QStringLiteral(":/fonts")));
    return painter;
}

void ScoreDocument::apply(const ScoreLayout& layout) {
    if (layout.opened) {
        // An edit's sequence still being built belongs to the song before.
        ++sequenceGeneration_;
        sequencing_.reset();
        document_.emplace(*layout.song);
        caret_ = core::Caret{.track = static_cast<std::size_t>(std::max(0, layout.track))};
        pendingDigit_ = -1;
        setInput(core::Duration{});
        emit historyChanged();
    }
    // The song and its sequence change together, so whoever hears songChanged
    // sees them as a pair.
    const bool newSong = layout.song != song_;
    song_ = layout.song;
    bool newSequence = false;
    if (layout.sequence) {
        sequenceSong_ = layout.song;
        newSequence = layout.sequence != sequence_;
        sequence_ = layout.sequence;
    }
    if (layout.opened) {
        emit songReplaced();
    }
    if (newSong) {
        if (!layout.source.isEmpty() && layout.source != source_) {
            source_ = layout.source;
            emit sourceChanged();
        }
        emit songChanged();
    }
    if (newSequence) {
        emit sequenceChanged();
    }
    if (layout.track != track_) {
        track_ = layout.track;
        emit trackChanged();
    }
    if (layout.view != view_) {
        view_ = layout.view;
        emit viewChanged();
    }
    pages_ = layout.pages;
    pagesSong_ = layout.song;
    hasTablature_ = layout.hasTablature;
    layoutMilliseconds_ = layout.milliseconds;
    emit layoutChanged();
    clampCaret();
    placeCaretBox();
    if (status_ != Status::Ready) {
        status_ = Status::Ready;
        errorMessage_.clear();
        emit statusChanged();
    }
}

// ---- Editing ----------------------------------------------------------------

QString ScoreDocument::songTitle() const {
    return song_ ? qstring(song_->info().title) : QString();
}

int ScoreDocument::tempo() const {
    return song_ ? static_cast<int>(std::lround(song_->startTempo().bpm())) : 120;
}

int ScoreDocument::timeNumerator() const {
    return song_ && song_->measureCount() > 0 ? song_->measureHeader(0).timeSignature.numerator()
                                              : 4;
}

int ScoreDocument::timeDenominator() const {
    return song_ && song_->measureCount() > 0 ? song_->measureHeader(0).timeSignature.denominator()
                                              : 4;
}

int ScoreDocument::keyFifths() const {
    return song_ && song_->measureCount() > 0 ? song_->measureHeader(0).keySignature.fifths() : 0;
}

bool ScoreDocument::keyMinor() const {
    return song_ && song_->measureCount() > 0 &&
           song_->measureHeader(0).keySignature.mode() == core::KeyMode::Minor;
}

QVariantList ScoreDocument::tracks() const {
    QVariantList list;
    if (!song_) {
        return list;
    }
    const QStringList names = trackNames();
    for (std::size_t t = 0; t < song_->trackCount(); ++t) {
        const core::Track& track = song_->track(t);
        const core::TrackMix& mix = track.mix();
        const QVariantMap tuning = trackTuning(static_cast<int>(t));
        list.append(
            QVariantMap{{QStringLiteral("name"), names[static_cast<qsizetype>(t)]},
                        {QStringLiteral("detail"), trackDetail(track)},
                        {QStringLiteral("symbol"), trackSymbol(track)},
                        {QStringLiteral("volume"), mix.volume},
                        {QStringLiteral("pan"), mix.pan},
                        {QStringLiteral("mute"), mix.mute},
                        {QStringLiteral("solo"), mix.solo},
                        {QStringLiteral("audible"), song_->audible(t)},
                        {QStringLiteral("fretted"), tuning.value(QStringLiteral("fretted"))},
                        {QStringLiteral("tuning"), tuning.value(QStringLiteral("notes"))},
                        {QStringLiteral("tuningPreset"), tuning.value(QStringLiteral("preset"))},
                        {QStringLiteral("capo"), tuning.value(QStringLiteral("capo"))}});
    }
    return list;
}

QVariantList ScoreDocument::instrumentPresets() {
    QVariantList list;
    for (const core::InstrumentPreset& preset : core::instrumentPresets()) {
        list.append(
            QVariantMap{{QStringLiteral("id"), qstring(preset.id)},
                        {QStringLiteral("name"), translated(kPresetNames, preset.id, preset.name)},
                        {QStringLiteral("group"), presetGroup(preset)}});
    }
    return list;
}

QVariantList ScoreDocument::tuningPresets() {
    QVariantList list;
    for (const std::string_view id : core::Tuning::presetNames()) {
        const std::optional<core::Tuning> tuning = core::Tuning::preset(id);
        list.append(QVariantMap{
            {QStringLiteral("id"), qstring(id)},
            {QStringLiteral("name"), translated(kTuningNames, id, id)},
            {QStringLiteral("notes"), tuning ? qstring(tuning->toString()) : QString()}});
    }
    return list;
}

QVariantMap ScoreDocument::trackTuning(int track) const {
    if (!song_ || track < 0 || static_cast<std::size_t>(track) >= song_->trackCount()) {
        return {{QStringLiteral("fretted"), false}};
    }
    const core::Staff& staff = song_->track(static_cast<std::size_t>(track)).staff(0);
    if (!staff.tuning()) {
        return {{QStringLiteral("fretted"), false}};
    }
    QString preset;
    for (const std::string_view id : core::Tuning::presetNames()) {
        if (core::Tuning::preset(id) == staff.tuning()) {
            preset = qstring(id);
            break;
        }
    }
    return {{QStringLiteral("fretted"), true},
            {QStringLiteral("notes"), qstring(staff.tuning()->toString())},
            {QStringLiteral("preset"), preset},
            {QStringLiteral("capo"), staff.capo()}};
}

QString ScoreDocument::undoName() const {
    return canUndo() ? translated(kEditNames, document_->undoName(), document_->undoName())
                     : QString();
}

QString ScoreDocument::redoName() const {
    return canRedo() ? translated(kEditNames, document_->redoName(), document_->redoName())
                     : QString();
}

bool ScoreDocument::savesInPlace() const {
    return source_.isLocalFile() &&
           source_.toLocalFile().endsWith(QStringLiteral(".elara"), Qt::CaseInsensitive);
}

void ScoreDocument::newSong(const QString& presetId) {
    const auto* preset = core::findInstrumentPreset(presetId.toStdString());
    if (preset == nullptr) {
        emit editFailed(tr("Unknown instrument: %1").arg(presetId));
        return;
    }
    core::Song song;
    core::Track track = core::Track::fromPreset(*preset);
    track.setName(translated(kPresetNames, preset->id, preset->name).toStdString());
    song.addTrack(std::move(track));
    song.appendMeasures(8);
    ++generation_;         // a file still opening must not replace the new song
    ++sequenceGeneration_; // nor an edit's sequence still being built
    sequencing_.reset();
    document_.emplace(std::move(song));
    song_ = std::make_shared<const core::Song>(document_->song());
    sequence_ = std::make_shared<const audio::Sequence>(*song_);
    sequenceSong_ = song_;
    source_.clear();
    track_ = 0;
    caret_ = core::Caret{};
    pendingDigit_ = -1;
    setInput(core::Duration{});
    const std::array<std::size_t, 1> first{0};
    view_ = notation::hasTablature(*song_, first) ? View::Both : View::Standard;
    emit songReplaced();
    emit sourceChanged();
    emit songChanged();
    emit sequenceChanged();
    emit trackChanged();
    emit viewChanged();
    emit historyChanged();
    relayout();
}

void ScoreDocument::setSongTitle(const QString& title) {
    if (!document_ || title == songTitle()) {
        return;
    }
    core::SongInfo info = document_->song().info();
    info.title = title.toStdString();
    execute(std::make_unique<core::SetSongInfo>(std::move(info)));
}

void ScoreDocument::setArtist(const QString& artist) {
    if (!document_ || artist == this->artist()) {
        return;
    }
    core::SongInfo info = document_->song().info();
    info.artist = artist.toStdString();
    execute(std::make_unique<core::SetSongInfo>(std::move(info)));
}

void ScoreDocument::setTempo(int bpm) {
    if (!document_ || bpm == tempo() || bpm < static_cast<int>(core::Tempo::kMinBpm) ||
        bpm > static_cast<int>(core::Tempo::kMaxBpm)) {
        return;
    }
    execute(std::make_unique<core::SetStartTempo>(core::Tempo(bpm)));
}

void ScoreDocument::setTimeSignature(int numerator, int denominator) {
    setTimeSignatureFrom(0, numerator, denominator);
}

void ScoreDocument::setKeySignature(int fifths, bool minor) {
    setKeySignatureFrom(0, fifths, minor);
}

void ScoreDocument::setTimeSignatureFrom(int measure, int numerator, int denominator) {
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return;
    }
    try {
        execute(core::changeTimeSignature(document_->song(), static_cast<std::size_t>(measure),
                                          core::TimeSignature(numerator, denominator)));
    } catch (const std::invalid_argument&) {
        emit editFailed(tr("%1/%2 is not a time signature.").arg(numerator).arg(denominator));
    }
}

void ScoreDocument::setKeySignatureFrom(int measure, int fifths, bool minor) {
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount()) ||
        std::abs(fifths) > core::KeySignature::kMaxFifths) {
        return;
    }
    execute(core::changeKeySignature(
        document_->song(), static_cast<std::size_t>(measure),
        core::KeySignature(fifths, minor ? core::KeyMode::Minor : core::KeyMode::Major)));
}

namespace {

// The flags navigationAt() and setNavigation() name.
struct NavigationFlag {
    const char* key;
    bool core::MeasureHeader::* flag;
};
constexpr std::array kNavigationFlags{
    NavigationFlag{"segno", &core::MeasureHeader::segno},
    NavigationFlag{"segnoSegno", &core::MeasureHeader::segnoSegno},
    NavigationFlag{"coda", &core::MeasureHeader::coda},
    NavigationFlag{"doubleCoda", &core::MeasureHeader::doubleCoda},
    NavigationFlag{"fine", &core::MeasureHeader::fine},
    NavigationFlag{"toCoda", &core::MeasureHeader::toCoda},
    NavigationFlag{"toDoubleCoda", &core::MeasureHeader::toDoubleCoda},
};

} // namespace

QVariantMap ScoreDocument::navigationAt(int measure) const {
    QVariantMap marks;
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return marks;
    }
    const core::MeasureHeader& header =
        document_->song().measureHeader(static_cast<std::size_t>(measure));
    for (const NavigationFlag& item : kNavigationFlags) {
        marks.insert(QLatin1StringView(item.key), header.*item.flag);
    }
    marks.insert(QStringLiteral("jump"), static_cast<int>(header.jump));
    marks.insert(QStringLiteral("jumpThen"), static_cast<int>(header.jumpThen));
    return marks;
}

void ScoreDocument::setNavigation(int measure, const QVariantMap& marks) {
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return;
    }
    const auto m = static_cast<std::size_t>(measure);
    core::MeasureHeader header = document_->song().measureHeader(m);
    for (const NavigationFlag& item : kNavigationFlags) {
        if (const auto found = marks.constFind(QLatin1StringView(item.key));
            found != marks.cend()) {
            header.*item.flag = found->toBool();
        }
    }
    const auto choice = [&marks](const QString& key, int current) {
        const int value = marks.value(key, current).toInt();
        return value >= 0 && value <= 3 ? value : current;
    };
    header.jump =
        static_cast<core::JumpTo>(choice(QStringLiteral("jump"), static_cast<int>(header.jump)));
    header.jumpThen = static_cast<core::JumpThen>(
        choice(QStringLiteral("jumpThen"), static_cast<int>(header.jumpThen)));
    if (header.jump == core::JumpTo::None) {
        header.jumpThen = core::JumpThen::End;
    }
    if (header != document_->song().measureHeader(m)) {
        execute(std::make_unique<core::SetMeasureHeader>(m, std::move(header)));
    }
}

QVariantMap ScoreDocument::repeatsAt(int measure) const {
    QVariantMap repeats;
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return repeats;
    }
    const core::MeasureHeader& header =
        document_->song().measureHeader(static_cast<std::size_t>(measure));
    QVariantList endings;
    for (int time = 1; time <= core::MeasureHeader::kMaxEnding; ++time) {
        if (header.alternateEndings != 0 && header.playedOnTime(time)) {
            endings.append(time);
        }
    }
    repeats.insert(QStringLiteral("start"), header.repeatStart);
    repeats.insert(QStringLiteral("times"), header.repeatEnd() ? header.repeatTimes : 0);
    repeats.insert(QStringLiteral("endings"), endings);
    return repeats;
}

void ScoreDocument::setRepeats(int measure, const QVariantMap& repeats) {
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return;
    }
    const auto m = static_cast<std::size_t>(measure);
    core::MeasureHeader header = document_->song().measureHeader(m);
    if (const auto found = repeats.constFind(QStringLiteral("start")); found != repeats.cend()) {
        header.repeatStart = found->toBool();
    }
    if (const auto found = repeats.constFind(QStringLiteral("times")); found != repeats.cend()) {
        const int times = found->toInt();
        if (times == 0 || (times >= 2 && times <= std::numeric_limits<std::uint8_t>::max())) {
            header.repeatTimes = static_cast<std::uint8_t>(times);
        }
    }
    if (const auto found = repeats.constFind(QStringLiteral("endings")); found != repeats.cend()) {
        unsigned endings = 0;
        for (const QVariant& time : found->toList()) {
            const int value = time.toInt();
            if (value >= 1 && value <= core::MeasureHeader::kMaxEnding) {
                endings |= 1U << static_cast<unsigned>(value - 1);
            }
        }
        header.alternateEndings = static_cast<std::uint8_t>(endings);
    }
    if (header != document_->song().measureHeader(m)) {
        execute(std::make_unique<core::SetMeasureHeader>(m, std::move(header)));
    }
}

int ScoreDocument::caretTimeNumerator() const {
    return document_ && caret_.measure < document_->song().measureCount()
               ? document_->song().measureHeader(caret_.measure).timeSignature.numerator()
               : 4;
}

int ScoreDocument::caretTimeDenominator() const {
    return document_ && caret_.measure < document_->song().measureCount()
               ? document_->song().measureHeader(caret_.measure).timeSignature.denominator()
               : 4;
}

int ScoreDocument::caretKeyFifths() const {
    return document_ && caret_.measure < document_->song().measureCount()
               ? document_->song().measureHeader(caret_.measure).keySignature.fifths()
               : 0;
}

bool ScoreDocument::caretKeyMinor() const {
    return document_ && caret_.measure < document_->song().measureCount() &&
           document_->song().measureHeader(caret_.measure).keySignature.mode() ==
               core::KeyMode::Minor;
}

void ScoreDocument::insertMeasure(bool after) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    const std::size_t at = caret_.measure + (after ? 1 : 0);
    caret_.measure = at;
    caret_.beat = 0;
    pendingDigit_ = -1;
    execute(std::make_unique<core::InsertMeasures>(at, 1));
}

void ScoreDocument::removeMeasure() {
    if (!document_ || document_->song().measureCount() < 2) {
        return;
    }
    pendingDigit_ = -1;
    execute(std::make_unique<core::RemoveMeasures>(caret_.measure, 1));
}

void ScoreDocument::setMeasureCount(int count) {
    if (!document_ || count < 1) {
        return;
    }
    const auto current = static_cast<int>(document_->song().measureCount());
    if (count > current) {
        execute(std::make_unique<core::InsertMeasures>(static_cast<std::size_t>(current),
                                                       static_cast<std::size_t>(count - current)));
    } else if (count < current) {
        execute(std::make_unique<core::RemoveMeasures>(static_cast<std::size_t>(count),
                                                       static_cast<std::size_t>(current - count)));
    }
}

void ScoreDocument::addTrack(const QString& presetId) {
    const auto* preset = core::findInstrumentPreset(presetId.toStdString());
    if (!document_ || preset == nullptr) {
        return;
    }
    core::Track track = core::Track::fromPreset(*preset);
    track.setName(translated(kPresetNames, preset->id, preset->name).toStdString());
    const std::size_t at = document_->song().trackCount();
    execute(std::make_unique<core::InsertTrack>(at, std::move(track)));
    setTrack(static_cast<int>(at));
}

void ScoreDocument::removeTrack(int index) {
    if (!document_ || index < 0 || document_->song().trackCount() < 2 ||
        index >= static_cast<int>(document_->song().trackCount())) {
        return;
    }
    execute(std::make_unique<core::RemoveTrack>(static_cast<std::size_t>(index)));
}

void ScoreDocument::undo() {
    if (canUndo()) {
        document_->undo();
        edited();
    }
}

void ScoreDocument::redo() {
    if (canRedo()) {
        document_->redo();
        edited();
    }
}

bool ScoreDocument::save(const QUrl& file) {
    if (!document_) {
        return false;
    }
    QString local = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!local.endsWith(QStringLiteral(".elara"), Qt::CaseInsensitive)) {
        local += QStringLiteral(".elara");
    }
    try {
        io::saveNative(document_->song(), std::filesystem::path(local.toStdU16String()));
    } catch (const std::exception& error) {
        emit editFailed(tr("Could not save %1: %2").arg(local, qstring(error.what())));
        return false;
    }
    document_->markSaved();
    const QUrl saved = QUrl::fromLocalFile(local);
    if (saved != source_) {
        source_ = saved;
        emit sourceChanged();
        emit songChanged(); // an untitled song is named after its file
    }
    emit historyChanged();
    return true;
}

bool ScoreDocument::execute(std::unique_ptr<core::Command> command) {
    if (!document_ || !command) {
        return false;
    }
    try {
        document_->execute(std::move(command));
    } catch (const std::exception& error) {
        emit editFailed(qstring(error.what()));
        return false;
    }
    edited();
    return true;
}

// After an edit, undo or redo: show the new song, keeping the shown track when
// it is still there.
void ScoreDocument::edited() {
    ++edits_;
    // Playback hears the edit once its layout is done (relayout()); until then
    // it plays on as before, with the new mix.
    song_ = std::make_shared<const core::Song>(document_->song());
    const auto tracks = static_cast<int>(song_->trackCount());
    if (track_ >= tracks) {
        track_ = std::max(0, tracks - 1);
        emit trackChanged();
    }
    clampCaret();
    emit caretChanged();
    emit songChanged();
    emit historyChanged();
    relayout();
}

void ScoreDocument::setTrackMix(int track, double volume, double pan, bool mute, bool solo) {
    if (!document_ || track < 0 ||
        static_cast<std::size_t>(track) >= document_->song().trackCount() ||
        !std::isfinite(volume) || !std::isfinite(pan)) {
        return;
    }
    const auto index = static_cast<std::size_t>(track);
    const core::TrackMix mix{.volume = std::clamp(volume, 0.0, 1.0),
                             .pan = std::clamp(pan, -1.0, 1.0),
                             .mute = mute,
                             .solo = solo};
    if (mix == document_->song().track(index).mix()) {
        return;
    }
    execute(std::make_unique<core::SetTrackMix>(index, mix));
}

void ScoreDocument::setTrackTuning(int track, const QString& tuning, int capo, bool keepFrets) {
    if (!document_ || track < 0 ||
        static_cast<std::size_t>(track) >= document_->song().trackCount()) {
        return;
    }
    const auto index = static_cast<std::size_t>(track);
    if (!document_->song().track(index).staff(0).tuning()) {
        emit editFailed(tr("Only a track with strings has a tuning."));
        return;
    }
    const std::string text = tuning.simplified().toStdString();
    std::optional<core::Tuning> chosen = core::Tuning::preset(text);
    if (!chosen) {
        chosen = core::Tuning::parse(text);
    }
    if (!chosen) {
        emit editFailed(tr("%1 is not a tuning: give open strings from the lowest, such as "
                           "D2 A2 D3 G3 B3 E4.")
                            .arg(tuning));
        return;
    }
    const core::Retuning what{.track = index,
                              .staff = 0,
                              .tuning = *chosen,
                              .capo = std::clamp(capo, 0, core::Tuning::kMaxFret),
                              .keep = keepFrets ? core::Retune::Frets : core::Retune::Pitches};
    std::unique_ptr<core::Command> command;
    try {
        command = core::makeRetune(document_->song(), what);
    } catch (const std::invalid_argument&) {
        const QString name = trackNames().value(track);
        emit editFailed(keepFrets
                            ? tr("Some frets of %1 do not fit that tuning and capo.").arg(name)
                            : tr("That tuning cannot play every note of %1.").arg(name));
        return;
    }
    if (command) {
        execute(std::move(command));
    }
}

void ScoreDocument::previewTrackMix(int track, double volume, double pan) {
    if (!song_ || track < 0 || static_cast<std::size_t>(track) >= song_->trackCount() ||
        !std::isfinite(volume) || !std::isfinite(pan)) {
        return;
    }
    previewMixes_.clear();
    for (std::size_t t = 0; t < song_->trackCount(); ++t) {
        previewMixes_.push_back(song_->track(t).mix());
    }
    core::TrackMix& mix = previewMixes_[static_cast<std::size_t>(track)];
    mix.volume = std::clamp(volume, 0.0, 1.0);
    mix.pan = std::clamp(pan, -1.0, 1.0);
    emit mixPreviewed();
}

void ScoreDocument::fail(const QString& message) {
    status_ = Status::Error;
    errorMessage_ = message;
    emit statusChanged();
}

void ScoreDocument::setPending(int delta) {
    const bool wasBusy = busy();
    pending_ += delta;
    if (busy() != wasBusy) {
        emit busyChanged();
    }
}

// ---- Keyboard entry ---------------------------------------------------------

namespace {

// The note values the input duration takes, longest first.
constexpr std::array kInputValues{core::NoteValue::Whole,      core::NoteValue::Half,
                                  core::NoteValue::Quarter,    core::NoteValue::Eighth,
                                  core::NoteValue::Sixteenth,  core::NoteValue::ThirtySecond,
                                  core::NoteValue::SixtyFourth};

// The tuplets the input offers (ADR-0050): actual notes in the time of normal
// ones.
struct InputTuplet {
    int actual;
    int normal;
};
constexpr std::array kInputTuplets{InputTuplet{2, 3}, InputTuplet{3, 2}, InputTuplet{4, 3},
                                   InputTuplet{5, 4}, InputTuplet{6, 4}, InputTuplet{7, 4},
                                   InputTuplet{9, 8}};

// 1 for a whole note, 2 for a half note... 64.
int denominatorOf(core::NoteValue value) {
    const auto steps = static_cast<int>(value) - static_cast<int>(core::NoteValue::Whole);
    return steps < 0 ? 0 : 1 << steps;
}

std::optional<std::size_t> inputIndex(core::NoteValue value) {
    const auto it = std::ranges::find(kInputValues, value);
    if (it == kInputValues.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - kInputValues.begin());
}

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

core::CaretMove coreMove(ScoreDocument::CaretMove move) {
    switch (move) {
    case ScoreDocument::CaretMove::NextBeat:
        return core::CaretMove::NextBeat;
    case ScoreDocument::CaretMove::PreviousBeat:
        return core::CaretMove::PreviousBeat;
    case ScoreDocument::CaretMove::NextMeasure:
        return core::CaretMove::NextMeasure;
    case ScoreDocument::CaretMove::PreviousMeasure:
        return core::CaretMove::PreviousMeasure;
    case ScoreDocument::CaretMove::StringUp:
        return core::CaretMove::StringUp;
    case ScoreDocument::CaretMove::StringDown:
        break;
    }
    return core::CaretMove::StringDown;
}

} // namespace

QString ScoreDocument::caretStatus() const {
    if (!document_ || !hasCaret(document_->song())) {
        return {};
    }
    const core::Song& song = document_->song();
    QStringList parts{tr("Measure %1").arg(caret_.measure + 1)};
    parts.append(core::onFreeSlot(song, caret_) ? tr("New beat")
                                                : tr("Beat %1").arg(caret_.beat + 1));
    if (caretFretted()) {
        parts.append(tr("String %1").arg(caret_.string));
        if (const auto fret = fretAtCaret()) {
            parts.append(tr("Fret %1").arg(*fret));
        }
    }
    QStringList names;
    for (const core::Pitch& pitch : pitchesAtCaret()) {
        names.append(pitchName(pitch));
    }
    if (!names.isEmpty() && caretPitched()) {
        parts.append(names.join(QLatin1Char(' ')));
    }
    return parts.join(QStringLiteral(" · "));
}

bool ScoreDocument::caretFretted() const {
    if (!document_ || caret_.track >= document_->song().trackCount()) {
        return false;
    }
    const core::Track& track = document_->song().track(caret_.track);
    return caret_.staff < track.staffCount() && track.staff(caret_.staff).isFretted();
}

int ScoreDocument::inputValue() const {
    return denominatorOf(input_.value());
}

std::optional<int> ScoreDocument::fretAtCaret() const {
    if (!document_ || !hasCaret(document_->song()) || core::onFreeSlot(document_->song(), caret_)) {
        return std::nullopt;
    }
    const core::Beat& beat = document_->song()
                                 .track(caret_.track)
                                 .staff(caret_.staff)
                                 .measure(caret_.measure)
                                 .voices.at(caret_.voice)
                                 .beats.at(caret_.beat);
    for (const core::Note& note : beat.notes) {
        if (note.position && note.position->string == caret_.string) {
            return note.position->fret;
        }
    }
    return std::nullopt;
}

std::vector<core::Pitch> ScoreDocument::pitchesAtCaret() const {
    std::vector<core::Pitch> pitches;
    if (!document_ || !hasCaret(document_->song()) || core::onFreeSlot(document_->song(), caret_)) {
        return pitches;
    }
    const bool fretted = caretFretted();
    const core::Beat& beat = document_->song()
                                 .track(caret_.track)
                                 .staff(caret_.staff)
                                 .measure(caret_.measure)
                                 .voices.at(caret_.voice)
                                 .beats.at(caret_.beat);
    for (const core::Note& note : beat.notes) {
        if (!fretted || (note.position && note.position->string == caret_.string)) {
            pitches.push_back(note.pitch);
        }
    }
    std::ranges::sort(pitches, {}, &core::Pitch::midiNumber);
    return pitches;
}

bool ScoreDocument::caretPitched() const {
    if (!document_ || caret_.track >= document_->song().trackCount()) {
        return false;
    }
    const core::Track& track = document_->song().track(caret_.track);
    return track.instrument().kind != core::InstrumentKind::Percussion &&
           caret_.staff < track.staffCount() &&
           track.staff(caret_.staff).clef() != core::Clef::Percussion;
}

void ScoreDocument::moveCaret(CaretMove move) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    pendingDigit_ = -1;
    caret_ = core::moveCaret(document_->song(), caret_, coreMove(move));
    followBeat();
    placeCaretBox();
}

bool ScoreDocument::placeCaret(int index, double x, double y) {
    const notation::EngravingFont* font = musicMetrics();
    if (!document_ || !hasCaret(document_->song()) || !pages_ || !pagesSong_ || font == nullptr ||
        index < 0 || index >= static_cast<int>(pages_->size())) {
        return false;
    }
    const auto picked = notation::caretAt((*pages_)[static_cast<std::size_t>(index)], *pagesSong_,
                                          {x, y}, caret_, *font);
    if (!picked) {
        return false;
    }
    pendingDigit_ = -1;
    caret_ = core::clampCaret(document_->song(), *picked);
    followBeat();
    placeCaretBox();
    return true;
}

void ScoreDocument::typeDigit(int digit) {
    if (!document_ || !hasCaret(document_->song()) || digit < 0 || digit > 9) {
        return;
    }
    if (!caretFretted()) {
        emit editFailed(tr("Frets are typed on a track with strings."));
        return;
    }
    const bool second = pendingDigit_ > 0 && digitCaret_ == caret_ && digitEdit_ == edits_ &&
                        digitClock_.isValid() && digitClock_.elapsed() <= kTwoDigitMilliseconds;
    if (second) {
        const int fret = pendingDigit_ * 10 + digit;
        pendingDigit_ = -1;
        if (digitChanged_) {
            document_->undo(); // the first digit's note, so both are one undo step
        }
        try {
            core::Entry entry = core::enterFret(document_->song(), caret_, fret, input_);
            if (!enter(std::move(entry)) && digitChanged_) {
                edited(); // nothing to change: the song is back as before the first digit
            }
            return;
        } catch (const std::exception&) {
            // Off the neck: keep the first digit, and the second starts a fret.
            if (digitChanged_) {
                document_->redo();
            }
        }
    }
    try {
        core::Entry entry = core::enterFret(document_->song(), caret_, digit, input_);
        digitChanged_ = enter(std::move(entry));
    } catch (const std::exception& error) {
        emit editFailed(qstring(error.what()));
        return;
    }
    pendingDigit_ = digit;
    digitCaret_ = caret_;
    digitEdit_ = edits_;
    digitClock_.restart();
}

void ScoreDocument::setInputValue(int value) {
    const auto it = std::ranges::find_if(kInputValues, [value](core::NoteValue candidate) {
        return denominatorOf(candidate) == value;
    });
    if (it == kInputValues.end()) {
        return;
    }
    setInput(core::Duration(*it, input_.dots(), input_.tuplet()));
    runEntry(
        [this](const core::Song& song) { return core::setBeatDuration(song, caret_, input_); });
}

void ScoreDocument::toggleDot() {
    setInput(core::Duration(input_.value(), input_.dots() > 0 ? 0 : 1, input_.tuplet()));
    runEntry(
        [this](const core::Song& song) { return core::setBeatDuration(song, caret_, input_); });
}

void ScoreDocument::setInputTuplet(int actual) {
    core::Tuplet tuplet;
    if (actual != 0 && actual != inputTuplet()) {
        const auto found = std::ranges::find(kInputTuplets, actual, &InputTuplet::actual);
        if (found == kInputTuplets.end()) {
            return;
        }
        tuplet = core::Tuplet(found->actual, found->normal);
    }
    setInput(core::Duration(input_.value(), input_.dots(), tuplet));
    runEntry(
        [this](const core::Song& song) { return core::setBeatDuration(song, caret_, input_); });
}

void ScoreDocument::longerDuration() {
    const auto index = inputIndex(input_.value());
    if (index && *index > 0) {
        setInputValue(denominatorOf(kInputValues.at(*index - 1)));
    }
}

void ScoreDocument::shorterDuration() {
    const auto index = inputIndex(input_.value());
    if (index && *index + 1 < kInputValues.size()) {
        setInputValue(denominatorOf(kInputValues.at(*index + 1)));
    }
}

void ScoreDocument::deleteAtCaret() {
    runEntry([this](const core::Song& song) { return core::deleteAtCaret(song, caret_); });
}

void ScoreDocument::deleteBeat() {
    runEntry([this](const core::Song& song) { return core::deleteBeat(song, caret_); });
}

void ScoreDocument::insertRest() {
    runEntry([this](const core::Song& song) { return core::insertRest(song, caret_, input_); });
}

void ScoreDocument::typeNoteName(int step, bool addToChord) {
    if (!document_ || !hasCaret(document_->song()) || step < 0 || step > 6) {
        return;
    }
    if (!caretPitched()) {
        emit editFailed(tr("Drums take no note names."));
        return;
    }
    pendingDigit_ = -1;
    try {
        enter(
            core::enterPitch(document_->song(), caret_, static_cast<core::Step>(step), input_,
                             addToChord ? core::PitchMode::AddToChord : core::PitchMode::Replace));
    } catch (const std::out_of_range&) {
        emit editFailed(caretFretted() ? tr("No string can play that note.")
                                       : tr("That note is out of range."));
    }
}

void ScoreDocument::shiftPitch(int semitones) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    pendingDigit_ = -1;
    try {
        enter(core::shiftPitch(document_->song(), caret_, semitones));
    } catch (const std::out_of_range&) {
        emit editFailed(caretFretted() ? tr("The note would leave the neck.")
                                       : tr("That note is out of range."));
    }
}

bool ScoreDocument::enter(core::Entry entry) {
    caret_ = entry.caret;
    if (entry.command) {
        if (execute(std::move(entry.command))) {
            return true; // the new layout places the caret
        }
    }
    clampCaret();
    placeCaretBox();
    return false;
}

void ScoreDocument::runEntry(const std::function<core::Entry(const core::Song&)>& make) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    pendingDigit_ = -1;
    try {
        enter(make(document_->song()));
    } catch (const std::exception& error) {
        emit editFailed(qstring(error.what()));
    }
}

void ScoreDocument::followBeat() {
    if (!document_ || core::onFreeSlot(document_->song(), caret_)) {
        return;
    }
    const core::Duration duration = document_->song()
                                        .track(caret_.track)
                                        .staff(caret_.staff)
                                        .measure(caret_.measure)
                                        .voices.at(caret_.voice)
                                        .beats.at(caret_.beat)
                                        .duration;
    const core::Tuplet tuplet = duration.tuplet();
    const bool offered =
        tuplet.isNone() || std::ranges::any_of(kInputTuplets, [&tuplet](const InputTuplet& item) {
            return item.actual == tuplet.actual() && item.normal == tuplet.normal();
        });
    if (offered && duration.dots() <= 1 && inputIndex(duration.value())) {
        setInput(duration);
    }
}

void ScoreDocument::setInput(core::Duration duration) {
    if (duration != input_) {
        input_ = duration;
        emit inputChanged();
    }
}

void ScoreDocument::clampCaret() {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    if (track_ != kAllTracks) {
        caret_.track = static_cast<std::size_t>(track_);
    }
    caret_ = core::clampCaret(document_->song(), caret_);
}

void ScoreDocument::placeCaretBox() {
    int page = -1;
    QRectF rect;
    const notation::EngravingFont* font = musicMetrics();
    if (pages_ && pagesSong_ && font != nullptr) {
        for (std::size_t i = 0; i < pages_->size(); ++i) {
            if (const auto box = notation::caretBox((*pages_)[i], *pagesSong_, caret_, *font)) {
                page = static_cast<int>(i);
                rect = QRectF(box->x, box->y, box->width, box->height);
                break;
            }
        }
    }
    caretPage_ = page;
    caretRect_ = rect;
    emit caretChanged();
}

} // namespace elara::app
