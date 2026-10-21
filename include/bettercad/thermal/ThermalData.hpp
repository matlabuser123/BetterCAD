#pragma once

// P18-DATA-001 -- the thermal analysis data model: canonical intent on one side,
// derived value types on the other, and the line between them.
//
// THE LINE, WHICH IS THE WHOLE MILESTONE:
//
// ```text
// CANONICAL   the analysis definition: which mesh control, which mode, the
//             heat loads, the boundary conditions, the initial condition, the
//             solver and transient settings. Document state. Persisted, undone,
//             redone, revisioned. Names CAD geometry by FaceName and nothing
//             mesh-local.
// DERIVED     everything a solve produces or needs on the way: the DOF map, K,
//             C, Q, T, grad(T), q and the heat balance. Reproducible from the
//             canonical intent plus the current mesh and material. Never
//             persisted as authority, never undo payload, never a member of
//             anything canonical.
// ```
//
// The value types for the derived side live here too -- a gradient triple, a
// flux triple, a heat balance -- because they are VALUES with units, not
// results. The result CONTAINER, which binds those values to a mesh stamp and
// a provenance record, is `ThermalResult.hpp`, and it is deliberately not
// reachable from the analysis object's header: a definition cannot hold a
// result because the result type is not visible where the definition is.
//
// EVERY QUANTITY IS SI THROUGH THE UNITS FRAMEWORK. Temperature is absolute
// (kelvin); there is no Celsius anywhere in the tree, and a temperature below
// 0 K is refused wherever one is accepted. Gradient in K/m, heat flux in W/m^2,
// convection coefficient in W/(m^2 K), generation in W/m^3, power in W, time
// in s. No field below is a bare double except the two solver tolerances,
// which are ratios.
//
// WHAT IS NOT HERE. No conductivity matrix, no capacity matrix, no element, no
// DOF numbering, no load integration, no boundary-condition resolution, no
// solve, no post-processing. Those are P18-ELEM-001 onwards. This file defines
// what they will read and what they will produce.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/thermal/Export.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

namespace bettercad::thermal {

// ---------------------------------------------------------------------------
// Mode
// ---------------------------------------------------------------------------

/// What kind of conduction problem an analysis poses (ADR-049).
///
/// AN ENUMERATION, NOT A BOOL. `bool transient` reads as a flag that can be
/// flipped on any analysis; the two modes have different REQUIRED inputs -- a
/// transient needs an initial condition, a time step and an end time, and a
/// steady analysis must not carry them -- and `validate(definition)` enforces
/// that per mode. An enum also has room for a third mode without a second
/// flag, should one ever be authorized.
enum class ThermalAnalysisMode : std::uint8_t {
    /// `K T = Q`: the temperature field the loads and boundary conditions
    /// settle to, with no time in it.
    Steady,
    /// `C dT/dt + K T = Q`, advanced by backward Euler at a fixed step from a
    /// uniform initial temperature to an end time (ADR-049). The method is
    /// fixed and not a field: there is one, so a selector would be a false
    /// capability.
    Transient,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ThermalAnalysisMode mode) noexcept;

// ---------------------------------------------------------------------------
// Temperature policy
// ---------------------------------------------------------------------------

/// Whether @p temperature is a finite absolute temperature: no NaN, no
/// infinity, and not below 0 K.
///
/// THE ONE PLACE THE POLICY IS WRITTEN. An initial condition, a prescribed
/// temperature, a convection ambient and every nodal temperature in a result
/// ask this same function, so the rule cannot drift between them. 0 K itself
/// is accepted: it is the limit of the scale, mathematically ordinary in
/// linear conduction, and refusing it would need a threshold with no physical
/// basis. Below it is not a temperature.
[[nodiscard]] inline bool isAbsoluteTemperature(Temperature temperature) noexcept {
    return std::isfinite(temperature.si()) && temperature.si() >= 0.0;
}

// ---------------------------------------------------------------------------
// Canonical heat loads
// ---------------------------------------------------------------------------

/// A heat flux through a CAD face, in W/m^2.
///
/// POSITIVE IS INTO THE BODY. A positive `flux` heats the body through that
/// face; a negative one is heat leaving through it, which is ordinary and is
/// accepted rather than refused. The same convention `HeatBalance` uses, so a
/// load's sign and its appearance in the balance agree.
///
/// The target is a `FaceName` and nothing else: no facet handles, no node
/// handles, no snapshot of a resolved set (ADR-032). Its facets are resolved
/// afresh for whatever mesh is current.
struct SurfaceHeatFluxLoad {
    FaceName face{};
    HeatFlux flux{};

    friend bool operator==(const SurfaceHeatFluxLoad&, const SurfaceHeatFluxLoad&) = default;
};

/// Uniform volumetric heat generation in the whole body, in W/m^3.
///
/// Positive generates heat, negative is a volumetric sink. There is no target:
/// ADR-049's scope is one solid, so the body is the whole analysed domain and
/// a region selector would name something that cannot yet vary.
struct VolumetricHeatSourceLoad {
    VolumetricHeatSource generation{};

    friend bool operator==(const VolumetricHeatSourceLoad&, const VolumetricHeatSourceLoad&) = default;
};

/// Which kind of load a `ThermalLoad` carries.
enum class ThermalLoadKind : std::uint8_t {
    SurfaceHeatFlux,
    VolumetricHeatSource,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(ThermalLoadKind kind) noexcept;

/// One canonical heat load: an identity and exactly one strongly-typed payload.
///
/// A VARIANT, for the reason `structural::StructuralLoad` is one: the two
/// payloads have different units -- W/m^2 on a face, W/m^3 in the volume --
/// and a `struct { int type; double value; }` would make one assignable from
/// the other and lose the unit. Each payload is its own type and the compiler
/// keeps them apart.
class BETTERCAD_THERMAL_EXPORT ThermalLoad {
public:
    ThermalLoad(ThermalLoadId id, SurfaceHeatFluxLoad load) : id_(id), payload_(std::move(load)) {}
    ThermalLoad(ThermalLoadId id, VolumetricHeatSourceLoad load) : id_(id), payload_(load) {}

    /// The persisted document identity. There is no second load identifier
    /// in the tree, and this is NOT a structural `LoadId`.
    [[nodiscard]] ThermalLoadId id() const noexcept { return id_; }
    [[nodiscard]] ThermalLoadKind kind() const noexcept;

    /// The payload, or nullptr if this load is of another kind.
    [[nodiscard]] const SurfaceHeatFluxLoad* surfaceHeatFlux() const noexcept {
        return std::get_if<SurfaceHeatFluxLoad>(&payload_);
    }
    [[nodiscard]] const VolumetricHeatSourceLoad* volumetricHeatSource() const noexcept {
        return std::get_if<VolumetricHeatSourceLoad>(&payload_);
    }

    /// The CAD face this load targets, or nullopt for a load on the whole
    /// body. The canonical target, and the only one.
    [[nodiscard]] std::optional<FaceName> target() const noexcept;

    friend bool operator==(const ThermalLoad&, const ThermalLoad&) = default;

private:
    ThermalLoadId id_{};
    std::variant<SurfaceHeatFluxLoad, VolumetricHeatSourceLoad> payload_;
};

/// Checks @p load on its own terms: a valid identity, a valid face selector
/// where there is a face, and a finite value.
///
/// DELIBERATELY DOES NOT CONSULT A MESH. Whether the face RESOLVES is a
/// question about a particular mesh and belongs to P18-LOAD-001; whether the
/// record is well formed is a question about the record, and is this. The
/// division `validate(StructuralLoad)` drew, kept.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void> validate(const ThermalLoad& load);

// ---------------------------------------------------------------------------
// Canonical boundary conditions
// ---------------------------------------------------------------------------

/// A temperature held on a CAD face, in K.
///
/// The thermal sibling of a restraint: it fixes the one degree of freedom a
/// node has, and the nodes it fixes are derived from the current mesh, never
/// stored. The value must be a finite absolute temperature.
struct PrescribedTemperature {
    FaceName face{};
    Temperature value{};

    friend bool operator==(const PrescribedTemperature&, const PrescribedTemperature&) = default;
};

/// Convective exchange with an ambient on a CAD face: `q = h (T_ambient - T)`,
/// positive into the body, so a surface hotter than its ambient loses heat.
///
/// `coefficient` must be finite and POSITIVE. A zero coefficient exchanges
/// nothing and is an adiabatic face wearing a convection record, which is not
/// a weaker condition but one whose intent cannot be acted on -- the same
/// reason an empty restraint mask is refused. A negative coefficient would
/// pump heat against the difference and is not convection.
struct ConvectionBoundary {
    FaceName face{};
    HeatTransferCoefficient coefficient{};
    Temperature ambient{};

    friend bool operator==(const ConvectionBoundary&, const ConvectionBoundary&) = default;
};

/// Which kind of condition a `ThermalBoundaryCondition` carries.
enum class ThermalBoundaryConditionKind : std::uint8_t {
    PrescribedTemperature,
    Convection,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view
toString(ThermalBoundaryConditionKind kind) noexcept;

/// One canonical boundary condition: an identity, a CAD face, and exactly one
/// strongly-typed payload.
///
/// Both payloads target a face, so `face()` is always answerable -- unlike a
/// load, where a volumetric source has no face. A variant all the same,
/// because a held temperature and a convection pair have different units and
/// a shared `double value` would let one be read as the other.
class BETTERCAD_THERMAL_EXPORT ThermalBoundaryCondition {
public:
    ThermalBoundaryCondition(ThermalBoundaryConditionId id, PrescribedTemperature condition)
        : id_(id), payload_(std::move(condition)) {}
    ThermalBoundaryCondition(ThermalBoundaryConditionId id, ConvectionBoundary condition)
        : id_(id), payload_(std::move(condition)) {}

    /// The persisted document identity. NOT a `RestraintId`.
    [[nodiscard]] ThermalBoundaryConditionId id() const noexcept { return id_; }
    [[nodiscard]] ThermalBoundaryConditionKind kind() const noexcept;

    /// The payload, or nullptr if this condition is of another kind.
    [[nodiscard]] const PrescribedTemperature* prescribedTemperature() const noexcept {
        return std::get_if<PrescribedTemperature>(&payload_);
    }
    [[nodiscard]] const ConvectionBoundary* convection() const noexcept {
        return std::get_if<ConvectionBoundary>(&payload_);
    }

    /// The CAD face this condition applies to. Every kind has one.
    [[nodiscard]] const FaceName& face() const noexcept;

    friend bool operator==(const ThermalBoundaryCondition&, const ThermalBoundaryCondition&) = default;

private:
    ThermalBoundaryConditionId id_{};
    std::variant<PrescribedTemperature, ConvectionBoundary> payload_;
};

/// Checks @p condition on its own terms: a valid identity, a valid face
/// selector, a finite absolute temperature, and for convection a finite
/// positive coefficient. Does not consult a mesh, for the reason
/// `validate(ThermalLoad)` gives.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
validate(const ThermalBoundaryCondition& condition);

// ---------------------------------------------------------------------------
// Canonical initial condition
// ---------------------------------------------------------------------------

/// The temperature every node starts at, in K.
///
/// UNIFORM, AND THAT IS THE AUTHORIZED SHAPE (ADR-049). A per-node initial
/// field would be mesh-local state inside canonical intent -- a remesh would
/// orphan it -- and a field from a previous result would be derived state
/// promoted to authority. One value, validated as an absolute temperature.
struct UniformInitialTemperature {
    Temperature value{};

    friend bool operator==(const UniformInitialTemperature&, const UniformInitialTemperature&) = default;
};

/// Finite and not below 0 K; see `isAbsoluteTemperature`.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void>
validate(const UniformInitialTemperature& initial);

// ---------------------------------------------------------------------------
// Canonical solver settings
// ---------------------------------------------------------------------------

/// How the linear system is judged, for the steady solve and for every
/// transient step alike.
///
/// CANONICAL INTENT, for the reason ADR-047 made structural solver settings
/// canonical: a tolerance a user chooses must be undoable, redoable and
/// persistable, and settings that live only as an argument to the solve are
/// none of those. Two ratios and nothing else. There is NO algorithm selector:
/// P18-SOLVE-001 decides how P17's sparse direct solve is reached from below
/// layer 50, and until a second algorithm exists a selector would be a field
/// with one legal value.
struct ThermalSolverSettings {
    /// The normalized residual above which a solve is refused. Finite, positive. The residual is
    /// the independently computed one of the reduced free system,
    /// `r = Kff Tf - (Qf - Kfc Tc)`, normalized as `||r|| / (||Kff|| ||Tf|| + ||beff||)`;
    /// scaling `K` and `Q` leaves it unchanged (P18-SOLVE-001).
    double relativeResidualTolerance = 1.0e-9;
    /// The smallest ratio `min|D| / max|D|` of the factorisation's pivots accepted
    /// before the system is declared singular. Finite, positive.
    double pivotFloor = 1.0e-12;

    friend bool operator==(const ThermalSolverSettings&, const ThermalSolverSettings&) = default;
};

/// Both tolerances finite and positive. A NaN threshold is refused here
/// rather than left to decide a comparison: `r < NaN` is false, so it would
/// reject every solve for a reason no diagnostic could explain.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void> validate(const ThermalSolverSettings& settings);

/// Which time levels a transient analysis keeps.
enum class TransientOutputPolicy : std::uint8_t {
    /// Only the field at the end time.
    FinalOnly,
    /// The field at every step, including the end time.
    EveryStep,
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(TransientOutputPolicy policy) noexcept;

/// The time discretisation of a transient analysis, in seconds.
///
/// A FIXED STEP. Backward Euler at constant `timeStep` from `t = 0` to
/// `endTime` (ADR-049); adaptive stepping is out of scope and has no field
/// here. When `endTime` is not a multiple of the step the LAST step is the
/// shortened remainder, so the run lands on `endTime` exactly and never
/// overshoots it (P18-TRANSIENT-001, `TimeGrid`); what this type fixes is that
/// both are finite, both positive, and the step is no longer than the span.
struct TransientSettings {
    Time timeStep{};
    Time endTime{};
    TransientOutputPolicy output = TransientOutputPolicy::FinalOnly;

    friend bool operator==(const TransientSettings&, const TransientSettings&) = default;
};

/// `0 < timeStep <= endTime`, both finite.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT Result<void> validate(const TransientSettings& settings);

// ---------------------------------------------------------------------------
// Derived value types
// ---------------------------------------------------------------------------

/// The three Cartesian components, in the order every vector field below
/// stores them.
///
/// FROZEN HERE so that P18-POST-001's recovery, any report and any renderer
/// index the same thing. A module that chose its own order would swap two
/// gradient components and produce a flux that is wrong in direction only,
/// which no magnitude check would catch.
enum class VectorComponent : std::uint8_t { X = 0, Y = 1, Z = 2 };

/// The three, in index order.
inline constexpr std::array<VectorComponent, 3> kVectorComponents{
    VectorComponent::X, VectorComponent::Y, VectorComponent::Z};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT std::string_view toString(VectorComponent component) noexcept;

/// grad(T) of one element, in K/m: `[dT/dx, dT/dy, dT/dz]`.
///
/// Constant within a Tet4, so one per element. The field names ARE the
/// components, so a reader cannot take `x` for a position.
struct TemperatureGradient3 {
    TemperatureGradient x{};
    TemperatureGradient y{};
    TemperatureGradient z{};

    [[nodiscard]] constexpr TemperatureGradient component(VectorComponent which) const noexcept {
        switch (which) {
        case VectorComponent::X:
            return x;
        case VectorComponent::Y:
            return y;
        case VectorComponent::Z:
            return z;
        }
        return {};
    }

    friend constexpr bool operator==(const TemperatureGradient3&, const TemperatureGradient3&) = default;
};

/// The heat flux of one element, in W/m^2: `[qx, qy, qz]`.
///
/// q POINTS IN THE DIRECTION HEAT FLOWS, which by Fourier's law is
/// `q = -k grad(T)`: down the temperature gradient. The sign is part of the
/// type's contract and not a convention a consumer may choose; a renderer
/// that drew `+k grad(T)` would show heat flowing from cold to hot.
///
/// THE MAGNITUDE IS DERIVED, NOT STORED. Three components and nothing else;
/// `magnitude()` computes `|q|` every time, so it can never disagree with the
/// components it is the magnitude of.
struct HeatFlux3 {
    HeatFlux x{};
    HeatFlux y{};
    HeatFlux z{};

    [[nodiscard]] constexpr HeatFlux component(VectorComponent which) const noexcept {
        switch (which) {
        case VectorComponent::X:
            return x;
        case VectorComponent::Y:
            return y;
        case VectorComponent::Z:
            return z;
        }
        return {};
    }

    /// `sqrt(qx^2 + qy^2 + qz^2)`, in W/m^2.
    [[nodiscard]] HeatFlux magnitude() const noexcept {
        return HeatFlux::fromSi(std::hypot(x.si(), y.si(), z.si()));
    }

    friend constexpr bool operator==(const HeatFlux3&, const HeatFlux3&) = default;
};

[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool isFinite(const TemperatureGradient3& gradient) noexcept;
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool isFinite(const HeatFlux3& flux) noexcept;

/// The global heat balance of one solved field, every term in W.
///
/// SIGN CONVENTION: HEAT INTO THE BODY IS POSITIVE, for every term. A surface
/// flux load that heats the body is positive; convection from a body hotter
/// than its ambient is negative; the reaction at a prescribed-temperature
/// face is the heat that face had to supply (positive) or absorb (negative)
/// to hold its temperature. With that convention a steady solve satisfies
///
/// ```text
///     applied + generated + convection + reaction = 0
/// ```
///
/// and a transient one satisfies it with the stored-energy rate subtracted:
/// what goes in and is not stored must come out.
///
/// `storedEnergyRate` IS OPTIONAL, NOT ZERO FOR STEADY. A steady analysis has
/// no capacity term, and writing `0 W` would make "there is no such term"
/// indistinguishable from "the stored-energy rate was computed and is zero",
/// which for a transient at thermal equilibrium is a real and different
/// statement. `ThermalResult::create` requires it present for a transient and
/// absent for a steady result.
///
/// THE IMBALANCE IS DERIVED. `netImbalance()` and `normalizedImbalance()` are
/// computed from the terms every time; a stored imbalance could disagree with
/// the terms it summarises.
struct HeatBalance {
    /// Heat entering through surface flux loads.
    Power appliedSurfaceHeat{};
    /// Heat generated in the volume.
    Power generatedHeat{};
    /// Heat entering through convection surfaces (negative when the body is
    /// hotter than its ambient).
    Power convectionHeat{};
    /// Heat supplied through prescribed-temperature faces to hold them.
    Power prescribedTemperatureReaction{};
    /// `d/dt` of the energy stored in the body. Present for a transient
    /// result, absent for a steady one.
    std::optional<Power> storedEnergyRate{};

    /// `in - stored`, which a converged solve drives to zero. In W.
    [[nodiscard]] Power netImbalance() const noexcept {
        return appliedSurfaceHeat + generatedHeat + convectionHeat + prescribedTemperatureReaction -
               storedEnergyRate.value_or(Power{});
    }

    /// The sum of the magnitudes of every term: the scale an imbalance is
    /// judged against. In W.
    [[nodiscard]] Power scale() const noexcept {
        return abs(appliedSurfaceHeat) + abs(generatedHeat) + abs(convectionHeat) +
               abs(prescribedTemperatureReaction) + abs(storedEnergyRate.value_or(Power{}));
    }

    /// `|netImbalance| / scale`, dimensionless, or nullopt when every term is
    /// zero and there is nothing to normalise by. NOT ZERO FOR THAT CASE: a
    /// balance with no heat in it is not a perfectly balanced one, it is one
    /// the metric does not apply to.
    [[nodiscard]] std::optional<double> normalizedImbalance() const noexcept {
        const Power denominator = scale();
        if (denominator.si() == 0.0) {
            return std::nullopt;
        }
        return abs(netImbalance()) / denominator;
    }

    friend bool operator==(const HeatBalance&, const HeatBalance&) = default;
};

/// Every term finite, including the stored-energy rate when present.
[[nodiscard]] BETTERCAD_THERMAL_EXPORT bool isFinite(const HeatBalance& balance) noexcept;

} // namespace bettercad::thermal
