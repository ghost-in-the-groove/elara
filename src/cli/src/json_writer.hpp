// SPDX-License-Identifier: AGPL-3.0-or-later
// JSON written as it goes, for elara-cli's --json output.
#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace elara::cli {

/// Escapes text for a JSON string literal (without the quotes).
[[nodiscard]] std::string jsonEscape(std::string_view text);

/// A number for JSON output: four decimals at most, no "-0", no exponent
/// (lengths computed from font metrics carry binary noise, 3.2760000000000002).
/// A NaN or an infinity, which JSON cannot write, is "null".
[[nodiscard]] std::string jsonNumber(double value);

/// JSON text written as it goes: the writer puts the commas, the quotes and
/// the escapes, and writes every number through jsonNumber(). Values follow a
/// key inside an object, and stand alone inside an array or at the top.
///
///     JsonWriter json;
///     json.beginObject().field("file", path).key("tracks").beginArray();
///     for (const auto& track : tracks) json.value(track.name);
///     json.endArray().endObject();
///     std::cout << json.text() << '\n';
class JsonWriter {
public:
    JsonWriter& beginObject();
    JsonWriter& endObject();
    JsonWriter& beginArray();
    JsonWriter& endArray();
    /// The key of the next value, in an object.
    JsonWriter& key(std::string_view name);

    JsonWriter& string(std::string_view text);
    JsonWriter& number(double value);
    JsonWriter& integer(std::int64_t value);
    JsonWriter& unsignedInteger(std::uint64_t value);
    JsonWriter& boolean(bool value);
    JsonWriter& null();
    /// A value that is already JSON text, written as it is.
    JsonWriter& raw(std::string_view json);

    /// A value of any kind: text, a boolean, an integer or a floating-point
    /// number.
    template <typename T>
    JsonWriter& value(const T& item) {
        if constexpr (std::is_same_v<T, bool>) {
            return boolean(item);
        } else if constexpr (std::signed_integral<T>) {
            return integer(item);
        } else if constexpr (std::unsigned_integral<T>) {
            return unsignedInteger(item);
        } else if constexpr (std::floating_point<T>) {
            return number(static_cast<double>(item));
        } else {
            static_assert(std::is_convertible_v<const T&, std::string_view>,
                          "a JSON value is text, a boolean or a number");
            return string(item);
        }
    }
    /// A key and its value.
    template <typename T>
    JsonWriter& field(std::string_view name, const T& item) {
        key(name);
        return value(item);
    }

    /// The text written so far.
    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    void beforeValue();

    std::string text_;
    std::vector<bool> empty_; ///< for each open object or array, whether it has nothing yet
    bool afterKey_ = false;
};

} // namespace elara::cli
