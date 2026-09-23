#include <bettercad/core/geometry/HiddenLine.hpp>

#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomAbs_CurveType.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace bettercad::geometry {
namespace {

/// How short a projected curve may be before it is not a line any more.
///
/// An edge running along the direction of sight projects to a point, which is
/// not a drawing error: it is what an edge pointing at you looks like. A box
/// seen square-on loses its four depth edges this way. 1e-7 mm is the figure
/// the rest of the geometry module uses to decide that two points coincide.
constexpr double kDegenerateSi = 1e-10;

/// How far a sampled polyline may depart from the curve it samples.
///
/// 0.01 mm in MODEL units. Deterministic for a given curve, which is what
/// matters here; it is not aware of the scale the drawing will be at, and
/// that is recorded as a limitation rather than guessed at.
constexpr double kDeflectionSi = 1e-5;

/// Reads a projected point as view-plane coordinates.
///
/// The kernel returns its hidden-line result already projected, in the
/// coordinate system of the projector it was given. That projector is built
/// from the view basis below, so x and y here are the basis's own axes and
/// z is the (discarded) depth -- which is exactly what Frame3D::toLocal
/// gives for the same model point, and a test asserts that rather than
/// trusting it.
[[nodiscard]] Point2D projected(const gp_Pnt& point) noexcept {
    return Point2D{occt::lengthFromModel(point.X()), occt::lengthFromModel(point.Y())};
}

[[nodiscard]] EdgeCurve curveKind(const BRepAdaptor_Curve& curve) noexcept {
    switch (curve.GetType()) {
    case GeomAbs_Line:
        return EdgeCurve::Line;
    case GeomAbs_Circle:
        return EdgeCurve::Circle;
    default:
        // A circle seen at an angle projects to an ellipse; the model edge
        // was a circle and the drawn curve is not, and the drawn one is what
        // this reports.
        return EdgeCurve::Other;
    }
}

/// Appends every edge of one of the kernel's result compounds.
void collect(const TopoDS_Shape& compound, EdgeVisibility visibility, ProjectedEdgeKind kind,
             std::size_t source, std::vector<ProjectedEdge>& into) {
    if (compound.IsNull()) {
        return;
    }
    for (TopExp_Explorer it(compound, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(it.Current());
        const BRepAdaptor_Curve curve(edge);
        const double first = curve.FirstParameter();
        const double last = curve.LastParameter();
        const double length = GCPnts_AbscissaPoint::Length(curve);
        if (!(length > kDegenerateSi)) {
            continue; // an edge pointing at the viewer; there is no line to draw
        }
        ProjectedEdge drawn;
        drawn.curve = curveKind(curve);
        drawn.start = projected(curve.Value(first));
        drawn.end = projected(curve.Value(last));
        drawn.midpoint = projected(curve.Value(0.5 * (first + last)));
        drawn.length = occt::lengthFromModel(length);
        drawn.visibility = visibility;
        drawn.kind = kind;
        drawn.source = source;
        if (drawn.curve == EdgeCurve::Line) {
            // A straight edge IS its two endpoints; sampling it would only
            // add points that say nothing.
            drawn.polyline = {drawn.start, drawn.end};
        } else {
            GCPnts_QuasiUniformDeflection sampler(const_cast<BRepAdaptor_Curve&>(curve),
                                                  kDeflectionSi);
            if (sampler.IsDone() && sampler.NbPoints() >= 2) {
                drawn.polyline.reserve(static_cast<std::size_t>(sampler.NbPoints()));
                for (int i = 1; i <= sampler.NbPoints(); ++i) {
                    drawn.polyline.push_back(projected(sampler.Value(i)));
                }
            } else {
                // The sampler declining is not a reason to draw nothing:
                // fall back to the three points the curve is described by,
                // which is still better than a chord from start to end.
                drawn.polyline = {drawn.start, drawn.midpoint, drawn.end};
            }
        }
        into.push_back(drawn);
    }
}

/// Orders two points the way the canonical sort does.
[[nodiscard]] bool before(const Point2D& a, const Point2D& b) noexcept {
    if (a.x.si() != b.x.si()) {
        return a.x.si() < b.x.si();
    }
    return a.y.si() < b.y.si();
}

} // namespace

std::string_view toString(EdgeVisibility visibility) noexcept {
    switch (visibility) {
    case EdgeVisibility::Visible:
        return "visible";
    case EdgeVisibility::Hidden:
        return "hidden";
    }
    return "unknown";
}

std::string_view toString(ProjectedEdgeKind kind) noexcept {
    switch (kind) {
    case ProjectedEdgeKind::Sharp:
        return "sharp";
    case ProjectedEdgeKind::Smooth:
        return "smooth";
    case ProjectedEdgeKind::Outline:
        return "outline";
    case ProjectedEdgeKind::Sewn:
        return "sewn";
    }
    return "unknown";
}

std::size_t HiddenLineDrawing::count(EdgeVisibility visibility) const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(edges, [&](const ProjectedEdge& e) { return e.visibility == visibility; }));
}

std::size_t HiddenLineDrawing::count(ProjectedEdgeKind kind) const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(edges, [&](const ProjectedEdge& e) { return e.kind == kind; }));
}

std::size_t HiddenLineDrawing::count(EdgeVisibility visibility, ProjectedEdgeKind kind) const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(edges, [&](const ProjectedEdge& e) {
        return e.visibility == visibility && e.kind == kind;
    }));
}

Result<HiddenLineDrawing> hiddenLineDrawing(const Body& body, const Frame3D& viewBasis) {
    // The single body IS a set of one, so there is one implementation and the
    // part path cannot drift from the assembly path (ADR-021).
    return hiddenLineDrawing(std::span<const Body>(&body, 1), viewBasis);
}

Result<HiddenLineDrawing> hiddenLineDrawing(std::span<const Body> bodies, const Frame3D& viewBasis) {
    if (bodies.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "hidden line removal: there are no bodies, so there is nothing to draw");
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].isEmpty()) {
            // Not "skip it": a set with a member missing draws a different
            // assembly, and it would look like a complete drawing of one.
            return makeError(ErrorCode::FailedPrecondition,
                             bodies.size() == 1
                                 ? std::string("hidden line removal: the body is empty, so there "
                                               "is nothing to draw")
                                 : std::format("hidden line removal: body {} of {} is empty, and a "
                                               "set with a member missing draws a different thing",
                                               i, bodies.size()));
        }
    }
    return occt::guardKernelCall("hidden line removal", [&]() -> Result<HiddenLineDrawing> {
        // The projector's Z is the direction the viewer is on, and its X is
        // the view's own right. Its Y is then Z x X, which is how Frame3D
        // builds its Y as well -- so the projected coordinates come out in
        // the view's axes and need no further mapping.
        const gp_Ax2 axes(occt::toModel(viewBasis.origin()), occt::toModel(viewBasis.normal()),
                          occt::toModel(viewBasis.xAxis()));

        // EVERY body goes into ONE algorithm, which is what makes the
        // occlusion between them the kernel's answer rather than something
        // assembled afterwards (ADR-021). Run one problem each, and every
        // body would be classified against itself alone: a body standing
        // wholly behind another would come back fully visible.
        Handle(HLRBRep_Algo) algorithm = new HLRBRep_Algo();
        std::vector<TopoDS_Shape> shapes;
        shapes.reserve(bodies.size());
        for (const Body& body : bodies) {
            shapes.push_back(*occt::BodyAccess::shape(body));
            algorithm->Add(shapes.back());
        }
        algorithm->Projector(HLRAlgo_Projector(axes));
        algorithm->Update();
        algorithm->Hide();

        HLRBRep_HLRToShape toShape(algorithm);
        HiddenLineDrawing drawing;

        // The eight sets the kernel separates its answer into, mapped onto
        // the two independent facts BetterCAD keeps: is it hidden, and what
        // kind of line is it. Iso-parametric lines are deliberately not
        // collected -- they are a surface-display aid, not a drawing.
        // Extracted PER SHAPE, so each line comes back already attributed to
        // the body it belongs to. Asking for the whole answer at once would
        // give the same lines with nothing to say which occurrence drew them.
        struct Set {
            TopoDS_Shape shape;
            EdgeVisibility visibility;
            ProjectedEdgeKind kind;
        };
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            const TopoDS_Shape& of = shapes[i];
            const std::array<Set, 8> sets{{
                {toShape.VCompound(of), EdgeVisibility::Visible, ProjectedEdgeKind::Sharp},
                {toShape.Rg1LineVCompound(of), EdgeVisibility::Visible, ProjectedEdgeKind::Smooth},
                {toShape.RgNLineVCompound(of), EdgeVisibility::Visible, ProjectedEdgeKind::Sewn},
                {toShape.OutLineVCompound(of), EdgeVisibility::Visible, ProjectedEdgeKind::Outline},
                {toShape.HCompound(of), EdgeVisibility::Hidden, ProjectedEdgeKind::Sharp},
                {toShape.Rg1LineHCompound(of), EdgeVisibility::Hidden, ProjectedEdgeKind::Smooth},
                {toShape.RgNLineHCompound(of), EdgeVisibility::Hidden, ProjectedEdgeKind::Sewn},
                {toShape.OutLineHCompound(of), EdgeVisibility::Hidden, ProjectedEdgeKind::Outline},
            }};
            for (const Set& set : sets) {
                collect(set.shape, set.visibility, set.kind, i, drawing.edges);
            }
        }

        // Canonical order. The kernel's traversal order carries no meaning
        // and is not stable enough to compare two runs by, so the result is
        // sorted on what it IS rather than on how it was found.
        std::ranges::sort(drawing.edges, [](const ProjectedEdge& a, const ProjectedEdge& b) {
            if (a.kind != b.kind) {
                return a.kind < b.kind;
            }
            if (a.visibility != b.visibility) {
                return a.visibility < b.visibility;
            }
            if (before(a.start, b.start) || before(b.start, a.start)) {
                return before(a.start, b.start);
            }
            if (before(a.end, b.end) || before(b.end, a.end)) {
                return before(a.end, b.end);
            }
            if (a.length.si() != b.length.si()) {
                return a.length.si() < b.length.si();
            }
            // Two bodies can draw the same line -- two plates meeting face to
            // face do exactly that -- so the source is part of the order.
            // Without it the two would sort against each other by whatever
            // the kernel happened to return first.
            return a.source < b.source;
        });
        return drawing;
    });
}

} // namespace bettercad::geometry
