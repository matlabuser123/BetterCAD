#include "ObjectJson.hpp"

#include "JsonReader.hpp"

#include <format>
#include <string>
#include <utility>

namespace bettercad::io::detail {

// DocumentJson.cpp dispatches on the literal "view"; this is what keeps that
// literal and the type name itself from drifting apart. Binding a reference
// to a dll-imported constexpr static does not link in a shared build.
static_assert(drawing::View::kTypeName == "view",
              "the view type name and the name the reader dispatches on must agree");

Json viewToJson(const drawing::View& view) {
    const drawing::ViewDefinition& d = view.definition();
    Json json = Json::object();
    json["sheet"] = d.sheet.value();

    if (drawing::isBaseView(d)) {
        // A base view says what it draws and which way it looks.
        json["source"] = d.source.object.value();
        json["orientation"] = std::string{drawing::toString(*d.orientation)};
        // Placement is intent for a base view only; a projected view's is
        // derived from its parent's (ADR-018), so writing one would be
        // writing derived state.
        json["x"] = d.placement.x.si();
        json["y"] = d.placement.y.si();
    } else {
        json["parent"] = d.parent->value();
        json["direction"] = std::string{drawing::toString(*d.direction)};
        json["spacing"] = d.spacing.si();
    }

    // Absent means "the sheet's scale", which is not the same as writing the
    // sheet's scale here: a sheet whose scale changes must carry its views
    // with it unless they opted out.
    if (d.scale) {
        json["scale"] = Json::array({d.scale->paper, d.scale->model});
    }
    return json;
}

Result<std::unique_ptr<drawing::View>> viewFromJson(const Json& data, std::string name,
                                                    std::string_view path) {
    if (auto object = requireObject(data, path,
                                    {"sheet", "source", "orientation", "x", "y", "parent",
                                     "direction", "spacing", "scale"});
        !object) {
        return std::unexpected(object.error());
    }

    drawing::ViewDefinition definition;

    auto sheet = readId(data, "sheet", path);
    if (!sheet) {
        return std::unexpected(sheet.error());
    }
    definition.sheet = SheetId::fromValue(*sheet);

    const bool hasOrientation = data.contains("orientation");
    const bool hasParent = data.contains("parent");
    if (hasOrientation == hasParent) {
        return parseError(path, hasOrientation
                                    ? "a view has either an orientation or a parent, not both"
                                    : "a view must have either an orientation or a parent");
    }

    if (hasOrientation) {
        auto orientationText = readString(data, "orientation", path);
        if (!orientationText) {
            return std::unexpected(orientationText.error());
        }
        const auto orientation = drawing::standardViewFromString(*orientationText);
        if (!orientation) {
            return parseError(childPath(path, "orientation"),
                              std::format("unknown view orientation '{}'", *orientationText));
        }
        definition.orientation = *orientation;

        auto source = readId(data, "source", path);
        if (!source) {
            return std::unexpected(source.error());
        }
        definition.source = ObjectReference{ObjectId::fromValue(*source)};

        auto x = readNumber(data, "x", path);
        if (!x) {
            return std::unexpected(x.error());
        }
        auto y = readNumber(data, "y", path);
        if (!y) {
            return std::unexpected(y.error());
        }
        definition.placement = Point2D{Length::fromSi(*x), Length::fromSi(*y)};
    } else {
        auto parent = readId(data, "parent", path);
        if (!parent) {
            return std::unexpected(parent.error());
        }
        definition.parent = ViewId::fromValue(*parent);

        auto directionText = readString(data, "direction", path);
        if (!directionText) {
            return std::unexpected(directionText.error());
        }
        const auto direction = drawing::projectedDirectionFromString(*directionText);
        if (!direction) {
            return parseError(childPath(path, "direction"),
                              std::format("unknown projected direction '{}'", *directionText));
        }
        definition.direction = *direction;

        auto spacing = readNumber(data, "spacing", path);
        if (!spacing) {
            return std::unexpected(spacing.error());
        }
        definition.spacing = Length::fromSi(*spacing);
    }

    if (const auto scale = data.find("scale"); scale != data.end()) {
        if (!scale->is_array() || scale->size() != 2) {
            return parseError(childPath(path, "scale"), "must be an array of two whole numbers");
        }
        for (std::size_t i = 0; i < 2; ++i) {
            if (!(*scale)[i].is_number_unsigned()) {
                return parseError(childPath(path, "scale"), "must be an array of two whole numbers");
            }
        }
        definition.scale = drawing::DrawingScale{(*scale)[0].get<std::uint32_t>(),
                                                 (*scale)[1].get<std::uint32_t>()};
    }

    // View::create validates, so a file describing a view that is both a base
    // and a projected view, or one with a zero-term scale, is refused on load
    // rather than becoming a document that cannot be drawn.
    auto view = drawing::View::create(std::move(name), definition);
    if (!view) {
        return parseError(path, view.error().message);
    }
    return std::move(*view);
}

} // namespace bettercad::io::detail
