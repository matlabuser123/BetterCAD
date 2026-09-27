#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>

// Mass properties (P15-MASS-001): mass, centre of mass and inertia, derived from
// a body's geometry and its part's material.
//
// DERIVED, NEVER STORED (ADR-026). Nothing here is persisted, nothing here is
// cached, and there is no setter: every value is recomputed from the geometry the
// regenerator holds and the density the material states. That is what makes it
// impossible for a mass to go stale in a file, and it is the same rule that keeps
// a solved transform (ADR-005) and a drawing's projected curves (ADR-011) out of
// the document.
//
// A request on a part with no material, or with a material that states no
// density, is a DIAGNOSTIC naming the part -- never a mass of zero (ADR-026,
// ADR-027). `unknown` is not `0`, and a zero mass is a physically meaningful
// answer that must not be returned for a question that was not answered.
namespace bettercad {
class Document;
}

namespace bettercad::features {

class Regenerator;

/// One body's inertia tensor, with the point it is taken about.
///
/// The reference point travels WITH the components, because a tensor and the
/// point it is about are one fact: pairing a tensor with the wrong reference
/// point is a defect that no dimension check and no plausibility check would
/// catch. The axes are always the document's own, parallel to global X, Y and Z;
/// there is deliberately no rotation field, so a tensor cannot claim axes that
/// something else would have to be trusted to honour.
///
/// TENSOR CONVENTION, matching geometry::VolumeSecondMoments and measured there
/// rather than assumed: these are inertia TENSOR components, so the off-diagonals
/// are the NEGATED products of inertia,
///
///     xx = integral of (y^2 + z^2) dm        xy = -integral of xy dm
///
/// which is the layout of
///
///     | xx  xy  xz |
///     | xy  yy  yz |
///     | xz  yz  zz |
///
/// so a caller assembling that matrix uses the six values exactly as they are.
/// A caller who wants the positive products of inertia negates the off-diagonals
/// and should say so where they do it.
struct InertiaTensor {
    MassMomentOfInertia xx{};
    MassMomentOfInertia yy{};
    MassMomentOfInertia zz{};
    MassMomentOfInertia xy{};
    MassMomentOfInertia xz{};
    MassMomentOfInertia yz{};
    /// The point the moments are taken about, in the document's axes.
    Point3D about{};

    friend bool operator==(const InertiaTensor&, const InertiaTensor&) = default;
};

/// The same body's tensor about @p to, by Huygens' parallel-axis theorem.
///
///     I(P) = I(cm) + m ( |d|^2 1 - d d^T ),   d = cm - P
///
/// @p centroidal must be about the body's centre of mass, which is what the
/// theorem is stated from; it is not a general point-to-point shift, and passing
/// a tensor about anything else gives a wrong answer that nothing can detect. The
/// direction is deliberate: from the centroid outwards every term is ADDED, so no
/// significance is lost however far the body sits from the point. The reverse
/// shift subtracts two nearly equal numbers, and for a body 1 m from the origin
/// and 10 mm across it would lose about four digits.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT InertiaTensor shiftedFromCentroid(
    const InertiaTensor& centroidal, Mass mass, const Point3D& to);

/// The same tensor about the same MATERIAL POINT, after @p motion has moved the
/// body: A I A^T, with the reference point carried through @p motion as a point.
///
/// The reference point moves WITH THE BODY, which is the whole contract and the
/// part that is easy to get wrong. So for a tensor about the centre of mass the
/// result is about the new centre of mass, and for a tensor about the origin it is
/// about wherever the motion sent that origin -- not about the origin, which stays
/// where it is. To get the moved body's tensor about the FIXED origin, take the
/// centroidal one through here and then shiftedFromCentroid() to Point3D{}, which
/// is what transformed(PartMassProperties, ...) does.
///
/// Correct for any rigid motion, not only a rotation:
///   - a translation leaves the components alone, because an inertia tensor about a
///     material point does not depend on where that point is, and moves only the
///     reference point. A I A^T with A = 1 gives exactly that.
///   - a reflection is orthogonal too, and a mirrored body's moments are the
///     mirrored ones, so a mirror feature needs no separate path.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT InertiaTensor transformed(const InertiaTensor& tensor,
                                                                 const RigidTransform3D& motion);

/// What a part weighs and how its mass is distributed: the derived result of
/// geometry and material together.
///
/// Every field is an output. There is no partially filled instance: a
/// PartMassProperties exists only where a density and a valid volume both did, so
/// a caller never has to ask whether `mass` means anything.
struct PartMassProperties {
    /// The feature whose body this describes.
    ObjectId feature{};
    /// The material that answered, so a report can name it and a caller can tell
    /// two results apart when the assignment changed between them.
    MaterialId material{};
    Density density{};
    Volume volume{};
    /// density x volume. Positive and finite, always.
    Mass mass{};
    /// For a part of uniform density this is the geometric centroid (ADR-026),
    /// in the document's axes and measured from its origin.
    Point3D centreOfMass{};
    /// The invariant tensor: unchanged by translating the body, and what every
    /// other frame is derived from.
    InertiaTensor aboutCentreOfMass{};
    /// The same tensor about the document's origin. Supplied because it is what a
    /// drawing note and an assembly aggregation ask for, and because deriving it
    /// once here is better than having each caller reimplement Huygens.
    InertiaTensor aboutOrigin{};

    friend bool operator==(const PartMassProperties&, const PartMassProperties&) = default;
};

/// The mass properties of the body @p feature produced, using the material
/// @p document is assigned.
///
/// The reference frame is the document's own: the origin and axes the geometry is
/// modelled in. `centreOfMass` and `aboutOrigin.about` are measured from that
/// origin, and every tensor's axes are parallel to its.
///
/// Fails, with a diagnostic that says which of these it is, when:
///   - no material is assigned, or the assigned one has been deleted (ADR-026);
///   - the material states no density, or an unusable one (ADR-027);
///   - @p feature is not a feature, or produced no body;
///   - its last regeneration failed, or was blocked, or has not happened;
///   - the body is empty, encloses no volume, or is of a shape class whose second
///     moments BetterCAD does not integrate;
///   - a configuration with parameter overrides is active. That last one is a
///     carried REGENERATION defect, not a mass one: a configuration override does
///     not currently rebuild the geometry it changes, so the volume in hand would
///     be the base configuration's. Refusing is the only honest answer available
///     from this layer -- the alternative is a wrong mass reported as a right one.
///     When that defect is fixed, this guard and its test go with it.
///
/// It never returns a zero mass for a question it could not answer.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<PartMassProperties> partMassProperties(
    const Document& document, const Regenerator& regenerator, ObjectId feature);

/// @p properties as they would be after @p motion has moved the body.
///
/// The centroidal tensor rotates (A I A^T) and the centre of mass moves; the
/// tensor about the ORIGIN is then re-derived by Huygens from the new centre of
/// mass, because the origin is fixed in space and does not travel with the body.
/// Mass, volume, density and material are invariant under a rigid motion, and are
/// carried through unchanged.
///
/// This exists so that a transformed body's mass properties can be obtained
/// without re-integrating the geometry, and so that the identity
/// "integrate the moved body == move the integrated body" is testable. It is not
/// a substitute for integration: it assumes @p motion is the only difference.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT PartMassProperties transformed(
    const PartMassProperties& properties, const RigidTransform3D& motion);

} // namespace bettercad::features
