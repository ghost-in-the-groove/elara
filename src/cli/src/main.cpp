// SPDX-License-Identifier: AGPL-3.0-or-later
// elara-cli: headless access to Elara for tests and scripts (ADR-0003).
//
// Subcommands live in *_commands.cpp; each registers itself with a callback that
// stores its exit code in the shared Context.
#include <elara/core/version.hpp>

#include "common.hpp"
#include <CLI/CLI.hpp>

#include <exception>
#include <format>
#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

int runVersion(bool json) {
    if (json) {
        std::cout << std::format(R"({{"name":"elara","version":"{}"}})", elara::core::version())
                  << '\n';
    } else {
        std::cout << "elara " << elara::core::version() << '\n';
    }
    return elara::cli::kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    using namespace elara::cli;
#ifdef _WIN32
    // Output is UTF-8 (as are arguments, through the manifest): show it so.
    SetConsoleOutputCP(CP_UTF8);
#endif

    CLI::App app{"Elara command-line tools (headless)", "elara-cli"};
    app.require_subcommand(1);
    app.fallthrough();

    Context context;
    app.add_flag("--json", context.json, "Print one machine-readable JSON object on stdout");

    app.add_subcommand("version", "Print the Elara version")->callback([&context] {
        context.exitCode = runVersion(context.json);
    });
    addAudioCommands(app, context);
    addTheoryCommands(app, context);
    addEditCommands(app, context);
    addFileCommands(app, context);
    addFontCommands(app, context);
    addRenderCommands(app, context);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        const int code = app.exit(error);
        return code == 0 ? kExitOk : kExitUsage;
    } catch (const std::exception& error) {
        return fail(error.what());
    }
    return context.exitCode;
}
