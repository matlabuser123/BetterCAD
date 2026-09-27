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
    /// Relative error estimates: the kernel's, from its integration, and for
    /// faces swept from ellipses and splines that BetterCAD integrates
    /// itself, the change of the last refinement.
    double volumeRelativeError = 0.0;
    double areaRelativeError = 0.0;
};

/// The second moments of a body's VOLUME about its own CENTROID, in axes parallel
/// to the document's (P15-MASS-001).
///
/// Geometry, not mass: no density has been applied, so every component is a
/// second moment of volume (m^5). Multiplying by a density gives a mass moment of
/// inertia, and the dimensions check that rather than a comment asserting it:
/// (M L^-3)(L^5) = M L^2.
///
/// CENTROIDAL, not about the origin. The reference point is the body's own centre
/// of volume -- massProperties().centerOfMass -- so these are the invariant
/// moments, and moments about any other point follow by Huygens' parallel-axis
/// theorem, which ADDS a term and so cannot lose precision. Taking the centroidal
/// moments by subtraction from moments about a distant origin would, which is why
/// this is the direction the API offers.
///
/// TENSOR CONVENTION, stated because the two conventions differ by a sign and
/// silently mixing them is a real defect: these are the components of the inertia
/// TENSOR, so the off-diagonals are the NEGATED products,
///
///     xx = integral of (y^2 + z^2) dV        xy = -integral of xy dV
///
/// and not the positive products of inertia. A caller assembling a matrix uses
/// them as they are.
///
/// Both of those claims are MEASURED, not taken from the kernel's documentation.
/// A box at the origin settled the reference point -- it returned its centroidal
/// moments, while the kernel's header says the reference point it is given is
/// used for "inertia accumulation" -- and a three-box staircase, whose centroid
/// has all three products non-zero, settled the sign. See
/// tests/core/geometry/VolumeSecondMomentsTests.cpp.
struct VolumeSecondMoments {
    VolumeSecondMoment xx{};
    VolumeSecondMoment yy{};
    VolumeSecondMoment zz{};
    VolumeSecondMoment xy{};
    VolumeSecondMoment xz{};
    VolumeSecondMoment yz{};

    friend bool operator==(const VolumeSecondMoments&, const VolumeSecondMoments&) = default;
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

    /// The second moments of this body's volume about its own centroid.
    ///
    /// Fails for an empty body, for one that encloses no volume, and -- with a
    /// diagnostic saying so -- for a body with a general swept-curve face.
    /// massProperties() integrates those itself because the kernel's result is
    /// not accurate enough for them, and that integration produces a volume and
    /// a first moment only; taking second moments from the kernel for exactly the
    /// shapes whose volume it is not trusted for would be inconsistent.
    [[nodiscard]] Result<VolumeSecondMoments> centroidalVolumeSecondMoments() const;
    /// Tight axis-aligned bounds; fails with FailedPrecondition for an empty body.
    [[nodiscard]] Result<BoundingBox3D> boundingBox() const;

private:
    friend struct occt::BodyAccess;
    explicit Body(std::shared_ptr<const occt::BodyData> data) noexcept;

    std::shared_ptr<const occt::BodyData> data_;
};

} // namespace bettercad::geometry
