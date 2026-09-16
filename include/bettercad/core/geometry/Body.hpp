#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/BoundingBox.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <memory>

namespace bettercad::geometry {

namespace occt {
struct BodyData;
struct BodyAccess;
} // namespace occt

/// Volume, surface area and centre of mass of a body (uniform density).
struct MassProperties {
    Volume volume{};
    Area surfaceArea{};
    Point3D centerOfMass{};
    /// Relative error estimates: the kernel's, from its adaptive integration,
    /// and for faces swept from ellipses and splines, whose areas BetterCAD
    /// integrates itself, the change of the last refinement.
    double volumeRelativeError = 0.0;
    double areaRelativeError = 0.0;
};

/// Number of distinct topological entities of each kind in a body.
struct TopologySummary {
    std::size_t solids = 0;
    std::size_t shells = 0;
    std::size_t faces = 0;
    std::size_t edges = 0;
    std::size_t vertices = 0;

    friend bool operator==(const TopologySummary&, const TopologySummary&) = default;
};

/// Solid geometry consisting of zero or more solids.
///
/// Bodies are produced by geometry operations (primitives, booleans) and are
/// immutable: operations return new bodies. Copies are cheap and share the
/// underlying kernel shape. A default-constructed Body is empty.
class BETTERCAD_GEOMETRY_EXPORT Body {
public:
    Body() noexcept;

    [[nodiscard]] bool isEmpty() const noexcept;
    /// Topological and geometric validity as judged by the kernel's analyzer.
    /// An empty body is not valid.
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] TopologySummary topology() const;
    /// Fails with FailedPrecondition for an empty body.
    [[nodiscard]] Result<MassProperties> massProperties() const;
    /// Tight axis-aligned bounds; fails with FailedPrecondition for an empty body.
    [[nodiscard]] Result<BoundingBox3D> boundingBox() const;

private:
    friend struct occt::BodyAccess;
    explicit Body(std::shared_ptr<const occt::BodyData> data) noexcept;

    std::shared_ptr<const occt::BodyData> data_;
};

} // namespace bettercad::geometry
