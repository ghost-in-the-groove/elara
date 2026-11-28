// SPDX-License-Identifier: AGPL-3.0-or-later
#include "score_painter.hpp"

#include <elara/notation/smufl.hpp>

#include <QFile>
#include <QFont>
#include <QHash>
#include <QList>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRawFont>
#include <QRectF>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace elara::app {

namespace {

using notation::DisplayList;

// One em of the music font is four staff spaces (SMuFL).
constexpr double kStaffSpacesPerEm = 4.0;

QPointF at(const notation::Point& p, double scale) {
    return {p.x * scale, p.y * scale};
}

// A face of the text font from its bytes or, without them, the system's
// sans-serif in that face.
QRawFont textFont(const ScoreFonts& fonts, notation::TextFace face) {
    const QByteArray& bytes = face == notation::TextFace::Italic ? fonts.italic
                              : face == notation::TextFace::Bold ? fonts.bold
                                                                 : fonts.text;
    if (!bytes.isEmpty()) {
        QRawFont font(bytes, 1.0, QFont::PreferNoHinting);
        if (font.isValid()) {
            return font;
        }
    }
    QFont font;
    font.setFamilies(
        {QStringLiteral("Arial"), QStringLiteral("Helvetica"), QStringLiteral("Liberation Sans")});
    font.setStyleHint(QFont::SansSerif);
    font.setItalic(face == notation::TextFace::Italic);
    font.setBold(face == notation::TextFace::Bold);
    return QRawFont::fromFont(font);
}

QByteArray readAll(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Glyph outlines of one font at one size, each made once. Glyphs are painted
// as outlines rather than through the platform's glyph cache: that works the
// same on every platform, on worker threads and without a screen. Each glyph
// is filled on its own: Qt's rasteriser fills many small paths several times
// faster than one large path holding them all (docs/performance.md).
class Outlines {
public:
    explicit Outlines(QRawFont font) : font_(std::move(font)) {}

    [[nodiscard]] const QRawFont& font() const { return font_; }

    // Fills glyph `index` with its origin at `origin`, `size` times as large.
    void fill(QPainter& painter, quint32 index, const QPointF& origin, const QColor& ink,
              double size = 1.0) {
        auto found = outlines_.find(index);
        if (found == outlines_.end()) {
            found = outlines_.insert(index, font_.pathForGlyph(index));
        }
        if (found->isEmpty()) {
            return;
        }
        if (size == 1.0) {
            painter.fillPath(found->translated(origin), ink);
            return;
        }
        QTransform transform;
        transform.translate(origin.x(), origin.y());
        transform.scale(size, size);
        painter.fillPath(transform.map(*found), ink);
    }

private:
    QRawFont font_;
    QHash<quint32, QPainterPath> outlines_;
};

void paintLines(QPainter& painter, const DisplayList& list, double scale,
                const ScoreColours& colours) {
    const QColor& staff = colours.staff.isValid() ? colours.staff : colours.ink;
    QPen pen(colours.ink);
    pen.setCapStyle(Qt::FlatCap);
    for (const notation::LineItem& line : list.lines) {
        pen.setColor(line.role == notation::Role::StaffLine ? staff : colours.ink);
        pen.setWidthF(line.thickness * scale);
        if (line.dash > 0.0 && line.thickness > 0.0) {
            // Dash patterns are in pen widths.
            const double dash = line.dash / line.thickness;
            pen.setDashPattern({dash, dash});
        } else {
            pen.setStyle(Qt::SolidLine);
        }
        painter.setPen(pen);
        painter.drawLine(at(line.from, scale), at(line.to, scale));
    }
    painter.setPen(Qt::NoPen);
}

void paintBeams(QPainter& painter, const DisplayList& list, double scale) {
    for (const notation::BeamItem& beam : list.beams) {
        const double half = beam.thickness / 2 * scale;
        const QPointF left = at(beam.left, scale);
        const QPointF right = at(beam.right, scale);
        const std::array<QPointF, 4> corners{
            QPointF(left.x(), left.y() - half), QPointF(right.x(), right.y() - half),
            QPointF(right.x(), right.y() + half), QPointF(left.x(), left.y() + half)};
        painter.drawPolygon(corners.data(), static_cast<int>(corners.size()));
    }
}

void paintGlyphs(QPainter& painter, const DisplayList& list, double scale,
                 const QByteArray& musicFont, const QColor& ink) {
    if (list.glyphs.empty()) {
        return;
    }
    Outlines outlines(QRawFont(musicFont, kStaffSpacesPerEm * scale, QFont::PreferNoHinting));
    if (!outlines.font().isValid()) {
        return;
    }
    QString text;
    text.reserve(static_cast<qsizetype>(list.glyphs.size()));
    for (const notation::GlyphItem& glyph : list.glyphs) {
        // SMuFL code points are in the Basic Multilingual Plane.
        text.append(QChar(static_cast<char16_t>(notation::codepoint(glyph.glyph))));
    }
    const QList<quint32> indexes = outlines.font().glyphIndexesForString(text);
    if (static_cast<std::size_t>(indexes.size()) != list.glyphs.size()) {
        return;
    }
    for (std::size_t i = 0; i < list.glyphs.size(); ++i) {
        outlines.fill(painter, indexes[static_cast<qsizetype>(i)], at(list.glyphs[i].origin, scale),
                      ink, list.glyphs[i].scale);
    }
}

// Knockouts first, then the letters, so that a knockout never clips a
// neighbouring text. Fonts are made once per size and outlines once per glyph
// and size.
void paintTexts(QPainter& painter, const DisplayList& list, double scale, const ScoreFonts& fonts,
                const ScoreColours& colours) {
    if (list.texts.empty()) {
        return;
    }
    // Each face of the font is read once, when a text first needs it.
    std::array<QRawFont, 3> loaded;
    std::map<std::pair<long long, notation::TextFace>, Outlines> faces;
    const auto face = [&](double pixelSize, notation::TextFace textFace) -> Outlines& {
        const std::pair key{std::llround(pixelSize * 64.0), textFace};
        auto found = faces.find(key);
        if (found == faces.end()) {
            QRawFont& whole = loaded[static_cast<std::size_t>(textFace)];
            if (!whole.isValid()) {
                whole = textFont(fonts, textFace);
            }
            QRawFont font = whole;
            font.setPixelSize(pixelSize);
            found = faces.emplace(key, Outlines(std::move(font))).first;
        }
        return found->second;
    };
    struct Letter {
        Outlines* outlines;
        quint32 index;
        QPointF origin;
    };
    std::vector<Letter> letters;
    letters.reserve(list.texts.size() * 2);
    for (const notation::TextItem& item : list.texts) {
        Outlines& outlines = face(item.size * scale, item.face);
        const QList<quint32> indexes = outlines.font().glyphIndexesForString(
            QString::fromUtf8(item.text.data(), static_cast<qsizetype>(item.text.size())));
        const QList<QPointF> advances =
            outlines.font().advancesForGlyphIndexes(indexes, QRawFont::KernedAdvances);
        double width = 0.0;
        for (const QPointF& advance : advances) {
            width += advance.x();
        }
        const QPointF origin = at(item.origin, scale);
        double left = origin.x();
        if (item.align == notation::TextAlign::Centre) {
            left -= width / 2;
        } else if (item.align == notation::TextAlign::Right) {
            left -= width;
        }
        if (item.knockout) {
            const double pad = 0.08 * item.size * scale;
            const double cap = notation::capHeight(item.size) * scale;
            painter.fillRect(
                QRectF(left - pad, origin.y() - cap - pad, width + 2 * pad, cap + 2 * pad),
                colours.paper);
        }
        double x = left;
        for (qsizetype i = 0; i < indexes.size() && i < advances.size(); ++i) {
            letters.push_back({&outlines, indexes[i], QPointF(x, origin.y())});
            x += advances[i].x();
        }
    }
    for (const Letter& letter : letters) {
        letter.outlines->fill(painter, letter.index, letter.origin, colours.ink);
    }
}

void paintCurves(QPainter& painter, const DisplayList& list, double scale, const QColor& ink) {
    for (const notation::CurveItem& curve : list.curves) {
        QPainterPath path(at(curve.from, scale));
        if (curve.thickness <= curve.endThickness) {
            path.cubicTo(at(curve.control1, scale), at(curve.control2, scale), at(curve.to, scale));
            QPen pen(ink, curve.thickness * scale);
            pen.setCapStyle(Qt::FlatCap);
            painter.strokePath(path, pen);
            continue;
        }
        // A lens: the control points moved up and down so that the middle gets
        // `thickness`, outlined with the end thickness to round the tips.
        const double offset = (curve.thickness - curve.endThickness) / 1.5 * scale;
        const auto moved = [scale](const notation::Point& p, double by) {
            return QPointF(p.x * scale, p.y * scale + by);
        };
        path.cubicTo(moved(curve.control1, -offset / 2), moved(curve.control2, -offset / 2),
                     at(curve.to, scale));
        path.cubicTo(moved(curve.control2, offset / 2), moved(curve.control1, offset / 2),
                     at(curve.from, scale));
        path.closeSubpath();
        QPen pen(ink, curve.endThickness * scale);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.drawPath(path);
        painter.setPen(Qt::NoPen);
    }
}

void paintPolygons(QPainter& painter, const DisplayList& list, double scale) {
    for (const notation::PolygonItem& polygon : list.polygons) {
        QPolygonF points;
        points.reserve(static_cast<qsizetype>(polygon.points.size()));
        for (const notation::Point& p : polygon.points) {
            points.append(at(p, scale));
        }
        painter.drawPolygon(points);
    }
}

} // namespace

ScoreFonts ScoreFonts::fromDirectory(const QString& directory) {
    return ScoreFonts{.music = readAll(directory + QStringLiteral("/Bravura.otf")),
                      .text = readAll(directory + QStringLiteral("/Arimo.ttf")),
                      .italic = readAll(directory + QStringLiteral("/Arimo-Italic.ttf")),
                      .bold = readAll(directory + QStringLiteral("/Arimo-Bold.ttf"))};
}

ScorePainter::ScorePainter(ScoreFonts fonts) : fonts_(std::move(fonts)) {}

bool ScorePainter::isValid() const {
    return QRawFont(fonts_.music, kStaffSpacesPerEm).isValid();
}

void ScorePainter::paint(QPainter& painter, const DisplayList& list, double scale,
                         const ScoreColours& colours) const {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.ink);
    // The order of the SVG writer: knockouts clear lines and glyphs, curves
    // and polygons go on top.
    paintLines(painter, list, scale, colours);
    paintBeams(painter, list, scale);
    paintGlyphs(painter, list, scale, fonts_.music, colours.ink);
    paintTexts(painter, list, scale, fonts_, colours);
    paintCurves(painter, list, scale, colours.ink);
    paintPolygons(painter, list, scale);
    painter.restore();
}

QImage ScorePainter::render(const DisplayList& list, double scale, double devicePixelRatio,
                            const ScoreColours& colours) const {
    // Painted in device pixels so that glyphs get their real size, not a
    // scaled-up small one.
    const double pixels = scale * devicePixelRatio;
    const int width = std::max(1, static_cast<int>(std::ceil(list.width * pixels)));
    const int height = std::max(1, static_cast<int>(std::ceil((list.bottom - list.top) * pixels)));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(colours.paper);
    {
        QPainter painter(&image);
        painter.translate(0.0, -list.top * pixels);
        paint(painter, list, pixels, colours);
    }
    image.setDevicePixelRatio(devicePixelRatio);
    return image;
}

} // namespace elara::app
