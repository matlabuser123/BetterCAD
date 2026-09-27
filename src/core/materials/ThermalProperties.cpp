#include <bettercad/core/materials/ThermalProperties.hpp>

#include <bettercad/core/units/Format.hpp>

#include <format>
#include <optional>
#include <string>
#include <vector>

namespace bettercad::materials {

namespace {

constexpr ThermalPropertyKind kKinds[] = {
    ThermalPropertyKind::ThermalConductivity,
    ThermalPropertyKind::SpecificHeatCapacity,
    ThermalPropertyKind::ThermalExpansion,
    ThermalPropertyKind::MeltingTemperature,
    ThermalPropertyKind::ElectricalResistivity,
};

/// The invariants a tabulated property will have to satisfy. Text, so a test can
/// read the contract rather than restating it.
constexpr std::string_view kTableInvariants[] = {
    "the independent variable is absolute temperature",
    "values carry their property's own strong type",
    "temperature points are ordered, ascending",
    "temperature points are deterministic, never from unordered iteration",
    "duplicate temperature points are invalid",
    "behaviour outside the range is chosen explicitly, never defaulted",
};

/// A stored property is supplied or unknown, never Derived -- the same rule the
/// mechanical properties hold, for the same reason: a stored value claiming to
/// have been computed, with nothing to have computed it from, would make a
/// supplied value and a derived one indistinguishable.
template <typename Value>
void checkNotDerived(const MaterialProperty<Value>& property, std::string_view name,
                     std::vector<std::string>& problems) {
    if (property.isDerived()) {
        problems.emplace_back(
            std::format("{} is marked derived, but a stored property is supplied or unknown", name));
    }
}

/// A known quantity that must be finite and strictly positive.
template <Dimension D>
void checkPositive(const MaterialProperty<Quantity<D>>& property, std::string_view name,
                   std::vector<std::string>& problems) {
    const std::optional<Quantity<D>> value = property.value();
    if (!value) {
        return;
    }
    if (!isFinite(*value)) {
        problems.emplace_back(std::format("{} is not a finite value", name));
        return;
    }
    if (value->si() <= 0.0) {
        problems.emplace_back(
            std::format("{} must be greater than zero, not {}", name, toString(*value)));
    }
}

/// A property's reference temperature, if it recorded one.
template <typename Value>
void checkReferenceTemperature(const MaterialProperty<Value>& property, std::string_view name,
                               std::vector<std::string>& problems) {
    if (std::optional<std::string> problem =
            referenceTemperatureProblem(property.referenceTemperature(), name)) {
        problems.push_back(std::move(*problem));
    }
}

} // namespace

std::span<const ThermalPropertyKind> thermalPropertyKinds() noexcept {
    return std::span<const ThermalPropertyKind>{kKinds};
}

std::string_view toString(ThermalPropertyKind kind) noexcept {
    switch (kind) {
    case ThermalPropertyKind::ThermalConductivity:
        return "thermal conductivity";
    case ThermalPropertyKind::SpecificHeatCapacity:
        return "specific heat capacity";
    case ThermalPropertyKind::ThermalExpansion:
        return "thermal expansion coefficient";
    case ThermalPropertyKind::MeltingTemperature:
        return "melting temperature";
    case ThermalPropertyKind::ElectricalResistivity:
        return "electrical resistivity";
    }
    return "unknown property";
}

std::string_view toString(PropertyLawKind kind) noexcept {
    switch (kind) {
    case PropertyLawKind::Constant:
        return "constant";
    case PropertyLawKind::Table:
        return "table";
    case PropertyLawKind::AnalyticLaw:
        return "analytic law";
    }
    return "constant";
}

std::string_view toString(OutOfRangeBehaviour behaviour) noexcept {
    switch (behaviour) {
    case OutOfRangeBehaviour::Fail:
        return "fail";
    case OutOfRangeBehaviour::Clamp:
        return "clamp";
    case OutOfRangeBehaviour::Extrapolate:
        return "extrapolate";
    }
    return "fail";
}

std::span<const std::string_view> temperatureTableInvariants() noexcept {
    return std::span<const std::string_view>{kTableInvariants};
}

Result<void> validate(const ThermalProperties& properties) {
    std::vector<std::string> problems;

    checkNotDerived(properties.thermalConductivity, "thermal conductivity", problems);
    checkNotDerived(properties.specificHeatCapacity, "specific heat capacity", problems);
    checkNotDerived(properties.thermalExpansion, "thermal expansion coefficient", problems);
    checkNotDerived(properties.meltingTemperature, "melting temperature", problems);
    checkNotDerived(properties.electricalResistivity, "electrical resistivity", problems);

    checkPositive(properties.thermalConductivity, "thermal conductivity", problems);
    checkPositive(properties.specificHeatCapacity, "specific heat capacity", problems);

    // The thermal expansion coefficient is checked for FINITENESS ONLY. Negative
    // thermal expansion is real -- some materials contract when heated -- so a
    // positive-only range would reject good engineering data, and no standard
    // BetterCAD has justifies an upper bound either.
    if (const std::optional<ThermalExpansionCoefficient> alpha = properties.thermalExpansion.value()) {
        if (!isFinite(*alpha)) {
            problems.emplace_back("thermal expansion coefficient is not a finite value");
        }
    }

    // An ABSOLUTE thermodynamic temperature, so above absolute zero. This is the
    // one property whose validity differs from a temperature INTERVAL's: an
    // interval of -20 K is an ordinary cooling, a melting point of -20 K is not a
    // temperature. The type cannot tell them apart (see the header), so the
    // validator is where the distinction is kept.
    if (const std::optional<Temperature> melting = properties.meltingTemperature.value()) {
        if (!isFinite(*melting)) {
            problems.emplace_back("melting temperature is not a finite value");
        } else if (melting->si() <= 0.0) {
            problems.emplace_back(std::format(
                "melting temperature must be above absolute zero, not {}", toString(*melting)));
        }
    }

    if (const std::optional<ElectricalResistivity> resistivity =
            properties.electricalResistivity.value()) {
        if (!isFinite(*resistivity)) {
            problems.emplace_back("electrical resistivity is not a finite value");
        } else if (resistivity->ohmMetres() <= 0.0) {
            problems.emplace_back(std::format("electrical resistivity must be greater than zero, "
                                              "not {}",
                                              toString(*resistivity)));
        }
    }

    checkReferenceTemperature(properties.thermalConductivity, "thermal conductivity", problems);
    checkReferenceTemperature(properties.specificHeatCapacity, "specific heat capacity", problems);
    checkReferenceTemperature(properties.thermalExpansion, "thermal expansion coefficient",
                              problems);
    checkReferenceTemperature(properties.meltingTemperature, "melting temperature", problems);
    checkReferenceTemperature(properties.electricalResistivity, "electrical resistivity", problems);

    if (problems.empty()) {
        return {};
    }
    std::string joined;
    for (const std::string& problem : problems) {
        if (!joined.empty()) {
            joined += "; ";
        }
        joined += problem;
    }
    return makeError(ErrorCode::InvalidArgument,
                     std::format("the thermal properties are not valid: {}", joined));
}

bool hasSteadyConductionProperties(const ThermalProperties& properties) {
    const std::optional<ThermalConductivity> k = properties.thermalConductivity.value();
    return k && isFinite(*k) && k->si() > 0.0;
}

bool hasSpecificHeatCapacity(const ThermalProperties& properties) {
    const std::optional<SpecificHeatCapacity> cp = properties.specificHeatCapacity.value();
    return cp && isFinite(*cp) && cp->si() > 0.0;
}

bool hasThermalExpansion(const ThermalProperties& properties) {
    const std::optional<ThermalExpansionCoefficient> alpha = properties.thermalExpansion.value();
    return alpha && isFinite(*alpha);
}

} // namespace bettercad::materials
