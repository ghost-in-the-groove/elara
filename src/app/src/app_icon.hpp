// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// The app icon (ADR-0064): drawn from icons/elara.svg at build time and packed
// as the icon of the window, the Windows executable and the macOS bundle.

#include <QByteArray>
#include <QImage>

#include <array>
#include <span>

namespace elara::app {

/// The sizes the build draws the icon at, in pixels: the window takes those up
/// to 256, the Windows icon file too, and the macOS one those it names.
inline constexpr std::array<int, 9> kIconSizes{16, 24, 32, 48, 64, 128, 256, 512, 1024};

/// Draws an SVG image as a square icon `size` pixels wide, on a transparent
/// ground; a null image when `svg` is not an SVG image.
[[nodiscard]] QImage renderIcon(const QByteArray& svg, int size);

/// A Windows icon file (.ico) holding each of `images` that is square and at
/// most 256 pixels wide, as PNG, in the order given.
[[nodiscard]] QByteArray icoFile(std::span<const QImage> images);

/// A macOS icon file (.icns) holding, as PNG, each of `images` under every
/// name macOS gives its size (32 pixels is both 32 and 16 at twice the
/// density); sizes macOS has no name for are left out.
[[nodiscard]] QByteArray icnsFile(std::span<const QImage> images);

} // namespace elara::app
