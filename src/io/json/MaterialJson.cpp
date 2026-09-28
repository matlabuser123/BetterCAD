#include "io/json/ObjectJson.hpp"

#include <bettercad/core/materials/MaterialProperty.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/Provenance.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/features/Material.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// The persisted form of a material's canonical engineering intent
// (P15-PERSIST-001).
//
// WHAT IS PERSISTED: identity, metadata, mechanical properties, thermal and
// electrical properties, provenance, and the library origin key. WHAT IS NOT:
// anything derived. No mass, no volume, no centroid, no inertia, no shear or bulk
// modulus, no completeness report, no effective material. Those recompute from what
// is here (ADR-026, ADR-027, ADR-028), and several of them could not be written even
// deliberately -- MechanicalProperties has no slot for a shear modulus at all.
//
// ABSENT MEANS UNKNOWN. A property that nobody has measured is omitted rather than
// written as null or zero, which is the one representation that cannot be misread:
// there is no number in the file to be mistaken for a measurement. So a material
// with one known property has one property in its file.
//
// THE FILE'S KEYS ARE NOT THE DIAGNOSTIC STRINGS. `toString(MechanicalPropertyKind)`
// returns "Young's modulus" and "Poisson's ratio" -- display text, with an
// apostrophe, that a later milestone may reword or translate. A file key must
// outlive that, so the mapping below is explicit and separate, and rewording a
// diagnostic cannot break a saved document. The same for every enumeration here.
//
// ENUMERATIONS ARE STRINGS, NOT INTEGERS. A string says what it means in the file
// and an unrecognised one is rejected by name; an integer would silently become a
// different case the day someone inserts an enumerator in the middle.
//
// PROVENANCE IS KEYED BY PROPERTY NAME, NOT BY POSITION. Each record carries the
// property it describes, so no reordering of the file or of the enumeration can
// reattach a citation to the wrong value.
namespace bettercad::io::detail {

namespace {

using materials::HardnessScale;
using materials::MechanicalPropertyKind;
using materials::SourceKind;
using materials::ThermalPropertyKind;

/// The persisted key for each mechanical property that can be stored.
///
/// The derived kinds -- shear and bulk modulus -- are deliberately absent: they are
/// never stored (ADR-027), so there is no key for them and a file cannot claim one.
constexpr std::array<std::pair<MechanicalPropertyKind, std::string_view>, 9> kMechanicalKeys{{
    {MechanicalPropertyKind::Density, "density"},
    {MechanicalPropertyKind::YoungsModulus, "youngs_modulus"},
    {MechanicalPropertyKind::PoissonRatio, "poisson_ratio"},
    {MechanicalPropertyKind::YieldStrength, "yield_strength"},
    {MechanicalPropertyKind::UltimateTensileStrength, "ultimate_tensile_strength"},
    {MechanicalPropertyKind::UltimateCompressiveStrength, "ultimate_compressive_strength"},
    {MechanicalPropertyKind::ShearStrength, "shear_strength"},
    {MechanicalPropertyKind::Elongation, "elongation"},
    {MechanicalPropertyKind::Hardness, "hardness"},
}};

constexpr std::array<std::pair<ThermalPropertyKind, std::string_view>, 5> kThermalKeys{{
    {ThermalPropertyKind::ThermalConductivity, "thermal_conductivity"},
    {ThermalPropertyKind::SpecificHeatCapacity, "specific_heat_capacity"},
    {ThermalPropertyKind::ThermalExpansion, "thermal_expansion"},
    {ThermalPropertyKind::MeltingTemperature, "melting_temperature"},
    {ThermalPropertyKind::ElectricalResistivity, "electrical_resistivity"},
}};

constexpr std::array<std::pair<SourceKind, std::string_view>, 8> kSourceKinds{{
    {SourceKind::Unspecified, "unspecified"},
    {SourceKind::LibraryReference, "library_reference"},
    {SourceKind::ManufacturerData, "manufacturer_data"},
    {SourceKind::Standard, "standard"},
    {SourceKind::Handbook, "handbook"},
    {SourceKind::Measured, "measured"},
    {SourceKind::Calculated, "calculated"},
    {SourceKind::UserEntered, "user_entered"},
}};

/// Hardness scales, written as the designation an engineer would recognise. These
/// happen to match toString() today, and are listed separately anyway so that a
/// change to the diagnostic cannot change the file.
constexpr std::array<std::pair<HardnessScale, std::string_view>, 4> kHardnessScales{{
    {HardnessScale::Brinell, "HBW"},
    {HardnessScale::Vickers, "HV"},
    {HardnessScale::RockwellB, "HRB"},
    {HardnessScale::RockwellC, "HRC"},
}};

template <typename Enum, std::size_t N>
[[nodiscard]] std::string_view keyOf(const std::array<std::pair<Enum, std::string_view>, N>& table,
                                    Enum value) {
    for (const auto& [kind, key] : table) {
        if (kind == value) {
            return key;
        }
    }
    return {};
}

template <typename Enum, std::size_t N>
[[nodiscard]] std::optional<Enum> valueOf(
    const std::array<std::pair<Enum, std::string_view>, N>& table, std::string_view key) {
    for (const auto& [kind, name] : table) {
        if (name == key) {
            return kind;
        }
    }
    return std::nullopt;
}

/// Every persisted property key, for the allowed-key check.
template <std::size_t N, typename Enum>
[[nodiscard]] bool isKnownKey(const std::array<std::pair<Enum, std::string_view>, N>& table,
                              const std::string& key) {
    return valueOf(table, key).has_value();
}

/// A finite number, rejected otherwise.
///
/// JSON has no NaN or infinity, so a conforming file cannot contain one -- but a
/// number can still be large enough to become infinite as a double, and a
/// non-conforming producer can write anything. Checked here rather than trusted,
/// because an infinite density would otherwise reach a validator expecting a real
/// one.
[[nodiscard]] Result<double> readFiniteNumber(const Json& object, std::string_view key,
                                              std::string_view path) {
    auto value = readNumber(object, key, path);
    if (!value) {
        return value;
    }
    if (!(*value == *value) || *value == std::numeric_limits<double>::infinity() ||
        *value == -std::numeric_limits<double>::infinity()) {
        return parseError(childPath(path, key), "expected a finite number");
    }
    return *value;
}

/// {"value": <SI>} plus "at" when a reference temperature was recorded.
///
/// SI, always, so a display-unit preference can never become engineering authority
/// (P15-UNITS-001: a Quantity stores SI and converts only at a boundary). A file
/// written while the UI showed GPa and one written while it showed MPa are the same
/// bytes.
template <typename Value>
[[nodiscard]] Json propertyToJson(const materials::MaterialProperty<Value>& property,
                                  double siValue) {
    Json json = Json::object();
    json["value"] = siValue;
    if (const std::optional<Temperature> at = property.referenceTemperature()) {
        // An ABSOLUTE temperature in kelvin, not an interval. The two are different
        // quantities (P15-THERM-001) and only one of them is meaningful here.
        json["at"] = at->si();
    }
    return json;
}

struct ReadProperty {
    double value = 0.0;
    std::optional<Temperature> at;
};

[[nodiscard]] Result<ReadProperty> propertyFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"value", "at"}); !object) {
        return std::unexpected(object.error());
    }
    auto number = readFiniteNumber(value, "value", path);
    if (!number) {
        return std::unexpected(number.error());
    }
    ReadProperty result;
    result.value = *number;
    if (value.contains("at")) {
        auto at = readFiniteNumber(value, "at", path);
        if (!at) {
            return std::unexpected(at.error());
        }
        result.at = Temperature::fromSi(*at);
    }
    return result;
}

/// Builds a property from a read value, carrying the reference temperature when one
/// was recorded. Always `known()` -- never `derived()`, because a stored property in
/// the Derived state is refused by validate() and a file must not be able to
/// introduce one.
template <typename Value>
[[nodiscard]] materials::MaterialProperty<Value> knownFrom(const ReadProperty& read, Value value) {
    if (read.at) {
        return materials::MaterialProperty<Value>::known(value, *read.at);
    }
    return materials::MaterialProperty<Value>::known(value);
}

// --- mechanical --------------------------------------------------------------

[[nodiscard]] Json mechanicalToJson(const materials::MechanicalProperties& properties) {
    Json json = Json::object();
    const auto put = [&](MechanicalPropertyKind kind, Json value) {
        json[std::string{keyOf(kMechanicalKeys, kind)}] = std::move(value);
    };
    if (const auto value = properties.density.value()) {
        put(MechanicalPropertyKind::Density, propertyToJson(properties.density, value->si()));
    }
    if (const auto value = properties.youngsModulus.value()) {
        put(MechanicalPropertyKind::YoungsModulus,
            propertyToJson(properties.youngsModulus, value->si()));
    }
    if (const auto value = properties.poissonRatio.value()) {
        // A pure number, so there is no SI conversion to make -- value() IS the
        // ratio.
        put(MechanicalPropertyKind::PoissonRatio,
            propertyToJson(properties.poissonRatio, value->value()));
    }
    if (const auto value = properties.yieldStrength.value()) {
        put(MechanicalPropertyKind::YieldStrength,
            propertyToJson(properties.yieldStrength, value->si()));
    }
    if (const auto value = properties.ultimateTensileStrength.value()) {
        put(MechanicalPropertyKind::UltimateTensileStrength,
            propertyToJson(properties.ultimateTensileStrength, value->si()));
    }
    if (const auto value = properties.ultimateCompressiveStrength.value()) {
        put(MechanicalPropertyKind::UltimateCompressiveStrength,
            propertyToJson(properties.ultimateCompressiveStrength, value->si()));
    }
    if (const auto value = properties.shearStrength.value()) {
        put(MechanicalPropertyKind::ShearStrength,
            propertyToJson(properties.shearStrength, value->si()));
    }
    if (const auto value = properties.elongation.value()) {
        put(MechanicalPropertyKind::Elongation,
            propertyToJson(properties.elongation, value->value()));
    }
    if (const auto value = properties.hardness.value()) {
        // The VALUE AND THE SCALE together: 60 HRC is not 60 HBW, and a number
        // without its scale is not a hardness at all (P15-MECH-001).
        Json hardness = propertyToJson(properties.hardness, value->value());
        hardness["scale"] = std::string{keyOf(kHardnessScales, value->scale())};
        put(MechanicalPropertyKind::Hardness, std::move(hardness));
    }
    return json;
}

[[nodiscard]] Result<materials::MechanicalProperties> mechanicalFromJson(const Json& value,
                                                                        std::string_view path) {
    if (!value.is_object()) {
        return parseError(path, "expected an object");
    }
    // Every key must be one this build knows. An unrecognised key is rejected by
    // name rather than ignored, following the document format's own policy -- a
    // reader that skipped what it did not understand would load a material that is
    // not the one the file describes.
    for (const auto& item : value.items()) {
        if (!isKnownKey(kMechanicalKeys, item.key())) {
            return parseError(childPath(path, item.key()), "unknown mechanical property");
        }
    }
    materials::MechanicalProperties properties;
    const auto read = [&](MechanicalPropertyKind kind) -> Result<std::optional<ReadProperty>> {
        const std::string key{keyOf(kMechanicalKeys, kind)};
        if (!value.contains(key)) {
            return std::optional<ReadProperty>{};
        }
        auto property = propertyFromJson(value.at(key), childPath(path, key));
        if (!property) {
            return std::unexpected(property.error());
        }
        return std::optional<ReadProperty>{*property};
    };

    auto density = read(MechanicalPropertyKind::Density);
    if (!density) {
        return std::unexpected(density.error());
    }
    if (*density) {
        properties.density = knownFrom(**density, Density::fromSi((*density)->value));
    }
    auto modulus = read(MechanicalPropertyKind::YoungsModulus);
    if (!modulus) {
        return std::unexpected(modulus.error());
    }
    if (*modulus) {
        properties.youngsModulus =
            knownFrom(**modulus, ElasticModulus::fromSi((*modulus)->value));
    }
    auto ratio = read(MechanicalPropertyKind::PoissonRatio);
    if (!ratio) {
        return std::unexpected(ratio.error());
    }
    if (*ratio) {
        properties.poissonRatio = knownFrom(**ratio, PoissonRatio::of((*ratio)->value));
    }
    auto yield = read(MechanicalPropertyKind::YieldStrength);
    if (!yield) {
        return std::unexpected(yield.error());
    }
    if (*yield) {
        properties.yieldStrength = knownFrom(**yield, Stress::fromSi((*yield)->value));
    }
    auto tensile = read(MechanicalPropertyKind::UltimateTensileStrength);
    if (!tensile) {
        return std::unexpected(tensile.error());
    }
    if (*tensile) {
        properties.ultimateTensileStrength =
            knownFrom(**tensile, Stress::fromSi((*tensile)->value));
    }
    auto compressive = read(MechanicalPropertyKind::UltimateCompressiveStrength);
    if (!compressive) {
        return std::unexpected(compressive.error());
    }
    if (*compressive) {
        properties.ultimateCompressiveStrength =
            knownFrom(**compressive, Stress::fromSi((*compressive)->value));
    }
    auto shear = read(MechanicalPropertyKind::ShearStrength);
    if (!shear) {
        return std::unexpected(shear.error());
    }
    if (*shear) {
        properties.shearStrength = knownFrom(**shear, Stress::fromSi((*shear)->value));
    }
    auto elongation = read(MechanicalPropertyKind::Elongation);
    if (!elongation) {
        return std::unexpected(elongation.error());
    }
    if (*elongation) {
        properties.elongation =
            knownFrom(**elongation, materials::Elongation::of((*elongation)->value));
    }
    // Hardness carries an extra key, so it is read on its own.
    const std::string hardnessKey{keyOf(kMechanicalKeys, MechanicalPropertyKind::Hardness)};
    if (value.contains(hardnessKey)) {
        const std::string hardnessPath = childPath(path, hardnessKey);
        const Json& hardness = value.at(hardnessKey);
        if (auto object = requireObject(hardness, hardnessPath, {"value", "at", "scale"}); !object) {
            return std::unexpected(object.error());
        }
        auto number = readFiniteNumber(hardness, "value", hardnessPath);
        if (!number) {
            return std::unexpected(number.error());
        }
        auto scaleName = readString(hardness, "scale", hardnessPath);
        if (!scaleName) {
            return std::unexpected(scaleName.error());
        }
        const std::optional<HardnessScale> scale = valueOf(kHardnessScales, *scaleName);
        if (!scale) {
            return parseError(childPath(hardnessPath, "scale"),
                              std::format("unknown hardness scale '{}'", *scaleName));
        }
        ReadProperty plain;
        plain.value = *number;
        if (hardness.contains("at")) {
            auto at = readFiniteNumber(hardness, "at", hardnessPath);
            if (!at) {
                return std::unexpected(at.error());
            }
            plain.at = Temperature::fromSi(*at);
        }
        properties.hardness = knownFrom(plain, materials::Hardness::of(*number, *scale));
    }
    return properties;
}

// --- thermal -----------------------------------------------------------------

[[nodiscard]] Json thermalToJson(const materials::ThermalProperties& properties) {
    Json json = Json::object();
    const auto put = [&](ThermalPropertyKind kind, Json value) {
        json[std::string{keyOf(kThermalKeys, kind)}] = std::move(value);
    };
    // DENSITY IS NOT HERE, and its absence is the design. It lives in the
    // mechanical properties, which is its one home, and thermal consumers read that
    // same value (P15-THERM-001). Writing it in both sections would create two
    // persisted authorities that a hand-edited file could make disagree.
    if (const auto value = properties.thermalConductivity.value()) {
        put(ThermalPropertyKind::ThermalConductivity,
            propertyToJson(properties.thermalConductivity, value->si()));
    }
    if (const auto value = properties.specificHeatCapacity.value()) {
        put(ThermalPropertyKind::SpecificHeatCapacity,
            propertyToJson(properties.specificHeatCapacity, value->si()));
    }
    if (const auto value = properties.thermalExpansion.value()) {
        put(ThermalPropertyKind::ThermalExpansion,
            propertyToJson(properties.thermalExpansion, value->si()));
    }
    if (const auto value = properties.meltingTemperature.value()) {
        // An ABSOLUTE temperature in kelvin. Persisting it in kelvin rather than as
        // a difference is what keeps it from being read back as an interval.
        put(ThermalPropertyKind::MeltingTemperature,
            propertyToJson(properties.meltingTemperature, value->si()));
    }
    if (const auto value = properties.electricalResistivity.value()) {
        // Its own type, not a Quantity (ADR-029), so it is written in ohm metres and
        // read back into that same type -- never a bare double.
        put(ThermalPropertyKind::ElectricalResistivity,
            propertyToJson(properties.electricalResistivity, value->ohmMetres()));
    }
    return json;
}

[[nodiscard]] Result<materials::ThermalProperties> thermalFromJson(const Json& value,
                                                                  std::string_view path) {
    if (!value.is_object()) {
        return parseError(path, "expected an object");
    }
    for (const auto& item : value.items()) {
        if (!isKnownKey(kThermalKeys, item.key())) {
            return parseError(childPath(path, item.key()), "unknown thermal property");
        }
    }
    materials::ThermalProperties properties;
    const auto read = [&](ThermalPropertyKind kind) -> Result<std::optional<ReadProperty>> {
        const std::string key{keyOf(kThermalKeys, kind)};
        if (!value.contains(key)) {
            return std::optional<ReadProperty>{};
        }
        auto property = propertyFromJson(value.at(key), childPath(path, key));
        if (!property) {
            return std::unexpected(property.error());
        }
        return std::optional<ReadProperty>{*property};
    };

    auto conductivity = read(ThermalPropertyKind::ThermalConductivity);
    if (!conductivity) {
        return std::unexpected(conductivity.error());
    }
    if (*conductivity) {
        properties.thermalConductivity =
            knownFrom(**conductivity, ThermalConductivity::fromSi((*conductivity)->value));
    }
    auto heat = read(ThermalPropertyKind::SpecificHeatCapacity);
    if (!heat) {
        return std::unexpected(heat.error());
    }
    if (*heat) {
        properties.specificHeatCapacity =
            knownFrom(**heat, SpecificHeatCapacity::fromSi((*heat)->value));
    }
    auto expansion = read(ThermalPropertyKind::ThermalExpansion);
    if (!expansion) {
        return std::unexpected(expansion.error());
    }
    if (*expansion) {
        properties.thermalExpansion =
            knownFrom(**expansion, ThermalExpansionCoefficient::fromSi((*expansion)->value));
    }
    auto melting = read(ThermalPropertyKind::MeltingTemperature);
    if (!melting) {
        return std::unexpected(melting.error());
    }
    if (*melting) {
        properties.meltingTemperature =
            knownFrom(**melting, Temperature::fromSi((*melting)->value));
    }
    auto resistivity = read(ThermalPropertyKind::ElectricalResistivity);
    if (!resistivity) {
        return std::unexpected(resistivity.error());
    }
    if (*resistivity) {
        properties.electricalResistivity = knownFrom(
            **resistivity, materials::ElectricalResistivity::ofOhmMetres((*resistivity)->value));
    }
    return properties;
}

// --- provenance --------------------------------------------------------------

[[nodiscard]] Json provenanceRecordToJson(const materials::PropertyProvenance& provenance) {
    Json json = Json::object();
    // The kind is always written, even when Unspecified, so a reader never has to
    // guess what an absent kind meant.
    json["kind"] = std::string{keyOf(kSourceKinds, provenance.kind)};
    const auto putIfSet = [&](std::string_view key, const std::string& text) {
        if (!text.empty()) {
            json[std::string{key}] = text;
        }
    };
    putIfSet("source", provenance.source);
    putIfSet("standard", provenance.standard);
    putIfSet("reference", provenance.reference);
    putIfSet("revision", provenance.revision);
    putIfSet("condition", provenance.condition);
    putIfSet("notes", provenance.notes);
    if (provenance.date) {
        // ISO 8601, from materials::toString -- no locale, no time zone, no clock.
        json["date"] = materials::toString(*provenance.date);
    }
    return json;
}

[[nodiscard]] Result<materials::PropertyProvenance> provenanceRecordFromJson(
    const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path,
                                    {"kind", "source", "standard", "reference", "revision",
                                     "condition", "notes", "date", "property"});
        !object) {
        return std::unexpected(object.error());
    }
    auto kindName = readString(value, "kind", path);
    if (!kindName) {
        return std::unexpected(kindName.error());
    }
    const std::optional<SourceKind> kind = valueOf(kSourceKinds, *kindName);
    if (!kind) {
        return parseError(childPath(path, "kind"),
                          std::format("unknown source kind '{}'", *kindName));
    }
    materials::PropertyProvenance provenance;
    provenance.kind = *kind;
    const auto readText = [&](std::string_view key, std::string& into) -> Result<void> {
        auto text = readOptionalString(value, key, path);
        if (!text) {
            return std::unexpected(text.error());
        }
        if (*text) {
            into = std::move(**text);
        }
        return {};
    };
    for (const auto& [key, target] :
         {std::pair<std::string_view, std::string*>{"source", &provenance.source},
          {"standard", &provenance.standard},
          {"reference", &provenance.reference},
          {"revision", &provenance.revision},
          {"condition", &provenance.condition},
          {"notes", &provenance.notes}}) {
        if (auto read = readText(key, *target); !read) {
            return std::unexpected(read.error());
        }
    }
    if (value.contains("date")) {
        auto text = readString(value, "date", path);
        if (!text) {
            return std::unexpected(text.error());
        }
        const std::optional<materials::Date> date = materials::parseDate(*text);
        if (!date) {
            return parseError(childPath(path, "date"),
                              std::format("expected an ISO 8601 date, got '{}'", *text));
        }
        provenance.date = date;
    }
    return provenance;
}

[[nodiscard]] Json provenanceToJson(const materials::MaterialProvenance& provenance) {
    Json json = Json::object();
    if (!provenance.material.empty()) {
        json["material"] = provenanceRecordToJson(provenance.material);
    }
    // ARRAYS OF RECORDS THAT NAME THEIR PROPERTY, not a positional list and not an
    // object keyed by a name a reader must trust. The maps are std::map over the
    // property enumerations, so the order is the enumeration's and is the same in
    // every build; the explicit "property" key is what makes a reader independent of
    // it anyway.
    if (!provenance.mechanical.empty()) {
        Json records = Json::array();
        for (const auto& [kind, record] : provenance.mechanical) {
            Json entry = provenanceRecordToJson(record);
            entry["property"] = std::string{keyOf(kMechanicalKeys, kind)};
            records.push_back(std::move(entry));
        }
        json["mechanical"] = std::move(records);
    }
    if (!provenance.thermal.empty()) {
        Json records = Json::array();
        for (const auto& [kind, record] : provenance.thermal) {
            Json entry = provenanceRecordToJson(record);
            entry["property"] = std::string{keyOf(kThermalKeys, kind)};
            records.push_back(std::move(entry));
        }
        json["thermal"] = std::move(records);
    }
    return json;
}

[[nodiscard]] Result<materials::MaterialProvenance> provenanceFromJson(const Json& value,
                                                                      std::string_view path) {
    if (auto object = requireObject(value, path, {"material", "mechanical", "thermal"}); !object) {
        return std::unexpected(object.error());
    }
    materials::MaterialProvenance provenance;
    if (value.contains("material")) {
        auto record = provenanceRecordFromJson(value.at("material"), childPath(path, "material"));
        if (!record) {
            return std::unexpected(record.error());
        }
        if (record->empty()) {
            return parseError(childPath(path, "material"),
                              "a provenance record that states nothing should be omitted");
        }
        provenance.material = std::move(*record);
    }
    const auto readRecords = [&](std::string_view key, auto& table, const auto& keys) -> Result<void> {
        const std::string name{key};
        if (!value.contains(name)) {
            return {};
        }
        auto array = requireArray(value, key, path);
        if (!array) {
            return std::unexpected(array.error());
        }
        for (std::size_t i = 0; i < (*array)->size(); ++i) {
            const Json& entry = (**array)[i];
            const std::string entryPath = indexPath(childPath(path, key), i);
            auto record = provenanceRecordFromJson(entry, entryPath);
            if (!record) {
                return std::unexpected(record.error());
            }
            auto propertyName = readString(entry, "property", entryPath);
            if (!propertyName) {
                return std::unexpected(propertyName.error());
            }
            const auto kind = valueOf(keys, *propertyName);
            if (!kind) {
                return parseError(childPath(entryPath, "property"),
                                  std::format("unknown property '{}'", *propertyName));
            }
            if (record->empty()) {
                return parseError(entryPath,
                                  "a provenance record that states nothing should be omitted");
            }
            // A second record for one property is ambiguous canonical data, so it is
            // refused rather than resolved by a first-wins or last-wins rule.
            if (table.contains(*kind)) {
                return parseError(entryPath,
                                  std::format("provenance for '{}' appears more than once",
                                              *propertyName));
            }
            table.emplace(*kind, std::move(*record));
        }
        return {};
    };
    if (auto read = readRecords("mechanical", provenance.mechanical, kMechanicalKeys); !read) {
        return std::unexpected(read.error());
    }
    if (auto read = readRecords("thermal", provenance.thermal, kThermalKeys); !read) {
        return std::unexpected(read.error());
    }
    return provenance;
}

// --- the library origin key --------------------------------------------------

[[nodiscard]] Json originToJson(const materials::MaterialLibraryKey& key) {
    return Json{{"library", key.library}, {"entry", key.entry}, {"revision", key.revision}};
}

[[nodiscard]] Result<materials::MaterialLibraryKey> originFromJson(const Json& value,
                                                                  std::string_view path) {
    if (auto object = requireObject(value, path, {"library", "entry", "revision"}); !object) {
        return std::unexpected(object.error());
    }
    auto library = readString(value, "library", path);
    auto entry = readString(value, "entry", path);
    auto revision = readInt(value, "revision", path);
    if (!library || !entry || !revision) {
        return std::unexpected(!library ? library.error()
                               : !entry ? entry.error()
                                        : revision.error());
    }
    return materials::MaterialLibraryKey{std::move(*library), std::move(*entry), *revision};
}

} // namespace

// DocumentJson.cpp dispatches on the literal "material"; this is what keeps that
// literal and the type name itself from drifting apart. The literal is necessary
// because binding a reference to a dll-imported constexpr static does not link in a
// shared build -- the same reason ComponentJson.cpp carries the identical pair.
static_assert(features::Material::kTypeName == "material",
              "the material type name and the name the reader dispatches on must agree");

Json materialToJson(const features::Material& material) {
    const features::MaterialDefinition& definition = material.definition();
    Json json = Json::object();
    const auto putIfSet = [&](std::string_view key, const std::string& text) {
        if (!text.empty()) {
            json[std::string{key}] = text;
        }
    };
    putIfSet("designation", definition.designation);
    putIfSet("standard", definition.standard);
    putIfSet("family", definition.family);
    putIfSet("notes", definition.notes);
    if (definition.origin) {
        // PROVENANCE, NOT A LIVE REFERENCE (ADR-025). Nothing consults the library
        // through this at load time or at solve time; the values in this file are the
        // document's own, so a library that has moved on, or is absent entirely,
        // cannot change what this document computes.
        json["origin"] = originToJson(*definition.origin);
    }
    if (Json mechanical = mechanicalToJson(definition.mechanical); !mechanical.empty()) {
        json["mechanical"] = std::move(mechanical);
    }
    if (Json thermal = thermalToJson(definition.thermal); !thermal.empty()) {
        json["thermal"] = std::move(thermal);
    }
    if (Json provenance = provenanceToJson(definition.provenance); !provenance.empty()) {
        json["provenance"] = std::move(provenance);
    }
    return json;
}

Result<std::unique_ptr<features::Material>> materialFromJson(const Json& value, std::string_view name,
                                                            std::string_view path) {
    if (auto object = requireObject(value, path,
                                    {"designation", "standard", "family", "notes", "origin",
                                     "mechanical", "thermal", "provenance"});
        !object) {
        return std::unexpected(object.error());
    }
    features::MaterialDefinition definition;
    const auto readText = [&](std::string_view key, std::string& into) -> Result<void> {
        auto text = readOptionalString(value, key, path);
        if (!text) {
            return std::unexpected(text.error());
        }
        if (*text) {
            into = std::move(**text);
        }
        return {};
    };
    for (const auto& [key, target] :
         {std::pair<std::string_view, std::string*>{"designation", &definition.designation},
          {"standard", &definition.standard},
          {"family", &definition.family},
          {"notes", &definition.notes}}) {
        if (auto read = readText(key, *target); !read) {
            return std::unexpected(read.error());
        }
    }
    if (value.contains("origin")) {
        auto origin = originFromJson(value.at("origin"), childPath(path, "origin"));
        if (!origin) {
            return std::unexpected(origin.error());
        }
        definition.origin = std::move(*origin);
    }
    if (value.contains("mechanical")) {
        auto mechanical = mechanicalFromJson(value.at("mechanical"), childPath(path, "mechanical"));
        if (!mechanical) {
            return std::unexpected(mechanical.error());
        }
        definition.mechanical = std::move(*mechanical);
    }
    if (value.contains("thermal")) {
        auto thermal = thermalFromJson(value.at("thermal"), childPath(path, "thermal"));
        if (!thermal) {
            return std::unexpected(thermal.error());
        }
        definition.thermal = std::move(*thermal);
    }
    if (value.contains("provenance")) {
        auto provenance = provenanceFromJson(value.at("provenance"), childPath(path, "provenance"));
        if (!provenance) {
            return std::unexpected(provenance.error());
        }
        definition.provenance = std::move(*provenance);
    }
    // THROUGH Material::create, so the file goes through exactly the validation a
    // caller does: a zero density, a negative modulus, a Poisson ratio of 0.5 or a
    // half-filled origin key are refused here as they would be refused there. A
    // deserializer with its own weaker checks is how invalid engineering data gets
    // into a model.
    auto material = features::Material::create(std::string{name}, std::move(definition));
    if (!material) {
        return detail::atPath(path, material.error());
    }
    return std::move(*material);
}

} // namespace bettercad::io::detail
