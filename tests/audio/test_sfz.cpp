// SPDX-License-Identifier: AGPL-3.0-or-later
// SFZ instruments as text (ADR-0066).
#include <elara/audio/sfz.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::audio::parseSfz;
using elara::audio::SfzCcRange;
using elara::audio::SfzIncludeReader;
using elara::audio::sfzKey;
using elara::audio::SfzLoopMode;
using elara::audio::SfzProgram;
using elara::audio::SfzTrigger;

namespace {

// Includes from a map of file names to their text.
SfzIncludeReader files(std::map<std::string, std::string> texts) {
    return [texts = std::move(texts)](
               std::string_view path) -> std::expected<std::string, std::string> {
        const auto found = texts.find(std::string(path));
        if (found == texts.end()) {
            return std::unexpected(std::string("no file ") + std::string(path));
        }
        return found->second;
    };
}

SfzProgram parse(std::string_view text, std::map<std::string, std::string> includes = {}) {
    auto program = parseSfz(text, files(std::move(includes)));
    REQUIRE(program.has_value());
    return *std::move(program);
}

} // namespace

TEST_CASE("SFZ: keys are numbers or note names with middle C as c4", "[audio][sfz]") {
    CHECK(sfzKey("60") == 60);
    CHECK(sfzKey("c4") == 60);
    CHECK(sfzKey("C4") == 60);
    CHECK(sfzKey("c#4") == 61);
    CHECK(sfzKey("db4") == 61);
    CHECK(sfzKey("a4") == 69);
    CHECK(sfzKey("e2") == 40);
    CHECK(sfzKey("c-1") == 0);
    CHECK(sfzKey("g9") == 127);
    CHECK_FALSE(sfzKey("g#9").has_value());
    CHECK_FALSE(sfzKey("h4").has_value());
    CHECK_FALSE(sfzKey("").has_value());
    CHECK_FALSE(sfzKey("128").has_value());
}

TEST_CASE("SFZ: a region takes the opcodes of the headers above it", "[audio][sfz]") {
    const SfzProgram program = parse(R"(
<global> ampeg_release=0.5 volume=-3
<group> lovel=1 hivel=64
<region> sample=soft_e2.wav key=e2
<region> sample=soft_a2.wav lokey=a2 hikey=c3 pitch_keycenter=a2 volume=-6
<group> lovel=65
<region> sample=loud_e2.wav key=40 tune=-12 transpose=12
)");
    REQUIRE(program.regions.size() == 3);
    const auto& soft = program.regions[0];
    CHECK(soft.sample == "soft_e2.wav");
    CHECK(soft.loKey == 40);
    CHECK(soft.hiKey == 40);
    CHECK(soft.keyCenter == 40);
    CHECK(soft.loVel == 1);
    CHECK(soft.hiVel == 64);
    CHECK_THAT(soft.ampegRelease, WithinAbs(0.5, 1e-12));
    CHECK_THAT(soft.volume, WithinAbs(-3.0, 1e-12));

    const auto& wide = program.regions[1];
    CHECK(wide.loKey == 45);
    CHECK(wide.hiKey == 48);
    CHECK(wide.keyCenter == 45);
    CHECK_THAT(wide.volume, WithinAbs(-6.0, 1e-12)); // the region's own wins

    const auto& loud = program.regions[2];
    CHECK(loud.loVel == 65); // a new group forgets the old one's opcodes...
    CHECK(loud.hiVel == 127);
    CHECK_THAT(loud.ampegRelease, WithinAbs(0.5, 1e-12)); // ...but not the global ones
    CHECK_THAT(loud.tune, WithinAbs(-12.0, 1e-12));
    CHECK(loud.transpose == 12);
}

TEST_CASE("SFZ: a master sits between the global header and its groups", "[audio][sfz]") {
    const SfzProgram program = parse(R"(
<global> amplitude=50
<master> hicc118=63
<group> seq_length=2
<region> sample=a.wav seq_position=1
<master> locc118=64
<region> sample=b.wav
)");
    REQUIRE(program.regions.size() == 2);
    CHECK(program.regions[0].ccRanges == std::vector<SfzCcRange>{{.cc = 118, .lo = 0, .hi = 63}});
    CHECK(program.regions[0].seqLength == 2);
    CHECK(program.regions[1].ccRanges == std::vector<SfzCcRange>{{.cc = 118, .lo = 64, .hi = 127}});
    CHECK(program.regions[1].seqLength == 1); // the new master dropped the group
    CHECK_THAT(program.regions[1].amplitude, WithinAbs(50.0, 1e-12));
}

TEST_CASE("SFZ: sample paths keep their spaces and take the default path", "[audio][sfz]") {
    const SfzProgram program = parse(R"(
<control> default_path=Samples\Clean Guitar/
<region> sample=open e\low string.wav lokey=40 hikey=44
<region>sample=*sine
)");
    REQUIRE(program.regions.size() == 2);
    CHECK(program.regions[0].sample == "Samples/Clean Guitar/open e/low string.wav");
    CHECK(program.regions[0].hiKey == 44);
    CHECK(program.regions[1].sample == "*sine"); // generators take no path
}

TEST_CASE("SFZ: comments, defines and includes", "[audio][sfz]") {
    const SfzProgram program = parse(R"(// a guitar
#define $VEL 80
/* a block
   comment <region> sample=no.wav */
<group> hivel=$VEL // soft notes
#include "maps/soft.sfz"
<region> sample=last.wav
)",
                                     {{"maps/soft.sfz", "<region> sample=../s/soft.wav key=50\n"}});
    REQUIRE(program.regions.size() == 2);
    CHECK(program.regions[0].sample == "../s/soft.wav");
    CHECK(program.regions[0].hiVel == 80);
    CHECK(program.regions[0].loKey == 50);
    CHECK(program.regions[1].sample == "last.wav");
    CHECK(program.regions[1].hiVel == 80); // the included file's region did not end the group
}

TEST_CASE("SFZ: an include that cannot be read is an error naming it", "[audio][sfz]") {
    const auto program = parseSfz("#include \"missing.sfz\"\n", files({}));
    REQUIRE_FALSE(program.has_value());
    CHECK(program.error().find("missing.sfz") != std::string::npos);
}

TEST_CASE("SFZ: an include of itself stops instead of looping forever", "[audio][sfz]") {
    const auto program =
        parseSfz("#include \"self.sfz\"\n", files({{"self.sfz", "#include \"self.sfz\"\n"}}));
    REQUIRE_FALSE(program.has_value());
    CHECK(program.error().find("deep") != std::string::npos);
}

TEST_CASE("SFZ: envelope, loop, round robin, triggers, groups and keyswitches", "[audio][sfz]") {
    const SfzProgram program = parse(R"(
<control> set_cc70=20 set_cc1=127
<region> sample=a.wav ampeg_delay=0.1 ampeg_attack=0.01 ampeg_hold=0.2 ampeg_decay=1.5
  ampeg_sustain=40 ampeg_release=0.3 loop_mode=loop_continuous loop_start=100 loop_end=900
  offset=10 end=1000 seq_length=3 seq_position=2 group=1 off_by=2 off_mode=normal
  sw_lokey=24 sw_hikey=26 sw_last=25 sw_default=24 amp_veltrack=50 amp_velcurve_64=0.3
  amp_keytrack=-1.5 amp_keycenter=50 pitch_keytrack=0
<region> sample=rel.wav trigger=release rt_decay=6 loopmode=one_shot
<region> sample=leg.wav trigger=legato
<region> sample=first.wav trigger=first
)");
    REQUIRE(program.regions.size() == 4);
    CHECK(program.ccDefaults[70] == 20);
    CHECK(program.ccDefaults[1] == 127);
    CHECK(program.ccDefaults[2] == 0);
    const auto& a = program.regions[0];
    CHECK_THAT(a.ampegDelay, WithinAbs(0.1, 1e-12));
    CHECK_THAT(a.ampegAttack, WithinAbs(0.01, 1e-12));
    CHECK_THAT(a.ampegHold, WithinAbs(0.2, 1e-12));
    CHECK_THAT(a.ampegDecay, WithinAbs(1.5, 1e-12));
    CHECK_THAT(a.ampegSustain, WithinAbs(40.0, 1e-12));
    CHECK_THAT(a.ampegRelease, WithinAbs(0.3, 1e-12));
    CHECK(a.loopMode == SfzLoopMode::Continuous);
    CHECK(a.loopModeSet);
    CHECK(a.loopStart == 100);
    CHECK(a.loopEnd == 900);
    CHECK(a.offset == 10);
    CHECK(a.end == 1000);
    CHECK(a.seqLength == 3);
    CHECK(a.seqPosition == 2);
    CHECK(a.group == 1);
    CHECK(a.offBy == 2);
    CHECK_FALSE(a.offFast);
    CHECK(a.swLoKey == 24);
    CHECK(a.swHiKey == 26);
    CHECK(a.swLast == 25);
    CHECK(a.swDefault == 24);
    CHECK_THAT(a.ampVeltrack, WithinAbs(50.0, 1e-12));
    CHECK(a.ampVelcurve == std::vector<std::pair<int, double>>{{64, 0.3}});
    CHECK_THAT(a.ampKeytrack, WithinAbs(-1.5, 1e-12));
    CHECK(a.ampKeycenter == 50);
    CHECK_THAT(a.pitchKeytrack, WithinAbs(0.0, 1e-12));

    CHECK(program.regions[1].trigger == SfzTrigger::Release);
    CHECK_THAT(program.regions[1].rtDecay, WithinAbs(6.0, 1e-12));
    CHECK(program.regions[1].loopMode == SfzLoopMode::OneShot);
    CHECK(program.regions[2].trigger == SfzTrigger::Legato);
    CHECK(program.regions[3].trigger == SfzTrigger::First);
    CHECK_FALSE(program.regions[3].loopModeSet);
}

TEST_CASE("SFZ: opcodes Elara does not play are listed once, by kind", "[audio][sfz]") {
    const SfzProgram program = parse(R"(
<control> label_cc70=Muting
<curve> curve_index=1 v000=0 v127=1
<region> sample=a.wav lfo02_pitch_oncc113=104 lfo03_pitch_oncc113=50 pan=-20 fil_type=lpf_2p
<effect> type=fverb
<region> sample=b.wav pan=10
)");
    REQUIRE(program.regions.size() == 2);
    CHECK(program.unknownOpcodes ==
          std::vector<std::string>{"fil_type", "lfoN_pitch_onccN", "pan"});
    CHECK(program.skippedHeaders == std::vector<std::string>{"curve", "effect"});
}

TEST_CASE("SFZ: regions without a sample are left out", "[audio][sfz]") {
    const SfzProgram program = parse("<region> key=60\n<region> sample=a.wav\n");
    REQUIRE(program.regions.size() == 1);
    CHECK(program.regions[0].sample == "a.wav");
}

TEST_CASE("SFZ: controllers move amplitude, volume, tune and the envelope", "[audio][sfz]") {
    using elara::audio::SfzCcModulation;
    using elara::audio::SfzTarget;
    const SfzProgram program = parse(
        "<region> sample=a.wav amplitude_oncc100=100 amplitude_cc27=50 ampeg_releasecc27=0.5 "
        "ampeg_sustain_oncc70=-100 tune_cc1=100 volume_oncc7=-6 gain_cc7=-3 sw_label=Twang\n");
    REQUIRE(program.regions.size() == 1);
    CHECK(program.regions[0].ccModulations ==
          std::vector<SfzCcModulation>{
              {.target = SfzTarget::Amplitude, .cc = 100, .depth = 100.0},
              {.target = SfzTarget::Amplitude, .cc = 27, .depth = 50.0},
              {.target = SfzTarget::AmpegRelease, .cc = 27, .depth = 0.5},
              {.target = SfzTarget::AmpegSustain, .cc = 70, .depth = -100.0},
              {.target = SfzTarget::Tune, .cc = 1, .depth = 100.0},
              {.target = SfzTarget::Volume, .cc = 7, .depth = -3.0}, // the later one wins
          });
    CHECK(program.unknownOpcodes.empty());
}

TEST_CASE("SFZ: keyswitch and controller names are kept", "[audio][sfz]") {
    const SfzProgram program = parse(R"(
<control> label_cc70=Muting label_cc1=Mod wheel
<group> sw_last=36 sw_label=Palm mute
<region> sample=a.wav
)");
    REQUIRE(program.regions.size() == 1);
    CHECK(program.ccLabels[70] == "Muting");
    CHECK(program.ccLabels[1] == "Mod wheel");
    CHECK(program.ccLabels[2].empty());
    CHECK(program.regions[0].swLabel == "Palm mute");
    CHECK(program.unknownOpcodes.empty());
}
