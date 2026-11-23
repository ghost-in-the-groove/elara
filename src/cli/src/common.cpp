// SPDX-License-Identifier: AGPL-3.0-or-later
#include "common.hpp"

#include <format>
#include <iostream>

namespace elara::cli {

std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(c));
            } else {
                out += c;
            }
        }
    }
    return out;
}

int fail(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return kExitError;
}

int usageError(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return kExitUsage;
}

} // namespace elara::cli
