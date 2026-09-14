#pragma once

#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>

#include <variant>
#include <vector>

// Planar profiles: closed loops of line, arc and circle segments in the local
// coordinates of a plane. Features build them from sketches; the geometry
// kernel turns them into faces and solids.
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

using ProfileSegment = std::variant<LineSegment2D, ArcSegment2D, CircleSegment2D>;

/// A closed loop: segments head to tail, the last ending where the first
/// starts. A full circle is a loop of one CircleSegment2D.
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

/// Signed enclosed area (Green's theorem, arcs exact): positive for a
/// counter-clockwise loop.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Area signedArea(const ProfileLoop& loop);

/// The same loop traversed in the opposite direction.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT ProfileLoop reversed(const ProfileLoop& loop);

/// Area of the region: outer area minus hole areas (orientation-independent).
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Area regionArea(const PlanarRegion& region);

} // namespace bettercad::geometry
