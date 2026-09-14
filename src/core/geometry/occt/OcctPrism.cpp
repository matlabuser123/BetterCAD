#include <bettercad/core/geometry/Sweeps.hpp>
#include <bettercad/core/units/Format.hpp>

#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <format>
#include <map>
#include <utility>

namespace bettercad::geometry {

namespace {

// Sketch tolerance: loop ends must meet within this.
constexpr Length kClosureTolerance = Length::fromSi(1e-10);

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

} // namespace

Result<Body> makePrism(const PlanarRegion& region, Length from, Length to) {
    if (!isFinite(from) || !isFinite(to) || occt::toModel(to - from) <= Precision::Confusion()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("prism extent must be finite with to > from, got [{}, {}]",
                                     toString(from, units::mm), toString(to, units::mm)));
    }
    if (auto valid = checkLoop(region.outer, "outer"); !valid) {
        return std::unexpected(valid.error());
    }
    for (const ProfileLoop& hole : region.holes) {
        if (auto valid = checkLoop(hole, "hole"); !valid) {
            return std::unexpected(valid.error());
        }
    }
    // Normalize orientation: outer counter-clockwise, holes clockwise.
    const ProfileLoop outer = signedArea(region.outer) < Area{} ? reversed(region.outer) : region.outer;
    std::vector<ProfileLoop> holes;
    for (const ProfileLoop& hole : region.holes) {
        holes.push_back(signedArea(hole) > Area{} ? reversed(hole) : hole);
    }

    return occt::guardKernelCall("makePrism", [&]() -> Result<Body> {
        WireBuilder builder(region.plane, occt::toModel(from));
        auto outerWire = builder.build(outer);
        if (!outerWire) {
            return std::unexpected(outerWire.error());
        }
        const gp_Pln plane(gp_Ax3(builder.point(Point2D{}), builder.normal(),
                                  occt::toModel(region.plane.xAxis())));
        BRepBuilderAPI_MakeFace face(plane, *outerWire, /*Inside=*/true);
        if (!face.IsDone()) {
            return makeError(ErrorCode::Internal, "makePrism: cannot build a face from the outer loop");
        }
        for (const ProfileLoop& hole : holes) {
            auto holeWire = builder.build(hole);
            if (!holeWire) {
                return std::unexpected(holeWire.error());
            }
            face.Add(*holeWire);
        }
        const TopoDS_Face profile = face.Face();
        if (!BRepCheck_Analyzer(profile).IsValid()) {
            return makeError(ErrorCode::Internal,
                             "makePrism: the profile face is invalid (self-intersecting or overlapping loops?)");
        }
        const gp_Vec sweep = gp_Vec(builder.normal()) * occt::toModel(to - from);
        BRepPrimAPI_MakePrism prism(profile, sweep);
        Body body = occt::BodyAccess::makeBody(prism.Shape());
        if (body.isEmpty() || !body.isValid()) {
            return makeError(ErrorCode::Internal, "makePrism: the kernel produced an invalid solid");
        }
        return body;
    });
}

} // namespace bettercad::geometry
