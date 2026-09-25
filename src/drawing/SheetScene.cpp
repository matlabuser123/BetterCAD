#include <bettercad/drawing/SheetScene.hpp>

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Section.hpp>
#include <bettercad/drawing/Sheets.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <vector>

namespace bettercad::drawing {
namespace {

/// How a projected edge is drawn, from what hidden-line removal already
/// decided about it. Nothing is reclassified here: this is a lookup, and the
/// engineering was done in P14-HLR-001.
struct EdgeStyle {
    LineStyle style;
    Length width;
};

[[nodiscard]] EdgeStyle styleFor(const DrawnEdge& edge) noexcept {
    if (edge.visibility == geometry::EdgeVisibility::Hidden) {
        // ISO 128: hidden detail is a narrow dashed line.
        return EdgeStyle{LineStyle::Dashed, weights::kThin};
    }
    if (edge.kind == geometry::ProjectedEdgeKind::Smooth) {
        // A tangent edge that the view chose to show is drawn narrow: it is
        // real topology but not an outline.
        return EdgeStyle{LineStyle::Continuous, weights::kThin};
    }
    // Visible edges and silhouettes: the wide continuous line a drawing is
    // mostly made of.
    return EdgeStyle{LineStyle::Continuous, weights::kThick};
}

/// The circle through three points, or nothing if they are collinear.
///
/// Used to turn an edge that hidden-line removal reported as an exact CIRCLE
/// back into a centre and a radius, so a writer that has a circle primitive
/// can use one. The polyline is always available as a fallback, so failing
/// here costs fidelity and never correctness.
///
/// TWO cases, because a circular edge comes in two shapes: an ARC, whose
/// three points are distinct, and a CLOSED circle, whose start and end are
/// the same point. The second is the common one -- it is what a hole is.
struct Circle {
    Point2D centre;
    double radius;
};

[[nodiscard]] std::optional<Circle> circleThrough(const Point2D& a, const Point2D& b,
                                                  const Point2D& c) noexcept {
    const double ax = a.x.si();
    const double ay = a.y.si();
    const double bx = b.x.si();
    const double by = b.y.si();
    const double cx = c.x.si();
    const double cy = c.y.si();
    // A CLOSED edge -- every hole on every drawing -- has start == end, so
    // only two of the three points are distinct and the determinant below is
    // identically zero. Those two are half a turn apart, which is a DIAMETER,
    // and that defines the circle exactly. Without this a full circle was
    // never recovered and was drawn as its sampled polyline: over a thousand
    // points where a DXF CIRCLE, an SVG <circle> or four Beziers belong.
    const double diameter = std::hypot(bx - ax, by - ay);
    const double gap = std::hypot(cx - ax, cy - ay);
    if (diameter > 0.0 && gap <= 1e-9 * diameter) {
        return Circle{Point2D{Length::fromSi((ax + bx) / 2.0), Length::fromSi((ay + by) / 2.0)},
                      diameter / 2.0};
    }

    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(d) < 1e-15) {
        return std::nullopt; // collinear: no circle through them
    }
    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;
    const double ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
    const double uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
    const Point2D centre{Length::fromSi(ux), Length::fromSi(uy)};
    const double radius = std::hypot(ax - ux, ay - uy);
    if (!(radius > 0.0) || !std::isfinite(radius)) {
        return std::nullopt;
    }
    return Circle{centre, radius};
}

/// Adds one projected edge, as an arc when it is exactly one and as its
/// polyline otherwise.
void addEdge(SceneItems& items, const DrawnEdge& edge) {
    const EdgeStyle style = styleFor(edge);
    if (edge.curve == geometry::EdgeCurve::Circle) {
        if (const auto circle = circleThrough(edge.start, edge.midpoint, edge.end)) {
            const auto angleAt = [&](const Point2D& p) {
                return std::atan2(p.y.si() - circle->centre.y.si(), p.x.si() - circle->centre.x.si());
            };
            const double from = angleAt(edge.start);
            const double to = angleAt(edge.end);
            const double middle = angleAt(edge.midpoint);
            constexpr double kTwoPi = 2.0 * std::numbers::pi;
            // Which way round, decided by the midpoint rather than assumed:
            // the sweep is the one that passes through it.
            const auto normalise = [](double a) {
                while (a < 0.0) {
                    a += kTwoPi;
                }
                while (a >= kTwoPi) {
                    a -= kTwoPi;
                }
                return a;
            };
            double sweep = normalise(to - from);
            const double toMiddle = normalise(middle - from);
            if (sweep > 1e-12 && toMiddle > sweep) {
                sweep -= kTwoPi; // the short way round does not contain the middle
            }
            if (std::abs(sweep) < 1e-12) {
                sweep = kTwoPi; // start and end coincide: a whole circle
            }
            items.arcs.push_back(SceneArc{.centre = circle->centre,
                                          .radius = Length::fromSi(circle->radius),
                                          .start = Angle::fromSi(from),
                                          .sweep = Angle::fromSi(sweep),
                                          .style = style.style,
                                          .width = style.width});
            return;
        }
    }
    if (edge.polyline.size() < 2) {
        return; // nothing to draw; validate() would refuse it and it says nothing
    }
    items.lines.push_back(
        SceneLine{.points = edge.polyline, .style = style.style, .width = style.width});
}

} // namespace

Result<DrawingScene> sheetScene(const Document& document, SheetId sheet, const BodyLookup& bodies,
                                const TransformLookup& transforms) {
    const Sheet* page = findSheet(document, sheet);
    if (page == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("there is no sheet {}", sheet));
    }
    const auto [width, height] = page->size();
    DrawingScene scene{.width = width, .height = height};

    // The frame: the sheet's border, at its margins. Wide, because ISO 128
    // draws a drawing frame with the same line as a visible edge.
    const BoundingBox2D usable = page->usableRegion();
    scene.items.lines.push_back(
        SceneLine{.points = {usable.min,
                             Point2D{usable.max.x, usable.min.y},
                             usable.max,
                             Point2D{usable.min.x, usable.max.y},
                             usable.min},
                  .style = LineStyle::Continuous,
                  .width = weights::kThick});

    // Each view, in ascending ID order, so two runs of one document give one
    // sequence and a comparison means something.
    for (const ViewId id : viewsOn(document, sheet)) {
        auto drawn = projectedGeometry(document, id, bodies, transforms);
        if (!drawn) {
            return makeError(drawn.error().code,
                             std::format("{} cannot be drawn: {}", id, drawn.error().message));
        }
        for (const DrawnEdge& edge : drawn->edges) {
            addEdge(scene.items, edge);
        }

        // A section's hatch, already clipped to the material and already in
        // sheet coordinates (P14-VIEW-002). Narrow continuous, as ISO 128
        // hatches.
        const View* view = findView(document, id);
        if (view != nullptr && view->definition().kind == ViewKind::Section) {
            auto section = sectionOf(document, id, bodies, transforms);
            if (!section) {
                return makeError(section.error().code,
                                 std::format("{}'s section cannot be drawn: {}", id,
                                             section.error().message));
            }
            for (const auto& [from, to] : section->hatch) {
                scene.items.lines.push_back(SceneLine{.points = std::vector<Point2D>{from, to},
                                                     .style = LineStyle::Thin,
                                                     .width = weights::kThin});
            }
        }

        auto dimensions = drawDimensions(document, id, bodies, transforms);
        if (!dimensions) {
            return std::unexpected(dimensions.error());
        }
        scene.items.append(*dimensions);

        auto annotations = drawAnnotations(document, id, bodies, transforms);
        if (!annotations) {
            return std::unexpected(annotations.error());
        }
        scene.items.append(*annotations);
    }

    // Checked before it leaves, so no writer ever receives a scene with a
    // point off the page (ADR-016).
    if (auto valid = validate(scene); !valid) {
        return std::unexpected(valid.error());
    }
    return scene;
}

} // namespace bettercad::drawing
