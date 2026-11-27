// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/io/open.hpp>
#include <elara/notation/display_list.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/smufl.hpp>
#include <elara/notation/staff_layout.hpp>

#include "score_painter.hpp"
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QString>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

using elara::app::ScoreColours;
using elara::app::ScoreFonts;
using elara::app::ScorePainter;
using elara::notation::BeamItem;
using elara::notation::CurveItem;
using elara::notation::DisplayList;
using elara::notation::Glyph;
using elara::notation::GlyphItem;
using elara::notation::LineItem;
using elara::notation::PolygonItem;
using elara::notation::Role;
using elara::notation::TextAlign;
using elara::notation::TextFace;
using elara::notation::TextItem;

namespace {

const std::filesystem::path kFonts = ELARA_FONT_DIR;
const std::filesystem::path kCorpus = ELARA_CORPUS_DIR;

const ScorePainter& painter() {
    static const ScorePainter bravura(
        ScoreFonts::fromDirectory(QString::fromStdU16String(kFonts.generic_u16string())));
    return bravura;
}

const elara::notation::EngravingFont& metrics() {
    static const elara::notation::EngravingFont font = [] {
        auto loaded = elara::notation::EngravingFont::fromMetadataFile(kFonts / "Bravura.json");
        REQUIRE(loaded.has_value());
        return *std::move(loaded);
    }();
    return font;
}

// A blank list `width` staff spaces wide from y = `top` to y = `bottom`.
DisplayList blank(double width = 20.0, double top = -2.0, double bottom = 6.0) {
    DisplayList list;
    list.width = width;
    list.top = top;
    list.bottom = bottom;
    return list;
}

// Whether the pixel at (x, y) in staff spaces is ink, for a list painted at
// `scale` pixels per staff space.
bool inkAt(const QImage& image, const DisplayList& list, double scale, double x, double y) {
    const int px = static_cast<int>(std::floor(x * scale));
    const int py = static_cast<int>(std::floor((y - list.top) * scale));
    REQUIRE(image.valid(px, py));
    return qGray(image.pixel(px, py)) < 128;
}

// Whether any pixel from (x0, y0) to (x1, y1) in staff spaces is ink.
bool inkIn(const QImage& image, const DisplayList& list, double scale, double x0, double y0,
           double x1, double y1) {
    for (double y = y0; y <= y1; y += 1.0 / scale) {
        for (double x = x0; x <= x1; x += 1.0 / scale) {
            if (inkAt(image, list, scale, x, y)) {
                return true;
            }
        }
    }
    return false;
}

double inkShare(const QImage& image) {
    std::size_t ink = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qGray(image.pixel(x, y)) < 128) {
                ++ink;
            }
        }
    }
    return static_cast<double>(ink) / (static_cast<double>(image.width()) * image.height());
}

// The rightmost column with ink, -1 for none.
int rightmostInk(const QImage& image) {
    for (int x = image.width() - 1; x >= 0; --x) {
        for (int y = 0; y < image.height(); ++y) {
            if (qGray(image.pixel(x, y)) < 128) {
                return x;
            }
        }
    }
    return -1;
}

} // namespace

TEST_CASE("Score painter: the fonts it ships with", "[app]") {
    const auto fonts =
        ScoreFonts::fromDirectory(QString::fromStdU16String(kFonts.generic_u16string()));
    CHECK_FALSE(fonts.music.isEmpty());
    CHECK_FALSE(fonts.text.isEmpty());
    CHECK_FALSE(fonts.italic.isEmpty());
    CHECK_FALSE(fonts.bold.isEmpty());
    CHECK(ScoreFonts::fromDirectory(QStringLiteral("no/such/folder")).music.isEmpty());
}

TEST_CASE("Score painter: a bold text is in the bold face, as wide as layout takes it", "[app]") {
    // ADR-0133: the tempo and the names of sections.
    constexpr double kScale = 20.0;
    constexpr double kLeft = 1.0;
    const std::string text = "Chorus = 120";
    const auto paint = [&](TextFace face) {
        DisplayList list = blank(30.0, -3.0, 1.0);
        list.texts.push_back(TextItem{.text = text,
                                      .origin = {kLeft, 0.0},
                                      .size = 2.0,
                                      .align = TextAlign::Left,
                                      .face = face,
                                      .role = Role::Section});
        return painter().render(list, kScale);
    };
    const QImage regular = paint(TextFace::Regular);
    const QImage bold = paint(TextFace::Bold);
    CHECK(inkShare(bold) > 1.3 * inkShare(regular));
    // Its last letter ends where layout's bold widths end the text, which is
    // further than the regular face's.
    const double end = kLeft + elara::notation::textWidth(text, 2.0, TextFace::Bold);
    CHECK(end > kLeft + elara::notation::textWidth(text, 2.0) + 0.5);
    const double inkEnd = static_cast<double>(rightmostInk(bold) + 1) / kScale;
    CHECK(inkEnd > end - 0.3);
    CHECK(inkEnd < end + 0.05);
}

TEST_CASE("Score painter: an image of the list's size, white where nothing is drawn", "[app]") {
    REQUIRE(painter().isValid());
    const DisplayList list = blank();
    const QImage image = painter().render(list, 10.0);
    CHECK(image.width() == 200);
    CHECK(image.height() == 80);
    CHECK(inkShare(image) == 0.0);
    CHECK(image.pixelColor(100, 40) == QColor(Qt::white));
}

TEST_CASE("Score painter: device pixels for high-density screens", "[app]") {
    const QImage image = painter().render(blank(), 10.0, 2.0);
    CHECK(image.width() == 400);
    CHECK(image.height() == 160);
    CHECK(image.devicePixelRatio() == 2.0);
}

TEST_CASE("Score painter: lines, dashed lines and beams", "[app]") {
    DisplayList list = blank();
    list.lines.push_back(LineItem{.from = {0.0, 0.0}, .to = {20.0, 0.0}, .thickness = 0.13});
    list.lines.push_back(
        LineItem{.from = {0.0, 4.0}, .to = {20.0, 4.0}, .thickness = 0.13, .dash = 1.0});
    list.beams.push_back(BeamItem{.left = {2.0, 2.0}, .right = {8.0, 2.0}, .thickness = 0.5});
    constexpr double kScale = 20.0;
    const QImage image = painter().render(list, kScale);

    CHECK(inkAt(image, list, kScale, 10.0, 0.0));
    CHECK_FALSE(inkAt(image, list, kScale, 10.0, 0.5));
    // Dashes one staff space long with gaps as long.
    CHECK(inkAt(image, list, kScale, 0.5, 4.0));
    CHECK_FALSE(inkAt(image, list, kScale, 1.5, 4.0));
    CHECK(inkAt(image, list, kScale, 2.5, 4.0));
    CHECK(inkAt(image, list, kScale, 5.0, 2.0));
    CHECK(inkAt(image, list, kScale, 5.0, 2.2));
    CHECK_FALSE(inkAt(image, list, kScale, 5.0, 2.4));
    CHECK_FALSE(inkAt(image, list, kScale, 9.0, 2.0));
}

TEST_CASE("Score painter: glyphs from the music font at their SMuFL origin", "[app]") {
    DisplayList list = blank();
    list.glyphs.push_back(GlyphItem{.glyph = Glyph::NoteheadWhole, .origin = {5.0, 2.0}});
    constexpr double kScale = 20.0;
    const QImage image = painter().render(list, kScale);

    // A whole note is a ring: ink on its left and right, its hole in the middle.
    const auto& box = metrics().metrics(Glyph::NoteheadWhole).box;
    const double left = 5.0 + box.southWest.x;
    const double right = 5.0 + box.northEast.x;
    CHECK(inkAt(image, list, kScale, left + 0.1, 2.0));
    CHECK(inkAt(image, list, kScale, right - 0.1, 2.0));
    CHECK_FALSE(inkAt(image, list, kScale, (left + right) / 2, 2.0));
    CHECK_FALSE(inkAt(image, list, kScale, (left + right) / 2, 3.0));
    CHECK_FALSE(inkAt(image, list, kScale, right + 0.3, 2.0));
}

TEST_CASE("Score painter: knocked-out text interrupts the line under it", "[app]") {
    DisplayList list = blank();
    list.lines.push_back(LineItem{.from = {0.0, 2.0}, .to = {20.0, 2.0}, .thickness = 0.13});
    const double size = 1.3;
    list.texts.push_back(TextItem{.text = "0",
                                  .origin = {10.0, 2.0 + elara::notation::capHeight(size) / 2},
                                  .size = size,
                                  .align = TextAlign::Centre,
                                  .knockout = true,
                                  .role = Role::FretNumber});
    constexpr double kScale = 20.0;
    const QImage image = painter().render(list, kScale);

    CHECK(inkAt(image, list, kScale, 3.0, 2.0));
    CHECK(inkAt(image, list, kScale, 17.0, 2.0));
    // The line stops at the zero, whose hole shows the white of the knockout.
    CHECK_FALSE(inkAt(image, list, kScale, 10.0, 2.0));
    // The zero itself: ink at its top, nothing above it.
    CHECK(inkIn(image, list, kScale, 9.6, 1.4, 10.4, 1.7));
    CHECK_FALSE(inkIn(image, list, kScale, 9.0, 0.5, 11.0, 0.9));
}

TEST_CASE("Score painter: a dark page has its paper, its ink and knockouts of its paper", "[app]") {
    DisplayList list = blank();
    list.lines.push_back(LineItem{.from = {0.0, 2.0}, .to = {20.0, 2.0}, .thickness = 0.13});
    const double size = 1.6;
    list.texts.push_back(TextItem{.text = "0",
                                  .origin = {10.0, 2.0 + elara::notation::capHeight(size) / 2},
                                  .size = size,
                                  .align = TextAlign::Centre,
                                  .knockout = true,
                                  .role = Role::FretNumber});
    constexpr double kScale = 20.0;
    const ScoreColours night{.paper = QColor(0x1c, 0x1b, 0x21), .ink = QColor(0xe7, 0xe5, 0xec)};
    const QImage image = painter().render(list, kScale, 1.0, night);
    const auto colourAt = [&](double x, double y) {
        return image.pixelColor(static_cast<int>(std::floor(x * kScale)),
                                static_cast<int>(std::floor((y - list.top) * kScale)));
    };

    // The paper where nothing is drawn, the ink on the line.
    CHECK(colourAt(1.0, 0.0) == night.paper);
    CHECK(colourAt(3.0, 2.0) == night.ink);
    // The zero's hole shows the paper of the knockout, not white: the line
    // stops there and nothing lighter than the paper is left behind it.
    CHECK(colourAt(10.0, 2.0) == night.paper);
    // By default a page is black on white, as it is printed.
    const QImage day = painter().render(list, kScale);
    CHECK(day.pixelColor(20, 0) == QColor(Qt::white));
    CHECK(inkAt(day, list, kScale, 3.0, 2.0));
}

TEST_CASE("Score painter: a page on opaque paper is opaque everywhere", "[app]") {
    // What lets ScorePage hand the page to the scene graph as an opaque
    // texture, drawn without blending: nothing painted leaves a hole, not
    // the knockouts behind fret numbers, glyphs, curves or polygons.
    const auto opened = elara::io::openFile(kCorpus / "gp/guitar-techniques.gp");
    REQUIRE(opened.has_value());
    const std::array<std::size_t, 1> tracks{0};
    const auto staves =
        elara::notation::selectTracks(opened->song, tracks, elara::notation::TrackView::Both);
    const auto pages = elara::notation::layoutPages(
        opened->song, staves, metrics(), elara::notation::pageOptions(elara::notation::kA4));
    REQUIRE_FALSE(pages.empty());
    constexpr double kScale = 8.0;
    const ScoreColours night{.paper = QColor(0x27, 0x26, 0x2e), .ink = QColor(0xe7, 0xe5, 0xec)};
    for (const ScoreColours& colours : std::array{ScoreColours{}, night}) {
        const QImage image = painter().render(pages.front(), kScale, 1.0, colours);
        REQUIRE_FALSE(image.isNull());
        std::size_t holes = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) != 255) {
                    ++holes;
                }
            }
        }
        CHECK(holes == 0);
    }
}

TEST_CASE("Score painter: ties are lenses, thick in the middle", "[app]") {
    DisplayList list = blank();
    list.curves.push_back(CurveItem{.from = {2.0, 2.0},
                                    .control1 = {4.0, 0.5},
                                    .control2 = {10.0, 0.5},
                                    .to = {12.0, 2.0},
                                    .thickness = 0.3,
                                    .endThickness = 0.05,
                                    .role = Role::Tie});
    list.polygons.push_back(
        PolygonItem{.points = {{15.0, 0.0}, {17.0, 0.0}, {16.0, 2.0}}, .role = Role::Bend});
    constexpr double kScale = 20.0;
    const QImage image = painter().render(list, kScale);

    // The middle of the curve is at y = 0.875 (t = 0.5).
    CHECK(inkAt(image, list, kScale, 7.0, 0.875));
    CHECK_FALSE(inkAt(image, list, kScale, 7.0, 1.5));
    CHECK_FALSE(inkAt(image, list, kScale, 7.0, 0.3));
    CHECK(inkAt(image, list, kScale, 16.0, 0.5));
    CHECK_FALSE(inkAt(image, list, kScale, 15.2, 1.5));
}

TEST_CASE("Score painter: without a music font, everything but glyphs is painted", "[app]") {
    ScoreFonts fonts;
    fonts.music = QByteArrayLiteral("not a font");
    const ScorePainter broken(fonts);
    CHECK_FALSE(broken.isValid());
    DisplayList list = blank();
    list.lines.push_back(LineItem{.from = {0.0, 0.0}, .to = {20.0, 0.0}, .thickness = 0.2});
    list.glyphs.push_back(GlyphItem{.glyph = Glyph::NoteheadBlack, .origin = {5.0, 2.0}});
    const QImage image = broken.render(list, 10.0);
    CHECK(inkAt(image, list, 10.0, 10.0, 0.0));
    CHECK_FALSE(inkAt(image, list, 10.0, 5.5, 2.0));
}

TEST_CASE("Score painter: a laid-out page keeps its ink inside the margins", "[app]") {
    // Apart from the footer (copyright or page number) in the bottom margin.
    const auto opened = elara::io::openFile(kCorpus / "gp/guitar-techniques.gp");
    REQUIRE(opened.has_value());
    const std::array<std::size_t, 1> tracks{0};
    const auto staves =
        elara::notation::selectTracks(opened->song, tracks, elara::notation::TrackView::Both);
    const auto options = elara::notation::pageOptions(elara::notation::kA4);
    const auto pages = elara::notation::layoutPages(opened->song, staves, metrics(), options);
    REQUIRE_FALSE(pages.empty());
    constexpr double kScale = 8.0;
    const QImage image = painter().render(pages.front(), kScale);
    CHECK(image.width() == static_cast<int>(std::ceil(options.width * kScale)));
    CHECK(image.height() == static_cast<int>(std::ceil(options.height * kScale)));

    const double share = inkShare(image);
    CHECK(share > 0.003);
    CHECK(share < 0.3);
    const int margin = static_cast<int>((options.margin - 1.0) * kScale);
    const double footerBaseline = options.height - options.margin / 2;
    const int footerTop = static_cast<int>((footerBaseline - 1.5) * kScale);
    const int footerBottom = static_cast<int>((footerBaseline + 0.5) * kScale);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const bool footer = y >= footerTop && y < footerBottom;
            const bool inside = x >= margin && x < image.width() - margin &&
                                ((y >= margin && y < image.height() - margin) || footer);
            if (!inside && qGray(image.pixel(x, y)) < 128) {
                FAIL_CHECK("ink at " << x << ", " << y << " outside the margins");
                return;
            }
        }
    }
}
