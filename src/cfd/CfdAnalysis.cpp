#include <bettercad/cfd/CfdAnalysis.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace bettercad::cfd {
namespace {

/// One pass over the prerequisites, shared by both entry points so that they
/// cannot drift apart.
struct Prepared {
    /// Set on failure; the fields below are then meaningless.
    std::optional<InputProblem> problem{};
    /// The diagnostic, already naming the specific reason P15 or P16 gave.
    Error error{};
    ObjectId body{};
    const meshing::VolumeMesh* mesh = nullptr;
    const meshing::GeometryMeshMap* map = nullptr;
    const meshing::MeshQualityReport* quality = nullptr;
    MaterialId material{};
    Density density{};
    meshing::GeometryRevision revision{};
};

[[nodiscard]] Prepared fail(InputProblem problem, Error error) {
    return Prepared{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] Prepared prepare(const Document& document, const features::Regenerator& regenerator,
                               const meshing::Mesher& mesher, MeshControlId control) {
    const meshing::MeshControl* intent = meshing::findMeshControl(document, control);
    if (intent == nullptr) {
        return fail(InputProblem::ControlNotFound,
                    makeError(ErrorCode::NotFound,
                              std::format("there is no meshing control {}", control.value()))
                        .error());
    }
    const ObjectId body = intent->definition().body;

    // GEOMETRY FIRST, BEFORE ANYTHING IS READ OFF THE MESH. A mesh of a body the
    // user has already changed should be reported as describing a stale model, not
    // as whatever else is wrong with it.
    //
    // DELEGATED, NOT REIMPLEMENTED. P16's one geometry boundary refuses an
    // unregenerated, failed, blocked, stale, bodiless, empty, invalid, non-solid or
    // zero-volume body, and a body behind a configuration override. P19 inherits
    // every one of those refusals by reusing the check.
    if (const std::optional<meshing::GeometryIneligibility> reason =
            meshing::geometryIneligibility(document, regenerator, body)) {
        return fail(InputProblem::GeometryIneligible,
                    makeError(ErrorCode::FailedPrecondition,
                              std::format("the body of meshing control {} cannot be analysed: {}",
                                          control.value(), meshing::toString(*reason)))
                        .error());
    }

    // THE WHOLE POINT OF THIS FUNCTION. `Mesher::mesh()` would hand over a stale
    // mesh without complaint, by design, so currency is asked against the document
    // as it is NOW -- not inferred from possessing a mesh, and not taken from a
    // caller's word.
    const meshing::MeshCurrency currency = mesher.currency(document, control);
    if (!meshing::describesTheModel(currency)) {
        const InputProblem problem = currency == meshing::MeshCurrency::NoMesh
                                         ? InputProblem::NoMesh
                                         : (currency == meshing::MeshCurrency::GenerationFailed
                                                ? InputProblem::MeshGenerationFailed
                                                : InputProblem::MeshStale);
        return fail(problem,
                    makeError(ErrorCode::FailedPrecondition,
                              std::format("meshing control {} has no mesh that describes the "
                                          "model: {}",
                                          control.value(), meshing::toString(currency)))
                        .error());
    }

    // Current implies a held entry, and a held entry holds the mesh, its mapping and
    // its quality report as members of one struct. Taking all three from one lookup
    // is what makes them impossible to mismatch.
    const meshing::VolumeMesh* mesh = mesher.mesh(control);
    const meshing::GeometryMeshMap* map = mesher.map(control);
    const meshing::MeshQualityReport* quality = mesher.quality(control);
    if (mesh == nullptr || map == nullptr || quality == nullptr) {
        // P16's invariant, checked rather than assumed. Unreachable through the
        // public API -- `Held` holds all three by value -- so it is recorded as an
        // internal error and is deliberately NOT claimed to be tested.
        return fail(InputProblem::NoMesh,
                    makeError(ErrorCode::Internal,
                              std::format("meshing control {} reports a current mesh but does "
                                          "not hold a complete one",
                                          control.value()))
                        .error());
    }

    // THE FLUID, THROUGH P15 AND ONLY THROUGH P15. ADR-028: a downstream solver
    // holds no material data of its own, so there is no typical density to fall back
    // to here. P15 already refuses a missing, non-finite or non-positive density and
    // names the material. The dynamic viscosity a CFD analysis also needs is
    // P19-FLUID-001's: no material carries one yet, and this boundary says so in
    // its header rather than pretending to have checked it.
    const Result<const features::Material*> material = features::requireEffectiveMaterial(document);
    if (!material.has_value()) {
        return fail(InputProblem::NoMaterialAssigned, material.error());
    }
    Result<Density> density = features::requireDensity(document, (*material)->materialId());
    if (!density.has_value()) {
        return fail(InputProblem::FluidDensityUnusable, density.error());
    }

    return Prepared{.problem = std::nullopt,
                    .error = {},
                    .body = body,
                    .mesh = mesh,
                    .map = map,
                    .quality = quality,
                    .material = (*material)->materialId(),
                    .density = *density,
                    .revision = meshing::geometryRevision(document, body)};
}

} // namespace

std::string_view toString(InputProblem problem) noexcept {
    switch (problem) {
    case InputProblem::ControlNotFound:
        return "control_not_found";
    case InputProblem::GeometryIneligible:
        return "geometry_ineligible";
    case InputProblem::NoMesh:
        return "no_mesh";
    case InputProblem::MeshStale:
        return "mesh_stale";
    case InputProblem::MeshGenerationFailed:
        return "mesh_generation_failed";
    case InputProblem::NoMaterialAssigned:
        return "no_material_assigned";
    case InputProblem::FluidDensityUnusable:
        return "fluid_density_unusable";
    }
    return "unknown";
}

Result<CfdModel> requireCfdModel(const Document& document, const features::Regenerator& regenerator,
                                 const meshing::Mesher& mesher, MeshControlId control) {
    Prepared prepared = prepare(document, regenerator, mesher, control);
    if (prepared.problem.has_value()) {
        return std::unexpected(std::move(prepared.error));
    }
    return CfdModel(prepared.body, control, *prepared.mesh, *prepared.map, *prepared.quality,
                    prepared.material, prepared.density, std::move(prepared.revision));
}

std::optional<InputProblem> cfdInputProblem(const Document& document,
                                            const features::Regenerator& regenerator,
                                            const meshing::Mesher& mesher, MeshControlId control) {
    return prepare(document, regenerator, mesher, control).problem;
}

} // namespace bettercad::cfd
