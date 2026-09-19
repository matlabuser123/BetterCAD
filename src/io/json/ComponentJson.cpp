#include "ObjectJson.hpp"

#include "JsonReader.hpp"

#include <utility>

namespace bettercad::io::detail {

// DocumentJson.cpp dispatches on the literal "component"; this is what
// keeps that literal and the type name itself from drifting apart.
static_assert(assembly::Component::kTypeName == "component",
              "the component type name and the name the reader dispatches on must agree");

Json componentToJson(const assembly::Component& component) {
    const assembly::ComponentDefinition& d = component.definition();
    Json json = Json::object();
    json["part"] = d.part.value();
    // Written only when true, so the common case stays as small as the
    // shape it describes and a document of unsuppressed components has no
    // redundant keys.
    if (d.suppressed) {
        json["suppressed"] = true;
    }
    return json;
}

Result<std::unique_ptr<assembly::Component>> componentFromJson(const Json& data, std::string name,
                                                               std::string_view path) {
    if (auto valid = requireObject(data, path, {"part", "suppressed"}); !valid) {
        return std::unexpected(valid.error());
    }
    auto part = readId(data, "part", path);
    if (!part) {
        return std::unexpected(part.error());
    }
    assembly::ComponentDefinition d{.part = ObjectId::fromValue(*part)};
    if (const auto suppressed = data.find("suppressed"); suppressed != data.end()) {
        if (!suppressed->is_boolean()) {
            return parseError(childPath(path, "suppressed"), "must be a boolean");
        }
        d.suppressed = suppressed->get<bool>();
    }
    return assembly::Component::create(std::move(name), d);
}

} // namespace bettercad::io::detail
