// SPDX-License-Identifier: AGPL-3.0-or-later
#include "score_document.hpp"

#include <elara/audio/sequence.hpp>
#include <elara/core/clipboard.hpp>
#include <elara/core/commands.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/lyrics.hpp>
#include <elara/core/names.hpp>
#include <elara/core/retune.hpp>
#include <elara/core/transpose.hpp>
#include <elara/io/compression.hpp>
#include <elara/io/midi.hpp>
#include <elara/io/musicxml.hpp>
#include <elara/io/native_format.hpp>
#include <elara/io/open.hpp>
#include <elara/notation/caret.hpp>
#include <elara/notation/cursor.hpp>
#include <elara/notation/font_outlines.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/pdf.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include "score_common.hpp"
#include "when_done.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QSaveFile>
#include <QStandardPaths>
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
#include <optional>
#include <span>
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
    bool continuous = false; ///< the pages are strips of one endless page
    bool hasTablature = false;
    int milliseconds = 0;
    QString error;
    bool opened = false;  ///< a song just read from `source`, to edit from now on
    QStringList warnings; ///< what reading `source` approximated or dropped
};

// An edited song's sequence, or why it could not be built.
struct SequenceResult {
    std::shared_ptr<const audio::Sequence> sequence;
    QString error;
};

namespace {

constexpr double kStaffSpaceMm = 1.75;

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

// The music font's glyph outlines, for PDF documents (ADR-0074): read from the
// app's resources the first time a score is exported.
const notation::FontOutlines* musicOutlines() {
    static const std::optional<notation::FontOutlines> outlines =
        []() -> std::optional<notation::FontOutlines> {
        QFile file(QStringLiteral(":/fonts/Bravura.otf"));
        if (!file.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        const QByteArray otf = file.readAll();
        auto loaded = notation::FontOutlines::fromBytes(
            std::span(reinterpret_cast<const std::uint8_t*>(otf.constData()),
                      static_cast<std::size_t>(otf.size())));
        if (!loaded) {
            return std::nullopt;
        }
        return *std::move(loaded);
    }();
    return outlines ? &*outlines : nullptr;
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
                                                   std::shared_ptr<const audio::Sequence> heard,
                                                   audio::SequenceOptions practice) {
    auto sequence = std::make_shared<const audio::Sequence>(song, practice);
    if (heard && *sequence == *heard) {
        return heard;
    }
    return sequence;
}

// Lays out `track` (or every track) of `song` as `view` on A4 pages or, when
// `continuous`, on one endless page cut into strips as high as A4 pages
// (ADR-0057). Goes through `cache` so that only what changed since its last
// layout is laid out again. Runs on a worker thread and never throws.
ScoreLayout layOut(std::shared_ptr<const core::Song> song, int track, ScoreDocument::View view,
                   bool continuous, notation::PageLayoutCache& cache) {
    ScoreLayout result;
    result.song = song;
    result.track = track;
    result.view = view;
    result.continuous = continuous;
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
        notation::PageLayoutOptions options = pageOptions();
        if (continuous) {
            options.height = 0.0;
        }
        auto pages = cache.layout(song, staves, *font, options);
        if (continuous) {
            pages = notation::cutIntoStrips(pages.front(), pageOptions().height);
        }
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
ScoreLayout openAndLayOut(const QUrl& file, audio::SequenceOptions practice, bool continuous,
                          notation::PageLayoutCache& cache) {
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
        auto sequence = std::make_shared<const audio::Sequence>(*song, practice);
        result = layOut(std::move(song), 0, view, continuous, cache);
        result.source = file;
        result.opened = true;
        result.sequence = std::move(sequence);
        for (const std::string& warning : opened->warnings) {
            result.warnings.append(qstring(warning));
        }
    } catch (const std::exception& error) {
        result.error = qstring(error.what());
    }
    return result;
}

constexpr char kContext[] = "elara::app::ScoreDocument";

// The names of the instrument presets and of the edits, translated in the
// context of this class. Unknown ones are shown as the core writes them.
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
    Translation{"Rename track", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Rename track")},
    Translation{"Change navigation marks",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change navigation marks")},
    Translation{"Change repeats", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change repeats")},
    Translation{"Change barline", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change barline")},
    Translation{"Change section", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change section")},
    Translation{"Change triplet feel",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change triplet feel")},
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
    Translation{"Paste", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Paste")},
    Translation{"Transpose", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Transpose")},
    Translation{"Change effect", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change effect")},
    Translation{"Change bend", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change bend")},
    Translation{"Change beat effects",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change beat effects")},
    Translation{"Change dynamic", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change dynamic")},
    Translation{"Change slide", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change slide")},
    Translation{"Change vibrato", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change vibrato")},
    Translation{"Change accent", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change accent")},
    Translation{"Change trill", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change trill")},
    Translation{"Change ornament",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change ornament")},
    Translation{"Change fingering",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change fingering")},
    Translation{"Change plucking finger",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change plucking finger")},
    Translation{"Change harmonic",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change harmonic")},
    Translation{"Add grace note", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Add grace note")},
    Translation{"Remove grace notes",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Remove grace notes")},
    Translation{"Set chord symbol",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Set chord symbol")},
    Translation{"Remove chord symbol",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Remove chord symbol")},
    Translation{"Set beat text", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Set beat text")},
    Translation{"Remove beat text",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Remove beat text")},
    Translation{"Set lyrics", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Set lyrics")},
    Translation{"Remove lyrics", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Remove lyrics")},
    Translation{"Change clef", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change clef")},
    Translation{"Repeat measure", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Repeat measure")},
    Translation{"Repeat two measures",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Repeat two measures")},
    Translation{"Write out measure repeat",
                QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Write out measure repeat")},
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

ScoreDocument::ScoreDocument(QObject* parent)
    : QObject(parent),
      layouts_(std::make_unique<JobLane<ScoreLayout>>(this, [this] { updateBusy(); })),
      sequences_(std::make_unique<JobLane<SequenceResult>>(this, [this] { updateBusy(); })) {
    // Connected first, so that the list is up to date for whoever else hears it.
    connect(this, &ScoreDocument::songChanged, this, [this] { trackList_.setTracks(tracks()); });
    // The measures played and their times change far less often than the
    // notes do: whoever draws them hears of it only then.
    connect(this, &ScoreDocument::sequenceChanged, this, [this] {
        std::vector<audio::PlayedMeasure> played;
        if (sequence_) {
            played = sequence_->measures();
        }
        if (played == played_) {
            return;
        }
        played_ = std::move(played);
        measureStarts_.clear();
        measureStarts_.reserve(static_cast<qsizetype>(played_.size()));
        for (const audio::PlayedMeasure& measure : played_) {
            measureStarts_.append(measure.start);
        }
        emit measureStartsChanged();
    });
    recoveryTimer_.setSingleShot(true);
    recoveryTimer_.setInterval(kRecoveryMilliseconds);
    connect(&recoveryTimer_, &QTimer::timeout, this, &ScoreDocument::writeRecovery);
    connect(this, &ScoreDocument::historyChanged, this, &ScoreDocument::keepRecovery);
    saveQueue_.setMaxThreadCount(1);
    exportQueue_.setMaxThreadCount(1);
}

ScoreDocument::~ScoreDocument() {
    if (recovery_) {
        recovery_->remove();
        if (!recoveredFrom_.isEmpty()) {
            recovery_->discard(recoveredFrom_);
        }
    }
}

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

QStringList ScoreDocument::credits() const {
    constexpr int kLines = static_cast<int>(core::kEnumCount<core::Credit>);
    QStringList lines;
    lines.reserve(kLines);
    for (int line = 0; line < kLines; ++line) {
        lines.append(song_ ? qstring(core::creditOf(song_->info(), static_cast<core::Credit>(line)))
                           : QString());
    }
    return lines;
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

int ScoreDocument::measureAt(double seconds) const {
    if (!sequence_) {
        return -1;
    }
    const std::size_t played = sequence_->playedMeasureAt(seconds);
    return played < sequence_->measures().size()
               ? static_cast<int>(sequence_->measures()[played].measure)
               : -1;
}

double ScoreDocument::measureStart(int measure) const {
    if (!sequence_ || measure < 0) {
        return -1.0;
    }
    return sequence_->secondsAt(static_cast<std::size_t>(measure), 0.0).value_or(-1.0);
}

QVariantList ScoreDocument::sectionMarks() const {
    QVariantList marks;
    if (!sequence_ || !sequenceSong_) {
        return marks;
    }
    for (std::size_t measure = 0; measure < sequenceSong_->measureCount(); ++measure) {
        const std::string& name = sequenceSong_->measureHeader(measure).section;
        if (name.empty()) {
            continue;
        }
        if (const auto seconds = sequence_->secondsAt(measure, 0.0)) {
            marks.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(name)},
                                     {QStringLiteral("seconds"), *seconds}});
        }
    }
    return marks;
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

void ScoreDocument::setContinuous(bool continuous) {
    if (continuous == continuous_) {
        return;
    }
    continuous_ = continuous;
    emit continuousChanged();
    relayout();
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
            tr("MIDI (*.mid *.midi *.smf *.kar)"),
            tr("Elara (*.elara)"),
            tr("All files (*)")};
}

void ScoreDocument::open(const QUrl& file) {
    load(file, std::nullopt);
}

void ScoreDocument::load(const QUrl& file, std::optional<RecoveryStore::Copy> recovered) {
    status_ = Status::Loading;
    errorMessage_.clear();
    emit statusChanged();
    layouts_->request(
        [file, practice = practice_, continuous = continuous_, cache = layoutCache_] {
            return openAndLayOut(file, practice, continuous, *cache);
        },
        [this, recovered = std::move(recovered)](const ScoreLayout& result) {
            if (!result.error.isEmpty()) {
                fail(result.error);
                return;
            }
            if (!recovered) {
                apply(result);
                return;
            }
            // The song the copy holds, with the file it came from and
            // changes no file holds.
            ScoreLayout layout = result;
            layout.source = recovered->source;
            apply(layout);
            if (source_ != recovered->source) {
                source_ = recovered->source;
                emit sourceChanged();
                emit songChanged();
            }
            document_->markUnsaved();
            recoveredFrom_ = recovered->file;
            emit historyChanged();
            writeRecovery();
        });
}

QString ScoreDocument::recoveryFolder() const {
    return recovery_ ? recovery_->folder() : QString();
}

void ScoreDocument::setRecoveryFolder(const QString& folder) {
    if (folder == recoveryFolder()) {
        return;
    }
    if (recovery_) {
        recovery_->remove();
    }
    recovery_ = folder.isEmpty() ? nullptr : std::make_shared<RecoveryStore>(folder);
    recoverySong_.reset();
    recoveredFrom_.clear();
    emit recoveryFolderChanged();
    keepRecovery();
}

void ScoreDocument::keepRecovery() {
    if (!recovery_) {
        return;
    }
    if (!modified()) {
        recoveryTimer_.stop();
        // A write still running is dropped by the removal.
        if (recoverySong_) {
            recoverySong_.reset();
            recovery_->remove();
        }
        if (!recoveredFrom_.isEmpty()) {
            recovery_->discard(recoveredFrom_); // the recovered song is saved
            recoveredFrom_.clear();
        }
        return;
    }
    // Started by the first change after a write and not again by the next
    // ones, so that a song edited without a pause is still copied.
    if (song_ != recoverySong_ && !recoveryTimer_.isActive()) {
        recoveryTimer_.start();
    }
}

void ScoreDocument::writeRecovery() {
    if (!recovery_ || !song_ || !modified() || song_ == recoverySong_) {
        return;
    }
    if (recoveryWriting_) {
        recoveryTimer_.start(); // once the write running is done
        return;
    }
    recoveryWriting_ = true;
    recoverySong_ = song_;
    whenDone(QtConcurrent::run([store = recovery_, song = song_, source = source_, title = title(),
                                removals = recovery_->removals()] {
                 return store->write(*song, source, title, removals);
             }),
             this, [this, store = recovery_, song = song_](bool written) {
                 recoveryWriting_ = false;
                 if (store != recovery_) {
                     return; // written to a folder no longer used
                 }
                 // A write that failed, not one dropped by a removal, is
                 // tried again.
                 if (!written && recoverySong_ == song) {
                     recoverySong_.reset();
                 }
                 // Unless the copy was removed since: the song was saved.
                 if (written && recoverySong_ == song) {
                     if (!recoveredFrom_.isEmpty()) {
                         recovery_->discard(recoveredFrom_); // this run's copy holds it now
                         recoveredFrom_.clear();
                     }
                     emit recoveryWritten();
                 }
                 // Changes made while it was written.
                 keepRecovery();
             });
}

QVariantList ScoreDocument::recoverable() {
    QVariantList copies;
    if (!recovery_) {
        return copies;
    }
    for (const RecoveryStore::Copy& copy : recovery_->left()) {
        QString title = copy.title;
        if (title.isEmpty()) {
            title = copy.source.isLocalFile()
                        ? QFileInfo(copy.source.toLocalFile()).completeBaseName()
                        : tr("Untitled");
        }
        copies.append(QVariantMap{{QStringLiteral("file"), copy.file},
                                  {QStringLiteral("title"), title},
                                  {QStringLiteral("source"), copy.source},
                                  {QStringLiteral("written"), copy.written}});
    }
    return copies;
}

void ScoreDocument::recover(const QString& file) {
    if (!recovery_) {
        return;
    }
    for (RecoveryStore::Copy& copy : recovery_->left()) {
        if (copy.file == file) {
            load(QUrl::fromLocalFile(file), std::move(copy));
            return;
        }
    }
}

void ScoreDocument::discardRecovery(const QString& file) {
    if (recovery_) {
        recovery_->discard(file);
    }
}

void ScoreDocument::relayout() {
    if (!song_) {
        return;
    }
    layouts_->request(
        [song = song_, track = track_, view = view_, continuous = continuous_,
         cache = layoutCache_] { return layOut(song, track, view, continuous, *cache); },
        [this](const ScoreLayout& result) {
            if (!result.error.isEmpty()) {
                fail(result.error);
                return;
            }
            apply(result);
        });
}

// Builds the edited song's sequence off the interface thread: copying the song
// is all an edit costs it. Until it arrives, playback plays the previous one
// with the new mix (ADR-0031).
void ScoreDocument::resequence() {
    sequencing_ = song_;
    sequences_->request(
        [song = song_, heard = sequence_, practice = practice_] {
            SequenceResult result;
            try {
                result.sequence = sequenceFor(*song, heard, practice);
            } catch (const std::exception& error) {
                result.error = qstring(error.what());
            }
            return result;
        },
        [this](const SequenceResult& result) {
            sequencing_.reset();
            if (!result.error.isEmpty()) {
                fail(result.error);
                return;
            }
            // The song and its sequence change together, so whoever hears
            // sequenceChanged sees them as a pair. The song is the one edited
            // now: the lane delivers only the newest request, and the edits
            // made since changed no sound (edited()).
            sequenceSong_ = song_;
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
        sequences_->drop();
        sequencing_.reset();
        document_.emplace(*layout.song);
        ++documentsMade_;
        startSession(static_cast<std::size_t>(std::max(0, layout.track)));
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
    // The speed or the metronome changed while the song was opening.
    const bool practiceMoved = sequence_ && (sequence_->speed() != practice_.speed ||
                                             sequence_->metronome() != practice_.metronome);
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
    showsStrips_ = layout.continuous;
    pageTops_.clear();
    pageTops_.reserve(static_cast<qsizetype>(pages_->size()) + 1);
    double top = 0.0;
    pageTops_.append(top);
    for (const notation::DisplayList& page : *pages_) {
        top += page.bottom - page.top;
        pageTops_.append(top);
    }
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
    if (practiceMoved && sequencing_ != song_) {
        resequence();
    }
    // What the file opened lost goes with it: the status bar tells it for as
    // long as the song is open.
    if (layout.opened) {
        setImportWarnings(layout.warnings);
    }
}

void ScoreDocument::setImportWarnings(const QStringList& warnings) {
    if (warnings != importWarnings_) {
        importWarnings_ = warnings;
        emit importWarningsChanged();
    }
}

// ---- Practice -----------------------------------------------------------------

int ScoreDocument::speed() const noexcept {
    return static_cast<int>(std::lround(practice_.speed * 100.0));
}

void ScoreDocument::setSpeed(int percent) {
    const double clamped = std::clamp(percent, static_cast<int>(audio::kMinSpeed * 100),
                                      static_cast<int>(audio::kMaxSpeed * 100)) /
                           100.0;
    if (clamped == practice_.speed) {
        return;
    }
    practice_.speed = clamped;
    emit practiceChanged();
    if (song_) {
        resequence();
    }
}

void ScoreDocument::setMetronome(bool on) {
    if (on == practice_.metronome) {
        return;
    }
    practice_.metronome = on;
    emit practiceChanged();
    if (song_) {
        resequence();
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
        list.append(QVariantMap{
            {QStringLiteral("id"), qstring(preset.id)},
            {QStringLiteral("name"), translated(kContext, kPresetNames, preset.id, preset.name)},
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
            {QStringLiteral("name"), translated(kContext, kTuningNames, id, id)},
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
    return canUndo()
               ? translated(kContext, kEditNames, document_->undoName(), document_->undoName())
               : QString();
}

QString ScoreDocument::redoName() const {
    return canRedo()
               ? translated(kContext, kEditNames, document_->redoName(), document_->redoName())
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
    track.setName(translated(kContext, kPresetNames, preset->id, preset->name).toStdString());
    song.addTrack(std::move(track));
    song.appendMeasures(8);
    layouts_->drop();   // a file still opening must not replace the new song
    sequences_->drop(); // nor an edit's sequence still being built
    sequencing_.reset();
    document_.emplace(std::move(song));
    ++documentsMade_;
    song_ = std::make_shared<const core::Song>(document_->song());
    sequence_ = std::make_shared<const audio::Sequence>(*song_, practice_);
    sequenceSong_ = song_;
    source_.clear();
    setImportWarnings({});
    track_ = 0;
    startSession(0);
    clampCaret(); // the session hears which track is shown
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

void ScoreDocument::setCredit(int line, const QString& text) {
    const std::optional<core::Credit> credit = core::enumFromIndex<core::Credit>(line);
    if (!document_ || !credit) {
        return;
    }
    if (auto change = core::changeCredit(document_->song(), *credit, text.toStdString())) {
        execute(std::move(change));
    }
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

QVariantMap ScoreDocument::navigationAt(int measure) const {
    QVariantMap marks;
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return marks;
    }
    const core::NavigationMarks now =
        core::navigationOf(document_->song().measureHeader(static_cast<std::size_t>(measure)));
    for (const core::NavigationSign sign : core::enumValues<core::NavigationSign>()) {
        marks.insert(qstring(core::signField(sign)), now.has(sign));
    }
    marks.insert(QStringLiteral("jump"), static_cast<int>(now.jump));
    marks.insert(QStringLiteral("jumpThen"), static_cast<int>(now.jumpThen));
    return marks;
}

void ScoreDocument::setNavigation(int measure, const QVariantMap& marks) {
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return;
    }
    const auto m = static_cast<std::size_t>(measure);
    core::NavigationMarks wanted = core::navigationOf(document_->song().measureHeader(m));
    for (const core::NavigationSign sign : core::enumValues<core::NavigationSign>()) {
        if (const auto found = marks.constFind(qstring(core::signField(sign)));
            found != marks.cend()) {
            wanted.set(sign, found->toBool());
        }
    }
    const auto choice = [&marks]<typename Enum>(const QString& key, Enum current) {
        const int value = marks.value(key, static_cast<int>(current)).toInt();
        return core::enumFromIndex<Enum>(value).value_or(current);
    };
    wanted.jump = choice(QStringLiteral("jump"), wanted.jump);
    wanted.jumpThen = choice(QStringLiteral("jumpThen"), wanted.jumpThen);
    execute(core::changeNavigation(document_->song(), m, wanted));
}

QVariantMap ScoreDocument::repeatsAt(int measure) const {
    QVariantMap repeats;
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return repeats;
    }
    const core::MeasureHeader& header =
        document_->song().measureHeader(static_cast<std::size_t>(measure));
    const core::RepeatMarks now = core::repeatsOf(header);
    QVariantList endings;
    for (int time = 1; time <= core::MeasureHeader::kMaxEnding; ++time) {
        if ((now.endings & core::endingBit(time)) != 0) {
            endings.append(time);
        }
    }
    repeats.insert(QStringLiteral("start"), now.start);
    repeats.insert(QStringLiteral("times"), now.times);
    repeats.insert(QStringLiteral("endings"), endings);
    repeats.insert(QStringLiteral("doubleBar"), header.doubleBar);
    return repeats;
}

void ScoreDocument::setRepeats(int measure, const QVariantMap& repeats) {
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return;
    }
    const auto m = static_cast<std::size_t>(measure);
    core::RepeatMarks wanted = core::repeatsOf(document_->song().measureHeader(m));
    const auto start = repeats.constFind(QStringLiteral("start"));
    const auto times = repeats.constFind(QStringLiteral("times"));
    const auto endings = repeats.constFind(QStringLiteral("endings"));
    if (start != repeats.cend()) {
        wanted.start = start->toBool();
    }
    if (times != repeats.cend()) {
        wanted.times = times->toInt();
    }
    if (endings != repeats.cend()) {
        wanted.endings = 0;
        try {
            for (const QVariant& time : endings->toList()) {
                wanted.endings =
                    static_cast<std::uint8_t>(wanted.endings | core::endingBit(time.toInt()));
            }
        } catch (const std::invalid_argument&) {
            emit editFailed(tr("Endings are the times through a repeat, 1 to %1.")
                                .arg(core::MeasureHeader::kMaxEnding));
            return;
        }
    }
    if (start != repeats.cend() || times != repeats.cend() || endings != repeats.cend()) {
        try {
            execute(core::changeRepeats(document_->song(), m, wanted));
        } catch (const std::invalid_argument&) {
            emit editFailed(
                tr("A repeated section plays 2 to 255 times, not %1.").arg(wanted.times));
            return;
        }
    }
    if (const auto found = repeats.constFind(QStringLiteral("doubleBar"));
        found != repeats.cend()) {
        execute(core::changeBarline(document_->song(), m, found->toBool()));
    }
}

QVariantMap ScoreDocument::tempoAt(int measure) const {
    QVariantMap tempo;
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return tempo;
    }
    const core::Song& song = document_->song();
    const auto m = static_cast<std::size_t>(measure);
    const core::Tempo inForce = song.tempoAt(m);
    tempo.insert(QStringLiteral("bpm"), static_cast<int>(std::lround(inForce.bpm())));
    tempo.insert(QStringLiteral("beat"), denominatorOf(inForce.beatUnit().value()));
    tempo.insert(QStringLiteral("dotted"), inForce.beatUnit().dots() > 0);
    tempo.insert(QStringLiteral("own"), std::ranges::any_of(song.measureHeader(m).tempoChanges,
                                                            [](const core::TempoChange& change) {
                                                                return change.offset ==
                                                                       core::Fraction{};
                                                            }));
    tempo.insert(QStringLiteral("gradual"),
                 std::ranges::any_of(song.measureHeader(m).tempoChanges,
                                     [](const core::TempoChange& change) {
                                         return change.offset == core::Fraction{} && change.gradual;
                                     }));
    tempo.insert(QStringLiteral("first"), measure == 0);
    return tempo;
}

void ScoreDocument::setTempoGradual(int measure, bool gradual) {
    if (!document_ || measure <= 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return;
    }
    if (auto change = core::changeTempoGradual(document_->song(), static_cast<std::size_t>(measure),
                                               gradual)) {
        execute(std::move(change));
    }
}

int ScoreDocument::tripletFeelAt(int measure) const {
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return 0;
    }
    return static_cast<int>(
        document_->song().measureHeader(static_cast<std::size_t>(measure)).tripletFeel);
}

void ScoreDocument::setTripletFeelFrom(int measure, int feel) {
    const std::optional<core::TripletFeel> wanted = core::enumFromIndex<core::TripletFeel>(feel);
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount()) || !wanted) {
        return;
    }
    if (auto change = core::changeTripletFeel(document_->song(), static_cast<std::size_t>(measure),
                                              *wanted)) {
        execute(std::move(change));
    }
}

void ScoreDocument::setTempoFrom(int measure, int bpm) {
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return;
    }
    const core::Song& song = document_->song();
    const bool removing = bpm <= 0;
    if (!removing && (bpm < static_cast<int>(core::Tempo::kMinBpm) ||
                      bpm > static_cast<int>(core::Tempo::kMaxBpm))) {
        return;
    }
    if (measure == 0) {
        // The song's tempo, counted in the beat it already counts in; the
        // first measure always has one.
        if (removing) {
            return;
        }
        const core::Tempo tempo(bpm, song.startTempo().beatUnit());
        if (tempo != song.startTempo()) {
            execute(std::make_unique<core::SetStartTempo>(tempo));
        }
        return;
    }
    const auto bpmOrNone = removing ? std::nullopt : std::optional<double>(bpm);
    if (auto change = core::changeTempo(song, static_cast<std::size_t>(measure), bpmOrNone)) {
        execute(std::move(change));
    }
}

QString ScoreDocument::sectionAt(int measure) const {
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return {};
    }
    return QString::fromStdString(
        document_->song().measureHeader(static_cast<std::size_t>(measure)).section);
}

void ScoreDocument::setSection(int measure, const QString& name) {
    if (!document_ || measure < 0 ||
        measure >= static_cast<int>(document_->song().measureCount())) {
        return;
    }
    if (auto change = core::changeSection(document_->song(), static_cast<std::size_t>(measure),
                                          name.trimmed().toStdString())) {
        execute(std::move(change));
    }
}

int ScoreDocument::caretTimeNumerator() const {
    return document_ && session_.caret().measure < document_->song().measureCount()
               ? document_->song().measureHeader(session_.caret().measure).timeSignature.numerator()
               : 4;
}

int ScoreDocument::caretTimeDenominator() const {
    return document_ && session_.caret().measure < document_->song().measureCount()
               ? document_->song()
                     .measureHeader(session_.caret().measure)
                     .timeSignature.denominator()
               : 4;
}

int ScoreDocument::caretKeyFifths() const {
    return document_ && session_.caret().measure < document_->song().measureCount()
               ? document_->song().measureHeader(session_.caret().measure).keySignature.fifths()
               : 0;
}

bool ScoreDocument::caretKeyMinor() const {
    return document_ && session_.caret().measure < document_->song().measureCount() &&
           document_->song().measureHeader(session_.caret().measure).keySignature.mode() ==
               core::KeyMode::Minor;
}

void ScoreDocument::insertMeasure(bool after) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    core::Caret caret = session_.caret();
    caret.measure += after ? 1 : 0;
    caret.beat = 0;
    enter(core::Entry{.command = std::make_unique<core::InsertMeasures>(caret.measure, 1),
                      .caret = caret});
}

void ScoreDocument::removeMeasure() {
    if (!document_ || document_->song().measureCount() < 2) {
        return;
    }
    session_.endTyping();
    execute(std::make_unique<core::RemoveMeasures>(session_.caret().measure, 1));
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
    track.setName(translated(kContext, kPresetNames, preset->id, preset->name).toStdString());
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
        session_.songChanged(document_->song());
        edited();
    }
}

void ScoreDocument::redo() {
    if (canRedo()) {
        document_->redo();
        session_.songChanged(document_->song());
        edited();
    }
}

void ScoreDocument::failLater(void (ScoreDocument::*finished)(const QUrl&, bool),
                              const QString& file, const QString& problem) {
    // After the call that failed returns, as the work's own result comes.
    QMetaObject::invokeMethod(
        this,
        [this, finished, file, problem] {
            emit editFailed(problem);
            emit(this->*finished)(QUrl::fromLocalFile(file), false);
        },
        Qt::QueuedConnection);
}

void ScoreDocument::save(const QUrl& file) {
    QString local = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!local.endsWith(QStringLiteral(".elara"), Qt::CaseInsensitive)) {
        local += QStringLiteral(".elara");
    }
    if (!document_ || !song_) {
        failLater(&ScoreDocument::saveFinished, local,
                  tr("Could not save %1: %2").arg(local, tr("There is no song to save.")));
        return;
    }
    if (saving_++ == 0) {
        emit savingChanged();
    }
    // The song shown is the song edited (edited()), shared with the worker
    // instead of copied.
    whenDone(QtConcurrent::run(&saveQueue_,
                               [song = song_, local] {
                                   try {
                                       io::saveNative(
                                           *song, std::filesystem::path(local.toStdU16String()));
                                   } catch (const std::exception& error) {
                                       return qstring(error.what());
                                   }
                                   return QString();
                               }),
             this,
             [this, local, revision = document_->revision(),
              document = documentsMade_](const QString& error) {
                 if (--saving_ == 0) {
                     emit savingChanged();
                 }
                 const QUrl saved = QUrl::fromLocalFile(local);
                 if (!error.isEmpty()) {
                     emit editFailed(tr("Could not save %1: %2").arg(local, error));
                     emit saveFinished(saved, false);
                     return;
                 }
                 // Unless another song replaced the one saved meanwhile.
                 if (document == documentsMade_) {
                     document_->markSaved(revision);
                     if (saved != source_) {
                         source_ = saved;
                         emit sourceChanged();
                         emit songChanged(); // an untitled song is named after its file
                     }
                     emit historyChanged();
                 }
                 emit saveFinished(saved, true);
             });
}

QUrl ScoreDocument::exportFile(const QString& extension) const {
    const QString suffix = QLatin1Char('.') + extension;
    if (source_.isLocalFile()) {
        const QFileInfo file(source_.toLocalFile());
        return QUrl::fromLocalFile(file.dir().filePath(file.completeBaseName() + suffix));
    }
    QString name = songTitle().trimmed();
    // Characters no file name may hold on any of the three systems.
    for (QChar& c : name) {
        if (QStringLiteral("\\/:*?\"<>|").contains(c) || c.unicode() < 0x20) {
            c = QLatin1Char('-');
        }
    }
    if (name.isEmpty()) {
        name = tr("Untitled");
    }
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QUrl::fromLocalFile(QDir(folder).filePath(name + suffix));
}

namespace {

// `file` as a local path ending in one of `extensions`, or with the first of
// them added.
QString exportPath(const QUrl& file, std::initializer_list<QLatin1StringView> extensions) {
    QString local = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const bool known = std::ranges::any_of(extensions, [&local](QLatin1StringView extension) {
        return local.endsWith(extension, Qt::CaseInsensitive);
    });
    if (!known) {
        local += *extensions.begin();
    }
    return local;
}

} // namespace

bool ScoreDocument::exportMusicXml(const QUrl& file) {
    if (!song_) {
        return false;
    }
    const QString local = exportPath(file, {QLatin1StringView(".musicxml"),
                                            QLatin1StringView(".xml"), QLatin1StringView(".mxl")});
    try {
        const bool compressed = local.endsWith(QStringLiteral(".mxl"), Qt::CaseInsensitive);
        return writeExport(local, io::exportMusicXml(*song_, {.compressed = compressed}),
                           tr("MusicXML"));
    } catch (const std::exception& error) {
        emit editFailed(tr("Could not export %1: %2").arg(local, qstring(error.what())));
        return false;
    }
}

bool ScoreDocument::exportMidi(const QUrl& file) {
    if (!song_) {
        return false;
    }
    const QString local = exportPath(file, {QLatin1StringView(".mid"), QLatin1StringView(".midi")});
    try {
        return writeExport(local, io::exportMidi(*song_), tr("MIDI"));
    } catch (const std::exception& error) {
        emit editFailed(tr("Could not export %1: %2").arg(local, qstring(error.what())));
        return false;
    }
}

// Writes an export whole or not at all, then tells what its format left out.
bool ScoreDocument::writeExport(const QString& local, const io::ExportResult& result,
                                const QString& format) {
    QSaveFile out(local);
    const auto size = static_cast<qint64>(result.data.size());
    if (!out.open(QIODevice::WriteOnly) || out.write(result.data.data(), size) != size ||
        !out.commit()) {
        emit editFailed(tr("Could not export %1: %2").arg(local, out.errorString()));
        return false;
    }
    const QString name = QFileInfo(local).fileName();
    QStringList details;
    details.reserve(static_cast<qsizetype>(result.warnings.size()));
    for (const std::string& warning : result.warnings) {
        details.append(qstring(warning));
    }
    emit report(details.isEmpty()
                    ? tr("Exported %1.").arg(name)
                    : tr("Exported %1. %2 left out %n thing(s).", "", int(details.size()))
                          .arg(name, format),
                details);
    return true;
}

void ScoreDocument::exportPdf(const QUrl& file) {
    QString local = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!local.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        local += QStringLiteral(".pdf");
    }
    if (!song_) {
        failLater(&ScoreDocument::exportFinished, local,
                  tr("Could not export %1: %2").arg(local, tr("There is no song to export.")));
        return;
    }
    const notation::EngravingFont* font = musicMetrics();
    const notation::FontOutlines* outlines = musicOutlines();
    if (font == nullptr || outlines == nullptr) {
        failLater(&ScoreDocument::exportFinished, local,
                  tr("Could not export %1: %2").arg(local, tr("The music font is missing.")));
        return;
    }
    whenDone(
        QtConcurrent::run(&exportQueue_,
                          [song = song_, tracks = shownTracks(*song_, track_),
                           view = trackView(view_), font, outlines, local] {
                              std::string pdf;
                              try {
                                  const auto staves = notation::selectTracks(*song, tracks, view);
                                  const auto pages =
                                      notation::layoutPages(*song, staves, *font, pageOptions());
                                  pdf = notation::toPdf(pages, *outlines,
                                                        {.staffSpace = kStaffSpaceMm * 72.0 / 25.4,
                                                         .title = song->info().title,
                                                         .compress = io::zlibCompress});
                              } catch (const std::exception& error) {
                                  return qstring(error.what());
                              }
                              // Written whole or not at all: a failed export leaves no half
                              // file behind.
                              QSaveFile out(local);
                              const auto size = static_cast<qint64>(pdf.size());
                              if (!out.open(QIODevice::WriteOnly) ||
                                  out.write(pdf.data(), size) != size || !out.commit()) {
                                  return out.errorString();
                              }
                              return QString();
                          }),
        this, [this, local](const QString& error) {
            if (error.isEmpty()) {
                emit notice(tr("Exported %1.").arg(QFileInfo(local).fileName()));
            } else {
                emit editFailed(tr("Could not export %1: %2").arg(local, error));
            }
            emit exportFinished(QUrl::fromLocalFile(local), error.isEmpty());
        });
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
    session_.songChanged(document_->song());
    edited();
    return true;
}

// After an edit, undo or redo: show the new song, keeping the shown track when
// it is still there.
void ScoreDocument::edited() {
    const core::Changes changes = document_->lastChanges();
    // Playback hears the edit once its sequence is built (resequence()); until
    // then it plays on as before, with the new mix.
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
    // Only what the edit can have changed is made again (ADR-0111): the
    // sequence is built beside the layout, on another worker, so the pages
    // show as soon as they are laid out instead of waiting for it.
    if (changes.notation) {
        relayout();
    }
    if (changes.sound) {
        resequence();
    } else if (!sequencing_) {
        sequenceSong_ = song_; // the sequence plays this song as it played the one before
    }
}

void ScoreDocument::setTrackName(int track, const QString& name) {
    if (!document_ || track < 0 ||
        static_cast<std::size_t>(track) >= document_->song().trackCount()) {
        return;
    }
    const auto index = static_cast<std::size_t>(track);
    std::string wanted = name.trimmed().toStdString();
    if (wanted == document_->song().track(index).name()) {
        return;
    }
    execute(std::make_unique<core::SetTrackName>(index, std::move(wanted)));
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

bool ScoreDocument::busy() const {
    return layouts_->busy() || sequences_->busy();
}

LaneWork ScoreDocument::layoutWork() const {
    return layouts_->work();
}

LaneWork ScoreDocument::sequenceWork() const {
    return sequences_->work();
}

void ScoreDocument::updateBusy() {
    if (busy() != busy_) {
        busy_ = busy();
        emit busyChanged();
    }
}

// ---- Keyboard entry ---------------------------------------------------------

namespace {

std::optional<std::size_t> inputIndex(core::NoteValue value) {
    const auto it = std::ranges::find(core::EditSession::kInputValues, value);
    if (it == core::EditSession::kInputValues.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - core::EditSession::kInputValues.begin());
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

int ScoreDocument::inputValue() const {
    return denominatorOf(session_.input().value());
}

void ScoreDocument::moveCaret(CaretMove move) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    const core::Duration input = session_.input();
    session_.move(document_->song(), coreMove(move));
    inputMaybeChanged(input);
    placeCaretBox();
}

void ScoreDocument::setCaretVoice(int voice) {
    if (!document_ || !hasCaret(document_->song()) || voice < 1 ||
        voice > static_cast<int>(core::Measure::kMaxVoices) || voice == caretVoice()) {
        return;
    }
    const core::Duration input = session_.input();
    session_.setVoice(document_->song(), static_cast<std::size_t>(voice - 1));
    inputMaybeChanged(input);
    placeCaretBox();
}

bool ScoreDocument::placeCaret(int index, double x, double y) {
    const notation::EngravingFont* font = musicMetrics();
    if (!document_ || !hasCaret(document_->song()) || !pages_ || !pagesSong_ || font == nullptr ||
        index < 0 || index >= static_cast<int>(pages_->size())) {
        return false;
    }
    const auto picked = notation::caretAt((*pages_)[static_cast<std::size_t>(index)], *pagesSong_,
                                          {x, y}, session_.caret(), *font);
    if (!picked) {
        return false;
    }
    placeCaretAt(*picked);
    return true;
}

void ScoreDocument::typeDigit(int digit) {
    if (!document_ || !hasCaret(document_->song()) || digit < 0 || digit > 9) {
        return;
    }
    if (onDrumStaff(document_->song(), session_.caret())) {
        emit editFailed(tr("On drums, Enter adds or removes the lane's hit."));
        return;
    }
    if (!onStrings(document_->song(), session_.caret())) {
        emit editFailed(tr("Frets are typed on a track with strings."));
        return;
    }
    bool changed = false;
    try {
        changed = session_.typeDigit(*document_, digit, core::EditSession::Clock::now());
    } catch (const std::exception& error) {
        emit editFailed(qstring(error.what()));
        return;
    }
    showEdit(changed);
}

void ScoreDocument::setInputValue(int value) {
    const auto it =
        std::ranges::find_if(core::EditSession::kInputValues, [value](core::NoteValue candidate) {
            return denominatorOf(candidate) == value;
        });
    if (it == core::EditSession::kInputValues.end()) {
        return;
    }
    giveInput(core::Duration(*it, session_.input().dots(), session_.input().tuplet()));
}

void ScoreDocument::toggleDot() {
    const core::Duration input = session_.input();
    giveInput(core::Duration(input.value(), input.dots() > 0 ? 0 : 1, input.tuplet()));
}

void ScoreDocument::setInputTuplet(int actual) {
    core::Tuplet tuplet;
    if (actual != 0 && actual != inputTuplet()) {
        const auto found = std::ranges::find(core::EditSession::kInputTuplets, actual,
                                             &core::EditSession::InputTuplet::actual);
        if (found == core::EditSession::kInputTuplets.end()) {
            return;
        }
        tuplet = core::Tuplet(found->actual, found->normal);
    }
    giveInput(core::Duration(session_.input().value(), session_.input().dots(), tuplet));
}

void ScoreDocument::longerDuration() {
    const auto index = inputIndex(session_.input().value());
    if (index && *index > 0) {
        setInputValue(denominatorOf(core::EditSession::kInputValues.at(*index - 1)));
    }
}

void ScoreDocument::shorterDuration() {
    const auto index = inputIndex(session_.input().value());
    if (index && *index + 1 < core::EditSession::kInputValues.size()) {
        setInputValue(denominatorOf(core::EditSession::kInputValues.at(*index + 1)));
    }
}

bool ScoreDocument::enter(core::Entry entry) {
    bool changed = false;
    try {
        changed = session_.enter(*document_, std::move(entry));
    } catch (const std::exception& error) {
        emit editFailed(qstring(error.what()));
    }
    return showEdit(changed);
}

bool ScoreDocument::showEdit(bool changed) {
    if (changed) {
        edited();
        return true; // the new layout places the caret
    }
    placeCaretBox();
    return false;
}

void ScoreDocument::startSession(std::size_t track) {
    const core::Duration input = session_.input();
    session_.start(track);
    inputMaybeChanged(input);
}

void ScoreDocument::inputMaybeChanged(core::Duration before) {
    if (session_.input() != before) {
        emit inputChanged();
    }
}

void ScoreDocument::giveInput(core::Duration duration) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    const core::Duration before = session_.input();
    bool changed = false;
    try {
        changed = session_.setInput(*document_, duration);
    } catch (const std::exception& error) {
        emit editFailed(qstring(error.what()));
    }
    inputMaybeChanged(before);
    showEdit(changed);
}

void ScoreDocument::placeCaretAt(const core::Caret& caret) {
    const core::Duration input = session_.input();
    session_.place(document_->song(), caret);
    inputMaybeChanged(input);
    placeCaretBox();
}

void ScoreDocument::clampCaret() {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    session_.showTrack(document_->song(),
                       track_ == kAllTracks
                           ? std::nullopt
                           : std::optional<std::size_t>(static_cast<std::size_t>(track_)));
}

void ScoreDocument::placeCaretBox() {
    int page = -1;
    QRectF rect;
    const notation::EngravingFont* font = musicMetrics();
    if (pages_ && pagesSong_ && font != nullptr) {
        for (std::size_t i = 0; i < pages_->size(); ++i) {
            if (const auto box =
                    notation::caretBox((*pages_)[i], *pagesSong_, session_.caret(), *font)) {
                page = static_cast<int>(i);
                rect = QRectF(box->x, box->y, box->width, box->height);
                break;
            }
        }
    }
    caretPage_ = page;
    caretRect_ = rect;
    emit caretChanged();
    selectionMoved();
}

int ScoreDocument::selectionFirst() const {
    return session_.hasSelection() ? static_cast<int>(session_.selection().first) : -1;
}

int ScoreDocument::selectionLast() const {
    return session_.hasSelection() ? static_cast<int>(session_.selection().last()) : -1;
}

QVariantList ScoreDocument::selectionBoxes(int index) const {
    QVariantList boxes;
    if (!session_.hasSelection() || !pages_ || index < 0 ||
        index >= static_cast<int>(pages_->size())) {
        return boxes;
    }
    const core::EditSession::Measures selected = session_.selection();
    for (const notation::MeasureBox& box : (*pages_)[static_cast<std::size_t>(index)].measures) {
        if (box.index >= selected.first && box.index <= selected.last()) {
            boxes.append(QRectF(box.x, box.y, box.width, box.height));
        }
    }
    return boxes;
}

void ScoreDocument::selectionMoved() {
    ++selectionRevision_;
    emit selectionChanged();
}

void ScoreDocument::extendSelection(int measures) {
    if (!document_ || !hasCaret(document_->song()) || measures == 0) {
        return;
    }
    const core::Duration input = session_.input();
    session_.extendSelection(document_->song(), measures);
    inputMaybeChanged(input);
    placeCaretBox();
}

void ScoreDocument::selectAll() {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    const core::Duration input = session_.input();
    session_.selectAll(document_->song());
    inputMaybeChanged(input);
    placeCaretBox();
}

void ScoreDocument::clearSelection() {
    if (!session_.clearSelection()) {
        return;
    }
    emit caretChanged(); // the status line
    selectionMoved();
}

void ScoreDocument::copy() {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    const core::EditSession::Measures copied = session_.copy(document_->song());
    emit clipboardChanged();
    emit notice(copied.count == 1
                    ? tr("Copied measure %1.").arg(copied.first + 1)
                    : tr("Copied measures %1-%2.").arg(copied.first + 1).arg(copied.last() + 1));
}

void ScoreDocument::paste(bool insert) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    if (!session_.canPaste()) {
        emit editFailed(tr("Copy some measures first."));
        return;
    }
    bool changed = false;
    try {
        changed =
            session_.paste(*document_, insert ? core::PasteMode::Insert : core::PasteMode::Replace);
    } catch (const std::out_of_range&) {
        emit editFailed(tr("The song has fewer tracks than were copied."));
        return;
    } catch (const std::invalid_argument&) {
        emit editFailed(tr("Those measures do not fit this track."));
        return;
    } catch (const std::exception& error) {
        emit editFailed(qstring(error.what()));
    }
    showEdit(changed);
}

void ScoreDocument::transposeSelection(int semitones) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    try {
        if (session_.transpose(*document_, semitones)) {
            edited();
        }
    } catch (const std::exception&) {
        emit editFailed(onStrings(document_->song(), session_.caret())
                            ? tr("A note would leave the range or the neck.")
                            : tr("A note would leave the range."));
    }
}

} // namespace elara::app
