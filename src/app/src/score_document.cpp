// SPDX-License-Identifier: AGPL-3.0-or-later
#include "score_document.hpp"

#include <elara/audio/sequence.hpp>
#include <elara/io/open.hpp>
#include <elara/notation/cursor.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QtConcurrent/QtConcurrentRun>

#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
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
        result.sequence = std::move(sequence);
    } catch (const std::exception& error) {
        result.error = qstring(error.what());
    }
    return result;
}

} // namespace

ScoreDocument::ScoreDocument(QObject* parent) : QObject(parent) {}

QString ScoreDocument::title() const {
    if (!song_) {
        return {};
    }
    const std::string& title = song_->info().title;
    return title.empty() ? QFileInfo(source_.toLocalFile()).completeBaseName() : qstring(title);
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
    if (layout.song != song_) {
        song_ = layout.song;
        sequence_ = layout.sequence;
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
