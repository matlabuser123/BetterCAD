// Resolving the fluid a CFD analysis consumes, through P15 and only P15
// (P19-FLUID-001).

#include <bettercad/cfd/CfdFluid.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>

#include <utility>

namespace bettercad::cfd {
namespace {

/// One pass, shared by both entry points so they cannot drift apart.
struct Resolved {
    std::optional<FluidProblem> problem{};
    Error error{};
    MaterialId id{};
    std::uint64_t revision = 0;
    CfdFluidProperties properties{};
};

[[nodiscard]] Resolved fail(FluidProblem problem, Error error) {
    return Resolved{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] Resolved resolve(const Document& document) {
    // "Nothing assigned" and "the assigned material is gone" are P15's to tell
    // apart (ADR-026); its diagnostic is carried unchanged.
    const Result<const features::Material*> material = features::requireEffectiveMaterial(document);
    if (!material.has_value()) {
        return fail(FluidProblem::NoMaterialAssigned, material.error());
    }
    const MaterialId id = (*material)->materialId();

    // WHAT IS REQUIRED IS P15's: its one requirement function decides success and
    // writes the diagnostic, naming every missing input at once, defaulting
    // nothing. P15's table (`requiredProperties(IncompressibleFlow)`) and its own
    // `isAvailable` decide which CATEGORY a failure is, so no rule is restated
    // here. That the function and the table agree is P15's own cross-check
    // (ProvenanceTests' consumer table), extended for this consumer.
    const features::MaterialDefinition& definition = (*material)->definition();
    const materials::PropertyRequirement required = materials::requiredProperties(kFluidConsumer);

    const Result<features::IncompressibleFlowProperties> flow =
        features::requireIncompressibleFlowProperties(document, id);
    if (!flow.has_value()) {
        // The CATEGORY is the first gap in P15's order: the mechanical half (the
        // density) before the thermal half (the viscosity).
        for (const materials::MechanicalPropertyKind kind : required.mechanical) {
            if (!materials::isAvailable(definition.mechanical, kind)) {
                return fail(FluidProblem::DensityUnavailable, flow.error());
            }
        }
        for (const materials::ThermalPropertyKind kind : required.thermal) {
            if (!materials::isAvailable(definition.thermal, kind)) {
                return fail(FluidProblem::ViscosityUnavailable, flow.error());
            }
        }
        // UNREACHABLE while P15's function and P15's table agree, which P15's own
        // cross-check proves for this consumer. Not mislabelled as a viscosity
        // problem if they ever drift: an internal error naming the disagreement.
        return fail(FluidProblem::ViscosityUnavailable,
                    Error{ErrorCode::Internal,
                          "P15 refused the fluid for incompressible flow, but its requirement table reports every "
                          "required property available: " + flow.error().message});
    }

    return Resolved{.problem = std::nullopt,
                    .error = {},
                    .id = id,
                    .revision = (*material)->revision(),
                    .properties = CfdFluidProperties{.density = flow->density, .viscosity = flow->dynamicViscosity}};
}

} // namespace

std::string_view toString(FluidProblem problem) noexcept {
    switch (problem) {
    case FluidProblem::NoMaterialAssigned:
        return "no_material_assigned";
    case FluidProblem::DensityUnavailable:
        return "density_unavailable";
    case FluidProblem::ViscosityUnavailable:
        return "viscosity_unavailable";
    }
    return "unknown";
}

Result<CfdFluid> resolveCfdFluid(const Document& document) {
    Resolved resolved = resolve(document);
    if (resolved.problem.has_value()) {
        return std::unexpected(std::move(resolved.error));
    }
    return CfdFluid(resolved.id, resolved.revision, resolved.properties);
}

std::optional<FluidProblem> cfdFluidProblem(const Document& document) {
    return resolve(document).problem;
}

Result<materials::CompletenessReport> cfdFluidCompleteness(const Document& document) {
    // P15's own report for P15's own consumer: not reinterpreted, not filtered,
    // not reordered.
    return features::effectiveMaterialCompleteness(document, kFluidConsumer);
}

} // namespace bettercad::cfd
