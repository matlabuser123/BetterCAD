#pragma once

// P18-ARCH-001 -- the boundary a thermal analysis is prepared at.
//
// WHAT THIS FILE IS FOR. P16 records a limitation it could not close on its
// own: nothing FORCES the holder of a mesh to ask whether it is stale.
// `Mesher::mesh()` hands back a stale mesh deliberately -- "it is old, not
// wrong", because P16-VIZ-001 inspects stale meshes -- so a function taking
// `const VolumeMesh&` cannot tell a current mesh from one describing a body
// the user has since changed. P17 closed that gap for a structural solver with
// one device, possession is the proof (ADR-036), and a thermal solver that
// took a mesh would reopen it. So this module does not take a mesh either.
//
// It takes a document, a regenerator, a mesher and a control, and asks. The
// answer is a `ThermalModel`, and POSSESSION OF ONE IS THE EVIDENCE: it has no
// public constructor, and the only thing that can make one is
// `requireThermalModel`, which checks every prerequisite first. The checks are
// P16's and P15's, delegated and not reimplemented, exactly as
// `requireStructuralModel` delegates them -- and deliberately NOT shared with
// it, because thermal (layer 45) sits below structural (50) so that a later
// thermo-mechanical coupling can read temperatures from above (ADR-050).
//
// WHAT IT IS NOT. There is no conductivity matrix here, no capacity matrix, no
// element, no degree of freedom, no load, no boundary condition, no time step
// and no solve. Those are P18-ELEM-001, P18-DOF-001, P18-LOAD-001, P18-BC-001,
// P18-SOLVE-001 and P18-TRANSIENT-001. This milestone defines where the gate
// is and makes it impossible to walk around; the checks it cannot perform yet
// are the ones whose canonical types do not exist, and they are named below
// rather than stubbed.
//
// See ADR-049 (what the solver is), ADR-050 (the module and its layer) and
// ADR-051 (this boundary).

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/thermal/Export.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::thermal {

/// Why a thermal analysis cannot be prepared from a document as it stands.
///
/// THE REASON IS PART OF THE ANSWER, following `MeshCurrency`,
/// `GeometryIneligibility` and `structural::InputProblem`: "cannot solve"
/// tells a user nothing they can act on, while "the geometry has changed since
/// the mesh was built" tells them to remesh.
///
/// EVERY VALUE HERE IS REACHABLE, and that constrained the list, as it did for
/// P17. A mesh that fails P16's structural verdict is never HELD, so "the held
/// mesh is structurally invalid" cannot occur; a held mesh's `GeometryMeshMap`
/// is a member of the same `Held` struct, so "no mapping" cannot occur either.
/// This boundary takes mesh, map and quality from one lookup, which is why a
/// caller cannot mismatch them.
///
/// WHAT IS NOT HERE YET, and why. A heat flux that does not resolve, a
/// prescribed temperature that conflicts with another, a model with no
/// temperature datum, a mesh whose quality a thermal analysis should refuse, a
/// transient analysis on a material with no specific heat, and a time step
/// that is not positive are all real input problems -- and each needs a
/// canonical type this milestone deliberately does not define. They belong to
/// `P18-LOAD-001`, `P18-BC-001`, `P18-SOLVE-001`, `P18-VALID-001`,
/// `P18-MAT-001` and `P18-TRANSIENT-001`, which add both the value and the
/// check.
enum class InputProblem : std::uint8_t {
    /// The document has no mesh control with that id.
    ControlNotFound,
    /// The body the control names cannot be meshed at all, so it cannot be
    /// analysed either. The reason comes from `meshing::geometryIneligibility`
    /// unchanged -- including `ConfigurationOverrideActive`, which is how the
    /// carried configuration defect reaches P18 as a refusal rather than as a
    /// wrong answer, exactly as it reaches P17.
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
    /// The document names no material, or names one that is gone. A thermal
    /// analysis without a material is not a solve with a typical conductivity:
    /// ADR-028 forbids a solver holding any material data of its own, so there
    /// is nothing to fall back to and nothing should be.
    NoMaterialAssigned,
    /// A material resolves, but it has no usable thermal conductivity. P15
    /// decides that, not P18: `requireThermalConductivity` refuses a missing,
    /// non-finite or non-positive `k` and names the material. What STEADY
    /// conduction needs is exactly `k`; the density and specific heat a
    /// transient analysis adds are `P18-MAT-001`'s question, asked per mode.
    MaterialUnusableForConduction,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(InputProblem problem) noexcept;

/// A thermal analysis input that has been validated, and whose existence is
/// the proof.
///
/// There is no public constructor and no setter. The only way to obtain one is
/// `requireThermalModel`, so a function taking a `const ThermalModel&` does not
/// need to re-check anything, and a function that wants to skip the checks
/// cannot construct its argument. The device is `VolumeMesh`'s and
/// `StructuralModel`'s, chosen a third time because it turns a rule a reviewer
/// must remember into one the compiler enforces.
///
/// WHAT POSSESSION PROVES, at the moment of construction:
///
/// ```text
/// the control exists
/// the body is eligible -- regenerated, current, a solid, non-empty, valid,
///   and not behind a configuration override
/// a mesh is held for the control and its currency is Current
/// the mapping and the quality report came from that same mesh, in one lookup
/// the document's material resolves and yields a usable thermal conductivity
/// ```
///
/// WHAT IT DOES NOT PROVE. That the mesh is good enough for an accurate answer
/// (`P18-VALID-001` owns the acceptance policy), that loads and boundary
/// conditions resolve (`P18-LOAD-001`, `P18-BC-001`), that the model has a
/// temperature datum (`P18-SOLVE-001`), or that the material can carry a
/// transient (`P18-MAT-001`). P17 learned that those gates land in a STAGED
/// validation entry that runs after this boundary (ADR-042), not inside it,
/// and P18 inherits that shape: this type proves the input, the validation
/// entry judges it.
///
/// BORROWED, NOT OWNED, AND MOVE-ONLY. The mesh, mapping and quality report
/// belong to the `Mesher`. A `ThermalModel` is a snapshot of a validated state
/// and must not outlive it, nor survive an edit -- exactly the lifetime the
/// pointer from `Mesher::mesh()` already has. Re-preparing is a few lookups and
/// one material resolution, and re-preparing rather than holding is the point.
class BETTERCAD_THERMAL_EXPORT ThermalModel {
public:
    ThermalModel(const ThermalModel&) = delete;
    ThermalModel& operator=(const ThermalModel&) = delete;
    ThermalModel(ThermalModel&&) noexcept = default;
    ThermalModel& operator=(ThermalModel&&) noexcept = default;
    ~ThermalModel() = default;

    /// The feature whose body is analysed.
    [[nodiscard]] ObjectId body() const noexcept { return body_; }

    /// The control whose intent produced the mesh.
    [[nodiscard]] MeshControlId control() const noexcept { return control_; }

    /// The validated Tet4 volume mesh. Current as of construction.
    [[nodiscard]] const meshing::VolumeMesh& mesh() const noexcept { return *mesh_; }

    /// The CAD-face to boundary-facet mapping for that mesh. This is how a heat
    /// flux, a prescribed temperature or a convection surface will reach
    /// facets and nodes: by naming a CAD face and resolving it now, never by
    /// holding a `NodeId` (ADR-032).
    [[nodiscard]] const meshing::GeometryMeshMap& map() const noexcept { return *map_; }

    /// P16's quality measurements for that mesh -- the MEASUREMENTS, not a
    /// verdict. `P18-VALID-001` owns the thermal acceptance policy and reads
    /// `summaries`; P16 measures and states no opinion.
    [[nodiscard]] const meshing::MeshQualityReport& quality() const noexcept { return *quality_; }

    /// The material's thermal conductivity, resolved through P15.
    ///
    /// BY VALUE, and this module stores no other material data. ADR-028's hard
    /// rule is that a downstream solver maintains no second authoritative
    /// material database: no helper for steel, no typical conductivity. This
    /// number came from `requireThermalConductivity` and is a derived view of
    /// P15's canonical data, never a copy of it. Isotropic, constant: a
    /// conductivity TENSOR or a conductivity law is out of scope (ADR-049).
    [[nodiscard]] ThermalConductivity conductivity() const noexcept { return conductivity_; }

    /// The geometry stamp the mesh was built from, and which was still current
    /// when this input was validated.
    [[nodiscard]] const meshing::GeometryRevision& geometryRevision() const noexcept {
        return geometryRevision_;
    }

private:
    ThermalModel(ObjectId body, MeshControlId control, const meshing::VolumeMesh& mesh,
                 const meshing::GeometryMeshMap& map, const meshing::MeshQualityReport& quality,
                 ThermalConductivity conductivity, meshing::GeometryRevision geometryRevision) noexcept
        : body_(body)
        , control_(control)
        , mesh_(&mesh)
        , map_(&map)
        , quality_(&quality)
        , conductivity_(conductivity)
        , geometryRevision_(std::move(geometryRevision)) {}

    /// The one function permitted to build one. Possession is the evidence only
    /// because this list has exactly one entry.
    friend BETTERCAD_THERMAL_EXPORT Result<ThermalModel> requireThermalModel(
        const Document& document, const features::Regenerator& regenerator,
        const meshing::Mesher& mesher, MeshControlId control);

    ObjectId body_{};
    MeshControlId control_{};
    const meshing::VolumeMesh* mesh_ = nullptr;
    const meshing::GeometryMeshMap* map_ = nullptr;
    const meshing::MeshQualityReport* quality_ = nullptr;
    ThermalConductivity conductivity_{};
    meshing::GeometryRevision geometryRevision_{};
};

/// Validates every prerequisite of a thermal analysis and hands back the
/// proof, or fails saying which one.
///
/// Read-only in every argument: nothing is regenerated, remeshed, healed or
/// assigned. A caller whose input is stale is told to fix the model, never
/// quietly fixed for them. In particular this never calls `Mesher::generate`:
/// meshing is an explicit request, and a solve that silently remeshed would
/// hide both the cost and the intent change behind it.
///
/// The order of checks is the order of the enum, and it is deliberate.
/// Geometry currency is asked BEFORE anything is read off the mesh, so a mesh
/// of a body the user has already changed is reported as stale rather than as
/// whatever else happens to be wrong with it.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalModel> requireThermalModel(
    const Document& document, const features::Regenerator& regenerator,
    const meshing::Mesher& mesher, MeshControlId control);

/// The problem `requireThermalModel` would report, or none if it would
/// succeed. The same checks in the same order, for a caller that wants to
/// present or count the reason rather than parse a message.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<InputProblem> thermalInputProblem(
    const Document& document, const features::Regenerator& regenerator,
    const meshing::Mesher& mesher, MeshControlId control);

} // namespace bettercad::thermal
