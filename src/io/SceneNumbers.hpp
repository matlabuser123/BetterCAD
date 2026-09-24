#pragma once

#include <bettercad/core/Units.hpp>
#include <bettercad/drawing/Scene.hpp>

#include <cmath>
#include <format>
#include <string>
#include <vector>

// How the three writers put a number on paper (P14-EXPORT-001).
//
// ONE formatter, because three would eventually disagree and the disagreement
// would be invisible: a drawing that is 0.0001 mm different in one format is
// not something anyone notices until it matters.
//
// The rules, and why each is a rule:
//
//   FIXED NOTATION      never scientific. "1e-05" is a valid double and an
//                       invalid coordinate in DXF and in a PDF content
//                       stream.
//   FOUR DECIMALS       a tenth of a micrometre on paper, which is finer than
//                       any plotter and coarser than double-precision noise.
//   NO LOCALE           std::format is locale-independent by definition,
//                       unlike printf: a German machine must not write 1,25.
//   NO NEGATIVE ZERO    "-0.0000" is the same point as "0.0000" and a
//                       different string, which would break byte determinism
//                       for a coordinate that happened to arrive as -0.
//   TRAILING ZEROS KEPT one spelling per number. Trimming them would make
//                       the output depend on the value's exact bits.
namespace bettercad::io::detail {

/// A millimetre count, written the one way.
[[nodiscard]] inline std::string mm(double millimetres) {
    // -0.0 and 0.0 are the same point and must be the same text.
    if (millimetres == 0.0) {
        millimetres = 0.0;
    }
    return std::format("{:.4f}", millimetres);
}

/// A Length, in millimetres.
[[nodiscard]] inline std::string mm(Length length) { return mm(length.in(units::mm)); }

/// An angle in degrees, written the one way. DXF measures arcs in degrees.
[[nodiscard]] inline std::string degrees(Angle angle) {
    double value = angle.in(units::deg);
    if (value == 0.0) {
        value = 0.0;
    }
    return std::format("{:.6f}", value);
}

/// The dash pattern for a line style, in millimetres on paper.
///
/// ISO 128's patterns, at the figures a 0.5 line group uses. Continuous and
/// Thin have none: a thin line differs from a wide one by its WIDTH, which the
/// scene already carries, and not by being broken up.
[[nodiscard]] inline std::vector<double> dashPattern(drawing::LineStyle style) {
    switch (style) {
    case drawing::LineStyle::Dashed:
        return {4.0, 2.0}; // 4 mm on, 2 mm off
    case drawing::LineStyle::Centre:
        return {12.0, 2.0, 2.0, 2.0}; // long dash, gap, short dash, gap
    case drawing::LineStyle::Continuous:
    case drawing::LineStyle::Thin:
        break;
    }
    return {};
}

/// Samples an arc into points, for the formats that want a path rather than a
/// centre and a radius.
///
/// Fine enough that the chord error is far below what any plotter resolves:
/// one segment per 5 degrees, and never fewer than four for a whole circle.
/// Only PDF needs this, and only because a PDF has no circle operator.
[[nodiscard]] inline std::vector<Point2D> sampleArc(const drawing::SceneArc& arc) {
    const double start = arc.start.si();
    const double sweep = arc.sweep.si();
    const double radius = arc.radius.si();
    const int steps = std::max(4, static_cast<int>(std::ceil(std::abs(sweep) / (5.0 * 3.14159265358979323846 / 180.0))));
    std::vector<Point2D> points;
    points.reserve(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const double at = start + sweep * (static_cast<double>(i) / static_cast<double>(steps));
        points.push_back(Point2D{arc.centre.x + Length::fromSi(radius * std::cos(at)),
                                 arc.centre.y + Length::fromSi(radius * std::sin(at))});
    }
    return points;
}

} // namespace bettercad::io::detail
