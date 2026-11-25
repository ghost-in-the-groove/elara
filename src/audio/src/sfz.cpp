// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/sfz.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace elara::audio {
namespace {

constexpr int kMaxIncludeDepth = 16;

bool isNameChar(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string lower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return result;
}

// A whole number at the start of `text`; what follows it is ignored, as
// players ignore it ("64 " or "100.0" read as 64 and 100).
template <typename T>
std::optional<T> number(std::string_view text) noexcept {
    text = trim(text);
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
    }
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end == text.data()) {
        return std::nullopt;
    }
    return value;
}

// A decimal such as "-12.5" or ".25", read by hand: from_chars for floating
// point is not on every toolchain yet, and strtod follows the locale, which
// the app sets from the system's.
template <>
std::optional<double> number<double>(std::string_view text) noexcept {
    text = trim(text);
    double sign = 1.0;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        sign = text.front() == '-' ? -1.0 : 1.0;
        text.remove_prefix(1);
    }
    // The digits as one whole number and a power of ten, divided once at the
    // end, so "0.3" is the double nearest 0.3.
    std::uint64_t mantissa = 0;
    double scale = 1.0;
    bool digits = false;
    bool fraction = false;
    for (const char c : text) {
        if (c == '.' && !fraction) {
            fraction = true;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            break;
        }
        digits = true;
        if (mantissa < 100'000'000'000'000'000ULL) {
            mantissa = mantissa * 10 + static_cast<std::uint64_t>(c - '0');
            if (fraction) {
                scale *= 10.0;
            }
        } else if (!fraction) {
            scale /= 10.0; // a whole part too long to hold: drop its last digits
        }
    }
    if (!digits) {
        return std::nullopt;
    }
    return sign * static_cast<double>(mantissa) / scale;
}

// The opcode's name with every run of digits as "N": "lfo02_pitch_oncc113"
// is "lfoN_pitch_onccN".
std::string kindOf(std::string_view name) {
    std::string kind;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(name[i])) != 0) {
            if (kind.empty() || kind.back() != 'N') {
                kind += 'N';
            }
            while (i + 1 < name.size() &&
                   std::isdigit(static_cast<unsigned char>(name[i + 1])) != 0) {
                ++i;
            }
        } else {
            kind += name[i];
        }
    }
    return kind;
}

// The number at the end of `name` after `prefix` ("locc64" -> 64), when the
// name is that prefix and digits only.
std::optional<int> suffixNumber(std::string_view name, std::string_view prefix) noexcept {
    if (!name.starts_with(prefix) || name.size() == prefix.size()) {
        return std::nullopt;
    }
    const std::string_view digits = name.substr(prefix.size());
    if (!std::ranges::all_of(
            digits, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        return std::nullopt;
    }
    return number<int>(digits);
}

using Opcode = std::pair<std::string, std::string>;

enum class Level : std::uint8_t { Control, Global, Master, Group, Region, Skipped };

class Parser {
public:
    explicit Parser(const SfzIncludeReader& include) : include_(include) {}

    std::expected<void, std::string> parse(std::string_view text, int depth) {
        if (depth > kMaxIncludeDepth) {
            return std::unexpected(
                std::format("includes nest more than {} deep", kMaxIncludeDepth));
        }
        const std::string clean = withoutComments(text);
        std::size_t start = 0;
        while (start <= clean.size()) {
            const std::size_t end = std::min(clean.find('\n', start), clean.size());
            const std::string_view line = trim(std::string_view(clean).substr(start, end - start));
            start = end + 1;
            if (line.starts_with("#define")) {
                define(line.substr(7));
            } else if (line.starts_with("#include")) {
                if (auto included = includeFile(line.substr(8), depth); !included) {
                    return included;
                }
            } else if (!line.empty()) {
                scan(substituted(line));
            }
        }
        return {};
    }

    SfzProgram finish() {
        endRegion();
        program_.unknownOpcodes.assign(unknown_.begin(), unknown_.end());
        program_.skippedHeaders.assign(skipped_.begin(), skipped_.end());
        return std::move(program_);
    }

private:
    // The text with `//` comments to the end of their line and `/* */` blocks
    // blanked out; lines stay where they were.
    static std::string withoutComments(std::string_view text) {
        std::string result;
        result.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
                while (i < text.size() && text[i] != '\n') {
                    ++i;
                }
                if (i < text.size()) {
                    result += '\n';
                }
            } else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
                i += 2;
                while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
                    if (text[i] == '\n') {
                        result += '\n';
                    }
                    ++i;
                }
                ++i;
                result += ' ';
            } else if (text[i] == '\r') {
                result += ' ';
            } else {
                result += text[i];
            }
        }
        return result;
    }

    void define(std::string_view rest) {
        rest = trim(rest);
        const std::size_t space = rest.find_first_of(" \t");
        if (space == std::string_view::npos || !rest.starts_with('$')) {
            return;
        }
        defines_.emplace_back(std::string(rest.substr(0, space)),
                              std::string(trim(rest.substr(space))));
        // Longer names first, so "$VEL" never eats the start of "$VELOCITY".
        std::ranges::stable_sort(defines_, [](const Opcode& a, const Opcode& b) {
            return a.first.size() > b.first.size();
        });
    }

    std::string substituted(std::string_view line) const {
        std::string result(line);
        if (result.find('$') == std::string::npos) {
            return result;
        }
        for (const auto& [name, value] : defines_) {
            for (std::size_t at = result.find(name); at != std::string::npos;
                 at = result.find(name, at + value.size())) {
                result.replace(at, name.size(), value);
            }
        }
        return result;
    }

    std::expected<void, std::string> includeFile(std::string_view rest, int depth) {
        const std::string path = substituted(trim(rest));
        std::string_view name = path;
        if (name.size() >= 2 && name.front() == '"') {
            name = name.substr(1, name.find('"', 1) - 1);
        }
        auto text = include_(name);
        if (!text) {
            return std::unexpected(std::format("cannot include {}: {}", name, text.error()));
        }
        return parse(*text, depth + 1);
    }

    // Headers and opcodes on one line. A value runs to the next opcode or
    // header, so a sample path may hold spaces.
    void scan(std::string_view line) {
        std::size_t i = 0;
        while (i < line.size()) {
            if (line[i] == '<') {
                const std::size_t close = line.find('>', i);
                if (close == std::string_view::npos) {
                    return;
                }
                header(lower(trim(line.substr(i + 1, close - i - 1))));
                i = close + 1;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(line[i])) != 0) {
                ++i;
                continue;
            }
            const std::size_t equals = line.find('=', i);
            if (equals == std::string_view::npos) {
                return;
            }
            const std::string name = lower(trim(line.substr(i, equals - i)));
            const std::size_t valueStart = equals + 1;
            const std::size_t valueEnd = nextToken(line, valueStart);
            opcode(name, std::string(trim(line.substr(valueStart, valueEnd - valueStart))));
            i = valueEnd;
        }
    }

    // Where the value starting at `from` ends: at the next header, or at the
    // space before the next `name=`.
    static std::size_t nextToken(std::string_view line, std::size_t from) {
        for (std::size_t i = from; i < line.size(); ++i) {
            if (line[i] == '<') {
                return i;
            }
            if (std::isspace(static_cast<unsigned char>(line[i])) == 0) {
                continue;
            }
            std::size_t name = i;
            while (name < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[name])) != 0) {
                ++name;
            }
            std::size_t after = name;
            while (after < line.size() && isNameChar(line[after])) {
                ++after;
            }
            if (after > name && after < line.size() && line[after] == '=') {
                return i;
            }
        }
        return line.size();
    }

    void header(const std::string& name) {
        endRegion();
        if (name == "control") {
            level_ = Level::Control;
        } else if (name == "global") {
            level_ = Level::Global;
            global_.clear();
            master_.clear();
            group_.clear();
        } else if (name == "master") {
            level_ = Level::Master;
            master_.clear();
            group_.clear();
        } else if (name == "group") {
            level_ = Level::Group;
            group_.clear();
        } else if (name == "region") {
            level_ = Level::Region;
            inRegion_ = true;
            region_.clear();
        } else {
            level_ = Level::Skipped;
            skipped_.insert(name);
        }
    }

    void opcode(const std::string& name, std::string value) {
        switch (level_) {
        case Level::Control:
            control(name, value);
            break;
        case Level::Global:
            global_.emplace_back(name, std::move(value));
            break;
        case Level::Master:
            master_.emplace_back(name, std::move(value));
            break;
        case Level::Group:
            group_.emplace_back(name, std::move(value));
            break;
        case Level::Region:
            region_.emplace_back(name, std::move(value));
            break;
        case Level::Skipped:
            break;
        }
    }

    void control(const std::string& name, const std::string& value) {
        if (name == "default_path") {
            defaultPath_ = value;
            std::ranges::replace(defaultPath_, '\\', '/');
        } else if (const auto cc = suffixNumber(name, "set_cc"); cc) {
            if (*cc >= 0 && *cc < 128) {
                program_.ccDefaults[static_cast<std::size_t>(*cc)] =
                    std::clamp(number<int>(value).value_or(0), 0, 127);
            }
        } else if (const auto label = suffixNumber(name, "label_cc"); label) {
            if (*label >= 0 && *label < 128) {
                program_.ccLabels[static_cast<std::size_t>(*label)] = value;
            }
        } else {
            unknown_.insert(kindOf(name));
        }
    }

    void endRegion() {
        if (!inRegion_) {
            return;
        }
        inRegion_ = false;
        SfzRegion region;
        for (const auto* opcodes : {&global_, &master_, &group_, &region_}) {
            for (const auto& [name, value] : *opcodes) {
                if (!apply(region, name, value)) {
                    unknown_.insert(kindOf(name));
                }
            }
        }
        if (!region.sample.empty()) {
            if (!region.sample.starts_with('*')) {
                region.sample = defaultPath_ + region.sample;
            }
            program_.regions.push_back(std::move(region));
        }
    }

    // Sets one opcode on a region. False for one Elara does not play.
    static bool apply(SfzRegion& region, const std::string& name, const std::string& value) {
        const auto key = [&value] {
            return sfzKey(value);
        };
        const auto real = [&value] {
            return number<double>(value);
        };
        const auto whole = [&value] {
            return number<std::int64_t>(value);
        };
        const auto setKey = [&](int& field) {
            if (const auto k = key(); k) {
                field = *k;
            }
        };
        const auto setReal = [&](double& field) {
            if (const auto v = real(); v) {
                field = *v;
            }
        };
        const auto setInt = [&](int& field) {
            if (const auto v = whole(); v) {
                field = static_cast<int>(std::clamp<std::int64_t>(*v, -100000, 100000));
            }
        };
        const auto setWhole = [&](std::int64_t& field) {
            if (const auto v = whole(); v) {
                field = *v;
            }
        };

        if (const auto modulation = ccModulation(name); modulation) {
            if (const auto depth = real();
                depth && modulation->second >= 0 && modulation->second < 128) {
                auto& list = region.ccModulations;
                std::erase_if(list, [&](const SfzCcModulation& m) {
                    return m.target == modulation->first && m.cc == modulation->second;
                });
                list.push_back(
                    {.target = modulation->first, .cc = modulation->second, .depth = *depth});
            }
        } else if (name == "sample") {
            region.sample = value;
            std::ranges::replace(region.sample, '\\', '/');
        } else if (name == "key") {
            if (const auto k = key(); k) {
                region.loKey = region.hiKey = region.keyCenter = *k;
            }
        } else if (name == "lokey") {
            setKey(region.loKey);
        } else if (name == "hikey") {
            setKey(region.hiKey);
        } else if (name == "lovel") {
            setInt(region.loVel);
        } else if (name == "hivel") {
            setInt(region.hiVel);
        } else if (name == "pitch_keycenter") {
            setKey(region.keyCenter);
        } else if (name == "tune" || name == "pitch") {
            setReal(region.tune);
        } else if (name == "transpose") {
            setInt(region.transpose);
        } else if (name == "pitch_keytrack") {
            setReal(region.pitchKeytrack);
        } else if (name == "volume" || name == "gain") {
            setReal(region.volume);
        } else if (name == "amplitude") {
            setReal(region.amplitude);
        } else if (name == "amp_veltrack") {
            setReal(region.ampVeltrack);
        } else if (name == "amp_keytrack") {
            setReal(region.ampKeytrack);
        } else if (name == "amp_keycenter") {
            setKey(region.ampKeycenter);
        } else if (const auto velocity = suffixNumber(name, "amp_velcurve_"); velocity) {
            const auto gain = real();
            if (!gain || *velocity < 0 || *velocity > 127) {
                return true;
            }
            std::erase_if(region.ampVelcurve,
                          [&](const auto& point) { return point.first == *velocity; });
            region.ampVelcurve.emplace_back(*velocity, std::clamp(*gain, 0.0, 1.0));
            std::ranges::sort(region.ampVelcurve);
        } else if (name == "ampeg_delay") {
            setReal(region.ampegDelay);
        } else if (name == "ampeg_attack") {
            setReal(region.ampegAttack);
        } else if (name == "ampeg_hold") {
            setReal(region.ampegHold);
        } else if (name == "ampeg_decay") {
            setReal(region.ampegDecay);
        } else if (name == "ampeg_sustain") {
            setReal(region.ampegSustain);
        } else if (name == "ampeg_release") {
            setReal(region.ampegRelease);
        } else if (name == "seq_length") {
            setInt(region.seqLength);
        } else if (name == "seq_position") {
            setInt(region.seqPosition);
        } else if (name == "lorand") {
            setReal(region.loRand);
        } else if (name == "hirand") {
            setReal(region.hiRand);
        } else if (name == "trigger") {
            const std::string kind = lower(value);
            region.trigger = kind == "release" || kind == "release_key" ? SfzTrigger::Release
                             : kind == "first"                          ? SfzTrigger::First
                             : kind == "legato"                         ? SfzTrigger::Legato
                                                                        : SfzTrigger::Attack;
        } else if (const auto lo = suffixNumber(name, "locc"); lo) {
            ccRange(region, *lo).lo = std::clamp(number<int>(value).value_or(0), 0, 127);
        } else if (const auto hi = suffixNumber(name, "hicc"); hi) {
            ccRange(region, *hi).hi = std::clamp(number<int>(value).value_or(127), 0, 127);
        } else if (name == "sw_lokey") {
            setKey(region.swLoKey);
        } else if (name == "sw_hikey") {
            setKey(region.swHiKey);
        } else if (name == "sw_last") {
            setKey(region.swLast);
        } else if (name == "sw_default") {
            setKey(region.swDefault);
        } else if (name == "group" || name == "polyphony_group") {
            setWhole(region.group);
        } else if (name == "off_by") {
            setWhole(region.offBy);
        } else if (name == "off_mode") {
            region.offFast = lower(value) != "normal";
        } else if (name == "loop_mode" || name == "loopmode") {
            const std::string mode = lower(value);
            region.loopModeSet = true;
            region.loopMode = mode == "one_shot"          ? SfzLoopMode::OneShot
                              : mode == "loop_continuous" ? SfzLoopMode::Continuous
                              : mode == "loop_sustain"    ? SfzLoopMode::Sustain
                                                          : SfzLoopMode::NoLoop;
        } else if (name == "loop_start" || name == "loopstart") {
            setWhole(region.loopStart);
        } else if (name == "loop_end" || name == "loopend") {
            setWhole(region.loopEnd);
        } else if (name == "offset") {
            setWhole(region.offset);
        } else if (name == "end") {
            setWhole(region.end);
        } else if (name == "rt_decay") {
            setReal(region.rtDecay);
        } else if (name == "sw_label") {
            region.swLabel = value;
        } else if (name.starts_with("bend_") || name.starts_with("amplitude_smoothcc")) {
            // Nothing to play: the pitch wheel's range and smoothing (Elara
            // bends each note by its own glides); smoothing of a controller,
            // which holds its value.
        } else {
            return false;
        }
        return true;
    }

    // What a controller opcode moves and the controller: "amplitude_oncc100"
    // and "amplitude_cc100" are both {Amplitude, 100}.
    static std::optional<std::pair<SfzTarget, int>> ccModulation(std::string_view name) {
        struct Kind {
            std::string_view prefix;
            SfzTarget target;
        };
        static constexpr std::array<Kind, 22> kKinds{{
            {"amplitude_oncc", SfzTarget::Amplitude},
            {"amplitude_cc", SfzTarget::Amplitude},
            {"volume_oncc", SfzTarget::Volume},
            {"gain_oncc", SfzTarget::Volume},
            {"gain_cc", SfzTarget::Volume},
            {"tune_oncc", SfzTarget::Tune},
            {"tune_cc", SfzTarget::Tune},
            {"pitch_oncc", SfzTarget::Tune},
            {"pitch_cc", SfzTarget::Tune},
            {"ampeg_delay_oncc", SfzTarget::AmpegDelay},
            {"ampeg_delaycc", SfzTarget::AmpegDelay},
            {"ampeg_attack_oncc", SfzTarget::AmpegAttack},
            {"ampeg_attackcc", SfzTarget::AmpegAttack},
            {"ampeg_hold_oncc", SfzTarget::AmpegHold},
            {"ampeg_holdcc", SfzTarget::AmpegHold},
            {"ampeg_decay_oncc", SfzTarget::AmpegDecay},
            {"ampeg_decaycc", SfzTarget::AmpegDecay},
            {"ampeg_sustain_oncc", SfzTarget::AmpegSustain},
            {"ampeg_sustaincc", SfzTarget::AmpegSustain},
            {"ampeg_release_oncc", SfzTarget::AmpegRelease},
            {"ampeg_releasecc", SfzTarget::AmpegRelease},
            {"volume_cc", SfzTarget::Volume},
        }};
        for (const Kind& kind : kKinds) {
            if (const auto cc = suffixNumber(name, kind.prefix); cc) {
                return std::pair{kind.target, *cc};
            }
        }
        return std::nullopt;
    }

    static SfzCcRange& ccRange(SfzRegion& region, int cc) {
        const auto found = std::ranges::find(region.ccRanges, cc, &SfzCcRange::cc);
        if (found != region.ccRanges.end()) {
            return *found;
        }
        return region.ccRanges.emplace_back(SfzCcRange{.cc = cc});
    }

    const SfzIncludeReader& include_;
    SfzProgram program_;
    Level level_ = Level::Control;
    std::vector<Opcode> global_;
    std::vector<Opcode> master_;
    std::vector<Opcode> group_;
    std::vector<Opcode> region_;
    bool inRegion_ = false;
    std::string defaultPath_;
    std::vector<Opcode> defines_;
    std::set<std::string> unknown_;
    std::set<std::string> skipped_;
};

} // namespace

std::optional<int> sfzKey(std::string_view text) {
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    if (std::isdigit(static_cast<unsigned char>(text.front())) != 0) {
        const auto key = number<int>(text);
        if (!key || *key < 0 || *key > 127) {
            return std::nullopt;
        }
        return key;
    }
    static constexpr std::array<int, 7> kSteps{9, 11, 0, 2, 4, 5, 7}; // a b c d e f g
    const char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(text.front())));
    if (letter < 'a' || letter > 'g') {
        return std::nullopt;
    }
    int semitone = kSteps[static_cast<std::size_t>(letter - 'a')];
    text.remove_prefix(1);
    if (!text.empty() && text.front() == '#') {
        ++semitone;
        text.remove_prefix(1);
    } else if (!text.empty() && text.front() == 'b') {
        --semitone;
        text.remove_prefix(1);
    }
    const auto octave = number<int>(text);
    if (!octave) {
        return std::nullopt;
    }
    const int key = (*octave + 1) * 12 + semitone;
    if (key < 0 || key > 127) {
        return std::nullopt;
    }
    return key;
}

std::expected<SfzProgram, std::string> parseSfz(std::string_view text,
                                                const SfzIncludeReader& include) {
    Parser parser(include);
    if (auto parsed = parser.parse(text, 0); !parsed) {
        return std::unexpected(parsed.error());
    }
    return parser.finish();
}

std::expected<SfzProgram, std::string> readSfz(const std::filesystem::path& path) {
    const auto read =
        [](const std::filesystem::path& file) -> std::expected<std::string, std::string> {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            return std::unexpected(std::format("cannot open {}", file.generic_string()));
        }
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    const auto text = read(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    const std::filesystem::path folder = path.parent_path();
    auto program = parseSfz(*text, [&](std::string_view name) {
        return read(folder / std::filesystem::path(std::string(name)));
    });
    if (!program) {
        return std::unexpected(std::format("{}: {}", path.generic_string(), program.error()));
    }
    return program;
}

} // namespace elara::audio
