// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/version.hpp>

namespace elara::core {

std::string_view version() noexcept {
    return ELARA_VERSION_STRING;
}

} // namespace elara::core
