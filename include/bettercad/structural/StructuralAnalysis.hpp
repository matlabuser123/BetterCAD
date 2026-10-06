#pragma once

// P17-ARCH-001 -- the boundary a structural analysis is prepared at.
//
// WHAT THIS FILE IS FOR. P16 records a limitation it could not close on its
// own: nothing FORCES the holder of a mesh to ask whether it is stale.
// `Mesher::mesh()` hands back a stale mesh deliberately -- "it is old, not
// wrong", because P16-VIZ-001 inspects stale meshes -- so a function taking
// `const VolumeMesh&` cannot tell a current mesh from one describing a body
// the user has since changed. A structural solver that took one would solve
// the wrong body and report success.
//
// So P17 does not take a mesh. It takes a document, a regenerator, a mesher
// and a control, and asks. The answer is a `StructuralModel`, and POSSESSION
// OF ONE IS THE EVIDENCE: it has no public constructor, and the only thing
// that can make one is `requireStructuralModel`, which checks every
// prerequisite first. The pattern is P16's own -- `VolumeMesh` and
// `GeometryMeshMap` are built the same way, for the same reason (ADR-030).
//
// WHAT IT IS NOT. There is no stiffness matrix here, no element, no degree of
// freedom, no load, no restraint and no solve. Those are P17-ELEM-001,
// P17-DOF-001, P17-LOAD-001, P17-BC-001 and P17-SOLVE-001. This milestone
// defines where the gate is and makes it impossible to walk around; the
// checks it cannot perform yet are the ones whose canonical types do not
// exist, and they are named below rather than stubbed.
//
// See ADR-034 (what the solver is), ADR-035 (the module and its layer) and
// ADR-036 (this boundary).

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/structural/Export.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::structural {

/// Why a structural analysis cannot be prepared from a document as it stands.
///
/// THE REASON IS PART OF THE ANSWER, following `MeshCurrency` and
/// `GeometryIneligibility`: "cannot solve" tells a user nothing they can act
/// on, while "the geometry has changed since the mesh was built" tells them to
/// remesh.
///
/// EVERY VALUE HERE IS REACHABLE, and that constrained the list. Two more were
/// drafted and removed once the audit showed nothing could return them. A mesh
/// that fails P16's structural verdict is never HELD -- `Mesher::generate`
/// refuses it and records a failure -- so "the held mesh is structurally
/// invalid" cannot occur, and P17 relies on that contract instead of
/// re-deriving signed volumes P16 already owns. A held mesh's
/// `GeometryMeshMap` is a member of the same `Held` struct, so "no mapping" and
/// "a mapping for a different mesh" cannot occur either; this boundary takes
/// both from one lookup, which is why they cannot be mismatched by a caller.
///
/// WHAT IS NOT HERE YET, and why. A load that does not resolve, a restraint
/// that conflicts, a model with too few restraints, a mesh whose quality a
/// structural analysis should refuse, and an unsupported solver setting are all
/// real input problems -- and each needs a canonical type this milestone
/// deliberately does not define. They belong to `P17-LOAD-001`, `P17-BC-001`,
/// `P17-VALID-001` and `P17-DATA-001`, which add both the value and the check.
enum class InputProblem : std::uint8_t {
    /// The document has no mesh control with that id.
    ControlNotFound,
    /// The body the control names cannot be meshed at all, so it cannot be
    /// analysed either. The reason comes from `meshing::geometryIneligibility`
    /// unchanged -- including `ConfigurationOverrideActive`, which is how the
    /// carried configuration defect reaches P17 as a refusal rather than as a
    /// wrong answer.
    GeometryIneligible,
    /// Nothing has been meshed for this control yet.
    NoMesh,
    /// A mesh is held, but it does not describe the document as it is now: the
    /// geometry moved, or the meshing intent did. P16 distinguishes the two and
    /// the diagnostic carries which.
    MeshStale,
    /// The most recent generation attempt failed. Any mesh still held is stale
    /// by definition, and reporting it as merely stale would hide that the
    /// attempt to replace it failed.
    MeshGenerationFailed,
    /// The document names no material, or names one that is gone. A structural
    /// analysis without a material is not a solve with defaults: ADR-028
    /// forbids a solver holding any material data of its own, so there is
    /// nothing to fall back to and nothing should be.
    NoMaterialAssigned,
    /// A material resolves, but its linear-elastic inputs are missing or out of
    /// range. P15 decides that, not P17: `requireLinearElasticConstants`
    /// already refuses a non-finite or non-positive Young's modulus and a
    /// Poisson ratio outside -1 < nu < 0.5, and names every gap at once.
    MaterialUnusableForLinearElasticity,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view toString(InputProblem problem) noexcept;

/// A structural analysis input that has been validated, and whose existence is
/// the proof.
///
/// There is no public constructor and no setter. The only way to obtain one is
/// `requireStructuralModel`, so a function taking a `const StructuralModel&`
/// does not need to re-check anything, and a function that wants to skip the
/// checks cannot construct its argument. That is the device `VolumeMesh` uses,
/// chosen again here because it turns a rule a reviewer must remember into one
/// the compiler enforces.
///
/// WHAT POSSESSION PROVES, at the moment of construction:
///
/// ```text
/// the control exists
/// the body is eligible -- regenerated, current, a solid, non-empty, valid,
///   and not behind a configuration override
/// a mesh is held for the control and its currency is Current
/// the mapping and the quality report came from that same mesh, in one lookup
/// the document's material resolves and yields complete linear-elastic
///   constants
/// ```
///
/// WHAT IT DOES NOT PROVE. That the mesh is good enough for an accurate answer
/// (`P17-VALID-001` owns the acceptance policy), that loads and restraints
/// resolve (`P17-LOAD-001`, `P17-BC-001`), or that the model is sufficiently
/// constrained (`P17-SOLVE-001`). Those gates are added to
/// `requireStructuralModel` by the milestones that define their data, and this
/// type is where they will land.
///
/// BORROWED, NOT OWNED. The mesh, mapping and quality report belong to the
/// `Mesher`. A `StructuralModel` is a snapshot of a validated state and must
/// not outlive it, nor survive an edit -- exactly the lifetime the pointer from
/// `Mesher::mesh()` already has. It is cheap to re-prepare, and re-preparing
/// rather than holding is the point.
class BETTERCAD_STRUCTURAL_EXPORT StructuralModel {
public:
    /// MOVE-ONLY, DELIBERATELY. The mesh, the mapping and the quality report
    /// are borrowed from the `Mesher`, so an instance that outlives them
    /// dangles. Deleting the copy does not make that impossible -- the
    /// original has the same lifetime -- but it removes the realistic way it
    /// happens, which is a copy stashed in a container or a member that
    /// outlives the mesher it came from. A move cannot be stored accidentally:
    /// the source is visibly spent.
    ///
    /// The residual hazard is holding one across an edit, and the answer to
    /// that is not a smarter type but not holding one: re-preparing is a few
    /// lookups and one material resolution, with no geometry or mesh work.
    StructuralModel(const StructuralModel&) = delete;
    StructuralModel& operator=(const StructuralModel&) = delete;
    StructuralModel(StructuralModel&&) noexcept = default;
    StructuralModel& operator=(StructuralModel&&) noexcept = default;
    ~StructuralModel() = default;

    /// The feature whose body is analysed.
    [[nodiscard]] ObjectId body() const noexcept { return body_; }

    /// The control whose intent produced the mesh.
    [[nodiscard]] MeshControlId control() const noexcept { return control_; }

    /// The validated Tet4 volume mesh. Current as of construction.
    [[nodiscard]] const meshing::VolumeMesh& mesh() const noexcept { return *mesh_; }

    /// The CAD-face to boundary-facet mapping for that mesh. This is how a load
    /// or a restraint will reach nodes: by naming a CAD face and resolving it
    /// now, never by holding a `NodeId` (ADR-032).
    [[nodiscard]] const meshing::GeometryMeshMap& map() const noexcept { return *map_; }

    /// P16's quality measurements for that mesh.
    ///
    /// THE MEASUREMENTS, NOT A VERDICT. `MeshQualityReport::satisfiesPolicy()`
    /// answers the question the MESH CONTROL asked, under thresholds P16 ships
    /// empty on purpose -- `reportOnlyThresholds()` classifies nothing, so that
    /// call is true for any structurally valid mesh. A structural acceptance
    /// policy reads `summaries`, which carry every measured minimum, maximum,
    /// mean and worst element, and applies thresholds P17 is responsible for.
    /// `P17-VALID-001` owns them; P16 measures and states no opinion.
    [[nodiscard]] const meshing::MeshQualityReport& quality() const noexcept { return *quality_; }

    /// The material's isotropic elastic constants, resolved through P15.
    ///
    /// BY VALUE, and this module stores no other material data. ADR-028's hard
    /// rule is that a downstream solver maintains no second authoritative
    /// material database: no helper for steel, no default modulus, no typical
    /// value. These four numbers came from `requireLinearElasticConstants` and
    /// are a derived view of P15's canonical data, never a copy of it.
    [[nodiscard]] const materials::LinearElasticConstants& elastic() const noexcept {
        return elastic_;
    }

    /// The geometry stamp the mesh was built from, and which was still current
    /// when this input was validated.
    [[nodiscard]] const meshing::GeometryRevision& geometryRevision() const noexcept {
        return geometryRevision_;
    }

private:
    StructuralModel(ObjectId body, MeshControlId control, const meshing::VolumeMesh& mesh,
                    const meshing::GeometryMeshMap& map, const meshing::MeshQualityReport& quality,
                    materials::LinearElasticConstants elastic,
                    meshing::GeometryRevision geometryRevision) noexcept
        : body_(body)
        , control_(control)
        , mesh_(&mesh)
        , map_(&map)
        , quality_(&quality)
        , elastic_(elastic)
        , geometryRevision_(std::move(geometryRevision)) {}

    /// The one function permitted to build one. Possession is the evidence only
    /// because this list has exactly one entry.
    friend BETTERCAD_STRUCTURAL_EXPORT Result<StructuralModel> requireStructuralModel(
        const Document& document, const features::Regenerator& regenerator,
        const meshing::Mesher& mesher, MeshControlId control);

    ObjectId body_{};
    MeshControlId control_{};
    const meshing::VolumeMesh* mesh_ = nullptr;
    const meshing::GeometryMeshMap* map_ = nullptr;
    const meshing::MeshQualityReport* quality_ = nullptr;
    materials::LinearElasticConstants elastic_{};
    meshing::GeometryRevision geometryRevision_{};
};

/// Validates every prerequisite of a structural analysis and hands back the
/// proof, or fails saying which one.
///
/// Read-only in every argument: nothing is regenerated, remeshed, healed or
/// assigned. A caller whose input is stale is told to fix the model, never
/// quietly fixed for them -- the choice `requireMeshableGeometry` makes, for
/// the same reason. In particular this never calls `Mesher::generate`: meshing
/// is an explicit request, and a solve that silently remeshed would hide both
/// the cost and the intent change behind it.
///
/// The order of checks is the order of the enum, and it is deliberate.
/// Geometry currency is asked BEFORE anything is read off the mesh, so a mesh
/// of a body the user has already changed is reported as stale rather than as
/// whatever else happens to be wrong with it -- the same ordering argument
/// `requireMeshableGeometry` makes about asking currency before volume.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<StructuralModel> requireStructuralModel(
    const Document& document, const features::Regenerator& regenerator,
    const meshing::Mesher& mesher, MeshControlId control);

/// The problem `requireStructuralModel` would report, or none if it would
/// succeed.
///
/// The same checks in the same order, for a caller that wants to present or
/// count the reason rather than parse a message. Provided for the reason
/// `meshing::geometryIneligibility` is: a UI, a CLI report or a test should not
/// have to match on diagnostic text.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<InputProblem> structuralInputProblem(
    const Document& document, const features::Regenerator& regenerator,
    const meshing::Mesher& mesher, MeshControlId control);

} // namespace bettercad::structural
