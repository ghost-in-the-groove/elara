// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <string_view>

namespace elara::core {

/// Semantic version of this Elara build, e.g. "0.1.0" (from project() in CMakeLists.txt).
[[nodiscard]] std::string_view version() noexcept;

} // namespace elara::core
