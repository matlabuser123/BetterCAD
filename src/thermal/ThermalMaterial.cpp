// Resolving the material a thermal analysis consumes (P18-MAT-001).
//
// Every check below is DELEGATED. The audit's finding was that P15 already
// validates each property this milestone's checklist names, and validates it
// for this consumer by name, so the work here is choosing the consumer and
// carrying P15's answer -- not re-deciding what a usable conductivity is.

#include <bettercad/thermal/ThermalMaterial.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace bettercad::thermal {
namespace {

/// One pass, shared by the entry points so they cannot drift apart.
struct Resolved {
    std::optional<MaterialProblem> problem{};
    Error error{};
    MaterialId id{};
    std::uint64_t revision = 0;
    ThermalConductivity conductivity{};
    std::optional<ThermalCapacity> capacity{};
};

[[nodiscard]] Resolved fail(MaterialProblem problem, Error error) {
    return Resolved{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] Resolved resolve(const Document& document, ObjectId body, ThermalAnalysisMode mode) {
    if (document.findObject(body) == nullptr) {
        return fail(MaterialProblem::BodyNotFound,
                    makeError(ErrorCode::NotFound,
                              std::format("there is no object {} to resolve a material for", body))
                        .error());
    }

    // THE ASSIGNMENT, AND ITS OWN DIAGNOSTIC. P15 distinguishes "nothing is
    // assigned" from "the assigned material is gone" and from "the assigned
    // material lacks a property" (ADR-026), because those need different
    // things from the user. Carrying its error rather than flattening it is
    // what keeps that distinction.
    const Result<const features::Material*> material = features::requireEffectiveMaterial(document);
    if (!material.has_value()) {
        return fail(MaterialProblem::NoMaterialAssigned, material.error());
    }
    const MaterialId id = (*material)->materialId();

    // WHICH PROPERTIES THIS MODE NEEDS, FROM P15's TABLE. Requiring a density
    // or a specific heat for a steady problem would block a solvable case,
    // which is exactly what P15's table says about ThermalSteady. Asking the
    // table rather than hardcoding the rule means the two cannot disagree.
    const materials::PropertyRequirement required = materials::requiredProperties(consumerFor(mode));
    const bool needsDensity =
        std::ranges::find(required.mechanical, materials::MechanicalPropertyKind::Density) !=
        required.mechanical.end();
    const bool needsSpecificHeat =
        std::ranges::find(required.thermal, materials::ThermalPropertyKind::SpecificHeatCapacity) !=
        required.thermal.end();

    if (needsDensity || needsSpecificHeat) {
        // THE TRANSIENT TRIPLE, THROUGH P15's OWN BOUNDARY. It refuses a
        // missing, non-finite or non-positive rho, cp or k and names EVERY gap
        // at once, in one diagnostic. Nothing is re-checked here, and the
        // density is read from the material's one density, in the mechanical
        // half, by P15 -- there is no thermal density to prefer.
        Result<features::TransientConductionProperties> transient =
            features::requireTransientConductionProperties(document, id);
        if (!transient.has_value()) {
            return fail(MaterialProblem::RequiredPropertyUnavailable, transient.error());
        }
        return Resolved{.problem = std::nullopt,
                        .error = {},
                        .id = id,
                        .revision = (*material)->revision(),
                        .conductivity = transient->thermalConductivity,
                        .capacity = ThermalCapacity{.density = transient->density,
                                                    .specificHeatCapacity =
                                                        transient->specificHeatCapacity}};
    }

    // k ALONE, THROUGH P15's OWN BOUNDARY. Nothing else is asked for, so a
    // material with no density and no specific heat resolves here -- and a
    // diagnostic about either can never be raised at a steady analysis.
    Result<ThermalConductivity> conductivity = features::requireThermalConductivity(document, id);
    if (!conductivity.has_value()) {
        return fail(MaterialProblem::RequiredPropertyUnavailable, conductivity.error());
    }
    return Resolved{.problem = std::nullopt,
                    .error = {},
                    .id = id,
                    .revision = (*material)->revision(),
                    .conductivity = *conductivity,
                    .capacity = std::nullopt};
}

} // namespace

materials::ConsumerKind consumerFor(ThermalAnalysisMode mode) noexcept {
    switch (mode) {
    case ThermalAnalysisMode::Steady:
        return materials::ConsumerKind::ThermalSteady;
    case ThermalAnalysisMode::Transient:
        return materials::ConsumerKind::ThermalTransient;
    }
    return materials::ConsumerKind::ThermalSteady;
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

Result<ThermalMaterial> resolveThermalMaterial(const Document& document, ObjectId body,
                                               ThermalAnalysisMode mode) {
    Resolved resolved = resolve(document, body, mode);
    if (resolved.problem.has_value()) {
        return std::unexpected(std::move(resolved.error));
    }
    return ThermalMaterial(resolved.id, resolved.revision, mode, resolved.conductivity,
                           resolved.capacity);
}

std::optional<MaterialProblem> thermalMaterialProblem(const Document& document, ObjectId body,
                                                      ThermalAnalysisMode mode) {
    return resolve(document, body, mode).problem;
}

Result<materials::CompletenessReport> thermalMaterialCompleteness(const Document& document,
                                                                  ThermalAnalysisMode mode) {
    // P15's own report, for P15's own consumer. Not reinterpreted, not
    // filtered, not reordered: its ordering is already defined and
    // deterministic, and its property identities are the canonical ones.
    return features::effectiveMaterialCompleteness(document, consumerFor(mode));
}

Result<ThermalCapability> thermalCapability(const Document& document, MaterialId id) {
    const Result<materials::CompletenessReport> steady =
        features::materialCompleteness(document, id, materials::ConsumerKind::ThermalSteady);
    if (!steady.has_value()) {
        return std::unexpected(steady.error());
    }
    const Result<materials::CompletenessReport> transient =
        features::materialCompleteness(document, id, materials::ConsumerKind::ThermalTransient);
    if (!transient.has_value()) {
        return std::unexpected(transient.error());
    }
    return ThermalCapability{.steady = steady->ready(), .transient = transient->ready()};
}

} // namespace bettercad::thermal
