// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared plumbing for elara-cli subcommands.
#pragma once

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

/// Escapes text for a JSON string literal (without the quotes).
[[nodiscard]] std::string jsonEscape(std::string_view text);

/// Prints "error: <message>" on stderr and returns kExitError (runtime failure).
int fail(std::string_view message);

/// Prints "error: <message>" on stderr and returns kExitUsage (bad argument).
int usageError(std::string_view message);

// Each group registers its subcommands; a subcommand's callback runs it and stores
// the exit code in the context.
void addAudioCommands(CLI::App& app, Context& context);
void addEditCommands(CLI::App& app, Context& context);
void addTheoryCommands(CLI::App& app, Context& context);

} // namespace elara::cli
