#include <bettercad/structural/StructuralAnalysis.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace bettercad::structural {
namespace {

/// One pass over the prerequisites, shared by both entry points so that they
/// cannot drift apart. `requireStructuralModel` needs the validated parts, and
/// `structuralInputProblem` needs only the verdict, so the checks live here
/// once and each caller takes what it needs.
struct Prepared {
    /// Set on failure; the two outputs below are then meaningless.
    std::optional<InputProblem> problem{};
    /// The diagnostic, already naming the specific reason P15 or P16 gave.
    Error error{};
    ObjectId body{};
    const meshing::VolumeMesh* mesh = nullptr;
    const meshing::GeometryMeshMap* map = nullptr;
    const meshing::MeshQualityReport* quality = nullptr;
    materials::LinearElasticConstants elastic{};
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

    // GEOMETRY FIRST, BEFORE ANYTHING IS READ OFF THE MESH. A mesh of a body
    // the user has already changed should be reported as describing a stale
    // model, not as whatever else is wrong with it -- and asking the mesh first
    // would answer the wrong question.
    //
    // DELEGATED, NOT REIMPLEMENTED. This is P16's one geometry boundary, which
    // refuses an unregenerated, failed, blocked, stale, bodiless, empty,
    // invalid, non-solid or zero-volume body, and refuses a body behind a
    // configuration override. That last one is how the carried configuration
    // defect reaches a structural solve as a refusal: P17 inherits the refusal
    // by reusing the check rather than by remembering to make it.
    if (const std::optional<meshing::GeometryIneligibility> reason =
            meshing::geometryIneligibility(document, regenerator, body)) {
        return fail(InputProblem::GeometryIneligible,
                    makeError(ErrorCode::FailedPrecondition,
                              std::format("the body of meshing control {} cannot be analysed: {}",
                                          control.value(), meshing::toString(*reason)))
                        .error());
    }

    // THE WHOLE POINT OF THIS FUNCTION. `Mesher::mesh()` would hand over a
    // stale mesh without complaint, by design, so currency is asked against
    // the document as it is NOW -- not inferred from possessing a mesh, and not
    // taken from a caller's word.
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

    // Current implies a held entry, and a held entry holds the mesh, its
    // mapping and its quality report as members of one struct. Taking all three
    // from one lookup is what makes them impossible to mismatch: there is no
    // signature here through which a caller could supply a map from one mesh
    // and a mesh from another.
    const meshing::VolumeMesh* mesh = mesher.mesh(control);
    const meshing::GeometryMeshMap* map = mesher.map(control);
    const meshing::MeshQualityReport* quality = mesher.quality(control);
    if (mesh == nullptr || map == nullptr || quality == nullptr) {
        // P16's invariant, checked rather than assumed. Unreachable through the
        // public API -- `Held` holds all three by value -- so it is recorded as
        // an internal error and is deliberately NOT claimed to be tested.
        return fail(InputProblem::NoMesh,
                    makeError(ErrorCode::Internal,
                              std::format("meshing control {} reports a current mesh but does "
                                          "not hold a complete one",
                                          control.value()))
                        .error());
    }

    // THE MATERIAL, THROUGH P15 AND ONLY THROUGH P15. ADR-028: a downstream
    // solver holds no material data of its own, so there is no default modulus
    // to fall back to here and no `steel()` helper to reach for. Resolution and
    // validation both happen behind these two calls -- P15 already refuses a
    // non-finite or non-positive E and a Poisson ratio outside its limits, and
    // names every gap in one diagnostic.
    const Result<const features::Material*> material = features::requireEffectiveMaterial(document);
    if (!material.has_value()) {
        return fail(InputProblem::NoMaterialAssigned, material.error());
    }
    Result<materials::LinearElasticConstants> elastic =
        features::requireLinearElasticConstants(document, (*material)->materialId());
    if (!elastic.has_value()) {
        return fail(InputProblem::MaterialUnusableForLinearElasticity, elastic.error());
    }

    return Prepared{.problem = std::nullopt,
                    .error = {},
                    .body = body,
                    .mesh = mesh,
                    .map = map,
                    .quality = quality,
                    .elastic = *elastic,
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
    case InputProblem::MaterialUnusableForLinearElasticity:
        return "material_unusable_for_linear_elasticity";
    }
    return "unknown";
}

Result<StructuralModel> requireStructuralModel(const Document& document,
                                               const features::Regenerator& regenerator,
                                               const meshing::Mesher& mesher,
                                               MeshControlId control) {
    Prepared prepared = prepare(document, regenerator, mesher, control);
    if (prepared.problem.has_value()) {
        return std::unexpected(std::move(prepared.error));
    }
    return StructuralModel(prepared.body, control, *prepared.mesh, *prepared.map, *prepared.quality,
                           prepared.elastic, std::move(prepared.revision));
}

std::optional<InputProblem> structuralInputProblem(const Document& document,
                                                   const features::Regenerator& regenerator,
                                                   const meshing::Mesher& mesher,
                                                   MeshControlId control) {
    return prepare(document, regenerator, mesher, control).problem;
}

} // namespace bettercad::structural
