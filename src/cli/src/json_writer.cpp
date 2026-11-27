// SPDX-License-Identifier: AGPL-3.0-or-later
#include "json_writer.hpp"

#include <cmath>
#include <format>

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

std::string jsonNumber(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    // Fixed-point, not the shortest form, which writes 0.0003 as "3e-04".
    std::string text = std::format("{:.4f}", value);
    if (text.find('.') != std::string::npos) {
        text.erase(text.find_last_not_of('0') + 1);
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    return text == "-0" ? "0" : text;
}

void JsonWriter::beforeValue() {
    if (afterKey_) {
        afterKey_ = false;
        return;
    }
    if (!empty_.empty()) {
        if (!empty_.back()) {
            text_ += ',';
        }
        empty_.back() = false;
    }
}

JsonWriter& JsonWriter::beginObject() {
    beforeValue();
    text_ += '{';
    empty_.push_back(true);
    return *this;
}

JsonWriter& JsonWriter::endObject() {
    text_ += '}';
    empty_.pop_back();
    return *this;
}

JsonWriter& JsonWriter::beginArray() {
    beforeValue();
    text_ += '[';
    empty_.push_back(true);
    return *this;
}

JsonWriter& JsonWriter::endArray() {
    text_ += ']';
    empty_.pop_back();
    return *this;
}

JsonWriter& JsonWriter::key(std::string_view name) {
    beforeValue();
    text_ += '"';
    text_ += jsonEscape(name);
    text_ += "\":";
    afterKey_ = true;
    return *this;
}

JsonWriter& JsonWriter::string(std::string_view text) {
    beforeValue();
    text_ += '"';
    text_ += jsonEscape(text);
    text_ += '"';
    return *this;
}

JsonWriter& JsonWriter::number(double value) {
    beforeValue();
    text_ += jsonNumber(value);
    return *this;
}

JsonWriter& JsonWriter::integer(std::int64_t value) {
    beforeValue();
    text_ += std::format("{}", value);
    return *this;
}

JsonWriter& JsonWriter::unsignedInteger(std::uint64_t value) {
    beforeValue();
    text_ += std::format("{}", value);
    return *this;
}

JsonWriter& JsonWriter::boolean(bool value) {
    beforeValue();
    text_ += value ? "true" : "false";
    return *this;
}

JsonWriter& JsonWriter::null() {
    beforeValue();
    text_ += "null";
    return *this;
}

JsonWriter& JsonWriter::raw(std::string_view json) {
    beforeValue();
    text_ += json;
    return *this;
}

} // namespace elara::cli
