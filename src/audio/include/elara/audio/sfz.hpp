// SPDX-License-Identifier: AGPL-3.0-or-later
// SFZ instruments (ADR-0066): the text format of sample-based instruments,
// read into regions that say which sample a key and velocity play and how.
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::audio {

/// What strikes a region.
enum class SfzTrigger : std::uint8_t {
    Attack,  ///< a key going down (the default)
    Release, ///< a key coming up: release noises
    First,   ///< a key going down while no other is held
    Legato,  ///< a key going down while another is held
};

/// How a region plays its sample.
enum class SfzLoopMode : std::uint8_t {
    NoLoop,     ///< to its end or until the release ends (the default)
    OneShot,    ///< to its end, whatever the key does
    Continuous, ///< looping from loop_start to loop_end until the release ends
    Sustain,    ///< looping while the key is held, then on to its end
};

/// A controller a region needs within a range to play (locc / hicc).
struct SfzCcRange {
    int cc = 0;
    int lo = 0;
    int hi = 127;

    friend bool operator==(const SfzCcRange&, const SfzCcRange&) = default;
};

/// What a controller moves (`amplitude_oncc64`, `ampeg_release_oncc1`...).
enum class SfzTarget : std::uint8_t {
    Amplitude,  ///< multiplies the amplitude by depth % of the controller's value
    Volume,     ///< adds depth dB at the controller's top
    Tune,       ///< adds depth cents
    AmpegDelay, ///< adds depth seconds
    AmpegAttack,
    AmpegHold,
    AmpegDecay,
    AmpegSustain, ///< adds depth percent
    AmpegRelease,
};

/// A controller moving one of a region's values, by `depth` at its top (127)
/// and in proportion below. Elara's controllers keep their `set_ccN` values,
/// so the move is made when a note starts.
struct SfzCcModulation {
    SfzTarget target = SfzTarget::Amplitude;
    int cc = 0;
    double depth = 0.0;

    friend bool operator==(const SfzCcModulation&, const SfzCcModulation&) = default;
};

/// One region: a sample and the keys, velocities and controllers that play
/// it, with the opcodes of its headers above folded in.
struct SfzRegion {
    /// The sample's path as the file writes it, with `default_path` in front
    /// and `/` between folders, or a generator ("*sine", "*silence").
    std::string sample;
    int loKey = 0;
    int hiKey = 127;
    int loVel = 1;
    int hiVel = 127;
    int keyCenter = 60;           ///< the key the sample sounds at unchanged
    double tune = 0.0;            ///< cents
    int transpose = 0;            ///< semitones
    double pitchKeytrack = 100.0; ///< cents the pitch moves per key
    double volume = 0.0;          ///< dB
    double amplitude = 100.0;     ///< percent
    double ampVeltrack = 100.0;   ///< percent of the gain velocity sets
    double ampKeytrack = 0.0;     ///< dB per key away from ampKeycenter
    int ampKeycenter = 60;
    /// amp_velcurve_N: the gain (0-1) at velocity N, straight lines between.
    std::vector<std::pair<int, double>> ampVelcurve;
    double ampegDelay = 0.0;     ///< seconds
    double ampegAttack = 0.0;    ///< seconds
    double ampegHold = 0.0;      ///< seconds
    double ampegDecay = 0.0;     ///< seconds
    double ampegSustain = 100.0; ///< percent
    double ampegRelease = 0.001; ///< seconds
    int seqLength = 1;           ///< round robin: the region plays one note in seqLength...
    int seqPosition = 1;         ///< ...the seqPosition-th
    SfzTrigger trigger = SfzTrigger::Attack;
    std::vector<SfzCcRange> ccRanges;
    int swLoKey = -1;       ///< keyswitch range: keys that only switch...
    int swHiKey = -1;       ///< ...(-1: none)
    int swLast = -1;        ///< plays only after this keyswitch (-1: always)
    int swDefault = -1;     ///< the keyswitch before any is struck
    std::string swLabel;    ///< the name of its keyswitch (`sw_label`)
    std::int64_t group = 0; ///< exclusive group...
    std::int64_t offBy = 0; ///< ...whose notes silence this one (0: none)
    bool offFast = true;    ///< silenced at once, or else through its release
    SfzLoopMode loopMode = SfzLoopMode::NoLoop;
    bool loopModeSet = false;    ///< loop_mode was written (else the sample's own loop decides)
    std::int64_t loopStart = -1; ///< frames (-1: the sample's own loop or start)
    std::int64_t loopEnd = -1;   ///< frames (-1: the sample's own loop or end)
    std::int64_t offset = 0;     ///< frames skipped at the start
    std::int64_t end = -1;       ///< the last frame played (-1: the sample's end)
    double rtDecay = 0.0;        ///< dB a release region loses per second the key was held
    std::vector<SfzCcModulation> ccModulations; ///< one per target and controller

    friend bool operator==(const SfzRegion&, const SfzRegion&) = default;
};

/// An SFZ instrument as written: its regions, in file order.
struct SfzProgram {
    std::vector<SfzRegion> regions;
    /// Controller values before any is sent (`set_ccN`), 0 when unset.
    std::array<int, 128> ccDefaults{};
    /// Controller names (`label_ccN`), empty when unnamed.
    std::array<std::string, 128> ccLabels{};
    /// Opcodes Elara does not play, each once and sorted, with the numbers in
    /// their names as "N" ("lfoN_pitch_onccN").
    std::vector<std::string> unknownOpcodes;
    /// Headers Elara skips with what they hold ("curve", "effect"), sorted.
    std::vector<std::string> skippedHeaders;
};

/// Reads the text of an `#include`d file by the path the file writes.
using SfzIncludeReader = std::function<std::expected<std::string, std::string>(std::string_view)>;

/// Parses SFZ text: `<control>`, `<global>`, `<master>`, `<group>` and
/// `<region>` headers, opcodes (values may hold spaces: a sample path runs to
/// the next opcode), `//` and `/* */` comments, `#define $NAME value` and
/// `#include "file"`. Regions without a sample are left out. Fails with a
/// message for an include that cannot be read or nests too deep.
[[nodiscard]] std::expected<SfzProgram, std::string> parseSfz(std::string_view text,
                                                              const SfzIncludeReader& include);

/// Reads and parses an .sfz file; includes are found from its folder.
[[nodiscard]] std::expected<SfzProgram, std::string> readSfz(const std::filesystem::path& path);

/// A key as SFZ writes it: a MIDI number ("60") or a note name with its
/// octave, middle C being "c4" ("c#4", "db4", "C-1" for key 0). Empty for
/// anything else or a key outside 0-127.
[[nodiscard]] std::optional<int> sfzKey(std::string_view text);

} // namespace elara::audio
