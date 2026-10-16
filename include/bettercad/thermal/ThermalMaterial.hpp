#pragma once

// P18-MAT-001 -- resolving the material a thermal analysis consumes.
//
// THIS FILE IS THIN ON PURPOSE, AND THAT IS THE RESULT RATHER THAN THE AMBITION.
// The audit found that P15 already owns every property, requirement and
// validation this milestone's checklist names, and owns them for THIS consumer
// by name:
//
//   ThermalProperties::thermalConductivity   k, W/(m K), a MaterialProperty:
//                                            Known, Unknown or Derived, never a
//                                            zero standing in for "unknown"
//   ThermalProperties::specificHeatCapacity  cp, J/(kg K), likewise
//   MechanicalProperties::density            rho, kg/m^3 -- the material's ONE
//                                            density, read from its one home;
//                                            P15 refused a thermal density
//                                            beside it and said why
//   materials::requiredProperties            ThermalSteady needs { k } ONLY;
//                                            ThermalTransient needs { rho } and
//                                            { k, cp }. "THE one definition;
//                                            nothing else may hold a second
//                                            copy of this table"
//   materials::validate(ThermalProperties)   refuses a non-finite or
//                                            non-positive k or cp AT ENTRY, so
//                                            such a value cannot be in a
//                                            document at all
//   features::requireThermalConductivity     refuses a missing, non-finite or
//                                            non-positive k and names the
//                                            material
//   features::requireTransientConductionProperties
//                                            rho, cp and k, every missing input
//                                            named at once, in one diagnostic
//   features::requireEffectiveMaterial       distinguishes "nothing assigned"
//                                            from "the assigned material is
//                                            gone" (ADR-026)
//
// So there is no range check, no finiteness check, no default and no
// requirement table here. ADR-028's hard rule is that a downstream solver
// maintains no second authoritative material database, and a second copy of
// the VALIDATION would be the same defect one layer along.
//
// What this file adds is the one thing P15 cannot know: WHICH consumer a given
// thermal analysis is -- steady or transient -- and therefore whether a density
// and a specific heat are required. That is analysis intent, it lives on the
// analysis definition as `ThermalAnalysisMode`, and it is read from there.
// The shape is P17-MAT-001's `StructuralMaterial`, adopted rather than
// re-derived.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/core/units/Quantity.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/thermal/Export.hpp>
#include <bettercad/thermal/ThermalData.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace bettercad {
class Document;
} // namespace bettercad

namespace bettercad::thermal {

/// P15's consumer for @p mode: the requirement table is theirs, the choice of
/// which row to read is the analysis's.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT materials::ConsumerKind
consumerFor(ThermalAnalysisMode mode) noexcept;

/// Why a thermal material cannot be resolved.
///
/// Three values, each reachable and each a different thing for the user to do.
/// NOT a fourth for "present but out of range": P15 folds that into
/// unavailability deliberately -- `isAvailable` counts a value validation would
/// refuse as absent -- and in any case refuses such a value at entry, so it
/// cannot reach a document. The distinction survives in the DIAGNOSTIC, where
/// P15 says "has no thermal conductivity" or "has a thermal conductivity of X,
/// which is not a usable value". Adding a thermal enumerator for it would mean
/// deciding here what "out of range" means, which is the duplication this
/// milestone exists to avoid.
enum class MaterialProblem : std::uint8_t {
    /// The document has no object with that id, so there is no body to resolve
    /// a material for.
    BodyNotFound,
    /// Nothing is assigned, or what is assigned is gone. Not a solve with
    /// defaults: ADR-028 forbids this module holding material data, so there is
    /// nothing to fall back to and nothing should be.
    NoMaterialAssigned,
    /// A material resolves, but a property this analysis mode requires is not
    /// available -- absent, or (through a path P15 does not currently allow)
    /// non-finite or non-positive. P15's diagnostic names which, and the
    /// completeness report lists them in its own deterministic order.
    RequiredPropertyUnavailable,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(MaterialProblem problem) noexcept;

/// rho cp, in J/(m^3 K): the heat a unit volume stores per kelvin.
///
/// THE TYPE IS THE DIMENSION'S OWN, not a new core quantity: `Density *
/// SpecificHeatCapacity` already has this dimension, (M L^-3)(L^2 T^-2 Theta^-1)
/// = M L^-1 T^-2 Theta^-1, and the alias only names it. No unit, no literal and
/// no catalog entry are added, because nothing stores one of these: it is
/// derived from rho and cp every time it is asked for, so it can never drift
/// from them.
using VolumetricHeatCapacity =
    Quantity<dimensions::density * dimensions::specificHeatCapacity>;

/// What the capacity matrix needs from a material: rho and cp, both concrete.
///
/// Present on a resolved material exactly when the mode is transient, and then
/// BOTH are present -- there is no half-resolved capacity. P18-ELEM-001's
/// capacity matrix is `C = integral rho cp N^T N dV`, so this is the pair it
/// reads.
struct ThermalCapacity {
    Density density{};
    SpecificHeatCapacity specificHeatCapacity{};

    /// rho cp, derived. Never stored, never canonical.
    [[nodiscard]] VolumetricHeatCapacity volumetric() const noexcept {
        return density * specificHeatCapacity;
    }

    friend bool operator==(const ThermalCapacity&, const ThermalCapacity&) = default;
};

/// The material one thermal analysis consumes, resolved and validated.
///
/// DERIVED, AND REBUILT EVERY TIME. It is not cached and there is no setter:
/// resolving it is a handful of lookups against P15, which is nothing beside a
/// solve, and a cache would need invalidating against exactly the material
/// revision that `ThermalResultSource` already tracks. Recomputing is both
/// simpler and impossible to get stale.
///
/// NOT A SECOND CANONICAL RECORD. It holds the material's `MaterialId` and
/// revision as REFERENCES BACK to P15, a conductivity P15 handed over, and a
/// capacity pair P15 handed over when the mode needs one. It holds no name, no
/// provenance copy, no database entry and no property it was not asked for.
///
/// WHAT POSSESSION PROVES, for the mode it was resolved in:
///
/// ```text
/// the body exists
/// a material is assigned and resolves
/// k is present, finite and positive
/// rho is present, finite and positive   -- IF the mode is transient
/// cp is present, finite and positive    -- IF the mode is transient
/// ```
///
/// ISOTROPIC, CONSTANT. P15 stores one scalar `k` and no tensor and no law
/// (ADR-027 fixes the shape a law will take; ADR-049 fixes that P18 consumes a
/// constant). There is nothing anisotropic or temperature-dependent in a
/// document for this to reduce, sample or reject, and nothing here does.
class BETTERCAD_THERMAL_EXPORT ThermalMaterial {
public:
    /// Which canonical P15 material this came from.
    [[nodiscard]] MaterialId id() const noexcept { return id_; }

    /// Its document revision when it was resolved.
    ///
    /// The traceability half of the answer, and what makes "the same material
    /// with a changed conductivity" a different solver input. A `MaterialId`
    /// alone cannot say that, which is why `ThermalResultSource` carries both.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /// The mode this was resolved for. Part of the identity: the same material
    /// resolved for a steady and for a transient analysis are different solver
    /// inputs, because one of them guarantees a capacity and the other does not.
    [[nodiscard]] ThermalAnalysisMode mode() const noexcept { return mode_; }

    /// k, in W/(m K). Always present: every conduction analysis needs it.
    [[nodiscard]] ThermalConductivity conductivity() const noexcept { return conductivity_; }

    /// rho and cp, present exactly when `mode()` is transient.
    ///
    /// EMPTY IS NOT "UNKNOWN" HERE. For `Steady` it means the analysis does
    /// not need them, which is P15's own position: requiring a density or a
    /// specific heat for `K T = Q` "would block a perfectly solvable steady
    /// problem". A material may well carry both and this view leaves them out,
    /// and that is correct rather than lossy -- the view is what this analysis
    /// consumes, not what the material has. For `Transient` it is engaged, and
    /// a caller resolving for that mode may rely on it.
    [[nodiscard]] const std::optional<ThermalCapacity>& capacity() const noexcept {
        return capacity_;
    }

    /// Equality over the effective solver inputs AND the source identity.
    ///
    /// Deliberately not `MaterialId` alone: two revisions of one material with
    /// different conductivities are different solver inputs, and comparing only
    /// the ID would call them the same.
    friend bool operator==(const ThermalMaterial&, const ThermalMaterial&) = default;

private:
    ThermalMaterial(MaterialId id, std::uint64_t revision, ThermalAnalysisMode mode,
                    ThermalConductivity conductivity, std::optional<ThermalCapacity> capacity) noexcept
        : id_(id), revision_(revision), mode_(mode), conductivity_(conductivity), capacity_(capacity) {}

    /// The one function permitted to build one.
    friend BETTERCAD_THERMAL_EXPORT Result<ThermalMaterial> resolveThermalMaterial(
        const Document& document, ObjectId body, ThermalAnalysisMode mode);

    MaterialId id_{};
    std::uint64_t revision_ = 0;
    ThermalAnalysisMode mode_{};
    ThermalConductivity conductivity_{};
    std::optional<ThermalCapacity> capacity_{};
};

/// Resolves the material @p body is made of, for @p mode, or fails saying why.
///
/// READ-ONLY IN EVERY ARGUMENT, and that is a hard rule rather than an
/// implementation detail. An invalid material is a refusal, never a repair:
/// nothing here takes an absolute value, replaces a value by an epsilon or
/// substitutes a density, and nothing writes to the document. P15's rule is
/// that "missing data is reported, never filled", and a solver that filled it
/// would make the user's model silently different from the one they
/// described.
///
/// WHICH PROPERTIES ARE REQUIRED IS READ FROM P15's TABLE, not hardcoded here:
/// `requiredProperties(consumerFor(mode))` says whether a density and a
/// specific heat are needed, and this asks the table. That is the one line
/// that makes the two impossible to disagree, and it is the one place a GUI,
/// a CLI or P18-VALID-001 should obtain the policy from.
///
/// @p body names the thermal target. P15 assigns a material per DOCUMENT
/// today -- there is no inheritance and no per-body assignment -- so the body
/// is used to establish that the target exists rather than to select the
/// material (ADR-049's scope is one solid). It is in the signature because the
/// question "what is this body made of" is the one being asked, and because a
/// per-body assignment would change the answer and not the call.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalMaterial>
resolveThermalMaterial(const Document& document, ObjectId body, ThermalAnalysisMode mode);

/// The problem `resolveThermalMaterial` would report, or none if it would
/// succeed. For a caller that wants to count or present the category rather
/// than parse a message.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::optional<MaterialProblem>
thermalMaterialProblem(const Document& document, ObjectId body, ThermalAnalysisMode mode);

/// P15's completeness report for @p mode's consumer, naming every required
/// property that is and is not available.
///
/// Exposed because a failure's useful content is the PROPERTY LIST, and it is
/// P15's to produce: deterministic order (mechanical kinds, then thermal
/// kinds, each in enumeration order), semantic property identity, and only
/// the properties this consumer actually requires -- no Young's modulus
/// reported at a thermal analysis, no density reported at a steady one. A
/// caller that needs to show a user what to supply asks this; one that needs
/// to fail asks `resolveThermalMaterial`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<materials::CompletenessReport>
thermalMaterialCompleteness(const Document& document, ThermalAnalysisMode mode);

/// Which thermal analyses a material can serve, as of now.
///
/// DERIVED, NEVER STORED: both answers are P15's `materialCompleteness` for
/// the two thermal consumers, asked at the moment of the call. A stored flag
/// could drift from the property values it summarises; this cannot.
struct ThermalCapability {
    /// k is present and usable.
    bool steady = false;
    /// k, rho and cp are all present and usable.
    bool transient = false;

    friend bool operator==(const ThermalCapability&, const ThermalCapability&) = default;
};

/// The capability of the material with @p id, or an error if there is no
/// such material.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<ThermalCapability>
thermalCapability(const Document& document, MaterialId id);

} // namespace bettercad::thermal
