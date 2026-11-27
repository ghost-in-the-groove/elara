// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace elara::app {

/// Ends the process at once with `code`, as a crash would: no destructor,
/// handler or DLL notification runs. Flush what must be read first.
///
/// std::_Exit is not enough on Windows: it calls ExitProcess, which stops the
/// other threads and then runs each DLL's detach code, global destructors
/// included, and one that waits for a thread already stopped hangs forever.
[[noreturn]] void quitAsCrash(int code) noexcept;

} // namespace elara::app
