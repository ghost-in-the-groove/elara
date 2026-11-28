// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/names.hpp>
#include <elara/core/version.hpp>
#include <elara/io/native_format.hpp>

#include "archive.hpp"
#include "chord_pool.hpp"
#include <yyjson.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace elara::io {

namespace {

using namespace core;

constexpr std::string_view kFormatName = "elara";
// A measure's spacing, in percent of its ideal spaces (ADR-0152).
constexpr std::int64_t kMinSpacing = 50;
constexpr std::int64_t kMaxSpacing = 200;
// "generator" is this name and the version: "Elara 0.1.0".
constexpr std::string_view kGeneratorName = "Elara ";

// Enumerations are written by their names in the model (core::EnumNames), so
// renaming one there changes the format.

// ============================================================================
// Writing
// ============================================================================

// The length of the UTF-8 sequence that starts at `at`; 0 when it is not
// valid UTF-8 (RFC 3629: no overlong forms, surrogates or code points past
// U+10FFFF).
std::size_t sequenceLength(std::string_view text, std::size_t at) noexcept {
    const auto byte = [&](std::size_t i) -> unsigned {
        return at + i < text.size() ? static_cast<unsigned char>(text[at + i]) : 0U;
    };
    const auto follows = [&](std::size_t i) {
        return (byte(i) & 0xC0U) == 0x80U;
    };
    const unsigned lead = byte(0);
    const unsigned second = byte(1);
    if (lead >= 0xC2U && lead <= 0xDFU) {
        return follows(1) ? 2 : 0;
    }
    if (lead >= 0xE0U && lead <= 0xEFU) {
        const bool overlong = lead == 0xE0U && second < 0xA0U;
        const bool surrogate = lead == 0xEDU && second >= 0xA0U;
        return follows(1) && follows(2) && !overlong && !surrogate ? 3 : 0;
    }
    if (lead >= 0xF0U && lead <= 0xF4U) {
        const bool overlong = lead == 0xF0U && second < 0x90U;
        const bool beyond = lead == 0xF4U && second >= 0x90U;
        return follows(1) && follows(2) && follows(3) && !overlong && !beyond ? 4 : 0;
    }
    return 0;
}

// song.json in yyjson's compact form, written while the song is walked: the
// text grows a value at a time, with no tree of values before it (on a large
// song the tree took four times the text, and yyjson reserved the text at
// three times its size).
class JsonOut {
public:
    [[nodiscard]] std::string take() && { return std::move(text_); }

    void beginObject() {
        separate();
        text_ += '{';
        first_ = true;
    }
    void endObject() {
        text_ += '}';
        first_ = false;
    }
    void beginArray() {
        separate();
        text_ += '[';
        first_ = true;
    }
    void endArray() {
        text_ += ']';
        first_ = false;
    }

    /// An object's key, which the next value goes with.
    void key(std::string_view name) {
        separate();
        quoted(name);
        text_ += ':';
        first_ = true;
    }
    void string(std::string_view value) {
        separate();
        quoted(value);
    }
    void integer(std::int64_t value) {
        separate();
        std::array<char, 24> digits{};
        const char* end = std::to_chars(digits.data(), digits.data() + digits.size(), value).ptr;
        text_.append(digits.data(), static_cast<std::size_t>(end - digits.data()));
    }
    /// As yyjson writes a number: the shortest text that reads back the same.
    /// Infinities and NaN are not JSON, and cannot be written.
    void real(double value) {
        if (!std::isfinite(value)) {
            throw std::runtime_error("cannot write song.json: a number that is not finite");
        }
        separate();
        yyjson_val number{};
        yyjson_set_real(&number, value);
        std::array<char, 48> digits{};
        const char* end = yyjson_write_number(&number, digits.data());
        if (end == nullptr) {
            throw std::runtime_error("cannot write song.json");
        }
        text_.append(digits.data(), static_cast<std::size_t>(end - digits.data()));
    }
    void boolean(bool value) {
        separate();
        text_ += value ? "true" : "false";
    }

private:
    void separate() {
        if (!first_) {
            text_ += ',';
        }
        first_ = false;
    }

    // Escapes what yyjson escapes: quotes, backslashes and control
    // characters; the rest of the UTF-8 text is copied. Text that is not
    // UTF-8 cannot be written, as with yyjson.
    void quoted(std::string_view value) {
        static constexpr std::string_view kHex = "0123456789ABCDEF";
        text_ += '"';
        std::size_t copied = 0; // value[copied, i) still to append
        for (std::size_t i = 0; i < value.size();) {
            const auto byte = static_cast<unsigned char>(value[i]);
            if (byte >= 0x80U) {
                const std::size_t length = sequenceLength(value, i);
                if (length == 0) {
                    throw std::runtime_error("cannot write song.json: text that is not UTF-8");
                }
                i += length;
                continue;
            }
            if (byte >= 0x20U && byte != '"' && byte != '\\') {
                ++i;
                continue;
            }
            text_.append(value.substr(copied, i - copied));
            switch (byte) {
            case '"':
                text_ += "\\\"";
                break;
            case '\\':
                text_ += "\\\\";
                break;
            case '\b':
                text_ += "\\b";
                break;
            case '\t':
                text_ += "\\t";
                break;
            case '\n':
                text_ += "\\n";
                break;
            case '\f':
                text_ += "\\f";
                break;
            case '\r':
                text_ += "\\r";
                break;
            default:
                text_ += "\\u00";
                text_ += kHex[byte >> 4U];
                text_ += kHex[byte & 0x0FU];
                break;
            }
            copied = ++i;
        }
        text_.append(value.substr(copied));
        text_ += '"';
    }

    std::string text_;
    bool first_ = true;
};

class Writer {
public:
    std::string write(const Song& song) && {
        out_.beginObject();
        text("format", kFormatName);
        integer("formatVersion", kNativeFormatVersion);
        text("generator", std::format("{}{}", kGeneratorName, version()));
        info(song.info());
        out_.key("tempo");
        out_.beginObject();
        tempoFields(song.startTempo());
        out_.endObject();

        out_.key("measures");
        out_.beginArray();
        for (std::size_t m = 0; m < song.measureCount(); ++m) {
            header(song.measureHeader(m));
        }
        out_.endArray();
        layouts(song.measureLayouts(LayoutScope::song()));

        out_.key("tracks");
        out_.beginArray();
        for (std::size_t t = 0; t < song.trackCount(); ++t) {
            track(song.track(t));
        }
        out_.endArray();
        out_.endObject();
        return std::move(out_).take();
    }

private:
    void text(std::string_view key, std::string_view value) {
        out_.key(key);
        out_.string(value);
    }
    void textIfAny(std::string_view key, std::string_view value) {
        if (!value.empty()) {
            text(key, value);
        }
    }
    void integer(std::string_view key, std::int64_t value) {
        out_.key(key);
        out_.integer(value);
    }
    void flag(std::string_view key, bool value) {
        if (value) {
            out_.key(key);
            out_.boolean(true);
        }
    }
    // Whole numbers are written without a fraction ("120", not "120.0").
    void number(std::string_view key, double value) {
        if (std::trunc(value) == value && std::abs(value) < 1e15) {
            integer(key, static_cast<std::int64_t>(value));
        } else {
            out_.key(key);
            out_.real(value);
        }
    }
    template <NamedEnum Enum>
    void enumIfNot(std::string_view key, Enum value, Enum skip) {
        if (value != skip) {
            text(key, nameOf(value));
        }
    }

    void info(const SongInfo& info) {
        out_.key("info");
        out_.beginObject();
        textIfAny("title", info.title);
        textIfAny("subtitle", info.subtitle);
        textIfAny("artist", info.artist);
        textIfAny("album", info.album);
        textIfAny("words", info.words);
        textIfAny("music", info.music);
        textIfAny("copyright", info.copyright);
        textIfAny("transcriber", info.transcriber);
        textIfAny("notes", info.notes);
        out_.endObject();
    }

    // The measures of a scope whose layout is not the default (ADR-0152), as
    // "layout"; nothing for a scope that holds none.
    void layouts(const MeasureLayouts& value) {
        if (!value) {
            return;
        }
        out_.key("layout");
        out_.beginArray();
        for (std::size_t m = 0; m < value->size(); ++m) {
            const MeasureLayout& layout = (*value)[m];
            if (layout.isDefault()) {
                continue;
            }
            out_.beginObject();
            integer("measure", static_cast<std::int64_t>(m));
            enumIfNot("break", layout.breakBefore, Break::Auto);
            if (layout.spacing != MeasureLayout::kDefaultSpacing) {
                integer("spacing", layout.spacing);
            }
            out_.endObject();
        }
        out_.endArray();
    }

    // A tempo's fields, in an object its caller opens.
    void tempoFields(const Tempo& value) {
        number("bpm", value.bpm());
        if (value.beatUnit() != Duration{}) {
            text("beatUnit", value.beatUnit().toString());
        }
    }

    void header(const MeasureHeader& value) {
        out_.beginObject();
        if (value.timeSignature != TimeSignature{}) {
            text("timeSignature", value.timeSignature.toString());
        }
        if (value.keySignature != KeySignature{}) {
            text("keySignature", value.keySignature.toString());
        }
        if (!value.tempoChanges.empty()) {
            out_.key("tempoChanges");
            out_.beginArray();
            for (const auto& change : value.tempoChanges) {
                out_.beginObject();
                tempoFields(change.tempo);
                if (!change.offset.isZero()) {
                    text("offset", change.offset.toString());
                }
                flag("gradual", change.gradual);
                out_.endObject();
            }
            out_.endArray();
        }
        flag("repeatStart", value.repeatStart);
        if (value.repeatEnd()) {
            integer("repeatTimes", value.repeatTimes);
        }
        if (value.alternateEndings != 0) {
            out_.key("alternateEndings");
            out_.beginArray();
            for (int time = 1; time <= MeasureHeader::kMaxEnding; ++time) {
                if (value.playedOnTime(time)) {
                    out_.integer(time);
                }
            }
            out_.endArray();
        }
        flag("doubleBar", value.doubleBar);
        enumIfNot("tripletFeel", value.tripletFeel, TripletFeel::None);
        // Each sign as the flag MeasureHeader names it ("segnoSegno").
        for (const NavigationSign sign : enumValues<NavigationSign>()) {
            flag(signField(sign), value.*signFlag(sign));
        }
        enumIfNot("jump", value.jump, JumpTo::None);
        if (value.jump != JumpTo::None) {
            enumIfNot("jumpThen", value.jumpThen, JumpThen::End);
        }
        textIfAny("section", value.section);
        out_.endObject();
    }

    void track(const Track& value) {
        out_.beginObject();
        text("name", value.name());
        const Instrument& instrument = value.instrument();
        out_.key("instrument");
        out_.beginObject();
        textIfAny("name", instrument.name);
        text("kind", nameOf(instrument.kind));
        if (instrument.midiProgram != 0) {
            integer("midiProgram", instrument.midiProgram);
        }
        if (instrument.writtenOffset != 0) {
            integer("writtenOffset", instrument.writtenOffset);
        }
        out_.endObject();

        if (const TrackMix& mix = value.mix(); mix != TrackMix{}) {
            out_.key("mix");
            out_.beginObject();
            if (mix.volume != TrackMix::kDefaultVolume) {
                number("volume", mix.volume);
            }
            if (mix.pan != 0.0) {
                number("pan", mix.pan);
            }
            flag("mute", mix.mute);
            flag("solo", mix.solo);
            out_.endObject();
        }
        layouts(value.measureLayouts());

        out_.key("staves");
        out_.beginArray();
        for (std::size_t s = 0; s < value.staffCount(); ++s) {
            staff(value.staff(s));
        }
        out_.endArray();
        out_.endObject();
    }

    void staff(const Staff& value) {
        out_.beginObject();
        text("clef", nameOf(value.clef()));
        if (value.tuning()) {
            text("tuning", value.tuning()->toString());
        }
        if (value.capo() != 0) {
            integer("capo", value.capo());
        }
        // The measures that change the clef (ADR-0087), apart from their
        // content: a change can stand on an empty measure at the end.
        bool clefs = false;
        for (std::size_t m = 0; m < value.measureCount(); ++m) {
            if (const std::optional<Clef>& clef = value.measure(m).clef) {
                if (!clefs) {
                    out_.key("clefs");
                    out_.beginArray();
                    clefs = true;
                }
                out_.beginObject();
                integer("measure", static_cast<std::int64_t>(m));
                text("clef", nameOf(*clef));
                out_.endObject();
            }
        }
        if (clefs) {
            out_.endArray();
        }
        // The measure repeats (ADR-0102), which stand on empty measures.
        bool repeats = false;
        for (std::size_t m = 0; m < value.measureCount(); ++m) {
            if (const MeasureRepeat repeat = value.measure(m).repeat;
                repeat != MeasureRepeat::None) {
                if (!repeats) {
                    out_.key("repeats");
                    out_.beginArray();
                    repeats = true;
                }
                out_.beginObject();
                integer("measure", static_cast<std::int64_t>(m));
                text("repeat", nameOf(repeat));
                out_.endObject();
            }
        }
        if (repeats) {
            out_.endArray();
        }
        std::size_t used = value.measureCount(); // trailing empty measures are omitted
        while (used > 0 && value.measure(used - 1).isEmpty()) {
            --used;
        }
        out_.key("measures");
        out_.beginArray();
        for (std::size_t m = 0; m < used; ++m) {
            measure(value.measure(m));
        }
        out_.endArray();
        out_.endObject();
    }

    void measure(const Measure& value) {
        std::size_t used = Measure::kMaxVoices; // trailing empty voices are omitted
        while (used > 0 && value.voices[used - 1].isEmpty()) {
            --used;
        }
        out_.beginArray();
        for (std::size_t v = 0; v < used; ++v) {
            out_.beginArray();
            for (const Beat& b : value.voices[v].beats) {
                beat(b);
            }
            out_.endArray();
        }
        out_.endArray();
    }

    void beat(const Beat& value) {
        out_.beginObject();
        text("duration", value.duration.toString());
        if (value.chord) {
            out_.key("chord");
            chord(*value.chord);
        }
        if (value.text) {
            text("text", *value.text);
        }
        if (value.lyrics) { // ADR-0084: "be-" joins the next syllable, "low_" is held
            out_.key("lyrics");
            out_.beginArray();
            for (const Syllable& syllable : *value.lyrics) {
                std::string written = syllable.text;
                if (syllable.hyphen) {
                    written += '-';
                }
                if (syllable.extend) {
                    written += '_';
                }
                out_.string(written);
            }
            out_.endArray();
        }
        if (!value.graces.empty()) {
            out_.key("graces");
            out_.beginArray();
            for (const Grace& g : value.graces) {
                grace(g);
            }
            out_.endArray();
        }
        if (!value.notes.empty()) {
            out_.key("notes");
            out_.beginArray();
            for (const Note& n : value.notes) {
                note(n);
            }
            out_.endArray();
        }
        if (value.effects != BeatEffects{}) {
            out_.key("effects");
            beatEffects(value.effects);
        }
        out_.endObject();
    }

    void chord(const ChordSymbol& value) {
        out_.beginObject();
        textIfAny("name", value.name);
        if (value.diagram) {
            out_.key("diagram");
            out_.beginObject();
            integer("firstFret", value.diagram->firstFret);
            out_.key("frets");
            out_.beginArray();
            for (const std::int8_t fret : value.diagram->frets) {
                out_.integer(fret);
            }
            out_.endArray();
            out_.endObject();
        }
        out_.endObject();
    }

    void grace(const Grace& value) {
        out_.beginObject();
        text("duration", value.duration.toString());
        if (!value.notes.empty()) {
            out_.key("notes");
            out_.beginArray();
            for (const Note& n : value.notes) {
                note(n);
            }
            out_.endArray();
        }
        flag("onBeat", value.onBeat);
        out_.endObject();
    }

    void note(const Note& value) {
        out_.beginObject();
        text("pitch", value.pitch.toString());
        if (value.position) {
            integer("string", value.position->string);
            integer("fret", value.position->fret);
        }
        if (value.effects != NoteEffects{}) {
            out_.key("effects");
            noteEffects(value.effects);
        }
        out_.endObject();
    }

    void curve(const BendCurve& value) {
        out_.beginArray();
        for (const BendPoint& point : value.points()) {
            out_.beginArray();
            out_.integer(point.position);
            out_.integer(point.cents);
            out_.endArray();
        }
        out_.endArray();
    }

    void noteEffects(const NoteEffects& fx) {
        out_.beginObject();
        if (!fx.bend.isEmpty()) {
            out_.key("bend");
            curve(fx.bend);
        }
        if (fx.harmonic != Harmonic{}) {
            out_.key("harmonic");
            out_.beginObject();
            text("kind", nameOf(fx.harmonic.kind));
            if (fx.harmonic.fretTenths != 0) {
                number("fret", fx.harmonic.fretTenths / 10.0);
            }
            out_.endObject();
        }
        if (fx.trill.isTrill()) {
            out_.key("trill");
            out_.beginObject();
            integer("semitones", fx.trill.semitones);
            text("speed", Duration(fx.trill.speed).toString());
            out_.endObject();
        }
        flag("tiedFromPrevious", fx.tiedFromPrevious);
        flag("legatoFromPrevious", fx.legatoFromPrevious);
        enumIfNot("slideIn", fx.slideIn, SlideIn::None);
        enumIfNot("slideOut", fx.slideOut, SlideOut::None);
        enumIfNot("vibrato", fx.vibrato, Vibrato::None);
        flag("palmMute", fx.palmMute);
        flag("deadNote", fx.deadNote);
        flag("ghost", fx.ghost);
        flag("letRing", fx.letRing);
        flag("staccato", fx.staccato);
        flag("staccatissimo", fx.staccatissimo);
        flag("tenuto", fx.tenuto);
        enumIfNot("accent", fx.accent, Accent::None);
        enumIfNot("ornament", fx.ornament, Ornament::None);
        enumIfNot("fingering", fx.fingering, Finger::None);
        enumIfNot("pluck", fx.pluck, Finger::None);
        enumIfNot("dynamic", fx.dynamic, Dynamic::MF);
        out_.endObject();
    }

    void beatEffects(const BeatEffects& fx) {
        out_.beginObject();
        if (!fx.whammyBar.isEmpty()) {
            out_.key("whammyBar");
            curve(fx.whammyBar);
        }
        enumIfNot("pickStroke", fx.pickStroke, PickStroke::None);
        enumIfNot("strum", fx.strum, Strum::None);
        if (fx.tremoloPicking) {
            text("tremoloPicking", Duration(*fx.tremoloPicking).toString());
        }
        enumIfNot("bassTechnique", fx.bassTechnique, BassTechnique::None);
        enumIfNot("hairpin", fx.hairpin, Hairpin::None);
        enumIfNot("fermata", fx.fermata, Fermata::None);
        flag("slur", fx.slur);
        enumIfNot("ottava", fx.ottava, Ottava::None);
        enumIfNot("pedal", fx.pedal, Pedal::None);
        enumIfNot("breath", fx.breath, Breath::None);
        out_.endObject();
    }

    JsonOut out_;
};

// ============================================================================
// Reading
// ============================================================================

struct ReadError {
    std::string message;
};

// Where the reader is, kept as segments and only turned into text on error.
class JsonPath {
public:
    struct Segment {
        const char* key = nullptr; // nullptr: an array index
        std::size_t index = 0;
    };

    void push(Segment segment) { segments_.push_back(segment); }
    void pop() { segments_.pop_back(); }

    [[nodiscard]] std::string toString() const {
        std::string text;
        for (const auto& segment : segments_) {
            if (segment.key != nullptr) {
                if (!text.empty()) {
                    text += '.';
                }
                text += segment.key;
            } else {
                text += std::format("[{}]", segment.index);
            }
        }
        return text;
    }

private:
    std::vector<Segment> segments_;
};

class Reader {
public:
    Song read(yyjson_val* root) {
        if (!yyjson_is_obj(root)) {
            fail("the document is not a JSON object");
        }
        const auto format = optionalText(root, "format");
        if (format != kFormatName) {
            fail("not an Elara song (missing \"format\": \"elara\")");
        }
        const std::int64_t formatVersion =
            integer(root, "formatVersion", 1, std::numeric_limits<int>::max());
        if (formatVersion > kNativeFormatVersion) {
            fail(std::format("made by a newer version of Elara (format {}; this version reads "
                             "format {})",
                             formatVersion, kNativeFormatVersion));
        }

        Song song;
        if (yyjson_val* info = optionalObject(root, "info")) {
            Scope scope(*this, "info");
            readInfo(info, song.info());
        }
        if (yyjson_val* tempo = optionalObject(root, "tempo")) {
            Scope scope(*this, "tempo");
            song.setStartTempo(readTempo(tempo));
        }
        if (yyjson_val* measures = optionalArray(root, "measures")) {
            Scope scope(*this, "measures");
            song.appendMeasures(yyjson_arr_size(measures));
            forEach(measures, [&](std::size_t m, yyjson_val* header) {
                song.measureHeader(m) = readHeader(header);
            });
        }
        readLayouts(root, song, LayoutScope::song());
        if (yyjson_val* tracks = optionalArray(root, "tracks")) {
            Scope scope(*this, "tracks");
            forEach(tracks, [&](std::size_t, yyjson_val* track) { readTrack(track, song); });
        }
        // Who wrote the file says nothing about the song: anything but text
        // is left alone, as an unknown key would be.
        if (yyjson_val* generator = yyjson_obj_get(root, "generator"); yyjson_is_str(generator)) {
            generator_.assign(yyjson_get_str(generator), yyjson_get_len(generator));
        }
        return song;
    }

    /// The "generator" of the song last read ("Elara 0.1.0"), or nothing.
    [[nodiscard]] const std::string& generator() const { return generator_; }

private:
    class Scope {
    public:
        Scope(Reader& reader, const char* key) : reader_(reader) {
            reader_.path_.push({.key = key, .index = 0});
        }
        Scope(Reader& reader, std::size_t index) : reader_(reader) {
            reader_.path_.push({.key = nullptr, .index = index});
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&&) = delete;
        Scope& operator=(Scope&&) = delete;
        ~Scope() { reader_.path_.pop(); }

    private:
        Reader& reader_;
    };

    [[noreturn]] void fail(std::string_view message) const {
        const std::string where = path_.toString();
        throw ReadError{where.empty() ? std::string(message)
                                      : std::format("{}: {}", where, message)};
    }

    // Runs `body` and turns the model's own validation errors into located ones.
    template <typename Body>
    auto guarded(Body body) const {
        try {
            return body();
        } catch (const std::invalid_argument& error) {
            fail(error.what());
        } catch (const std::out_of_range& error) {
            fail(error.what());
        }
    }

    template <typename Body>
    void forEach(yyjson_val* array, Body body) {
        yyjson_arr_iter iter = yyjson_arr_iter_with(array);
        std::size_t index = 0;
        while (yyjson_val* item = yyjson_arr_iter_next(&iter)) {
            Scope scope(*this, index);
            body(index, item);
            ++index;
        }
    }

    // ---- Typed access -------------------------------------------------------

    template <typename Is>
    yyjson_val* optionalOf(yyjson_val* object, const char* key, Is is, const char* what) {
        yyjson_val* value = yyjson_obj_get(object, key);
        if (value != nullptr && !is(value)) {
            Scope scope(*this, key);
            fail(std::format("expected {}", what));
        }
        return value;
    }

    yyjson_val* optionalObject(yyjson_val* object, const char* key) {
        return optionalOf(object, key, yyjson_is_obj, "an object");
    }
    yyjson_val* optionalArray(yyjson_val* object, const char* key) {
        return optionalOf(object, key, yyjson_is_arr, "an array");
    }
    yyjson_val* requiredArray(yyjson_val* object, const char* key) {
        yyjson_val* value = optionalArray(object, key);
        if (value == nullptr) {
            fail(std::format("missing \"{}\"", key));
        }
        return value;
    }

    std::optional<std::string_view> optionalText(yyjson_val* object, const char* key) {
        yyjson_val* value = optionalOf(object, key, yyjson_is_str, "a string");
        if (value == nullptr) {
            return std::nullopt;
        }
        return std::string_view(yyjson_get_str(value), yyjson_get_len(value));
    }
    std::string_view requiredText(yyjson_val* object, const char* key) {
        const auto value = optionalText(object, key);
        if (!value) {
            fail(std::format("missing \"{}\"", key));
        }
        return *value;
    }

    std::optional<double> optionalNumber(yyjson_val* object, const char* key) {
        yyjson_val* value = optionalOf(object, key, yyjson_is_num, "a number");
        return value == nullptr ? std::nullopt : std::optional(yyjson_get_num(value));
    }

    std::optional<double> optionalNumberIn(yyjson_val* object, const char* key, double min,
                                           double max) {
        const auto value = optionalNumber(object, key);
        if (value && !(*value >= min && *value <= max)) {
            Scope scope(*this, key);
            fail(std::format("{} is outside {}..{}", *value, min, max));
        }
        return value;
    }

    std::optional<std::int64_t> optionalInteger(yyjson_val* object, const char* key,
                                                std::int64_t min, std::int64_t max) {
        yyjson_val* value = yyjson_obj_get(object, key);
        if (value == nullptr) {
            return std::nullopt;
        }
        Scope scope(*this, key);
        return integerValue(value, min, max);
    }
    std::int64_t integer(yyjson_val* object, const char* key, std::int64_t min, std::int64_t max) {
        const auto value = optionalInteger(object, key, min, max);
        if (!value) {
            fail(std::format("missing \"{}\"", key));
        }
        return *value;
    }
    std::int64_t integerValue(yyjson_val* value, std::int64_t min, std::int64_t max) const {
        std::optional<std::int64_t> number;
        if (yyjson_is_sint(value)) {
            number = yyjson_get_sint(value);
        } else if (yyjson_is_uint(value) &&
                   yyjson_get_uint(value) <=
                       static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            number = static_cast<std::int64_t>(yyjson_get_uint(value));
        }
        if (!number) {
            fail("expected an integer");
        }
        if (*number < min || *number > max) {
            fail(std::format("{} is outside {}..{}", *number, min, max));
        }
        return *number;
    }

    bool flag(yyjson_val* object, const char* key) {
        yyjson_val* value = optionalOf(object, key, yyjson_is_bool, "true or false");
        return value != nullptr && yyjson_get_bool(value);
    }

    template <NamedEnum Enum>
    Enum enumValue(yyjson_val* object, const char* key, Enum fallback) {
        const auto text = optionalText(object, key);
        if (!text) {
            return fallback;
        }
        const auto value = parseName<Enum>(*text);
        if (!value) {
            Scope scope(*this, key);
            fail(std::format("unknown value: {}", *text));
        }
        return *value;
    }

    // Parses a text field with one of the model's parse() functions.
    template <typename T, typename Parse>
    T parsed(yyjson_val* object, const char* key, Parse parse, const char* what) {
        const std::string_view text = requiredText(object, key);
        const std::optional<T> value = parse(text);
        if (!value) {
            Scope scope(*this, key);
            fail(std::format("not {}: {}", what, text));
        }
        return *value;
    }

    Duration duration(yyjson_val* object, const char* key) {
        return parsed<Duration>(object, key, Duration::parse, "a duration");
    }

    // ---- Song parts -----------------------------------------------------------

    void readInfo(yyjson_val* object, SongInfo& info) {
        const auto text = [&](const char* key, std::string& field) {
            if (const auto value = optionalText(object, key)) {
                field = std::string(*value);
            }
        };
        text("title", info.title);
        text("subtitle", info.subtitle);
        text("artist", info.artist);
        text("album", info.album);
        text("words", info.words);
        text("music", info.music);
        text("copyright", info.copyright);
        text("transcriber", info.transcriber);
        text("notes", info.notes);
    }

    Tempo readTempo(yyjson_val* object) {
        const auto bpm = optionalNumber(object, "bpm");
        if (!bpm) {
            fail("missing \"bpm\"");
        }
        const Duration unit = yyjson_obj_get(object, "beatUnit") != nullptr
                                  ? duration(object, "beatUnit")
                                  : Duration{};
        return guarded([&] { return Tempo(*bpm, unit); });
    }

    Fraction offset(yyjson_val* object) {
        const auto text = optionalText(object, "offset");
        if (!text) {
            return Fraction(0);
        }
        const auto slash = text->find('/');
        const auto number = [](std::string_view part) -> std::optional<std::int64_t> {
            std::int64_t value = 0;
            const auto* end = part.data() + part.size();
            const auto [ptr, error] = std::from_chars(part.data(), end, value);
            if (part.empty() || error != std::errc{} || ptr != end) {
                return std::nullopt;
            }
            return value;
        };
        const auto num = number(text->substr(0, slash));
        const auto den = slash == std::string_view::npos ? std::optional<std::int64_t>(1)
                                                         : number(text->substr(slash + 1));
        if (!num || !den || *den <= 0 || *num < 0) {
            Scope scope(*this, "offset");
            fail(std::format("not a position: {}", *text));
        }
        return Fraction(*num, *den);
    }

    MeasureHeader readHeader(yyjson_val* object) {
        if (!yyjson_is_obj(object)) {
            fail("expected an object");
        }
        MeasureHeader header;
        if (yyjson_obj_get(object, "timeSignature") != nullptr) {
            header.timeSignature = parsed<TimeSignature>(object, "timeSignature",
                                                         TimeSignature::parse, "a time signature");
        }
        if (yyjson_obj_get(object, "keySignature") != nullptr) {
            header.keySignature =
                parsed<KeySignature>(object, "keySignature", KeySignature::parse, "a key");
        }
        if (yyjson_val* changes = optionalArray(object, "tempoChanges")) {
            Scope scope(*this, "tempoChanges");
            forEach(changes, [&](std::size_t, yyjson_val* change) {
                if (!yyjson_is_obj(change)) {
                    fail("expected an object");
                }
                const Fraction at = offset(change);
                const bool ascending =
                    header.tempoChanges.empty() || header.tempoChanges.back().offset < at;
                if (at >= header.timeSignature.measureLength() || !ascending) {
                    fail("tempo changes must be in ascending order inside the measure");
                }
                header.tempoChanges.push_back(TempoChange{
                    .offset = at, .tempo = readTempo(change), .gradual = flag(change, "gradual")});
            });
        }
        header.repeatStart = flag(object, "repeatStart");
        header.repeatTimes =
            static_cast<std::uint8_t>(optionalInteger(object, "repeatTimes", 2, 255).value_or(0));
        if (yyjson_val* endings = optionalArray(object, "alternateEndings")) {
            Scope scope(*this, "alternateEndings");
            int last = 0;
            forEach(endings, [&](std::size_t, yyjson_val* ending) {
                const auto time =
                    static_cast<int>(integerValue(ending, 1, MeasureHeader::kMaxEnding));
                if (time <= last) {
                    fail("alternate endings must be in ascending order");
                }
                last = time;
                header.alternateEndings =
                    static_cast<std::uint8_t>(header.alternateEndings | (1U << (time - 1)));
            });
        }
        header.doubleBar = flag(object, "doubleBar");
        header.tripletFeel = enumValue(object, "tripletFeel", TripletFeel::None);
        for (const NavigationSign sign : enumValues<NavigationSign>()) {
            header.*signFlag(sign) = flag(object, signField(sign).data());
        }
        header.jump = enumValue(object, "jump", JumpTo::None);
        if (header.jump != JumpTo::None) {
            header.jumpThen = enumValue(object, "jumpThen", JumpThen::End);
        }
        if (const auto section = optionalText(object, "section")) {
            header.section = std::string(*section);
        }
        return header;
    }

    void readTrack(yyjson_val* object, Song& song) {
        if (!yyjson_is_obj(object)) {
            fail("expected an object");
        }
        Instrument instrument;
        if (yyjson_val* inst = optionalObject(object, "instrument")) {
            Scope scope(*this, "instrument");
            instrument.name = std::string(optionalText(inst, "name").value_or(""));
            instrument.kind = enumValue(inst, "kind", InstrumentKind::Pitched);
            instrument.midiProgram =
                static_cast<int>(optionalInteger(inst, "midiProgram", 0, 127).value_or(0));
            instrument.writtenOffset =
                static_cast<int>(optionalInteger(inst, "writtenOffset", -127, 127).value_or(0));
        }
        TrackMix mix;
        if (yyjson_val* mixJson = optionalObject(object, "mix")) {
            Scope scope(*this, "mix");
            mix.volume = optionalNumberIn(mixJson, "volume", 0.0, 1.0).value_or(mix.volume);
            mix.pan = optionalNumberIn(mixJson, "pan", -1.0, 1.0).value_or(mix.pan);
            mix.mute = flag(mixJson, "mute");
            mix.solo = flag(mixJson, "solo");
        }

        yyjson_val* stavesJson = requiredArray(object, "staves");
        std::vector<Staff> staves;
        {
            Scope scope(*this, "staves");
            forEach(stavesJson, [&](std::size_t, yyjson_val* staff) {
                if (!yyjson_is_obj(staff)) {
                    fail("expected an object");
                }
                const Clef clef = enumValue(staff, "clef", Clef::Treble);
                std::optional<Tuning> tuning;
                if (yyjson_obj_get(staff, "tuning") != nullptr) {
                    tuning = parsed<Tuning>(staff, "tuning", Tuning::parse, "a tuning");
                }
                const auto capo = optionalInteger(staff, "capo", 0, Tuning::kMaxFret).value_or(0);
                staves.push_back(
                    guarded([&] { return Staff(clef, tuning, static_cast<int>(capo)); }));
            });
        }
        const std::string name(optionalText(object, "name").value_or(""));
        const std::size_t index = guarded(
            [&] { return song.addTrack(Track(name, std::move(instrument), std::move(staves))); });

        Track& track = song.track(index);
        track.setMix(mix);
        readLayouts(object, song, LayoutScope::ofTrack(index));
        Scope stavesScope(*this, "staves");
        forEach(stavesJson, [&](std::size_t s, yyjson_val* staffJson) {
            readMeasures(staffJson, track.staff(s), song.measureCount());
            readClefs(staffJson, track.staff(s), song.measureCount());
            readRepeats(staffJson, track.staff(s), song.measureCount());
        });
    }

    // The measure layouts of a scope (ADR-0152): the measures that differ.
    void readLayouts(yyjson_val* object, Song& song, LayoutScope scope) {
        yyjson_val* entries = optionalArray(object, "layout");
        if (entries == nullptr) {
            return;
        }
        Scope where(*this, "layout");
        const std::size_t count = song.measureCount();
        std::vector<MeasureLayout> layouts(count);
        forEach(entries, [&](std::size_t, yyjson_val* entry) {
            if (!yyjson_is_obj(entry)) {
                fail("expected an object");
            }
            const auto m = static_cast<std::size_t>(
                integer(entry, "measure", 0, static_cast<std::int64_t>(count) - 1));
            layouts[m].breakBefore = enumValue(entry, "break", Break::Auto);
            layouts[m].spacing = static_cast<std::uint8_t>(
                optionalInteger(entry, "spacing", kMinSpacing, kMaxSpacing)
                    .value_or(MeasureLayout::kDefaultSpacing));
        });
        song.setMeasureLayouts(scope, MeasureLayouts(std::move(layouts)));
    }

    // The measures that change the staff's clef (ADR-0087).
    void readClefs(yyjson_val* staffJson, Staff& staff, std::size_t measureCount) {
        yyjson_val* clefs = optionalArray(staffJson, "clefs");
        if (clefs == nullptr) {
            return;
        }
        Scope scope(*this, "clefs");
        forEach(clefs, [&](std::size_t, yyjson_val* change) {
            if (!yyjson_is_obj(change)) {
                fail("expected an object");
            }
            const auto m = static_cast<std::size_t>(
                integer(change, "measure", 0, static_cast<std::int64_t>(measureCount) - 1));
            const std::string_view name = requiredText(change, "clef");
            const auto clef = parseName<Clef>(name);
            if (!clef) {
                Scope inner(*this, "clef");
                fail(std::format("unknown value: {}", name));
            }
            staff.measure(m).clef = *clef;
        });
    }

    // The staff's measure repeats (ADR-0102).
    void readRepeats(yyjson_val* staffJson, Staff& staff, std::size_t measureCount) {
        yyjson_val* repeats = optionalArray(staffJson, "repeats");
        if (repeats == nullptr) {
            return;
        }
        Scope scope(*this, "repeats");
        forEach(repeats, [&](std::size_t, yyjson_val* sign) {
            if (!yyjson_is_obj(sign)) {
                fail("expected an object");
            }
            const auto m = static_cast<std::size_t>(
                integer(sign, "measure", 0, static_cast<std::int64_t>(measureCount) - 1));
            const std::string_view name = requiredText(sign, "repeat");
            const auto repeat = parseName<MeasureRepeat>(name);
            if (!repeat) {
                Scope inner(*this, "repeat");
                fail(std::format("unknown value: {}", name));
            }
            staff.measure(m).repeat = *repeat;
        });
    }

    void readMeasures(yyjson_val* staffJson, Staff& staff, std::size_t measureCount) {
        yyjson_val* measures = optionalArray(staffJson, "measures");
        if (measures == nullptr) {
            return;
        }
        Scope scope(*this, "measures");
        if (yyjson_arr_size(measures) > measureCount) {
            fail(std::format("{} measures, but the song has {}", yyjson_arr_size(measures),
                             measureCount));
        }
        const int strings = staff.tuning() ? staff.tuning()->stringCount() : Tuning::kMaxStrings;
        forEach(measures, [&](std::size_t m, yyjson_val* voices) {
            if (!yyjson_is_arr(voices) || yyjson_arr_size(voices) > Measure::kMaxVoices) {
                fail(std::format("expected an array of at most {} voices", Measure::kMaxVoices));
            }
            Measure& measure = staff.measure(m);
            forEach(voices, [&](std::size_t v, yyjson_val* beats) {
                if (!yyjson_is_arr(beats)) {
                    fail("expected an array of beats");
                }
                auto& target = measure.voices[v].beats;
                target.reserve(yyjson_arr_size(beats));
                forEach(beats, [&](std::size_t, yyjson_val* beat) {
                    target.push_back(readBeat(beat, strings));
                });
            });
        });
    }

    // One syllable per verse, "" for a verse that sings nothing (ADR-0084).
    SharedOptional<std::vector<Syllable>> readLyrics(yyjson_val* array) {
        std::vector<Syllable> verses;
        verses.reserve(yyjson_arr_size(array));
        forEach(array, [&](std::size_t, yyjson_val* item) {
            if (!yyjson_is_str(item)) {
                fail("expected a string");
            }
            std::string_view text(yyjson_get_str(item), yyjson_get_len(item));
            const bool extend = text.size() > 1 && text.ends_with('_');
            if (extend) {
                text.remove_suffix(1);
            }
            const bool hyphen = text.size() > 1 && text.ends_with('-');
            if (hyphen) {
                text.remove_suffix(1);
            }
            verses.push_back(
                Syllable{.text = std::string(text), .hyphen = hyphen, .extend = extend});
        });
        while (!verses.empty() && verses.back().text.empty()) {
            verses.pop_back();
        }
        return verses.empty() ? SharedOptional<std::vector<Syllable>>{}
                              : SharedOptional(std::move(verses));
    }

    Beat readBeat(yyjson_val* object, int strings) {
        if (!yyjson_is_obj(object)) {
            fail("expected an object");
        }
        Beat beat{.duration = duration(object, "duration"), .notes = {}};
        if (yyjson_val* chord = optionalObject(object, "chord")) {
            Scope scope(*this, "chord");
            beat.chord = readChord(chord);
        }
        if (const auto text = optionalText(object, "text")) {
            beat.text = SharedOptional(std::string(*text)); // ADR-0081
        }
        if (yyjson_val* lyrics = optionalArray(object, "lyrics")) {
            Scope scope(*this, "lyrics");
            beat.lyrics = readLyrics(lyrics);
        }
        if (yyjson_val* graces = optionalArray(object, "graces")) {
            Scope scope(*this, "graces");
            beat.graces.reserve(yyjson_arr_size(graces));
            forEach(graces, [&](std::size_t, yyjson_val* grace) {
                beat.graces.push_back(readGrace(grace, strings));
            });
        }
        if (yyjson_val* notes = optionalArray(object, "notes")) {
            Scope scope(*this, "notes");
            beat.notes.reserve(yyjson_arr_size(notes));
            forEach(notes, [&](std::size_t, yyjson_val* note) {
                beat.notes.push_back(readNote(note, strings));
            });
        }
        if (yyjson_val* effects = optionalObject(object, "effects")) {
            Scope scope(*this, "effects");
            beat.effects = readBeatEffects(effects);
        }
        return beat;
    }

    // Beats that name the same chord share one symbol (ADR-0080).
    SharedOptional<ChordSymbol> readChord(yyjson_val* object) {
        ChordSymbol symbol{.name = std::string(optionalText(object, "name").value_or("")),
                           .diagram = {}};
        if (yyjson_val* diagram = optionalObject(object, "diagram")) {
            Scope scope(*this, "diagram");
            ChordDiagram grid{.firstFret = static_cast<std::uint8_t>(
                                  integer(diagram, "firstFret", 1, Tuning::kMaxFret)),
                              .frets = {}};
            yyjson_val* frets = requiredArray(diagram, "frets");
            Scope inner(*this, "frets");
            const std::size_t count = yyjson_arr_size(frets);
            if (count < 1 || count > static_cast<std::size_t>(Tuning::kMaxStrings)) {
                fail(std::format("expected 1 to {} strings", Tuning::kMaxStrings));
            }
            grid.frets.reserve(count);
            forEach(frets, [&](std::size_t, yyjson_val* fret) {
                grid.frets.push_back(static_cast<std::int8_t>(
                    integerValue(fret, ChordDiagram::kMuted, Tuning::kMaxFret)));
            });
            symbol.diagram = std::move(grid);
        }
        return chords_.share(std::move(symbol));
    }

    Grace readGrace(yyjson_val* object, int strings) {
        if (!yyjson_is_obj(object)) {
            fail("expected an object");
        }
        Grace grace{.duration = duration(object, "duration"), .notes = {}};
        if (yyjson_val* notes = optionalArray(object, "notes")) {
            Scope scope(*this, "notes");
            grace.notes.reserve(yyjson_arr_size(notes));
            forEach(notes, [&](std::size_t, yyjson_val* note) {
                grace.notes.push_back(readNote(note, strings));
            });
        }
        grace.onBeat = flag(object, "onBeat");
        return grace;
    }

    Note readNote(yyjson_val* object, int strings) {
        if (!yyjson_is_obj(object)) {
            fail("expected an object");
        }
        Note note{.pitch = parsed<Pitch>(object, "pitch", Pitch::parse, "a pitch"),
                  .position = std::nullopt};
        const auto string = optionalInteger(object, "string", 1, strings);
        const auto fret = optionalInteger(object, "fret", 0, Tuning::kMaxFret);
        if (string.has_value() != fret.has_value()) {
            fail("\"string\" and \"fret\" go together");
        }
        if (string) {
            note.position = FretPosition{.string = static_cast<std::int8_t>(*string),
                                         .fret = static_cast<std::int8_t>(*fret)};
        }
        if (yyjson_val* effects = optionalObject(object, "effects")) {
            Scope scope(*this, "effects");
            note.effects = readNoteEffects(effects);
        }
        return note;
    }

    BendCurve readCurve(yyjson_val* object, const char* key) {
        yyjson_val* points = optionalArray(object, key);
        if (points == nullptr) {
            return {};
        }
        Scope scope(*this, key);
        std::vector<BendPoint> result;
        forEach(points, [&](std::size_t, yyjson_val* pair) {
            if (!yyjson_is_arr(pair) || yyjson_arr_size(pair) != 2) {
                fail("expected [position, cents]");
            }
            constexpr std::int64_t kLimit = 1'000'000; // the curve checks the real limits
            result.push_back(BendPoint{
                .position =
                    static_cast<int>(integerValue(yyjson_arr_get(pair, 0), -kLimit, kLimit)),
                .cents = static_cast<int>(integerValue(yyjson_arr_get(pair, 1), -kLimit, kLimit))});
        });
        return guarded([&] { return BendCurve(std::move(result)); });
    }

    NoteEffects readNoteEffects(yyjson_val* object) {
        NoteEffects fx;
        fx.bend = readCurve(object, "bend");
        if (yyjson_val* harmonic = optionalObject(object, "harmonic")) {
            Scope scope(*this, "harmonic");
            fx.harmonic.kind = enumValue(harmonic, "kind", HarmonicKind::None);
            if (const auto fret = optionalNumber(harmonic, "fret")) {
                const double tenths = std::round(*fret * 10.0);
                if (!(tenths >= 0.0 && tenths <= Tuning::kMaxFret * 10.0)) {
                    fail(std::format("harmonic fret outside 0..{}", Tuning::kMaxFret));
                }
                fx.harmonic.fretTenths = static_cast<std::int16_t>(tenths);
            }
        }
        if (yyjson_val* trill = optionalObject(object, "trill")) {
            Scope scope(*this, "trill");
            const auto semitones =
                integer(trill, "semitones", -Trill::kMaxSemitones, Trill::kMaxSemitones);
            if (semitones == 0) {
                fail("a trill needs semitones other than 0");
            }
            fx.trill.semitones = static_cast<std::int8_t>(semitones);
            if (yyjson_obj_get(trill, "speed") != nullptr) {
                const Duration speed = duration(trill, "speed");
                if (speed.dots() != 0 || !speed.tuplet().isNone()) {
                    Scope inner(*this, "speed");
                    fail("expected a plain note value such as 32");
                }
                fx.trill.speed = speed.value();
            }
        }
        fx.tiedFromPrevious = flag(object, "tiedFromPrevious");
        fx.legatoFromPrevious = flag(object, "legatoFromPrevious");
        fx.slideIn = enumValue(object, "slideIn", SlideIn::None);
        fx.slideOut = enumValue(object, "slideOut", SlideOut::None);
        fx.vibrato = enumValue(object, "vibrato", Vibrato::None);
        fx.palmMute = flag(object, "palmMute");
        fx.deadNote = flag(object, "deadNote");
        fx.ghost = flag(object, "ghost");
        fx.letRing = flag(object, "letRing");
        fx.staccato = flag(object, "staccato");
        fx.staccatissimo = flag(object, "staccatissimo");
        fx.tenuto = flag(object, "tenuto");
        fx.accent = enumValue(object, "accent", Accent::None);
        fx.ornament = enumValue(object, "ornament", Ornament::None);
        fx.fingering = enumValue(object, "fingering", Finger::None);
        fx.pluck = enumValue(object, "pluck", Finger::None);
        fx.dynamic = enumValue(object, "dynamic", Dynamic::MF);
        return fx;
    }

    BeatEffects readBeatEffects(yyjson_val* object) {
        BeatEffects fx;
        fx.whammyBar = readCurve(object, "whammyBar");
        fx.pickStroke = enumValue(object, "pickStroke", PickStroke::None);
        fx.strum = enumValue(object, "strum", Strum::None);
        if (yyjson_obj_get(object, "tremoloPicking") != nullptr) {
            const Duration speed = duration(object, "tremoloPicking");
            if (speed.dots() != 0 || !speed.tuplet().isNone()) {
                Scope scope(*this, "tremoloPicking");
                fail("expected a plain note value such as 16");
            }
            fx.tremoloPicking = speed.value();
        }
        fx.bassTechnique = enumValue(object, "bassTechnique", BassTechnique::None);
        fx.hairpin = enumValue(object, "hairpin", Hairpin::None);
        fx.fermata = enumValue(object, "fermata", Fermata::None);
        fx.slur = flag(object, "slur");
        fx.ottava = enumValue(object, "ottava", Ottava::None);
        fx.pedal = enumValue(object, "pedal", Pedal::None);
        fx.breath = enumValue(object, "breath", Breath::None);
        return fx;
    }

    JsonPath path_;
    detail::ChordPool chords_;
    std::string generator_;
};

using Version = std::array<int, 3>;

// The three numbers "X.Y.Z" start with, whatever follows them ("-beta.2").
std::optional<Version> versionOf(std::string_view text) {
    Version numbers{};
    const char* at = text.data();
    const char* const end = text.data() + text.size();
    for (std::size_t i = 0; i < numbers.size(); ++i) {
        if (i > 0) {
            if (at == end || *at != '.') {
                return std::nullopt;
            }
            ++at;
        }
        const auto [next, problem] = std::from_chars(at, end, numbers[i]);
        if (problem != std::errc{} || next == at) {
            return std::nullopt;
        }
        at = next;
    }
    return numbers;
}

// What reading a song written by `generator` cannot keep (ADR-0139): a newer
// Elara can have added data this one does not read.
std::optional<std::string> newerWarning(std::string_view generator) {
    if (!generator.starts_with(kGeneratorName)) {
        return std::nullopt;
    }
    const auto theirs = versionOf(generator.substr(kGeneratorName.size()));
    const auto ours = versionOf(version());
    if (!theirs || !ours || *theirs <= *ours) {
        return std::nullopt;
    }
    return std::format("saved by {}, newer than this one ({}): what it added is not shown, and "
                       "saving here leaves it out",
                       generator, version());
}

Song songOf(ImportResult&& read) {
    return std::move(read.song);
}

} // namespace

std::string songToJson(const Song& song) {
    return Writer().write(song);
}

namespace {

// yyjson's pool of values starts at a guess from the text's size, a value for
// every 6 bytes, and grows by half through realloc() when the guess falls
// short, holding both blocks while it copies. song.json has a value for about
// every 5.5 bytes, so the pool of a large song grows once. Each block gets
// that half more room when it is made, and a realloc() that fits stays.
namespace roomy {

constexpr std::size_t kHeader = 16; // the block's room, keeping malloc()'s alignment

void* allocate(void* /*context*/, std::size_t size) noexcept {
    const std::size_t room = size + (size / 2);
    auto* block = static_cast<unsigned char*>(std::malloc(kHeader + room));
    if (block == nullptr) {
        return nullptr;
    }
    std::memcpy(block, &room, sizeof room);
    return block + kHeader;
}

void release(void* /*context*/, void* data) noexcept {
    if (data != nullptr) {
        std::free(static_cast<unsigned char*>(data) - kHeader);
    }
}

void* reallocate(void* context, void* data, std::size_t oldSize, std::size_t size) noexcept {
    std::size_t room = 0;
    std::memcpy(&room, static_cast<unsigned char*>(data) - kHeader, sizeof room);
    if (size <= room) {
        return data;
    }
    void* grown = allocate(context, size);
    if (grown != nullptr) {
        std::memcpy(grown, data, std::min(oldSize, size));
        release(context, data);
    }
    return grown;
}

constexpr yyjson_alc kAllocator{
    .malloc = allocate, .realloc = reallocate, .free = release, .ctx = nullptr};

} // namespace roomy

// Reads `length` bytes of song.json at `text`. Without YYJSON_READ_INSITU in
// `flags` yyjson copies them and never modifies them; with it, it parses them
// where they are, which needs YYJSON_PADDING_SIZE zero bytes after them.
std::expected<ImportResult, FormatError> readJson(char* text, std::size_t length,
                                                  yyjson_read_flag flags,
                                                  const yyjson_alc* allocator = nullptr) {
    yyjson_read_err error{};
    yyjson_doc* doc = yyjson_read_opts(text, length, flags, allocator, &error);
    if (doc == nullptr) {
        return std::unexpected(FormatError{
            std::format("invalid JSON at byte {}: {}", error.pos, error.msg ? error.msg : "")});
    }
    const std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> owner(doc, yyjson_doc_free);
    try {
        Reader reader;
        ImportResult result{.song = reader.read(yyjson_doc_get_root(doc)), .warnings = {}};
        if (auto warning = newerWarning(reader.generator())) {
            result.warnings.push_back(std::move(*warning));
        }
        return result;
    } catch (const ReadError& failure) {
        return std::unexpected(FormatError{failure.message});
    }
}

// The song.json of a native file's bytes, followed by the zero bytes that
// readInPlace() needs.
std::expected<std::string, FormatError> nativeJson(std::span<const std::uint8_t> bytes) {
    try {
        detail::ZipReader zip(bytes);
        if (!zip.ok()) {
            return std::unexpected(FormatError{"not an Elara file (not a zip archive)"});
        }
        const auto mime = zip.entry("mimetype", 256);
        if (mime && *mime != kNativeMimeType) {
            return std::unexpected(
                FormatError{std::format("not an Elara file (mimetype {})", *mime)});
        }
        auto json = zip.entry("song.json", detail::kMaxEntryBytes, YYJSON_PADDING_SIZE);
        if (!json) {
            return std::unexpected(FormatError{"not an Elara file (no song.json)"});
        }
        return *std::move(json);
    } catch (const FormatError& failure) {
        return std::unexpected(failure);
    }
}

// Reads nativeJson()'s text where it is, without a copy of it.
std::expected<ImportResult, FormatError> readInPlace(std::string&& json) {
    return readJson(json.data(), json.size() - YYJSON_PADDING_SIZE, YYJSON_READ_INSITU,
                    &roomy::kAllocator);
}

} // namespace

std::expected<ImportResult, FormatError> readSongJson(std::string_view json) {
    return readJson(const_cast<char*>(json.data()), json.size(), YYJSON_READ_NOFLAG);
}

std::expected<Song, FormatError> songFromJson(std::string_view json) {
    return readSongJson(json).transform(songOf);
}

std::vector<std::uint8_t> encodeNative(const Song& song) {
    const std::string json = Writer().write(song);
    const std::array entries{
        detail::ZipEntry{.name = "mimetype", .content = kNativeMimeType, .level = 0},
        detail::ZipEntry{.name = "song.json", .content = json}};
    return detail::writeZip(entries);
}

std::expected<ImportResult, FormatError> readNative(std::span<const std::uint8_t> bytes) {
    return nativeJson(bytes).and_then(readInPlace);
}

std::expected<Song, FormatError> decodeNative(std::span<const std::uint8_t> bytes) {
    return readNative(bytes).transform(songOf);
}

void saveNative(const Song& song, const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes = encodeNative(song);
    detail::writeFileAtomically(
        path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::expected<Song, FormatError> loadNative(const std::filesystem::path& path) {
    return openNative(path).transform(songOf);
}

std::expected<ImportResult, FormatError> openNative(const std::filesystem::path& path) {
    // The file's bytes go before the song is read: only its text is held then.
    auto json = detail::readFile(path).and_then(
        [](const std::vector<std::uint8_t>& bytes) { return nativeJson(bytes); });
    return std::move(json).and_then(readInPlace);
}

} // namespace elara::io
