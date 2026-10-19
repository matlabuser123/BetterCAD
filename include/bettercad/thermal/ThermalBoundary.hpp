#pragma once

// P18-BC-001 -- turning canonical thermal boundary conditions into current-mesh
// temperature constraints and convection boundary terms.
//
// THE AUTHORITY CHAIN, AND THE ONE RULE THAT MATTERS MOST.
//
//     canonical BC intent          ThermalBoundaryConditionId + FaceName + the
//              |                   physical numbers: T_bar, or h and T_inf
//              |                   (P18-DATA-001, unchanged)
//     P16's GeometryMeshMap        boundaryFacetsOf
//              |
//     current boundary facets      ElementIds of THIS mesh, derived
//              |
//     unique current NodeIds       the corners of those facets, once each
//              |
//     P18-DOF-001's ThermalDofMap  NodeId -> ThermalDofIndex
//              |
//     DERIVED:                     the temperature constraint map; the
//                                  convection facet terms Kconv, Qconv
//
// **A FACET HANDLE, A NODE HANDLE AND A DEGREE-OF-FREEDOM NUMBER ARE NEVER
// BOUNDARY-CONDITION AUTHORITY.** The canonical conditions in `ThermalData.hpp`
// hold a `FaceName` and the physical numbers and nothing else about the mesh;
// everything in this file is rebuilt from them against whatever mesh is current,
// and a remesh invalidates every derived value here.
//
// THE CANONICAL MODEL ALREADY EXISTS. P18-DATA-001 defined
// `ThermalBoundaryCondition`, `PrescribedTemperature`, `ConvectionBoundary` and
// `validate`; P18-DOF-001 defined `ThermalConstraintMap`. This milestone adds no
// canonical type. It adds the resolution, the convection integrals and the
// provenance.
//
// TWO KINDS OF CONDITION, AND THEY COMBINE DIFFERENTLY. That is the easiest
// thing to get wrong, so it is written down once:
//
// ```text
// PRESCRIBED TEMPERATURE   a MATHEMATICAL constraint on an unknown.
//                          Two conditions on one unknown with the SAME value are
//                          one constraint (deduplicated); with DIFFERENT values
//                          they contradict and the whole set is REFUSED. Not the
//                          first, not the last, not the mean.
// CONVECTION               a PHYSICAL exchange with an ambient. Two conditions on
//                          one face are two exchanges and ADD, even if their
//                          numbers are identical. Never deduplicated.
// ```
//
// A prescribed temperature and a convection on the SAME face are allowed. The
// temperature is imposed exactly by the solve; the convection term is still part
// of the physical heat exchange and of the reaction heat balance, so dropping it
// would silently change the heat that leaves the body. (The standard finite
// element treatment; the alternative, refusing the overlap, would forbid a
// perfectly ordinary model.)
//
// NO PENALTY, NO EPSILON ANCHOR. A prescribed temperature is returned as an
// exact constraint (`ThermalConstraintMap`) and nothing is added to any diagonal
// or right-hand side for it; P18-SOLVE-001 reduces the system exactly. A model
// with no prescribed temperature is valid HERE: a convection face with h > 0
// removes the null mode of the conductivity matrix by itself, and whether a
// system is solvable is the solver's question.
//
// NO MATERIAL. Neither the constraints nor the convection terms involve the
// conductivity, so preparing them asks for none and a material edit cannot
// stale them.
//
// ATOMIC. One refusal refuses the whole set: a partially prepared boundary is
// not a smaller model, it is a model that claims to be the user's and is not.
//
// WHAT THIS FILE DOES NOT DO. No global assembly or scatter (P18-ASSEMBLY-001),
// no reduced system and no solve (P18-SOLVE-001), no heat balance
// (P18-BALANCE-001), no persistence, no radiation, no time-varying condition, no
// thermal contact.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalConvection.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::thermal {

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why a set of boundary conditions cannot be turned into constraints and
/// convection terms.
///
/// EVERY VALUE IS REACHABLE and has a test. There is no `Stale` here, because
/// possession of a `ThermalModel` already proves the mesh and mapping are
/// current (ADR-051), and staleness of a PREPARED value is a different question
/// answered by `staleReasons`; and there is no ambiguous or unsupported-target
/// value, because P16's `MappingState` has neither and the canonical condition
/// cannot express a target other than a `FaceName`.
enum class BoundaryProblem : std::uint8_t {
    /// A condition carries an invalid `ThermalBoundaryConditionId`.
    InvalidIdentity,
    /// Two conditions carry the same id. Refused rather than merged: one of them
    /// would be silently ignored.
    DuplicateConditionId,
    /// A prescribed temperature (or an ambient, see below) is not a finite
    /// absolute temperature: NaN, an infinity, or below 0 K.
    TemperatureInvalid,
    /// A convection coefficient is not finite and positive (canonical record) or
    /// finite and non-negative (the integration seam).
    CoefficientInvalid,
    /// A convection ambient is not a finite absolute temperature.
    AmbientInvalid,
    /// The `FaceSelector` is malformed on its own terms.
    TargetInvalid,
    /// The `FaceName` names no face of the body as it is now. NOTHING NEARBY IS
    /// SUBSTITUTED.
    TargetUnresolved,
    /// The target resolved, and the mapping gave it no boundary facet. Not an
    /// empty condition: a model whose attribution chain lost a face.
    TargetWithoutFacets,
    /// A boundary facet's area is zero, negative or not finite, so it cannot
    /// carry a convection term. Never a zero contribution.
    DegenerateFacet,
    /// A facet, node or unknown the mesh or numbering does not have. Never
    /// resolved to a nearby one.
    EntityNotInMesh,
    /// A target list repeats a facet, which would count it twice.
    EntitiesNotUnique,
    /// The numbering is not this mesh's.
    DofMapMismatch,
    /// One unknown is prescribed two different temperatures.
    ConflictingTemperature,
    /// A convection entry or a running quantity overflowed to infinity.
    NonFiniteResult,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(BoundaryProblem problem) noexcept;

/// Two conditions that prescribe different temperatures on shared unknowns.
///
/// ONE RECORD PER PAIR, not one per unknown: two overlapping faces that disagree
/// along a whole edge are one conflict with a count, not thousands of lines.
/// Every unknown is still examined, so no conflicting unknown can hide behind a
/// summary. The pair is ordered by condition id (`first < second`), which makes
/// the diagnostic independent of the order the conditions were listed in.
struct TemperatureConflict {
    ThermalBoundaryConditionId first{};
    ThermalBoundaryConditionId second{};
    /// The temperatures each of the pair asks for at `firstDof`.
    Temperature firstValue{};
    Temperature secondValue{};
    /// How many unknowns the pair disagrees on.
    std::size_t unknownCount = 0;
    /// The lowest-numbered such unknown, a representative.
    ThermalDofIndex firstDof = ThermalDofIndex::fromValue(0);

    friend bool operator==(const TemperatureConflict&, const TemperatureConflict&) = default;
};

/// A refusal with its classification, for the integration layer.
struct BoundaryFailure {
    BoundaryProblem problem = BoundaryProblem::NonFiniteResult;
    Error error{};
    /// Non-empty only for `ConflictingTemperature`, ascending by (first, second).
    std::vector<TemperatureConflict> conflicts{};
};

// ---------------------------------------------------------------------------
// Prescribed temperature: the seams
// ---------------------------------------------------------------------------

/// The unknowns at the corners of @p facets, once each, ascending by
/// `ThermalDofIndex`.
///
/// A mapped face is many triangles sharing nodes, and a constraint is per
/// UNKNOWN: facets (n1 n2 n3) and (n1 n3 n4) constrain n1 n2 n3 n4, four
/// entries and not six. The order is by `ThermalDofIndex`, never by `NodeId`.
///
/// @p facets must be strictly ascending, as P16 delivers them, and non-empty.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<std::vector<ThermalDofIndex>, BoundaryFailure>
uniqueFacetDofs(const meshing::Mesh& mesh, const ThermalDofMap& map,
                std::span<const meshing::ElementId> facets);

/// One condition's wish: this unknown at this temperature.
struct TemperatureRequest {
    ThermalBoundaryConditionId condition{};
    ThermalDofIndex dof = ThermalDofIndex::fromValue(0);
    Temperature value{};

    friend bool operator==(const TemperatureRequest&, const TemperatureRequest&) = default;
};

/// The constraint map the requests amount to, and who asked for each constraint.
struct ResolvedTemperatureConstraints {
    ThermalConstraintMap constraints;
    /// Parallel to `constraints.constrained()`: for each constrained unknown, the
    /// conditions that prescribed it, in the order the requests were given.
    /// Attribution, not mathematics: two conditions asking for the same
    /// temperature on one unknown appear together here and as ONE constraint in
    /// the map.
    std::vector<std::vector<ThermalBoundaryConditionId>> sources;
};

/// Reduces @p requests to a constraint map, or says why not.
///
/// THE SEAM THE TESTS CALL DIRECTLY, with hand-made requests: an unknown past the
/// end of the numbering, a temperature below 0 K, or two conditions that disagree
/// can be presented to it without a mesh. The mathematics is P18-DOF-001's
/// `buildThermalConstraintMap` (sort by unknown, collapse exact duplicates, refuse
/// a contradiction, order-independent); this adds ATTRIBUTION and the structured
/// conflict report.
///
/// An empty request list is accepted and gives an empty map.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<ResolvedTemperatureConstraints, BoundaryFailure>
resolveTemperatureConstraints(const ThermalDofMap& map, std::span<const TemperatureRequest> requests);

// ---------------------------------------------------------------------------
// Convection: the seam
// ---------------------------------------------------------------------------

/// One boundary facet's convection term, bound to the unknowns of its corners.
///
/// `dofs[i]` is the unknown of the triangle's corner `i` AS P16 STORES IT, and
/// row and source `i` of `term` belong to it: nothing is sorted, so a later
/// scatter cannot pair a row with the wrong unknown.
struct ConvectionFacet {
    meshing::ElementId facet{};
    std::array<ThermalDofIndex, kTri3Nodes> dofs{ThermalDofIndex::fromValue(0), ThermalDofIndex::fromValue(0),
                                                 ThermalDofIndex::fromValue(0)};
    Tri3Convection term;

    friend bool operator==(const ConvectionFacet&, const ConvectionFacet&) = default;
};

/// The convection terms of @p facets for a constant @p coefficient and
/// @p ambient, one per facet, in the order given.
///
/// THE SEAM THE TESTS CALL DIRECTLY, with a hand-built mesh: a zero-area
/// triangle, a sparse `NodeId`, a repeated facet or a foreign numbering can be
/// presented to it. `prepareThermalBoundary` is the same code driven by P16's
/// resolution.
///
/// Total over `h >= 0`: a zero coefficient gives all-zero terms. (The canonical
/// record refuses zero; see `validate(ThermalBoundaryCondition)`.) Nothing is
/// scattered: the terms are returned and the assembler adds them, so there is no
/// global matrix here to overwrite.
///
/// @p facets must be strictly ascending and non-empty.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<std::vector<ConvectionFacet>, BoundaryFailure>
integrateConvection(const meshing::Mesh& mesh, const ThermalDofMap& map,
                    std::span<const meshing::ElementId> facets, HeatTransferCoefficient coefficient,
                    Temperature ambient);

// ---------------------------------------------------------------------------
// Prepared boundary
// ---------------------------------------------------------------------------

/// What one prescribed-temperature condition resolved to. DERIVED.
struct PreparedTemperatureBoundary {
    ThermalBoundaryConditionId condition{};
    Temperature value{};
    /// The boundary facets of its face on this mesh.
    std::size_t facetCount = 0;
    /// The unknowns it constrains, ascending, each once.
    std::vector<ThermalDofIndex> dofs{};

    friend bool operator==(const PreparedTemperatureBoundary&, const PreparedTemperatureBoundary&) = default;
};

/// What one convection condition resolved to. DERIVED.
///
/// EXPORTED, because it is the one plain struct here with member functions defined
/// in the library (`area`, `conductance`, `source`). A static build links them
/// without an annotation; a shared build cannot find them without one, and the
/// first qualification run of this milestone failed exactly there
/// (debug-shared-ext: "undefined reference to PreparedConvectionBoundary::area").
/// Every other struct in this file is an aggregate with no out-of-line member.
struct BETTERCAD_THERMAL_EXPORT PreparedConvectionBoundary {
    ThermalBoundaryConditionId condition{};
    HeatTransferCoefficient coefficient{};
    Temperature ambient{};
    /// One term per boundary facet of its face, in P16's ascending facet order.
    std::vector<ConvectionFacet> facets{};

    /// The resolved area, summed from the facets every time. DERIVED, so it
    /// cannot disagree with them, and never a canonical value.
    [[nodiscard]] Area area() const noexcept;
    /// The sum of every `Kconv` entry of every facet: `h A`, in W/K.
    [[nodiscard]] ThermalConductance conductance() const noexcept;
    /// The sum of every `Qconv` entry of every facet: `h T_inf A`, in W.
    [[nodiscard]] Power source() const noexcept;

    friend bool operator==(const PreparedConvectionBoundary&, const PreparedConvectionBoundary&) = default;
};

/// The identity of everything a prepared boundary was computed from.
///
/// A VALUE, NOT A HASH: no pointer, no address, no timestamp. Equality is
/// field-complete by `= default`, so a dependency added here without being
/// compared is not possible, and `staleReasons` can say WHICH moved.
///
/// ```text
/// body        which solid
/// mesh        WHICH MESH AND WHICH GENERATION. A remesh of the same body with
///             the same counts and the same NodeIds has a different stamp, and
///             that alone makes a prepared boundary stale
/// nodeCount   a stamp identifies the BUILDER and not the snapshot (the lesson
///             of MeshDofMap::describes), so the count is compared as well
/// geometry    the geometry revision the mapping resolved against
/// conditions  the canonical conditions it was prepared FROM, by value
/// ```
///
/// WHAT IS DELIBERATELY ABSENT: the material, the loads, the solver settings, the
/// mode, the time step and the analysis revision. A constraint or a convection
/// term depends on none of them; the analysis revision moves on ANY edit and
/// would stale this value for a load or tolerance edit that cannot change it. The
/// thermal RESULT goes stale on those through `ThermalResultSource`, a separate
/// gate.
struct PreparedThermalBoundarySource {
    ObjectId body{};
    meshing::MeshStamp mesh{};
    std::size_t nodeCount = 0;
    meshing::GeometryRevision geometry{};
    std::vector<ThermalBoundaryCondition> conditions{};

    friend bool operator==(const PreparedThermalBoundarySource&, const PreparedThermalBoundarySource&) = default;
};

/// Which part of a prepared boundary's source no longer matches.
enum class BoundaryStaleReason : std::uint8_t {
    Body,
    Mesh,
    Geometry,
    Conditions,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(BoundaryStaleReason reason) noexcept;

/// Every field of @p prepared that differs from @p current, in the enum's order.
/// Empty when the two are equal.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<BoundaryStaleReason>
staleReasons(const PreparedThermalBoundarySource& prepared, const PreparedThermalBoundarySource& current);

/// The source @p conditions would be prepared against on @p model, right now.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT PreparedThermalBoundarySource
currentBoundarySource(const ThermalModel& model, std::span<const ThermalBoundaryCondition> conditions);

/// The constraints and convection terms a set of boundary conditions produces on
/// one current mesh.
///
/// POSSESSION IS THE EVIDENCE that every condition resolved, every value was
/// valid, no two prescriptions contradicted and the whole set was prepared
/// against the mesh `source()` names. There is no public constructor and one
/// friend, so a consumer cannot be handed a partially prepared boundary.
///
/// DERIVED AND DISPOSABLE. Bound to one mesh generation and one list of
/// conditions; rebuilt after a remesh or any condition edit; never persisted,
/// never a member of anything canonical. `P18-PERSIST-001` stores the canonical
/// conditions instead.
///
/// STORAGE SCALES WITH THE BOUNDARY, not with the largest `NodeId`: constraints
/// are keyed by `ThermalDofIndex` and convection terms by facet.
class BETTERCAD_THERMAL_EXPORT PreparedThermalBoundary {
public:
    [[nodiscard]] const PreparedThermalBoundarySource& source() const noexcept { return source_; }

    /// The prescribed temperatures, ascending by unknown and without repeats.
    [[nodiscard]] const ThermalConstraintMap& constraints() const noexcept { return *constraints_; }

    /// The conditions that prescribed `constraints().constrained()[index]`.
    [[nodiscard]] std::span<const ThermalBoundaryConditionId> constraintSources(std::size_t index) const noexcept;

    /// Each prescribed-temperature condition, in the order given.
    [[nodiscard]] std::span<const PreparedTemperatureBoundary> temperatures() const noexcept {
        return temperatures_;
    }

    /// Each convection condition, in the order given. Overlapping conditions are
    /// separate entries: they ADD, they are not merged.
    [[nodiscard]] std::span<const PreparedConvectionBoundary> convections() const noexcept {
        return convections_;
    }

    /// The area under convection, summed over conditions (a face carrying two
    /// counts twice, as the exchange does).
    [[nodiscard]] Area convectionArea() const noexcept;
    /// `sum h A`, in W/K.
    [[nodiscard]] ThermalConductance convectionConductance() const noexcept;
    /// `sum h T_inf A`, in W.
    [[nodiscard]] Power convectionSource() const noexcept;

    /// Whether this boundary's numbering is @p map's: the stamp AND the count.
    [[nodiscard]] bool describes(const ThermalDofMap& map) const noexcept;
    /// Whether this boundary was prepared against @p mesh: the mesh owns the
    /// stamp AND the node count agrees.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept;

    friend bool operator==(const PreparedThermalBoundary&, const PreparedThermalBoundary&) = default;

private:
    PreparedThermalBoundary() = default;

    friend BETTERCAD_THERMAL_EXPORT Result<PreparedThermalBoundary>
    prepareThermalBoundary(const ThermalModel& model, const ThermalDofMap& map,
                           std::span<const ThermalBoundaryCondition> conditions);

    PreparedThermalBoundarySource source_{};
    /// Engaged from construction on. An optional only because ThermalConstraintMap
    /// has no public default constructor: a constraint map exists only if it was
    /// validated against a numbering.
    std::optional<ThermalConstraintMap> constraints_{};
    std::vector<std::vector<ThermalBoundaryConditionId>> constraintSources_{};
    std::vector<PreparedTemperatureBoundary> temperatures_{};
    std::vector<PreparedConvectionBoundary> convections_{};
};

/// Turns @p conditions into constraints and convection terms on @p model's
/// current mesh.
///
/// ATOMIC, READ-ONLY and DETERMINISTIC. Conditions are processed in the order
/// given (the definition's canonical order); facets come from P16's ascending
/// deduplicated set; corners in their stored order; constraints are sorted by
/// unknown. Nothing is built from an unordered container, so the same inputs
/// give the same bits in every build configuration.
///
/// The checks, in this order: the numbering is this mesh's; no identity is
/// invalid or repeated; every value is valid (the CANONICAL rule, which for a
/// convection coefficient is strictly positive); every target resolves and
/// integrates; no two prescriptions contradict.
///
/// IT TAKES A `ThermalModel`, which answers the stale-input question without a
/// check here: possession proves the geometry and mesh are current. It takes the
/// `ThermalDofMap` as an argument rather than building one, so a caller holding a
/// numbering for a DIFFERENT mesh is refused (`DofMapMismatch`) instead of
/// silently renumbered.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<PreparedThermalBoundary>
prepareThermalBoundary(const ThermalModel& model, const ThermalDofMap& map,
                       std::span<const ThermalBoundaryCondition> conditions);

/// The conditions of @p definition, prepared. A convenience: it reads
/// `definition.boundaryConditions` and nothing else, so a load, solver, mode or
/// initial-condition edit cannot change the answer.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<PreparedThermalBoundary>
prepareThermalBoundary(const ThermalModel& model, const ThermalDofMap& map,
                       const ThermalAnalysisDefinition& definition);

/// The full refusal `prepareThermalBoundary` would report (classification, error
/// and any structured conflicts), or none if it would succeed. The same checks in
/// the same order, for a caller that wants to present or count the reason rather
/// than parse a message.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<BoundaryFailure>
thermalBoundaryFailure(const ThermalModel& model, const ThermalDofMap& map,
                       std::span<const ThermalBoundaryCondition> conditions);

/// Just the classification of `thermalBoundaryFailure`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<BoundaryProblem>
thermalBoundaryProblem(const ThermalModel& model, const ThermalDofMap& map,
                       std::span<const ThermalBoundaryCondition> conditions);

/// Whether @p prepared still describes @p model and @p conditions. A consumer
/// calls this (or `requireCurrent`) BEFORE assembly; currentness is never
/// inferred from the size of anything.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool isCurrent(const PreparedThermalBoundary& prepared,
                                                      const ThermalModel& model,
                                                      std::span<const ThermalBoundaryCondition> conditions);

/// `Ok` if @p prepared is current, otherwise a `FailedPrecondition` naming each
/// reason that moved.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
requireCurrent(const PreparedThermalBoundary& prepared, const ThermalModel& model,
               std::span<const ThermalBoundaryCondition> conditions);

} // namespace bettercad::thermal
