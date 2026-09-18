#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Format.hpp>

#include "core/geometry/LoftPlan.hpp"
#include "core/geometry/ProfileExtent.hpp"
#include "core/geometry/SweepPlan.hpp"
#include "core/geometry/SweptPathPlan.hpp"
#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctFaceNames.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/math/BSpline.hpp>

#include <BRepAlgoAPI_Check.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_TransitionMode.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <NCollection_Array1.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepSweep_Revol.hxx>
#include <Geom_BSplineCurve.hxx>
#include <NCollection_Array1.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <utility>

namespace bettercad::geometry {

namespace {

// Sketch tolerance: loop ends must meet within this.
constexpr Length kClosureTolerance = Length::fromSi(1e-10);
// A revolution axis counts as parallel to the profile plane when the cosine
// of its angle to the plane normal is below this (angles near 1e-9 rad;
// axes built from sketch data are parallel to rounding error, ~1e-16).
constexpr double kAxisParallelTolerance = 1e-9;
// Sweep angles within this of 360 degrees make a full revolution; it is far
// below any meaningful angle and far above the rounding of 360 deg -> rad.
constexpr double kFullTurnTolerance = 1e-12;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

bool finite(const Point2D& p) {
    return isFinite(p.x) && isFinite(p.y);
}

/// Checks a segment that is a loop on its own (see isClosedSegment()).
Result<void> checkClosedSegment(const ProfileSegment& segment, std::string_view name) {
    if (const auto* circle = std::get_if<CircleSegment2D>(&segment)) {
        if (!isFinite(circle->radius) || circle->radius <= kClosureTolerance) {
            return makeError(ErrorCode::InvalidArgument, std::format("{} loop has an invalid circle", name));
        }
        return {};
    }
    if (const auto* ellipse = std::get_if<EllipseSegment2D>(&segment)) {
        if (!finite(ellipse->center) || !finite(ellipse->xVertex) || !isFinite(ellipse->radiusY) ||
            distance(ellipse->center, ellipse->xVertex) <= kClosureTolerance ||
            ellipse->radiusY <= kClosureTolerance) {
            return makeError(ErrorCode::InvalidArgument, std::format("{} loop has an invalid ellipse", name));
        }
        return {};
    }
    if (auto valid = validate(std::get<SplineSegment2D>(segment)); !valid) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} loop has an invalid spline: {}", name, valid.error().message));
    }
    return {};
}

std::string_view closedKind(const ProfileSegment& segment) {
    if (std::holds_alternative<CircleSegment2D>(segment)) {
        return "a full circle";
    }
    if (std::holds_alternative<EllipseSegment2D>(segment)) {
        return "a full ellipse";
    }
    return "a periodic spline";
}

Result<void> checkLoop(const ProfileLoop& loop, std::string_view name) {
    if (loop.segments.empty()) {
        return makeError(ErrorCode::InvalidArgument, std::format("{} loop is empty", name));
    }
    const auto closed = std::ranges::find_if(loop.segments, isClosedSegment);
    if (closed != loop.segments.end()) {
        if (loop.segments.size() != 1) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} loop mixes {} with other segments", name, closedKind(*closed)));
        }
        return checkClosedSegment(*closed, name);
    }
    for (std::size_t i = 0; i < loop.segments.size(); ++i) {
        const ProfileSegment& segment = loop.segments[i];
        const ProfileSegment& next = loop.segments[(i + 1) % loop.segments.size()];
        if (const auto* spline = std::get_if<SplineSegment2D>(&segment)) {
            // An open spline may end where it starts (a loop with a corner);
            // its control polygon must not vanish.
            if (auto valid = validate(*spline); !valid) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("{} loop has an invalid spline: {}", name, valid.error().message));
            }
        } else if (distance(firstPoint(segment), lastPoint(segment)) <= kClosureTolerance) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} loop has a degenerate segment", name));
        }
        if (distance(lastPoint(segment), firstPoint(next)) > kClosureTolerance) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} loop is not closed: segment {} does not meet the next", name, i));
        }
        if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
            const Length r0 = distance(arc->center, arc->start);
            const Length r1 = distance(arc->center, arc->end);
            if (r0 <= kClosureTolerance || abs(r1 - r0) > kClosureTolerance) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("{} loop has an arc whose ends are not on one circle", name));
            }
        }
    }
    return {};
}

// Builds OCCT wires for loops on a plane offset along its normal. Vertices are
// created once per distinct 2D point so that consecutive edges share them.
// With @p seamOnXAxis, full circles start (have their seam) on the plane's X
// axis through their centre; otherwise the kernel chooses.
class WireBuilder {
public:
    WireBuilder(const Frame3D& plane, double offset, bool seamOnXAxis = false) : plane_(plane), offset_(offset),
        normal_(occt::toModel(plane.normal())), xAxis_(occt::toModel(plane.xAxis())), seamOnXAxis_(seamOnXAxis) {}

    /// The loop's wire; with @p edges, also its edges in the loop's order.
    Result<TopoDS_Wire> build(const ProfileLoop& loop, std::vector<TopoDS_Edge>* edges = nullptr) {
        BRepBuilderAPI_MakeWire wire;
        for (const ProfileSegment& segment : loop.segments) {
            const TopoDS_Edge edge = makeEdge(segment);
            if (edges != nullptr) {
                edges->push_back(edge);
            }
            wire.Add(edge);
            if (!wire.IsDone()) {
                return makeError(ErrorCode::Internal, "makePrism: segments do not form a connected wire");
            }
        }
        return wire.Wire();
    }

    [[nodiscard]] gp_Pnt point(const Point2D& p) const {
        gp_Pnt q = occt::toModel(plane_.toGlobal(p));
        q.Translate(gp_Vec(normal_) * offset_);
        return q;
    }

    [[nodiscard]] const gp_Dir& normal() const noexcept { return normal_; }

private:
    TopoDS_Vertex vertex(const Point2D& p) {
        const std::pair<double, double> key{p.x.si(), p.y.si()};
        const auto it = vertices_.find(key);
        if (it != vertices_.end()) {
            return it->second;
        }
        TopoDS_Vertex v = BRepBuilderAPI_MakeVertex(point(p));
        vertices_.emplace(key, v);
        return v;
    }

    [[nodiscard]] gp_Circ circle(const Point2D& center, double radius, bool counterClockwise) const {
        // Counter-clockwise about the plane normal is the circle's parametric direction.
        const gp_Dir axis = counterClockwise ? normal_ : normal_.Reversed();
        if (seamOnXAxis_) {
            return gp_Circ(gp_Ax2(point(center), axis, xAxis_), radius);
        }
        return gp_Circ(gp_Ax2(point(center), axis), radius);
    }

    /// The ellipse's first axis runs from the centre to xVertex, the second
    /// 90 deg counter-clockwise from it (about the plane normal). The kernel
    /// needs the major axis first: with a longer second axis, the frame turns
    /// by 90 deg, which traces the same curve in the same sense.
    [[nodiscard]] gp_Elips ellipse(const EllipseSegment2D& segment) const {
        const gp_Pnt center = point(segment.center);
        const gp_Vec first(center, point(segment.xVertex));
        const double a = first.Magnitude();
        const double b = occt::toModel(segment.radiusY);
        const gp_Dir axis = segment.counterClockwise ? normal_ : normal_.Reversed();
        const gp_Dir u(first);
        if (a >= b) {
            return gp_Elips(gp_Ax2(center, axis, u), a, b);
        }
        return gp_Elips(gp_Ax2(center, axis, normal_.Crossed(u)), b, a);
    }

    /// A B-spline edge with the spline's own poles, knots and degree. The
    /// ends of an open spline are the loop's shared vertices.
    [[nodiscard]] TopoDS_Edge splineEdge(const SplineSegment2D& segment) {
        const UniformBSpline curve = *UniformBSpline::create(segment.poles, segment.degree, segment.periodic);
        NCollection_Array1<gp_Pnt> poles(1, static_cast<int>(segment.poles.size()));
        for (std::size_t i = 0; i < segment.poles.size(); ++i) {
            poles.SetValue(static_cast<int>(i) + 1, point(segment.poles[i]));
        }
        const std::vector<double> knotValues = curve.knotValues();
        const std::vector<int> multiplicities = curve.knotMultiplicities();
        NCollection_Array1<double> knots(1, static_cast<int>(knotValues.size()));
        NCollection_Array1<int> mults(1, static_cast<int>(multiplicities.size()));
        for (std::size_t i = 0; i < knotValues.size(); ++i) {
            knots.SetValue(static_cast<int>(i) + 1, knotValues[i]);
            mults.SetValue(static_cast<int>(i) + 1, multiplicities[i]);
        }
        const occ::handle<Geom_BSplineCurve> geometry =
            new Geom_BSplineCurve(poles, knots, mults, segment.degree, segment.periodic);
        if (segment.periodic) {
            return BRepBuilderAPI_MakeEdge(geometry);
        }
        return BRepBuilderAPI_MakeEdge(geometry, vertex(segment.poles.front()), vertex(segment.poles.back()));
    }

    TopoDS_Edge makeEdge(const ProfileSegment& segment) {
        if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
            return BRepBuilderAPI_MakeEdge(vertex(line->start), vertex(line->end));
        }
        if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
            const double radius = occt::toModel(distance(arc->center, arc->start));
            return BRepBuilderAPI_MakeEdge(circle(arc->center, radius, arc->counterClockwise),
                                           vertex(arc->start), vertex(arc->end));
        }
        if (const auto* full = std::get_if<CircleSegment2D>(&segment)) {
            return BRepBuilderAPI_MakeEdge(circle(full->center, occt::toModel(full->radius), full->counterClockwise));
        }
        if (const auto* oval = std::get_if<EllipseSegment2D>(&segment)) {
            return BRepBuilderAPI_MakeEdge(ellipse(*oval));
        }
        return splineEdge(std::get<SplineSegment2D>(segment));
    }

    Frame3D plane_;
    double offset_;
    gp_Dir normal_;
    gp_Dir xAxis_;
    bool seamOnXAxis_;
    std::map<std::pair<double, double>, TopoDS_Vertex> vertices_;
};

/// Checks the region's loops (InvalidArgument for malformed ones).
Result<void> checkRegion(const PlanarRegion& region) {
    if (auto valid = checkLoop(region.outer, "outer"); !valid) {
        return std::unexpected(valid.error());
    }
    for (const ProfileLoop& hole : region.holes) {
        if (auto valid = checkLoop(hole, "hole"); !valid) {
            return std::unexpected(valid.error());
        }
    }
    return {};
}

/// The edges of a loop built from @p loop, or from its reversal, in @p loop's
/// segment order.
using LoopEdges = std::vector<TopoDS_Edge>;

/// Planar face of a checked region, moved along the plane normal by
/// @p offset (model units). Loop orientation is normalized: outer
/// counter-clockwise, holes clockwise. With @p edges, also the edges of each
/// loop (the outer loop first, then the holes) in the region's segment order.
/// Call inside guardKernelCall.
Result<TopoDS_Face> makeProfileFace(const PlanarRegion& region, double offset, std::string_view operation,
                                    std::vector<LoopEdges>* edges = nullptr) {
    WireBuilder builder(region.plane, offset);
    // Builds a loop, oriented as asked, recording its edges in the given order.
    const auto buildLoop = [&](const ProfileLoop& loop, bool reverse) -> Result<TopoDS_Wire> {
        LoopEdges built;
        auto wire = builder.build(reverse ? reversed(loop) : loop, edges != nullptr ? &built : nullptr);
        if (wire && edges != nullptr) {
            if (reverse) {
                std::ranges::reverse(built);
            }
            edges->push_back(std::move(built));
        }
        return wire;
    };
    auto outerWire = buildLoop(region.outer, signedArea(region.outer) < Area{});
    if (!outerWire) {
        return std::unexpected(outerWire.error());
    }
    const gp_Pln plane(gp_Ax3(builder.point(Point2D{}), builder.normal(), occt::toModel(region.plane.xAxis())));
    BRepBuilderAPI_MakeFace face(plane, *outerWire, /*Inside=*/true);
    if (!face.IsDone()) {
        return makeError(ErrorCode::Internal, std::format("{}: cannot build a face from the outer loop", operation));
    }
    for (const ProfileLoop& hole : region.holes) {
        auto holeWire = buildLoop(hole, signedArea(hole) > Area{});
        if (!holeWire) {
            return std::unexpected(holeWire.error());
        }
        face.Add(*holeWire);
    }
    TopoDS_Face profile = face.Face();
    if (!BRepCheck_Analyzer(profile).IsValid()) {
        return makeError(ErrorCode::Internal,
                         std::format("{}: the profile face is invalid (self-intersecting or overlapping loops?)",
                                     operation));
    }
    return profile;
}

/// The sweep path as one connected wire: each segment starts at the vertex
/// where the one before ends, and a closed path ends at its first vertex.
/// Call inside guardKernelCall.
Result<TopoDS_Wire> makePathWire(const PlanarPath& path, bool closed) {
    const Frame3D& plane = path.plane;
    const gp_Dir normal = occt::toModel(plane.normal());
    const auto point = [&](const Point2D& p) { return occt::toModel(plane.toGlobal(p)); };
    BRepBuilderAPI_MakeWire wire;
    TopoDS_Vertex first;
    TopoDS_Vertex previous;
    for (std::size_t i = 0; i < path.segments.size(); ++i) {
        const ProfileSegment& segment = path.segments[i];
        if (const auto* circle = std::get_if<CircleSegment2D>(&segment)) {
            // Starts on the plane's X axis through the centre (see PlanarPath).
            const gp_Ax2 frame(point(circle->center), circle->counterClockwise ? normal : normal.Reversed(),
                               occt::toModel(plane.xAxis()));
            wire.Add(BRepBuilderAPI_MakeEdge(gp_Circ(frame, occt::toModel(circle->radius))).Edge());
            continue;
        }
        const TopoDS_Vertex start =
            i == 0 ? BRepBuilderAPI_MakeVertex(point(firstPoint(segment))).Vertex() : previous;
        if (i == 0) {
            first = start;
        }
        const bool last = i + 1 == path.segments.size();
        const TopoDS_Vertex end =
            last && closed ? first : BRepBuilderAPI_MakeVertex(point(lastPoint(segment))).Vertex();
        BRepBuilderAPI_MakeEdge edge = [&] {
            if (std::holds_alternative<LineSegment2D>(segment)) {
                return BRepBuilderAPI_MakeEdge(start, end);
            }
            const auto& arc = std::get<ArcSegment2D>(segment);
            const gp_Pnt center = point(arc.center);
            const gp_Ax2 frame(center, arc.counterClockwise ? normal : normal.Reversed(),
                               gp_Dir(gp_Vec(center, point(arc.start))));
            return BRepBuilderAPI_MakeEdge(gp_Circ(frame, occt::toModel(distance(arc.center, arc.start))), start, end);
        }();
        if (!edge.IsDone()) {
            return makeError(ErrorCode::Internal,
                             std::format("makeSweep: the kernel cannot make an edge of path segment {}", i + 1));
        }
        wire.Add(edge.Edge());
        if (!wire.IsDone()) {
            return makeError(ErrorCode::Internal, "makeSweep: the path's edges do not form a connected wire");
        }
        previous = end;
    }
    return wire.Wire();
}

/// Collects the names @p namer gives faces of @p shape; faces that are not
/// faces of the shape are ignored.
class FaceNaming {
public:
    FaceNaming(const TopoDS_Shape& shape, const SweptFaceNamer& namer) : namer_(namer) {
        if (namer_) {
            TopExp::MapShapes(shape, TopAbs_FACE, faces_);
        }
    }

    void name(const TopoDS_Shape& face, const SweptFace& origin) {
        if (!namer_ || face.IsNull() || face.ShapeType() != TopAbs_FACE || !faces_.Contains(face)) {
            return;
        }
        if (const auto given = namer_(origin)) {
            names_.push_back({TopoDS::Face(face), *given});
        }
    }

    [[nodiscard]] std::vector<occt::NamedFace> take(const TopoDS_Shape& shape) {
        return occt::canonicalNames(shape, std::move(names_));
    }

private:
    const SweptFaceNamer& namer_;
    occt::ShapeMap faces_;
    std::vector<occt::NamedFace> names_;
};

/// One profile loop swept along the path as a solid: the binormal stays the
/// path plane's normal (no twist), and corners are mitred (right corners).
/// With @p namer, the faces are named: the caps for the outer loop
/// (@p loop 0), and the side of each edge of @p edges along each of the
/// @p pathSegments segments. Call inside guardKernelCall.
Result<Body> sweepLoop(const TopoDS_Wire& spine, const gp_Dir& binormal, const TopoDS_Wire& wire,
                       const LoopEdges& edges, std::size_t loop, std::size_t pathSegments,
                       const SweptFaceNamer& namer) {
    BRepOffsetAPI_MakePipeShell pipe(spine);
    pipe.SetMode(binormal);
    pipe.SetTransitionMode(BRepBuilderAPI_RightCorner);
    pipe.Add(wire, /*WithContact=*/false, /*WithCorrection=*/false);
    pipe.Build();
    if (!pipe.IsDone()) {
        return makeError(ErrorCode::Internal, "makeSweep: the kernel could not sweep the profile along the path");
    }
    if (!pipe.MakeSolid()) {
        return makeError(ErrorCode::Internal, "makeSweep: the kernel could not close the swept profile into a solid");
    }
    const TopoDS_Shape shape = pipe.Shape();
    FaceNaming naming(shape, namer);
    if (namer) {
        if (loop == 0) {
            naming.name(pipe.FirstShape(), SweptFace{.kind = SweptFace::Kind::First});
            naming.name(pipe.LastShape(), SweptFace{.kind = SweptFace::Kind::Last});
        }
        // The faces an edge sweeps come in path order
        // (docs/verification/P12-SKETCH-003/kernel-probe).
        for (std::size_t segment = 0; segment < edges.size(); ++segment) {
            const occt::ShapeList& swept = pipe.Generated(edges[segment]);
            if (static_cast<std::size_t>(swept.Extent()) != pathSegments) {
                continue;
            }
            std::size_t along = 0;
            for (const TopoDS_Shape& side : swept) {
                naming.name(side, SweptFace{.kind = SweptFace::Kind::Side,
                                            .loop = loop,
                                            .segment = segment,
                                            .pathSegment = along++});
            }
        }
    }
    return occt::BodyAccess::makeBody(shape, naming.take(shape));
}

using detail::PlaneAxis;

/// A revolution axis in the region's plane coordinates (metres).
Result<PlaneAxis> axisInPlane(const Frame3D& plane, const Axis3D& axis) {
    // Directions are finite unit vectors by construction (Direction3D).
    if (!isFinite(axis.origin.x) || !isFinite(axis.origin.y) || !isFinite(axis.origin.z)) {
        return makeError(ErrorCode::InvalidArgument, "makeRevolution: the axis origin is not finite");
    }
    if (std::abs(axis.direction.dot(plane.normal())) > kAxisParallelTolerance) {
        return makeError(ErrorCode::InvalidArgument,
                         "makeRevolution: the axis is not parallel to the profile plane");
    }
    const Length offset = plane.signedDistance(axis.origin);
    if (!isFinite(offset) || abs(offset) > kClosureTolerance) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("makeRevolution: the axis does not lie in the profile plane ({} away)",
                                     toString(abs(offset), units::mm)));
    }
    const Point2D origin = plane.toLocal(axis.origin);
    const double dx = axis.direction.dot(plane.xAxis());
    const double dy = axis.direction.dot(plane.yAxis());
    const double length = std::hypot(dx, dy);
    return PlaneAxis{origin.x.si(), origin.y.si(), dx / length, dy / length};
}

} // namespace

Result<Body> makePrism(const PlanarRegion& region, Length from, Length to) {
    return makePrism(region, from, to, SweptFaceNamer{});
}

Result<Body> makePrism(const PlanarRegion& region, Length from, Length to, const SweptFaceNamer& namer) {
    if (!isFinite(from) || !isFinite(to) || occt::toModel(to - from) <= Precision::Confusion()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("prism extent must be finite with to > from, got [{}, {}]",
                                     toString(from, units::mm), toString(to, units::mm)));
    }
    if (auto valid = checkRegion(region); !valid) {
        return std::unexpected(valid.error());
    }

    return occt::guardKernelCall("makePrism", [&]() -> Result<Body> {
        std::vector<LoopEdges> edges;
        auto profile = makeProfileFace(region, occt::toModel(from), "makePrism", namer ? &edges : nullptr);
        if (!profile) {
            return std::unexpected(profile.error());
        }
        const gp_Vec sweep = gp_Vec(occt::toModel(region.plane.normal())) * occt::toModel(to - from);
        BRepPrimAPI_MakePrism prism(*profile, sweep);
        std::vector<occt::NamedFace> names;
        if (namer) {
            // The caps are the profile at both ends; each profile edge sweeps
            // one side face (docs/verification/P12-STREF-001/kernel-probe).
            const auto name = [&](const TopoDS_Shape& face, const SweptFace& origin) {
                if (face.IsNull() || face.ShapeType() != TopAbs_FACE) {
                    return;
                }
                if (const auto given = namer(origin)) {
                    names.push_back({TopoDS::Face(face), *given});
                }
            };
            name(prism.FirstShape(), SweptFace{.kind = SweptFace::Kind::First});
            name(prism.LastShape(), SweptFace{.kind = SweptFace::Kind::Last});
            for (std::size_t loop = 0; loop < edges.size(); ++loop) {
                for (std::size_t segment = 0; segment < edges[loop].size(); ++segment) {
                    for (const TopoDS_Shape& side : prism.Generated(edges[loop][segment])) {
                        name(side, SweptFace{.kind = SweptFace::Kind::Side, .loop = loop, .segment = segment});
                    }
                }
            }
        }
        std::vector<occt::NamedFace> canonical = occt::canonicalNames(prism.Shape(), std::move(names));
        Body body = occt::BodyAccess::makeBody(prism.Shape(), std::move(canonical));
        if (body.isEmpty() || !body.isValid()) {
            return makeError(ErrorCode::Internal, "makePrism: the kernel produced an invalid solid");
        }
        return body;
    });
}

Result<Body> makeRevolution(const PlanarRegion& region, const Axis3D& axis, Angle from, Angle to) {
    return makeRevolution(region, axis, from, to, SweptFaceNamer{});
}

Result<Body> makeRevolution(const PlanarRegion& region, const Axis3D& axis, Angle from, Angle to,
                            const SweptFaceNamer& namer) {
    const double sweep = (to - from).si();
    if (!isFinite(from) || !isFinite(to) || sweep <= Precision::Angular() || sweep > kTwoPi + kFullTurnTolerance) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("makeRevolution: the sweep must be in (0, 360] deg, got [{}, {}] deg",
                                     from.in(units::deg), to.in(units::deg)));
    }
    if (auto valid = checkRegion(region); !valid) {
        return std::unexpected(valid.error());
    }
    auto planeAxis = axisInPlane(region.plane, axis);
    if (!planeAxis) {
        return std::unexpected(planeAxis.error());
    }
    // The outer loop bounds the region, but checking the holes too costs nothing.
    double low = 0.0;
    double high = 0.0;
    detail::extendSideRange(region.outer, *planeAxis, low, high);
    for (const ProfileLoop& hole : region.holes) {
        detail::extendSideRange(hole, *planeAxis, low, high);
    }
    const double tolerance = kClosureTolerance.si();
    if (low < -tolerance && high > tolerance) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("makeRevolution: the profile crosses the revolution axis (it extends {} and "
                                     "{} to either side)",
                                     toString(Length::fromSi(-low), units::mm),
                                     toString(Length::fromSi(high), units::mm)));
    }

    return occt::guardKernelCall("makeRevolution", [&]() -> Result<Body> {
        std::vector<LoopEdges> edges;
        auto profile = makeProfileFace(region, 0.0, "makeRevolution", namer ? &edges : nullptr);
        if (!profile) {
            return std::unexpected(profile.error());
        }
        const gp_Ax1 kernelAxis(occt::toModel(axis.origin), occt::toModel(axis.direction));
        TopoDS_Shape start = *profile;
        if (from.si() != 0.0) {
            gp_Trsf rotation;
            rotation.SetRotation(kernelAxis, from.si());
            BRepBuilderAPI_Transform turn(*profile, rotation, /*copy=*/true);
            start = turn.Shape();
            for (LoopEdges& loop : edges) {
                for (TopoDS_Edge& edge : loop) {
                    edge = TopoDS::Edge(turn.ModifiedShape(edge));
                }
            }
        }
        const bool fullTurn = std::abs(sweep - kTwoPi) <= kFullTurnTolerance;
        BRepPrimAPI_MakeRevol revolution = fullTurn ? BRepPrimAPI_MakeRevol(start, kernelAxis)
                                                    : BRepPrimAPI_MakeRevol(start, kernelAxis, sweep);
        if (!revolution.IsDone()) {
            return makeError(ErrorCode::Internal, "makeRevolution: the kernel could not revolve the profile");
        }
        const TopoDS_Shape shape = revolution.Shape();
        FaceNaming naming(shape, namer);
        if (namer) {
            // A full turn's first and last shapes are the profile, which is
            // not a face of the result. The sweep's own Shape(edge) gives the
            // face each edge sweeps; Generated() misses planar ones on a full
            // turn (docs/verification/P12-SKETCH-003/kernel-probe).
            naming.name(revolution.FirstShape(), SweptFace{.kind = SweptFace::Kind::First});
            naming.name(revolution.LastShape(), SweptFace{.kind = SweptFace::Kind::Last});
            auto& sweeper = const_cast<BRepSweep_Revol&>(revolution.Revol());
            for (std::size_t loop = 0; loop < edges.size(); ++loop) {
                for (std::size_t segment = 0; segment < edges[loop].size(); ++segment) {
                    naming.name(sweeper.Shape(edges[loop][segment]),
                                SweptFace{.kind = SweptFace::Kind::Side, .loop = loop, .segment = segment});
                }
            }
        }
        Body body = occt::BodyAccess::makeBody(shape, naming.take(shape));
        if (body.isEmpty() || body.topology().solids == 0 || !body.isValid()) {
            return makeError(ErrorCode::Internal, "makeRevolution: the kernel produced an invalid solid");
        }
        // A valid solid can still be inside out or degenerate; require a
        // finite, positive volume.
        const auto properties = body.massProperties();
        if (!properties || !isFinite(properties->volume) || !(properties->volume > Volume{})) {
            return makeError(ErrorCode::Internal,
                             "makeRevolution: the kernel produced a solid without a finite positive volume");
        }
        return body;
    });
}

namespace {

/// A path of measured model-space segments as one connected wire, with a
/// shared vertex at every join and the first vertex again when it closes.
/// Call inside guardKernelCall.
Result<TopoDS_Wire> makeSpatialWire(const std::vector<detail::SpatialSegment>& segments, bool closed) {
    BRepBuilderAPI_MakeWire wire;
    TopoDS_Vertex first;
    TopoDS_Vertex previous;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const detail::SpatialSegment& segment = segments[i];
        if (segment.circle) {
            const gp_Ax2 frame(occt::toModel(segment.center), occt::toModel(segment.axis),
                               gp_Dir(gp_Vec(occt::toModel(segment.center), occt::toModel(segment.start))));
            wire.Add(BRepBuilderAPI_MakeEdge(gp_Circ(frame, occt::toModel(segment.radius))).Edge());
            continue;
        }
        const TopoDS_Vertex start =
            i == 0 ? BRepBuilderAPI_MakeVertex(occt::toModel(segment.start)).Vertex() : previous;
        if (i == 0) {
            first = start;
        }
        const bool last = i + 1 == segments.size();
        const TopoDS_Vertex end =
            last && closed ? first : BRepBuilderAPI_MakeVertex(occt::toModel(segment.end)).Vertex();
        BRepBuilderAPI_MakeEdge edge = [&] {
            if (segment.straight) {
                return BRepBuilderAPI_MakeEdge(start, end);
            }
            const gp_Pnt center = occt::toModel(segment.center);
            const gp_Ax2 frame(center, occt::toModel(segment.axis),
                               gp_Dir(gp_Vec(center, occt::toModel(segment.start))));
            return BRepBuilderAPI_MakeEdge(gp_Circ(frame, occt::toModel(segment.radius)), start, end);
        }();
        if (!edge.IsDone()) {
            return makeError(ErrorCode::Internal,
                             std::format("makeSweep: the kernel cannot make an edge of path segment {}", i + 1));
        }
        wire.Add(edge.Edge());
        if (!wire.IsDone()) {
            return makeError(ErrorCode::Internal, "makeSweep: the path's edges do not form a connected wire");
        }
        previous = end;
    }
    return wire.Wire();
}

/// How many samples the auxiliary spine of a twist is fitted through: more
/// for more turn, so that the fitted curve follows theta(u) = u theta_total
/// closely whatever the total is. Fixed by the twist alone, so the same
/// definition always gives the same curve.
std::size_t twistSamples(Angle twist) {
    constexpr double kQuarterTurn = std::numbers::pi / 2.0;
    const double quarters = std::ceil(std::abs(twist.si()) / kQuarterTurn);
    const double samples = 64.0 + 64.0 * quarters;
    return static_cast<std::size_t>(std::min(samples, 512.0));
}

/// The auxiliary spine of a twist: the path offset by @p radius in the
/// direction that BetterCAD's own rotation-minimizing frame gives, turned by
/// theta(u) = u * twist about the tangent. The frame starts on @p start, the
/// profile plane's X axis, so the twist is measured from the profile's own
/// X axis. Call inside guardKernelCall.
Result<TopoDS_Wire> makeTwistGuide(const detail::SweptPlan& plan, Angle twist, const Direction3D& start,
                                   Length radius) {
    const std::size_t count = twistSamples(twist);
    const std::vector<detail::PathSample> samples = detail::samplePath(plan.segments, start, count);
    if (samples.size() < 2) {
        return makeError(ErrorCode::Internal, "makeSweep: the path cannot be sampled for its twist");
    }
    NCollection_Array1<gp_Pnt> points(1, static_cast<int>(samples.size()));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const detail::PathSample& sample = samples[i];
        const auto across = sample.tangent.cross(sample.reference);
        if (!across) {
            return makeError(ErrorCode::Internal, "makeSweep: the path's frame is degenerate");
        }
        const double theta = sample.u * twist.si();
        const Translation3D offset =
            Translation3D::along(sample.reference, radius * std::cos(theta)) +
            Translation3D::along(*across, radius * std::sin(theta));
        points.SetValue(static_cast<int>(i + 1), occt::toModel(sample.point + offset));
    }
    GeomAPI_PointsToBSpline fit(points);
    if (fit.Curve().IsNull()) {
        return makeError(ErrorCode::Internal, "makeSweep: the kernel cannot fit the twist's guide curve");
    }
    return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(fit.Curve()).Edge()).Wire();
}

/// One profile loop swept along a spatial, twisted or guided path. The frame
/// is the one @p plan records: a guide carries the section where a guide or
/// a twist is given, and the kernel's corrected Frenet frame otherwise,
/// which on a planar path gives the same solid as the fixed binormal
/// (kernel-probe case G). Call inside guardKernelCall.
Result<Body> sweepLoopAlong(const TopoDS_Wire& spine, const TopoDS_Wire* guide, const TopoDS_Wire& wire,
                            const LoopEdges& edges, std::size_t loop, std::size_t pathSegments,
                            const SweptFaceNamer& namer) {
    BRepOffsetAPI_MakePipeShell pipe(spine);
    if (guide != nullptr) {
        // NoContact without curvilinear equivalence: the two the probe
        // measured as building a valid solid that keeps the profile's area
        // (cases D and E). The other combinations fail or scale the section.
        pipe.SetMode(*guide, false, BRepFill_NoContact);
    } else {
        pipe.SetMode(false); // corrected Frenet
    }
    pipe.SetTransitionMode(BRepBuilderAPI_RightCorner);
    pipe.Add(wire, /*WithContact=*/false, /*WithCorrection=*/false);
    pipe.Build();
    if (!pipe.IsDone()) {
        return makeError(ErrorCode::Internal, "makeSweep: the kernel could not sweep the profile along the path");
    }
    if (!pipe.MakeSolid()) {
        return makeError(ErrorCode::Internal, "makeSweep: the kernel could not close the swept profile into a solid");
    }
    const TopoDS_Shape shape = pipe.Shape();
    FaceNaming naming(shape, namer);
    if (namer) {
        if (loop == 0) {
            naming.name(pipe.FirstShape(), SweptFace{.kind = SweptFace::Kind::First});
            naming.name(pipe.LastShape(), SweptFace{.kind = SweptFace::Kind::Last});
        }
        for (std::size_t segment = 0; segment < edges.size(); ++segment) {
            const occt::ShapeList& swept = pipe.Generated(edges[segment]);
            if (static_cast<std::size_t>(swept.Extent()) != pathSegments) {
                continue;
            }
            std::size_t along = 0;
            for (occt::ShapeList::Iterator it(swept); it.More(); it.Next()) {
                naming.name(it.Value(), SweptFace{.kind = SweptFace::Kind::Side,
                                                  .loop = loop,
                                                  .segment = segment,
                                                  .pathSegment = along++});
            }
        }
    }
    return occt::BodyAccess::makeBody(shape, naming.take(shape));
}

} // namespace

Result<Body> makeSweep(const PlanarRegion& region, const SweptPath& path) {
    return makeSweep(region, path, SweptFaceNamer{});
}

Result<Body> makeSweep(const PlanarRegion& region, const SweptPath& path, const SweptFaceNamer& namer) {
    // One planar run with neither a twist nor a guide is P11-FEAT-008's
    // sweep, built exactly as it was: the same code, the same solid.
    if (path.runs.size() == 1 && path.twist == Angle{} && path.guide.empty()) {
        return makeSweep(region, path.runs.front(), namer);
    }
    if (auto valid = checkRegion(region); !valid) {
        return std::unexpected(valid.error());
    }
    auto plan = detail::planSweptPath(region, path);
    if (!plan) {
        return std::unexpected(plan.error());
    }
    const bool guided = plan->frame == SweepFrame::Guided;
    std::optional<detail::SweptPlan> guidePlan;
    if (!path.guide.empty()) {
        // The guide is checked as a curve: connected, non-degenerate and
        // finite. Nothing sits on it -- it carries the section rather than
        // supporting it -- so it has no profile and no placement rule.
        SweptPath asPath;
        asPath.runs = path.guide;
        auto planned = detail::planPathCurve(asPath, "the guide ");
        if (!planned) {
            return std::unexpected(planned.error());
        }
        guidePlan = std::move(*planned);
    }

    auto swept = occt::guardKernelCall("makeSweep", [&]() -> Result<Body> {
        if (auto face = makeProfileFace(region, 0.0, "makeSweep"); !face) {
            return std::unexpected(face.error());
        }
        auto spine = makeSpatialWire(plan->segments, plan->closed);
        if (!spine) {
            return std::unexpected(spine.error());
        }
        std::optional<TopoDS_Wire> guide;
        if (guidePlan) {
            auto wire = makeSpatialWire(guidePlan->segments, guidePlan->closed);
            if (!wire) {
                return std::unexpected(wire.error());
            }
            guide = *wire;
        } else if (path.twist != Angle{}) {
            auto wire = makeTwistGuide(*plan, path.twist, region.plane.xAxis(), plan->reach);
            if (!wire) {
                return std::unexpected(wire.error());
            }
            guide = *wire;
        }
        WireBuilder loops(region.plane, 0.0);
        const auto sweepOf = [&](const ProfileLoop& loop, std::size_t index) -> Result<Body> {
            const bool reverse = signedArea(loop) < Area{};
            LoopEdges edges;
            auto wire = loops.build(reverse ? reversed(loop) : loop, &edges);
            if (!wire) {
                return std::unexpected(wire.error());
            }
            if (reverse) {
                std::ranges::reverse(edges);
            }
            return sweepLoopAlong(*spine, guide ? &*guide : nullptr, *wire, edges, index, plan->segments.size(),
                                  namer);
        };
        auto body = sweepOf(region.outer, 0);
        for (std::size_t i = 0; body && i < region.holes.size(); ++i) {
            auto hole = sweepOf(region.holes[i], i + 1);
            body = hole ? booleanDifference(*body, *hole) : hole;
        }
        return body;
    });
    if (!swept) {
        return std::unexpected(swept.error());
    }
    const Body& body = *swept;
    if (body.isEmpty() || body.topology().solids != 1 || !body.isValid()) {
        return makeError(ErrorCode::Internal, "makeSweep: the kernel produced an invalid solid");
    }
    auto intersects = occt::guardKernelCall("makeSweep", [&]() -> Result<bool> {
        BRepAlgoAPI_Check check(*occt::BodyAccess::shape(body), /*bTestSE=*/false, /*bTestSI=*/true);
        return !check.IsValid();
    });
    if (!intersects) {
        return std::unexpected(intersects.error());
    }
    if (*intersects) {
        return makeError(ErrorCode::InvalidArgument,
                         "makeSweep: the swept solid would intersect itself: the path comes back within the profile's "
                         "reach of itself");
    }
    const auto properties = body.massProperties();
    if (!properties || !isFinite(properties->volume) || !(properties->volume > Volume{})) {
        return makeError(ErrorCode::Internal,
                         "makeSweep: the kernel produced a solid without a finite positive volume");
    }
    // Pappus, as for a planar path: the centroid rides on the path, so the
    // volume is the region's area times the path's length however the
    // section turns. A guided sweep follows a curve fitted through samples,
    // so it meets that to 1e-5 rather than the 1e-9 an exact frame gives
    // (measured: kernel-probe cases D, E and H, worst 1.9e-6).
    const double tolerance = guided ? 1e-5 : 1e-9;
    const double actual = properties->volume.in(units::mm3);
    const double expected = plan->expectedVolume.in(units::mm3);
    if (std::abs(actual - expected) > tolerance * expected) {
        return makeError(ErrorCode::Internal,
                         std::format("makeSweep: the kernel's solid encloses {:.12g} mm^3, but the profile's area "
                                     "times the length of its centroid's path is {:.12g} mm^3",
                                     actual, expected));
    }
    return body;
}

Result<Body> makeSweep(const PlanarRegion& region, const PlanarPath& path) {
    return makeSweep(region, path, SweptFaceNamer{});
}

Result<Body> makeSweep(const PlanarRegion& region, const PlanarPath& path, const SweptFaceNamer& namer) {
    if (auto valid = checkRegion(region); !valid) {
        return std::unexpected(valid.error());
    }
    auto plan = detail::planSweep(region, path);
    if (!plan) {
        return std::unexpected(plan.error());
    }

    auto swept = occt::guardKernelCall("makeSweep", [&]() -> Result<Body> {
        // The loops must bound a valid face (no self-intersecting or
        // overlapping loops), exactly as for prisms and revolutions.
        if (auto face = makeProfileFace(region, 0.0, "makeSweep"); !face) {
            return std::unexpected(face.error());
        }
        auto spine = makePathWire(path, plan->closed);
        if (!spine) {
            return std::unexpected(spine.error());
        }
        const gp_Dir binormal = occt::toModel(path.plane.normal());
        // Each loop is swept on its own; the holes' solids are subtracted.
        WireBuilder loops(region.plane, 0.0);
        const auto sweepOf = [&](const ProfileLoop& loop, std::size_t index) -> Result<Body> {
            const bool reverse = signedArea(loop) < Area{};
            LoopEdges edges;
            auto wire = loops.build(reverse ? reversed(loop) : loop, &edges);
            if (!wire) {
                return std::unexpected(wire.error());
            }
            if (reverse) {
                std::ranges::reverse(edges);
            }
            return sweepLoop(*spine, binormal, *wire, edges, index, path.segments.size(), namer);
        };
        auto body = sweepOf(region.outer, 0);
        for (std::size_t i = 0; body && i < region.holes.size(); ++i) {
            auto hole = sweepOf(region.holes[i], i + 1);
            body = hole ? booleanDifference(*body, *hole) : hole;
        }
        return body;
    });
    if (!swept) {
        return std::unexpected(swept.error());
    }
    const Body& body = *swept;
    if (body.isEmpty() || body.topology().solids != 1 || !body.isValid()) {
        return makeError(ErrorCode::Internal, "makeSweep: the kernel produced an invalid solid");
    }
    // A swept solid can pass through itself where the path comes back near
    // itself: the kernel's validity check does not see that, its
    // self-interference check does.
    auto intersects = occt::guardKernelCall("makeSweep", [&]() -> Result<bool> {
        BRepAlgoAPI_Check check(*occt::BodyAccess::shape(body), /*bTestSE=*/false, /*bTestSI=*/true);
        return !check.IsValid();
    });
    if (!intersects) {
        return std::unexpected(intersects.error());
    }
    if (*intersects) {
        return makeError(ErrorCode::InvalidArgument,
                         "makeSweep: the swept solid would intersect itself: the path comes back within the profile's "
                         "reach of itself");
    }
    // Independent check of the kernel's result (Pappus): the volume is the
    // region's area times the length of the path its centroid travels. The
    // kernel's volumes agree to rounding along smooth paths and to a few
    // 1e-12 where it trims mitred corners numerically (at its 1e-7 mm
    // precision); 1e-9 tells that from a wrongly built solid.
    const auto properties = body.massProperties();
    if (!properties || !isFinite(properties->volume) || !(properties->volume > Volume{})) {
        return makeError(ErrorCode::Internal,
                         "makeSweep: the kernel produced a solid without a finite positive volume");
    }
    const double actual = properties->volume.in(units::mm3);
    const double expected = plan->expectedVolume.in(units::mm3);
    if (std::abs(actual - expected) > 1e-9 * expected) {
        return makeError(ErrorCode::Internal,
                         std::format("makeSweep: the kernel's solid encloses {:.12g} mm^3, but the profile's area "
                                     "times the length of its centroid's path is {:.12g} mm^3",
                                     actual, expected));
    }
    return body;
}

Result<Body> makeLoft(std::span<const PlanarRegion> sections) {
    return makeLoft(sections, SweptFaceNamer{});
}

Result<Body> makeLoft(std::span<const PlanarRegion> sections, const SweptFaceNamer& namer) {
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (auto valid = checkRegion(sections[i]); !valid) {
            return makeError(valid.error().code, std::format("makeLoft: section {}: {}", i + 1, valid.error().message));
        }
    }
    auto plan = detail::planLoft(sections);
    if (!plan) {
        return std::unexpected(plan.error());
    }

    auto lofted = occt::guardKernelCall("makeLoft", [&]() -> Result<Body> {
        // Ruled: straight lines join matching points of consecutive sections.
        BRepOffsetAPI_ThruSections loft(/*isSolid=*/true, /*ruled=*/true);
        // BetterCAD matched the sections (planLoft()); the kernel must keep
        // that matching rather than re-match them itself.
        loft.CheckCompatibility(false);
        for (std::size_t i = 0; i < plan->sections.size(); ++i) {
            const PlanarRegion& section = plan->sections[i];
            // The loop must bound a valid face, as for prisms and sweeps.
            if (auto face = makeProfileFace(section, 0.0, std::format("makeLoft: section {}", i + 1)); !face) {
                return std::unexpected(face.error());
            }
            WireBuilder builder(section.plane, 0.0, /*seamOnXAxis=*/true);
            auto wire = builder.build(section.outer);
            if (!wire) {
                return std::unexpected(wire.error());
            }
            loft.AddWire(*wire);
        }
        loft.Build();
        if (!loft.IsDone()) {
            return makeError(ErrorCode::Internal, "makeLoft: the kernel could not loft the sections");
        }
        const TopoDS_Shape shape = loft.Shape();
        FaceNaming naming(shape, namer);
        naming.name(loft.FirstShape(), SweptFace{.kind = SweptFace::Kind::First});
        naming.name(loft.LastShape(), SweptFace{.kind = SweptFace::Kind::Last});
        return occt::BodyAccess::makeBody(shape, naming.take(shape));
    });
    if (!lofted) {
        return std::unexpected(lofted.error());
    }
    const Body& body = *lofted;
    if (body.isEmpty() || body.topology().solids != 1 || !body.isValid()) {
        return makeError(ErrorCode::Internal, "makeLoft: the kernel produced an invalid solid");
    }
    auto intersects = occt::guardKernelCall("makeLoft", [&]() -> Result<bool> {
        BRepAlgoAPI_Check check(*occt::BodyAccess::shape(body), /*bTestSE=*/false, /*bTestSI=*/true);
        return !check.IsValid();
    });
    if (!intersects) {
        return std::unexpected(intersects.error());
    }
    if (*intersects) {
        return makeError(ErrorCode::InvalidArgument,
                         "makeLoft: the lofted solid would intersect itself: its sides pass through one another "
                         "between the sections");
    }
    // Independent check of the kernel's result (prismatoid formula, see
    // planLoft()). The kernel's volumes agree to rounding between lines and
    // between coaxial circles and arcs, and within 6.3e-10 between arcs or
    // circles that are not coaxial or are turned against each other (its
    // B-spline ruled faces; measured, see docs/verification/P11-FEAT-009). A
    // wrongly matched loft is off by more than 1e-1 in every case probed;
    // 1e-8 keeps both apart.
    const auto properties = body.massProperties();
    if (!properties || !isFinite(properties->volume) || !(properties->volume > Volume{})) {
        return makeError(ErrorCode::Internal, "makeLoft: the kernel produced a solid without a finite positive volume");
    }
    const double actual = properties->volume.in(units::mm3);
    const double expected = plan->expectedVolume.in(units::mm3);
    if (std::abs(actual - expected) > 1e-8 * expected) {
        return makeError(ErrorCode::Internal,
                         std::format("makeLoft: the kernel's solid encloses {:.12g} mm^3, but the sections' "
                                     "prismatoid volume is {:.12g} mm^3",
                                     actual, expected));
    }
    return body;
}

} // namespace bettercad::geometry
