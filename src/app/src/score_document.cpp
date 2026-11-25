// SPDX-License-Identifier: AGPL-3.0-or-later
#include "score_document.hpp"

#include <elara/audio/sequence.hpp>
#include <elara/core/clipboard.hpp>
#include <elara/core/commands.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/lyrics.hpp>
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
#include <limits>
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

// An edited song's sequence, or why it could not be built.
struct SequenceResult {
    std::shared_ptr<const audio::Sequence> sequence;
    QString error;
};

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
    Translation{"Change barline", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change barline")},
    Translation{"Change section", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Change section")},
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

// The note effects, in core::NoteEffect order, and their keys (ADR-0038):
// letters A to G type note names, so the effects take others.
struct EffectKey {
    const char* name;
    char16_t key;
};
constexpr std::array<EffectKey, ScoreDocument::kEffectCount> kEffects{{
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Tie"), u'T'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Hammer-on or pull-off"), u'H'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Slide"), u'S'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Bend"), u'U'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Vibrato"), u'V'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Palm mute"), u'P'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Dead note"), u'X'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Let ring"), u'L'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Natural harmonic"), u'N'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Ghost note"), u'O'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Accent"), u'>'},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Staccato"), u'\''},
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Tenuto"), u'_'},        // ADR-0088
    {QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Staccatissimo"), u'"'}, // ADR-0094
}};
static_assert(static_cast<int>(core::NoteEffect::Staccatissimo) + 1 == ScoreDocument::kEffectCount);

// The kit pieces of drum entry, by their names in elara_core.
constexpr std::array kDrumPieceNames{
    Translation{"Crash cymbal", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Crash cymbal")},
    Translation{"Closed hi-hat", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Closed hi-hat")},
    Translation{"Open hi-hat", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Open hi-hat")},
    Translation{"Ride cymbal", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Ride cymbal")},
    Translation{"Ride bell", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Ride bell")},
    Translation{"High tom", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "High tom")},
    Translation{"Mid tom", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Mid tom")},
    Translation{"Snare", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Snare")},
    Translation{"Side stick", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Side stick")},
    Translation{"Floor tom", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Floor tom")},
    Translation{"Bass drum", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Bass drum")},
    Translation{"Hi-hat pedal", QT_TRANSLATE_NOOP("elara::app::ScoreDocument", "Hi-hat pedal")},
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

ScoreDocument::ScoreDocument(QObject* parent) : QObject(parent) {
    // Connected first, so that the list is up to date for whoever else hears it.
    connect(this, &ScoreDocument::songChanged, this, [this] { trackList_.setTracks(tracks()); });
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
    constexpr int kLines = static_cast<int>(core::Credit::Notes) + 1;
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
    const std::uint64_t generation = ++generation_;
    status_ = Status::Loading;
    errorMessage_.clear();
    emit statusChanged();
    setPending(+1);
    whenDone(QtConcurrent::run(
                 [file, practice = practice_, continuous = continuous_, cache = layoutCache_] {
                     return openAndLayOut(file, practice, continuous, *cache);
                 }),
             this, [this, generation](const ScoreLayout& result) {
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
    whenDone(QtConcurrent::run(
                 [song = song_, track = track_, view = view_, continuous = continuous_,
                  cache = layoutCache_] { return layOut(song, track, view, continuous, *cache); }),
             this, [this, generation](const ScoreLayout& result) {
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
    whenDone(QtConcurrent::run([song = song_, heard = sequence_, practice = practice_] {
                 SequenceResult result;
                 try {
                     result.sequence = sequenceFor(*song, heard, practice);
                 } catch (const std::exception& error) {
                     result.error = qstring(error.what());
                 }
                 return result;
             }),
             this, [this, generation, song = song_](const SequenceResult& result) {
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
    // What the file opened lost goes with it, and is told once.
    if (layout.opened) {
        setImportWarnings(layout.warnings);
        if (!layout.warnings.isEmpty()) {
            emit report(tr("Opened %1. %n thing(s) changed or left out.", "",
                           static_cast<int>(layout.warnings.size()))
                            .arg(QFileInfo(layout.source.toLocalFile()).fileName()),
                        layout.warnings);
        }
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
    sequence_ = std::make_shared<const audio::Sequence>(*song_, practice_);
    sequenceSong_ = song_;
    source_.clear();
    setImportWarnings({});
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

void ScoreDocument::setCredit(int line, const QString& text) {
    if (!document_ || line < 0 || line > static_cast<int>(core::Credit::Notes)) {
        return;
    }
    if (auto change = core::changeCredit(document_->song(), static_cast<core::Credit>(line),
                                         text.toStdString())) {
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
    repeats.insert(QStringLiteral("doubleBar"), header.doubleBar);
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
    if (const auto found = repeats.constFind(QStringLiteral("doubleBar"));
        found != repeats.cend()) {
        header.doubleBar = found->toBool();
    }
    if (header != document_->song().measureHeader(m)) {
        execute(std::make_unique<core::SetMeasureHeader>(m, std::move(header)));
    }
}

namespace {
int denominatorOf(core::NoteValue value);
} // namespace

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

bool ScoreDocument::exportPdf(const QUrl& file) {
    if (!song_) {
        return false;
    }
    QString local = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!local.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        local += QStringLiteral(".pdf");
    }
    const notation::EngravingFont* font = musicMetrics();
    const notation::FontOutlines* outlines = musicOutlines();
    if (font == nullptr || outlines == nullptr) {
        emit editFailed(tr("Could not export %1: %2").arg(local, tr("The music font is missing.")));
        return false;
    }
    std::string pdf;
    try {
        const auto staves =
            notation::selectTracks(*song_, shownTracks(*song_, track_), trackView(view_));
        const auto pages = notation::layoutPages(*song_, staves, *font, pageOptions());
        pdf = notation::toPdf(pages, *outlines,
                              {.staffSpace = kStaffSpaceMm * 72.0 / 25.4,
                               .title = song_->info().title,
                               .compress = io::zlibCompress});
    } catch (const std::exception& error) {
        emit editFailed(tr("Could not export %1: %2").arg(local, qstring(error.what())));
        return false;
    }
    // Written whole or not at all: a failed export leaves no half file behind.
    QSaveFile out(local);
    const auto size = static_cast<qint64>(pdf.size());
    if (!out.open(QIODevice::WriteOnly) || out.write(pdf.data(), size) != size || !out.commit()) {
        emit editFailed(tr("Could not export %1: %2").arg(local, out.errorString()));
        return false;
    }
    emit notice(tr("Exported %1.").arg(QFileInfo(local).fileName()));
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
    if (anchor_ && *anchor_ >= song_->measureCount()) {
        anchor_ = song_->measureCount() - 1;
    }
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
    QStringList parts;
    if (anchor_) {
        const int first = selectionFirst() + 1;
        const int last = selectionLast() + 1;
        parts.append(first == last ? tr("Measure %1 selected").arg(first)
                                   : tr("Measures %1-%2 selected").arg(first).arg(last));
    }
    parts.append(tr("Measure %1").arg(caret_.measure + 1));
    if (caret_.voice > 0) {
        parts.append(tr("Voice %1").arg(caret_.voice + 1));
    }
    parts.append(core::onFreeSlot(song, caret_) ? tr("New beat")
                                                : tr("Beat %1").arg(caret_.beat + 1));
    if (const QString chord = caretChord(); !chord.isEmpty()) {
        parts.append(tr("Chord %1").arg(chord)); // the beat's, before its notes
    }
    if (const QString text = caretText(); !text.isEmpty()) {
        parts.append(tr("\u201C%1\u201D").arg(text));
    }
    if (caretFretted()) {
        parts.append(tr("String %1").arg(caret_.string));
        if (const auto fret = fretAtCaret()) {
            parts.append(tr("Fret %1").arg(*fret));
        }
    }
    if (caretOnDrums()) {
        parts.append(laneName(caret_.string));
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
    if (const int ottava = caretOttava(); ottava != 0) {
        static constexpr std::array<const char*, 5> kOttavaNames{"", "8va", "8vb", "15ma", "15mb"};
        parts.append(
            tr("Octave line %1")
                .arg(QLatin1StringView(
                    kOttavaNames[static_cast<std::size_t>(ottava) % kOttavaNames.size()])));
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

bool ScoreDocument::caretOnDrums() const {
    if (!document_ || caret_.track >= document_->song().trackCount()) {
        return false;
    }
    const core::Track& track = document_->song().track(caret_.track);
    return caret_.staff < track.staffCount() &&
           track.staff(caret_.staff).clef() == core::Clef::Percussion;
}

QString ScoreDocument::laneName(int lane) {
    const auto pieces = core::drumPieces();
    if (lane < 1 || lane > static_cast<int>(pieces.size())) {
        return {};
    }
    const std::string_view name = pieces[static_cast<std::size_t>(lane - 1)].name;
    return translated(kDrumPieceNames, name, name);
}

void ScoreDocument::moveCaret(CaretMove move) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    clearSelection();
    pendingDigit_ = -1;
    caret_ = core::moveCaret(document_->song(), caret_, coreMove(move));
    followBeat();
    placeCaretBox();
}

void ScoreDocument::setCaretVoice(int voice) {
    if (!document_ || !hasCaret(document_->song()) || voice < 1 ||
        voice > static_cast<int>(core::Measure::kMaxVoices) || voice == caretVoice()) {
        return;
    }
    pendingDigit_ = -1;
    caret_.voice = static_cast<std::size_t>(voice - 1);
    caret_ = core::clampCaret(document_->song(), caret_);
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
    clearSelection();
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
    if (caretOnDrums()) {
        emit editFailed(tr("On drums, Enter adds or removes the lane's hit."));
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

void ScoreDocument::toggleDrum() {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    if (!caretOnDrums()) {
        emit editFailed(tr("Hits are typed on a drum track."));
        return;
    }
    runEntry([this](const core::Song& song) { return core::toggleDrum(song, caret_, input_); });
}

void ScoreDocument::toggleEffect(int effect) {
    if (!document_ || !hasCaret(document_->song()) || effect < 0 || effect >= kEffectCount) {
        return;
    }
    pendingDigit_ = -1;
    const auto which = static_cast<core::NoteEffect>(effect);
    try {
        core::Entry entry = core::toggleEffect(document_->song(), caret_, which);
        if (!entry.command) {
            emit editFailed(tr("Effects go on notes: put the caret on one."));
            return;
        }
        enter(std::move(entry));
    } catch (const std::out_of_range&) {
        emit editFailed(tr("That fret has no natural harmonic."));
    } catch (const std::logic_error&) {
        emit editFailed(which == core::NoteEffect::Tie
                            ? tr("There is no note to tie to.")
                            : tr("%1 needs strings.").arg(effectName(effect)));
    }
}

int ScoreDocument::caretEffects() const {
    if (!document_ || !hasCaret(document_->song())) {
        return 0;
    }
    int effects = 0;
    for (int effect = 0; effect < kEffectCount; ++effect) {
        if (core::hasEffectAtCaret(document_->song(), caret_,
                                   static_cast<core::NoteEffect>(effect))) {
            effects |= 1 << effect;
        }
    }
    return effects;
}

QString ScoreDocument::effectName(int effect) const {
    if (effect < 0 || effect >= kEffectCount) {
        return {};
    }
    return tr(kEffects.at(static_cast<std::size_t>(effect)).name);
}

QString ScoreDocument::effectKey(int effect) const {
    if (effect < 0 || effect >= kEffectCount) {
        return {};
    }
    return QString(QChar(kEffects.at(static_cast<std::size_t>(effect)).key));
}

bool ScoreDocument::effectNeedsStrings(int effect) const {
    return effect >= 0 && effect < kEffectCount &&
           core::needsStrings(static_cast<core::NoteEffect>(effect));
}

int ScoreDocument::effectForKey(const QString& text) const {
    if (text.size() != 1) {
        return -1;
    }
    const char16_t key = text.front().toUpper().unicode();
    const auto found =
        std::ranges::find_if(kEffects, [key](const EffectKey& entry) { return entry.key == key; });
    return found == kEffects.end() ? -1 : static_cast<int>(found - kEffects.begin());
}

void ScoreDocument::setBend(int form, int cents) {
    if (!document_ || !hasCaret(document_->song()) || form < 0 ||
        form >= static_cast<int>(core::BendForm::Custom)) {
        return;
    }
    pendingDigit_ = -1;
    core::BendCurve curve;
    try {
        curve = core::bendCurve(static_cast<core::BendForm>(form), cents);
    } catch (const std::invalid_argument&) {
        return;
    }
    core::Entry entry = core::setBend(document_->song(), caret_, curve);
    if (!entry.command) {
        // Silent when the notes have that bend already.
        if (core::bendAtCaret(document_->song(), caret_) != curve) {
            emit editFailed(tr("Effects go on notes: put the caret on one."));
        }
        return;
    }
    enter(std::move(entry));
}

core::BeatEffects ScoreDocument::beatEffectsAtCaret() const {
    if (!document_ || !hasCaret(document_->song())) {
        return {};
    }
    return core::beatEffectsAt(document_->song(), caret_);
}

void ScoreDocument::applyBeatEffects(const core::BeatEffects& effects) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    pendingDigit_ = -1;
    core::Entry entry = core::setBeatEffects(document_->song(), caret_, effects);
    if (!entry.command) {
        // Silent when the beat has them already.
        if (core::beatEffectsAt(document_->song(), caret_) != effects) {
            emit editFailed(tr("Effects go on notes: put the caret on one."));
        }
        return;
    }
    enter(std::move(entry));
}

void ScoreDocument::setStrum(int direction) {
    if (direction < 0 || direction > 2) {
        return;
    }
    core::BeatEffects effects = beatEffectsAtCaret();
    effects.strum = static_cast<core::Strum>(direction);
    applyBeatEffects(effects);
}

void ScoreDocument::setHairpin(int hairpin) {
    if (hairpin < 0 || hairpin > static_cast<int>(core::Hairpin::Diminuendo)) {
        return;
    }
    core::BeatEffects effects = beatEffectsAtCaret();
    effects.hairpin = static_cast<core::Hairpin>(hairpin);
    applyBeatEffects(effects);
}

void ScoreDocument::setSlur(bool slurred) {
    core::BeatEffects effects = beatEffectsAtCaret();
    effects.slur = slurred;
    applyBeatEffects(effects);
}

void ScoreDocument::setFermata(int fermata) {
    if (fermata < 0 || fermata > static_cast<int>(core::Fermata::Long)) {
        return;
    }
    core::BeatEffects effects = beatEffectsAtCaret();
    effects.fermata = static_cast<core::Fermata>(fermata);
    applyBeatEffects(effects);
}

void ScoreDocument::setOttava(int ottava) {
    if (ottava < 0 || ottava > static_cast<int>(core::Ottava::QuindicesimaBassa)) {
        return;
    }
    core::BeatEffects effects = beatEffectsAtCaret();
    effects.ottava = static_cast<core::Ottava>(ottava);
    applyBeatEffects(effects);
}

void ScoreDocument::setPedal(int pedal) {
    if (pedal < 0 || pedal > static_cast<int>(core::Pedal::Change)) {
        return;
    }
    core::BeatEffects effects = beatEffectsAtCaret();
    effects.pedal = static_cast<core::Pedal>(pedal);
    applyBeatEffects(effects);
}

void ScoreDocument::setBreath(int breath) {
    if (breath < 0 || breath > static_cast<int>(core::Breath::Caesura)) {
        return;
    }
    core::BeatEffects effects = beatEffectsAtCaret();
    effects.breath = static_cast<core::Breath>(breath);
    applyBeatEffects(effects);
}

void ScoreDocument::toggleGraceNote() {
    if (caretGraceNotes() > 0) {
        runEntry([this](const core::Song& song) { return core::removeGraceNotes(song, caret_); });
        return;
    }
    runEntry([this](const core::Song& song) {
        const core::Duration eighth(core::NoteValue::Eighth);
        try {
            return core::addGraceNote(song, caret_, -2, eighth, false);
        } catch (const std::out_of_range&) {
            return core::addGraceNote(song, caret_, 2, eighth, false); // from above
        }
    });
}

void ScoreDocument::setChordSymbol(const QString& name) {
    runEntry([this, text = name.toStdString()](const core::Song& song) {
        return core::setChordSymbol(song, caret_, text);
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

QStringList ScoreDocument::songChords() const {
    if (!document_) {
        return {};
    }
    return wordsOf(document_->song(), [](const core::Beat& beat) -> const std::string* {
        return beat.chord ? &beat.chord->name : nullptr;
    });
}

void ScoreDocument::setBeatText(const QString& text) {
    runEntry([this, words = text.toStdString()](const core::Song& song) {
        return core::setBeatText(song, caret_, words);
    });
}

QStringList ScoreDocument::songTexts() const {
    if (!document_) {
        return {};
    }
    return wordsOf(document_->song(), [](const core::Beat& beat) -> const std::string* {
        return beat.text ? &*beat.text : nullptr;
    });
}

int ScoreDocument::caretVerseCount() const {
    if (!document_ || !hasCaret(document_->song())) {
        return 0;
    }
    const core::Song& song = document_->song();
    const core::Caret caret = core::clampCaret(song, caret_);
    return static_cast<int>(core::verseCount(song.track(caret.track).staff(caret.staff)));
}

QVariantMap ScoreDocument::caretVerse(int verse) const {
    core::VerseText typed;
    if (document_ && hasCaret(document_->song()) && verse >= 0) {
        typed = core::verseAt(document_->song(), caret_, static_cast<std::size_t>(verse));
    }
    return {{QStringLiteral("first"), static_cast<int>(typed.first)},
            {QStringLiteral("text"), qstring(typed.text)}};
}

int ScoreDocument::setVerse(int verse, int first, const QString& text) {
    if (verse < 0 || verse >= maxVerses() || first < 0) {
        return 0;
    }
    std::size_t leftover = 0;
    runEntry([&](const core::Song& song) {
        core::VerseEntry typed =
            core::setVerse(song, caret_, static_cast<std::size_t>(verse),
                           static_cast<std::size_t>(first), text.toStdString());
        leftover = typed.leftover;
        return std::move(typed.entry);
    });
    return static_cast<int>(leftover);
}

void ScoreDocument::setPickStroke(int direction) {
    if (direction < 0 || direction > 2) {
        return;
    }
    core::BeatEffects effects = beatEffectsAtCaret();
    effects.pickStroke = static_cast<core::PickStroke>(direction);
    applyBeatEffects(effects);
}

void ScoreDocument::setTremolo(int value) {
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
    core::BeatEffects effects = beatEffectsAtCaret();
    effects.tremoloPicking = speed;
    applyBeatEffects(effects);
}

void ScoreDocument::setWhammy(int form, int cents) {
    if (form < 0 || form >= static_cast<int>(core::BendForm::Custom)) {
        return;
    }
    core::BeatEffects effects = beatEffectsAtCaret();
    try {
        effects.whammyBar = core::whammyCurve(static_cast<core::BendForm>(form), cents);
    } catch (const std::invalid_argument&) {
        return;
    }
    applyBeatEffects(effects);
}

void ScoreDocument::setBassTechnique(int technique) {
    if (technique < 0 || technique > static_cast<int>(core::BassTechnique::Pop)) {
        return;
    }
    core::BeatEffects effects = beatEffectsAtCaret();
    effects.bassTechnique = static_cast<core::BassTechnique>(technique);
    applyBeatEffects(effects);
}

QString ScoreDocument::whammyAmount(int cents) const {
    constexpr int kStep = 200;
    const int depth = std::abs(cents);
    const QString steps = depth == kStep ? QStringLiteral("1") : bendAmount(depth);
    return (cents < 0 ? QStringLiteral("-") : QStringLiteral("+")) + steps;
}

QString ScoreDocument::whammyName(int form, int cents) const {
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

QString ScoreDocument::bassTechniqueName(int technique) const {
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

core::NoteEffects ScoreDocument::noteEffectsAtCaret() const {
    if (!document_ || !hasCaret(document_->song())) {
        return {};
    }
    return core::noteEffectsAt(document_->song(), caret_);
}

void ScoreDocument::changeNoteEffects(
    const std::function<core::Entry(const core::Song&, const core::Caret&)>& make,
    const std::function<bool(const core::NoteEffects&)>& has) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    pendingDigit_ = -1;
    try {
        core::Entry entry = make(document_->song(), caret_);
        if (!entry.command) {
            if (!has(noteEffectsAtCaret()) || pitchesAtCaret().empty()) {
                emit editFailed(tr("Effects go on notes: put the caret on one."));
            }
            return;
        }
        enter(std::move(entry));
    } catch (const std::out_of_range&) {
        emit editFailed(tr("That fret has no natural harmonic."));
    } catch (const std::logic_error&) {
        emit editFailed(tr("Harmonics need strings."));
    }
}

void ScoreDocument::setSlideIn(int kind) {
    if (kind < 0 || kind > static_cast<int>(core::SlideIn::FromAbove)) {
        return;
    }
    const auto in = static_cast<core::SlideIn>(kind);
    changeNoteEffects(
        [in](const core::Song& song, const core::Caret& caret) {
            return core::setSlides(song, caret, in, core::noteEffectsAt(song, caret).slideOut);
        },
        [in](const core::NoteEffects& fx) { return fx.slideIn == in; });
}

void ScoreDocument::setSlideOut(int kind) {
    if (kind < 0 || kind > static_cast<int>(core::SlideOut::Upwards)) {
        return;
    }
    const auto out = static_cast<core::SlideOut>(kind);
    changeNoteEffects(
        [out](const core::Song& song, const core::Caret& caret) {
            return core::setSlides(song, caret, core::noteEffectsAt(song, caret).slideIn, out);
        },
        [out](const core::NoteEffects& fx) { return fx.slideOut == out; });
}

void ScoreDocument::setVibrato(int kind) {
    if (kind < 0 || kind > static_cast<int>(core::Vibrato::Wide)) {
        return;
    }
    const auto vibrato = static_cast<core::Vibrato>(kind);
    changeNoteEffects(
        [vibrato](const core::Song& song, const core::Caret& caret) {
            return core::setVibrato(song, caret, vibrato);
        },
        [vibrato](const core::NoteEffects& fx) { return fx.vibrato == vibrato; });
}

void ScoreDocument::setAccent(int kind) {
    if (kind < 0 || kind > static_cast<int>(core::Accent::Marcato)) {
        return;
    }
    const auto accent = static_cast<core::Accent>(kind);
    changeNoteEffects(
        [accent](const core::Song& song, const core::Caret& caret) {
            return core::setAccent(song, caret, accent);
        },
        [accent](const core::NoteEffects& fx) { return fx.accent == accent; });
}

void ScoreDocument::setHarmonic(int kind) {
    if (kind < 0 || kind > static_cast<int>(core::HarmonicKind::Semi)) {
        return;
    }
    const auto harmonic = static_cast<core::HarmonicKind>(kind);
    changeNoteEffects(
        [harmonic](const core::Song& song, const core::Caret& caret) {
            return core::setHarmonic(song, caret, harmonic);
        },
        [harmonic](const core::NoteEffects& fx) { return fx.harmonic.kind == harmonic; });
}

int ScoreDocument::caretSlideIn() const {
    return static_cast<int>(noteEffectsAtCaret().slideIn);
}

int ScoreDocument::caretSlideOut() const {
    return static_cast<int>(noteEffectsAtCaret().slideOut);
}

int ScoreDocument::caretVibrato() const {
    return static_cast<int>(noteEffectsAtCaret().vibrato);
}

int ScoreDocument::caretAccent() const {
    return static_cast<int>(noteEffectsAtCaret().accent);
}

int ScoreDocument::caretTrill() const {
    return noteEffectsAtCaret().trill.semitones;
}

int ScoreDocument::caretOrnament() const {
    return static_cast<int>(noteEffectsAtCaret().ornament);
}

void ScoreDocument::setOrnament(int kind) {
    if (kind < 0 || kind > static_cast<int>(core::Ornament::InvertedTurn)) {
        return;
    }
    const auto ornament = static_cast<core::Ornament>(kind);
    changeNoteEffects(
        [ornament](const core::Song& song, const core::Caret& caret) {
            return core::setOrnament(song, caret, ornament);
        },
        [ornament](const core::NoteEffects& fx) { return fx.ornament == ornament; });
}

int ScoreDocument::caretFingering() const {
    return static_cast<int>(noteEffectsAtCaret().fingering);
}

int ScoreDocument::caretPluck() const {
    return static_cast<int>(noteEffectsAtCaret().pluck);
}

void ScoreDocument::setFingering(int finger) {
    if (finger < 0 || finger > static_cast<int>(core::Finger::Little)) {
        return;
    }
    const auto chosen = static_cast<core::Finger>(finger);
    changeNoteEffects(
        [chosen](const core::Song& song, const core::Caret& caret) {
            return core::setFingering(song, caret, std::span(&chosen, 1));
        },
        [chosen](const core::NoteEffects& fx) { return fx.fingering == chosen; });
}

void ScoreDocument::setPluck(int finger) {
    if (finger < 0 || finger > static_cast<int>(core::Finger::Little) || !caretFretted()) {
        return;
    }
    const auto chosen = static_cast<core::Finger>(finger);
    changeNoteEffects(
        [chosen](const core::Song& song, const core::Caret& caret) {
            return core::setPluck(song, caret, chosen);
        },
        [chosen](const core::NoteEffects& fx) { return fx.pluck == chosen; });
}

void ScoreDocument::toggleTrill() {
    const bool on = caretTrill() == 0;
    changeNoteEffects(
        [on](const core::Song& song, const core::Caret& caret) {
            return on ? core::setTrillToNextStep(song, caret, core::NoteValue::ThirtySecond)
                      : core::setTrill(song, caret, core::Trill{});
        },
        [on](const core::NoteEffects& fx) { return fx.trill.isTrill() == on; });
}

int ScoreDocument::caretDynamic() const {
    if (!document_ || !hasCaret(document_->song())) {
        return static_cast<int>(core::Dynamic::MF);
    }
    return static_cast<int>(core::dynamicAt(document_->song(), caret_));
}

void ScoreDocument::setDynamic(int dynamic) {
    if (!document_ || !hasCaret(document_->song()) || dynamic < 0 ||
        dynamic > static_cast<int>(core::Dynamic::FFF)) {
        return;
    }
    pendingDigit_ = -1;
    const auto wanted = static_cast<core::Dynamic>(dynamic);
    core::Entry entry = core::setDynamic(document_->song(), caret_, wanted);
    if (!entry.command) {
        if (core::dynamicAt(document_->song(), caret_) != wanted) {
            emit editFailed(tr("Dynamics go on notes: put the caret on one."));
        }
        return;
    }
    enter(std::move(entry));
}

QString ScoreDocument::dynamicName(int dynamic) const {
    static constexpr std::array<const char*, 8> kNames{"ppp", "pp", "p",  "mp",
                                                       "mf",  "f",  "ff", "fff"};
    return dynamic >= 0 && dynamic < static_cast<int>(kNames.size())
               ? QString::fromLatin1(kNames.at(static_cast<std::size_t>(dynamic)))
               : QString();
}

int ScoreDocument::caretHarmonic() const {
    return static_cast<int>(noteEffectsAtCaret().harmonic.kind);
}

QString ScoreDocument::slideInName(int kind) const {
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

QString ScoreDocument::slideOutName(int kind) const {
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

QString ScoreDocument::harmonicName(int kind) const {
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

int ScoreDocument::caretStrum() const {
    return static_cast<int>(beatEffectsAtCaret().strum);
}

int ScoreDocument::caretHairpin() const {
    return static_cast<int>(beatEffectsAtCaret().hairpin);
}

int ScoreDocument::caretFermata() const {
    return static_cast<int>(beatEffectsAtCaret().fermata);
}

int ScoreDocument::caretOttava() const {
    return static_cast<int>(beatEffectsAtCaret().ottava);
}

int ScoreDocument::caretPedal() const {
    return static_cast<int>(beatEffectsAtCaret().pedal);
}

int ScoreDocument::caretBreath() const {
    return static_cast<int>(beatEffectsAtCaret().breath);
}

bool ScoreDocument::caretSlur() const {
    return beatEffectsAtCaret().slur;
}

namespace {

// The clefs the measure popup offers, in its order (ADR-0087).
constexpr std::array kOfferedClefs{core::Clef::Treble, core::Clef::Bass, core::Clef::Alto,
                                   core::Clef::Tenor};

} // namespace

int ScoreDocument::caretClef() const {
    if (!document_ || !hasCaret(document_->song()) || caretOnDrums()) {
        return -1;
    }
    const core::Caret caret = core::clampCaret(document_->song(), caret_);
    switch (document_->song().track(caret.track).staff(caret.staff).clefAt(caret.measure)) {
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

void ScoreDocument::setClef(int clef) {
    if (clef < 0 || std::cmp_greater_equal(clef, kOfferedClefs.size())) {
        return;
    }
    runEntry([this, chosen = kOfferedClefs.at(static_cast<std::size_t>(clef))](
                 const core::Song& song) { return core::setClef(song, caret_, chosen); });
}

int ScoreDocument::caretRepeat() const {
    if (!document_ || !hasCaret(document_->song())) {
        return 0;
    }
    return static_cast<int>(core::measureRepeatAt(document_->song(), caret_));
}

void ScoreDocument::setMeasureRepeat(int measures) {
    if (measures < 0 || measures > 2) {
        return;
    }
    runEntry([this, measures](const core::Song& song) {
        return core::setMeasureRepeat(song, caret_, static_cast<std::size_t>(measures));
    });
}

QString ScoreDocument::caretText() const {
    if (!document_ || !hasCaret(document_->song())) {
        return {};
    }
    return qstring(core::beatTextAt(document_->song(), caret_));
}

QString ScoreDocument::caretChord() const {
    if (!document_ || !hasCaret(document_->song())) {
        return {};
    }
    return qstring(core::chordNameAt(document_->song(), caret_));
}

int ScoreDocument::caretGraceNotes() const {
    if (!document_ || !hasCaret(document_->song())) {
        return 0;
    }
    return static_cast<int>(core::graceNotesAt(document_->song(), caret_));
}

int ScoreDocument::caretPickStroke() const {
    return static_cast<int>(beatEffectsAtCaret().pickStroke);
}

int ScoreDocument::caretTremolo() const {
    const auto speed = beatEffectsAtCaret().tremoloPicking;
    return speed ? denominatorOf(*speed) : 0;
}

int ScoreDocument::caretWhammyForm() const {
    return static_cast<int>(core::whammyForm(beatEffectsAtCaret().whammyBar));
}

int ScoreDocument::caretWhammyCents() const {
    return core::whammyCents(beatEffectsAtCaret().whammyBar);
}

int ScoreDocument::caretBassTechnique() const {
    return static_cast<int>(beatEffectsAtCaret().bassTechnique);
}

int ScoreDocument::caretBendForm() const {
    if (!document_ || !hasCaret(document_->song())) {
        return 0;
    }
    return static_cast<int>(core::bendForm(core::bendAtCaret(document_->song(), caret_)));
}

int ScoreDocument::caretBendCents() const {
    if (!document_ || !hasCaret(document_->song())) {
        return 0;
    }
    return core::bendAtCaret(document_->song(), caret_).maxCents();
}

QString ScoreDocument::bendAmount(int cents) const {
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

QString ScoreDocument::bendName(int form, int cents) const {
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
    selectionMoved();
}

int ScoreDocument::selectionFirst() const {
    return anchor_ ? static_cast<int>(std::min(*anchor_, caret_.measure)) : -1;
}

int ScoreDocument::selectionLast() const {
    return anchor_ ? static_cast<int>(std::max(*anchor_, caret_.measure)) : -1;
}

QVariantList ScoreDocument::selectionBoxes(int index) const {
    QVariantList boxes;
    if (!anchor_ || !pages_ || index < 0 || index >= static_cast<int>(pages_->size())) {
        return boxes;
    }
    const auto first = static_cast<std::size_t>(selectionFirst());
    const auto last = static_cast<std::size_t>(selectionLast());
    for (const notation::MeasureBox& box : (*pages_)[static_cast<std::size_t>(index)].measures) {
        if (box.index >= first && box.index <= last) {
            boxes.append(QRectF(box.x, box.y, box.width, box.height));
        }
    }
    return boxes;
}

void ScoreDocument::selectionMoved() {
    ++selectionRevision_;
    emit selectionChanged();
}

std::pair<std::size_t, std::size_t> ScoreDocument::selectedRange() const {
    if (!anchor_) {
        return {caret_.measure, 1};
    }
    const auto first = static_cast<std::size_t>(selectionFirst());
    return {first, static_cast<std::size_t>(selectionLast()) - first + 1};
}

void ScoreDocument::extendSelection(int measures) {
    if (!document_ || !hasCaret(document_->song()) || measures == 0) {
        return;
    }
    const core::Song& song = document_->song();
    if (!anchor_) {
        anchor_ = caret_.measure;
    }
    const auto last = static_cast<long long>(song.measureCount()) - 1;
    const auto target = std::clamp(static_cast<long long>(caret_.measure) + measures, 0LL, last);
    pendingDigit_ = -1;
    caret_.measure = static_cast<std::size_t>(target);
    caret_.beat = 0;
    caret_ = core::clampCaret(song, caret_);
    followBeat();
    placeCaretBox();
}

void ScoreDocument::selectAll() {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    const core::Song& song = document_->song();
    pendingDigit_ = -1;
    anchor_ = song.measureCount() - 1;
    caret_.measure = 0;
    caret_.beat = 0;
    caret_ = core::clampCaret(song, caret_);
    followBeat();
    placeCaretBox();
}

void ScoreDocument::clearSelection() {
    if (!anchor_) {
        return;
    }
    anchor_.reset();
    emit caretChanged(); // the status line
    selectionMoved();
}

void ScoreDocument::copy() {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    const auto [first, count] = selectedRange();
    const bool every = track_ == kAllTracks;
    clip_ = core::copyMeasures(document_->song(), first, count, every ? 0 : caret_.track,
                               every ? 0 : 1);
    emit clipboardChanged();
    emit notice(count == 1 ? tr("Copied measure %1.").arg(first + 1)
                           : tr("Copied measures %1-%2.").arg(first + 1).arg(first + count));
}

void ScoreDocument::paste(bool insert) {
    if (!document_ || !hasCaret(document_->song())) {
        return;
    }
    if (!clip_) {
        emit editFailed(tr("Copy some measures first."));
        return;
    }
    const std::size_t at = selectedRange().first;
    // One copied track goes to the caret's; several to the tracks they came from.
    const std::size_t track = clip_->tracks.size() == 1 ? caret_.track : 0;
    std::unique_ptr<core::Command> command;
    try {
        command = core::makePaste(document_->song(), *clip_, at, track,
                                  insert ? core::PasteMode::Insert : core::PasteMode::Replace);
    } catch (const std::out_of_range&) {
        emit editFailed(tr("The song has fewer tracks than were copied."));
        return;
    } catch (const std::invalid_argument&) {
        emit editFailed(tr("Those measures do not fit this track."));
        return;
    }
    pendingDigit_ = -1;
    anchor_ = at;
    caret_.measure = at + clip_->measureCount() - 1;
    caret_.beat = 0;
    if (!execute(std::move(command))) {
        anchor_.reset();
        clampCaret();
        placeCaretBox();
    }
}

void ScoreDocument::transposeSelection(int semitones) {
    if (!document_ || !hasCaret(document_->song()) || semitones == 0) {
        return;
    }
    const auto [first, count] = selectedRange();
    core::Transposition what{.semitones = semitones, .first = first, .count = count};
    if (track_ != kAllTracks) {
        what.tracks.push_back(caret_.track);
    }
    std::unique_ptr<core::Command> command;
    try {
        command = core::makeTranspose(document_->song(), what);
    } catch (const std::exception&) {
        emit editFailed(caretFretted() ? tr("A note would leave the range or the neck.")
                                       : tr("A note would leave the range."));
        return;
    }
    pendingDigit_ = -1;
    execute(std::move(command));
}

} // namespace elara::app
