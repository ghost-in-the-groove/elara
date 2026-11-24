// SPDX-License-Identifier: AGPL-3.0-or-later
#include "song_report.hpp"

#include "common.hpp"

#include <format>
#include <iostream>

namespace elara::cli {

namespace {

std::string jsonOptional(const std::optional<std::size_t>& value) {
    return value ? std::to_string(*value) : std::string("null");
}

} // namespace

std::string tracksJson(const core::Song& song) {
    std::string out = "[";
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        const auto& track = song.track(t);
        std::size_t beats = 0;
        std::size_t notes = 0;
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            for (std::size_t m = 0; m < song.measureCount(); ++m) {
                for (const auto& voice : track.staff(s).measure(m).voices) {
                    beats += voice.beats.size();
                    for (const auto& beat : voice.beats) {
                        notes += beat.notes.size();
                    }
                }
            }
        }
        const core::TrackMix& mix = track.mix();
        const core::Staff& first = track.staff(0);
        out += std::format(
            R"({}{{"name":"{}","mix":{{"volume":{},"pan":{},"mute":{},"solo":{}}},"tuning":{},"capo":{},"staves":{},"beats":{},"notes":{}}})",
            t == 0 ? "" : ",", jsonEscape(track.name()), jsonNumber(mix.volume),
            jsonNumber(mix.pan), mix.mute, mix.solo,
            first.tuning() ? std::format("\"{}\"", first.tuning()->toString()) : "null",
            first.capo(), track.staffCount(), beats, notes);
    }
    return out + "]";
}

std::string measuresJson(const core::Song& song) {
    std::string out = "[";
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        const auto& header = song.measureHeader(m);
        out += std::format(R"({}{{"time":"{}","key":"{}")", m == 0 ? "" : ",",
                           header.timeSignature.toString(), header.keySignature.toString());
        std::string signs;
        for (const SignName& sign : kSignNames) {
            if (header.*sign.flag) {
                signs += std::format(R"({}"{}")", signs.empty() ? "" : ",", sign.name);
            }
        }
        if (!signs.empty()) {
            out += std::format(R"(,"signs":[{}])", signs);
        }
        if (header.jump != core::JumpTo::None) {
            out += std::format(R"(,"jump":"{}")",
                               kJumpToNames.at(static_cast<std::size_t>(header.jump)));
            if (header.jumpThen != core::JumpThen::End) {
                out += std::format(R"(,"jumpThen":"{}")",
                                   kJumpThenNames.at(static_cast<std::size_t>(header.jumpThen)));
            }
        }
        // A tempo that starts the measure (ADR-0042); changes inside it are
        // kept but not listed here.
        for (const core::TempoChange& change : header.tempoChanges) {
            if (change.offset == core::Fraction{}) {
                out += std::format(R"(,"tempo":{})", jsonNumber(change.tempo.bpm()));
            }
        }
        out += "}";
    }
    return out + "]";
}

std::string issuesJson(const std::vector<core::Issue>& issues) {
    std::string out = "[";
    for (std::size_t i = 0; i < issues.size(); ++i) {
        const auto& issue = issues[i];
        out += std::format(
            R"({}{{"kind":"{}","track":{},"staff":{},"measure":{},"voice":{},"beat":{},"note":{}}})",
            i == 0 ? "" : ",", core::issueName(issue.kind), issue.track, issue.staff, issue.measure,
            issue.voice, jsonOptional(issue.beat), jsonOptional(issue.note));
    }
    return out + "]";
}

void printSongText(const core::Song& song) {
    const auto& info = song.info();
    const double seconds = song.durationSeconds();
    std::cout << std::format("{}{}: {} track(s), {} measure(s), {}:{:04.1f} at {} bpm\n",
                             info.title.empty() ? "Untitled" : info.title,
                             info.artist.empty() ? "" : " - " + info.artist, song.trackCount(),
                             song.measureCount(), static_cast<int>(seconds) / 60,
                             seconds - (static_cast<int>(seconds) / 60) * 60.0,
                             song.startTempo().bpm());
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        const auto& track = song.track(t);
        std::cout << std::format("  track {}: {} ({} {})", t, track.name(), track.staffCount(),
                                 track.staffCount() == 1 ? "staff" : "staves");
        const auto& tuning = track.staff(0).tuning();
        if (tuning) {
            std::cout << std::format(", tuning {}", tuning->toString());
        }
        if (track.staff(0).capo() != 0) {
            std::cout << std::format(", capo {}", track.staff(0).capo());
        }
        if (const core::TrackMix& mix = track.mix(); mix != core::TrackMix{}) {
            std::cout << std::format(", volume {}, pan {}{}{}", jsonNumber(mix.volume),
                                     jsonNumber(mix.pan), mix.mute ? ", muted" : "",
                                     mix.solo ? ", solo" : "");
        }
        std::cout << '\n';
    }
    const auto issues = core::validate(song);
    if (issues.empty()) {
        std::cout << "  no issues\n";
    }
    for (const auto& issue : issues) {
        std::cout << std::format("  issue: {} (track {}, staff {}, measure {}, voice {})\n",
                                 core::issueName(issue.kind), issue.track, issue.staff,
                                 issue.measure, issue.voice);
    }
}

std::string displayPath(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

} // namespace elara::cli
