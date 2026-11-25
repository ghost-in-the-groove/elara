// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/version.hpp>
#include <elara/io/native_format.hpp>

#include "archive.hpp"
#include <yyjson.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace elara::io {

namespace {

using namespace core;

constexpr std::string_view kFormatName = "elara";

// ---- Names of enumerations (kebab-case, index = enum value) -------------------

constexpr std::array kClefNames = std::to_array<std::string_view>(
    {"treble", "treble-8vb", "bass", "bass-8vb", "alto", "tenor", "percussion"});
constexpr std::array kKindNames =
    std::to_array<std::string_view>({"fretted", "percussion", "keyboard", "pitched"});
constexpr std::array kSlideInNames =
    std::to_array<std::string_view>({"none", "from-below", "from-above"});
constexpr std::array kSlideOutNames =
    std::to_array<std::string_view>({"none", "shift", "legato", "downwards", "upwards"});
constexpr std::array kHarmonicNames =
    std::to_array<std::string_view>({"none", "natural", "artificial", "pinch", "tapped", "semi"});
constexpr std::array kVibratoNames = std::to_array<std::string_view>({"none", "slight", "wide"});
constexpr std::array kAccentNames = std::to_array<std::string_view>({"none", "accent", "marcato"});
constexpr std::array kDynamicNames =
    std::to_array<std::string_view>({"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"});
constexpr std::array kStrokeNames = std::to_array<std::string_view>({"none", "down", "up"});
constexpr std::array kHairpinNames =
    std::to_array<std::string_view>({"none", "crescendo", "diminuendo"});
constexpr std::array kBassNames = std::to_array<std::string_view>({"none", "tap", "slap", "pop"});
constexpr std::array kJumpToNames =
    std::to_array<std::string_view>({"none", "da-capo", "dal-segno", "dal-segno-segno"});
constexpr std::array kJumpThenNames =
    std::to_array<std::string_view>({"end", "al-fine", "al-coda", "al-double-coda"});

template <typename Enum, std::size_t N>
std::string_view nameOf(const std::array<std::string_view, N>& names, Enum value) {
    return names.at(static_cast<std::size_t>(value));
}

template <typename Enum, std::size_t N>
std::optional<Enum> enumFrom(const std::array<std::string_view, N>& names, std::string_view text) {
    const auto found = std::ranges::find(names, text);
    if (found == names.end()) {
        return std::nullopt;
    }
    return static_cast<Enum>(found - names.begin());
}

// ============================================================================
// Writing
// ============================================================================

class Writer {
public:
    Writer() : doc_(yyjson_mut_doc_new(nullptr)) {
        if (doc_ == nullptr) {
            throw std::bad_alloc();
        }
    }
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer(Writer&&) = delete;
    Writer& operator=(Writer&&) = delete;
    ~Writer() { yyjson_mut_doc_free(doc_); }

    std::string write(const Song& song) {
        yyjson_mut_val* root = yyjson_mut_obj(doc_);
        yyjson_mut_doc_set_root(doc_, root);
        yyjson_mut_obj_add_str(doc_, root, "format", kFormatName.data());
        yyjson_mut_obj_add_int(doc_, root, "formatVersion", kNativeFormatVersion);
        addText(root, "generator", std::format("Elara {}", version()));
        addInfo(root, song.info());
        add(root, "tempo", tempo(song.startTempo()));

        yyjson_mut_val* measures = yyjson_mut_arr(doc_);
        for (std::size_t m = 0; m < song.measureCount(); ++m) {
            yyjson_mut_arr_append(measures, header(song.measureHeader(m)));
        }
        add(root, "measures", measures);

        yyjson_mut_val* tracks = yyjson_mut_arr(doc_);
        for (std::size_t t = 0; t < song.trackCount(); ++t) {
            yyjson_mut_arr_append(tracks, track(song.track(t)));
        }
        add(root, "tracks", tracks);

        std::size_t length = 0;
        char* text = yyjson_mut_write(doc_, YYJSON_WRITE_NOFLAG, &length);
        if (text == nullptr) {
            throw std::runtime_error("cannot write song.json");
        }
        std::string json(text, length);
        std::free(text); // NOLINT(cppcoreguidelines-no-malloc): allocated by yyjson
        return json;
    }

private:
    // Keys are string literals (kept by pointer); values are copied.
    void add(yyjson_mut_val* object, const char* key, yyjson_mut_val* value) {
        yyjson_mut_obj_add_val(doc_, object, key, value);
    }
    void addText(yyjson_mut_val* object, const char* key, std::string_view value) {
        yyjson_mut_obj_add_strncpy(doc_, object, key, value.data(), value.size());
    }
    void addTextIfAny(yyjson_mut_val* object, const char* key, std::string_view value) {
        if (!value.empty()) {
            addText(object, key, value);
        }
    }
    void addInt(yyjson_mut_val* object, const char* key, std::int64_t value) {
        yyjson_mut_obj_add_int(doc_, object, key, value);
    }
    void addFlag(yyjson_mut_val* object, const char* key, bool value) {
        if (value) {
            yyjson_mut_obj_add_true(doc_, object, key);
        }
    }
    // Whole numbers are written without a fraction ("120", not "120.0").
    void addNumber(yyjson_mut_val* object, const char* key, double value) {
        if (std::trunc(value) == value && std::abs(value) < 1e15) {
            addInt(object, key, static_cast<std::int64_t>(value));
        } else {
            yyjson_mut_obj_add_real(doc_, object, key, value);
        }
    }
    template <typename Enum, std::size_t N>
    void addEnumIfNot(yyjson_mut_val* object, const char* key,
                      const std::array<std::string_view, N>& names, Enum value, Enum skip) {
        if (value != skip) {
            addText(object, key, nameOf(names, value));
        }
    }

    void addInfo(yyjson_mut_val* root, const SongInfo& info) {
        yyjson_mut_val* object = yyjson_mut_obj(doc_);
        addTextIfAny(object, "title", info.title);
        addTextIfAny(object, "subtitle", info.subtitle);
        addTextIfAny(object, "artist", info.artist);
        addTextIfAny(object, "album", info.album);
        addTextIfAny(object, "words", info.words);
        addTextIfAny(object, "music", info.music);
        addTextIfAny(object, "copyright", info.copyright);
        addTextIfAny(object, "transcriber", info.transcriber);
        addTextIfAny(object, "notes", info.notes);
        add(root, "info", object);
    }

    yyjson_mut_val* tempo(const Tempo& value) {
        yyjson_mut_val* object = yyjson_mut_obj(doc_);
        addNumber(object, "bpm", value.bpm());
        if (value.beatUnit() != Duration{}) {
            addText(object, "beatUnit", value.beatUnit().toString());
        }
        return object;
    }

    yyjson_mut_val* header(const MeasureHeader& value) {
        yyjson_mut_val* object = yyjson_mut_obj(doc_);
        if (value.timeSignature != TimeSignature{}) {
            addText(object, "timeSignature", value.timeSignature.toString());
        }
        if (value.keySignature != KeySignature{}) {
            addText(object, "keySignature", value.keySignature.toString());
        }
        if (!value.tempoChanges.empty()) {
            yyjson_mut_val* changes = yyjson_mut_arr(doc_);
            for (const auto& change : value.tempoChanges) {
                yyjson_mut_val* item = tempo(change.tempo);
                if (!change.offset.isZero()) {
                    addText(item, "offset", change.offset.toString());
                }
                yyjson_mut_arr_append(changes, item);
            }
            add(object, "tempoChanges", changes);
        }
        addFlag(object, "repeatStart", value.repeatStart);
        if (value.repeatEnd()) {
            addInt(object, "repeatTimes", value.repeatTimes);
        }
        if (value.alternateEndings != 0) {
            yyjson_mut_val* endings = yyjson_mut_arr(doc_);
            for (int time = 1; time <= MeasureHeader::kMaxEnding; ++time) {
                if (value.playedOnTime(time)) {
                    yyjson_mut_arr_add_int(doc_, endings, time);
                }
            }
            add(object, "alternateEndings", endings);
        }
        addFlag(object, "segno", value.segno);
        addFlag(object, "segnoSegno", value.segnoSegno);
        addFlag(object, "coda", value.coda);
        addFlag(object, "doubleCoda", value.doubleCoda);
        addFlag(object, "fine", value.fine);
        addFlag(object, "toCoda", value.toCoda);
        addFlag(object, "toDoubleCoda", value.toDoubleCoda);
        addEnumIfNot(object, "jump", kJumpToNames, value.jump, JumpTo::None);
        if (value.jump != JumpTo::None) {
            addEnumIfNot(object, "jumpThen", kJumpThenNames, value.jumpThen, JumpThen::End);
        }
        addTextIfAny(object, "section", value.section);
        return object;
    }

    yyjson_mut_val* track(const Track& value) {
        yyjson_mut_val* object = yyjson_mut_obj(doc_);
        addText(object, "name", value.name());
        const Instrument& instrument = value.instrument();
        yyjson_mut_val* inst = yyjson_mut_obj(doc_);
        addTextIfAny(inst, "name", instrument.name);
        addText(inst, "kind", nameOf(kKindNames, instrument.kind));
        if (instrument.midiProgram != 0) {
            addInt(inst, "midiProgram", instrument.midiProgram);
        }
        if (instrument.writtenOffset != 0) {
            addInt(inst, "writtenOffset", instrument.writtenOffset);
        }
        add(object, "instrument", inst);

        if (const TrackMix& mix = value.mix(); mix != TrackMix{}) {
            yyjson_mut_val* mixJson = yyjson_mut_obj(doc_);
            if (mix.volume != TrackMix::kDefaultVolume) {
                addNumber(mixJson, "volume", mix.volume);
            }
            if (mix.pan != 0.0) {
                addNumber(mixJson, "pan", mix.pan);
            }
            addFlag(mixJson, "mute", mix.mute);
            addFlag(mixJson, "solo", mix.solo);
            add(object, "mix", mixJson);
        }

        yyjson_mut_val* staves = yyjson_mut_arr(doc_);
        for (std::size_t s = 0; s < value.staffCount(); ++s) {
            yyjson_mut_arr_append(staves, staff(value.staff(s)));
        }
        add(object, "staves", staves);
        return object;
    }

    yyjson_mut_val* staff(const Staff& value) {
        yyjson_mut_val* object = yyjson_mut_obj(doc_);
        addText(object, "clef", nameOf(kClefNames, value.clef()));
        if (value.tuning()) {
            addText(object, "tuning", value.tuning()->toString());
        }
        if (value.capo() != 0) {
            addInt(object, "capo", value.capo());
        }
        std::size_t used = value.measureCount(); // trailing empty measures are omitted
        while (used > 0 && value.measure(used - 1).isEmpty()) {
            --used;
        }
        yyjson_mut_val* measures = yyjson_mut_arr(doc_);
        for (std::size_t m = 0; m < used; ++m) {
            yyjson_mut_arr_append(measures, measure(value.measure(m)));
        }
        add(object, "measures", measures);
        return object;
    }

    yyjson_mut_val* measure(const Measure& value) {
        std::size_t used = Measure::kMaxVoices; // trailing empty voices are omitted
        while (used > 0 && value.voices[used - 1].isEmpty()) {
            --used;
        }
        yyjson_mut_val* voices = yyjson_mut_arr(doc_);
        for (std::size_t v = 0; v < used; ++v) {
            yyjson_mut_val* beats = yyjson_mut_arr(doc_);
            for (const Beat& b : value.voices[v].beats) {
                yyjson_mut_arr_append(beats, beat(b));
            }
            yyjson_mut_arr_append(voices, beats);
        }
        return voices;
    }

    yyjson_mut_val* beat(const Beat& value) {
        yyjson_mut_val* object = yyjson_mut_obj(doc_);
        addText(object, "duration", value.duration.toString());
        if (!value.notes.empty()) {
            yyjson_mut_val* notes = yyjson_mut_arr(doc_);
            for (const Note& n : value.notes) {
                yyjson_mut_arr_append(notes, note(n));
            }
            add(object, "notes", notes);
        }
        if (value.effects != BeatEffects{}) {
            add(object, "effects", beatEffects(value.effects));
        }
        return object;
    }

    yyjson_mut_val* note(const Note& value) {
        yyjson_mut_val* object = yyjson_mut_obj(doc_);
        addText(object, "pitch", value.pitch.toString());
        if (value.position) {
            addInt(object, "string", value.position->string);
            addInt(object, "fret", value.position->fret);
        }
        if (value.effects != NoteEffects{}) {
            add(object, "effects", noteEffects(value.effects));
        }
        return object;
    }

    yyjson_mut_val* curve(const BendCurve& value) {
        yyjson_mut_val* points = yyjson_mut_arr(doc_);
        for (const BendPoint& point : value.points()) {
            yyjson_mut_val* pair = yyjson_mut_arr(doc_);
            yyjson_mut_arr_add_int(doc_, pair, point.position);
            yyjson_mut_arr_add_int(doc_, pair, point.cents);
            yyjson_mut_arr_append(points, pair);
        }
        return points;
    }

    yyjson_mut_val* noteEffects(const NoteEffects& fx) {
        yyjson_mut_val* object = yyjson_mut_obj(doc_);
        if (!fx.bend.isEmpty()) {
            add(object, "bend", curve(fx.bend));
        }
        if (fx.harmonic != Harmonic{}) {
            yyjson_mut_val* harmonic = yyjson_mut_obj(doc_);
            addText(harmonic, "kind", nameOf(kHarmonicNames, fx.harmonic.kind));
            if (fx.harmonic.fretTenths != 0) {
                addNumber(harmonic, "fret", fx.harmonic.fretTenths / 10.0);
            }
            add(object, "harmonic", harmonic);
        }
        addFlag(object, "tiedFromPrevious", fx.tiedFromPrevious);
        addFlag(object, "legatoFromPrevious", fx.legatoFromPrevious);
        addEnumIfNot(object, "slideIn", kSlideInNames, fx.slideIn, SlideIn::None);
        addEnumIfNot(object, "slideOut", kSlideOutNames, fx.slideOut, SlideOut::None);
        addEnumIfNot(object, "vibrato", kVibratoNames, fx.vibrato, Vibrato::None);
        addFlag(object, "palmMute", fx.palmMute);
        addFlag(object, "deadNote", fx.deadNote);
        addFlag(object, "ghost", fx.ghost);
        addFlag(object, "letRing", fx.letRing);
        addFlag(object, "staccato", fx.staccato);
        addEnumIfNot(object, "accent", kAccentNames, fx.accent, Accent::None);
        addEnumIfNot(object, "dynamic", kDynamicNames, fx.dynamic, Dynamic::MF);
        return object;
    }

    yyjson_mut_val* beatEffects(const BeatEffects& fx) {
        yyjson_mut_val* object = yyjson_mut_obj(doc_);
        if (!fx.whammyBar.isEmpty()) {
            add(object, "whammyBar", curve(fx.whammyBar));
        }
        addEnumIfNot(object, "pickStroke", kStrokeNames, fx.pickStroke, PickStroke::None);
        addEnumIfNot(object, "strum", kStrokeNames, fx.strum, Strum::None);
        if (fx.tremoloPicking) {
            addText(object, "tremoloPicking", Duration(*fx.tremoloPicking).toString());
        }
        addEnumIfNot(object, "bassTechnique", kBassNames, fx.bassTechnique, BassTechnique::None);
        addEnumIfNot(object, "hairpin", kHairpinNames, fx.hairpin, Hairpin::None);
        return object;
    }

    yyjson_mut_doc* doc_;
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
        if (yyjson_val* tracks = optionalArray(root, "tracks")) {
            Scope scope(*this, "tracks");
            forEach(tracks, [&](std::size_t, yyjson_val* track) { readTrack(track, song); });
        }
        return song;
    }

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

    template <typename Enum, std::size_t N>
    Enum enumValue(yyjson_val* object, const char* key,
                   const std::array<std::string_view, N>& names, Enum fallback) {
        const auto text = optionalText(object, key);
        if (!text) {
            return fallback;
        }
        const auto value = enumFrom<Enum>(names, *text);
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
                header.tempoChanges.push_back(
                    TempoChange{.offset = at, .tempo = readTempo(change)});
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
        header.segno = flag(object, "segno");
        header.segnoSegno = flag(object, "segnoSegno");
        header.coda = flag(object, "coda");
        header.doubleCoda = flag(object, "doubleCoda");
        header.fine = flag(object, "fine");
        header.toCoda = flag(object, "toCoda");
        header.toDoubleCoda = flag(object, "toDoubleCoda");
        header.jump = enumValue(object, "jump", kJumpToNames, JumpTo::None);
        if (header.jump != JumpTo::None) {
            header.jumpThen = enumValue(object, "jumpThen", kJumpThenNames, JumpThen::End);
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
            instrument.kind = enumValue(inst, "kind", kKindNames, InstrumentKind::Pitched);
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
                const Clef clef = enumValue(staff, "clef", kClefNames, Clef::Treble);
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
        Scope stavesScope(*this, "staves");
        forEach(stavesJson, [&](std::size_t s, yyjson_val* staffJson) {
            readMeasures(staffJson, track.staff(s), song.measureCount());
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

    Beat readBeat(yyjson_val* object, int strings) {
        if (!yyjson_is_obj(object)) {
            fail("expected an object");
        }
        Beat beat{.duration = duration(object, "duration"), .notes = {}};
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
            fx.harmonic.kind = enumValue(harmonic, "kind", kHarmonicNames, HarmonicKind::None);
            if (const auto fret = optionalNumber(harmonic, "fret")) {
                const double tenths = std::round(*fret * 10.0);
                if (!(tenths >= 0.0 && tenths <= Tuning::kMaxFret * 10.0)) {
                    fail(std::format("harmonic fret outside 0..{}", Tuning::kMaxFret));
                }
                fx.harmonic.fretTenths = static_cast<std::int16_t>(tenths);
            }
        }
        fx.tiedFromPrevious = flag(object, "tiedFromPrevious");
        fx.legatoFromPrevious = flag(object, "legatoFromPrevious");
        fx.slideIn = enumValue(object, "slideIn", kSlideInNames, SlideIn::None);
        fx.slideOut = enumValue(object, "slideOut", kSlideOutNames, SlideOut::None);
        fx.vibrato = enumValue(object, "vibrato", kVibratoNames, Vibrato::None);
        fx.palmMute = flag(object, "palmMute");
        fx.deadNote = flag(object, "deadNote");
        fx.ghost = flag(object, "ghost");
        fx.letRing = flag(object, "letRing");
        fx.staccato = flag(object, "staccato");
        fx.accent = enumValue(object, "accent", kAccentNames, Accent::None);
        fx.dynamic = enumValue(object, "dynamic", kDynamicNames, Dynamic::MF);
        return fx;
    }

    BeatEffects readBeatEffects(yyjson_val* object) {
        BeatEffects fx;
        fx.whammyBar = readCurve(object, "whammyBar");
        fx.pickStroke = enumValue(object, "pickStroke", kStrokeNames, PickStroke::None);
        fx.strum = enumValue(object, "strum", kStrokeNames, Strum::None);
        if (yyjson_obj_get(object, "tremoloPicking") != nullptr) {
            const Duration speed = duration(object, "tremoloPicking");
            if (speed.dots() != 0 || !speed.tuplet().isNone()) {
                Scope scope(*this, "tremoloPicking");
                fail("expected a plain note value such as 16");
            }
            fx.tremoloPicking = speed.value();
        }
        fx.bassTechnique = enumValue(object, "bassTechnique", kBassNames, BassTechnique::None);
        fx.hairpin = enumValue(object, "hairpin", kHairpinNames, Hairpin::None);
        return fx;
    }

    JsonPath path_;
};

} // namespace

std::string songToJson(const Song& song) {
    Writer writer;
    return writer.write(song);
}

std::expected<Song, FormatError> songFromJson(std::string_view json) {
    yyjson_read_err error{};
    // Without YYJSON_READ_INSITU the input is only read, never modified.
    yyjson_doc* doc =
        yyjson_read_opts(const_cast<char*>(json.data()), json.size(), 0, nullptr, &error);
    if (doc == nullptr) {
        return std::unexpected(FormatError{
            std::format("invalid JSON at byte {}: {}", error.pos, error.msg ? error.msg : "")});
    }
    const std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> owner(doc, yyjson_doc_free);
    try {
        Reader reader;
        return reader.read(yyjson_doc_get_root(doc));
    } catch (const ReadError& failure) {
        return std::unexpected(FormatError{failure.message});
    }
}

std::vector<std::uint8_t> encodeNative(const Song& song) {
    const std::string json = songToJson(song);
    const std::array entries{
        detail::ZipEntry{.name = "mimetype", .content = kNativeMimeType, .level = 0},
        detail::ZipEntry{.name = "song.json", .content = json}};
    return detail::writeZip(entries);
}

std::expected<Song, FormatError> decodeNative(std::span<const std::uint8_t> bytes) {
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
        const auto json = zip.entry("song.json");
        if (!json) {
            return std::unexpected(FormatError{"not an Elara file (no song.json)"});
        }
        return songFromJson(*json);
    } catch (const FormatError& failure) {
        return std::unexpected(failure);
    }
}

void saveNative(const Song& song, const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes = encodeNative(song);
    detail::writeFileAtomically(
        path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::expected<Song, FormatError> loadNative(const std::filesystem::path& path) {
    return detail::readFile(path).and_then(
        [](const std::vector<std::uint8_t>& bytes) { return decodeNative(bytes); });
}

} // namespace elara::io
