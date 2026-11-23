// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/fraction.hpp>

#include <format>

namespace elara::core {

std::string Fraction::toString() const {
    if (den_ == 1) {
        return std::format("{}", num_);
    }
    return std::format("{}/{}", num_, den_);
}

} // namespace elara::core
