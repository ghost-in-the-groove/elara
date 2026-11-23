// SPDX-License-Identifier: AGPL-3.0-or-later
#include "score_document.hpp"

#include <elara/audio/sequence.hpp>
#include <elara/core/commands.hpp>
#include <elara/core/instrument.hpp>
#include <elara/io/native_format.hpp>
#include <elara/io/open.hpp>
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
#include <cstdlib>
#include <exception>
#include <filesystem>
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
    std::shared_ptr<const audio::Sequence> sequence; // only when opened
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
    Translation{"Change time signature",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change time signature")},
    Translation{"Change key signature",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change key signature")},
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
        document_.emplace(*layout.song);
        emit historyChanged();
    }
    if (layout.song != song_) {
        song_ = layout.song;
        sequence_ = layout.sequence ? layout.sequence
                                    : std::make_shared<const audio::Sequence>(*song_);
        if (!layout.source.isEmpty() && layout.source != source_) {
            source_ = layout.source;
            emit sourceChanged();
        }
        emit songChanged();
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
    hasTablature_ = layout.hasTablature;
    layoutMilliseconds_ = layout.milliseconds;
    emit layoutChanged();
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
        list.append(QVariantMap{{QStringLiteral("name"), names[static_cast<qsizetype>(t)]},
                                {QStringLiteral("detail"), trackDetail(track)},
                                {QStringLiteral("symbol"), trackSymbol(track)}});
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
    ++generation_; // a file still opening must not replace the new song
    document_.emplace(std::move(song));
    song_ = std::make_shared<const core::Song>(document_->song());
    sequence_ = std::make_shared<const audio::Sequence>(*song_);
    source_.clear();
    track_ = 0;
    const std::array<std::size_t, 1> first{0};
    view_ = notation::hasTablature(*song_, first) ? View::Both : View::Standard;
    emit sourceChanged();
    emit songChanged();
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
    if (!document_ || document_->song().measureCount() == 0) {
        return;
    }
    try {
        execute(core::changeTimeSignature(document_->song(), 0,
                                          core::TimeSignature(numerator, denominator)));
    } catch (const std::invalid_argument&) {
        emit editFailed(tr("%1/%2 is not a time signature.").arg(numerator).arg(denominator));
    }
}

void ScoreDocument::setKeySignature(int fifths, bool minor) {
    if (!document_ || document_->song().measureCount() == 0 ||
        std::abs(fifths) > core::KeySignature::kMaxFifths) {
        return;
    }
    execute(core::changeKeySignature(
        document_->song(), 0,
        core::KeySignature(fifths, minor ? core::KeyMode::Minor : core::KeyMode::Major)));
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

void ScoreDocument::execute(std::unique_ptr<core::Command> command) {
    if (!document_ || !command) {
        return;
    }
    try {
        document_->execute(std::move(command));
    } catch (const std::exception& error) {
        emit editFailed(qstring(error.what()));
        return;
    }
    edited();
}

// After an edit, undo or redo: show the new song, keeping the shown track when
// it is still there.
void ScoreDocument::edited() {
    song_ = std::make_shared<const core::Song>(document_->song());
    sequence_ = std::make_shared<const audio::Sequence>(*song_); // playback hears the edit
    const auto tracks = static_cast<int>(song_->trackCount());
    if (track_ >= tracks) {
        track_ = std::max(0, tracks - 1);
        emit trackChanged();
    }
    emit songChanged();
    emit historyChanged();
    relayout();
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

} // namespace elara::app
