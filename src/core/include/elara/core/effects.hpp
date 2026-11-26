// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/duration.hpp>
#include <elara/core/fraction.hpp>
#include <elara/core/names.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace elara::core {

/// One point of a bend or whammy-bar curve.
struct BendPoint {
    int position = 0; // percent of the note's duration, 0..100
    int cents = 0;    // pitch offset; 100 = one semitone, 200 = a full-step bend

    friend constexpr bool operator==(const BendPoint&, const BendPoint&) noexcept = default;
};

/// Pitch offset over a note's duration, as points with increasing positions.
/// Empty means no bend. Values hold before the first and after the last point.
class BendCurve {
public:
    static constexpr std::size_t kMaxPoints = 32;
    static constexpr int kMaxCents = 2400; // two octaves (whammy-bar dives)

    BendCurve() = default;

    /// Throws std::invalid_argument for more than kMaxPoints points, a position
    /// outside 0..100, positions that do not increase, or |cents| > kMaxCents.
    explicit BendCurve(std::vector<BendPoint> points);

    /// Bend up by `cents` and hold.
    [[nodiscard]] static BendCurve bend(int cents);
    /// Bend up by `cents`, hold, release back.
    [[nodiscard]] static BendCurve bendRelease(int cents);
    /// Bent before the attack and held.
    [[nodiscard]] static BendCurve preBend(int cents);
    /// Bent before the attack, then released.
    [[nodiscard]] static BendCurve preBendRelease(int cents);

    [[nodiscard]] bool isEmpty() const noexcept { return points_.empty(); }
    [[nodiscard]] const std::vector<BendPoint>& points() const noexcept { return points_; }

    /// Offset at `position` percent, linearly interpolated and rounded.
    [[nodiscard]] int centsAt(int position) const noexcept;

    /// Highest upward offset (0 when the curve never goes up); what notation
    /// prints as "full", "1/2"...
    [[nodiscard]] int maxCents() const noexcept;

    friend bool operator==(const BendCurve&, const BendCurve&) = default;

private:
    std::vector<BendPoint> points_{};
};

/// The shapes a bend is usually written in (ADR-0055); Custom is any other
/// curve, such as a bend, release and bend again.
enum class BendForm : std::uint8_t { None, Bend, BendRelease, PreBend, PreBendRelease, Custom };

/// The form of `curve`, read from its shape rather than its points, so that
/// curves read from other formats are recognised: Bend rises from 0 to its top
/// and holds, BendRelease rises from 0 and falls back to 0, PreBend starts at
/// its top and holds, PreBendRelease starts at its top and falls back to 0.
/// None for an empty curve, Custom for one that never rises above 0, dips
/// below 0 or changes direction more often.
[[nodiscard]] BendForm bendForm(const BendCurve& curve);

/// The curve of `form` up by `cents`; empty for None. Throws
/// std::invalid_argument for Custom and for `cents` outside 1..kMaxCents.
[[nodiscard]] BendCurve bendCurve(BendForm form, int cents);

/// The form of a whammy-bar curve (ADR-0060): the shapes of BendForm going
/// down from the written pitch (a dive, a dip, a pre-dive, a pre-dive and
/// return) or up from it, as a bend does. None for an empty curve, Custom for
/// one that goes both ways or changes direction more often.
[[nodiscard]] BendForm whammyForm(const BendCurve& curve);

/// How far a whammy-bar curve goes from the written pitch: the cents of its
/// point farthest from it, negative below it; 0 for an empty curve.
[[nodiscard]] int whammyCents(const BendCurve& curve);

/// The whammy-bar curve of `form` going `cents` from the written pitch, down
/// for negative cents and up for positive ones; empty for None. Throws
/// std::invalid_argument for Custom, and for 0 or more than kMaxCents either way.
[[nodiscard]] BendCurve whammyCurve(BendForm form, int cents);

enum class SlideIn : std::uint8_t { None, FromBelow, FromAbove };
template <>
struct EnumNames<SlideIn> {
    static constexpr auto kNames =
        std::to_array<std::string_view>({"none", "from-below", "from-above"});
};
static_assert(namesEnd(SlideIn::FromAbove));

/// Shift: slide to the next note and pick it. Legato: slide to the next note
/// without picking it. Downwards/Upwards: slide off the note.
enum class SlideOut : std::uint8_t { None, Shift, Legato, Downwards, Upwards };
template <>
struct EnumNames<SlideOut> {
    static constexpr auto kNames =
        std::to_array<std::string_view>({"none", "shift", "legato", "downwards", "upwards"});
};
static_assert(namesEnd(SlideOut::Upwards));

enum class HarmonicKind : std::uint8_t { None, Natural, Artificial, Pinch, Tapped, Semi };
template <>
struct EnumNames<HarmonicKind> {
    static constexpr auto kNames = std::to_array<std::string_view>(
        {"none", "natural", "artificial", "pinch", "tapped", "semi"});
};
static_assert(namesEnd(HarmonicKind::Semi));

/// Natural: `fretTenths` is the touched node in tenths of a fret (120 = fret 12,
/// 32 = "3.2"). Artificial and tapped: how far above the fretted note the node
/// is (usually 120).
struct Harmonic {
    HarmonicKind kind = HarmonicKind::None;
    std::int16_t fretTenths = 0;

    friend constexpr bool operator==(const Harmonic&, const Harmonic&) noexcept = default;
};

/// A short ornament on a note (ADR-0091): a few quick notes on the steps of
/// the key next to it, then the note. A mordent dips to the step below and
/// back, an upper mordent goes to the step above and back; a turn goes round
/// the note from above (above, note, below, note), an inverted turn from below.
enum class Ornament : std::uint8_t { None, Mordent, UpperMordent, Turn, InvertedTurn };
template <>
struct EnumNames<Ornament> {
    static constexpr auto kNames = std::to_array<std::string_view>(
        {"none", "mordent", "upper-mordent", "turn", "inverted-turn"});
};
static_assert(namesEnd(Ornament::InvertedTurn));

/// A finger of a hand (ADR-0093). Written 1 to 5 from the thumb on keyboards
/// and most instruments; on a fretted one, T and 1 to 4 from the index for the
/// hand that frets, and p, i, m, a, c for the hand that plucks. Its name says
/// which finger ("thumb"), as files keep it; fingeringText() and pluckText()
/// are how it is written on the staff.
enum class Finger : std::uint8_t { None, Thumb, Index, Middle, Ring, Little };
template <>
struct EnumNames<Finger> {
    static constexpr auto kNames =
        std::to_array<std::string_view>({"none", "thumb", "index", "middle", "ring", "little"});
};
static_assert(namesEnd(Finger::Little));

/// How a fingering is written: "1" to "5" from the thumb, or on a fretted
/// instrument "T" and "1" to "4" from the index; "" for none.
[[nodiscard]] std::string_view fingeringText(Finger finger, bool fretted) noexcept;
/// The finger a written fingering names, as fingeringText() writes it (a
/// lowercase "t" too); Finger::None for "0", an open string; nullopt for
/// anything else.
[[nodiscard]] std::optional<Finger> fingerOfText(std::string_view text, bool fretted) noexcept;
/// The letter of a plucking finger: "p", "i", "m", "a", "c"; "" for none.
[[nodiscard]] std::string_view pluckText(Finger finger) noexcept;
/// The plucking finger of a letter, as pluckText() writes it ("e" and "x" for
/// the little finger too); nullopt for anything else.
[[nodiscard]] std::optional<Finger> pluckOfText(std::string_view text) noexcept;

/// The quick notes an ornament starts its note with, as steps of the key from
/// the note (+1 the step above, -1 the step below, 0 the note); the note
/// itself follows them for the rest of its length.
struct OrnamentSteps {
    std::array<std::int8_t, 3> steps{};
    std::size_t count = 0;
};

[[nodiscard]] constexpr OrnamentSteps ornamentSteps(Ornament ornament) noexcept {
    switch (ornament) {
    case Ornament::Mordent:
        return {.steps = {0, -1, 0}, .count = 2};
    case Ornament::UpperMordent:
        return {.steps = {0, 1, 0}, .count = 2};
    case Ornament::Turn:
        return {.steps = {1, 0, -1}, .count = 3};
    case Ornament::InvertedTurn:
        return {.steps = {-1, 0, 1}, .count = 3};
    case Ornament::None:
        break;
    }
    return {};
}

/// A trill (ADR-0090): the note alternates with the note `semitones` above
/// it (below when negative), starting on itself, in notes of `speed`.
struct Trill {
    static constexpr int kMaxSemitones = 12;

    std::int8_t semitones = 0; // 0: no trill
    NoteValue speed = NoteValue::ThirtySecond;

    [[nodiscard]] constexpr bool isTrill() const noexcept { return semitones != 0; }
    friend constexpr bool operator==(const Trill&, const Trill&) noexcept = default;
};

enum class Vibrato : std::uint8_t { None, Slight, Wide };
template <>
struct EnumNames<Vibrato> {
    static constexpr auto kNames = std::to_array<std::string_view>({"none", "slight", "wide"});
};
static_assert(namesEnd(Vibrato::Wide));

enum class Accent : std::uint8_t { None, Accent, Marcato };
template <>
struct EnumNames<Accent> {
    static constexpr auto kNames = std::to_array<std::string_view>({"none", "accent", "marcato"});
};
static_assert(namesEnd(Accent::Marcato));

/// Named as it is written: "ppp" to "fff".
enum class Dynamic : std::uint8_t { PPP, PP, P, MP, MF, F, FF, FFF };
template <>
struct EnumNames<Dynamic> {
    static constexpr auto kNames =
        std::to_array<std::string_view>({"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"});
};
static_assert(namesEnd(Dynamic::FFF));

/// MIDI velocity used to play a dynamic (ppp 16 ... fff 127).
[[nodiscard]] constexpr int dynamicVelocity(Dynamic dynamic) noexcept {
    constexpr std::array<int, 8> kVelocities{16, 32, 48, 64, 80, 96, 112, 127};
    return kVelocities[static_cast<std::size_t>(dynamic)];
}

/// Semitones above the open string that a natural harmonic touched at `fret`
/// sounds (12 -> 12, 7 -> 19, 5 -> 24, 4 -> 28...). std::nullopt when the fret
/// is not near a node of the first 16 partials.
[[nodiscard]] std::optional<int> naturalHarmonicSemitones(double fret);

/// How a note is played. "FromPrevious" flags join a note to the previous note
/// on the same string or pitch: no new attack.
struct NoteEffects {
    // Largest members first to keep padding small, the flags as bits (see the
    // memory benchmark: a Note stays 48 bytes).
    BendCurve bend{};
    Harmonic harmonic{};
    Trill trill{};                       // ADR-0090
    bool tiedFromPrevious : 1 = false;   // tie: same pitch, sustained
    bool legatoFromPrevious : 1 = false; // hammer-on (higher) or pull-off (lower)
    SlideIn slideIn = SlideIn::None;
    SlideOut slideOut = SlideOut::None;
    Vibrato vibrato = Vibrato::None;
    bool palmMute : 1 = false;
    bool deadNote : 1 = false; // muted, percussive "x"
    bool ghost : 1 = false;    // soft, written in parentheses
    bool letRing : 1 = false;
    bool staccato : 1 = false;
    bool staccatissimo : 1 = false; // shorter than staccato, a wedge (ADR-0094)
    bool tenuto : 1 = false;        // held its full length; with staccato, portato (ADR-0088)
    Accent accent = Accent::None;
    Dynamic dynamic = Dynamic::MF;
    Ornament ornament = Ornament::None; // ADR-0091
    Finger fingering = Finger::None;    // the finger that plays it (ADR-0093)
    Finger pluck = Finger::None;        // the one that plucks it, on strings

    friend bool operator==(const NoteEffects&, const NoteEffects&) = default;
};

enum class PickStroke : std::uint8_t { None, Down, Up };
template <>
struct EnumNames<PickStroke> {
    static constexpr auto kNames = std::to_array<std::string_view>({"none", "down", "up"});
};
static_assert(namesEnd(PickStroke::Up));

enum class Strum : std::uint8_t { None, Down, Up };
template <>
struct EnumNames<Strum> {
    static constexpr auto kNames = std::to_array<std::string_view>({"none", "down", "up"});
};
static_assert(namesEnd(Strum::Up));

enum class BassTechnique : std::uint8_t { None, Tap, Slap, Pop };
template <>
struct EnumNames<BassTechnique> {
    static constexpr auto kNames = std::to_array<std::string_view>({"none", "tap", "slap", "pop"});
};
static_assert(namesEnd(BassTechnique::Pop));

/// A wedge under the staff over the beat: the music gets louder or softer
/// (ADR-0078). Consecutive beats with the same hairpin share one wedge.
enum class Hairpin : std::uint8_t { None, Crescendo, Diminuendo };
template <>
struct EnumNames<Hairpin> {
    static constexpr auto kNames =
        std::to_array<std::string_view>({"none", "crescendo", "diminuendo"});
};
static_assert(namesEnd(Hairpin::Diminuendo));

/// A pause over the beat (ADR-0085): the beat is held longer than written.
enum class Fermata : std::uint8_t { None, Short, Normal, Long };
template <>
struct EnumNames<Fermata> {
    static constexpr auto kNames =
        std::to_array<std::string_view>({"none", "short", "normal", "long"});
};
static_assert(namesEnd(Fermata::Long));

/// How many times its written length a beat under `fermata` lasts: a short
/// fermata half as long again, a normal one twice, a long one three times.
[[nodiscard]] constexpr double fermataStretch(Fermata fermata) noexcept {
    switch (fermata) {
    case Fermata::Short:
        return 1.5;
    case Fermata::Normal:
        return 2.0;
    case Fermata::Long:
        return 3.0;
    case Fermata::None:
        break;
    }
    return 1.0;
}

/// An octave line over or under the beat (ADR-0089): its notes are written
/// one or two octaves from where they sound, as the line says. Consecutive
/// beats with the same octave line share one line.
enum class Ottava : std::uint8_t {
    None,
    Alta,              ///< 8va: sounding an octave above the written notes
    Bassa,             ///< 8vb: an octave below
    QuindicesimaAlta,  ///< 15ma: two octaves above
    QuindicesimaBassa, ///< 15mb: two octaves below
};
template <>
struct EnumNames<Ottava> {
    static constexpr auto kNames =
        std::to_array<std::string_view>({"none", "8va", "8vb", "15ma", "15mb"});
};
static_assert(namesEnd(Ottava::QuindicesimaBassa));

/// How many octaves above its written note a note under `ottava` sounds:
/// 1 for 8va, -1 for 8vb, 2 for 15ma, -2 for 15mb, 0 without a line.
[[nodiscard]] constexpr int ottavaOctaves(Ottava ottava) noexcept {
    switch (ottava) {
    case Ottava::Alta:
        return 1;
    case Ottava::Bassa:
        return -1;
    case Ottava::QuindicesimaAlta:
        return 2;
    case Ottava::QuindicesimaBassa:
        return -2;
    case Ottava::None:
        break;
    }
    return 0;
}

/// The sustain pedal under the beat (ADR-0092): every note sounding while it
/// is down rings on until it comes up. Consecutive beats under it, in any
/// staff and voice of the track, share one pedalling; a change lifts the pedal
/// and presses it again where its beat starts.
enum class Pedal : std::uint8_t { None, Down, Change };
template <>
struct EnumNames<Pedal> {
    static constexpr auto kNames = std::to_array<std::string_view>({"none", "down", "change"});
};
static_assert(namesEnd(Pedal::Change));

/// A break after the beat (ADR-0095): a breath mark (a comma over the staff)
/// takes a little off the end of its notes, a caesura (two strokes through
/// the top line) a little more.
enum class Breath : std::uint8_t { None, Comma, Caesura };
template <>
struct EnumNames<Breath> {
    static constexpr auto kNames = std::to_array<std::string_view>({"none", "comma", "caesura"});
};
static_assert(namesEnd(Breath::Caesura));

/// Effects that apply to every note of a beat.
struct BeatEffects {
    BendCurve whammyBar{};
    PickStroke pickStroke = PickStroke::None;
    Strum strum = Strum::None;
    std::optional<NoteValue> tremoloPicking{}; // speed of the repeated notes
    BassTechnique bassTechnique = BassTechnique::None;
    Hairpin hairpin = Hairpin::None;
    Fermata fermata = Fermata::None;
    // Slurred to the next beat of its voice (ADR-0086): a slur runs from the
    // first of the beats slurred in a row to the beat after the last of them.
    bool slur = false;
    // Whole bytes, not bit-fields: GCC 14 cannot copy-list-initialize (`= {}`)
    // a class whose enum bit-field has a default member initializer.
    Ottava ottava = Ottava::None; // ADR-0089
    Pedal pedal = Pedal::None;    // ADR-0092
    Breath breath = Breath::None; // after the beat (ADR-0095)

    friend bool operator==(const BeatEffects&, const BeatEffects&) = default;
};

} // namespace elara::core
