#include "ObjectJson.hpp"

#include "JsonReader.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace bettercad::io::detail {
namespace {

using drawing::DrawingScale;
using drawing::SheetDefinition;
using drawing::SheetMargins;
using drawing::TitleBlock;

/// The eight title-block fields, paired with their JSON keys, so the writer
/// and the reader cannot disagree about the set.
constexpr std::array<std::pair<std::string_view, std::string TitleBlock::*>, 8> kTitleBlockFields{{
    {"title", &TitleBlock::title},
    {"drawing_number", &TitleBlock::drawingNumber},
    {"revision", &TitleBlock::revision},
    {"designer", &TitleBlock::designer},
    {"checked_by", &TitleBlock::checkedBy},
    {"approved_by", &TitleBlock::approvedBy},
    {"date", &TitleBlock::date},
    {"organization", &TitleBlock::organization},
}};

/// The four margins, likewise.
constexpr std::array<std::pair<std::string_view, Length SheetMargins::*>, 4> kMarginFields{{
    {"left", &SheetMargins::left},
    {"right", &SheetMargins::right},
    {"top", &SheetMargins::top},
    {"bottom", &SheetMargins::bottom},
}};

[[nodiscard]] Json titleBlockToJson(const TitleBlock& block) {
    Json json = Json::object();
    // Only the fields that say something, so a sheet with an empty title
    // block writes `{}` rather than eight empty strings.
    for (const auto& [key, field] : kTitleBlockFields) {
        if (!(block.*field).empty()) {
            json[std::string{key}] = block.*field;
        }
    }
    return json;
}

[[nodiscard]] Result<TitleBlock> titleBlockFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path,
                                    {"title", "drawing_number", "revision", "designer", "checked_by",
                                     "approved_by", "date", "organization"});
        !object) {
        return std::unexpected(object.error());
    }
    TitleBlock block;
    for (const auto& [key, field] : kTitleBlockFields) {
        const auto found = value.find(std::string{key});
        if (found == value.end()) {
            continue;
        }
        if (!found->is_string()) {
            return parseError(childPath(path, key), "must be a string");
        }
        block.*field = found->get<std::string>();
    }
    return block;
}

[[nodiscard]] Json marginsToJson(const SheetMargins& margins) {
    Json json = Json::object();
    for (const auto& [key, field] : kMarginFields) {
        json[std::string{key}] = (margins.*field).si();
    }
    return json;
}

[[nodiscard]] Result<SheetMargins> marginsFromJson(const Json& value, std::string_view path) {
    if (auto object = requireObject(value, path, {"left", "right", "top", "bottom"}); !object) {
        return std::unexpected(object.error());
    }
    SheetMargins margins;
    for (const auto& [key, field] : kMarginFields) {
        auto read = readNumber(value, key, path);
        if (!read) {
            return std::unexpected(read.error());
        }
        margins.*field = Length::fromSi(*read);
    }
    return margins;
}

} // namespace

// DocumentJson.cpp dispatches on the literal "sheet"; this is what keeps that
// literal and the type name itself from drifting apart. Binding a reference
// to a dll-imported constexpr static does not link in a shared build, which
// is why the reader cannot compare against kTypeName directly.
static_assert(drawing::Sheet::kTypeName == "sheet",
              "the sheet type name and the name the reader dispatches on must agree");

Json sheetToJson(const drawing::Sheet& sheet) {
    const SheetDefinition& d = sheet.definition();
    Json json = Json::object();
    json["format"] = std::string{drawing::toString(d.format)};
    json["orientation"] = std::string{drawing::toString(d.orientation)};
    // A custom sheet's size is intent; a standard sheet's size is derived
    // from its format and is not written, so the file cannot disagree with
    // ISO 216 (ADR-011).
    if (d.format == drawing::SheetFormat::Custom) {
        json["width"] = d.customWidth.si();
        json["height"] = d.customHeight.si();
    }
    json["margins"] = marginsToJson(d.margins);
    // The exact pair, not the quotient: the label is intent and 1:3 has no
    // exact double (ADR-013).
    json["scale"] = Json::array({d.scale.paper, d.scale.model});
    if (!d.titleBlock.empty()) {
        json["title_block"] = titleBlockToJson(d.titleBlock);
    }
    return json;
}

Result<std::unique_ptr<drawing::Sheet>> sheetFromJson(const Json& data, std::string name,
                                                      std::string_view path) {
    if (auto object = requireObject(data, path,
                                    {"format", "orientation", "width", "height", "margins", "scale",
                                     "title_block"});
        !object) {
        return std::unexpected(object.error());
    }

    SheetDefinition definition;

    auto formatText = readString(data, "format", path);
    if (!formatText) {
        return std::unexpected(formatText.error());
    }
    const auto format = drawing::sheetFormatFromString(*formatText);
    if (!format) {
        return parseError(childPath(path, "format"), std::format("unknown sheet format '{}'", *formatText));
    }
    definition.format = *format;

    auto orientationText = readString(data, "orientation", path);
    if (!orientationText) {
        return std::unexpected(orientationText.error());
    }
    const auto orientation = drawing::sheetOrientationFromString(*orientationText);
    if (!orientation) {
        return parseError(childPath(path, "orientation"),
                          std::format("unknown sheet orientation '{}'", *orientationText));
    }
    definition.orientation = *orientation;

    if (definition.format == drawing::SheetFormat::Custom) {
        auto width = readNumber(data, "width", path);
        if (!width) {
            return std::unexpected(width.error());
        }
        auto height = readNumber(data, "height", path);
        if (!height) {
            return std::unexpected(height.error());
        }
        definition.customWidth = Length::fromSi(*width);
        definition.customHeight = Length::fromSi(*height);
    } else if (data.contains("width") || data.contains("height")) {
        // A standard sheet's size comes from its format. A file carrying one
        // is either wrong or from a reader that thought it was authoritative,
        // and either way silently preferring one of the two would be the
        // derived-state-as-truth failure ADR-011 exists to prevent.
        return parseError(path, std::format("a sheet of format '{}' takes its size from the format and "
                                            "must not carry a width or a height",
                                            *formatText));
    }

    const auto margins = data.find("margins");
    if (margins == data.end()) {
        return parseError(path, "missing 'margins'");
    }
    auto read = marginsFromJson(*margins, childPath(path, "margins"));
    if (!read) {
        return std::unexpected(read.error());
    }
    definition.margins = *read;

    const auto scale = data.find("scale");
    if (scale == data.end()) {
        return parseError(path, "missing 'scale'");
    }
    if (!scale->is_array() || scale->size() != 2) {
        return parseError(childPath(path, "scale"), "must be an array of two whole numbers");
    }
    for (std::size_t i = 0; i < 2; ++i) {
        if (!(*scale)[i].is_number_unsigned()) {
            return parseError(childPath(path, "scale"), "must be an array of two whole numbers");
        }
    }
    definition.scale = DrawingScale{(*scale)[0].get<std::uint32_t>(), (*scale)[1].get<std::uint32_t>()};

    if (const auto block = data.find("title_block"); block != data.end()) {
        auto readBlock = titleBlockFromJson(*block, childPath(path, "title_block"));
        if (!readBlock) {
            return std::unexpected(readBlock.error());
        }
        definition.titleBlock = *readBlock;
    }

    // Sheet::create validates, so a file whose margins do not fit its sheet,
    // or whose scale has a zero term, is refused on load rather than becoming
    // a document that cannot be drawn on.
    auto sheet = drawing::Sheet::create(std::move(name), definition);
    if (!sheet) {
        return parseError(path, sheet.error().message);
    }
    return std::move(*sheet);
}

} // namespace bettercad::io::detail
