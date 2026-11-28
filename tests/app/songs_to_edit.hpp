// SPDX-License-Identifier: AGPL-3.0-or-later
// The songs the window's edits are checked on, through the document's own
// interface: a new guitar song and the corpus files the window's smoke tests
// open, each laid out as the window shows it, with what is at its caret. An
// edit is read once its layout is done, as the window waits for it, and
// playback must then play the song the pages show (ADR-0031).
#pragma once

#include <elara/audio/sequence.hpp>
#include <elara/core/song.hpp>

#include "caret_editor.hpp"
#include "score_document.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QUrl>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

namespace elara::app::testing {

/// "new" is a new song on the electric guitar preset; the others are files
/// of the corpus's gp folder, each with a fretted first track.
inline constexpr std::array<const char*, 4> kSongsToEdit{"new", "band.gp", "quartet.gp4",
                                                         "guitar-techniques.gp"};

/// One of kSongsToEdit, open and laid out, the caret where opening put it.
struct SongToEdit {
    ScoreDocument score;
    CaretEditor editor; // what is at the caret
    core::Song opened;  // the song as it opened

    explicit SongToEdit(std::string_view source) {
        editor.setDocument(&score);
        if (source == "new") {
            score.newSong(QStringLiteral("electric-guitar"));
        } else {
            score.open(QUrl::fromLocalFile(
                QStringLiteral(ELARA_CORPUS_DIR "/gp/") +
                QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size()))));
        }
        REQUIRE(laidOut());
        REQUIRE(score.caretPage() >= 0);
        opened = *score.song();
    }

    /// Whether what was asked of the document so far is laid out, and
    /// playback plays the song the pages show.
    [[nodiscard]] bool laidOut() {
        const auto done = [this] {
            return score.status() == ScoreDocument::Status::Ready && !score.busy() &&
                   score.pageCount() > 0;
        };
        QElapsedTimer clock;
        clock.start();
        while (!done() && clock.elapsed() < 30000) {
            QCoreApplication::processEvents();
        }
        return done() && score.song() != nullptr && score.sequence() != nullptr &&
               *score.sequence() == audio::Sequence(*score.song(), score.practice());
    }

    /// Undoes every edit, then waits for the layout.
    [[nodiscard]] bool undoAll() {
        for (int i = 0; i < 64 && score.modified() && score.canUndo(); ++i) {
            score.undo();
        }
        return laidOut();
    }

    /// Whether the song is as it opened, with nothing left to save.
    [[nodiscard]] bool unchanged() const { return !score.modified() && *score.song() == opened; }
};

} // namespace elara::app::testing
