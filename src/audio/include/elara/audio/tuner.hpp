// SPDX-License-Identifier: AGPL-3.0-or-later
// The tuner: pitch readings from a stream of samples (ADR-0028, ADR-0029).
#pragma once

#include <elara/audio/pitch_detector.hpp>
#include <elara/core/tuning.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace elara::audio {

struct TunerOptions {
    double a4 = 440.0;                  ///< the reference A4 in Hz
    std::optional<core::Tuning> tuning; ///< name the nearest open string of this tuning
    double holdSeconds = 0.5;           ///< how long a reading stays after the note stops
};

/// Turns what an input hears into a steady reading. It looks for a pitch in
/// the latest window each time half a window of new samples has arrived,
/// shows the median of the last few estimates of the same note, ignores a
/// single estimate of another note (a slip to an octave or a fifth) and
/// keeps the reading for a moment after the note stops.
///
/// Runs on the thread that reads the input, not the audio callback: push()
/// copies and detects, which takes up to about a millisecond.
class Tuner {
public:
    static constexpr double kSameNoteCents = 50.0; ///< further apart: another note
    static constexpr std::size_t kSmoothing = 5;   ///< estimates the median is taken over

    explicit Tuner(double sampleRate, TunerOptions options = {});

    /// Applies new options to the next readings and to the current one.
    void setOptions(TunerOptions options);
    [[nodiscard]] const TunerOptions& options() const noexcept { return options_; }

    /// Samples one detection looks at.
    [[nodiscard]] std::size_t frameSize() const noexcept { return window_.size(); }
    [[nodiscard]] double sampleRate() const noexcept { return detector_.sampleRate(); }

    /// Takes the next samples heard. Returns true when the reading changed.
    bool push(std::span<const float> samples);

    /// Takes one detection: the frequency heard (none when nothing clear was)
    /// over the last `elapsed` samples. push() calls it; it is public so that
    /// pitches detected elsewhere can be shown the same way. Returns true when
    /// the reading changed.
    bool observe(std::optional<double> frequency, std::size_t elapsed);

    /// What to show; none before a note is heard and after the hold time.
    [[nodiscard]] const std::optional<TunerReading>& reading() const noexcept { return reading_; }

    /// Forgets the samples, estimates and reading.
    void reset() noexcept;

private:
    [[nodiscard]] double median() const;
    void update();

    PitchDetector detector_;
    TunerOptions options_;
    std::vector<float> window_; // the latest frameSize() samples, oldest first
    std::size_t filled_ = 0;    // samples of window_ that have been heard
    std::size_t unexamined_ = 0;
    std::size_t silent_ = 0; // samples since a pitch was last heard
    std::array<double, kSmoothing> recent_{};
    std::size_t recentCount_ = 0;
    std::size_t recentNext_ = 0;
    std::optional<double> candidate_; // another note heard once
    std::optional<TunerReading> reading_;
};

} // namespace elara::audio
