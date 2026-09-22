#include "ObjectJson.hpp"

#include "JsonReader.hpp"

#include <format>
#include <string>
#include <utility>

namespace bettercad::io::detail {
namespace {

Json cuttingPlaneToJson(const drawing::CuttingPlane& plane) {
    Json json = Json::object();
    json["kind"] = std::string{drawing::toString(plane.kind)};
    json["origin"] = pointToJson(plane.origin);
    json["normal"] = directionToJson(plane.normal);
    json["reference"] = directionToJson(plane.reference);
    // A half section's split and an offset section's legs are written only by
    // the kind that has them, so a file cannot describe a full section that
    // also steps.
    if (plane.kind == drawing::SectionKind::Half) {
        json["split_at"] = plane.splitAt.si();
    }
    if (plane.kind == drawing::SectionKind::Offset) {
        Json legs = Json::array();
        for (const drawing::SectionLeg& leg : plane.legs) {
            legs.push_back(Json{{"at", leg.at.si()}, {"offset", leg.offset.si()}});
        }
        json["legs"] = std::move(legs);
    }
    return json;
}

Result<drawing::CuttingPlane> cuttingPlaneFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path,
                                    {"kind", "origin", "normal", "reference", "split_at", "legs"});
        !object) {
        return std::unexpected(object.error());
    }
    drawing::CuttingPlane plane;

    auto kindText = readString(value, "kind", path);
    if (!kindText) {
        return std::unexpected(kindText.error());
    }
    const auto kind = drawing::sectionKindFromString(*kindText);
    if (!kind) {
        return parseError(childPath(path, "kind"),
                          std::format("unknown section kind '{}'", *kindText));
    }
    plane.kind = *kind;

    auto origin = pointFromJson(value, "origin", path);
    if (!origin) {
        return std::unexpected(origin.error());
    }
    plane.origin = *origin;

    auto normal = directionFromJson(value, "normal", path);
    if (!normal) {
        return std::unexpected(normal.error());
    }
    plane.normal = *normal;

    auto reference = directionFromJson(value, "reference", path);
    if (!reference) {
        return std::unexpected(reference.error());
    }
    plane.reference = *reference;

    if (plane.kind == drawing::SectionKind::Half) {
        auto splitAt = readNumber(value, "split_at", path);
        if (!splitAt) {
            return std::unexpected(splitAt.error());
        }
        plane.splitAt = Length::fromSi(*splitAt);
    }
    if (plane.kind == drawing::SectionKind::Offset) {
        const auto legs = value.find("legs");
        if (legs == value.end() || !legs->is_array()) {
            return parseError(childPath(path, "legs"), "an offset section must list its legs");
        }
        const std::string legsPath{childPath(path, "legs")};
        for (const Json& leg : *legs) {
            if (auto object = requireObject(leg, legsPath, {"at", "offset"}); !object) {
                return std::unexpected(object.error());
            }
            auto at = readNumber(leg, "at", legsPath);
            if (!at) {
                return std::unexpected(at.error());
            }
            auto offset = readNumber(leg, "offset", legsPath);
            if (!offset) {
                return std::unexpected(offset.error());
            }
            plane.legs.push_back({Length::fromSi(*at), Length::fromSi(*offset)});
        }
    }
    // validate() is not called here: View::create runs it on the whole
    // definition, so a bad plane is refused once, with one message, whichever
    // way the definition was built.
    return plane;
}

Json hatchToJson(const drawing::HatchSettings& hatch) {
    return Json{{"angle", hatch.angle.si()}, {"spacing", hatch.spacing.si()},
                {"pattern", hatch.pattern}};
}

Result<drawing::HatchSettings> hatchFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"angle", "spacing", "pattern"}); !object) {
        return std::unexpected(object.error());
    }
    drawing::HatchSettings hatch;
    auto angle = readNumber(value, "angle", path);
    if (!angle) {
        return std::unexpected(angle.error());
    }
    hatch.angle = Angle::fromSi(*angle);
    auto spacing = readNumber(value, "spacing", path);
    if (!spacing) {
        return std::unexpected(spacing.error());
    }
    hatch.spacing = Length::fromSi(*spacing);
    auto pattern = readString(value, "pattern", path);
    if (!pattern) {
        return std::unexpected(pattern.error());
    }
    hatch.pattern = *pattern;
    return hatch;
}

} // namespace

// DocumentJson.cpp dispatches on the literal "view"; this is what keeps that
// literal and the type name itself from drifting apart. Binding a reference
// to a dll-imported constexpr static does not link in a shared build.
static_assert(drawing::View::kTypeName == "view",
              "the view type name and the name the reader dispatches on must agree");

Json viewToJson(const drawing::View& view) {
    const drawing::ViewDefinition& d = view.definition();
    Json json = Json::object();
    // The kind is written first and always. A reader that had to infer it
    // from which other keys were present would have to be changed every time
    // a kind was added, and would read a file with the wrong keys as some
    // other kind rather than refusing it.
    json["kind"] = std::string{drawing::toString(d.kind)};
    json["sheet"] = d.sheet.value();

    if (d.kind == drawing::ViewKind::Base) {
        // A base view says what it draws and which way it looks.
        json["source"] = d.source.object.value();
        json["orientation"] = std::string{drawing::toString(*d.orientation)};
    } else {
        json["parent"] = d.parent->value();
    }

    switch (d.kind) {
    case drawing::ViewKind::Base:
        break;
    case drawing::ViewKind::Projected:
        json["direction"] = std::string{drawing::toString(*d.direction)};
        break;
    case drawing::ViewKind::Section:
        json["section"] = cuttingPlaneToJson(*d.section);
        json["hatch"] = hatchToJson(d.hatch);
        break;
    case drawing::ViewKind::Detail:
        json["detail"] = Json{{"x", d.detail->centre.x.si()},
                              {"y", d.detail->centre.y.si()},
                              {"radius", d.detail->radius.si()},
                              {"cropped", d.detail->cropped}};
        break;
    case drawing::ViewKind::Auxiliary:
        json["auxiliary"] = Json{{"normal", directionToJson(d.auxiliary->normal)},
                                 {"reference", directionToJson(d.auxiliary->reference)}};
        break;
    }

    // Placement is intent for the kinds that are placed directly; spacing is
    // intent for the kinds that align to a parent. Writing the other one would
    // be writing derived state (ADR-011), which a later edit could then
    // contradict.
    if (d.kind == drawing::ViewKind::Base || d.kind == drawing::ViewKind::Detail) {
        json["x"] = d.placement.x.si();
        json["y"] = d.placement.y.si();
    } else {
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
                                    {"kind", "sheet", "source", "orientation", "x", "y", "parent",
                                     "direction", "spacing", "scale", "section", "hatch", "detail",
                                     "auxiliary"});
        !object) {
        return std::unexpected(object.error());
    }

    drawing::ViewDefinition definition;

    auto kindText = readString(data, "kind", path);
    if (!kindText) {
        return std::unexpected(kindText.error());
    }
    const auto kind = drawing::viewKindFromString(*kindText);
    if (!kind) {
        return parseError(childPath(path, "kind"), std::format("unknown view kind '{}'", *kindText));
    }
    definition.kind = *kind;

    auto sheet = readId(data, "sheet", path);
    if (!sheet) {
        return std::unexpected(sheet.error());
    }
    definition.sheet = SheetId::fromValue(*sheet);

    if (definition.kind == drawing::ViewKind::Base) {
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
    } else {
        auto parent = readId(data, "parent", path);
        if (!parent) {
            return std::unexpected(parent.error());
        }
        definition.parent = ViewId::fromValue(*parent);
    }

    switch (definition.kind) {
    case drawing::ViewKind::Base:
        break;
    case drawing::ViewKind::Projected: {
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
        break;
    }
    case drawing::ViewKind::Section: {
        const auto section = data.find("section");
        if (section == data.end()) {
            return parseError(path, "a section view must name the plane it cuts on");
        }
        auto plane = cuttingPlaneFromJson(*section, childPath(path, "section"));
        if (!plane) {
            return std::unexpected(plane.error());
        }
        definition.section = std::move(*plane);

        const auto hatch = data.find("hatch");
        if (hatch == data.end()) {
            return parseError(path, "a section view must say how it is hatched");
        }
        auto settings = hatchFromJson(*hatch, childPath(path, "hatch"));
        if (!settings) {
            return std::unexpected(settings.error());
        }
        definition.hatch = std::move(*settings);
        break;
    }
    case drawing::ViewKind::Detail: {
        const auto detail = data.find("detail");
        if (detail == data.end()) {
            return parseError(path, "a detail view must name the region of its parent it enlarges");
        }
        const std::string detailPath{childPath(path, "detail")};
        if (auto object = requireObject(*detail, detailPath, {"x", "y", "radius", "cropped"});
            !object) {
            return std::unexpected(object.error());
        }
        auto x = readNumber(*detail, "x", detailPath);
        if (!x) {
            return std::unexpected(x.error());
        }
        auto y = readNumber(*detail, "y", detailPath);
        if (!y) {
            return std::unexpected(y.error());
        }
        auto radius = readNumber(*detail, "radius", detailPath);
        if (!radius) {
            return std::unexpected(radius.error());
        }
        auto cropped = readBool(*detail, "cropped", detailPath);
        if (!cropped) {
            return std::unexpected(cropped.error());
        }
        definition.detail = drawing::DetailRegion{
            Point2D{Length::fromSi(*x), Length::fromSi(*y)}, Length::fromSi(*radius), *cropped};
        break;
    }
    case drawing::ViewKind::Auxiliary: {
        const auto auxiliary = data.find("auxiliary");
        if (auxiliary == data.end()) {
            return parseError(path, "an auxiliary view must name the direction it looks from");
        }
        const std::string auxiliaryPath{childPath(path, "auxiliary")};
        if (auto object = requireObject(*auxiliary, auxiliaryPath, {"normal", "reference"});
            !object) {
            return std::unexpected(object.error());
        }
        auto normal = directionFromJson(*auxiliary, "normal", auxiliaryPath);
        if (!normal) {
            return std::unexpected(normal.error());
        }
        auto reference = directionFromJson(*auxiliary, "reference", auxiliaryPath);
        if (!reference) {
            return std::unexpected(reference.error());
        }
        definition.auxiliary = drawing::ViewDirection{*normal, *reference};
        break;
    }
    }

    if (definition.kind == drawing::ViewKind::Base || definition.kind == drawing::ViewKind::Detail) {
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

    // View::create validates, so a file describing a view carrying another
    // kind's intent, or one with a zero-term scale, is refused on load rather
    // than becoming a document that cannot be drawn.
    auto view = drawing::View::create(std::move(name), definition);
    if (!view) {
        return parseError(path, view.error().message);
    }
    return std::move(*view);
}

} // namespace bettercad::io::detail
