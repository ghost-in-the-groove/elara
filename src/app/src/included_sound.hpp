// SPDX-License-Identifier: AGPL-3.0-or-later
// The SoundFont that comes with Elara's packages and the one the app starts
// with (ADR-0069), and the guitar SFZ instrument that comes with them
// (ADR-0072).
#pragma once

#include <QString>

#include <optional>

namespace elara::app {

/// The SoundFont that comes with Elara: the first SF2 or SF3 file, by name, in
/// the soundfonts folder of the install tree around `appDir`, the folder of
/// the executable. That folder is share/elara/soundfonts next to bin on
/// Windows and Linux, Resources/soundfonts in the macOS bundle. Empty when
/// this build has none.
[[nodiscard]] QString includedSoundFont(const QString& appDir);

/// The guitar SFZ instrument that comes with Elara: the first SFZ file, by
/// name, in the first folder by name under the sfz folder of the install tree
/// around `appDir`, where each instrument keeps its samples in a folder of its
/// own. That folder is share/elara/sfz next to bin on Windows and Linux,
/// Resources/sfz in the macOS bundle. Empty when this build has none.
[[nodiscard]] QString includedGuitarSfz(const QString& appDir);

/// The SoundFont the app starts with, from the one `saved` the last time (none
/// when the user never chose a sound, empty when they chose the built-in
/// synthesizer): the saved one while its file is there, or else the included
/// one. Empty for the built-in synthesizer.
[[nodiscard]] QString startingSoundFont(const std::optional<QString>& saved,
                                        const QString& included);

} // namespace elara::app
