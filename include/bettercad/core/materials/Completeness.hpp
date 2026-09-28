#pragma once

#include <bettercad/core/Export.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// What a consumer needs, what a material has, and the difference (P15-PROV-001).
//
// THE CENTRAL RULE: "complete" is meaningless without a consumer. A material with
// a density and nothing else is complete for a mass and useless for a stress; one
// with a conductivity and nothing else is complete for steady conduction and
// useless for transient. So there is deliberately no `isComplete()` -- the same
// reason P15-MECH-001 refused one: "Complete for what? One flag covering all of
// them would mean nothing to any of them."
//
// MISSING DATA IS REPORTED, NEVER FILLED. No default modulus, no typical density,
// no nu = 0.3 anywhere behind any of this (ADR-027, ADR-028). A consumer that
// cannot obtain a property is told which property, by semantic identity rather
// than by a string a caller would have to parse.
//
// REQUIREMENTS OPERATE ON SEMANTIC IDENTITY, NOT ON DIMENSION. A Young's modulus,
// a yield strength, an ultimate tensile strength and a derived shear modulus are
// all pressures, and none of them substitutes for another. Requiring
// `YoungsModulus` is satisfied by a Young's modulus and by nothing else.
//
// ONE DEFINITION OF WHAT EACH CONSUMER NEEDS. requiredProperties() is it. The
// existing require*() entry points in features/Materials.hpp are the runtime
// contract those consumers actually call, and a test asserts the two agree for
// every consumer by removing each required property in turn -- so the table cannot
// drift away from the functions it describes.
namespace bettercad::materials {

/// Something that consumes material data. Named for the ANALYSIS, not the module,
/// because the requirement is a property of the physics rather than of whoever
/// implements it.
///
/// Only consumers the roadmap actually has. P19 CFD is deliberately ABSENT: ADR-028
/// names it as a future consumer but defines no requirements for it, and inventing
/// them now would be guessing at physics. The mechanism extends to it without a
/// second framework -- a requirement is a list of property kinds -- though a CFD
/// consumer will also need a dynamic-viscosity property, which no material carries
/// yet.
enum class ConsumerKind : std::uint8_t {
    /// Mass, centre of mass and inertia (P15-MASS-001).
    MassProperties,
    /// Linear isotropic elasticity with no body force (P17).
    FeaLinearStatic,
    /// The same, plus self-weight or inertia, which needs a density.
    FeaLinearStaticWithGravity,
    /// Linear elasticity plus a yield-based assessment.
    FeaYieldStrength,
    /// Steady-state conduction (P18). Needs a conductivity and NOTHING else.
    ThermalSteady,
    /// Transient conduction (P18): the diffusivity needs all three of density,
    /// specific heat capacity and conductivity.
    ThermalTransient,
    /// Thermal expansion coupled to elasticity.
    ThermoMechanical,
};

/// The consumers, in reporting order.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::span<const ConsumerKind> consumerKinds() noexcept;
/// "mass properties", "linear static FEA", ...
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(ConsumerKind consumer) noexcept;

/// The properties a consumer requires, by semantic identity.
///
/// Both lists are in property-kind enumeration order, so a missing-property report
/// built from them is deterministic.
struct PropertyRequirement {
    std::vector<MechanicalPropertyKind> mechanical;
    std::vector<ThermalPropertyKind> thermal;
};

/// What @p consumer requires. THE one definition; nothing else may hold a second
/// copy of this table.
///
/// Note what is NOT required, because over-requiring is as wrong as
/// under-requiring and is harder to notice:
///   - ThermalSteady needs a conductivity ONLY. Demanding a density or a specific
///     heat would block a perfectly solvable steady problem.
///   - FeaLinearStatic needs E and nu ONLY. A density is required by the WITH
///     GRAVITY variant, because that is when a mass enters the equations.
///   - No consumer requires a shear or bulk modulus: for an isotropic material
///     both are exactly determined by E and nu (ADR-027), so requiring the
///     independent pair is the honest statement of what is needed.
///   - No consumer requires PROVENANCE. Traceability is a separate question; see
///     traceabilityGaps().
[[nodiscard]] BETTERCAD_CORE_EXPORT PropertyRequirement requiredProperties(ConsumerKind consumer);

/// Whether a material can serve a consumer.
enum class CompletenessState : std::uint8_t {
    /// Every required property is present and valid.
    Ready,
    /// A required property is missing. Not a fault in the material -- it is simply
    /// not characterised that far yet (ADR-027).
    Incomplete,
    /// Every required property is present, but something about the data is wrong.
    Invalid,
};

[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(CompletenessState state) noexcept;

/// What kind of thing a report is complaining about.
enum class IssueKind : std::uint8_t {
    /// A required property has no value.
    MissingProperty,
    /// Two supplied values contradict each other.
    InconsistentValues,
    /// Provenance describes a property that has no value.
    OrphanProvenance,
    /// A provenance record contradicts itself, e.g. a standard with no standard
    /// named. A warning about metadata, never about a number.
    ProvenanceIncomplete,
};

[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(IssueKind kind) noexcept;

/// Whether @p kind stops a consumer or merely deserves attention.
///
/// MissingProperty and InconsistentValues block; the two provenance kinds do not.
/// That separation is the whole of "do not make FEA impossible because a citation
/// is absent".
[[nodiscard]] BETTERCAD_CORE_EXPORT bool isBlocking(IssueKind kind) noexcept;

/// One structured complaint. Structured rather than a sentence, so a GUI or a CLI
/// can present or translate it without parsing text; `message` is a fallback for
/// logs, not the payload.
struct MaterialIssue {
    IssueKind kind = IssueKind::MissingProperty;
    /// Which property, when the issue is about one. Exactly one of these is set
    /// for a property-specific issue, and neither for a material-level one.
    std::optional<MechanicalPropertyKind> mechanical;
    std::optional<ThermalPropertyKind> thermal;
    /// Which consumer raised it, when it came from a consumer-specific query.
    std::optional<ConsumerKind> consumer;
    std::string message;

    friend bool operator==(const MaterialIssue&, const MaterialIssue&) = default;
};

/// What a material has and lacks, for one consumer or in general.
///
/// ORDERING IS DEFINED AND DETERMINISTIC: mechanical properties before thermal,
/// each in its enumeration order, and issues in the order
/// MissingProperty, InconsistentValues, OrphanProvenance, ProvenanceIncomplete --
/// then by property within each. Nothing here is built from an unordered
/// container, so the same material state gives the same report in Debug, Release
/// and Debug-shared.
struct CompletenessReport {
    CompletenessState state = CompletenessState::Ready;
    /// For a consumer query, the required properties that ARE present. For a
    /// general report, every property that has a value.
    std::vector<MechanicalPropertyKind> presentMechanical;
    std::vector<ThermalPropertyKind> presentThermal;
    /// For a consumer query, the required properties that are NOT present. For a
    /// general report, every property without one.
    std::vector<MechanicalPropertyKind> missingMechanical;
    std::vector<ThermalPropertyKind> missingThermal;
    std::vector<MaterialIssue> issues;

    /// Ready and nothing blocking. Provided because it reads better than comparing
    /// the state, NOT as a substitute for it: a caller that wants to know WHY asks
    /// the state and the issues.
    [[nodiscard]] bool ready() const noexcept { return state == CompletenessState::Ready; }

    friend bool operator==(const CompletenessReport&, const CompletenessReport&) = default;
};

/// Whether @p properties has a usable value for @p kind.
///
/// "Usable" means Known or Derived AND within range -- a value that validation
/// would refuse does not count as present, or a report would call a material Ready
/// and then a consumer would fail. For a derived kind it is whether the INPUTS are
/// there, since that is what decides whether a value can be produced.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool isAvailable(const MechanicalProperties& properties,
                                                     MechanicalPropertyKind kind);
[[nodiscard]] BETTERCAD_CORE_EXPORT bool isAvailable(const ThermalProperties& properties,
                                                     ThermalPropertyKind kind);

/// Whether @p properties can serve @p consumer, and what is missing if not.
///
/// Only the consumer's own requirements appear. A material with a conductivity and
/// no density is Ready for ThermalSteady, and the absent density is not reported
/// as a blocker -- it is not needed. The general report is where everything shows.
[[nodiscard]] BETTERCAD_CORE_EXPORT CompletenessReport
consumerCompleteness(const MechanicalProperties& mechanical, const ThermalProperties& thermal,
                     const MaterialProvenance& provenance, ConsumerKind consumer);

/// Everything known and unknown about a material, across every property.
///
/// For inspection and for a UI, and NOT a readiness decision for any consumer:
/// a general report on a material characterised for steady conduction will list
/// plenty of missing properties while ThermalSteady is perfectly Ready. Using this
/// to gate an analysis would block work that can be done.
[[nodiscard]] BETTERCAD_CORE_EXPORT CompletenessReport
generalCompleteness(const MechanicalProperties& mechanical, const ThermalProperties& thermal,
                    const MaterialProvenance& provenance);

/// Supplied values that contradict each other, in reporting order.
///
/// What can be checked here is limited by what can be REPRESENTED, and that is a
/// good thing: ADR-027 gave MechanicalProperties no slot for a shear or bulk
/// modulus, so "a supplied G disagrees with E and nu" -- the classic inconsistency
/// a material model has to police -- cannot arise at all. It was designed out
/// rather than validated.
///
/// What remains is genuine: a yield strength above the ultimate tensile strength.
/// Reported as an issue and never corrected, and deliberately NON-blocking, because
/// the two may legitimately come from different test bases, directions or
/// conditions and BetterCAD is not entitled to overrule a user's data.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::vector<MaterialIssue>
inconsistencies(const MechanicalProperties& mechanical, const ThermalProperties& thermal);

/// Provenance that describes nothing, or describes itself incompletely, in
/// reporting order.
///
/// Both kinds are non-blocking warnings. An orphan citation is the one that
/// matters: provenance for a property with no value is metadata claiming a source
/// for a number that is not there, which is how a document comes to look
/// better-documented than it is.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::vector<MaterialIssue>
traceabilityGaps(const MechanicalProperties& mechanical, const ThermalProperties& thermal,
                 const MaterialProvenance& provenance);

} // namespace bettercad::materials
