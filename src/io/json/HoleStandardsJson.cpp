#include "io/json/ObjectJson.hpp"

#include <array>
#include <format>
#include <string>
#include <utility>

// The standard parts of holes (P12-HOLE-001): threads, clearance sizes and
// tolerance classes, written by their designations ("M8", "6H", "H7").
namespace bettercad::io::detail {

namespace {

using standards::ClearanceSeries;

constexpr std::array<std::pair<ClearanceSeries, std::string_view>, 3> kSeries{{
    {ClearanceSeries::Fine, "fine"},
    {ClearanceSeries::Medium, "medium"},
    {ClearanceSeries::Coarse, "coarse"},
}};

/// A designation string read with @p parse; its errors keep their code and
/// are prefixed with the field's path.
template <typename Parse>
auto readDesignation(const Json& object, std::string_view key, std::string_view path, Parse parse)
    -> decltype(parse(std::string_view{})) {
    auto text = readString(object, key, path);
    if (!text) {
        return std::unexpected(text.error());
    }
    auto value = parse(*text);
    if (!value) {
        return atPath(childPath(path, key), value.error());
    }
    return value;
}

} // namespace

Json holeThreadToJson(const features::HoleThread& thread) {
    Json json = Json::object();
    json["size"] = thread.size.designation();
    json["class"] = standards::toString(thread.tolerance);
    if (thread.length != Length{} || thread.lengthParameter) {
        json["length"] = thread.length.si();
    }
    if (thread.lengthParameter) {
        json["length_parameter"] = thread.lengthParameter->value();
    }
    return json;
}

Result<features::HoleThread> holeThreadFromJson(const Json& data, std::string_view path) {
    if (auto object = requireObject(data, path, {"size", "class", "length", "length_parameter"}); !object) {
        return std::unexpected(object.error());
    }
    auto size = readDesignation(data, "size", path, standards::parseMetricThread);
    auto tolerance = readDesignation(data, "class", path, standards::parseThreadToleranceClass);
    auto length = data.contains("length") ? readNumber(data, "length", path) : Result<double>{0.0};
    auto parameter = readOptionalId(data, "length_parameter", path);
    if (!size || !tolerance || !length || !parameter) {
        return std::unexpected(!size        ? size.error()
                               : !tolerance ? tolerance.error()
                               : !length    ? length.error()
                                            : parameter.error());
    }
    features::HoleThread thread{.size = *size, .tolerance = *tolerance, .length = Length::fromSi(*length)};
    if (*parameter) {
        thread.lengthParameter = ParameterId::fromValue(**parameter);
    }
    return thread;
}

Json holeClearanceToJson(const features::HoleClearance& clearance) {
    Json json = Json::object();
    json["size"] = clearance.bolt.designation();
    json["series"] = std::string{standards::toString(clearance.series)};
    return json;
}

Result<features::HoleClearance> holeClearanceFromJson(const Json& data, std::string_view path) {
    if (auto object = requireObject(data, path, {"size", "series"}); !object) {
        return std::unexpected(object.error());
    }
    auto size = readDesignation(data, "size", path, standards::parseMetricThread);
    auto series = readString(data, "series", path);
    if (!size || !series) {
        return std::unexpected(!size ? size.error() : series.error());
    }
    for (const auto& [value, name] : kSeries) {
        if (name == *series) {
            return features::HoleClearance{.bolt = *size, .series = value};
        }
    }
    return parseError(childPath(path, "series"), std::format("unknown value '{}'", *series));
}

Result<standards::HoleToleranceClass> holeToleranceFromJson(const Json& object, std::string_view key,
                                                            std::string_view path) {
    return readDesignation(object, key, path, standards::parseHoleToleranceClass);
}

} // namespace bettercad::io::detail
