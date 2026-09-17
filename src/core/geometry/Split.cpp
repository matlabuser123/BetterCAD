#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Split.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <limits>

namespace bettercad::geometry {

namespace {

// The split box reaches this far beyond the body's bounds.
constexpr double kClearanceSi = 1e-3;
// A body no further than this from the plane on one side lies on it (the
// kernel's confusion tolerance, 1e-7 mm).
constexpr double kOnPlaneSi = 1e-10;

/// The body's bounds in the plane's coordinates (u along X, v along Y, w
/// along the normal): the extremes of its bounding box's corners.
struct PlaneExtent {
    double uMin = std::numeric_limits<double>::infinity();
    double uMax = -std::numeric_limits<double>::infinity();
    double vMin = std::numeric_limits<double>::infinity();
    double vMax = -std::numeric_limits<double>::infinity();
    double wMin = std::numeric_limits<double>::infinity();
    double wMax = -std::numeric_limits<double>::infinity();
};

PlaneExtent extentOf(const BoundingBox3D& box, const Frame3D& plane) {
    PlaneExtent extent;
    const Point3D& origin = plane.origin();
    for (int corner = 0; corner < 8; ++corner) {
        const double x = ((corner & 1) != 0 ? box.max.x : box.min.x).si() - origin.x.si();
        const double y = ((corner & 2) != 0 ? box.max.y : box.min.y).si() - origin.y.si();
        const double z = ((corner & 4) != 0 ? box.max.z : box.min.z).si() - origin.z.si();
        const auto along = [&](const Direction3D& d) { return x * d.x() + y * d.y() + z * d.z(); };
        const double u = along(plane.xAxis());
        const double v = along(plane.yAxis());
        const double w = along(plane.normal());
        extent.uMin = std::min(extent.uMin, u);
        extent.uMax = std::max(extent.uMax, u);
        extent.vMin = std::min(extent.vMin, v);
        extent.vMax = std::max(extent.vMax, v);
        extent.wMin = std::min(extent.wMin, w);
        extent.wMax = std::max(extent.wMax, w);
    }
    return extent;
}

Point2D at(double u, double v) {
    return Point2D{Length::fromSi(u), Length::fromSi(v)};
}

} // namespace

std::string_view toString(SplitKeep keep) noexcept {
    switch (keep) {
    case SplitKeep::Front:
        return "front";
    case SplitKeep::Back:
        return "back";
    case SplitKeep::Both:
        return "both";
    }
    return "unknown";
}

Result<Body> splitBody(const Body& body, const Frame3D& plane, SplitKeep keep) {
    if (body.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument, "split: the body is empty");
    }
    if (keep != SplitKeep::Front && keep != SplitKeep::Back && keep != SplitKeep::Both) {
        return makeError(ErrorCode::InvalidArgument, "split: unknown side to keep");
    }
    auto box = body.boundingBox();
    if (!box) {
        return makeError(box.error().code, std::format("split: {}", box.error().message));
    }
    const PlaneExtent extent = extentOf(*box, plane);
    if (!(extent.wMax > kOnPlaneSi)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "split: the plane does not cross the body: all of it lies behind the plane");
    }
    if (!(extent.wMin < -kOnPlaneSi)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "split: the plane does not cross the body: all of it lies in front of the plane");
    }

    // A box on the plane's front side, enclosing the body's bounds.
    const double c = kClearanceSi;
    const double u0 = extent.uMin - c;
    const double u1 = extent.uMax + c;
    const double v0 = extent.vMin - c;
    const double v1 = extent.vMax + c;
    ProfileLoop outline;
    outline.segments = {LineSegment2D{at(u0, v0), at(u1, v0)}, LineSegment2D{at(u1, v0), at(u1, v1)},
                        LineSegment2D{at(u1, v1), at(u0, v1)}, LineSegment2D{at(u0, v1), at(u0, v0)}};
    const PlanarRegion region{.plane = plane, .outer = std::move(outline), .holes = {}};
    auto tool = makePrism(region, Length{}, Length::fromSi(extent.wMax + c));
    if (!tool) {
        return makeError(tool.error().code, std::format("split: {}", tool.error().message));
    }

    auto front = booleanIntersection(body, *tool);
    if (!front) {
        return makeError(front.error().code, std::format("split: {}", front.error().message));
    }
    auto back = booleanDifference(body, *tool);
    if (!back) {
        return makeError(back.error().code, std::format("split: {}", back.error().message));
    }
    // The bounds cross the plane; the body itself need not.
    if (front->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "split: the plane does not cross the body: nothing of it lies in front of the plane");
    }
    if (back->isEmpty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "split: the plane does not cross the body: nothing of it lies behind the plane");
    }
    switch (keep) {
    case SplitKeep::Front:
        return front;
    case SplitKeep::Back:
        return back;
    case SplitKeep::Both:
        break;
    }
    const std::array<Body, 2> parts{*front, *back};
    return gatherSolids(parts);
}

} // namespace bettercad::geometry
