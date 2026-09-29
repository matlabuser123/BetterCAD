#include "MaterialVocabulary.hpp"

#include <bettercad/core/units/Units.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <system_error>
#include <utility>

namespace bettercad::cli {

namespace {

using materials::ConsumerKind;
using materials::IssueKind;
using materials::MechanicalPropertyKind;
using materials::ThermalPropertyKind;

// The codes. In ENUMERATION order, so that what a report prints in is the
// model's order and not this table's; spelled as MaterialJson.cpp spells them,
// so that a file and a command line never disagree about what to call one
// number. The two entries MaterialJson.cpp does not have are the derived pair,
// which it must not have (ADR-027).
constexpr std::array<std::pair<MechanicalPropertyKind, std::string_view>, 11> kMechanical{{
    {MechanicalPropertyKind::Density, "density"},
    {MechanicalPropertyKind::YoungsModulus, "youngs_modulus"},
    {MechanicalPropertyKind::PoissonRatio, "poisson_ratio"},
    {MechanicalPropertyKind::ShearModulus, "shear_modulus"},
    {MechanicalPropertyKind::BulkModulus, "bulk_modulus"},
    {MechanicalPropertyKind::YieldStrength, "yield_strength"},
    {MechanicalPropertyKind::UltimateTensileStrength, "ultimate_tensile_strength"},
    {MechanicalPropertyKind::UltimateCompressiveStrength, "ultimate_compressive_strength"},
    {MechanicalPropertyKind::ShearStrength, "shear_strength"},
    {MechanicalPropertyKind::Elongation, "elongation"},
    {MechanicalPropertyKind::Hardness, "hardness"},
}};

constexpr std::array<std::pair<ThermalPropertyKind, std::string_view>, 5> kThermal{{
    {ThermalPropertyKind::ThermalConductivity, "thermal_conductivity"},
    {ThermalPropertyKind::SpecificHeatCapacity, "specific_heat_capacity"},
    {ThermalPropertyKind::ThermalExpansion, "thermal_expansion"},
    {ThermalPropertyKind::MeltingTemperature, "melting_temperature"},
    {ThermalPropertyKind::ElectricalResistivity, "electrical_resistivity"},
}};

constexpr std::array<std::pair<ConsumerKind, std::string_view>, 7> kConsumers{{
    {ConsumerKind::MassProperties, "mass_properties"},
    {ConsumerKind::FeaLinearStatic, "fea_linear_static"},
    {ConsumerKind::FeaLinearStaticWithGravity, "fea_linear_static_with_gravity"},
    {ConsumerKind::FeaYieldStrength, "fea_yield_strength"},
    {ConsumerKind::ThermalSteady, "thermal_steady"},
    {ConsumerKind::ThermalTransient, "thermal_transient"},
    {ConsumerKind::ThermoMechanical, "thermo_mechanical"},
}};

constexpr std::array<std::pair<IssueKind, std::string_view>, 4> kIssues{{
    {IssueKind::MissingProperty, "missing_property"},
    {IssueKind::InconsistentValues, "inconsistent_values"},
    {IssueKind::OrphanProvenance, "orphan_provenance"},
    {IssueKind::ProvenanceIncomplete, "provenance_incomplete"},
}};

/// Spelled as MaterialJson.cpp spells them, for the same reason the property
/// codes are: a hardness written on a command line and one written in a file
/// name the same scale.
constexpr std::array<std::pair<materials::HardnessScale, std::string_view>, 4> kHardnessScales{{
    {materials::HardnessScale::Brinell, "HBW"},
    {materials::HardnessScale::Vickers, "HV"},
    {materials::HardnessScale::RockwellB, "HRB"},
    {materials::HardnessScale::RockwellC, "HRC"},
}};

template <typename Enum, std::size_t N>
std::string_view codeOf(const std::array<std::pair<Enum, std::string_view>, N>& table, Enum value) noexcept {
    const auto found = std::ranges::find(table, value, &std::pair<Enum, std::string_view>::first);
    return found == table.end() ? std::string_view{"?"} : found->second;
}

/// The shortest text that reads back as the same double, in any locale.
std::string number(double value) { return std::format("{}", value); }

/// "<value> <unit>", in the unit that property is always printed in.
template <Dimension D>
std::string inUnit(Quantity<D> value, const Unit<D>& unit) {
    return std::format("{} {}", number(value.in(unit)), unit.symbol);
}

/// "UNKNOWN", the value, or the value marked derived.
///
/// The ONE place a property becomes text, so no report anywhere can print a 0
/// for something nobody measured.
template <typename Property, typename Render>
std::string render(const Property& property, Render&& text) {
    if (property.isUnknown()) {
        return "UNKNOWN";
    }
    std::string rendered = text(*property.value());
    if (property.isDerived()) {
        rendered += " (derived)";
    }
    if (const auto at = property.referenceTemperature()) {
        rendered += std::format(" at {}", inUnit(*at, units::K));
    }
    return rendered;
}

/// The number at the front of @p text, and whatever followed it.
struct NumberAndRest {
    double value = 0.0;
    std::string_view rest;
};

Result<NumberAndRest> splitNumber(std::string_view text) {
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    double value = 0.0;
    const auto [stop, code] = std::from_chars(begin, end, value);
    if (code != std::errc{} || !std::isfinite(value)) {
        return makeError(ErrorCode::InvalidArgument, std::format("'{}' is not a number", text));
    }
    std::string_view rest{stop, static_cast<std::size_t>(end - stop)};
    while (!rest.empty() && rest.front() == ' ') {
        rest.remove_prefix(1);
    }
    return NumberAndRest{value, rest};
}

/// A bare number, with NO unit symbol accepted.
///
/// NOT parseSiValue() against some dimensionless unit: the only dimensionless
/// entries in the catalog are angles, so `0.3 rad` would be taken for a Poisson
/// ratio and `0.3 deg` would silently become 0.0052. A dimensionless property
/// has no unit, and anything after the number is a mistake worth naming.
Result<double> parsePlainNumber(std::string_view text) {
    auto split = splitNumber(text);
    if (!split) {
        return std::unexpected(split.error());
    }
    if (!split->rest.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is a plain number and takes no unit, but '{}' follows it", text,
                                     split->rest));
    }
    return split->value;
}

/// "60HRC", "60 HRC". The scale is part of the value, never a label beside it,
/// so a bare number is refused rather than defaulted onto a scale.
Result<materials::Hardness> parseHardness(std::string_view text) {
    auto split = splitNumber(text);
    if (!split) {
        return std::unexpected(split.error());
    }
    if (split->rest.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' has no hardness scale; 60 HRC and 60 HRB are different hardnesses, so "
                                     "write one of HBW, HV, HRB, HRC",
                                     text));
    }
    const auto found = std::ranges::find(kHardnessScales, split->rest,
                                         &std::pair<materials::HardnessScale, std::string_view>::second);
    if (found == kHardnessScales.end()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is not a hardness scale; expected HBW, HV, HRB or HRC", split->rest));
    }
    return materials::Hardness::of(split->value, found->first);
}

Error unknownProperty(std::string_view code) {
    std::string known;
    for (const auto& entry : kMechanical) {
        known += known.empty() ? "" : ", ";
        known += entry.second;
    }
    for (const auto& entry : kThermal) {
        known += ", ";
        known += entry.second;
    }
    return Error{ErrorCode::InvalidArgument,
                 std::format("'{}' is not a material property; expected one of {}", code, known)};
}

/// A derived property refused BY NAME, so that the message says what to do
/// instead of pretending the code does not exist.
Error derivedProperty(std::string_view code) {
    return Error{ErrorCode::FailedPrecondition,
                 std::format("the {} is derived from the Young's modulus and the Poisson ratio (ADR-027) and is "
                             "never stored; set youngs_modulus and poisson_ratio instead",
                             code)};
}

} // namespace

std::string_view propertyCode(MechanicalPropertyKind kind) noexcept { return codeOf(kMechanical, kind); }
std::string_view propertyCode(ThermalPropertyKind kind) noexcept { return codeOf(kThermal, kind); }

std::string_view propertyCode(const PropertyKind& kind) noexcept {
    if (const auto* mechanical = std::get_if<MechanicalPropertyKind>(&kind)) {
        return propertyCode(*mechanical);
    }
    return propertyCode(std::get<ThermalPropertyKind>(kind));
}

std::vector<PropertyKind> allPropertyKinds() {
    std::vector<PropertyKind> kinds;
    kinds.reserve(kMechanical.size() + kThermal.size());
    for (const auto& entry : kMechanical) {
        kinds.emplace_back(entry.first);
    }
    for (const auto& entry : kThermal) {
        kinds.emplace_back(entry.first);
    }
    return kinds;
}

Result<PropertyKind> parsePropertyCode(std::string_view code) {
    const auto mechanical =
        std::ranges::find(kMechanical, code, &std::pair<MechanicalPropertyKind, std::string_view>::second);
    if (mechanical != kMechanical.end()) {
        return PropertyKind{mechanical->first};
    }
    const auto thermal = std::ranges::find(kThermal, code, &std::pair<ThermalPropertyKind, std::string_view>::second);
    if (thermal != kThermal.end()) {
        return PropertyKind{thermal->first};
    }
    return std::unexpected(unknownProperty(code));
}

bool isDerivedProperty(const PropertyKind& kind) noexcept {
    const auto* mechanical = std::get_if<MechanicalPropertyKind>(&kind);
    return mechanical != nullptr && (*mechanical == MechanicalPropertyKind::ShearModulus ||
                                     *mechanical == MechanicalPropertyKind::BulkModulus);
}

std::string formatProperty(const materials::MechanicalProperties& properties, MechanicalPropertyKind kind) {
    const auto pressure = [](Stress value) { return inUnit(value, units::MPa); };
    switch (kind) {
    case MechanicalPropertyKind::Density:
        return render(properties.density, [](Density value) { return inUnit(value, units::kg_per_m3); });
    case MechanicalPropertyKind::YoungsModulus:
        return render(properties.youngsModulus, pressure);
    case MechanicalPropertyKind::PoissonRatio:
        return render(properties.poissonRatio, [](PoissonRatio value) { return number(value.value()); });
    case MechanicalPropertyKind::ShearModulus:
        return render(materials::derivedShearModulus(properties), pressure);
    case MechanicalPropertyKind::BulkModulus:
        return render(materials::derivedBulkModulus(properties), pressure);
    case MechanicalPropertyKind::YieldStrength:
        return render(properties.yieldStrength, pressure);
    case MechanicalPropertyKind::UltimateTensileStrength:
        return render(properties.ultimateTensileStrength, pressure);
    case MechanicalPropertyKind::UltimateCompressiveStrength:
        return render(properties.ultimateCompressiveStrength, pressure);
    case MechanicalPropertyKind::ShearStrength:
        return render(properties.shearStrength, pressure);
    case MechanicalPropertyKind::Elongation:
        return render(properties.elongation, [](materials::Elongation value) { return number(value.value()); });
    case MechanicalPropertyKind::Hardness:
        return render(properties.hardness, [](materials::Hardness value) { return materials::toString(value); });
    }
    return "UNKNOWN";
}

std::string formatProperty(const materials::ThermalProperties& properties, ThermalPropertyKind kind) {
    switch (kind) {
    case ThermalPropertyKind::ThermalConductivity:
        return render(properties.thermalConductivity,
                      [](ThermalConductivity value) { return inUnit(value, units::W_per_m_K); });
    case ThermalPropertyKind::SpecificHeatCapacity:
        return render(properties.specificHeatCapacity,
                      [](SpecificHeatCapacity value) { return inUnit(value, units::J_per_kg_K); });
    case ThermalPropertyKind::ThermalExpansion:
        return render(properties.thermalExpansion,
                      [](ThermalExpansionCoefficient value) { return inUnit(value, units::per_K); });
    case ThermalPropertyKind::MeltingTemperature:
        return render(properties.meltingTemperature, [](Temperature value) { return inUnit(value, units::K); });
    case ThermalPropertyKind::ElectricalResistivity:
        return render(properties.electricalResistivity,
                      [](materials::ElectricalResistivity value) { return materials::toString(value); });
    }
    return "UNKNOWN";
}

std::string_view displayUnit(const PropertyKind& kind) noexcept {
    if (const auto* mechanical = std::get_if<MechanicalPropertyKind>(&kind)) {
        switch (*mechanical) {
        case MechanicalPropertyKind::Density:
            return units::kg_per_m3.symbol;
        case MechanicalPropertyKind::YoungsModulus:
        case MechanicalPropertyKind::ShearModulus:
        case MechanicalPropertyKind::BulkModulus:
        case MechanicalPropertyKind::YieldStrength:
        case MechanicalPropertyKind::UltimateTensileStrength:
        case MechanicalPropertyKind::UltimateCompressiveStrength:
        case MechanicalPropertyKind::ShearStrength:
            return units::MPa.symbol;
        case MechanicalPropertyKind::PoissonRatio:
        case MechanicalPropertyKind::Elongation:
        case MechanicalPropertyKind::Hardness:
            break;
        }
        return {};
    }
    switch (std::get<ThermalPropertyKind>(kind)) {
    case ThermalPropertyKind::ThermalConductivity:
        return units::W_per_m_K.symbol;
    case ThermalPropertyKind::SpecificHeatCapacity:
        return units::J_per_kg_K.symbol;
    case ThermalPropertyKind::ThermalExpansion:
        return units::per_K.symbol;
    case ThermalPropertyKind::MeltingTemperature:
        return units::K.symbol;
    case ThermalPropertyKind::ElectricalResistivity:
        break;
    }
    return {};
}

Result<void> setProperty(materials::MechanicalProperties& properties, MechanicalPropertyKind kind,
                         std::string_view text) {
    switch (kind) {
    case MechanicalPropertyKind::Density: {
        auto value = parseQuantity(text, units::kg_per_m3);
        if (!value) {
            return std::unexpected(value.error());
        }
        properties.density = materials::MaterialProperty<Density>::known(*value);
        return {};
    }
    case MechanicalPropertyKind::YoungsModulus:
    case MechanicalPropertyKind::YieldStrength:
    case MechanicalPropertyKind::UltimateTensileStrength:
    case MechanicalPropertyKind::UltimateCompressiveStrength:
    case MechanicalPropertyKind::ShearStrength: {
        // A bare number is Pa, not MPa and not GPa. One rule for every
        // property: `210` cannot mean 210 GPa here while `235` means 235 MPa
        // there.
        auto value = parseQuantity(text, units::Pa);
        if (!value) {
            return std::unexpected(value.error());
        }
        const auto stored = materials::MaterialProperty<Stress>::known(*value);
        if (kind == MechanicalPropertyKind::YoungsModulus) {
            properties.youngsModulus = stored;
        } else if (kind == MechanicalPropertyKind::YieldStrength) {
            properties.yieldStrength = stored;
        } else if (kind == MechanicalPropertyKind::UltimateTensileStrength) {
            properties.ultimateTensileStrength = stored;
        } else if (kind == MechanicalPropertyKind::UltimateCompressiveStrength) {
            properties.ultimateCompressiveStrength = stored;
        } else {
            properties.shearStrength = stored;
        }
        return {};
    }
    case MechanicalPropertyKind::PoissonRatio: {
        auto value = parsePlainNumber(text);
        if (!value) {
            return std::unexpected(value.error());
        }
        properties.poissonRatio = materials::MaterialProperty<PoissonRatio>::known(PoissonRatio::of(*value));
        return {};
    }
    case MechanicalPropertyKind::Elongation: {
        // A FRACTION, the way Elongation stores it, so that what is printed
        // reads back unchanged. 0.12 is twelve percent.
        auto value = parsePlainNumber(text);
        if (!value) {
            return std::unexpected(value.error());
        }
        properties.elongation =
            materials::MaterialProperty<materials::Elongation>::known(materials::Elongation::of(*value));
        return {};
    }
    case MechanicalPropertyKind::Hardness: {
        auto value = parseHardness(text);
        if (!value) {
            return std::unexpected(value.error());
        }
        properties.hardness = materials::MaterialProperty<materials::Hardness>::known(*value);
        return {};
    }
    case MechanicalPropertyKind::ShearModulus:
    case MechanicalPropertyKind::BulkModulus:
        return std::unexpected(derivedProperty(propertyCode(kind)));
    }
    return std::unexpected(unknownProperty(propertyCode(kind)));
}

Result<void> setProperty(materials::ThermalProperties& properties, ThermalPropertyKind kind, std::string_view text) {
    switch (kind) {
    case ThermalPropertyKind::ThermalConductivity: {
        auto value = parseQuantity(text, units::W_per_m_K);
        if (!value) {
            return std::unexpected(value.error());
        }
        properties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(*value);
        return {};
    }
    case ThermalPropertyKind::SpecificHeatCapacity: {
        auto value = parseQuantity(text, units::J_per_kg_K);
        if (!value) {
            return std::unexpected(value.error());
        }
        properties.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(*value);
        return {};
    }
    case ThermalPropertyKind::ThermalExpansion: {
        auto value = parseQuantity(text, units::per_K);
        if (!value) {
            return std::unexpected(value.error());
        }
        properties.thermalExpansion = materials::MaterialProperty<ThermalExpansionCoefficient>::known(*value);
        return {};
    }
    case ThermalPropertyKind::MeltingTemperature: {
        // ABSOLUTE, in kelvin. There is no degC in the catalog and the CLI does
        // not invent one: a UnitScale is a ratio, with no offset to carry it,
        // so an affine conversion here would exist only in the CLI.
        auto value = parseQuantity(text, units::K);
        if (!value) {
            return std::unexpected(value.error());
        }
        properties.meltingTemperature = materials::MaterialProperty<Temperature>::known(*value);
        return {};
    }
    case ThermalPropertyKind::ElectricalResistivity: {
        // A scoped strong type rather than a Quantity (ADR-029): the dimension
        // system has no electric-current exponent, so the catalog holds no
        // symbol for it and a plain number of ohm metres is the only spelling
        // there can be.
        auto value = parsePlainNumber(text);
        if (!value) {
            return std::unexpected(value.error());
        }
        properties.electricalResistivity = materials::MaterialProperty<materials::ElectricalResistivity>::known(
            materials::ElectricalResistivity::ofOhmMetres(*value));
        return {};
    }
    }
    return std::unexpected(unknownProperty(propertyCode(kind)));
}

std::string_view consumerCode(ConsumerKind consumer) noexcept { return codeOf(kConsumers, consumer); }

Result<ConsumerKind> parseConsumer(std::string_view code) {
    const auto found = std::ranges::find(kConsumers, code, &std::pair<ConsumerKind, std::string_view>::second);
    if (found == kConsumers.end()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is not a consumer; expected one of {}", code, consumerCodes()));
    }
    return found->first;
}

std::string consumerCodes() {
    std::string all;
    for (const auto& entry : kConsumers) {
        all += all.empty() ? "" : ", ";
        all += entry.second;
    }
    return all;
}

std::string_view issueCode(IssueKind kind) noexcept { return codeOf(kIssues, kind); }

std::string issueCode(const materials::MaterialIssue& issue) {
    if (issue.mechanical) {
        return std::format("{}:{}", issueCode(issue.kind), propertyCode(*issue.mechanical));
    }
    if (issue.thermal) {
        return std::format("{}:{}", issueCode(issue.kind), propertyCode(*issue.thermal));
    }
    return std::string{issueCode(issue.kind)};
}

std::string_view materialSelectorCode(const Error& error) noexcept {
    switch (error.code) {
    case ErrorCode::NotFound:
        return "material_not_found";
    case ErrorCode::AlreadyExists:
        return "material_ambiguous";
    case ErrorCode::FailedPrecondition:
        return "not_a_material";
    default:
        return "bad_material_selector";
    }
}

std::string_view stateCode(materials::CompletenessState state) noexcept {
    switch (state) {
    case materials::CompletenessState::Ready:
        return "ready";
    case materials::CompletenessState::Incomplete:
        return "incomplete";
    case materials::CompletenessState::Invalid:
        break;
    }
    return "invalid";
}

} // namespace bettercad::cli
