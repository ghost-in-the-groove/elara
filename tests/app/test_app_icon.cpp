// SPDX-License-Identifier: AGPL-3.0-or-later
// The app icon, drawn from its SVG and packed as the icon files of Windows and
// macOS (ADR-0064).
#include "app_icon.hpp"
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QImage>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using elara::app::icnsFile;
using elara::app::icoFile;
using elara::app::renderIcon;

QByteArray iconSvg() {
    QFile file(QStringLiteral(ELARA_APP_ICON_SVG));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

QImage plainImage(int size) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(40, 30, 90));
    return image;
}

std::uint32_t byteAt(const QByteArray& data, qsizetype at) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(data.at(at)));
}

std::uint32_t littleEndian(const QByteArray& data, qsizetype at, int bytes) {
    std::uint32_t value = 0;
    for (int i = bytes - 1; i >= 0; --i) {
        value = (value << 8U) | byteAt(data, at + i);
    }
    return value;
}

std::uint32_t bigEndian(const QByteArray& data, qsizetype at) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8U) | byteAt(data, at + i);
    }
    return value;
}

const QByteArray kPngSignature = QByteArray::fromHex("89504e470d0a1a0a");

} // namespace

TEST_CASE("The app icon is drawn as a rounded tile at any size", "[app][icon]") {
    const QByteArray svg = iconSvg();
    for (const int size : {16, 32, 256}) {
        const QImage icon = renderIcon(svg, size);
        REQUIRE(icon.width() == size);
        REQUIRE(icon.height() == size);
        // The tile's rounded corner leaves the corner clear; its middle is ink.
        CHECK(icon.pixelColor(0, 0).alpha() == 0);
        CHECK(icon.pixelColor(size / 2, size / 2).alpha() == 255);
    }
    CHECK(renderIcon(QByteArrayLiteral("not an image"), 32).isNull());
}

TEST_CASE("A Windows icon file holds each size as PNG", "[app][icon]") {
    const std::vector<QImage> images{plainImage(16), plainImage(48), plainImage(256),
                                     plainImage(512)};
    const QByteArray ico = icoFile(images);

    // The header: reserved, type 1 (icon), and the images up to 256 pixels.
    REQUIRE(ico.size() > 6);
    CHECK(littleEndian(ico, 0, 2) == 0);
    CHECK(littleEndian(ico, 2, 2) == 1);
    REQUIRE(littleEndian(ico, 4, 2) == 3);

    const std::vector<int> sides{16, 48, 0}; // 256 pixels are written as 0
    for (std::size_t i = 0; i < sides.size(); ++i) {
        const auto entry = static_cast<qsizetype>(6 + (16 * i));
        CHECK(byteAt(ico, entry) == static_cast<std::uint32_t>(sides.at(i)));
        CHECK(byteAt(ico, entry + 1) == static_cast<std::uint32_t>(sides.at(i)));
        CHECK(littleEndian(ico, entry + 4, 2) == 1);  // planes
        CHECK(littleEndian(ico, entry + 6, 2) == 32); // bits per pixel
        const std::uint32_t length = littleEndian(ico, entry + 8, 4);
        const std::uint32_t offset = littleEndian(ico, entry + 12, 4);
        REQUIRE(offset + length <= static_cast<std::uint32_t>(ico.size()));
        const QByteArray png = ico.mid(qsizetype{offset}, qsizetype{length});
        CHECK(png.startsWith(kPngSignature));
        const QImage decoded = QImage::fromData(png, "PNG");
        CHECK(decoded.width() == (sides.at(i) == 0 ? 256 : sides.at(i)));
    }
}

TEST_CASE("A macOS icon file names each size it holds", "[app][icon]") {
    const std::vector<QImage> images{plainImage(16), plainImage(24), plainImage(32),
                                     plainImage(128)};
    const QByteArray icns = icnsFile(images);

    REQUIRE(icns.size() > 8);
    CHECK(icns.left(4) == "icns");
    CHECK(bigEndian(icns, 4) == static_cast<std::uint32_t>(icns.size()));

    // 16 as icp4; 32 as icp5 and as 16 at twice the density (ic11); 128 as
    // ic07. 24 pixels has no name and is left out.
    std::vector<QByteArray> types;
    qsizetype at = 8;
    while (at + 8 <= icns.size()) {
        const QByteArray type = icns.mid(at, 4);
        const std::uint32_t length = bigEndian(icns, at + 4);
        REQUIRE(length > 8);
        REQUIRE(at + qsizetype{length} <= icns.size());
        CHECK(icns.mid(at + 8, kPngSignature.size()) == kPngSignature);
        types.push_back(type);
        at += qsizetype{length};
    }
    CHECK(at == icns.size());
    CHECK(types == std::vector<QByteArray>{"icp4", "icp5", "ic11", "ic07"});
}
