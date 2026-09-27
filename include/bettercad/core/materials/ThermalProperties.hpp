#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Export.hpp>
#include <bettercad/core/materials/MaterialProperty.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstdint>
#include <span>
#include <string_view>

// The physical and thermal properties of a material (P15-THERM-001, ADR-027).
//
// Engineering data only. No heat equation, no mesh, no temperature field, no
// boundary conditions: P18 owns thermal analysis and consumes this through the
// requirement functions in features/Materials.hpp.
//
// DENSITY IS NOT HERE, and that is deliberate. A material has ONE density, and it
// lives in MechanicalProperties because P15-MECH-001 put it there. Thermal
// consumers read that same property through features::requireDensity() and
// features::requireTransientConductionProperties(). A `thermalDensity` beside a
// `mechanicalDensity` would be two authoritative values for one physical
// quantity, which is the defect this note exists to prevent.
namespace bettercad::materials {

/// Every thermal property BetterCAD names, in the order it is reported in.
///
/// Semantic and fixed, so a diagnostic listing several gaps reads the same way
/// every time. Not any container's iteration order. Density is deliberately
/// absent: it is enumerated with the mechanical properties, which is its one
/// home.
enum class ThermalPropertyKind : std::uint8_t {
    ThermalConductivity,
    SpecificHeatCapacity,
    ThermalExpansion,
    MeltingTemperature,
    ElectricalResistivity,
};

/// The kinds, in reporting order.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::span<const ThermalPropertyKind>
thermalPropertyKinds() noexcept;

/// "thermal conductivity", "specific heat capacity", ...
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(ThermalPropertyKind kind) noexcept;

/// What a material is thermally and electrically.
///
/// Every property defaults to Unknown, so a material characterised only
/// mechanically needs no thermal placeholders. Each value is a CONSTANT: a
/// number used as-is over whatever range a consumer works in. A property may
/// record the temperature it was measured at (MaterialProperty::known(value, at)),
/// and that record does NOT make it a function of temperature -- see
/// "future temperature-dependent laws" below.
struct ThermalProperties {
    /// k, in W/(m K). What a future Fourier-law consumer needs: q = -k grad(T).
    MaterialProperty<ThermalConductivity> thermalConductivity{};
    /// cp, in J/(kg K), at constant pressure. Specific -- per unit mass -- and
    /// therefore NOT a total heat capacity, which would be J/K and would depend
    /// on how much of the material there is.
    MaterialProperty<SpecificHeatCapacity> specificHeatCapacity{};
    /// alpha, in 1/K. Linear, and may be NEGATIVE: some real materials contract
    /// when heated, so a positive-only range would reject good data.
    MaterialProperty<ThermalExpansionCoefficient> thermalExpansion{};
    /// An ABSOLUTE thermodynamic temperature, not a temperature interval.
    /// Validated above absolute zero.
    MaterialProperty<Temperature> meltingTemperature{};
    /// Electrical resistivity, in ohm metres, as a scoped strong type rather
    /// than a Quantity (ADR-029): the dimension system has no electric-current
    /// exponent, so this one carries no dimensional checking.
    MaterialProperty<materials::ElectricalResistivity> electricalResistivity{};

    friend bool operator==(const ThermalProperties&, const ThermalProperties&) = default;
};

/// Whether @p properties can be stored.
///
/// Reports EVERY problem it finds, not the first. Unknown properties are not
/// problems, and neither is a partially characterised material: a conductivity
/// without a specific heat is what most datasheets give.
///
/// Also checks each property's reference temperature, where one is recorded, as
/// the absolute temperature it is.
[[nodiscard]] BETTERCAD_CORE_EXPORT Result<void> validate(const ThermalProperties& properties);

/// Whether the thermal conductivity is Known and usable -- what steady
/// conduction needs, and all it needs.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool
hasSteadyConductionProperties(const ThermalProperties& properties);

/// Whether the specific heat capacity is Known and usable.
///
/// Transient conduction needs this, the conductivity AND the density, and the
/// density is not in this struct -- so the whole question is answered by
/// features::requireTransientConductionProperties(), which can see both halves
/// of the material. This answers only the half that lives here.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool
hasSpecificHeatCapacity(const ThermalProperties& properties);

/// Whether the thermal expansion coefficient is Known and usable.
[[nodiscard]] BETTERCAD_CORE_EXPORT bool
hasThermalExpansion(const ThermalProperties& properties);

// ---------------------------------------------------------------------------
// The contract a temperature-dependent property will follow (ADR-027).
//
// NOT IMPLEMENTED HERE, and deliberately so: P15 stores constant properties.
// What is fixed now is the shape the extension must take, so that today's data
// is not thrown away and a later law cannot arrive with different rules.
//
// ADR-027 already fixed the separation: "the separation is between a property's
// IDENTITY -- which material, which property kind, its provenance -- and its
// VALUE REPRESENTATION. P15 implements a constant value; a later milestone
// replaces the value representation with a law ... WITHOUT TOUCHING material
// identity, assignment, or the Known/Unknown/Derivable states."
//
// So a law changes none of: MaterialId, which property kind a value belongs to,
// whether it is Known or Unknown, or the shape of a consumer call. A consumer
// asks `requireX(material)` today and `requireX(material, state)` later: a new
// parameter, not a restructured caller. Evaluating a constant at any temperature
// yields the constant, so a temperature-aware consumer written later works
// against P15 data unchanged.
// ---------------------------------------------------------------------------

/// How a temperature-dependent property will represent its value.
///
/// Recorded now so the extension has one vocabulary. Only `Constant` is
/// implemented; the others exist to be named, not used.
enum class PropertyLawKind : std::uint8_t {
    /// One value, used over the whole range. What P15 stores.
    Constant,
    /// Values at listed temperatures. See temperatureTableInvariants().
    Table,
    /// A closed-form function of temperature.
    AnalyticLaw,
};

/// "constant", "table" or "analytic law".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(PropertyLawKind kind) noexcept;

/// What a future tabulated property must guarantee.
///
/// Fixed now, while there is no table to argue with, because each of these is a
/// defect that is cheap to prevent and expensive to find later. Returned as text
/// so the contract is readable from a test and cannot drift from the prose.
[[nodiscard]] BETTERCAD_CORE_EXPORT std::span<const std::string_view>
temperatureTableInvariants() noexcept;

/// What a future law must do when asked outside the range it covers.
///
/// A law will have to CHOOSE one of these explicitly. The point of naming them
/// now is that there is no default and no silent option: extrapolating without
/// being asked to is how a number from outside a material's measured range ends
/// up in a result.
enum class OutOfRangeBehaviour : std::uint8_t {
    /// Refuse, naming the material, the property and the temperature asked for.
    Fail,
    /// Use the nearest end of the range, and say so.
    Clamp,
    /// Continue the trend beyond the range. Never the default.
    Extrapolate,
};

/// "fail", "clamp" or "extrapolate".
[[nodiscard]] BETTERCAD_CORE_EXPORT std::string_view toString(OutOfRangeBehaviour behaviour) noexcept;

} // namespace bettercad::materials
