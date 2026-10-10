#pragma once

// P17-REACTION-001 -- support reactions and static equilibrium.
//
// A REACTION IS THE CONSTRAINED COMPONENT OF `K u - F`, AND NOTHING ELSE
// (ADR-041). The quantity already exists: `SolvedSystem::fullResidual()` is
// `K u - F` over the full system, computed in one pass with per-entry
// finiteness, and its header retained it for this milestone by name. So there
// is no second residual definition here, no re-solve, no penalty term, and no
// stress integration.
//
//     R[dof] = fullResidual()[dof]     for dof in constraints().constrained()
//
// THE SIGN CONVENTION, VERIFIED BEFORE IT WAS DOCUMENTED:
//
//     R = the force applied BY the support ON the structure
//     sum(F_external) + sum(R) = 0
//
// One degree of freedom, constrained so `u = 0`, external load `+F`:
// `K u - F = -F`, so `R = -F`, and `F + (-F) = 0`. Push on a structure held by
// a support and the support pushes back. No sign is flipped anywhere to make a
// sum come out zero.
//
// THE FREE RESIDUAL IS NOT A REACTION. `fullResidual()` covers every degree of
// freedom and its free entries are small but non-zero; they are solver-quality
// diagnostics. At a node constrained in `Ux` only, `Rx` is a reaction and the
// tiny values at its `Uy` and `Uz` rows are NOT -- reporting them would invent
// two support components no restraint asked for.
//
// A SHARED DEGREE OF FREEDOM IS COUNTED ONCE. Two restraints can lawfully
// reach the same one; the physical reaction there exists once and cannot be
// partitioned between them without inventing a convention. So each summary
// reports the DOFs it ALONE owns, the shared DOFs are aggregated once
// separately, and the sum is an exact identity rather than a rule a caller has
// to remember.
//
// NO ROTATIONAL REACTION DEGREES OF FREEDOM. A Tet4 node has `Ux, Uy, Uz`.
// A support region's moment is the moment of its translational reaction
// distribution, `sum (x - O) x R`, computed on request with the origin as a
// parameter. There is no nodal `Mx, My, Mz` in any type here.
//
// WHAT THIS FILE DOES NOT DO. No solve -- it consumes one. No load
// re-integration: the external force is the exact assembled `F` that was
// solved, never a recomputed pressure or traction. No mesh-quality policy, no
// acceptance verdict beyond equilibrium itself, no display units, no
// persistence.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralAnalysis.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralData.hpp>
#include <bettercad/structural/StructuralDof.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralRestraint.hpp>
#include <bettercad/structural/StructuralSolve.hpp>
#include <bettercad/structural/StructuralSystem.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::structural {

// ---------------------------------------------------------------------------
// One node's reaction
// ---------------------------------------------------------------------------

/// The support reaction at one constrained node.
///
/// IT CARRIES THE CONSTRAINED MASK, and that is the point. A node restrained
/// in `Ux` only has a reaction in `x` and nothing in `y` or `z`; without the
/// mask a consumer could not tell that `force.y` is zero because the node is
/// free there rather than because the support happens to carry no load.
/// `P17-DATA-001`'s `NodalReaction` is the eventual publication channel and
/// carries `{node, force}`; this is the stage product, and the mask is the
/// information the stage has that the channel does not yet model.
///
/// Components the mask does not hold are EXACTLY zero -- they are never read
/// from the full residual at all, so no free-residual noise can reach them.
struct SupportReaction {
    meshing::NodeId node{};
    /// The force the support applies to the structure, in newtons. Only the
    /// components `constrained` holds are meaningful; the others are zero.
    Force3D force{};
    /// Which of this node's three degrees of freedom a restraint constrained.
    RestraintComponents constrained{};

    friend constexpr bool operator==(const SupportReaction&,
                                     const SupportReaction&) noexcept = default;
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT bool isFinite(const SupportReaction& reaction) noexcept;

// ---------------------------------------------------------------------------
// Per-restraint attribution
// ---------------------------------------------------------------------------

/// What one restraint's reactions come to.
///
/// THE DECOMPOSITION IS ADDITIVE BY CONSTRUCTION (ADR-041). `ownedForce`
/// covers the degrees of freedom this restraint ALONE constrained, so
///
/// ```text
///     sum over restraints of ownedForce  +  sharedForce  ==  totalForce
/// ```
///
/// holds exactly, and the same for moments about one origin. Additivity is a
/// property of the split rather than a caveat a caller must remember.
///
/// `sharedForce` is the reaction on the degrees of freedom this restraint
/// shares with at least one other. It is here for traceability -- a reader
/// inspecting one restraint wants to see it -- and it is **NOT ADDITIVE**
/// across restraints: the same physical reaction appears in every summary that
/// shares it. `sharedDegreesOfFreedom != 0` is the signal that a summary has a
/// non-additive part.
struct RestraintReaction {
    RestraintId restraint{};
    /// Degrees of freedom only this restraint constrained.
    std::size_t ownedDegreesOfFreedom = 0;
    /// Degrees of freedom it constrained that at least one other also did.
    std::size_t sharedDegreesOfFreedom = 0;

    /// Resultant over the owned degrees of freedom. ADDITIVE.
    Force3D ownedForce{};
    /// Resultant over the shared ones. NOT additive across restraints.
    Force3D sharedForce{};

    /// Moment of the owned reactions about the requested origin. ADDITIVE.
    Moment3D ownedMoment{};
    /// Moment of the shared ones. NOT additive across restraints.
    Moment3D sharedMoment{};

    friend constexpr bool operator==(const RestraintReaction&,
                                     const RestraintReaction&) noexcept = default;
};

// ---------------------------------------------------------------------------
// Equilibrium
// ---------------------------------------------------------------------------

/// How close `sum(F_external) + sum(R)` is to zero, and against what scale.
///
/// THE COMPONENTS ARE REPORTED, NOT ONLY A NORM. A norm can hide a
/// cancellation between components, so `imbalance` is kept as a vector and the
/// two norms are derived from it.
///
/// THE SCALE IS A SUM OF MAGNITUDES, NOT THE NET RESULTANT (ADR-041).
/// Normalising by `||F_external||` would divide a real imbalance by almost
/// nothing whenever large loads cancel -- a couple has zero net force by
/// construction -- so the denominator is
///
/// ```text
///     scale = sum |F_i| over loaded nodes + sum |R_j| over constrained nodes
/// ```
///
/// which reflects what actually participates. `scale` is reported so the
/// tolerance is auditable rather than asserted.
struct ForceBalance {
    Force3D external{};
    Force3D reaction{};
    /// `external + reaction`. Zero in exact arithmetic.
    Force3D imbalance{};
    Force euclideanNorm{};
    Force infinityNorm{};
    /// Sum of the individual force magnitudes on both sides.
    Force scale{};
    /// `euclideanNorm / max(scale, floor)`. Dimensionless.
    double normalized = 0.0;

    friend constexpr bool operator==(const ForceBalance&, const ForceBalance&) noexcept = default;
};

/// The same for moments, about an explicit origin.
///
/// THE ORIGIN IS A FIELD, not a convention. A moment about an unstated point
/// is not a quantity, which is what core's `momentOf` already insists on by
/// taking a relative lever.
struct MomentBalance {
    Point3D origin{};
    Moment3D external{};
    Moment3D reaction{};
    Moment3D imbalance{};
    Torque euclideanNorm{};
    Torque infinityNorm{};
    /// Sum of the individual moment magnitudes on both sides.
    Torque scale{};
    double normalized = 0.0;

    friend constexpr bool operator==(const MomentBalance&, const MomentBalance&) noexcept = default;
};

/// The acceptance thresholds, and the floors that keep a zero system from
/// dividing by zero.
///
/// TWO FORCE THRESHOLDS, DELIBERATELY. The algebraic path -- the assembled `F`
/// that was solved, against the reactions recovered from the same system -- is
/// a different numerical question from comparing reactions against an
/// analytical continuum resultant, which carries the load integration's own
/// geometric error. Using one loose number for both would let a sign or
/// mapping defect hide behind a tolerance that exists for a different reason.
///
/// A SOLVER RESIDUAL TOLERANCE IS NOT AN EQUILIBRIUM TOLERANCE. The solver's
/// gate is on the free equations; this one is on the whole body's applied and
/// constrained forces. The values here were MEASURED -- see
/// docs/verification/P17-REACTION-001/EQUILIBRIUM_TOLERANCE.md -- and are not
/// inherited from `SolverSettings`.
///
/// THE FLOORS CARRY THEIR OWN DIMENSION. A force floor is in newtons and a
/// moment floor in newton-metres; one dimensionless number for both would be a
/// unit error the compiler could not see.
struct EquilibriumTolerance {
    /// Normalized force imbalance permitted against the assembled `F`.
    double force = 1.0e-12;
    /// Normalized moment imbalance permitted about the requested origin.
    double moment = 1.0e-12;
    /// Below this total force magnitude the system is treated as unloaded and
    /// the normalized value is defined as zero rather than divided.
    Force forceFloor = Force::fromSi(1.0e-12);
    /// Likewise for moments.
    Torque momentFloor = Torque::fromSi(1.0e-12);

    friend constexpr bool operator==(const EquilibriumTolerance&,
                                     const EquilibriumTolerance&) noexcept = default;
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<void>
validate(const EquilibriumTolerance& tolerance);

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why reactions cannot be recovered, or why they do not balance.
///
/// WHICH VALUES ARE REACHABLE FROM WHERE is recorded in the evidence rather
/// than claimed here, because possession of a `SolvedSystem` already proves a
/// great deal: every displacement and every residual entry is finite, and the
/// system and the constraints were built against one mesh.
enum class ReactionProblem : std::uint8_t {
    /// The tolerance itself is malformed: a negative or non-finite threshold
    /// or floor.
    InvalidTolerance,
    /// The solved system was not produced from the system handed in.
    SolutionSourceMismatch,
    /// The prepared restraints, the numbering or the loads do not describe the
    /// mesh this analysis holds.
    MeshMismatch,
    /// The prepared restraints' constrained set is not the one the solve used.
    /// REFUSED RATHER THAN RE-RESOLVED: attributing reactions through a
    /// mapping that was not the solve's is how a source mismatch hides.
    ConstraintSourceMismatch,
    /// A constrained degree of freedom is outside the solved system.
    DegreeOfFreedomOutOfRange,
    /// A constrained degree of freedom names a node the mesh does not have.
    NodeMissing,
    /// A recovered reaction component is not finite.
    NonFiniteReaction,
    /// An external force or moment total is not finite.
    NonFiniteExternal,
    /// A balance metric came out non-finite although its inputs were finite.
    NonFiniteBalance,
    /// The force imbalance exceeds the measured threshold. A FAILURE, not a
    /// warning: the solve can satisfy its own residual gate and still have a
    /// reaction sign, mapping or load-accounting defect, and this is the only
    /// check that sees it.
    ForceImbalance,
    /// The moment imbalance exceeds the measured threshold. Checked even when
    /// the force balance passes, because a pure couple balances in force and
    /// not in moment.
    MomentImbalance,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(ReactionProblem problem) noexcept;

// ---------------------------------------------------------------------------
// The recovered reactions
// ---------------------------------------------------------------------------

/// Every support reaction of one solved analysis, with its equilibrium proof.
///
/// POSSESSION IS THE EVIDENCE (ADR-036). There is no public constructor and
/// exactly one friend, so holding a `SupportReactions` proves that the
/// solution, the system, the numbering, the restraints and the loads all
/// agreed; that every constrained degree of freedom was read exactly once;
/// that every value is finite; and that BOTH force and moment equilibrium
/// passed their measured thresholds. A set of reactions that does not balance
/// is unrepresentable rather than merely reported.
///
/// ORDERED BY THE MESH: nodes ascending by `NodeId`, restraints in the order
/// they were given to `prepareStructuralRestraints`. Every sum is taken in
/// that order, which is what makes the floating-point answer reproducible.
///
/// `O(constrained nodes + restraints)`. Neither the mesh nor `K` is copied in.
class BETTERCAD_STRUCTURAL_EXPORT SupportReactions {
public:
    /// What the system these reactions came from was assembled from. Copied
    /// forward from the solved system, never derived.
    [[nodiscard]] const AssemblySource& source() const noexcept { return source_; }

    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }

    /// Whether these reactions belong to @p mesh. Stamp identity, never counts.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept {
        return mesh.owns(mesh_) && mesh.nodes().size() == nodeCount_;
    }

    /// One entry per node with at least one constrained degree of freedom,
    /// ascending by `NodeId`.
    ///
    /// OPTION A OF THE BRIEF'S CHOICE: a node with no restraint is ABSENT
    /// rather than present with a zero force, which is the same convention
    /// `PreparedLoads::nodal()` already uses and the reason `NodalReaction` is
    /// documented as "SPARSE BY NATURE". A zero here is a support carrying no
    /// load; a missing node is a node with no support.
    [[nodiscard]] std::span<const SupportReaction> nodal() const noexcept { return nodal_; }

    /// One summary per restraint, in the order the restraints were given.
    [[nodiscard]] std::span<const RestraintReaction> restraints() const noexcept {
        return restraints_;
    }

    /// How many constrained degrees of freedom more than one restraint reached.
    /// Non-zero means the per-restraint `sharedForce` fields are populated and
    /// are not additive across restraints.
    [[nodiscard]] std::size_t sharedDegreesOfFreedom() const noexcept { return sharedDofs_; }

    /// The reaction on the shared degrees of freedom, counted ONCE.
    [[nodiscard]] const Force3D& sharedForce() const noexcept { return sharedForce_; }
    [[nodiscard]] const Moment3D& sharedMoment() const noexcept { return sharedMoment_; }

    /// Every constrained degree of freedom, counted once.
    [[nodiscard]] std::size_t constrainedDegreesOfFreedom() const noexcept {
        return constrainedDofs_;
    }

    /// The total reaction force: the sum over `nodal()` in its own ascending
    /// order.
    [[nodiscard]] const Force3D& totalForce() const noexcept { return force_.reaction; }

    /// Force equilibrium against the assembled `F` that was solved.
    [[nodiscard]] const ForceBalance& forceBalance() const noexcept { return force_; }

    /// Moment equilibrium about the origin this was recovered for.
    [[nodiscard]] const MomentBalance& momentBalance() const noexcept { return moment_; }

    /// The thresholds these reactions were accepted under, so a reader never
    /// has to guess which tolerance produced a verdict.
    [[nodiscard]] const EquilibriumTolerance& tolerance() const noexcept { return tolerance_; }

    /// The reaction at @p node of @p mesh, or a failure if it is not a
    /// constrained node of this mesh.
    ///
    /// IT TAKES THE MESH AND REFUSES IF IT IS NOT THIS ONE -- the contract
    /// `StructuralResult::displacementOf` has and `RecoveredFields` adopted,
    /// for the reason P16 makes necessary: handles restart at 1 after every
    /// remesh, so a handle from another mesh would otherwise resolve here and
    /// return a plausible number for unrelated material.
    [[nodiscard]] Result<SupportReaction> at(const meshing::Mesh& mesh,
                                             meshing::NodeId node) const;

    /// The summary of @p restraint, or a failure if no restraint has that id.
    [[nodiscard]] Result<RestraintReaction> of(RestraintId restraint) const;

    /// The total reaction moment about @p origin, recomputed for a different
    /// reference point.
    ///
    /// DERIVED ON REQUEST, NEVER STORED AS AN INDEPENDENT QUANTITY (ADR-041).
    /// A moment depends on its origin, and the transfer relation
    /// `M(O2) = M(O1) - (O2 - O1) x F` is what this satisfies -- tested, which
    /// is what freezes the convention and catches a reversed cross product.
    /// Needs the mesh for node positions, and refuses one it does not describe.
    [[nodiscard]] Result<Moment3D> momentAbout(const meshing::Mesh& mesh,
                                               const Point3D& origin) const;

    friend bool operator==(const SupportReactions&, const SupportReactions&) = default;

private:
    /// THE ONLY CONSTRUCTOR, AND THERE IS NO DEFAULT ONE -- the shape
    /// `PreparedRestraints`, `GlobalStructuralSystem`, `SolvedSystem` and
    /// `RecoveredFields` all use, for the reason ADR-036 gives.
    SupportReactions(AssemblySource source, meshing::MeshStamp mesh, std::size_t nodeCount,
                     std::size_t constrainedDofs, std::size_t sharedDofs, Force3D sharedForce,
                     Moment3D sharedMoment, EquilibriumTolerance tolerance, ForceBalance force,
                     MomentBalance moment, std::vector<SupportReaction> nodal,
                     std::vector<RestraintReaction> restraints)
        : source_(std::move(source)), mesh_(std::move(mesh)), nodeCount_(nodeCount),
          constrainedDofs_(constrainedDofs), sharedDofs_(sharedDofs),
          sharedForce_(sharedForce), sharedMoment_(sharedMoment), tolerance_(tolerance),
          force_(force), moment_(moment), nodal_(std::move(nodal)),
          restraints_(std::move(restraints)) {}

    friend BETTERCAD_STRUCTURAL_EXPORT Result<SupportReactions>
    recoverSupportReactions(const StructuralModel& model, const GlobalStructuralSystem& system,
                            const PreparedRestraints& restraints, const PreparedLoads& loads,
                            const SolvedSystem& solution, const Point3D& origin,
                            const EquilibriumTolerance& tolerance);

    AssemblySource source_;
    meshing::MeshStamp mesh_;
    std::size_t nodeCount_;
    std::size_t constrainedDofs_;
    std::size_t sharedDofs_;
    Force3D sharedForce_;
    Moment3D sharedMoment_;
    EquilibriumTolerance tolerance_;
    ForceBalance force_;
    MomentBalance moment_;
    std::vector<SupportReaction> nodal_;
    std::vector<RestraintReaction> restraints_;
};

/// Recovers the support reactions of @p solution and proves equilibrium.
///
/// SIX INPUTS, AND EACH IS CHECKED AGAINST THE OTHERS rather than trusted:
///
/// ```text
/// solution.source() == system.source()     the solution is THIS system's
/// system.numbering().describes(mesh)       the numbering is THIS mesh's
/// solution.describes(mesh)                 the field is THIS mesh's size
/// restraints.describes(mesh)               the restraints are THIS mesh's
/// loads.describes(mesh)                    the loads are too
/// restraints.constraints() is the set
///   the solve reduced with                 compared element by element
/// ```
///
/// The last one is the check the brief insists on: per-restraint attribution
/// must use the mapping the SOLVE used, so a set that merely describes the
/// same mesh is not enough.
///
/// THE EXTERNAL FORCE IS THE ASSEMBLED `F`, reconstructed from
/// `system.force()` through the qualified numbering -- the exact vector that
/// was solved. @p loads is used only for the mesh check and as the independent
/// cross-check oracle; no pressure, traction or body force is re-integrated
/// here, and the two are never added together.
///
/// @p origin is the reference point for every moment. There is no default: a
/// moment about an unstated point is not a quantity.
///
/// FAILS if the force or the moment imbalance exceeds @p tolerance. That is a
/// failure and not a warning -- a solve can satisfy its own residual gate and
/// still have a reaction sign, mapping or load-accounting defect.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<SupportReactions>
recoverSupportReactions(const StructuralModel& model, const GlobalStructuralSystem& system,
                        const PreparedRestraints& restraints, const PreparedLoads& loads,
                        const SolvedSystem& solution, const Point3D& origin,
                        const EquilibriumTolerance& tolerance);

/// The problem `recoverSupportReactions` would report, or none if it would
/// succeed. Same checks, same order, one shared implementation.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<ReactionProblem>
reactionProblem(const StructuralModel& model, const GlobalStructuralSystem& system,
                const PreparedRestraints& restraints, const PreparedLoads& loads,
                const SolvedSystem& solution, const Point3D& origin,
                const EquilibriumTolerance& tolerance);

/// The resultant force of the assembled `F`, grouped into nodal components
/// through the qualified numbering.
///
/// EXPOSED SO THE EXTERNAL TOTAL CAN BE CHECKED DIRECTLY against
/// `PreparedLoads::resultantForce()`, which sums the same nodal forces in
/// `NodeId` order rather than in row order. The two must agree, and that
/// agreement is the brief's load cross-check: it triangulates load
/// preparation, assembly and reaction without this milestone inventing a third
/// load path.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<Force3D>
assembledForceResultant(const meshing::Mesh& mesh, const MeshDofMap& numbering,
                        const ForceVector& force);

/// The resultant moment of the assembled `F` about @p origin.
///
/// `sum (x_i - O) x F_i` over every node of the mesh, through core's
/// `momentOf` -- the one cross product in this module. Includes loads at
/// INTERNAL nodes and at constrained nodes: an applied load is external
/// wherever it acts.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<Moment3D>
assembledMomentResultant(const meshing::Mesh& mesh, const MeshDofMap& numbering,
                         const ForceVector& force, const Point3D& origin);

} // namespace bettercad::structural
