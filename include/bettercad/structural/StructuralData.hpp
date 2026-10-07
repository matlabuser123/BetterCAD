#pragma once

// P17-DATA-001 -- the structural analysis and result data model.
//
// THREE IDENTITY DOMAINS, AND THEY ARE NOT INTERCHANGEABLE. That is what this
// file exists to make true in the type system rather than in a convention:
//
//   DOCUMENT      AnalysisId, LoadId, RestraintId. Canonical, persisted,
//                 survive a remesh, a reload and a process. They live in
//                 core/Id.hpp with every other document identity.
//   MESH-LOCAL    NodeId, ElementId. P16's, unchanged by this milestone.
//                 Valid only against the MeshStamp that issued them, and
//                 never persisted as CAD identity (ADR-031).
//   SOLVER-LOCAL  DofIndex. Valid only within one DOF numbering of one mesh.
//                 Defined HERE and deliberately NOT in core/Id.hpp.
//
// A result may index its arrays by NodeId and ElementId, because the result
// belongs to exactly one mesh and dies with it. That is not a contradiction of
// ADR-031 -- it is what ADR-031 permits. What ADR-031 forbids is a handle in a
// file, in a control, in a command or as the identity of a load or a restraint,
// and none of those appears here.
//
// WHAT THIS FILE IS NOT. There is no stiffness matrix, no element formulation,
// no DOF numbering algorithm and no solve. P17-DOF-001 numbers the DOFs,
// P17-ELEM-001 forms the element, P17-LOAD-001 and P17-BC-001 define the load
// and restraint payloads that `StructuralAnalysisDefinition` will carry, and
// P17-SOLVE-001 produces a result. This defines what they each hand over.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/structural/Export.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::structural {

// ---------------------------------------------------------------------------
// Analysis mode
// ---------------------------------------------------------------------------

/// What a structural analysis asks of its material.
///
/// ANALYSIS INTENT DECIDES, never the data. A material that happens to carry a
/// density does not make an analysis a self-weight analysis, and one that lacks
/// it does not disqualify an ordinary linear-static solve. The mode is a field
/// of `StructuralAnalysisDefinition` for exactly that reason: the user says
/// whether gravity is part of the problem, and the material requirement follows
/// from the answer.
///
/// This maps onto P15's `ConsumerKind`, which already holds the one requirement
/// table (`requiredProperties`): FeaLinearStatic needs E and nu only, and the
/// WITH GRAVITY variant adds a density, "because that is when a mass enters the
/// equations".
enum class StructuralAnalysisMode : std::uint8_t {
    /// Linear static with no body force. Needs E and nu.
    LinearStatic,
    /// Linear static including self-weight. Needs E, nu and a density.
    ///
    /// SELECTABLE, NOT YET ASSEMBLED. P17-LOAD-001 owns the body-force vector.
    /// A material can be resolved for this mode today, which is what proves the
    /// data is there before the physics needs it.
    LinearStaticWithGravity,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(StructuralAnalysisMode mode) noexcept;

// ---------------------------------------------------------------------------
// Solver-local identity
// ---------------------------------------------------------------------------

/// Which displacement component a degree of freedom carries.
///
/// Three translations per node, which is the whole of P17's initial kinematics
/// (ADR-034). Rotations are not here because a Tet4 continuum element has none:
/// a node of a solid mesh translates and nothing else. Shells and beams would
/// add them, and both are explicitly out of scope.
enum class DofComponent : std::uint8_t {
    Ux,
    Uy,
    Uz,
};

/// The three, in the order a node's degrees of freedom are numbered.
inline constexpr std::array<DofComponent, 3> kDofComponents{DofComponent::Ux, DofComponent::Uy,
                                                            DofComponent::Uz};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(DofComponent component) noexcept;

/// One equation of one assembled system.
///
/// SOLVER-LOCAL, AND THAT IS WHY IT IS NOT IN core/Id.hpp. A DofIndex means a
/// row of K and F under one numbering of one mesh. Remesh and it means nothing;
/// renumber the same mesh and it means something else. Persisting one would be
/// the defect this milestone exists to prevent -- a restraint that stored
/// "DOF 1042" would silently constrain unrelated material after a remesh,
/// whereas a restraint that stores a CAD reference resolves correctly.
///
/// 64-BIT, DELIBERATELY. A NodeId is 32-bit, and three DOFs per node means the
/// count is 3 x nodeCount; a 32-bit index would overflow before a NodeId does,
/// which is the kind of arithmetic that looks fine until the mesh is large.
///
/// A distinct type rather than a bare integer, so that it cannot be passed
/// where a NodeId, an ElementId or a count is wanted. The numbering itself is
/// P17-DOF-001's; this is only the handle it hands out.
class DofIndex {
public:
    using ValueType = std::uint64_t;

    /// The invalid index. Valid indices start at 1, matching every other
    /// handle in BetterCAD, so a default-constructed one is invalid rather
    /// than pointing at the first equation.
    constexpr DofIndex() noexcept = default;

    [[nodiscard]] static constexpr DofIndex fromValue(ValueType value) noexcept {
        DofIndex index;
        index.value_ = value;
        return index;
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }
    constexpr explicit operator bool() const noexcept { return isValid(); }

    friend constexpr bool operator==(const DofIndex&, const DofIndex&) noexcept = default;
    friend constexpr auto operator<=>(const DofIndex&, const DofIndex&) noexcept = default;

private:
    constexpr explicit DofIndex(ValueType value) noexcept : value_(value) {}

    ValueType value_ = 0;
};

/// One degree of freedom, named the way a human and a mesh both understand it.
///
/// `(NodeId, DofComponent)` is meaningful for as long as the mesh is; the
/// `DofIndex` it maps to is meaningful only for as long as the numbering is.
/// Keeping the pair and the index as different types is what stops a
/// restraint's "the z of this node" becoming "equation 1042".
struct NodalDof {
    meshing::NodeId node{};
    DofComponent component{};

    friend constexpr bool operator==(const NodalDof&, const NodalDof&) noexcept = default;
    friend constexpr auto operator<=>(const NodalDof&, const NodalDof&) noexcept = default;
};

// ---------------------------------------------------------------------------
// Result component conventions
// ---------------------------------------------------------------------------

/// The six independent components of a symmetric 3x3 tensor, in the order
/// BetterCAD's structural code uses when one is written as a vector.
///
/// FROZEN HERE, ONCE, FOR EVERY P17 MILESTONE. `P17-ELEM-001` builds a 6x12 B
/// matrix and a 6x6 D matrix whose rows are in this order; `P17-POST-001` reads
/// them back; a GUI or CLI that prints components uses it too. A module that
/// chose its own would produce stress that looked plausible and was wrong in
/// the shear terms only -- which is the hardest kind of defect to see.
///
/// NOT THE SAME ORDER AS `InertiaTensor`, which lists xx, yy, zz, xy, xz, yz.
/// Said out loud because the two are six-component symmetric tensors in the
/// same codebase and a loop copied between them would be wrong. They differ
/// because this order is the cyclic Voigt convention the isotropic elasticity
/// matrix is conventionally written in, and an inertia tensor is never written
/// as a Voigt vector at all -- its field order carries no numerical meaning.
enum class TensorComponent : std::uint8_t {
    XX = 0,
    YY = 1,
    ZZ = 2,
    XY = 3,
    YZ = 4,
    ZX = 5,
};

/// The six, in index order.
inline constexpr std::array<TensorComponent, 6> kTensorComponents{
    TensorComponent::XX, TensorComponent::YY, TensorComponent::ZZ,
    TensorComponent::XY, TensorComponent::YZ, TensorComponent::ZX};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(TensorComponent component) noexcept;

/// Small strain at a point, in engineering shear.
///
/// THE SHEAR CONVENTION IS IN THE FIELD NAMES, not in a comment someone may not
/// read. `gammaXy` is the engineering shear strain
///
/// ```text
///     gammaXy = 2 * epsilonXy
/// ```
///
/// and a reader who sees `gammaXy` beside `xx` cannot mistake which of the two
/// conventions this is. The alternative -- six fields named xx..zx with a
/// comment saying "engineering" -- is exactly how a tensor-shear factor of two
/// gets lost between two milestones.
///
/// The constitutive matrix `P17-ELEM-001` writes must match: with engineering
/// shear the isotropic D has `mu` on its shear diagonal, not `2 mu`.
///
/// DIMENSIONLESS, so plain doubles. There is no Quantity for a ratio, and
/// inventing one here would be the second unit system this milestone must not
/// create.
struct Strain6 {
    double xx = 0.0;
    double yy = 0.0;
    double zz = 0.0;
    double gammaXy = 0.0;
    double gammaYz = 0.0;
    double gammaZx = 0.0;

    /// By `TensorComponent`, for code that genuinely indexes -- a B-matrix
    /// product, a report column. Prefer the named field everywhere else.
    [[nodiscard]] constexpr double component(TensorComponent which) const noexcept {
        switch (which) {
        case TensorComponent::XX: return xx;
        case TensorComponent::YY: return yy;
        case TensorComponent::ZZ: return zz;
        case TensorComponent::XY: return gammaXy;
        case TensorComponent::YZ: return gammaYz;
        case TensorComponent::ZX: return gammaZx;
        }
        return 0.0;
    }

    friend constexpr bool operator==(const Strain6&, const Strain6&) noexcept = default;
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT bool isFinite(const Strain6& strain) noexcept;

/// Stress at a point, in the same component order as `Strain6`.
///
/// Each component is a `Stress`, which is BetterCAD's `Pressure` -- Pa in SI,
/// the unit P15 already stores a Young's modulus in. There is no ambiguity in
/// the shear terms to name away: a `tau_xy` is a `tau_xy` either way, and the
/// factor of two lives on the strain side alone.
struct Stress6 {
    Stress xx{};
    Stress yy{};
    Stress zz{};
    Stress xy{};
    Stress yz{};
    Stress zx{};

    [[nodiscard]] constexpr Stress component(TensorComponent which) const noexcept {
        switch (which) {
        case TensorComponent::XX: return xx;
        case TensorComponent::YY: return yy;
        case TensorComponent::ZZ: return zz;
        case TensorComponent::XY: return xy;
        case TensorComponent::YZ: return yz;
        case TensorComponent::ZX: return zx;
        }
        return Stress{};
    }

    friend constexpr bool operator==(const Stress6&, const Stress6&) noexcept = default;
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT bool isFinite(const Stress6& stress) noexcept;

/// One node's reaction force.
///
/// SPARSE BY NATURE, which is why this carries its `NodeId` while displacement
/// does not: a reaction exists only where the model is restrained, and that is
/// a handful of nodes out of thousands. A dense array would be mostly zeros
/// that cannot be told from a genuine zero reaction at a restrained node.
struct NodalReaction {
    meshing::NodeId node{};
    Force3D force{};

    friend constexpr bool operator==(const NodalReaction&, const NodalReaction&) noexcept = default;
};

} // namespace bettercad::structural
