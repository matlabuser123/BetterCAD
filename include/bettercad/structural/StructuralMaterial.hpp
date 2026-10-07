#pragma once

// P17-MAT-001 -- resolving the material a structural solve consumes.
//
// THIS FILE IS THIN ON PURPOSE, AND THAT IS THE RESULT RATHER THAN THE AMBITION.
// The audit found that P15 already owns every validation a linear-elastic solve
// needs, and owns it for this consumer BY NAME:
//
//   materials::requiredProperties(ConsumerKind)   "THE one definition; nothing
//                                                 else may hold a second copy of
//                                                 this table". FeaLinearStatic
//                                                 needs E and nu ONLY; the WITH
//                                                 GRAVITY variant adds a density
//   features::effectiveMaterialCompleteness       per-consumer, deterministic
//                                                 order, property identity, and
//                                                 it distinguishes "no material
//                                                 is assigned" from "the
//                                                 assigned material lacks
//                                                 something" (ADR-026)
//   features::requireLinearElasticConstants       refuses a missing, non-finite
//                                                 or non-positive E, and a nu
//                                                 outside -1 < nu < 0.5
//   features::requireDensity                      refuses a missing, non-finite
//                                                 or non-positive density
//   materials::isAvailable                        "Known or Derived AND within
//                                                 range -- a value that
//                                                 validation would refuse does
//                                                 not count as present"
//
// So there is no range check, no finiteness check and no requirement table
// here. ADR-028's hard rule is that a downstream solver maintains no second
// authoritative material database, and a second copy of the VALIDATION would be
// the same defect one layer along: two notions of a usable Poisson ratio that
// agree today and drift later.
//
// What this file adds is the one thing P15 cannot know: WHICH consumer a given
// analysis is, and therefore whether a density is required. That is analysis
// intent, it lives on the analysis definition, and it is read from there.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralData.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace bettercad {
class Document;
} // namespace bettercad

namespace bettercad::structural {

/// P15's consumer for @p mode: the requirement table is theirs, the choice of
/// which row to read is the analysis's.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT materials::ConsumerKind
consumerFor(StructuralAnalysisMode mode) noexcept;

/// Why a structural material cannot be resolved.
///
/// Three values, each reachable and each a different thing for the user to do.
/// NOT a fourth for "present but out of range": P15 folds that into
/// unavailability deliberately -- `isAvailable` counts a value validation would
/// refuse as absent, because otherwise a report would call a material Ready and
/// then the consumer would fail. The distinction survives in the DIAGNOSTIC,
/// where P15 says "has no Young's modulus" or "has a Young's modulus of X,
/// which is not a usable value". Adding a structural enumerator for it would
/// mean deciding here what "out of range" means, which is the duplication this
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
    /// available -- absent, non-finite or out of range. P15's diagnostic names
    /// which, and the completeness report lists them in its own deterministic
    /// order.
    RequiredPropertyUnavailable,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(MaterialProblem problem) noexcept;

/// The material one structural analysis consumes, resolved and validated.
///
/// DERIVED, AND REBUILT EVERY TIME. It is not cached and there is no setter:
/// resolving it is a handful of lookups against P15, which is nothing beside a
/// solve, and a cache would need invalidating against exactly the material
/// revision that `StructuralResultSource` already tracks. Recomputing is both
/// simpler and impossible to get stale.
///
/// NOT A SECOND CANONICAL RECORD. It holds the material's `MaterialId` and
/// revision as REFERENCES BACK to P15, and four elastic constants that P15
/// derived. It holds no name, no provenance copy, no database entry and no
/// property it was not asked for.
///
/// WHAT POSSESSION PROVES, for the mode it was resolved in:
///
/// ```text
/// the body exists
/// a material is assigned and resolves
/// E is present, finite and positive
/// nu is present, finite and within -1 < nu < 0.5
/// G and K are derived from them by P15 (ADR-027)
/// a density is present, finite and positive  -- IF the mode requires one
/// ```
class BETTERCAD_STRUCTURAL_EXPORT StructuralMaterial {
public:
    /// Which canonical P15 material this came from.
    [[nodiscard]] MaterialId id() const noexcept { return id_; }

    /// Its document revision when it was resolved.
    ///
    /// The traceability half of the answer, and what makes "the same material
    /// with a changed modulus" a different solver input. A `MaterialId` alone
    /// cannot say that, which is why `StructuralResultSource` carries both.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /// The mode this was resolved for. Part of the identity: the same material
    /// resolved for gravity and without it are different solver inputs, because
    /// one of them guarantees a density and the other does not.
    [[nodiscard]] StructuralAnalysisMode mode() const noexcept { return mode_; }

    /// E, nu, and the G and K P15 derives from them.
    [[nodiscard]] const materials::LinearElasticConstants& elastic() const noexcept {
        return elastic_;
    }

    /// The density, present exactly when `mode()` requires one.
    ///
    /// EMPTY IS NOT "UNKNOWN" HERE. For `LinearStatic` it means the analysis
    /// does not need one, which is P15's own position: requiring a density for
    /// a problem with no mass in its equations "would block a perfectly
    /// solvable" case. A material may well carry a density that this view
    /// leaves out, and that is correct rather than lossy -- the view is what
    /// this analysis consumes, not what the material has.
    [[nodiscard]] const std::optional<Density>& density() const noexcept { return density_; }

    /// Equality over the effective solver inputs AND the source identity.
    ///
    /// Deliberately not `MaterialId` alone: two revisions of one material with
    /// different moduli are different solver inputs, and comparing only the ID
    /// would call them the same.
    friend bool operator==(const StructuralMaterial&, const StructuralMaterial&) = default;

private:
    StructuralMaterial(MaterialId id, std::uint64_t revision, StructuralAnalysisMode mode,
                       materials::LinearElasticConstants elastic,
                       std::optional<Density> density) noexcept
        : id_(id), revision_(revision), mode_(mode), elastic_(elastic), density_(density) {}

    /// The one function permitted to build one.
    friend BETTERCAD_STRUCTURAL_EXPORT Result<StructuralMaterial> resolveStructuralMaterial(
        const Document& document, ObjectId body, StructuralAnalysisMode mode);

    MaterialId id_{};
    std::uint64_t revision_ = 0;
    StructuralAnalysisMode mode_{};
    materials::LinearElasticConstants elastic_{};
    std::optional<Density> density_{};
};

/// Resolves the material @p body is made of, for @p mode, or fails saying why.
///
/// READ-ONLY IN EVERY ARGUMENT, and that is a hard rule rather than an
/// implementation detail. An invalid material is a refusal, never a repair:
/// nothing here takes an absolute value, clamps a Poisson ratio or substitutes
/// a density, and nothing writes to the document. P15's rule is that "missing
/// data is reported, never filled", and a solver that filled it would make the
/// user's model silently different from the one they described.
///
/// @p body names the structural target. P15 assigns a material per DOCUMENT
/// today -- there is no inheritance and no per-body assignment -- so the body is
/// used to establish that the target exists rather than to select the material.
/// It is in the signature because the question "what is this body made of" is
/// the one being asked, and because a per-body assignment would change the
/// answer and not the call.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<StructuralMaterial>
resolveStructuralMaterial(const Document& document, ObjectId body, StructuralAnalysisMode mode);

/// The problem `resolveStructuralMaterial` would report, or none if it would
/// succeed.
///
/// For a caller that wants to count or present the category rather than parse a
/// message, as `meshing::geometryIneligibility` is to
/// `requireMeshableGeometry`.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::optional<MaterialProblem>
structuralMaterialProblem(const Document& document, ObjectId body, StructuralAnalysisMode mode);

/// P15's completeness report for @p mode's consumer, naming every required
/// property that is and is not available.
///
/// Exposed because a failure's useful content is the PROPERTY LIST, and it is
/// P15's to produce: deterministic order, semantic property identity, and only
/// the properties this consumer actually requires -- no thermal conductivity
/// reported at a structural analysis. A caller that needs to show a user what
/// to supply asks this; one that needs to fail asks `resolveStructuralMaterial`.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<materials::CompletenessReport>
structuralMaterialCompleteness(const Document& document, StructuralAnalysisMode mode);

} // namespace bettercad::structural
