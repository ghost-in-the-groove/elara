// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/output_device.hpp>

#include <string>
#include <string_view>

namespace elara::audio {

OutputDeviceChoice parseOutputDeviceChoice(std::string_view text) {
    constexpr std::string_view kSeparator = " / ";
    if (const auto at = text.find(kSeparator); at != std::string_view::npos) {
        return {.type = std::string(text.substr(0, at)),
                .name = std::string(text.substr(at + kSeparator.size()))};
    }
    return {.type = {}, .name = std::string(text)};
}

} // namespace elara::audio
