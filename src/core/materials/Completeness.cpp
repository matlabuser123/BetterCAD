#include <bettercad/core/materials/Completeness.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace bettercad::materials {

namespace {

constexpr std::array kConsumerKinds{
    ConsumerKind::MassProperties,   ConsumerKind::FeaLinearStatic,
    ConsumerKind::FeaLinearStaticWithGravity, ConsumerKind::FeaYieldStrength,
    ConsumerKind::ThermalSteady,    ConsumerKind::ThermalTransient,
    ConsumerKind::ThermoMechanical,
};

using MechKind = MechanicalPropertyKind;
using ThermKind = ThermalPropertyKind;

/// Whether a stored mechanical property is present AND within range.
///
/// Range matters: a report that called a material Ready on a value validation
/// would refuse would be worse than no report, because the consumer would then
/// fail anyway and the report would have been a lie.
[[nodiscard]] bool storedAndValid(const MaterialProperty<Density>& property) {
    const std::optional<Density> value = property.value();
    return value.has_value() && isFinite(*value) && value->si() > 0.0;
}

/// One overload serves a Young's modulus, a yield strength, an ultimate strength
/// and a shear strength, because ElasticModulus and Stress are THE SAME TYPE --
/// both Quantity<pressure> (P15-UNITS-001: "stress and elastic moduli are
/// pressures dimensionally"). Writing two overloads here does not compile.
///
/// That is the concrete reason requirements operate on semantic identity rather
/// than on type: the type system cannot tell a modulus from a yield strength, so
/// only the property KIND can, and isAvailable() switches on the kind.
[[nodiscard]] bool storedAndValidPressure(const MaterialProperty<Stress>& property) {
    const std::optional<Stress> value = property.value();
    return value.has_value() && isFinite(*value) && value->si() > 0.0;
}

[[nodiscard]] bool storedAndValid(const MaterialProperty<PoissonRatio>& property) {
    const std::optional<PoissonRatio> value = property.value();
    return value.has_value() && isFinite(*value) && value->value() > limits::minPoissonRatio &&
           value->value() < limits::maxPoissonRatio;
}

[[nodiscard]] bool storedAndValid(const MaterialProperty<Elongation>& property) {
    const std::optional<Elongation> value = property.value();
    return value.has_value() && isFinite(*value) && value->value() >= 0.0;
}

[[nodiscard]] bool storedAndValid(const MaterialProperty<Hardness>& property) {
    const std::optional<Hardness> value = property.value();
    return value.has_value() && value->value() > 0.0;
}

template <typename Value>
[[nodiscard]] bool positiveQuantity(const MaterialProperty<Value>& property) {
    const std::optional<Value> value = property.value();
    return value.has_value() && isFinite(*value) && value->si() > 0.0;
}

[[nodiscard]] MaterialIssue missingIssue(MechKind kind, ConsumerKind consumer) {
    return MaterialIssue{.kind = IssueKind::MissingProperty,
                         .mechanical = kind,
                         .thermal = std::nullopt,
                         .consumer = consumer,
                         .message = std::format("{} is required and is not available",
                                                toString(kind))};
}

[[nodiscard]] MaterialIssue missingIssue(ThermKind kind, ConsumerKind consumer) {
    return MaterialIssue{.kind = IssueKind::MissingProperty,
                         .mechanical = std::nullopt,
                         .thermal = kind,
                         .consumer = consumer,
                         .message = std::format("{} is required and is not available",
                                                toString(kind))};
}

/// Sorts issues into the documented order: kind first, then property, so the
/// result cannot depend on the order the checks happened to run in.
void sortIssues(std::vector<MaterialIssue>& issues) {
    std::ranges::stable_sort(issues, [](const MaterialIssue& a, const MaterialIssue& b) {
        if (a.kind != b.kind) {
            return static_cast<std::uint8_t>(a.kind) < static_cast<std::uint8_t>(b.kind);
        }
        // Mechanical before thermal, each in enumeration order; material-level
        // issues (neither set) sort last within their kind.
        const auto rank = [](const MaterialIssue& issue) {
            if (issue.mechanical) {
                return std::pair{0, static_cast<int>(*issue.mechanical)};
            }
            if (issue.thermal) {
                return std::pair{1, static_cast<int>(*issue.thermal)};
            }
            return std::pair{2, 0};
        };
        return rank(a) < rank(b);
    });
}

} // namespace

std::span<const ConsumerKind> consumerKinds() noexcept {
    return std::span<const ConsumerKind>{kConsumerKinds};
}

std::string_view toString(ConsumerKind consumer) noexcept {
    switch (consumer) {
        case ConsumerKind::MassProperties:
            return "mass properties";
        case ConsumerKind::FeaLinearStatic:
            return "linear static FEA";
        case ConsumerKind::FeaLinearStaticWithGravity:
            return "linear static FEA with gravity";
        case ConsumerKind::FeaYieldStrength:
            return "yield strength assessment";
        case ConsumerKind::ThermalSteady:
            return "steady conduction";
        case ConsumerKind::ThermalTransient:
            return "transient conduction";
        case ConsumerKind::ThermoMechanical:
            return "thermo-mechanical analysis";
    }
    return "unknown consumer";
}

PropertyRequirement requiredProperties(ConsumerKind consumer) {
    switch (consumer) {
        case ConsumerKind::MassProperties:
            // m = rho V. The geometry supplies the volume and the assignment
            // supplies the material; the only PROPERTY needed is the density.
            return {.mechanical = {MechKind::Density}, .thermal = {}};
        case ConsumerKind::FeaLinearStatic:
            // E and nu, and nothing else. A static solve with no body force never
            // touches a mass.
            return {.mechanical = {MechKind::YoungsModulus, MechKind::PoissonRatio},
                    .thermal = {}};
        case ConsumerKind::FeaLinearStaticWithGravity:
            return {.mechanical = {MechKind::Density, MechKind::YoungsModulus,
                                   MechKind::PoissonRatio},
                    .thermal = {}};
        case ConsumerKind::FeaYieldStrength:
            // The yield strength specifically. An ultimate tensile strength does
            // NOT substitute: they are different stresses with different meanings,
            // and accepting one for the other would silently change what was
            // assessed.
            return {.mechanical = {MechKind::YoungsModulus, MechKind::PoissonRatio,
                                   MechKind::YieldStrength},
                    .thermal = {}};
        case ConsumerKind::ThermalSteady:
            // Conductivity ONLY. A steady field does not depend on how much heat
            // the material stores, so requiring a density or a specific heat would
            // block a solvable problem.
            return {.mechanical = {}, .thermal = {ThermKind::ThermalConductivity}};
        case ConsumerKind::ThermalTransient:
            // The diffusivity k/(rho cp) needs all three, and the density lives in
            // the mechanical half (it has one home).
            return {.mechanical = {MechKind::Density},
                    .thermal = {ThermKind::ThermalConductivity,
                                ThermKind::SpecificHeatCapacity}};
        case ConsumerKind::ThermoMechanical:
            return {.mechanical = {MechKind::YoungsModulus, MechKind::PoissonRatio},
                    .thermal = {ThermKind::ThermalExpansion}};
    }
    return {};
}

std::string_view toString(CompletenessState state) noexcept {
    switch (state) {
        case CompletenessState::Ready:
            return "ready";
        case CompletenessState::Incomplete:
            return "incomplete";
        case CompletenessState::Invalid:
            return "invalid";
    }
    return "unknown";
}

std::string_view toString(IssueKind kind) noexcept {
    switch (kind) {
        case IssueKind::MissingProperty:
            return "missing property";
        case IssueKind::InconsistentValues:
            return "inconsistent values";
        case IssueKind::OrphanProvenance:
            return "orphan provenance";
        case IssueKind::ProvenanceIncomplete:
            return "incomplete provenance";
    }
    return "unknown issue";
}

bool isBlocking(IssueKind kind) noexcept {
    switch (kind) {
        case IssueKind::MissingProperty:
        case IssueKind::InconsistentValues:
            return true;
        case IssueKind::OrphanProvenance:
        case IssueKind::ProvenanceIncomplete:
            // Metadata. A missing citation is a documentation gap, not a reason a
            // solver cannot run (ADR-028: "a material with no provenance at all
            // still computes").
            return false;
    }
    return false;
}

bool isAvailable(const MechanicalProperties& properties, MechanicalPropertyKind kind) {
    switch (kind) {
        case MechKind::Density:
            return storedAndValid(properties.density);
        case MechKind::YoungsModulus:
            return storedAndValidPressure(properties.youngsModulus);
        case MechKind::PoissonRatio:
            return storedAndValid(properties.poissonRatio);
        case MechKind::YieldStrength:
            return storedAndValidPressure(properties.yieldStrength);
        case MechKind::UltimateTensileStrength:
            return storedAndValidPressure(properties.ultimateTensileStrength);
        case MechKind::UltimateCompressiveStrength:
            return storedAndValidPressure(properties.ultimateCompressiveStrength);
        case MechKind::ShearStrength:
            return storedAndValidPressure(properties.shearStrength);
        case MechKind::Elongation:
            return storedAndValid(properties.elongation);
        case MechKind::Hardness:
            return storedAndValid(properties.hardness);
        case MechKind::ShearModulus:
        case MechKind::BulkModulus:
            // Derived: available exactly when its inputs are (ADR-027).
            return hasLinearElasticConstants(properties);
    }
    return false;
}

bool isAvailable(const ThermalProperties& properties, ThermalPropertyKind kind) {
    switch (kind) {
        case ThermKind::ThermalConductivity:
            return positiveQuantity(properties.thermalConductivity);
        case ThermKind::SpecificHeatCapacity:
            return positiveQuantity(properties.specificHeatCapacity);
        case ThermKind::ThermalExpansion:
            // A coefficient may legitimately be zero or negative (some ceramics
            // and composites contract on heating), so only finiteness is required.
            return properties.thermalExpansion.value().has_value() &&
                   isFinite(*properties.thermalExpansion.value());
        case ThermKind::MeltingTemperature:
            return positiveQuantity(properties.meltingTemperature);
        case ThermKind::ElectricalResistivity:
            return properties.electricalResistivity.value().has_value() &&
                   properties.electricalResistivity.value()->ohmMetres() > 0.0;
    }
    return false;
}

std::vector<MaterialIssue> inconsistencies(const MechanicalProperties& mechanical,
                                          const ThermalProperties& thermal) {
    std::vector<MaterialIssue> issues;
    // WRAPPED, NOT REIMPLEMENTED. P15-MECH-001 owns the rules and their
    // tolerances; restating them here would be a second source of truth that could
    // drift from the one consumers already validate against.
    //
    // No property is attached, and that is correct rather than lazy: an
    // inconsistency is a statement about a RELATIONSHIP between two properties, so
    // naming one of them would be arbitrary. The message names both.
    for (std::string& problem : mechanicalInconsistencies(mechanical)) {
        issues.push_back(MaterialIssue{.kind = IssueKind::InconsistentValues,
                                       .mechanical = std::nullopt,
                                       .thermal = std::nullopt,
                                       .consumer = std::nullopt,
                                       .message = std::move(problem)});
    }
    // There is no thermal equivalent to wrap: no two thermal properties in this
    // model constrain each other. Conductivity, specific heat and expansion are
    // independent measurements, and a melting temperature bounds none of them.
    // Saying so here is better than an empty function nobody can account for.
    (void)thermal;
    return issues;
}

std::vector<MaterialIssue> traceabilityGaps(const MechanicalProperties& mechanical,
                                           const ThermalProperties& thermal,
                                           const MaterialProvenance& provenance) {
    std::vector<MaterialIssue> issues;

    // Orphan provenance: a citation for a property that has no value. The maps are
    // ordered, so this walks them in enumeration order.
    for (const auto& [kind, record] : provenance.mechanical) {
        if (record.empty()) {
            continue;
        }
        if (!isAvailable(mechanical, kind)) {
            issues.push_back(MaterialIssue{
                .kind = IssueKind::OrphanProvenance,
                .mechanical = kind,
                .thermal = std::nullopt,
                .consumer = std::nullopt,
                .message = std::format("{} has a recorded source but no value", toString(kind))});
        }
    }
    for (const auto& [kind, record] : provenance.thermal) {
        if (record.empty()) {
            continue;
        }
        if (!isAvailable(thermal, kind)) {
            issues.push_back(MaterialIssue{
                .kind = IssueKind::OrphanProvenance,
                .mechanical = std::nullopt,
                .thermal = kind,
                .consumer = std::nullopt,
                .message = std::format("{} has a recorded source but no value", toString(kind))});
        }
    }

    // Provenance that contradicts itself, for the material default and for each
    // per-property record.
    for (const std::string& problem : provenanceProblems(provenance.material)) {
        issues.push_back(MaterialIssue{.kind = IssueKind::ProvenanceIncomplete,
                                       .mechanical = std::nullopt,
                                       .thermal = std::nullopt,
                                       .consumer = std::nullopt,
                                       .message = std::format("the material's source: {}", problem)});
    }
    for (const auto& [kind, record] : provenance.mechanical) {
        for (const std::string& problem : provenanceProblems(record)) {
            issues.push_back(
                MaterialIssue{.kind = IssueKind::ProvenanceIncomplete,
                              .mechanical = kind,
                              .thermal = std::nullopt,
                              .consumer = std::nullopt,
                              .message = std::format("{}: {}", toString(kind), problem)});
        }
    }
    for (const auto& [kind, record] : provenance.thermal) {
        for (const std::string& problem : provenanceProblems(record)) {
            issues.push_back(
                MaterialIssue{.kind = IssueKind::ProvenanceIncomplete,
                              .mechanical = std::nullopt,
                              .thermal = kind,
                              .consumer = std::nullopt,
                              .message = std::format("{}: {}", toString(kind), problem)});
        }
    }
    sortIssues(issues);
    return issues;
}

CompletenessReport consumerCompleteness(const MechanicalProperties& mechanical,
                                        const ThermalProperties& thermal,
                                        const MaterialProvenance& provenance,
                                        ConsumerKind consumer) {
    const PropertyRequirement required = requiredProperties(consumer);
    CompletenessReport report;

    for (const MechKind kind : required.mechanical) {
        if (isAvailable(mechanical, kind)) {
            report.presentMechanical.push_back(kind);
        } else {
            report.missingMechanical.push_back(kind);
            report.issues.push_back(missingIssue(kind, consumer));
        }
    }
    for (const ThermKind kind : required.thermal) {
        if (isAvailable(thermal, kind)) {
            report.presentThermal.push_back(kind);
        } else {
            report.missingThermal.push_back(kind);
            report.issues.push_back(missingIssue(kind, consumer));
        }
    }

    // Inconsistencies are reported wherever they are found, because a
    // contradiction in the data a consumer is about to use is its business even
    // when every required value is present.
    for (MaterialIssue& issue : inconsistencies(mechanical, thermal)) {
        issue.consumer = consumer;
        report.issues.push_back(std::move(issue));
    }
    // Traceability gaps are NOT included: this answers "can the analysis run", and
    // a missing citation does not stop it. generalCompleteness() reports them.
    (void)provenance;

    sortIssues(report.issues);
    const bool missing = !report.missingMechanical.empty() || !report.missingThermal.empty();
    const bool inconsistent = std::ranges::any_of(report.issues, [](const MaterialIssue& issue) {
        return issue.kind == IssueKind::InconsistentValues;
    });
    // Missing outranks invalid: a caller told "incomplete" knows to supply data,
    // and cannot act on "invalid" for a value that is not there yet.
    report.state = missing ? CompletenessState::Incomplete
                           : (inconsistent ? CompletenessState::Invalid : CompletenessState::Ready);
    return report;
}

CompletenessReport generalCompleteness(const MechanicalProperties& mechanical,
                                       const ThermalProperties& thermal,
                                       const MaterialProvenance& provenance) {
    CompletenessReport report;
    for (const MechKind kind : mechanicalPropertyKinds()) {
        if (isAvailable(mechanical, kind)) {
            report.presentMechanical.push_back(kind);
        } else {
            report.missingMechanical.push_back(kind);
        }
    }
    for (const ThermKind kind : thermalPropertyKinds()) {
        if (isAvailable(thermal, kind)) {
            report.presentThermal.push_back(kind);
        } else {
            report.missingThermal.push_back(kind);
        }
    }
    // No MissingProperty issues here, deliberately. A general report lists what a
    // material does not have; calling each absence an ISSUE would make every
    // ordinary material look broken, and would invite a caller to treat this as a
    // readiness gate, which it is not.
    for (MaterialIssue& issue : inconsistencies(mechanical, thermal)) {
        report.issues.push_back(std::move(issue));
    }
    for (MaterialIssue& issue : traceabilityGaps(mechanical, thermal, provenance)) {
        report.issues.push_back(std::move(issue));
    }
    sortIssues(report.issues);

    // A general report is Ready when nothing is WRONG, not when everything is
    // present -- no real material has every property, and a state that is always
    // Incomplete would carry no information.
    const bool blocking = std::ranges::any_of(
        report.issues, [](const MaterialIssue& issue) { return isBlocking(issue.kind); });
    report.state = blocking ? CompletenessState::Invalid : CompletenessState::Ready;
    return report;
}

} // namespace bettercad::materials
