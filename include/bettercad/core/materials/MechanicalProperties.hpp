#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/materials/MaterialProperty.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The mechanical properties of a material (P15-MECH-001, ADR-027).
//
// Engineering data only. No element matrices, no mesh, no solver: P17 owns
// structural analysis and consumes this through requireLinearElasticConstants()
// without redefining anything here.
namespace bettercad::materials {

/// Every mechanical property BetterCAD names, in the order it is reported in.
///
/// The order is semantic and fixed -- the elastic constants, then the strengths,
/// then the rest -- so a diagnostic listing several gaps reads the same way every
/// time. It is not any container's iteration order.
enum class MechanicalPropertyKind : std::uint8_t {
    Density,
    YoungsModulus,
    PoissonRatio,
    ShearModulus,
    BulkModulus,
    YieldStrength,
    UltimateTensileStrength,
    UltimateCompressiveStrength,
    ShearStrength,
    Elongation,
    Hardness,
};

/// The kinds, in reporting order.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::span<const MechanicalPropertyKind>
mechanicalPropertyKinds() noexcept;

/// "density", "Young's modulus", "Poisson's ratio", ...
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(MechanicalPropertyKind kind) noexcept;

/// Whether @p kind is stored on a material or computed from other properties.
///
/// Only the shear and bulk moduli are derived, and they are never stored
/// (ADR-027).
[[nodiscard]] BETTERCAD_CORE_EXPORT bool isDerivedKind(MechanicalPropertyKind kind) noexcept;

/// What a material is mechanically.
///
/// Every property defaults to Unknown, so a partially characterised material is
/// constructible without a single placeholder. There is no slot for the shear or
/// bulk modulus, and that absence is the design (ADR-027): both are exactly
/// determined by E and nu for an isotropic material, so storing one would create
/// a second source of truth that could disagree with the first. A supplied G that
/// contradicts E and nu therefore cannot be represented at all, which is why
/// there is no supplied-versus-derived reconciliation here and no
/// `suppliedShearModulus()` to pair with derivedShearModulus().
struct MechanicalProperties {
    MaterialProperty<Density> density{};
    MaterialProperty<ElasticModulus> youngsModulus{};
    MaterialProperty<bettercad::PoissonRatio> poissonRatio{};

    /// Strengths. Independent measurements, never inferred from one another: a
    /// yield strength is not a fraction of an ultimate strength, a compressive
    /// strength is not a tensile strength, and a shear strength is not 0.577 of
    /// anything unless a failure criterion asks for that by name.
    MaterialProperty<Stress> yieldStrength{};
    MaterialProperty<Stress> ultimateTensileStrength{};
    MaterialProperty<Stress> ultimateCompressiveStrength{};
    MaterialProperty<Stress> shearStrength{};

    MaterialProperty<materials::Elongation> elongation{};
    MaterialProperty<materials::Hardness> hardness{};

    friend bool operator==(const MechanicalProperties&, const MechanicalProperties&) = default;
};

/// The bounds a known mechanical property must satisfy to be engineering data.
///
/// These are PHYSICAL validity, which P15-UNITS-001 deliberately kept out of the
/// quantity types: `PoissonRatio` "carries NO range check ... the range belongs
/// to the material property layer (P15-MECH-001)". This is that layer.
namespace limits {

/// Poisson's ratio of an ordinary stable isotropic material: -1 < nu < 0.5.
///
/// Both ends are excluded, and not arbitrarily. At nu = 0.5 the bulk modulus
/// K = E / (3(1 - 2nu)) divides by zero; at nu = -1 the shear modulus
/// G = E / (2(1 + nu)) does. A ratio at either end is not a material this model
/// can describe, so it is refused rather than allowed to reach a division.
inline constexpr double minPoissonRatio = -1.0;
inline constexpr double maxPoissonRatio = 0.5;

} // namespace limits

/// Whether @p properties can be stored.
///
/// Reports EVERY problem it finds, not the first, so a caller fixing a material
/// sees the whole list. Unknown properties are not problems.
///
/// This checks each property on its own. Whether two properties make sense
/// TOGETHER is a different question with a different answer -- see
/// mechanicalInconsistencies() -- because a partially entered material passing
/// through an inconsistent intermediate state must stay constructible.
///
/// It also refuses a stored property in the Derived state. A stored value is
/// supplied or it is unknown; one claiming to have been computed, in a slot with
/// nothing to compute it from, would make a supplied value and a derived one
/// indistinguishable.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<void> validate(const MechanicalProperties& properties);

/// Engineering disagreements between properties that are each individually
/// valid. Empty when there are none.
///
/// Reported, never corrected and never a reason to refuse the data: a user
/// entering a datasheet may have mistyped, and telling them is useful while
/// silently changing their numbers is not.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::vector<std::string>
mechanicalInconsistencies(const MechanicalProperties& properties);

/// The shear modulus, computed from E and nu: G = E / (2(1 + nu)).
///
/// Derived, never stored. Unknown if either input is Unknown or nu is outside
/// limits -- in which case the diagnostic a caller shows should name the missing
/// INPUT, because reporting G as the gap tells the user nothing about what to do.
[[nodiscard]] BETTERCAD_CORE_EXPORT MaterialProperty<ElasticModulus>
derivedShearModulus(const MechanicalProperties& properties);

/// The bulk modulus, computed from E and nu: K = E / (3(1 - 2nu)).
///
/// Derived, never stored, on the same terms as derivedShearModulus().
[[nodiscard]] BETTERCAD_CORE_EXPORT MaterialProperty<ElasticModulus>
derivedBulkModulus(const MechanicalProperties& properties);

/// Whether E and nu are both Known and valid -- the pair linear isotropic
/// elasticity needs, and all it needs.
///
/// Deliberately not one `isComplete()`. Complete for what? Linear elasticity
/// wants E and nu; a mass wants density; a yield check wants a yield strength.
/// One flag covering all of them would mean nothing to any of them.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool
hasLinearElasticConstants(const MechanicalProperties& properties);

/// Whether the density is Known and valid.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool hasDensity(const MechanicalProperties& properties);

/// The four isotropic elastic constants, all concrete.
///
/// What a structural solver needs and the only shape it should need: no
/// optionals, no states, no raw doubles, no knowledge of where the numbers came
/// from. Obtained from features::requireLinearElasticConstants(), which fails
/// with one diagnostic listing every gap rather than handing over a partial set.
struct LinearElasticConstants {
    ElasticModulus youngsModulus{};
    bettercad::PoissonRatio poissonRatio{};
    /// Derived from the two above, not supplied.
    ElasticModulus shearModulus{};
    /// Derived from the two above, not supplied.
    ElasticModulus bulkModulus{};

    friend bool operator==(const LinearElasticConstants&, const LinearElasticConstants&) = default;
};

} // namespace bettercad::materials
