#include <bettercad/io/ParameterJson.hpp>
#include <bettercad/core/units/Format.hpp>

#include "io/json/ParameterJsonDetail.hpp"

#include <array>
#include <format>
#include <string>

namespace bettercad::io {

namespace detail {

namespace {

struct ExponentField {
    std::string_view key;
    int Dimension::* exponent;
};

constexpr std::array kExponentFields{
    ExponentField{"length", &Dimension::length},
    ExponentField{"mass", &Dimension::mass},
    ExponentField{"time", &Dimension::time},
    ExponentField{"temperature", &Dimension::temperature},
    ExponentField{"angle", &Dimension::angle},
};

} // namespace

Json dimensionToJson(const Dimension& dimension) {
    Json json = Json::object();
    for (const ExponentField& field : kExponentFields) {
        if (const int exponent = dimension.*field.exponent; exponent != 0) {
            json[std::string{field.key}] = exponent;
        }
    }
    return json;
}

Result<Dimension> dimensionFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"length", "mass", "time", "temperature", "angle"});
        !object) {
        return std::unexpected(object.error());
    }
    Dimension dimension;
    for (const ExponentField& field : kExponentFields) {
        if (!value.contains(std::string{field.key})) {
            continue;
        }
        auto exponent = readInt(value, field.key, path);
        if (!exponent) {
            return std::unexpected(exponent.error());
        }
        dimension.*field.exponent = *exponent;
    }
    return dimension;
}

Json parameterToJson(const Parameter& parameter) {
    Json json = Json::object();
    json["id"] = parameter.id().value();
    json["name"] = parameter.name();
    json["si_value"] = parameter.siValue();
    json["unit"] = std::string{parameter.displayUnit().symbol};
    json["dimension"] = dimensionToJson(parameter.dimension());
    if (parameter.expression()) {
        json["expression"] = *parameter.expression();
    }
    return json;
}

Result<Parameter> parameterFromJson(const Json& value, std::string_view path) {
    if (auto object =
            requireObject(value, path, {"id", "name", "si_value", "unit", "dimension", "expression"});
        !object) {
        return std::unexpected(object.error());
    }

    auto id = readId(value, "id", path);
    if (!id) {
        return std::unexpected(id.error());
    }
    auto name = readString(value, "name", path);
    if (!name) {
        return std::unexpected(name.error());
    }
    auto siValue = readNumber(value, "si_value", path);
    if (!siValue) {
        return std::unexpected(siValue.error());
    }
    auto unitSymbol = readString(value, "unit", path);
    if (!unitSymbol) {
        return std::unexpected(unitSymbol.error());
    }
    auto dimensionField = requireField(value, "dimension", path);
    if (!dimensionField) {
        return std::unexpected(dimensionField.error());
    }
    auto dimension = dimensionFromJson(**dimensionField, childPath(path, "dimension"));
    if (!dimension) {
        return std::unexpected(dimension.error());
    }
    auto expression = readOptionalString(value, "expression", path);
    if (!expression) {
        return std::unexpected(expression.error());
    }

    auto unit = resolveUnit(*unitSymbol);
    if (!unit) {
        return atPath(childPath(path, "unit"), unit.error());
    }
    // The dimension is redundant with the unit; a disagreement means the file
    // is corrupt or was written with a different unit definition.
    if (unit->dimension != *dimension) {
        return makeError(ErrorCode::DimensionMismatch,
                         std::format("{}: unit '{}' has dimension {}, but the file declares {}",
                                     path, unit->symbol, describeDimension(unit->dimension),
                                     describeDimension(*dimension)));
    }

    auto parameter = Parameter::create(ParameterId::fromValue(*id), std::move(*name), *siValue, *unit);
    if (!parameter) {
        return atPath(path, parameter.error());
    }
    if (*expression) {
        if (auto set = parameter->setExpression(std::move(*expression)); !set) {
            return atPath(childPath(path, "expression"), set.error());
        }
    }
    return parameter;
}

Json parameterTableToJson(const ParameterTable& table) {
    Json array = Json::array();
    for (const Parameter& parameter : table.all()) {
        array.push_back(parameterToJson(parameter));
    }
    return array;
}

Result<ParameterTable> parameterTableFromJson(const Json& value, std::string_view path) {
    if (!value.is_array()) {
        return parseError(path, "expected an array");
    }
    ParameterTable table;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const std::string elementPath = indexPath(path, i);
        auto parameter = parameterFromJson(value[i], elementPath);
        if (!parameter) {
            return std::unexpected(parameter.error());
        }
        if (auto added = table.add(std::move(*parameter)); !added) {
            return atPath(elementPath, added.error());
        }
    }
    return table;
}

} // namespace detail

Result<std::string> parametersToJson(const ParameterTable& table) {
    detail::Json document = detail::Json::object();
    document["format"] = std::string{kParameterFormat};
    document["version"] = kParameterFormatVersion;
    document["parameters"] = detail::parameterTableToJson(table);
    return detail::dumpJson(document);
}

Result<ParameterTable> parametersFromJson(std::string_view json) {
    auto document = detail::parseJson(json);
    if (!document) {
        return std::unexpected(document.error());
    }
    if (auto object = detail::requireObject(*document, "", {"format", "version", "parameters"});
        !object) {
        return std::unexpected(object.error());
    }
    auto format = detail::readString(*document, "format", "");
    if (!format) {
        return std::unexpected(format.error());
    }
    if (*format != kParameterFormat) {
        return detail::parseError("format", std::format("expected '{}', got '{}'",
                                                        kParameterFormat, *format));
    }
    auto version = detail::readUnsigned(*document, "version", "");
    if (!version) {
        return std::unexpected(version.error());
    }
    if (*version != static_cast<std::uint64_t>(kParameterFormatVersion)) {
        return detail::parseError("version", std::format("unsupported version {} (supported: {})",
                                                         *version, kParameterFormatVersion));
    }
    auto parameters = detail::requireField(*document, "parameters", "");
    if (!parameters) {
        return std::unexpected(parameters.error());
    }
    return detail::parameterTableFromJson(**parameters, "parameters");
}

} // namespace bettercad::io
