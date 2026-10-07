// Resolving the material a structural solve consumes (P17-MAT-001).
//
// Every check below is DELEGATED. The audit's finding was that P15 already
// validates each property this milestone's checklist names, and validates it
// for this consumer by name, so the work here is choosing the consumer and
// carrying P15's answer -- not re-deciding what a usable modulus is.

#include <bettercad/structural/StructuralMaterial.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>

#include <format>
#include <utility>

namespace bettercad::structural {
namespace {

/// One pass, shared by the three entry points so they cannot drift apart.
struct Resolved {
    std::optional<MaterialProblem> problem{};
    Error error{};
    MaterialId id{};
    std::uint64_t revision = 0;
    materials::LinearElasticConstants elastic{};
    std::optional<Density> density{};
};

[[nodiscard]] Resolved fail(MaterialProblem problem, Error error) {
    return Resolved{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] Resolved resolve(const Document& document, ObjectId body,
                               StructuralAnalysisMode mode) {
    if (document.findObject(body) == nullptr) {
        return fail(MaterialProblem::BodyNotFound,
                    makeError(ErrorCode::NotFound,
                              std::format("there is no object {} to resolve a material for", body))
                        .error());
    }

    // THE ASSIGNMENT, AND ITS OWN DIAGNOSTIC. P15 distinguishes "nothing is
    // assigned" from "the assigned material is gone" and from "the assigned
    // material lacks a property" (ADR-026), because those need different things
    // from the user. Carrying its error rather than flattening it is what keeps
    // that distinction.
    const Result<const features::Material*> material = features::requireEffectiveMaterial(document);
    if (!material.has_value()) {
        return fail(MaterialProblem::NoMaterialAssigned, material.error());
    }
    const MaterialId id = (*material)->materialId();

    // E AND NU, THROUGH P15's OWN BOUNDARY. It refuses a missing, non-finite or
    // non-positive E and a nu outside -1 < nu < 0.5, and names every gap at
    // once. Nothing is re-checked here.
    Result<materials::LinearElasticConstants> elastic =
        features::requireLinearElasticConstants(document, id);
    if (!elastic.has_value()) {
        return fail(MaterialProblem::RequiredPropertyUnavailable, elastic.error());
    }

    // A DENSITY ONLY WHERE THE MODE NEEDS ONE. Requiring it for a problem with
    // no mass in its equations would block a solvable case, which is exactly
    // what P15's requirement table says about this consumer. Asking the table
    // rather than hardcoding the rule means the two cannot disagree.
    std::optional<Density> density;
    const materials::PropertyRequirement required =
        materials::requiredProperties(consumerFor(mode));
    const bool needsDensity =
        std::ranges::find(required.mechanical, materials::MechanicalPropertyKind::Density) !=
        required.mechanical.end();
    if (needsDensity) {
        Result<Density> resolvedDensity = features::requireDensity(document, id);
        if (!resolvedDensity.has_value()) {
            return fail(MaterialProblem::RequiredPropertyUnavailable, resolvedDensity.error());
        }
        density = *resolvedDensity;
    }

    return Resolved{.problem = std::nullopt,
                    .error = {},
                    .id = id,
                    .revision = (*material)->revision(),
                    .elastic = *elastic,
                    .density = density};
}

} // namespace

materials::ConsumerKind consumerFor(StructuralAnalysisMode mode) noexcept {
    switch (mode) {
    case StructuralAnalysisMode::LinearStatic:
        return materials::ConsumerKind::FeaLinearStatic;
    case StructuralAnalysisMode::LinearStaticWithGravity:
        return materials::ConsumerKind::FeaLinearStaticWithGravity;
    }
    return materials::ConsumerKind::FeaLinearStatic;
}

std::string_view toString(MaterialProblem problem) noexcept {
    switch (problem) {
    case MaterialProblem::BodyNotFound:
        return "body_not_found";
    case MaterialProblem::NoMaterialAssigned:
        return "no_material_assigned";
    case MaterialProblem::RequiredPropertyUnavailable:
        return "required_property_unavailable";
    }
    return "unknown";
}

Result<StructuralMaterial> resolveStructuralMaterial(const Document& document, ObjectId body,
                                                     StructuralAnalysisMode mode) {
    Resolved resolved = resolve(document, body, mode);
    if (resolved.problem.has_value()) {
        return std::unexpected(std::move(resolved.error));
    }
    return StructuralMaterial(resolved.id, resolved.revision, mode, resolved.elastic,
                              resolved.density);
}

std::optional<MaterialProblem> structuralMaterialProblem(const Document& document, ObjectId body,
                                                         StructuralAnalysisMode mode) {
    return resolve(document, body, mode).problem;
}

Result<materials::CompletenessReport>
structuralMaterialCompleteness(const Document& document, StructuralAnalysisMode mode) {
    // P15's own report, for P15's own consumer. Not reinterpreted, not
    // filtered, not reordered: its ordering is already defined and
    // deterministic, and its property identities are the canonical ones.
    return features::effectiveMaterialCompleteness(document, consumerFor(mode));
}

} // namespace bettercad::structural
