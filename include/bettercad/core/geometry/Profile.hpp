#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/BSpline.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>

#include <variant>
#include <vector>

// Planar profiles: closed loops of line, arc, circle, ellipse and B-spline
// segments in the local coordinates of a plane. Features build them from
// sketches; the geometry kernel turns them into faces and solids.
namespace bettercad::geometry {

struct LineSegment2D {
    Point2D start{};
    Point2D end{};

    friend constexpr bool operator==(const LineSegment2D&, const LineSegment2D&) = default;
};

/// Arc around @p center from @p start to @p end; both ends lie on the circle.
struct ArcSegment2D {
    Point2D center{};
    Point2D start{};
    Point2D end{};
    bool counterClockwise = true;

    friend constexpr bool operator==(const ArcSegment2D&, const ArcSegment2D&) = default;
};

/// A full circle; it forms a loop on its own.
struct CircleSegment2D {
    Point2D center{};
    Length radius{};
    bool counterClockwise = true;

    friend constexpr bool operator==(const CircleSegment2D&, const CircleSegment2D&) = default;
};

/// A full ellipse; it forms a loop on its own. Its first semi-axis runs from
/// the centre to `xVertex`, of length radiusX = |xVertex - center|; its
/// second semi-axis, radiusY long, is 90° counter-clockwise from the first.
/// Either may be the longer one. The ellipse starts (for pointAt()) at
/// xVertex.
struct EllipseSegment2D {
    Point2D center{};
    Point2D xVertex{};
    Length radiusY{};
    bool counterClockwise = true;

    friend constexpr bool operator==(const EllipseSegment2D&, const EllipseSegment2D&) = default;
};

/// A non-rational B-spline curve with uniform knots (UniformBSpline), of
/// degree kMinBSplineDegree to kMaxBSplineDegree:
/// - open (`periodic == false`): clamped, so it starts at the first pole and
///   ends at the last, where it is joined to other segments; it needs at
///   least degree + 1 poles;
/// - periodic: a closed curve, smooth everywhere, that forms a loop on its
///   own; it needs at least degree + 1 poles.
///
/// The curve lies in the convex hull of its poles. Reversing the pole order
/// reverses the curve.
struct SplineSegment2D {
    std::vector<Point2D> poles{};
    int degree = 3;
    bool periodic = false;

    friend bool operator==(const SplineSegment2D&, const SplineSegment2D&) = default;
};

using ProfileSegment =
    std::variant<LineSegment2D, ArcSegment2D, CircleSegment2D, EllipseSegment2D, SplineSegment2D>;

/// A closed loop: segments head to tail, the last ending where the first
/// starts. A closed segment (a circle, an ellipse or a periodic spline) is a
/// loop on its own.
struct ProfileLoop {
    std::vector<ProfileSegment> segments{};

    friend bool operator==(const ProfileLoop&, const ProfileLoop&) = default;
};

/// A connected planar region: an outer boundary and zero or more holes,
/// in the local coordinates of @p plane.
struct PlanarRegion {
    Frame3D plane = Frame3D::xy();
    ProfileLoop outer{};
    std::vector<ProfileLoop> holes{};

    friend bool operator==(const PlanarRegion&, const PlanarRegion&) = default;
};

/// Checks a spline from any source: a degree in range, enough poles, finite
/// coordinates, and a control polygon of non-zero length. Fails with
/// InvalidArgument.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<void> validate(const SplineSegment2D& spline);

/// Whether @p segment is closed on its own: a circle, an ellipse or a
/// periodic spline.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT bool isClosedSegment(const ProfileSegment& segment) noexcept;

/// Where a segment starts and where it ends. A closed segment ends where it
/// starts: a circle at centre + radius along the plane's X axis, an ellipse
/// at its xVertex, a periodic spline at its parameter 0.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Point2D firstPoint(const ProfileSegment& segment);
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Point2D lastPoint(const ProfileSegment& segment);

/// The point at fraction @p t (in [0, 1]) of the segment's parameter range:
/// its length for lines, its angle for arcs, circles and ellipses, and its
/// knot range for splines (a precondition for splines is validate()).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Point2D pointAt(const ProfileSegment& segment, double t);

/// Signed enclosed area (Green's theorem; exact for lines, arcs, circles and
/// ellipses, and to rounding for splines, whose polynomial spans are
/// integrated by a Gauss rule exact for their degree): positive for a
/// counter-clockwise loop.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Area signedArea(const ProfileLoop& loop);

/// The same loop traversed in the opposite direction.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT ProfileLoop reversed(const ProfileLoop& loop);

/// Area of the region: outer area minus hole areas (orientation-independent).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Area regionArea(const PlanarRegion& region);

/// Centroid of the region's area (outer minus holes) in the plane's local
/// coordinates, by Green's theorem, exact as signedArea() is. The region must
/// enclose a positive area.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Point2D regionCentroid(const PlanarRegion& region);

} // namespace bettercad::geometry
