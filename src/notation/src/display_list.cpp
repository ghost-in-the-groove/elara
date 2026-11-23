// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/notation/display_list.hpp>

#include <array>
#include <string_view>

namespace elara::notation {

namespace {

constexpr std::array<std::string_view, kRoleCount> kRoleNames{
    "staffLine",  "ledgerLine", "barline", "clef", "keySignature", "timeSignature", "notehead",
    "accidental", "dot",        "stem",    "flag", "beam",         "rest",
};

} // namespace

std::string_view roleName(Role role) noexcept {
    return kRoleNames[static_cast<std::size_t>(role)];
}

} // namespace elara::notation
