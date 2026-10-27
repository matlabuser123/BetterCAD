#pragma once

// P19-FLUID-001 -- resolving the fluid a CFD analysis consumes.
//
// THIN ON PURPOSE, AS P18-MAT-001's FILE WAS, BECAUSE P15 OWNS THE REST. After
// this milestone P15 holds, for this consumer by name (ADR-062):
//
//   MechanicalProperties::density          rho, kg/m^3 -- the material's ONE density
//   ThermalProperties::dynamicViscosity    mu, Pa s, a MaterialProperty: Known or
//                                          Unknown, never a zero standing in for
//                                          "unknown"
//   materials::validate(ThermalProperties) refuses a non-finite or non-positive mu
//                                          AT ENTRY, so such a value cannot be in a
//                                          document at all
//   materials::requiredProperties          IncompressibleFlow needs { rho } and
//                                          { mu } -- "THE one definition"
//   features::requireIncompressibleFlowProperties
//                                          rho and mu, every missing input named
//                                          in one diagnostic, never a default
//
// So there is no range check, no finiteness check, no default and no requirement
// table here: a second copy of any of them would be the second material authority
// ADR-028 forbids. (P19-DATA-001's `validate(CfdFluidProperties)` was exactly such
// a copy, and this milestone retired it.) There is no fluid database, no "air", no "water", and no flag
// that makes a material a fluid -- a fluid is a material that carries a viscosity.
//
// What this file adds: the derived, source-bound view a CFD solve consumes -- the
// material's identity and revision, rho and mu by value, and nu = mu / rho computed
// on demand and never stored (ADR-058).

#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/materials/Completeness.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace bettercad {
class Document;
} // namespace bettercad

namespace bettercad::cfd {

/// P15's consumer for every CFD analysis of v1: laminar incompressible Newtonian
/// flow. One value, because ADR-058 admits one physics; the requirement table is
/// P15's, the choice of row is CFD's.
inline constexpr materials::ConsumerKind kFluidConsumer = materials::ConsumerKind::IncompressibleFlow;

/// Why the fluid cannot be resolved. Each value is reachable and each is a
/// different thing for the user to do.
enum class FluidProblem : std::uint8_t {
    /// Nothing is assigned, or what is assigned is gone. Not a solve with air.
    NoMaterialAssigned,
    /// A material resolves but has no usable density. (When the viscosity is
    /// missing too, the DIAGNOSTIC names both; the category is the first gap in
    /// P15's order.)
    DensityUnavailable,
    /// The density is usable and the viscosity is not: absent, or -- through a
    /// path P15 does not allow -- non-finite or non-positive.
    ViscosityUnavailable,
};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(FluidProblem problem) noexcept;

/// The fluid one CFD analysis consumes, resolved and validated.
///
/// DERIVED, AND REBUILT EVERY TIME: not cached, no setter. It holds the P15
/// material's `MaterialId` and revision as REFERENCES BACK, and the two values P15
/// handed over; no name, no provenance copy and nothing it was not asked for.
///
/// WHAT POSSESSION PROVES:
///
/// ```text
/// a material is assigned and resolves
/// rho is present, finite and positive
/// mu is present, finite and positive
/// ```
///
/// CONSTANT, NEWTONIAN (ADR-058). P15 stores one scalar mu and no law; there is
/// nothing shear-rate- or temperature-dependent here to reduce or reject.
class BETTERCAD_CFD_EXPORT CfdFluid {
public:
    /// Which canonical P15 material this came from.
    [[nodiscard]] MaterialId id() const noexcept { return id_; }

    /// Its document revision when it was resolved: what makes "the same material
    /// with an edited viscosity" a different solver input. A `MaterialId` alone
    /// cannot say that, which is why `CfdResultSource` carries both.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /// rho and mu, as the solver consumes them.
    [[nodiscard]] const CfdFluidProperties& properties() const noexcept { return properties_; }

    /// rho, in kg/m^3.
    [[nodiscard]] Density density() const noexcept { return properties_.density; }

    /// mu, in Pa s.
    [[nodiscard]] DynamicViscosity viscosity() const noexcept { return properties_.viscosity; }

    /// nu = mu / rho, in m^2/s. DERIVED on every call, never stored, so it cannot
    /// disagree with the two it comes from.
    [[nodiscard]] KinematicViscosity kinematicViscosity() const noexcept {
        return properties_.kinematicViscosity();
    }

    /// Equality over the solver inputs AND the source identity: two revisions of
    /// one material with different viscosities are different inputs.
    friend bool operator==(const CfdFluid&, const CfdFluid&) = default;

private:
    CfdFluid(MaterialId id, std::uint64_t revision, CfdFluidProperties properties) noexcept
        : id_(id), revision_(revision), properties_(properties) {}

    /// The one function permitted to build one.
    friend BETTERCAD_CFD_EXPORT Result<CfdFluid> resolveCfdFluid(const Document& document);

    MaterialId id_{};
    std::uint64_t revision_ = 0;
    CfdFluidProperties properties_{};
};

/// Resolves the document's fluid, or fails saying why.
///
/// READ-ONLY, AND A REFUSAL IS NEVER A REPAIR: nothing takes an absolute value,
/// substitutes an epsilon or a typical value, or writes to the document. The
/// diagnostic is P15's, unchanged, naming the material and every missing input.
///
/// THE DOCUMENT'S ASSIGNMENT IS THE FLUID (ADR-060): P15 has one material per
/// document, so no body argument would select anything. When per-body materials
/// exist, the analysed body becomes an argument and the call does not change shape.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdFluid> resolveCfdFluid(const Document& document);

/// The problem `resolveCfdFluid` would report, or none.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<FluidProblem> cfdFluidProblem(const Document& document);

/// P15's completeness report of the document's material for the incompressible-
/// flow consumer, unreinterpreted: every required property that is and is not
/// available, in P15's deterministic order -- AND P15's material-level findings,
/// which it reports for every consumer. In particular an INCONSISTENT material (a
/// yield strength above its ultimate strength, say) is reported `Invalid` here,
/// while `resolveCfdFluid`, which needs only rho and mu, accepts it. That is P15's
/// policy for every consumer, not a CFD rule (P18's thermal resolution behaves the
/// same), and it is recorded rather than re-decided here. Fails if nothing is
/// assigned.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<materials::CompletenessReport>
cfdFluidCompleteness(const Document& document);

} // namespace bettercad::cfd
