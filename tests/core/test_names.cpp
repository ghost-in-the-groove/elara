// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/effects.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/names.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tempo.hpp>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace core = elara::core;

namespace {

template <core::NamedEnum Enum>
std::vector<std::string_view> namesOf() {
    std::vector<std::string_view> names;
    for (const Enum value : core::enumValues<Enum>()) {
        names.push_back(core::nameOf(value));
    }
    return names;
}

} // namespace

TEMPLATE_TEST_CASE("every enumerator goes through its name and back", "[names]", core::SlideIn,
                   core::SlideOut, core::HarmonicKind, core::Ornament, core::Finger, core::Vibrato,
                   core::Accent, core::Dynamic, core::PickStroke, core::Strum, core::BassTechnique,
                   core::Hairpin, core::Fermata, core::Ottava, core::Pedal, core::Breath,
                   core::TripletFeel, core::MeasureRepeat, core::InstrumentKind, core::Clef,
                   core::JumpTo, core::JumpThen, core::NavigationSign, core::Credit,
                   core::NoteEffect) {
    const auto values = core::enumValues<TestType>();
    REQUIRE(values.size() == core::kEnumCount<TestType>);
    std::set<std::string_view> seen;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const TestType value = values[i];
        CHECK(static_cast<std::size_t>(value) == i);
        const std::string_view name = core::nameOf(value);
        CAPTURE(i, name);
        CHECK_FALSE(name.empty());
        CHECK(name.find_first_of(" _ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string_view::npos);
        CHECK(seen.insert(name).second); // no two values share a name
        CHECK(core::parseName<TestType>(name) == value);
        CHECK(core::enumFromIndex<TestType>(static_cast<int>(i)) == value);
    }
    // Past the last value there is neither a value nor a name.
    const auto count = static_cast<int>(core::kEnumCount<TestType>);
    CHECK(core::enumFromIndex<TestType>(count) == std::nullopt);
    CHECK(core::enumFromIndex<TestType>(-1) == std::nullopt);
    CHECK(core::nameOf(static_cast<TestType>(count)).empty());
    CHECK(core::parseName<TestType>("") == std::nullopt);
    CHECK(core::parseName<TestType>("not-a-name") == std::nullopt);
    // Names are matched exactly: every first name starts with a letter.
    std::string upper(core::nameOf(values.front()));
    upper.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(upper.front())));
    CHECK(core::parseName<TestType>(upper) == std::nullopt);
}

TEST_CASE("names keep the spellings files and the command line use", "[names]") {
    // The native file format (ADR-0006) and `elara-cli` read and write these:
    // changing one breaks files already saved and scripts already written.
    using List = std::vector<std::string_view>;
    CHECK(namesOf<core::SlideIn>() == List{"none", "from-below", "from-above"});
    CHECK(namesOf<core::SlideOut>() == List{"none", "shift", "legato", "downwards", "upwards"});
    CHECK(namesOf<core::HarmonicKind>() ==
          List{"none", "natural", "artificial", "pinch", "tapped", "semi"});
    CHECK(namesOf<core::Ornament>() ==
          List{"none", "mordent", "upper-mordent", "turn", "inverted-turn"});
    CHECK(namesOf<core::Finger>() == List{"none", "thumb", "index", "middle", "ring", "little"});
    CHECK(namesOf<core::Vibrato>() == List{"none", "slight", "wide"});
    CHECK(namesOf<core::Accent>() == List{"none", "accent", "marcato"});
    CHECK(namesOf<core::Dynamic>() == List{"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"});
    CHECK(namesOf<core::PickStroke>() == List{"none", "down", "up"});
    CHECK(namesOf<core::Strum>() == List{"none", "down", "up"});
    CHECK(namesOf<core::BassTechnique>() == List{"none", "tap", "slap", "pop"});
    CHECK(namesOf<core::Hairpin>() == List{"none", "crescendo", "diminuendo"});
    CHECK(namesOf<core::Fermata>() == List{"none", "short", "normal", "long"});
    CHECK(namesOf<core::Ottava>() == List{"none", "8va", "8vb", "15ma", "15mb"});
    CHECK(namesOf<core::Pedal>() == List{"none", "down", "change"});
    CHECK(namesOf<core::Breath>() == List{"none", "comma", "caesura"});
    CHECK(namesOf<core::TripletFeel>() == List{"none", "eighth", "sixteenth", "dotted-eighth",
                                               "dotted-sixteenth", "scottish-eighth",
                                               "scottish-sixteenth"});
    CHECK(namesOf<core::MeasureRepeat>() == List{"none", "one", "two-first", "two-second"});
    CHECK(namesOf<core::InstrumentKind>() == List{"fretted", "percussion", "keyboard", "pitched"});
    CHECK(namesOf<core::Clef>() ==
          List{"treble", "treble-8vb", "bass", "bass-8vb", "alto", "tenor", "percussion"});
    CHECK(namesOf<core::JumpTo>() == List{"none", "da-capo", "dal-segno", "dal-segno-segno"});
    CHECK(namesOf<core::JumpThen>() == List{"end", "al-fine", "al-coda", "al-double-coda"});
    CHECK(namesOf<core::NavigationSign>() ==
          List{"segno", "segno-segno", "coda", "double-coda", "fine", "to-coda", "to-double-coda"});
    CHECK(namesOf<core::Credit>() == List{"title", "subtitle", "artist", "album", "words", "music",
                                          "copyright", "transcriber", "notes"});
    CHECK(namesOf<core::NoteEffect>() ==
          List{"tie", "legato", "slide", "bend", "vibrato", "palm-mute", "dead-note", "let-ring",
               "harmonic", "ghost", "accent", "staccato", "tenuto", "staccatissimo"});
}

TEST_CASE("each navigation sign is a flag of its own in the measure header", "[names]") {
    std::vector<bool core::MeasureHeader::*> flags;
    for (const core::NavigationSign sign : core::enumValues<core::NavigationSign>()) {
        core::MeasureHeader header;
        header.*core::signFlag(sign) = true;
        CHECK(header != core::MeasureHeader{});
        CHECK(std::ranges::find(flags, core::signFlag(sign)) == flags.end());
        flags.push_back(core::signFlag(sign));
    }
    CHECK(core::signFlag(core::NavigationSign::Segno) == &core::MeasureHeader::segno);
    CHECK(core::signFlag(core::NavigationSign::ToDoubleCoda) == &core::MeasureHeader::toDoubleCoda);
}

// Names, parsing and counts are constant expressions.
static_assert(core::nameOf(core::Hairpin::Crescendo) == "crescendo");
static_assert(core::parseName<core::Ottava>("15mb") == core::Ottava::QuindicesimaBassa);
static_assert(core::kEnumCount<core::Dynamic> == 8);
static_assert(core::enumFromIndex<core::JumpThen>(4U) == std::nullopt);
static_assert(core::enumValues<core::Pedal>().back() == core::Pedal::Change);
