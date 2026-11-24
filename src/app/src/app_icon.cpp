// SPDX-License-Identifier: AGPL-3.0-or-later
#include "app_icon.hpp"

#include <QBuffer>
#include <QPainter>
#include <QSvgRenderer>
#include <QtGlobal>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace elara::app {

namespace {

QByteArray png(const QImage& image) {
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

void appendLittleEndian(QByteArray& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.append(static_cast<char>((value >> (8U * static_cast<unsigned>(i))) & 0xFFU));
    }
}

void appendBigEndian(QByteArray& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.append(static_cast<char>((value >> static_cast<unsigned>(shift)) & 0xFFU));
    }
}

struct IcnsType {
    std::string_view name;
    int size;
};

// The PNG image types of the icns format, by the size of their image.
constexpr std::array<IcnsType, 11> kIcnsTypes{{
    {"icp4", 16},
    {"icp5", 32},
    {"ic11", 32}, // 16 at twice the density
    {"icp6", 64},
    {"ic12", 64}, // 32 at twice the density
    {"ic07", 128},
    {"ic08", 256},
    {"ic13", 256}, // 128 at twice the density
    {"ic09", 512},
    {"ic14", 512},  // 256 at twice the density
    {"ic10", 1024}, // 512 at twice the density
}};

} // namespace

QImage renderIcon(const QByteArray& svg, int size) {
    QSvgRenderer renderer(svg);
    if (!renderer.isValid() || size <= 0) {
        return {};
    }
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.end();
    return image;
}

QByteArray icoFile(std::span<const QImage> images) {
    constexpr int kLargest = 256;
    constexpr std::uint32_t kHeader = 6;
    constexpr std::uint32_t kEntry = 16;
    std::vector<QByteArray> pngs;
    std::vector<int> sides;
    for (const QImage& image : images) {
        if (image.width() == image.height() && image.width() > 0 && image.width() <= kLargest) {
            pngs.push_back(png(image));
            sides.push_back(image.width());
        }
    }
    QByteArray out;
    appendLittleEndian(out, 0, 2); // reserved
    appendLittleEndian(out, 1, 2); // an icon, not a cursor
    appendLittleEndian(out, static_cast<std::uint32_t>(pngs.size()), 2);
    auto offset = kHeader + (kEntry * static_cast<std::uint32_t>(pngs.size()));
    for (std::size_t i = 0; i < pngs.size(); ++i) {
        // A side of 256 pixels does not fit a byte and is written as 0.
        const auto side = static_cast<std::uint32_t>(sides[i] == kLargest ? 0 : sides[i]);
        appendLittleEndian(out, side, 1);
        appendLittleEndian(out, side, 1);
        appendLittleEndian(out, 0, 1);  // no palette
        appendLittleEndian(out, 0, 1);  // reserved
        appendLittleEndian(out, 1, 2);  // colour planes
        appendLittleEndian(out, 32, 2); // bits per pixel
        const auto length = static_cast<std::uint32_t>(pngs[i].size());
        appendLittleEndian(out, length, 4);
        appendLittleEndian(out, offset, 4);
        offset += length;
    }
    for (const QByteArray& data : pngs) {
        out.append(data);
    }
    return out;
}

QByteArray icnsFile(std::span<const QImage> images) {
    QByteArray body;
    for (const IcnsType& type : kIcnsTypes) {
        for (const QImage& image : images) {
            if (image.width() == type.size && image.height() == type.size) {
                const QByteArray data = png(image);
                body.append(type.name.data(), static_cast<qsizetype>(type.name.size()));
                appendBigEndian(body, static_cast<std::uint32_t>(data.size() + 8));
                body.append(data);
                break;
            }
        }
    }
    QByteArray out("icns");
    appendBigEndian(out, static_cast<std::uint32_t>(body.size() + 8));
    out.append(body);
    return out;
}

} // namespace elara::app
