#pragma once

// P18-LOAD-001 -- turning canonical heat-load intent into a current-mesh nodal
// heat input.
//
// THE AUTHORITY CHAIN, AND THE ONE RULE THAT MATTERS MOST.
//
//     canonical load intent          ThermalLoadId + FaceName + q'' / q'''
//              |                     (P18-DATA-001, unchanged)
//     P16's GeometryMeshMap          boundaryFacetsOf / regionOf
//              |
//     current facets / elements      ElementIds of THIS mesh, derived
//              |
//     P18-DOF-001's ThermalDofMap    NodeId -> ThermalDofIndex
//              |
//     nodal heat input Q [W]         one entry per unknown, derived
//
// **A FACET HANDLE, A NODE HANDLE AND AN ELEMENT HANDLE ARE NEVER LOAD
// AUTHORITY.** A `FaceName` is canonical and survives a remesh; the handles are
// mesh-local and a remesh invalidates every one. So the canonical loads in
// `ThermalData.hpp` hold a `FaceName` and a physical value and nothing else
// about the mesh, and everything in this file is rebuilt from them against
// whatever mesh is current.
//
// THE CANONICAL MODEL ALREADY EXISTS. P18-DATA-001 defined `ThermalLoad`,
// `SurfaceHeatFluxLoad`, `VolumetricHeatSourceLoad` and `validate`. This
// milestone adds no canonical type; it adds the resolution and the integration
// P18-DATA-001 said belonged here.
//
// SIGN, FROZEN BY P18-DATA-001 AND OBEYED: POSITIVE IS HEAT INTO THE BODY,
// positive generation is heat generated, negative is extraction or a sink, and
// zero is a valid load. The sign is a property of the SCALAR, so no normal is
// computed and a triangle's winding cannot change a contribution.
//
// THE TWO INTEGRALS ARE EXACT, because the load is constant over its target and
// a linear triangle and a linear tetrahedron integrate their shape functions in
// closed form:
//
//     surface   Qi = q'' A / 3      per facet corner, sum = q'' A
//     volume    Qi = q''' V / 4     per element corner, sum = q''' V
//
// P17 DOES NOT CLASSIFY GEOMETRY AND NEITHER DOES THIS. Which triangles lie on
// which CAD face is P16-MAP-001's; this asks `boundaryFacetsOf` and reads the
// answer. No nearest face, no distance to a plane, no centroid test.
//
// ATOMIC. One refusal refuses the whole set: a partially prepared vector is not
// a smaller load case, it is a load case that claims to be the user's and is
// not.
//
// WHAT THIS FILE DOES NOT DO. No convection and no prescribed temperature
// (P18-BC-001), no global assembly (P18-ASSEMBLY-001), no solve, no transient
// policy, no heat balance (P18-BALANCE-001), no persistence, no material
// lookup. A prescribed heat input needs no conductivity, density or specific
// heat, and preparing one asks for none.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Quantity.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace bettercad::thermal {

// ---------------------------------------------------------------------------
// Units, checked by the compiler
// ---------------------------------------------------------------------------

/// The dimensional reasoning of the two integrals.
///
/// ```text
///     q'' A     W/m^2 . m^2  =  W        each facet corner receives A/3 of it
///     q''' V    W/m^3 . m^3  =  W        each element corner receives V/4 of it
/// ```
///
/// and a nodal entry of `Q` is therefore a `Power`, which is what makes
/// `K [W/K] T [K] = Q [W]` and `C [J/K] dT/dt [K/s] = W` agree.
static_assert(decltype(HeatFlux{} * Area{})::dimension == dimensions::power,
              "q'' A must come out in W");
static_assert(decltype(VolumetricHeatSource{} * Volume{})::dimension == dimensions::power,
              "q''' V must come out in W");
/// And a heat flux is not a volumetric source, nor either of them a power.
static_assert(HeatFlux::dimension != VolumetricHeatSource::dimension);
static_assert(HeatFlux::dimension != Power::dimension);
static_assert(VolumetricHeatSource::dimension != Power::dimension);

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Why a set of heat loads cannot be turned into a nodal heat input.
///
/// EVERY VALUE IS REACHABLE and has a test, which is the discipline P17 applied
/// when it deleted three unreachable values from its own first draft. Two the
/// brief mentions are deliberately absent: there is no `Stale` here, because
/// possession of a `ThermalModel` already proves the mesh and mapping are
/// current (ADR-051), and staleness of a PREPARED value is a different
/// question answered by `staleReasons`; and there is no ambiguous or
/// unsupported-target value, because P16's `MappingState` has neither and the
/// canonical load cannot express a target other than a `FaceName`.
enum class LoadProblem : std::uint8_t {
    /// Two loads carry the same `ThermalLoadId`. Refused rather than resolved:
    /// one of them would be silently ignored, and which is not this layer's to
    /// choose.
    DuplicateLoadId,
    /// `q''` or `q'''` is NaN or infinite. Refused before any integration, so
    /// nothing non-finite can reach the vector.
    NonFiniteValue,
    /// The `FaceSelector` is malformed on its own terms (core's own
    /// `validate(FaceSelector)`).
    TargetInvalid,
    /// The `FaceName` names no face of the body as it is now. This is also
    /// where a drilled hole's unnamed cylindrical wall lands. NOTHING NEARBY IS
    /// SUBSTITUTED.
    TargetUnresolved,
    /// The target resolved, and the mapping gave it no boundary facet. Not a
    /// zero load: a model whose attribution chain lost a face.
    TargetWithoutFacets,
    /// A volumetric load, and the mapped body holds no Tet4 element. Not a zero
    /// generation: there is no body to generate in.
    BodyWithoutElements,
    /// A boundary facet's area is zero, negative or not finite. P16 refuses a
    /// degenerate triangle, so this is a defensive result check and not an
    /// invented input tolerance.
    DegenerateFacet,
    /// A tetrahedron is inverted, degenerate or non-finite, by P18-ELEM-001's
    /// geometry gate. Never repaired: no absolute value rescues an inverted
    /// element's volume.
    InvalidElement,
    /// A facet, element or node the mesh does not have. Never resolved to a
    /// nearby one.
    EntityNotInMesh,
    /// A target list repeats an entity, which would count it twice. P16
    /// delivers its sets ascending and without repeats, and this refuses a list
    /// that is not.
    EntitiesNotUnique,
    /// The numbering is not this mesh's, or the output vector is not `Ndof`
    /// long, so the entries would be indexed by the wrong thing.
    DofMapMismatch,
    /// A nodal share or a running sum overflowed to infinity.
    NonFiniteResult,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(LoadProblem problem) noexcept;

/// A refusal with its classification, for the integration layer.
struct LoadFailure {
    LoadProblem problem = LoadProblem::NonFiniteResult;
    Error error{};
};

// ---------------------------------------------------------------------------
// The integration layer
// ---------------------------------------------------------------------------

/// What one load contributed, integrated over its resolved target.
///
/// DERIVED, NEVER AUTHORITY. Computed from the load's canonical value and the
/// current mesh; nothing reads it back as intent.
struct IntegratedHeat {
    /// Boundary facets or tetrahedra integrated over.
    std::size_t entities = 0;
    /// The area integrated over, for a surface load; zero otherwise.
    Area area{};
    /// The volume integrated over, for a volumetric load; zero otherwise.
    Volume volume{};
    /// The heat rate this load adds, in W: `q'' A` or `q''' V`, formed as the sum
    /// of exactly the shares added to the vector, in the order they were added.
    Power heatRate{};

    friend bool operator==(const IntegratedHeat&, const IntegratedHeat&) = default;
};

/// The equivalent nodal heat each corner of a facet receives from a constant
/// flux over it: `q'' A / 3`.
///
/// The consistent load vector for a three-node linear triangle under a constant
/// load is `integral(N^T q'' dA)`, and each shape function integrates to `A/3`
/// over its own triangle, so the three corners receive equal thirds that sum to
/// `q'' A` exactly. NOT `q'' A` per node, and NOT `q'' A / 4`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Power facetNodalHeat(Area area, HeatFlux flux) noexcept;

/// The equivalent nodal heat each corner of a tetrahedron receives from a
/// constant generation in it: `q''' V / 4`.
///
/// `integral(N^T q''' dV)` with each shape function integrating to `V/4`. NOT
/// `q''' V / 3`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Power elementNodalHeat(Volume volume,
                                                              VolumetricHeatSource generation) noexcept;

/// Adds the heat a constant flux puts through @p facets into @p heatInput.
///
/// THE SEAM THE TESTS CALL DIRECTLY, with a hand-built mesh: it needs no
/// `ThermalModel`, so a zero-area triangle, a sparse `NodeId`, a repeated
/// entity or a foreign numbering can be presented to it. `prepareThermalLoads`
/// is the same code driven by P16's resolution.
///
/// @p facets must be strictly ascending, as P16 delivers them. @p heatInput is
/// indexed by `ThermalDofIndex` and must be `map.dofCount()` long; entries are
/// ADDED to, never assigned, so a node shared by several facets accumulates.
/// On failure @p heatInput may be partly written and must be discarded; the
/// caller owns atomicity.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<IntegratedHeat, LoadFailure>
integrateSurfaceHeatFlux(const meshing::Mesh& mesh, const ThermalDofMap& map,
                         std::span<const meshing::ElementId> facets, HeatFlux flux,
                         std::span<Power> heatInput);

/// Adds the heat a constant generation produces in @p elements into
/// @p heatInput. The same contract as `integrateSurfaceHeatFlux`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::expected<IntegratedHeat, LoadFailure>
integrateVolumetricHeatSource(const meshing::Mesh& mesh, const ThermalDofMap& map,
                              std::span<const meshing::ElementId> elements,
                              VolumetricHeatSource generation, std::span<Power> heatInput);

// ---------------------------------------------------------------------------
// Prepared loads
// ---------------------------------------------------------------------------

/// What one canonical load contributed to a prepared vector.
struct ThermalLoadContribution {
    ThermalLoadId load{};
    ThermalLoadKind kind = ThermalLoadKind::SurfaceHeatFlux;
    IntegratedHeat integrated{};

    friend bool operator==(const ThermalLoadContribution&, const ThermalLoadContribution&) = default;
};

/// The identity of everything a prepared heat input was computed from.
///
/// A VALUE, NOT A HASH: no pointer, no address, no timestamp. Equality is
/// field-complete by `= default`, so a dependency added here without being
/// compared is not possible, and `staleReasons` can say WHICH moved.
///
/// ```text
/// body        which solid
/// mesh        WHICH MESH AND WHICH GENERATION. A remesh of the same body with
///             the same counts and the same NodeIds has a different stamp, and
///             that alone makes a prepared load stale
/// nodeCount   a stamp identifies the BUILDER and not the snapshot (the lesson
///             of MeshDofMap::describes), so the count is compared as well
/// geometry    the geometry revision the mapping resolved against
/// loads       the canonical loads it was prepared FROM, by value
/// ```
///
/// WHAT IS DELIBERATELY ABSENT: the material, the solver settings, the mode,
/// the time step, every boundary condition, and the analysis revision. A
/// prescribed `q''` or `q'''` depends on none of them; the analysis revision
/// moves on ANY edit to the definition and would stale this value for a
/// convection or tolerance edit that cannot change it. The load LIST by value
/// is exactly the dependency the physics has. The thermal RESULT goes stale on
/// a material edit through `ThermalResultSource`, a separate gate.
struct PreparedThermalLoadsSource {
    ObjectId body{};
    meshing::MeshStamp mesh{};
    std::size_t nodeCount = 0;
    meshing::GeometryRevision geometry{};
    std::vector<ThermalLoad> loads{};

    friend bool operator==(const PreparedThermalLoadsSource&, const PreparedThermalLoadsSource&) = default;
};

/// Which part of a prepared load's source no longer matches.
enum class LoadStaleReason : std::uint8_t {
    Body,
    Mesh,
    Geometry,
    Loads,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(LoadStaleReason reason) noexcept;

/// Every field of @p prepared that differs from @p current, in the enum's
/// order. Empty when the two are equal.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::vector<LoadStaleReason>
staleReasons(const PreparedThermalLoadsSource& prepared, const PreparedThermalLoadsSource& current);

/// The source @p loads would be prepared against on @p model, right now.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT PreparedThermalLoadsSource
currentLoadSource(const ThermalModel& model, std::span<const ThermalLoad> loads);

/// The nodal heat input a set of loads produces on one current mesh.
///
/// POSSESSION IS THE EVIDENCE that every load resolved, every value was finite
/// and the whole set was integrated against the mesh `source()` names. There is
/// no public constructor and one friend, so a consumer cannot be handed a
/// partially prepared vector.
///
/// DERIVED AND DISPOSABLE. Bound to one mesh generation and one list of loads;
/// rebuilt after a remesh or any load edit; never persisted, never a member of
/// anything canonical. `P18-PERSIST-001` stores the canonical loads instead.
///
/// INDEXED BY `ThermalDofIndex`, and exactly `Ndof` long: a node's entry is at
/// `map.dofOf(node)`, never at `node.value()`, and the length is the number of
/// nodes and not the largest handle.
class BETTERCAD_THERMAL_EXPORT PreparedThermalLoads {
public:
    [[nodiscard]] const PreparedThermalLoadsSource& source() const noexcept { return source_; }

    /// `Ndof`: one entry per temperature unknown.
    [[nodiscard]] ThermalDofIndex::ValueType dofCount() const noexcept {
        return static_cast<ThermalDofIndex::ValueType>(heatInput_.size());
    }

    /// `Q`, in W, indexed by `ThermalDofIndex::value()`.
    [[nodiscard]] std::span<const Power> heatInput() const noexcept { return heatInput_; }

    /// The entry of @p dof, or nullopt for an unknown this vector does not have.
    [[nodiscard]] std::optional<Power> heatInputAt(ThermalDofIndex dof) const noexcept;

    /// What each load contributed, in the order the loads were given.
    [[nodiscard]] std::span<const ThermalLoadContribution> contributions() const noexcept {
        return contributions_;
    }

    /// The total heat rate of the vector: the sum of its entries, in index
    /// order, which is what makes the answer reproducible. DERIVED from the
    /// entries and never stored beside them, so it cannot disagree.
    [[nodiscard]] Power totalHeatRate() const noexcept;

    /// The heat applied through faces, summed over the per-load totals in load
    /// order. Kept apart from generation so P18-BALANCE-001 can attribute them.
    [[nodiscard]] Power appliedSurfaceHeatRate() const noexcept;

    /// The heat generated in the volume, likewise.
    [[nodiscard]] Power volumetricGenerationRate() const noexcept;

    /// Whether this vector's numbering is @p map's: the stamp AND the count.
    [[nodiscard]] bool describes(const ThermalDofMap& map) const noexcept;

    /// Whether this vector was prepared against @p mesh: the mesh owns the
    /// stamp AND the node count agrees.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept;

    friend bool operator==(const PreparedThermalLoads&, const PreparedThermalLoads&) = default;

private:
    PreparedThermalLoads() = default;

    friend BETTERCAD_THERMAL_EXPORT Result<PreparedThermalLoads>
    prepareThermalLoads(const ThermalModel& model, const ThermalDofMap& map,
                        std::span<const ThermalLoad> loads);

    PreparedThermalLoadsSource source_{};
    std::vector<Power> heatInput_{};
    std::vector<ThermalLoadContribution> contributions_{};
};

/// Turns @p loads into a nodal heat input on @p model's current mesh.
///
/// ATOMIC, READ-ONLY and DETERMINISTIC. Loads are processed in the order given
/// (the definition's canonical order); facets come from P16's ascending
/// deduplicated set; corners in their stored order; the sum is formed serially
/// into a dense `Ndof` vector. Nothing is built from an unordered container, so
/// the same inputs give the same vector, bit for bit, in every build.
///
/// SUPERPOSITION IS THE SEMANTICS. Two loads on one face, or a surface load and
/// a volumetric load reaching the same node, ADD. Overlapping targets are not an
/// error and are not deduplicated by target.
///
/// IT TAKES A `ThermalModel`, which answers the stale-input question without a
/// check here: possession proves the geometry and mesh are current and that the
/// mapping and mesh came from one lookup. It takes the `ThermalDofMap` as an
/// argument rather than building one, so a caller holding a numbering for a
/// DIFFERENT mesh is refused (`DofMapMismatch`) instead of silently renumbered.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<PreparedThermalLoads>
prepareThermalLoads(const ThermalModel& model, const ThermalDofMap& map,
                    std::span<const ThermalLoad> loads);

/// The loads of @p definition, prepared. A convenience: it reads
/// `definition.loads` and nothing else, so a boundary-condition, solver, mode or
/// initial-condition edit cannot change the answer.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<PreparedThermalLoads>
prepareThermalLoads(const ThermalModel& model, const ThermalDofMap& map,
                    const ThermalAnalysisDefinition& definition);

/// The problem `prepareThermalLoads` would report, or none if it would
/// succeed. The same checks in the same order, for a caller that wants to
/// present or count the reason rather than parse a message.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<LoadProblem>
thermalLoadProblem(const ThermalModel& model, const ThermalDofMap& map,
                   std::span<const ThermalLoad> loads);

/// Whether @p prepared still describes @p model and @p loads. A consumer calls
/// this (or `requireCurrent`) BEFORE assembly; currentness is never inferred
/// from the vector's length.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool isCurrent(const PreparedThermalLoads& prepared,
                                                      const ThermalModel& model,
                                                      std::span<const ThermalLoad> loads);

/// `Ok` if @p prepared is current, otherwise a `FailedPrecondition` naming each
/// reason that moved.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
requireCurrent(const PreparedThermalLoads& prepared, const ThermalModel& model,
               std::span<const ThermalLoad> loads);

} // namespace bettercad::thermal
