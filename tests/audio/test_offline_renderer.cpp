// SPDX-License-Identifier: AGPL-3.0-or-later
// Offline rendering with the built-in synthesizer (ADR-0018).
#include <elara/audio/offline_renderer.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/tone_generator.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

using elara::audio::EventKind;
using elara::audio::LoggedEvent;
using elara::audio::OfflineRenderer;
using elara::audio::peakLevel;
using elara::audio::RenderOptions;
using elara::audio::Sequence;
using elara::audio::Timbre;
using elara::core::Beat;
using elara::core::Duration;
using elara::core::Dynamic;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;

namespace {

constexpr double kRate = 48000.0;

Track track(const char* name, InstrumentKind kind, int program) {
    return Track(name, Instrument{.name = name, .kind = kind, .midiProgram = program}, {Staff{}});
}

// 4/4 at quarter = 120: two seconds per measure.
Song song(std::vector<Track> tracks, std::size_t measures) {
    Song result;
    for (Track& t : tracks) {
        result.addTrack(std::move(t));
    }
    result.appendMeasures(measures);
    return result;
}

Beat beat(NoteValue value, std::vector<int> keys = {}, Dynamic dynamic = Dynamic::MF) {
    Beat result{.duration = Duration(value)};
    for (const int key : keys) {
        Note note{.pitch = Pitch::fromMidi(key)};
        note.effects.dynamic = dynamic;
        result.notes.push_back(note);
    }
    return result;
}

std::vector<Beat>& beats(Song& s, std::size_t t, std::size_t measure) {
    return s.track(t).staff(0).measure(measure).voices.at(0).beats;
}

std::int64_t frame(double seconds) {
    return std::llround(seconds * kRate);
}

std::span<const float> frames(const std::vector<float>& stereo, double from, double to) {
    const auto first = static_cast<std::size_t>(2 * frame(from));
    const auto last = static_cast<std::size_t>(2 * frame(to));
    return std::span<const float>(stereo).subspan(first, last - first);
}

// Guitar, bass and drums over four measures.
Song band() {
    Song s = song({track("Guitar", InstrumentKind::Fretted, 25),
                   track("Bass", InstrumentKind::Fretted, 33),
                   track("Drums", InstrumentKind::Percussion, 0)},
                  4);
    for (std::size_t m = 0; m < 4; ++m) {
        beats(s, 0, m) = {beat(NoteValue::Quarter, {52, 59, 64}), beat(NoteValue::Quarter, {57}),
                          beat(NoteValue::Half, {55, 59, 62})};
        beats(s, 1, m) = {beat(NoteValue::Half, {28}), beat(NoteValue::Half, {33})};
        beats(s, 2, m) = {beat(NoteValue::Eighth, {36, 42}), beat(NoteValue::Eighth, {42}),
                          beat(NoteValue::Eighth, {38, 42}), beat(NoteValue::Eighth, {42}),
                          beat(NoteValue::Eighth, {36, 42}), beat(NoteValue::Eighth, {36, 46}),
                          beat(NoteValue::Eighth, {38, 42}), beat(NoteValue::Eighth, {49})};
    }
    return s;
}

} // namespace

TEST_CASE("OfflineRenderer: every event is dispatched at its time in frames", "[audio][render]") {
    Song s = song({track("Piano", InstrumentKind::Keyboard, 0)}, 2);
    beats(s, 0, 0) = {beat(NoteValue::Quarter, {60}), beat(NoteValue::Quarter, {62}),
                      beat(NoteValue::Quarter, {64}), beat(NoteValue::Quarter, {65})};
    beats(s, 0, 1) = {beat(NoteValue::Whole)};
    const Sequence sequence(s);
    OfflineRenderer renderer(s, sequence, {.sampleRate = kRate, .logEvents = true});
    CHECK(renderer.frames() == frame(4.0 + 2.0)); // two measures and the tail
    static_cast<void>(renderer.renderRest());
    CHECK(renderer.position() == renderer.frames());

    const int velocity = sequence.events().front().velocity;
    const auto on = [&](double seconds, int key) {
        return LoggedEvent{frame(seconds), EventKind::NoteOn, 0, key, velocity};
    };
    const auto off = [&](double seconds, int key) {
        return LoggedEvent{frame(seconds), EventKind::NoteOff, 0, key, 0};
    };
    const std::vector<LoggedEvent> expected{on(0.0, 60), off(0.5, 60), on(0.5, 62), off(1.0, 62),
                                            on(1.0, 64), off(1.5, 64), on(1.5, 65), off(2.0, 65)};
    REQUIRE(renderer.log().size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(renderer.log()[i].frame == expected[i].frame);
        CHECK(renderer.log()[i].kind == expected[i].kind);
        CHECK(renderer.log()[i].key == expected[i].key);
    }
}

TEST_CASE("OfflineRenderer: silence before the first note and after the last release",
          "[audio][render]") {
    Song s = song({track("Organ", InstrumentKind::Keyboard, 16)}, 2);
    beats(s, 0, 0) = {beat(NoteValue::Whole)};
    beats(s, 0, 1) = {beat(NoteValue::Whole, {69})};
    const Sequence sequence(s);
    OfflineRenderer renderer(s, sequence, {.sampleRate = kRate});
    const std::vector<float> stereo = renderer.renderRest();
    REQUIRE(stereo.size() == static_cast<std::size_t>(2 * frame(6.0)));
    CHECK(peakLevel(frames(stereo, 0.0, 2.0)) == 0.0f);
    CHECK(peakLevel(frames(stereo, 2.0, 2.1)) > 0.05f);
    CHECK(peakLevel(frames(stereo, 3.9, 4.0)) > 0.05f);
    CHECK(peakLevel(frames(stereo, 5.0, 6.0)) == 0.0f);
    for (std::size_t i = 0; i < stereo.size(); i += 2) {
        REQUIRE(stereo[i] == stereo[i + 1]); // centred until the mixer has pan
    }
}

TEST_CASE("OfflineRenderer: each track plays in the timbre of its instrument", "[audio][render]") {
    const Song s = band();
    const Sequence sequence(s);
    const OfflineRenderer renderer(s, sequence);
    CHECK(renderer.timbres() == std::vector<Timbre>{Timbre::Guitar, Timbre::Bass, Timbre::Drums});
}

TEST_CASE("OfflineRenderer: the output is the same on any number of threads and call sizes",
          "[audio][render]") {
    const Song s = band();
    const Sequence sequence(s);
    OfflineRenderer one(s, sequence, {.sampleRate = kRate, .threads = 1, .logEvents = true});
    OfflineRenderer many(s, sequence, {.sampleRate = kRate, .threads = 4, .logEvents = true});
    const std::vector<float> whole = one.renderRest();
    CHECK(peakLevel(whole) > 0.1f);

    std::vector<float> pieces(whole.size());
    std::size_t done = 0;
    std::size_t size = 2;
    while (done < pieces.size()) {
        const std::size_t n = std::min(size, pieces.size() - done);
        CHECK(many.render(std::span<float>(pieces).subspan(done, n)) == n / 2);
        done += n;
        size = size * 5 % 9998 + 2;
    }
    CHECK(many.render(std::span<float>(pieces).subspan(0, 2)) == 0); // at the end
    CHECK(pieces == whole);
    CHECK(many.log() == one.log());
    CHECK(one.log().size() == sequence.events().size());
}

TEST_CASE("OfflineRenderer: a loud mix stays within full scale", "[audio][render]") {
    std::vector<Track> organs;
    for (int t = 0; t < 8; ++t) {
        organs.push_back(track("Organ", InstrumentKind::Keyboard, 16));
    }
    Song s = song(std::move(organs), 1);
    for (std::size_t t = 0; t < 8; ++t) {
        beats(s, t, 0) = {beat(NoteValue::Whole, {48, 52, 55, 60, 64, 67}, Dynamic::FFF)};
    }
    const Sequence sequence(s);
    OfflineRenderer renderer(s, sequence);
    const std::vector<float> stereo = renderer.renderRest();
    CHECK(peakLevel(stereo) > 0.75f);
    CHECK(peakLevel(stereo) <= 1.0f);
}

TEST_CASE("OfflineRenderer: an empty song renders only the tail, silent", "[audio][render]") {
    const Song s = song({track("Piano", InstrumentKind::Keyboard, 0)}, 0);
    const Sequence sequence(s);
    OfflineRenderer renderer(s, sequence, {.sampleRate = kRate, .tail = 0.5, .logEvents = true});
    const std::vector<float> stereo = renderer.renderRest();
    CHECK(stereo.size() == static_cast<std::size_t>(2 * frame(0.5)));
    CHECK(peakLevel(stereo) == 0.0f);
    CHECK(renderer.log().empty());
}
