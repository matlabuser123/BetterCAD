#include "ObjectJson.hpp"

#include "JsonReader.hpp"

#include <format>
#include <string>
#include <utility>

namespace bettercad::io::detail {
namespace {

/// One end of a measurement. Exactly one of the three keys, matching the
/// vocabulary ADR-012 permits and nothing wider.
Json targetToJson(const drawing::DimensionTarget& target) {
    Json json = Json::object();
    if (target.plane) {
        json["plane"] = planeReferenceToJson(*target.plane);
    } else if (target.axis) {
        json["axis"] = axisReferenceToJson(*target.axis);
    } else if (target.cylinder) {
        json["cylinder"] = faceNameToJson(*target.cylinder);
    }
    return json;
}

Result<drawing::DimensionTarget> targetFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"plane", "axis", "cylinder"}); !object) {
        return std::unexpected(object.error());
    }
    drawing::DimensionTarget target;
    if (const auto plane = value.find("plane"); plane != value.end()) {
        auto reference = planeReferenceFromJson(*plane, childPath(path, "plane"));
        if (!reference) {
            return std::unexpected(reference.error());
        }
        target.plane = *reference;
    }
    if (const auto axis = value.find("axis"); axis != value.end()) {
        auto reference = axisReferenceFromJson(*axis, childPath(path, "axis"));
        if (!reference) {
            return std::unexpected(reference.error());
        }
        target.axis = *reference;
    }
    if (const auto cylinder = value.find("cylinder"); cylinder != value.end()) {
        auto name = faceNameFromJson(*cylinder, childPath(path, "cylinder"));
        if (!name) {
            return std::unexpected(name.error());
        }
        target.cylinder = *name;
    }
    // Whether exactly one was given is validate()'s to say, so a file with
    // two gets the same message a caller building one in memory would.
    return target;
}

Json formatToJson(const drawing::DimensionFormat& format) {
    return Json{{"decimals", format.decimals},
                {"trailing_zeros", format.trailingZeros},
                {"unit", format.unit},
                {"show_unit", format.showUnit}};
}

Result<drawing::DimensionFormat> formatFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path,
                                    {"decimals", "trailing_zeros", "unit", "show_unit"});
        !object) {
        return std::unexpected(object.error());
    }
    drawing::DimensionFormat format;
    const auto decimals = value.find("decimals");
    if (decimals == value.end() || !decimals->is_number_unsigned()) {
        return parseError(childPath(path, "decimals"), "must be a whole number");
    }
    const auto count = decimals->get<std::uint64_t>();
    if (count > 255) {
        return parseError(childPath(path, "decimals"), "is far more than any drawing can show");
    }
    format.decimals = static_cast<std::uint8_t>(count);

    auto trailing = readBool(value, "trailing_zeros", path);
    if (!trailing) {
        return std::unexpected(trailing.error());
    }
    format.trailingZeros = *trailing;

    auto unit = readString(value, "unit", path);
    if (!unit) {
        return std::unexpected(unit.error());
    }
    format.unit = *unit;

    auto showUnit = readBool(value, "show_unit", path);
    if (!showUnit) {
        return std::unexpected(showUnit.error());
    }
    format.showUnit = *showUnit;
    return format;
}

} // namespace

// DocumentJson.cpp dispatches on the literal "dimension"; this is what keeps
// that literal and the type name itself from drifting apart. Binding a
// reference to a dll-imported constexpr static does not link in a shared
// build.
static_assert(drawing::Dimension::kTypeName == "dimension",
              "the dimension type name and the name the reader dispatches on must agree");

Json dimensionToJson(const drawing::Dimension& dimension) {
    const drawing::DimensionDefinition& d = dimension.definition();
    Json json = Json::object();
    json["type"] = std::string{drawing::toString(d.type)};
    json["view"] = d.view.value();
    json["from"] = targetToJson(d.from);
    // A radius and a diameter measure one thing, so they write one target.
    // Writing an empty second one would be writing a fact with no meaning.
    if (!drawing::isSingleTarget(d.type)) {
        json["to"] = targetToJson(d.to);
    }
    if (d.type == drawing::DimensionType::Ordinate) {
        json["ordinate"] = std::string{drawing::toString(d.ordinate)};
    }
    json["format"] = formatToJson(d.format);
    json["x"] = d.placement.x.si();
    json["y"] = d.placement.y.si();
    // The measured VALUE is not written, and there is nowhere in this format
    // to put it. A file that carried a number would be a file that could
    // disagree with the model it describes (ADR-011).
    return json;
}

Result<std::unique_ptr<drawing::Dimension>> dimensionFromJson(const Json& data, std::string name,
                                                              std::string_view path) {
    if (auto object = requireObject(data, path,
                                    {"type", "view", "from", "to", "ordinate", "format", "x", "y"});
        !object) {
        return std::unexpected(object.error());
    }

    drawing::DimensionDefinition definition;

    auto typeText = readString(data, "type", path);
    if (!typeText) {
        return std::unexpected(typeText.error());
    }
    const auto type = drawing::dimensionTypeFromString(*typeText);
    if (!type) {
        return parseError(childPath(path, "type"),
                          std::format("unknown dimension type '{}'", *typeText));
    }
    definition.type = *type;

    auto view = readId(data, "view", path);
    if (!view) {
        return std::unexpected(view.error());
    }
    definition.view = ViewId::fromValue(*view);

    const auto from = data.find("from");
    if (from == data.end()) {
        return parseError(path, "a dimension must say what it measures from");
    }
    auto resolvedFrom = targetFromJson(*from, childPath(path, "from"));
    if (!resolvedFrom) {
        return std::unexpected(resolvedFrom.error());
    }
    definition.from = std::move(*resolvedFrom);

    if (const auto to = data.find("to"); to != data.end()) {
        auto resolvedTo = targetFromJson(*to, childPath(path, "to"));
        if (!resolvedTo) {
            return std::unexpected(resolvedTo.error());
        }
        definition.to = std::move(*resolvedTo);
    }

    if (const auto ordinate = data.find("ordinate"); ordinate != data.end()) {
        auto axisText = readString(data, "ordinate", path);
        if (!axisText) {
            return std::unexpected(axisText.error());
        }
        const auto axis = drawing::ordinateAxisFromString(*axisText);
        if (!axis) {
            return parseError(childPath(path, "ordinate"),
                              std::format("unknown ordinate axis '{}'", *axisText));
        }
        definition.ordinate = *axis;
    }

    const auto format = data.find("format");
    if (format == data.end()) {
        return parseError(path, "a dimension must say how it is written");
    }
    auto resolvedFormat = formatFromJson(*format, childPath(path, "format"));
    if (!resolvedFormat) {
        return std::unexpected(resolvedFormat.error());
    }
    definition.format = std::move(*resolvedFormat);

    auto x = readNumber(data, "x", path);
    if (!x) {
        return std::unexpected(x.error());
    }
    auto y = readNumber(data, "y", path);
    if (!y) {
        return std::unexpected(y.error());
    }
    definition.placement = Point2D{Length::fromSi(*x), Length::fromSi(*y)};

    // Dimension::create validates, so a file describing a radius with two
    // targets, an angle written in millimetres, or a unit this build does not
    // know is refused on load rather than becoming a drawing that cannot be
    // measured.
    auto dimension = drawing::Dimension::create(std::move(name), definition);
    if (!dimension) {
        return parseError(path, dimension.error().message);
    }
    return std::move(*dimension);
}

} // namespace bettercad::io::detail
