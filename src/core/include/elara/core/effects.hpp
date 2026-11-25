// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/duration.hpp>
#include <elara/core/fraction.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

/// Shift: slide to the next note and pick it. Legato: slide to the next note
/// without picking it. Downwards/Upwards: slide off the note.
enum class SlideOut : std::uint8_t { None, Shift, Legato, Downwards, Upwards };

enum class HarmonicKind : std::uint8_t { None, Natural, Artificial, Pinch, Tapped, Semi };

/// Natural: `fretTenths` is the touched node in tenths of a fret (120 = fret 12,
/// 32 = "3.2"). Artificial and tapped: how far above the fretted note the node
/// is (usually 120).
struct Harmonic {
    HarmonicKind kind = HarmonicKind::None;
    std::int16_t fretTenths = 0;

    friend constexpr bool operator==(const Harmonic&, const Harmonic&) noexcept = default;
};

enum class Vibrato : std::uint8_t { None, Slight, Wide };
enum class Accent : std::uint8_t { None, Accent, Marcato };
enum class Dynamic : std::uint8_t { PPP, PP, P, MP, MF, F, FF, FFF };

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
    // Largest members first to keep padding small (see the memory benchmark).
    BendCurve bend{};
    Harmonic harmonic{};
    bool tiedFromPrevious = false;   // tie: same pitch, sustained
    bool legatoFromPrevious = false; // hammer-on (higher) or pull-off (lower)
    SlideIn slideIn = SlideIn::None;
    SlideOut slideOut = SlideOut::None;
    Vibrato vibrato = Vibrato::None;
    bool palmMute = false;
    bool deadNote = false; // muted, percussive "x"
    bool ghost = false;    // soft, written in parentheses
    bool letRing = false;
    bool staccato = false;
    Accent accent = Accent::None;
    Dynamic dynamic = Dynamic::MF;

    friend bool operator==(const NoteEffects&, const NoteEffects&) = default;
};

enum class PickStroke : std::uint8_t { None, Down, Up };
enum class Strum : std::uint8_t { None, Down, Up };
enum class BassTechnique : std::uint8_t { None, Tap, Slap, Pop };
/// A wedge under the staff over the beat: the music gets louder or softer
/// (ADR-0078). Consecutive beats with the same hairpin share one wedge.
enum class Hairpin : std::uint8_t { None, Crescendo, Diminuendo };
/// A pause over the beat (ADR-0085): the beat is held longer than written.
enum class Fermata : std::uint8_t { None, Short, Normal, Long };

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

    friend bool operator==(const BeatEffects&, const BeatEffects&) = default;
};

} // namespace elara::core
