// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: runs independent jobs (parts, tracks) on several threads.
#pragma once

#include <elara/core/parallel.hpp>

namespace elara::io::detail {

using core::forEachIndex;
using core::threadsFor;

} // namespace elara::io::detail
