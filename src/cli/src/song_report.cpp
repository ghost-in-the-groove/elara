// SPDX-License-Identifier: AGPL-3.0-or-later
#include "song_report.hpp"

#include <elara/core/names.hpp>

#include "common.hpp"

#include <cstddef>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace elara::cli {

namespace {

// `"name":3`, or `"name":null` without a value.
void writeOptional(JsonWriter& json, std::string_view name,
                   const std::optional<std::size_t>& value) {
    json.key(name);
    if (value) {
        json.value(*value);
    } else {
        json.null();
    }
}

// A track's own breaks (ADR-0152), as `"breaks":[...]`; nothing when it holds none.
void writeBreaks(JsonWriter& json, const core::MeasureLayouts& layouts) {
    if (!layouts) {
        return;
    }
    bool any = false;
    for (std::size_t m = 0; m < layouts->size(); ++m) {
        if (const core::Break kind = (*layouts)[m].breakBefore; kind != core::Break::Auto) {
            if (!any) {
                json.key("breaks").beginArray();
                any = true;
            }
            json.beginObject().field("measure", m).field("break", core::nameOf(kind)).endObject();
        }
    }
    if (any) {
        json.endArray();
    }
}

} // namespace

void writeTracks(JsonWriter& json, const core::Song& song) {
    json.beginArray();
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
        json.beginObject()
            .field("name", track.name())
            .key("mix")
            .beginObject()
            .field("volume", mix.volume)
            .field("pan", mix.pan)
            .field("mute", mix.mute)
            .field("solo", mix.solo)
            .endObject()
            .key("tuning");
        if (first.tuning()) {
            json.string(first.tuning()->toString());
        } else {
            json.null();
        }
        json.field("capo", first.capo())
            .field("staves", track.staffCount())
            .field("beats", beats)
            .field("notes", notes);
        writeBreaks(json, track.measureLayouts());
        json.endObject();
    }
    json.endArray();
}

void writeMeasures(JsonWriter& json, const core::Song& song) {
    json.beginArray();
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        const auto& header = song.measureHeader(m);
        json.beginObject()
            .field("time", header.timeSignature.toString())
            .field("key", header.keySignature.toString());
        if (header.repeatStart) {
            json.field("repeatStart", true);
        }
        if (header.repeatEnd()) {
            json.field("repeatTimes", header.repeatTimes);
        }
        if (header.alternateEndings != 0) {
            json.key("endings").beginArray();
            for (int time = 1; time <= core::MeasureHeader::kMaxEnding; ++time) {
                if (((header.alternateEndings >> (time - 1)) & 1U) != 0) {
                    json.value(time);
                }
            }
            json.endArray();
        }
        if (header.doubleBar) {
            json.field("doubleBar", true);
        }
        if (header.tripletFeel != core::TripletFeel::None) {
            json.field("tripletFeel", core::nameOf(header.tripletFeel));
        }
        bool signs = false;
        for (const core::NavigationSign sign : core::enumValues<core::NavigationSign>()) {
            if (header.*core::signFlag(sign)) {
                if (!signs) {
                    json.key("signs").beginArray();
                    signs = true;
                }
                json.value(core::nameOf(sign));
            }
        }
        if (signs) {
            json.endArray();
        }
        if (header.jump != core::JumpTo::None) {
            json.field("jump", core::nameOf(header.jump));
            if (header.jumpThen != core::JumpThen::End) {
                json.field("jumpThen", core::nameOf(header.jumpThen));
            }
        }
        // A tempo that starts the measure (ADR-0042); changes inside it are
        // kept but not listed here.
        for (const core::TempoChange& change : header.tempoChanges) {
            if (change.offset == core::Fraction{}) {
                json.field("tempo", change.tempo.bpm());
                if (change.gradual) {
                    json.field("gradual", true); // ADR-0082
                }
            }
        }
        if (!header.section.empty()) {
            json.field("section", header.section);
        }
        // The song's break before the measure (ADR-0152); the tracks list their own.
        if (const core::Break kind = song.measureLayout(core::LayoutScope::song(), m).breakBefore;
            kind != core::Break::Auto) {
            json.field("break", core::nameOf(kind));
        }
        json.endObject();
    }
    json.endArray();
}

void writeIssues(JsonWriter& json, const std::vector<core::Issue>& issues) {
    json.beginArray();
    for (const auto& issue : issues) {
        json.beginObject()
            .field("kind", core::issueName(issue.kind))
            .field("track", issue.track)
            .field("staff", issue.staff)
            .field("measure", issue.measure)
            .field("voice", issue.voice);
        writeOptional(json, "beat", issue.beat);
        writeOptional(json, "note", issue.note);
        json.endObject();
    }
    json.endArray();
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
