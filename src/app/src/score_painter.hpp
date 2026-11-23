// SPDX-License-Identifier: AGPL-3.0-or-later
// Draws notation display lists with QPainter (ADR-0015).
#pragma once

#include <elara/notation/display_list.hpp>

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QString>

namespace elara::app {

/// The fonts a score is painted with: the bytes of their font files.
struct ScoreFonts {
    QByteArray music;  ///< the SMuFL music font (Bravura.otf)
    QByteArray text;   ///< the sans-serif text font (Arimo.ttf); empty: the system's
    QByteArray italic; ///< its italic (Arimo-Italic.ttf); empty: the system's

    /// Bravura.otf, Arimo.ttf and Arimo-Italic.ttf from `directory` (a folder
    /// or a resource prefix such as ":/fonts"); missing files stay empty.
    [[nodiscard]] static ScoreFonts fromDirectory(const QString& directory);
};

/// Paints display lists: glyphs of the music font and text as outlines, lines,
/// beams, curves and polygons as shapes, in the order the SVG writer uses.
/// Painting works the same on every platform, without a screen and on any
/// thread: each call makes its own font objects, and copies share the font
/// bytes.
class ScorePainter {
public:
    explicit ScorePainter(ScoreFonts fonts);

    /// Whether the music font could be read.
    [[nodiscard]] bool isValid() const;

    /// Paints `list` with one staff space `scale` pixels long, its (0, 0) at
    /// the painter's origin.
    void paint(QPainter& painter, const notation::DisplayList& list, double scale) const;

    /// `list` on white, from its top (0 for a page) to its bottom, at `scale`
    /// pixels per staff space and `devicePixelRatio` device pixels per pixel.
    [[nodiscard]] QImage render(const notation::DisplayList& list, double scale,
                                double devicePixelRatio = 1.0) const;

private:
    ScoreFonts fonts_;
    QColor ink_{Qt::black};
};

} // namespace elara::app
