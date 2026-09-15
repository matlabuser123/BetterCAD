#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Format.hpp>

#include "core/geometry/ProfileExtent.hpp"
#include "core/geometry/SweepPlan.hpp"
#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <bettercad/core/geometry/Booleans.hpp>

#include <BRepAlgoAPI_Check.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_TransitionMode.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
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

Point2D segmentStart(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return line->start;
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        return arc->start;
    }
    return {};
}

Point2D segmentEnd(const ProfileSegment& segment) {
    if (const auto* line = std::get_if<LineSegment2D>(&segment)) {
        return line->end;
    }
    if (const auto* arc = std::get_if<ArcSegment2D>(&segment)) {
        return arc->end;
    }
    return {};
}

Result<void> checkLoop(const ProfileLoop& loop, std::string_view name) {
    if (loop.segments.empty()) {
        return makeError(ErrorCode::InvalidArgument, std::format("{} loop is empty", name));
    }
    const bool hasCircle = std::ranges::any_of(loop.segments, [](const ProfileSegment& s) {
        return std::holds_alternative<CircleSegment2D>(s);
    });
    if (hasCircle) {
        if (loop.segments.size() != 1) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} loop mixes a full circle with other segments", name));
        }
        const auto& circle = std::get<CircleSegment2D>(loop.segments.front());
        if (!isFinite(circle.radius) || circle.radius <= kClosureTolerance) {
            return makeError(ErrorCode::InvalidArgument, std::format("{} loop has an invalid circle", name));
        }
        return {};
    }
    for (std::size_t i = 0; i < loop.segments.size(); ++i) {
        const ProfileSegment& segment = loop.segments[i];
        const ProfileSegment& next = loop.segments[(i + 1) % loop.segments.size()];
        if (distance(segmentStart(segment), segmentEnd(segment)) <= kClosureTolerance) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{} loop has a degenerate segment", name));
        }
        if (distance(segmentEnd(segment), segmentStart(next)) > kClosureTolerance) {
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
class WireBuilder {
public:
    WireBuilder(const Frame3D& plane, double offset) : plane_(plane), offset_(offset),
        normal_(occt::toModel(plane.normal())) {}

    Result<TopoDS_Wire> build(const ProfileLoop& loop) {
        BRepBuilderAPI_MakeWire wire;
        for (const ProfileSegment& segment : loop.segments) {
            const TopoDS_Edge edge = makeEdge(segment);
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
        return gp_Circ(gp_Ax2(point(center), axis), radius);
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
        const auto& full = std::get<CircleSegment2D>(segment);
        return BRepBuilderAPI_MakeEdge(circle(full.center, occt::toModel(full.radius), full.counterClockwise));
    }

    Frame3D plane_;
    double offset_;
    gp_Dir normal_;
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

/// Planar face of a checked region, moved along the plane normal by
/// @p offset (model units). Loop orientation is normalized: outer
/// counter-clockwise, holes clockwise. Call inside guardKernelCall.
Result<TopoDS_Face> makeProfileFace(const PlanarRegion& region, double offset, std::string_view operation) {
    const ProfileLoop outer = signedArea(region.outer) < Area{} ? reversed(region.outer) : region.outer;
    WireBuilder builder(region.plane, offset);
    auto outerWire = builder.build(outer);
    if (!outerWire) {
        return std::unexpected(outerWire.error());
    }
    const gp_Pln plane(gp_Ax3(builder.point(Point2D{}), builder.normal(), occt::toModel(region.plane.xAxis())));
    BRepBuilderAPI_MakeFace face(plane, *outerWire, /*Inside=*/true);
    if (!face.IsDone()) {
        return makeError(ErrorCode::Internal, std::format("{}: cannot build a face from the outer loop", operation));
    }
    for (const ProfileLoop& hole : region.holes) {
        auto holeWire = builder.build(signedArea(hole) > Area{} ? reversed(hole) : hole);
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
            i == 0 ? BRepBuilderAPI_MakeVertex(point(segmentStart(segment))).Vertex() : previous;
        if (i == 0) {
            first = start;
        }
        const bool last = i + 1 == path.segments.size();
        const TopoDS_Vertex end =
            last && closed ? first : BRepBuilderAPI_MakeVertex(point(segmentEnd(segment))).Vertex();
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

/// One profile loop swept along the path as a solid: the binormal stays the
/// path plane's normal (no twist), and corners are mitred (right corners).
/// Call inside guardKernelCall.
Result<TopoDS_Shape> sweepLoop(const TopoDS_Wire& spine, const gp_Dir& binormal, const TopoDS_Wire& loop) {
    BRepOffsetAPI_MakePipeShell pipe(spine);
    pipe.SetMode(binormal);
    pipe.SetTransitionMode(BRepBuilderAPI_RightCorner);
    pipe.Add(loop, /*WithContact=*/false, /*WithCorrection=*/false);
    pipe.Build();
    if (!pipe.IsDone()) {
        return makeError(ErrorCode::Internal, "makeSweep: the kernel could not sweep the profile along the path");
    }
    if (!pipe.MakeSolid()) {
        return makeError(ErrorCode::Internal, "makeSweep: the kernel could not close the swept profile into a solid");
    }
    return pipe.Shape();
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
    if (!isFinite(from) || !isFinite(to) || occt::toModel(to - from) <= Precision::Confusion()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("prism extent must be finite with to > from, got [{}, {}]",
                                     toString(from, units::mm), toString(to, units::mm)));
    }
    if (auto valid = checkRegion(region); !valid) {
        return std::unexpected(valid.error());
    }

    return occt::guardKernelCall("makePrism", [&]() -> Result<Body> {
        auto profile = makeProfileFace(region, occt::toModel(from), "makePrism");
        if (!profile) {
            return std::unexpected(profile.error());
        }
        const gp_Vec sweep = gp_Vec(occt::toModel(region.plane.normal())) * occt::toModel(to - from);
        BRepPrimAPI_MakePrism prism(*profile, sweep);
        Body body = occt::BodyAccess::makeBody(prism.Shape());
        if (body.isEmpty() || !body.isValid()) {
            return makeError(ErrorCode::Internal, "makePrism: the kernel produced an invalid solid");
        }
        return body;
    });
}

Result<Body> makeRevolution(const PlanarRegion& region, const Axis3D& axis, Angle from, Angle to) {
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
        auto profile = makeProfileFace(region, 0.0, "makeRevolution");
        if (!profile) {
            return std::unexpected(profile.error());
        }
        const gp_Ax1 kernelAxis(occt::toModel(axis.origin), occt::toModel(axis.direction));
        TopoDS_Shape start = *profile;
        if (from.si() != 0.0) {
            gp_Trsf rotation;
            rotation.SetRotation(kernelAxis, from.si());
            start = BRepBuilderAPI_Transform(*profile, rotation, /*copy=*/true).Shape();
        }
        const bool fullTurn = std::abs(sweep - kTwoPi) <= kFullTurnTolerance;
        BRepPrimAPI_MakeRevol revolution = fullTurn ? BRepPrimAPI_MakeRevol(start, kernelAxis)
                                                    : BRepPrimAPI_MakeRevol(start, kernelAxis, sweep);
        if (!revolution.IsDone()) {
            return makeError(ErrorCode::Internal, "makeRevolution: the kernel could not revolve the profile");
        }
        Body body = occt::BodyAccess::makeBody(revolution.Shape());
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

Result<Body> makeSweep(const PlanarRegion& region, const PlanarPath& path) {
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
        const auto sweepOf = [&](const ProfileLoop& loop) -> Result<Body> {
            auto wire = loops.build(signedArea(loop) < Area{} ? reversed(loop) : loop);
            if (!wire) {
                return std::unexpected(wire.error());
            }
            auto solid = sweepLoop(*spine, binormal, *wire);
            if (!solid) {
                return std::unexpected(solid.error());
            }
            return occt::BodyAccess::makeBody(*solid);
        };
        auto body = sweepOf(region.outer);
        for (std::size_t i = 0; body && i < region.holes.size(); ++i) {
            auto hole = sweepOf(region.holes[i]);
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

} // namespace bettercad::geometry
