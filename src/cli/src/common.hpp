// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared plumbing for elara-cli subcommands.
#pragma once

#include <elara/core/song.hpp>
#include <elara/core/tuning.hpp>

#include "json_writer.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace CLI {
class App;
}

namespace elara::cli {

inline constexpr int kExitOk = 0;
inline constexpr int kExitError = 1;
inline constexpr int kExitUsage = 2;

/// State shared by all subcommands: the global --json flag and the exit code the
/// subcommand that ran wants main() to return.
struct Context {
    bool json = false;
    int exitCode = kExitUsage;
};

/// The directory holding the running executable (empty if the system will not
/// say). Files shipped with Elara, such as fonts/, sit next to it.
[[nodiscard]] std::filesystem::path executableDirectory();

/// A path as UTF-8 text, whatever the system's code page.
[[nodiscard]] std::string utf8(const std::filesystem::path& path);

/// The SMuFL metadata of the music font shipped next to elara-cli.
[[nodiscard]] std::filesystem::path defaultFontMetadata();

/// A song from a .elara file or any file `import` reads; the error says why not.
[[nodiscard]] std::expected<core::Song, std::string> openSong(const std::filesystem::path& path);

/// A --tuning value: a preset name ("guitar", "bass-5") or open strings
/// ("D2 A2 D3 G3 B3 E4"); std::nullopt for neither.
[[nodiscard]] std::optional<core::Tuning> tuningFromOption(std::string_view text);

/// Prints "error: <message>" on stderr and returns kExitError (runtime failure).
int fail(std::string_view message);

/// Prints "error: <message>" on stderr and returns kExitUsage (bad argument).
int usageError(std::string_view message);

// Each group registers its subcommands; a subcommand's callback runs it and stores
// the exit code in the context.
void addAudioCommands(CLI::App& app, Context& context);
void addEditCommands(CLI::App& app, Context& context);
void addFileCommands(CLI::App& app, Context& context);
void addFontCommands(CLI::App& app, Context& context);
void addRenderCommands(CLI::App& app, Context& context);
void addTheoryCommands(CLI::App& app, Context& context);

} // namespace elara::cli
