#pragma once

// P19-ARCH-001 -- the boundary a CFD analysis is prepared at.
//
// WHAT THIS FILE IS FOR. P16 records a limitation it could not close on its own:
// nothing FORCES the holder of a mesh to ask whether it is stale. `Mesher::mesh()`
// hands back a stale mesh deliberately -- "it is old, not wrong", because the
// viewer inspects stale meshes -- so a function taking `const VolumeMesh&` cannot
// tell a current mesh from one describing a body the user has since changed. P17
// closed that gap for a structural solver with one device, possession is the proof
// (ADR-036), P18 adopted it for conduction (ADR-051), and a finite-volume solver
// that took a mesh would reopen it a third time -- worse, because every face area,
// every owner/neighbour pair and every flux of such a solver indexes into
// whatever mesh it was built from. So this module does not take a mesh either.
//
// It takes a document, a regenerator, a mesher and a control, and asks. The answer
// is a `CfdModel`, and POSSESSION OF ONE IS THE EVIDENCE: it has no public
// constructor, and the only thing that can make one is `requireCfdModel`, which
// checks every prerequisite first. The checks are P16's and P15's, delegated and
// not reimplemented, exactly as `requireThermalModel` delegates them -- and
// deliberately NOT shared with it: cfd (layer 42) sits below thermal (45) and
// structural (50) so that a pressure load or a heat-transfer coefficient can be
// read from above (ADR-059).
//
// WHAT IT IS NOT. There is no cell, no face, no owner/neighbour topology, no
// field, no boundary condition, no matrix, no pressure-velocity coupling and no
// solve. Those are P19-TOPO-001, P19-GEOM-001, P19-DOF-001, P19-BC-001,
// P19-MOM-001 .. P19-STEADY-001. This milestone defines where the gate is and
// makes it impossible to walk around; the checks it cannot perform yet are the
// ones whose canonical types do not exist, and they are named below rather than
// stubbed.
//
// See ADR-058 (what the solver is), ADR-059 (the module and its layer) and ADR-060
// (this boundary, and the fluid as a P15 material).

#include <bettercad/cfd/CfdFluid.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::cfd {

/// Why a CFD analysis cannot be prepared from a document as it stands.
///
/// THE REASON IS PART OF THE ANSWER, following `MeshCurrency`,
/// `GeometryIneligibility`, `structural::InputProblem` and `thermal::InputProblem`:
/// "cannot solve" tells a user nothing they can act on, while "the geometry has
/// changed since the mesh was built" tells them to remesh.
///
/// EVERY VALUE HERE IS REACHABLE. A mesh that fails P16's structural verdict is
/// never HELD, so "the held mesh is structurally invalid" cannot occur; a held
/// mesh's `GeometryMeshMap` is a member of the same `Held` struct, so "no mapping"
/// cannot occur either. This boundary takes mesh, map and quality from one lookup,
/// which is why a caller cannot mismatch them.
///
/// WHAT IS NOT HERE YET, and why. A boundary condition that does not resolve, a
/// mesh whose finite-volume quality a CFD analysis should refuse, prescribed
/// fluxes that do not balance, a system that is not well posed, and a time step
/// that is not positive are all real input problems. P19-DATA-001 defined the
/// canonical records some of them are about (the boundary conditions, the
/// reference pressure, `pressureDatumProblem`, the transient settings, whose own
/// validation already refuses a non-positive step; a missing pressure datum
/// already makes an analysis InputsUnavailable); the checks against a document and
/// a mesh belong to `P19-BC-001`, `P19-MESH-001`, `P19-VALID-001` and
/// `P19-TRANSIENT-001`. A fluid with no dynamic viscosity IS here
/// (`FluidViscosityUnusable`, P19-FLUID-001).
enum class InputProblem : std::uint8_t {
    /// The document has no mesh control with that id.
    ControlNotFound,
    /// The body the control names cannot be meshed at all, so it cannot be
    /// analysed either. The reason comes from `meshing::geometryIneligibility`
    /// unchanged -- including `ConfigurationOverrideActive`.
    GeometryIneligible,
    /// Nothing has been meshed for this control yet.
    NoMesh,
    /// A mesh is held, but it does not describe the document as it is now: the
    /// geometry moved, or the meshing intent did. P16 distinguishes the two and the
    /// diagnostic carries which.
    MeshStale,
    /// The most recent generation attempt failed. Any mesh still held is stale by
    /// definition, and reporting it as merely stale would hide that the attempt to
    /// replace it failed.
    MeshGenerationFailed,
    /// The document names no material, or names one that is gone. A CFD analysis
    /// without a fluid is not a solve with air: ADR-028 forbids a solver holding any
    /// material data of its own, so there is nothing to fall back to and nothing
    /// should be.
    NoMaterialAssigned,
    /// A material resolves, but it has no usable density. P15 decides that, not
    /// P19, and its diagnostic names the material -- and the viscosity too, when
    /// that is missing as well.
    FluidDensityUnusable,
    /// The density is usable, the dynamic viscosity is not: absent, or not a usable
    /// value (P19-FLUID-001). P15 decides, through
    /// `requireIncompressibleFlowProperties`. Appended, so every earlier problem
    /// keeps its position.
    FluidViscosityUnusable,
};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(InputProblem problem) noexcept;

/// A CFD analysis input that has been validated, and whose existence is the proof.
///
/// There is no public constructor and no setter. The only way to obtain one is
/// `requireCfdModel`, so a function taking a `const CfdModel&` does not need to
/// re-check anything, and a function that wants to skip the checks cannot construct
/// its argument. The device is `VolumeMesh`'s, `StructuralModel`'s and
/// `ThermalModel`'s, chosen a fourth time because it turns a rule a reviewer must
/// remember into one the compiler enforces.
///
/// WHAT POSSESSION PROVES, at the moment of construction:
///
/// ```text
/// the control exists
/// the body is eligible -- regenerated, current, a solid, non-empty, valid,
///   and not behind a configuration override
/// a mesh is held for the control and its currency is Current
/// the mapping and the quality report came from that same mesh (three accessors
///   keyed by the same control, read from one held entry)
/// the document's material resolves and yields a usable density AND a usable
///   dynamic viscosity (P19-FLUID-001), held as a `CfdFluid`
/// ```
///
/// WHAT IT DOES NOT PROVE. That the mesh is good enough for a finite-volume method (`P19-MESH-001`,
/// `P19-VALID-001`), that boundary conditions resolve (`P19-BC-001`), that the
/// system is well posed, that a solve converged or that mass is conserved. Those
/// gates land in a STAGED validation entry that runs after this boundary
/// (ADR-053's shape), not inside it. Nor that the material IS a fluid: a fluid is
/// a material that carries a viscosity, and there is no other marker.
///
/// BORROWED, NOT OWNED, AND MOVE-ONLY. The mesh, mapping and quality report belong
/// to the `Mesher`. A `CfdModel` is a snapshot of a validated state and must not
/// outlive it, nor survive an edit. Re-preparing is a few lookups and one material
/// resolution, and re-preparing rather than holding is the point.
class BETTERCAD_CFD_EXPORT CfdModel {
public:
    CfdModel(const CfdModel&) = delete;
    CfdModel& operator=(const CfdModel&) = delete;
    CfdModel(CfdModel&&) noexcept = default;
    CfdModel& operator=(CfdModel&&) noexcept = default;
    ~CfdModel() = default;

    /// The feature whose body is the fluid region.
    [[nodiscard]] ObjectId body() const noexcept { return body_; }

    /// The control whose intent produced the mesh.
    [[nodiscard]] MeshControlId control() const noexcept { return control_; }

    /// The validated Tet4 volume mesh. Current as of construction. In a
    /// finite-volume reading a cell is one of its tetrahedra (ADR-058); the
    /// topology is DERIVED from it by `P19-TOPO-001` and never held here.
    [[nodiscard]] const meshing::VolumeMesh& mesh() const noexcept { return *mesh_; }

    /// The CAD-face to boundary-facet mapping for that mesh. This is how an inlet,
    /// an outlet or a wall will reach boundary faces: by naming a CAD face and
    /// resolving it now, never by holding a face index (ADR-032).
    [[nodiscard]] const meshing::GeometryMeshMap& map() const noexcept { return *map_; }

    /// P16's quality measurements for that mesh -- the MEASUREMENTS, not a verdict.
    /// `P19-VALID-001` owns the CFD acceptance policy and reads `summaries`.
    [[nodiscard]] const meshing::MeshQualityReport& quality() const noexcept { return *quality_; }

    /// The fluid, resolved through P15 (`resolveCfdFluid`): the material's id and
    /// revision, rho and mu BY VALUE, nu derived. This module stores no other fluid
    /// data: ADR-028's hard rule is that a downstream solver maintains no second
    /// authoritative material database. Constant (ADR-058).
    [[nodiscard]] const CfdFluid& fluid() const noexcept { return fluid_; }

    /// The P15 material the fluid was resolved from (the document's single
    /// assignment in v1). The same as `fluid().id()`.
    [[nodiscard]] MaterialId materialId() const noexcept { return fluid_.id(); }

    /// The fluid's density. The same as `fluid().density()`.
    [[nodiscard]] Density density() const noexcept { return fluid_.density(); }

    /// The geometry stamp the mesh was built from, and which was still current when
    /// this input was validated.
    [[nodiscard]] const meshing::GeometryRevision& geometryRevision() const noexcept {
        return geometryRevision_;
    }

private:
    CfdModel(ObjectId body, MeshControlId control, const meshing::VolumeMesh& mesh,
             const meshing::GeometryMeshMap& map, const meshing::MeshQualityReport& quality,
             CfdFluid fluid, meshing::GeometryRevision geometryRevision) noexcept
        : body_(body)
        , control_(control)
        , mesh_(&mesh)
        , map_(&map)
        , quality_(&quality)
        , fluid_(std::move(fluid))
        , geometryRevision_(std::move(geometryRevision)) {}

    /// The one function permitted to build one. Possession is the evidence only
    /// because this list has exactly one entry.
    friend BETTERCAD_CFD_EXPORT Result<CfdModel> requireCfdModel(
        const Document& document, const features::Regenerator& regenerator,
        const meshing::Mesher& mesher, MeshControlId control);

    ObjectId body_{};
    MeshControlId control_{};
    const meshing::VolumeMesh* mesh_ = nullptr;
    const meshing::GeometryMeshMap* map_ = nullptr;
    const meshing::MeshQualityReport* quality_ = nullptr;
    CfdFluid fluid_;
    meshing::GeometryRevision geometryRevision_{};
};

/// Validates every prerequisite of a CFD analysis and hands back the proof, or
/// fails saying which one.
///
/// Read-only in every argument: nothing is regenerated, remeshed, healed or
/// assigned. A caller whose input is stale is told to fix the model, never quietly
/// fixed for them. In particular this never calls `Mesher::generate`: meshing is an
/// explicit request, and a solve that silently remeshed would hide both the cost
/// and the intent change behind it.
///
/// The order of checks is the order of the enum, and it is deliberate. Geometry
/// currency is asked BEFORE anything is read off the mesh, so a mesh of a body the
/// user has already changed is reported as stale rather than as whatever else
/// happens to be wrong with it.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdModel> requireCfdModel(
    const Document& document, const features::Regenerator& regenerator,
    const meshing::Mesher& mesher, MeshControlId control);

/// The problem `requireCfdModel` would report, or none if it would succeed. The
/// same checks in the same order, for a caller that wants to present or count the
/// reason rather than parse a message.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<InputProblem> cfdInputProblem(
    const Document& document, const features::Regenerator& regenerator,
    const meshing::Mesher& mesher, MeshControlId control);

} // namespace bettercad::cfd
