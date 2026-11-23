// SPDX-License-Identifier: AGPL-3.0-or-later
// Guitar Pro 3, 4 and 5 import (ADR-0010). These versions keep the score as a
// little-endian binary stream: the song's information, a header per measure, the
// tracks, then every track's content measure by measure. The format has no
// public specification; this reader follows the structure of the files.
#include "guitar_pro_binary.hpp"

#include <elara/core/validation.hpp>

#include "general_midi.hpp"
#include "gp_directions.hpp"
#include "midi_shared.hpp"
#include "warnings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::io::detail {

namespace {

using core::Accent;
using core::Beat;
using core::BendCurve;
using core::BendPoint;
using core::Clef;
using core::Duration;
using core::Dynamic;
using core::Fraction;

using core::HarmonicKind;
using core::Instrument;
using core::InstrumentKind;
using core::KeyMode;
using core::KeySignature;
using core::Measure;
using core::MeasureHeader;
using core::Note;
using core::NoteValue;
using core::Pitch;
using core::SlideIn;
using core::SlideOut;
using core::Song;
using core::Staff;
using core::TimeSignature;
using core::Tuning;
using core::Tuplet;
using core::Vibrato;
using core::Voice;

constexpr std::size_t kMaxMeasures = 100'000;
constexpr std::size_t kMaxTracks = 128;
constexpr std::size_t kMaxBeats = 4096; // per voice and measure
constexpr std::size_t kMaxBendPoints = 1000;
constexpr int kStrings = 7; // every track has room for seven strings

// ---- Text ------------------------------------------------------------------------

void appendUtf8(std::string& out, char32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0U | (code >> 6U));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else {
        out += static_cast<char>(0xE0U | (code >> 12U));
        out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    }
}

/// Guitar Pro writes text in Windows-1252: Latin-1 with printable characters
/// in 0x80-0x9F.
std::string utf8FromWindows1252(std::span<const std::uint8_t> bytes) {
    static constexpr std::array<char16_t, 32> kHigh{
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    std::string text;
    text.reserve(bytes.size());
    for (const std::uint8_t byte : bytes) {
        if (byte == 0) {
            continue; // padding
        }
        appendUtf8(text,
                   byte >= 0x80 && byte < 0xA0 ? char32_t{kHigh[byte - 0x80U]} : char32_t{byte});
    }
    return text;
}

std::string trimmed(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// ---- Bytes -----------------------------------------------------------------------

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t position() const noexcept { return at_; }

    std::uint8_t u8() {
        need(1);
        return bytes_[at_++];
    }

    std::int8_t i8() { return static_cast<std::int8_t>(u8()); }

    std::int16_t i16() {
        need(2);
        const auto value = static_cast<std::uint16_t>(bytes_[at_] | (bytes_[at_ + 1] << 8U));
        at_ += 2;
        return static_cast<std::int16_t>(value);
    }

    std::int32_t i32() {
        need(4);
        std::uint32_t value = 0;
        for (std::size_t i = 4; i > 0; --i) {
            value = (value << 8U) | bytes_[at_ + i - 1];
        }
        at_ += 4;
        return static_cast<std::int32_t>(value);
    }

    bool flag() { return u8() != 0; }

    void skip(std::size_t count) {
        need(count);
        at_ += count;
    }

    /// A length byte, then a field of `size` bytes that holds the characters.
    std::string byteString(std::size_t size) {
        const std::size_t length = std::min<std::size_t>(u8(), size);
        std::string result = text(length);
        skip(size - length);
        return result;
    }

    /// `length` characters.
    std::string text(std::size_t length) {
        need(length);
        std::string result = utf8FromWindows1252(bytes_.subspan(at_, length));
        at_ += length;
        return result;
    }

    /// The field's size as a 32-bit number, then a length byte and the
    /// characters.
    std::string intByteString() {
        const std::size_t start = at_;
        const std::int32_t size = i32();
        if (size < 1) {
            throw FormatError{
                std::format("Guitar Pro: damaged file: bad text size at byte {}", start)};
        }
        const auto field = static_cast<std::size_t>(size);
        need(field);
        const std::size_t length = std::min<std::size_t>(bytes_[at_], field - 1);
        std::string text = utf8FromWindows1252(bytes_.subspan(at_ + 1, length));
        at_ += field;
        return text;
    }

    /// A 32-bit length, then the characters.
    std::string intString() {
        const std::size_t start = at_;
        const std::int32_t size = i32();
        if (size < 0) {
            throw FormatError{
                std::format("Guitar Pro: damaged file: bad text size at byte {}", start)};
        }
        const auto length = static_cast<std::size_t>(size);
        need(length);
        std::string text = utf8FromWindows1252(bytes_.subspan(at_, length));
        at_ += length;
        return text;
    }

    /// A count read as a 32-bit number, refused when negative or above `limit`.
    std::size_t count(std::size_t limit, std::string_view what) {
        const std::size_t start = at_;
        const std::int32_t value = i32();
        if (value < 0 || static_cast<std::size_t>(value) > limit) {
            throw FormatError{
                std::format("Guitar Pro: damaged file: {} {} at byte {}", value, what, start)};
        }
        return static_cast<std::size_t>(value);
    }

private:
    void need(std::size_t count) const {
        if (count > bytes_.size() - at_) {
            throw FormatError{
                std::format("Guitar Pro: damaged file: it ends early (byte {})", bytes_.size())};
        }
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t at_ = 0;
};

// ---- Values ----------------------------------------------------------------------

/// Bends and whammy-bar curves: positions count to 60 over the note, values
/// are hundredths of a tone (100 = a full step).
constexpr int kBendPositions = 60;

Dynamic dynamicFor(int code) {
    // 1 (ppp) to 8 (fff); notes without one are played forte.
    return static_cast<Dynamic>(std::clamp(code, 1, 8) - 1);
}

std::optional<NoteValue> tremoloSpeed(int code) {
    switch (code) {
    case 1:
        return NoteValue::Eighth;
    case 2:
        return NoteValue::Sixteenth;
    case 3:
        return NoteValue::ThirtySecond;
    default:
        return std::nullopt;
    }
}

/// What every note of a beat gets from the beat's effects.
struct NoteDefaults {
    Vibrato vibrato = Vibrato::None;
    HarmonicKind harmonic = HarmonicKind::None; // Guitar Pro 3 keeps harmonics per beat
};

/// What a note leaves for the next note on its string, in one voice.
struct StringState {
    int fret = -1;             // the last fret played, for ties that do not repeat it
    bool legatoToNext = false; // a hammer-on or pull-off starts here
};

// One of the 64 MIDI channels of the song's channel table.
struct ChannelData {
    int program = 0;
    int volume = 13; // 0 to 16
    int balance = 8; // 0 (left) to 16 (right)
};

struct TrackData {
    std::string name;
    bool percussion = false;
    bool banjo = false;
    core::TrackMix mix;
    std::vector<int> strings; // open strings as MIDI numbers, string 1 (highest) first
    int capo = 0;
    int program = 0;
    std::vector<Measure> measures;
    std::array<std::array<StringState, kStrings>, 2> voices{};
    long long pitchSum = 0;
    std::size_t noteCount = 0;
};

struct HeaderData {
    TimeSignature time;
    KeySignature key;
    bool repeatStart = false;
    std::uint8_t repeatTimes = 0;
    std::uint8_t alternateEndings = 0;
};

struct TempoMark {
    std::size_t measure = 0;
    Fraction offset;
    int bpm = 120;
};

// ---- The reader ------------------------------------------------------------------

class BinaryReader {
public:
    BinaryReader(std::span<const std::uint8_t> bytes, Warnings& warnings)
        : in_(bytes), warnings_(warnings) {}

    Song read() {
        readVersion();
        readInfo();
        if (version_ < 500 && in_.flag()) {
            warnings_.add({}, "triplet feel dropped");
        }
        if (version_ >= 400) {
            readLyrics();
        }
        if (version_ >= 510) {
            in_.skip(4 + 4 + 11); // master volume and equalizer
        }
        if (version_ >= 500) {
            in_.skip(7 * 4 + 2); // page size, margins, header and footer flags
            for (int i = 0; i < 10; ++i) {
                (void)in_.intByteString(); // header and footer templates
            }
            (void)in_.intByteString(); // tempo name
        }
        startBpm_ = in_.i32();
        if (version_ >= 510) {
            in_.skip(1); // tempo hidden
        }
        // Guitar Pro 4 and 5 keep a byte for the key, then four more (octave).
        int key = 0;
        if (version_ >= 400) {
            key = in_.i8();
            in_.skip(4);
        } else {
            key = in_.i32();
        }
        readChannels();
        if (version_ >= 500) {
            readDirections();
            in_.skip(4); // master reverb
        }
        const std::size_t measureCount = in_.count(kMaxMeasures, "measures");
        const std::size_t trackCount = in_.count(kMaxTracks, "tracks");
        readHeaders(measureCount, key);
        for (std::size_t t = 0; t < trackCount; ++t) {
            readTrack(t);
        }
        if (version_ >= 500) {
            in_.skip(version_ == 500 ? 2 : 1);
        }
        warnLyrics();
        for (std::size_t m = 0; m < measureCount; ++m) {
            for (TrackData& track : tracks_) {
                readMeasure(track, m);
            }
        }
        return build();
    }

private:
    void readVersion() {
        // A 30-byte field; its text is checked before the padding is needed.
        constexpr std::size_t kField = 30;
        const std::size_t length = std::min<std::size_t>(in_.u8(), kField);
        const std::string text = in_.text(length);
        // "FICHIER GUITAR PRO v5.10": the version is the last four characters.
        const auto digit = [&text](std::size_t fromEnd) {
            const char c = text[text.size() - fromEnd];
            return c >= '0' && c <= '9' ? c - '0' : -1;
        };
        int major = -1;
        int minor = -1;
        if (text.size() >= 5 && text[text.size() - 5] == 'v' && text[text.size() - 3] == '.') {
            major = digit(4);
            if (digit(2) >= 0 && digit(1) >= 0) {
                minor = digit(2) * 10 + digit(1);
            }
        }
        if (major == 1 || major == 2) {
            throw FormatError{"Guitar Pro: Guitar Pro 1 and 2 files are not supported"};
        }
        if (major < 3 || major > 5 || minor < 0) {
            throw FormatError{std::format("Guitar Pro: unknown version \"{}\"", text)};
        }
        version_ = major * 100 + minor;
        in_.skip(kField - length);
    }

    void readInfo() {
        core::SongInfo& info = info_;
        info.title = trimmed(in_.intByteString());
        info.subtitle = trimmed(in_.intByteString());
        info.artist = trimmed(in_.intByteString());
        info.album = trimmed(in_.intByteString());
        info.words = trimmed(in_.intByteString());
        if (version_ >= 500) {
            info.music = trimmed(in_.intByteString());
        } else {
            info.music = info.words; // one "author" for words and music
        }
        info.copyright = trimmed(in_.intByteString());
        info.transcriber = trimmed(in_.intByteString());
        std::vector<std::string> notes;
        if (std::string instructions = trimmed(in_.intByteString()); !instructions.empty()) {
            notes.push_back(std::move(instructions));
        }
        const std::size_t lines = in_.count(kMaxMeasures, "notice lines");
        for (std::size_t i = 0; i < lines; ++i) {
            if (std::string line = trimmed(in_.intByteString()); !line.empty()) {
                notes.push_back(std::move(line));
            }
        }
        for (const std::string& line : notes) {
            if (!info.notes.empty()) {
                info.notes += '\n';
            }
            info.notes += line;
        }
    }

    void readLyrics() {
        lyricsTrack_ = in_.i32();
        for (int line = 0; line < 5; ++line) {
            in_.skip(4); // first measure
            hasLyrics_ = !trimmed(in_.intString()).empty() || hasLyrics_;
        }
    }

    void readChannels() {
        for (ChannelData& channel : channels_) {
            channel.program = in_.i32();
            channel.volume = in_.i8();
            channel.balance = in_.i8();
            in_.skip(4 + 2); // chorus, reverb, phaser, tremolo, padding
        }
    }

    void readDirections() {
        // The measure (from 1) of each sign and jump, in GpDirection order, or -1.
        for (int& measure : directions_) {
            measure = in_.i16();
        }
    }

    void readHeaders(std::size_t count, int songKey) {
        TimeSignature time;
        KeySignature key;
        if (std::abs(songKey) <= KeySignature::kMaxFifths) {
            key = KeySignature(songKey, KeyMode::Major);
        }
        headers_.reserve(count);
        for (std::size_t m = 0; m < count; ++m) {
            if (version_ >= 500 && m > 0) {
                in_.skip(1);
            }
            const std::uint8_t flags = in_.u8();
            int numerator = time.numerator();
            int denominator = time.denominator();
            if ((flags & 0x01U) != 0) {
                numerator = in_.i8();
            }
            if ((flags & 0x02U) != 0) {
                denominator = in_.i8();
            }
            HeaderData header;
            header.repeatStart = (flags & 0x04U) != 0;
            if (version_ < 500) {
                // Guitar Pro 3 and 4 count the times the section is played again,
                // and name one alternate ending: it takes every time up to its
                // number that no earlier ending of the repeat took.
                if ((flags & 0x08U) != 0) {
                    header.repeatTimes = repeatTimes(in_.u8() + 1);
                }
                if ((flags & 0x10U) != 0) {
                    header.alternateEndings = endingUpTo(in_.u8(), header.repeatStart);
                }
                if ((flags & 0x20U) != 0) {
                    readMarker(m);
                }
                if ((flags & 0x40U) != 0) {
                    key = readKey(key, m);
                }
            } else {
                // Guitar Pro 5 counts the times the section is played, and keeps
                // the alternate endings as bits (the first time in the lowest).
                if ((flags & 0x08U) != 0) {
                    header.repeatTimes = repeatTimes(in_.u8());
                }
                if ((flags & 0x20U) != 0) {
                    readMarker(m);
                }
                if ((flags & 0x40U) != 0) {
                    key = readKey(key, m);
                }
                if ((flags & 0x10U) != 0) {
                    header.alternateEndings = in_.u8();
                }
                if ((flags & 0x03U) != 0) {
                    in_.skip(4); // beaming
                }
                if ((flags & 0x10U) == 0) {
                    in_.skip(1);
                }
                if (in_.u8() != 0) {
                    warnings_.add({}, "triplet feel dropped", m);
                }
            }
            if ((flags & 0x03U) != 0) {
                try {
                    time = TimeSignature(numerator, denominator);
                } catch (const std::invalid_argument&) {
                    warnings_.add({}, "unsupported time signatures replaced by the previous one",
                                  m);
                }
            }
            header.time = time;
            header.key = key;
            headers_.push_back(header);
        }
    }

    static std::uint8_t repeatTimes(int times) {
        return static_cast<std::uint8_t>(std::clamp(times, 2, 255));
    }

    // The times up to `number` that no earlier alternate ending of the current
    // repeat takes (Guitar Pro 3 and 4).
    [[nodiscard]] std::uint8_t endingUpTo(int number, bool repeatStartsHere) const {
        unsigned taken = 0;
        if (!repeatStartsHere) {
            for (auto it = headers_.rbegin(); it != headers_.rend(); ++it) {
                taken |= static_cast<unsigned>(it->alternateEndings);
                if (it->repeatStart) {
                    break;
                }
            }
        }
        const unsigned upTo = (1U << std::clamp(number, 0, MeasureHeader::kMaxEnding)) - 1U;
        return static_cast<std::uint8_t>(upTo & ~taken);
    }

    void readMarker(std::size_t m) {
        (void)in_.intByteString();
        in_.skip(4); // colour
        warnings_.add({}, "section marks dropped", m);
    }

    KeySignature readKey(const KeySignature& previous, std::size_t m) {
        const int fifths = in_.i8();
        const KeyMode mode = in_.i8() == 1 ? KeyMode::Minor : KeyMode::Major;
        if (std::abs(fifths) > KeySignature::kMaxFifths) {
            warnings_.add({}, "unsupported key signatures replaced by the previous one", m);
            return previous;
        }
        return KeySignature(fifths, mode);
    }

    void readTrack(std::size_t index) {
        if (version_ >= 500 && (index == 0 || version_ == 500)) {
            in_.skip(1);
        }
        const std::uint8_t flags = in_.u8();
        TrackData track;
        track.name = trimmed(in_.byteString(40));
        if (track.name.empty()) {
            track.name = std::format("Track {}", index + 1);
        }
        const std::int32_t stringCount = in_.i32();
        std::array<int, kStrings> tuning{};
        for (int& open : tuning) {
            open = in_.i32();
        }
        const int port = in_.i32();
        const int channel = in_.i32();
        in_.skip(4 + 4); // effect channel, fret count
        const int capo = in_.i32();
        in_.skip(4); // colour
        if (version_ >= 500) {
            // Display flags, sound settings and the RSE instrument.
            in_.skip(4 + 1 + 12 + 12 + 12);
            if (version_ == 500) {
                in_.skip(3);
            } else {
                in_.skip(4 + 4);
                (void)in_.intByteString(); // RSE effect
                (void)in_.intByteString(); // RSE effect category
            }
        }
        track.percussion = (flags & 0x01U) != 0 || channel == 10;
        track.banjo = (flags & 0x04U) != 0;
        if (version_ >= 500) {
            track.mix.solo = (flags & 0x10U) != 0;
            track.mix.mute = (flags & 0x20U) != 0;
        }
        if (port >= 1 && port <= 4 && channel >= 1 && channel <= 16) {
            const auto slot = static_cast<std::size_t>((port - 1) * 16 + channel - 1);
            const ChannelData& data = channels_[slot];
            track.program = data.program >= 0 && data.program <= 127 ? data.program : 0;
            // Volume and balance count sixteenths; 8 is the centre.
            track.mix.volume = static_cast<double>(std::clamp(data.volume, 0, 16)) / 16.0;
            track.mix.pan = static_cast<double>(std::clamp(data.balance, 0, 16) - 8) / 8.0;
        }
        if (!track.percussion && stringCount >= 1 && stringCount <= kStrings) {
            const auto used = std::span(tuning).first(static_cast<std::size_t>(stringCount));
            if (std::ranges::all_of(used, [](int open) { return open > 0 && open <= 127; })) {
                track.strings.assign(used.begin(), used.end());
            }
        }
        if (capo >= 0 && capo <= Tuning::kMaxFret) {
            track.capo = capo;
        } else {
            warnings_.add(track.name, "capo outside the neck dropped");
        }
        tracks_.push_back(std::move(track));
    }

    void warnLyrics() {
        if (hasLyrics_) {
            const bool known =
                lyricsTrack_ >= 1 && std::cmp_less_equal(lyricsTrack_, tracks_.size());
            warnings_.add(known ? tracks_[static_cast<std::size_t>(lyricsTrack_ - 1)].name : "",
                          "lyrics dropped");
        }
    }

    void addDirections(Song& song) {
        for (std::size_t d = 0; d < directions_.size(); ++d) {
            const int measure = directions_[d];
            if (measure <= 0) {
                continue;
            }
            const auto m = static_cast<std::size_t>(measure - 1);
            if (m >= song.measureCount()) {
                warnings_.add({}, "signs and jumps past the last measure dropped");
            } else if (!detail::applyDirection(song.measureHeader(m),
                                               static_cast<detail::GpDirection>(d))) {
                warnings_.add({}, "two jumps in one measure: the last one kept", m);
            }
        }
    }

    void readMeasure(TrackData& track, std::size_t m) {
        Measure measure;
        const TimeSignature& time = headers_[m].time;
        const std::size_t voices = version_ >= 500 ? 2 : 1;
        for (std::size_t v = 0; v < voices; ++v) {
            Voice& voice = measure.voices[v];
            const std::size_t beats = in_.count(kMaxBeats, "beats");
            Fraction offset;
            for (std::size_t b = 0; b < beats; ++b) {
                readBeat(track, voice, v, m, offset);
            }
            const core::VoiceFill fill = core::voiceFill(voice, time);
            if (fill == core::VoiceFill::Incomplete && !voice.isEmpty()) {
                (void)core::fillWithRests(voice, time);
                warnings_.add(track.name, "short voices filled with rests", m);
            } else if (fill == core::VoiceFill::Overfull) {
                warnings_.add(track.name, "voices longer than their measure kept as written", m);
            }
        }
        if (version_ >= 500) {
            in_.skip(1); // line break
        }
        track.measures.push_back(std::move(measure));
    }

    void readBeat(TrackData& track, Voice& voice, std::size_t v, std::size_t m, Fraction& offset) {
        const std::uint8_t flags = in_.u8();
        std::uint8_t status = 1; // 0 empty (takes no time), 1 notes, 2 rest
        if ((flags & 0x40U) != 0) {
            status = in_.u8();
        }
        Beat beat{.duration = readDuration(flags, track.name, m)};
        if ((flags & 0x02U) != 0) {
            readChord();
            warnings_.add(track.name, "chord names and diagrams dropped", m);
        }
        if ((flags & 0x04U) != 0) {
            (void)in_.intByteString();
            warnings_.add(track.name, "text dropped", m);
        }
        NoteDefaults defaults;
        if ((flags & 0x08U) != 0) {
            readBeatEffects(beat.effects, defaults, track.name, m);
        }
        if ((flags & 0x10U) != 0) {
            readMixTable(offset, track.name, m);
        }
        // The beat before, in this measure or at the end of the previous one.
        const Beat* previous = nullptr;
        if (!voice.beats.empty()) {
            previous = &voice.beats.back();
        } else if (!track.measures.empty() && !track.measures.back().voices[v].beats.empty()) {
            previous = &track.measures.back().voices[v].beats.back();
        }
        const std::uint8_t strings = in_.u8();
        for (int s = 0; s < kStrings; ++s) {
            if ((strings & (1U << static_cast<unsigned>(kStrings - 1 - s))) != 0) {
                if (auto note = readNote(track, beat, previous, v, s + 1, defaults, m)) {
                    beat.notes.push_back(std::move(*note));
                }
            }
        }
        if (version_ >= 500) {
            const std::int16_t display = in_.i16();
            if ((static_cast<std::uint16_t>(display) & 0x0800U) != 0) {
                in_.skip(1); // secondary beam breaks
            }
        }
        if (status == 0) {
            return;
        }
        if (status == 2) {
            beat.notes.clear();
        }
        offset += beat.duration.length();
        voice.beats.push_back(std::move(beat));
    }

    Duration readDuration(std::uint8_t flags, const std::string& who, std::size_t m) {
        const std::size_t at = in_.position();
        const int code = in_.i8(); // -2 whole, -1 half, 0 quarter ... 4 sixty-fourth
        if (code < -2 || code > 5) {
            throw FormatError{
                std::format("Guitar Pro: damaged file: note value {} at byte {}", code, at)};
        }
        const auto value = static_cast<NoteValue>(static_cast<int>(NoteValue::Whole) + code + 2);
        Tuplet tuplet;
        if ((flags & 0x20U) != 0) {
            // "n in the time of" the power of two below n: 3:2, 5:4, 6:4, 7:4, 9:8...
            const std::int32_t actual = in_.i32();
            if (actual >= 3 && actual <= Tuplet::kMaxCount) {
                tuplet = Tuplet(
                    actual, static_cast<int>(std::bit_floor(static_cast<unsigned>(actual - 1))));
            } else if (actual > Tuplet::kMaxCount) {
                warnings_.add(who, "tuplets the model cannot hold written as plain notes", m);
            }
        }
        return Duration(value, (flags & 0x01U) != 0 ? 1 : 0, tuplet);
    }

    void readChord() {
        if (version_ >= 500) {
            in_.skip(107);
            return;
        }
        const std::uint8_t format = in_.u8();
        if ((format & 0x01U) == 0) {
            (void)in_.intByteString(); // name
            if (in_.i32() != 0) {      // first fret, then six frets
                in_.skip(6 * 4);
            }
            return;
        }
        if (version_ >= 400) {
            in_.skip(16);
            (void)in_.byteString(22);
            in_.skip(3 + 4 + 7 * 4 + 32);
        } else {
            in_.skip(25);
            (void)in_.byteString(22);
            in_.skip(76);
        }
    }

    BendCurve readBend() {
        in_.skip(1 + 4); // type and the highest value
        const std::size_t count = in_.count(kMaxBendPoints, "bend points");
        std::vector<BendPoint> points;
        for (std::size_t i = 0; i < count; ++i) {
            const std::int32_t position = in_.i32();
            const std::int32_t value = in_.i32();
            in_.skip(1); // vibrato
            const auto percent = static_cast<int>(std::clamp(
                std::lround(static_cast<double>(position) * 100.0 / kBendPositions), 0L, 100L));
            const int cents =
                2 * std::clamp(value, -BendCurve::kMaxCents / 2, BendCurve::kMaxCents / 2);
            if ((points.empty() || percent > points.back().position) &&
                points.size() < BendCurve::kMaxPoints) {
                points.push_back(BendPoint{.position = percent, .cents = cents});
            }
        }
        if (std::ranges::all_of(points, [](const BendPoint& point) { return point.cents == 0; })) {
            return {};
        }
        return BendCurve(std::move(points));
    }

    void readBeatEffects(core::BeatEffects& effects, NoteDefaults& defaults, const std::string& who,
                         std::size_t m) {
        const std::uint8_t first = in_.u8();
        const std::uint8_t second = version_ >= 400 ? in_.u8() : std::uint8_t{0};
        if ((first & 0x01U) != 0) {
            defaults.vibrato = Vibrato::Slight;
        }
        if ((first & 0x02U) != 0) {
            defaults.vibrato = Vibrato::Wide;
        }
        if (version_ < 400 && (first & 0x04U) != 0) {
            defaults.harmonic = HarmonicKind::Natural;
        }
        if (version_ < 400 && (first & 0x08U) != 0) {
            defaults.harmonic = HarmonicKind::Artificial;
        }
        if ((first & 0x10U) != 0) {
            warnings_.add(who, "volume swells dropped", m);
        }
        if ((first & 0x20U) != 0) {
            // Tapping, slapping or popping; Guitar Pro 3 also keeps the whammy
            // bar here (kind 0) as one dip.
            const int kind = in_.u8();
            if (version_ < 400) {
                const std::int32_t value = in_.i32();
                if (kind == 0 && value != 0) {
                    const int cents =
                        -2 * std::clamp(value, -BendCurve::kMaxCents / 2, BendCurve::kMaxCents / 2);
                    effects.whammyBar = BendCurve({{.position = 0, .cents = 0},
                                                   {.position = 50, .cents = cents},
                                                   {.position = 100, .cents = 0}});
                }
            }
            effects.bassTechnique = kind == 1   ? core::BassTechnique::Tap
                                    : kind == 2 ? core::BassTechnique::Slap
                                    : kind == 3 ? core::BassTechnique::Pop
                                                : core::BassTechnique::None;
        }
        if ((second & 0x04U) != 0) {
            effects.whammyBar = readBend();
        }
        if ((first & 0x40U) != 0) {
            // Strum speeds: upwards, then downwards.
            const int up = in_.i8();
            const int down = in_.i8();
            effects.strum = up > 0     ? core::Strum::Up
                            : down > 0 ? core::Strum::Down
                                       : core::Strum::None;
        }
        if ((second & 0x01U) != 0) {
            warnings_.add(who, "rasgueado dropped", m);
        }
        if ((second & 0x02U) != 0) {
            const int pick = in_.i8();
            effects.pickStroke = pick == 1   ? core::PickStroke::Up
                                 : pick == 2 ? core::PickStroke::Down
                                             : core::PickStroke::None;
        }
    }

    void readMixTable(const Fraction& offset, const std::string& who, std::size_t m) {
        const int instrument = in_.i8();
        if (version_ >= 500) {
            in_.skip(16); // RSE instrument
        }
        std::array<int, 6> values{}; // volume, balance, chorus, reverb, phaser, tremolo
        for (int& value : values) {
            value = in_.i8();
        }
        if (version_ >= 500) {
            (void)in_.intByteString(); // tempo name
        }
        const std::int32_t bpm = in_.i32();
        for (const int value : values) {
            if (value >= 0) {
                in_.skip(1); // transition length
            }
        }
        int transition = 0;
        if (bpm >= 0) {
            transition = in_.i8();
            if (version_ >= 510) {
                in_.skip(1); // tempo hidden
            }
        }
        if (version_ >= 400) {
            in_.skip(1); // which tracks the changes apply to
        }
        if (version_ >= 510) {
            in_.skip(1); // wah
            (void)in_.intByteString();
            (void)in_.intByteString();
        }
        if (instrument >= 0 || std::ranges::any_of(values, [](int value) { return value >= 0; })) {
            warnings_.add(who, "instrument and volume changes dropped", m);
        }
        if (bpm < 0) {
            return;
        }
        if (bpm < core::Tempo::kMinBpm || bpm > core::Tempo::kMaxBpm) {
            warnings_.add({}, "tempos outside 10-999 bpm dropped", m);
            return;
        }
        if (transition > 0) {
            warnings_.add({}, "gradual tempo changes written as sudden ones", m);
        }
        tempos_.push_back(TempoMark{.measure = m, .offset = offset, .bpm = bpm});
    }

    std::optional<Note> readNote(TrackData& track, Beat& beat, const Beat* previous, std::size_t v,
                                 int string, const NoteDefaults& defaults, std::size_t m) {
        const std::uint8_t flags = in_.u8();
        std::uint8_t type = 1; // 1 normal, 2 tied, 3 dead
        if ((flags & 0x20U) != 0) {
            type = in_.u8();
        }
        if (version_ < 500 && (flags & 0x01U) != 0) {
            in_.skip(2); // a duration of its own
        }
        int dynamic = 6;
        if ((flags & 0x10U) != 0) {
            dynamic = in_.i8();
        }
        int fret = 0;
        if ((flags & 0x20U) != 0) {
            fret = in_.i8();
        }
        if ((flags & 0x80U) != 0) {
            in_.skip(2); // fingering
        }
        if (version_ >= 500) {
            if ((flags & 0x01U) != 0) {
                in_.skip(8); // a duration of its own
            }
            in_.skip(1);
        }
        core::NoteEffects effects;
        effects.dynamic = dynamicFor(dynamic);
        effects.ghost = (flags & 0x04U) != 0;
        if ((flags & 0x02U) != 0) {
            effects.accent = Accent::Marcato;
        } else if ((flags & 0x40U) != 0) {
            effects.accent = Accent::Accent;
        }
        effects.vibrato = defaults.vibrato;
        bool legatoToNext = false;
        if ((flags & 0x08U) != 0) {
            readNoteEffects(effects, beat.effects, legatoToNext, fret, track.name, m);
        }
        if (defaults.harmonic == HarmonicKind::Natural) {
            effects.harmonic = {.kind = HarmonicKind::Natural,
                                .fretTenths =
                                    static_cast<std::int16_t>(std::clamp(fret, 0, 30) * 10)};
        } else if (defaults.harmonic == HarmonicKind::Artificial) {
            effects.harmonic = {.kind = HarmonicKind::Artificial, .fretTenths = 120};
        }

        StringState& state = track.voices[v][static_cast<std::size_t>(string - 1)];
        if (type == 2 && state.fret >= 0) {
            fret = state.fret; // a tie repeats the string's last fret
        } else if (type != 2 && state.legatoToNext) {
            effects.legatoFromPrevious = true;
        }
        effects.deadNote = type == 3;
        state.legatoToNext = legatoToNext;
        state.fret = fret;

        Note note;
        note.effects = effects;
        if (track.percussion) {
            if (fret < 0 || fret > 127) {
                warnings_.add(track.name, "notes without a pitch dropped", m);
                return std::nullopt;
            }
            note.pitch = Pitch::fromMidi(fret);
        } else {
            if (std::cmp_greater(string, track.strings.size()) || fret < 0 ||
                track.strings[static_cast<std::size_t>(string - 1)] + track.capo + fret > 127) {
                warnings_.add(track.name, "notes without a pitch dropped", m);
                return std::nullopt;
            }
            const int midi =
                track.strings[static_cast<std::size_t>(string - 1)] + track.capo + fret;
            note.pitch = headers_[m].key.spell(midi);
            if (track.capo + fret <= Tuning::kMaxFret) {
                note.position = core::FretPosition{.string = static_cast<std::int8_t>(string),
                                                   .fret = static_cast<std::int8_t>(fret)};
            } else {
                warnings_.add(track.name, "frets beyond the 30th kept as pitches only", m);
            }
            track.pitchSum += midi;
            ++track.noteCount;
        }
        if (type == 2) {
            // Only a note just before on the same string can be held on.
            const bool held =
                previous != nullptr &&
                std::ranges::any_of(previous->notes, [&note, string](const Note& before) {
                    return before.pitch.midiNumber() == note.pitch.midiNumber() &&
                           (!before.position || before.position->string == string);
                });
            note.effects.tiedFromPrevious = held;
            if (!held) {
                warnings_.add(track.name, "ties after a rest played as new notes", m);
            }
        }
        return note;
    }

    void readNoteEffects(core::NoteEffects& effects, core::BeatEffects& beatEffects,
                         bool& legatoToNext, int fret, const std::string& who, std::size_t m) {
        const std::uint8_t first = in_.u8();
        const std::uint8_t second = version_ >= 400 ? in_.u8() : std::uint8_t{0};
        if ((first & 0x01U) != 0) {
            effects.bend = readBend();
        }
        legatoToNext = (first & 0x02U) != 0;
        if (version_ < 400 && (first & 0x04U) != 0) {
            effects.slideOut = SlideOut::Shift;
        }
        effects.letRing = (first & 0x08U) != 0;
        if ((first & 0x10U) != 0) {
            in_.skip(version_ >= 500 ? 5 : 4);
            warnings_.add(who, "grace notes dropped", m);
        }
        if (version_ < 400) {
            return;
        }
        effects.staccato = (second & 0x01U) != 0;
        effects.palmMute = (second & 0x02U) != 0;
        if ((second & 0x04U) != 0) {
            if (const auto speed = tremoloSpeed(in_.i8())) {
                beatEffects.tremoloPicking = speed;
            } else {
                warnings_.add(who, "unknown tremolo picking speeds dropped", m);
            }
        }
        if ((second & 0x08U) != 0) {
            readSlide(effects);
        }
        if ((second & 0x10U) != 0) {
            readHarmonic(effects, fret, who, m);
        }
        if ((second & 0x20U) != 0) {
            in_.skip(2); // trill fret and speed
            warnings_.add(who, "trills dropped", m);
        }
        if ((second & 0x40U) != 0 && effects.vibrato == Vibrato::None) {
            effects.vibrato = Vibrato::Slight;
        }
    }

    void readSlide(core::NoteEffects& effects) {
        if (version_ < 500) {
            // One kind: -2 in from above, -1 in from below, 1 shift, 2 legato,
            // 3 out downwards, 4 out upwards.
            switch (in_.i8()) {
            case -2:
                effects.slideIn = SlideIn::FromAbove;
                break;
            case -1:
                effects.slideIn = SlideIn::FromBelow;
                break;
            case 1:
                effects.slideOut = SlideOut::Shift;
                break;
            case 2:
                effects.slideOut = SlideOut::Legato;
                break;
            case 3:
                effects.slideOut = SlideOut::Downwards;
                break;
            case 4:
                effects.slideOut = SlideOut::Upwards;
                break;
            default:
                break;
            }
            return;
        }
        // Flags, as in GPIF: 1 shift, 2 legato, 4 out downwards, 8 out upwards,
        // 16 in from below, 32 in from above.
        const std::uint8_t flags = in_.u8();
        if ((flags & 0x01U) != 0) {
            effects.slideOut = SlideOut::Shift;
        } else if ((flags & 0x02U) != 0) {
            effects.slideOut = SlideOut::Legato;
        } else if ((flags & 0x04U) != 0) {
            effects.slideOut = SlideOut::Downwards;
        } else if ((flags & 0x08U) != 0) {
            effects.slideOut = SlideOut::Upwards;
        }
        if ((flags & 0x10U) != 0) {
            effects.slideIn = SlideIn::FromBelow;
        } else if ((flags & 0x20U) != 0) {
            effects.slideIn = SlideIn::FromAbove;
        }
    }

    void readHarmonic(core::NoteEffects& effects, int fret, const std::string& who, std::size_t m) {
        const int kind = in_.i8();
        const auto tenths = [](int frets) {
            return static_cast<std::int16_t>(std::clamp(frets, 0, 30) * 10);
        };
        switch (kind) {
        case 1:
            effects.harmonic = {.kind = HarmonicKind::Natural, .fretTenths = tenths(fret)};
            return;
        case 2: // Guitar Pro 5: the harmonic's pitch follows
            in_.skip(3);
            effects.harmonic = {.kind = HarmonicKind::Artificial, .fretTenths = 120};
            return;
        case 3: {
            int above = 12;
            if (version_ >= 500) {
                const int tapped = in_.u8(); // the fret the right hand taps
                above = tapped > fret ? tapped - fret : 12;
            }
            effects.harmonic = {.kind = HarmonicKind::Tapped, .fretTenths = tenths(above)};
            return;
        }
        case 4:
            effects.harmonic = {.kind = HarmonicKind::Pinch, .fretTenths = 120};
            return;
        case 5:
            effects.harmonic = {.kind = HarmonicKind::Semi, .fretTenths = 120};
            return;
        case 15: // Guitar Pro 4: artificial harmonics 5, 7 and 12 frets above
        case 17:
        case 22:
            effects.harmonic = {.kind = HarmonicKind::Artificial,
                                .fretTenths = tenths(kind == 15   ? 5
                                                     : kind == 17 ? 7
                                                                  : 12)};
            return;
        default:
            warnings_.add(who, "unknown harmonics dropped", m);
            return;
        }
    }

    // ---- The song ------------------------------------------------------------------

    Song build() {
        Song song;
        song.info() = std::move(info_);
        for (TrackData& track : tracks_) {
            song.track(song.addTrack(trackFor(track))).setMix(track.mix);
        }
        song.appendMeasures(headers_.size());
        for (std::size_t m = 0; m < headers_.size(); ++m) {
            MeasureHeader& header = song.measureHeader(m);
            header.timeSignature = headers_[m].time;
            header.keySignature = headers_[m].key;
            header.repeatStart = headers_[m].repeatStart;
            header.repeatTimes = headers_[m].repeatTimes;
            header.alternateEndings = headers_[m].alternateEndings;
        }
        addDirections(song);
        if (startBpm_ >= core::Tempo::kMinBpm && startBpm_ <= core::Tempo::kMaxBpm) {
            song.setStartTempo(core::Tempo(startBpm_));
        } else {
            warnings_.add({}, "tempos outside 10-999 bpm dropped");
        }
        for (const TempoMark& mark : tempos_) {
            addTempo(song, mark);
        }
        for (std::size_t t = 0; t < tracks_.size(); ++t) {
            TrackData& track = tracks_[t];
            Staff& staff = song.track(t).staff(0);
            const bool fretted = staff.tuning().has_value();
            for (std::size_t m = 0; m < track.measures.size(); ++m) {
                Measure& measure = track.measures[m];
                if (!fretted) {
                    for (Voice& voice : measure.voices) {
                        for (Beat& beat : voice.beats) {
                            for (Note& note : beat.notes) {
                                note.position.reset();
                            }
                        }
                    }
                }
                staff.measure(m) = std::move(measure);
            }
        }
        return song;
    }

    static core::Track trackFor(const TrackData& track) {
        if (track.percussion) {
            return core::Track(track.name,
                               Instrument{.name = "Drums",
                                          .kind = InstrumentKind::Percussion,
                                          .midiProgram = 0,
                                          .writtenOffset = 0},
                               {Staff(Clef::Percussion)});
        }
        InstrumentKind kind = detail::kindForProgram(track.program, false);
        if (track.banjo) {
            kind = InstrumentKind::Fretted;
        }
        const std::string name(detail::programName(track.program));
        if (kind == InstrumentKind::Fretted && !track.strings.empty()) {
            std::vector<Pitch> strings;
            for (const int open : track.strings) {
                strings.push_back(Pitch::fromMidi(open));
            }
            // Guitars and basses are written an octave above the sound.
            const bool bass = (track.program >= 32 && track.program <= 39) ||
                              std::ranges::min(track.strings) <= 28;
            return core::Track(
                track.name,
                Instrument{
                    .name = name, .kind = kind, .midiProgram = track.program, .writtenOffset = 12},
                {Staff(bass ? Clef::Bass8vb : Clef::Treble8vb, Tuning(strings), track.capo)});
        }
        if (kind == InstrumentKind::Fretted) {
            kind = InstrumentKind::Pitched; // no tuning to place the notes with
        }
        // Other instruments: the clef that suits the notes.
        const bool low =
            track.noteCount > 0 && track.pitchSum < 60LL * static_cast<long long>(track.noteCount);
        return core::Track(
            track.name,
            Instrument{
                .name = name, .kind = kind, .midiProgram = track.program, .writtenOffset = 0},
            {Staff(low ? Clef::Bass : Clef::Treble)});
    }

    static void addTempo(Song& song, const TempoMark& mark) {
        const core::Tempo tempo(mark.bpm);
        if (mark.measure == 0 && mark.offset == Fraction(0)) {
            song.setStartTempo(tempo);
            return;
        }
        core::MeasureHeader& header = song.measureHeader(mark.measure);
        if (mark.offset >= header.timeSignature.measureLength()) {
            return;
        }
        auto& changes = header.tempoChanges;
        const auto at = std::ranges::find_if(changes, [&mark](const core::TempoChange& change) {
            return change.offset >= mark.offset;
        });
        if (at != changes.end() && at->offset == mark.offset) {
            at->tempo = tempo;
        } else {
            changes.insert(at, core::TempoChange{.offset = mark.offset, .tempo = tempo});
        }
    }

    ByteReader in_;
    Warnings& warnings_;
    int version_ = 0; // 300, 400, 406, 500, 510
    core::SongInfo info_;
    int startBpm_ = 120;
    std::int32_t lyricsTrack_ = 0;
    bool hasLyrics_ = false;
    std::array<ChannelData, 64> channels_{};              // four ports of sixteen MIDI channels
    std::array<int, detail::kGpDirections> directions_{}; // 0: none (Guitar Pro 3 and 4)
    std::vector<HeaderData> headers_;
    std::vector<TrackData> tracks_;
    std::vector<TempoMark> tempos_;
};

} // namespace

ImportResult readGuitarProBinary(std::span<const std::uint8_t> bytes) {
    Warnings warnings;
    ImportResult result;
    try {
        result.song = BinaryReader(bytes, warnings).read();
    } catch (const std::invalid_argument& error) {
        throw FormatError{std::format("Guitar Pro: {}", error.what())};
    } catch (const std::out_of_range& error) {
        throw FormatError{std::format("Guitar Pro: {}", error.what())};
    }
    result.warnings = warnings.lines();
    return result;
}

} // namespace elara::io::detail
