// SPDX-License-Identifier: AGPL-3.0-or-later
// elara-icon-files: draws the app icon from its SVG at build time (ADR-0064).
//
//   elara-icon-files <icon.svg> <out-dir>
//
// writes elara-<size>.png for the window at each size up to 256, elara.ico for
// the Windows executable and elara.icns for the macOS bundle.
#include "app_icon.hpp"
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QString>

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace {

bool writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        std::fprintf(stderr, "elara-icon-files: cannot write %s\n", qPrintable(path));
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: elara-icon-files <icon.svg> <out-dir>\n");
        return EXIT_FAILURE;
    }
    QFile source(QString::fromLocal8Bit(argv[1]));
    if (!source.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "elara-icon-files: cannot read %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const QByteArray svg = source.readAll();
    const QDir out(QString::fromLocal8Bit(argv[2]));
    if (!out.mkpath(QStringLiteral("."))) {
        std::fprintf(stderr, "elara-icon-files: cannot make %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    std::vector<QImage> images;
    for (const int size : elara::app::kIconSizes) {
        QImage image = elara::app::renderIcon(svg, size);
        if (image.isNull()) {
            std::fprintf(stderr, "elara-icon-files: %s is not an SVG image\n", argv[1]);
            return EXIT_FAILURE;
        }
        if (size <= 256 && !image.save(out.filePath(QStringLiteral("elara-%1.png").arg(size)))) {
            std::fprintf(stderr, "elara-icon-files: cannot write the %d pixel image\n", size);
            return EXIT_FAILURE;
        }
        images.push_back(std::move(image));
    }
    const bool written =
        writeFile(out.filePath(QStringLiteral("elara.ico")), elara::app::icoFile(images)) &&
        writeFile(out.filePath(QStringLiteral("elara.icns")), elara::app::icnsFile(images));
    return written ? EXIT_SUCCESS : EXIT_FAILURE;
}
