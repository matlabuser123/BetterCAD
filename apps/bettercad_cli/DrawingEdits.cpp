#include "EditSupport.hpp"

#include "Edits.hpp"
#include "Selectors.hpp"

#include <bettercad/core/Naming.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/drawing/Annotations.hpp>
#include <bettercad/drawing/Commands.hpp>
#include <bettercad/drawing/Dimensions.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Drawing edits from a command line (P14-CLI-001).
//
// THE WHOLE FILE IS AN ADAPTER. Every command here reads arguments, resolves
// selectors through the qualified vocabulary, builds a definition, and hands
// it to a P14-CMD-001 command object. Nothing here projects, measures, counts
// a BOM row or decides what a face is; the CLI is not a second CAD engine,
// and the one way to keep it from becoming one is to give it no arithmetic to
// do.
//
// Atomicity comes from the shape the edit spine already has (ADR-009): load,
// apply, save only if every edit succeeded. Each command below either applies
// entirely or leaves the document untouched, because each is one P14-CMD-001
// command and those are all-or-nothing.
namespace bettercad::cli {
namespace {

// --- reading the options ------------------------------------------------------------------------

/// A drawing scale, written `paper:model` -- "1:2", "2:1", "1:1".
///
/// The pair, never the quotient: 1:3 has no exact double, and the label is
/// intent (ADR-013).
Result<drawing::DrawingScale> parseScale(std::string_view text) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is not a scale; expected paper:model, e.g. 1:2", text));
    }
    // Unsigned on purpose: a scale is a ratio of two counts, so "-1" is not
    // a number that got out of range -- it is not a scale at all, and saying
    // so is more use than reporting an overflow.
    const auto number = [](std::string_view field) -> Result<std::uint32_t> {
        std::uint32_t value = 0;
        const auto* const end = field.data() + field.size();
        const auto [stop, code] = std::from_chars(field.data(), end, value);
        if (code != std::errc{} || stop != end) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("'{}' is not a positive whole number", field));
        }
        return value;
    };
    auto paper = number(text.substr(0, colon));
    if (!paper) {
        return std::unexpected(paper.error());
    }
    auto model = number(text.substr(colon + 1));
    if (!model) {
        return std::unexpected(model.error());
    }
    drawing::DrawingScale scale{*paper, *model};
    if (auto valid = validate(scale); !valid) {
        return std::unexpected(valid.error());
    }
    return scale;
}

Result<drawing::SheetFormat> parseSheetFormat(std::string_view text) {
    if (auto format = drawing::sheetFormatFromString(text)) {
        return *format;
    }
    return makeError(ErrorCode::InvalidArgument,
                     std::format("'{}' is not a sheet format; expected A0, A1, A2, A3, A4 or custom", text));
}

Result<drawing::SheetOrientation> parseOrientation(std::string_view text) {
    if (auto orientation = drawing::sheetOrientationFromString(text)) {
        return *orientation;
    }
    return makeError(ErrorCode::InvalidArgument,
                     std::format("'{}' is not an orientation; expected landscape or portrait", text));
}

Result<drawing::StandardView> parseStandardView(std::string_view text) {
    if (auto view = drawing::standardViewFromString(text)) {
        return *view;
    }
    return makeError(ErrorCode::InvalidArgument,
                     std::format("'{}' is not a standard view; expected front, back, left, right, top "
                                 "or bottom",
                                 text));
}

Result<drawing::ProjectedDirection> parseProjectedDirection(std::string_view text) {
    if (auto direction = drawing::projectedDirectionFromString(text)) {
        return *direction;
    }
    return makeError(ErrorCode::InvalidArgument,
                     std::format("'{}' is not a projection direction; expected top, bottom, left or right",
                                 text));
}

Result<drawing::DimensionType> parseDimensionType(std::string_view text) {
    if (auto type = drawing::dimensionTypeFromString(text)) {
        return *type;
    }
    return makeError(ErrorCode::InvalidArgument,
                     std::format("'{}' is not a dimension type", text));
}

Result<drawing::AnnotationType> parseAnnotationType(std::string_view text) {
    if (auto type = drawing::annotationTypeFromString(text)) {
        return *type;
    }
    return makeError(ErrorCode::InvalidArgument, std::format("'{}' is not an annotation type", text));
}

/// A length option in sheet millimetres, which is what a drawing's paper
/// coordinates are.
Result<std::optional<Length>> lengthOption(const ParsedArguments& parsed, std::string_view option) {
    const auto text = parsed.value(option);
    if (!text) {
        return std::optional<Length>{};
    }
    auto length = parseLength(*text, units::mm);
    if (!length) {
        return std::unexpected(length.error());
    }
    return std::optional<Length>{*length};
}

/// `--x` and `--y` together: a placement is a point, so half of one is a
/// mistake worth naming rather than a default worth inventing.
Result<std::optional<Point2D>> placementOption(const ParsedArguments& parsed) {
    auto x = lengthOption(parsed, "--x");
    if (!x) {
        return std::unexpected(x.error());
    }
    auto y = lengthOption(parsed, "--y");
    if (!y) {
        return std::unexpected(y.error());
    }
    if (x->has_value() != y->has_value()) {
        return makeError(ErrorCode::InvalidArgument,
                         "--x and --y go together: a placement is a point on the sheet");
    }
    if (!x->has_value()) {
        return std::optional<Point2D>{};
    }
    return std::optional<Point2D>{Point2D{**x, **y}};
}

/// The one positional argument a command that edits an existing object takes.
Result<std::string_view> onlySelector(const ParsedArguments& parsed, std::string_view what) {
    if (parsed.positional().size() != 1) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("expected exactly one {} selector", what));
    }
    return parsed.positional().front();
}

template <typename Id>
Result<Id> resolveDrawingObject(const Document& document, std::string_view selector,
                                std::string_view what, auto find) {
    auto object = resolveObject(document, selector);
    if (!object) {
        return std::unexpected(object.error());
    }
    const Id id = Id::fromValue(object->value());
    if (find(document, id) == nullptr) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{} has type '{}'; expected a {}", label(document, *object),
                                     document.findObject(*object)->typeName(), what));
    }
    return id;
}

Result<SheetId> resolveSheet(const Document& document, std::string_view selector) {
    return resolveDrawingObject<SheetId>(document, selector, "sheet",
                                         [](const Document& d, SheetId id) { return drawing::findSheet(d, id); });
}
Result<ViewId> resolveView(const Document& document, std::string_view selector) {
    return resolveDrawingObject<ViewId>(document, selector, "view",
                                        [](const Document& d, ViewId id) { return drawing::findView(d, id); });
}
Result<DimensionId> resolveDimension(const Document& document, std::string_view selector) {
    return resolveDrawingObject<DimensionId>(
        document, selector, "dimension",
        [](const Document& d, DimensionId id) { return drawing::findDimension(d, id); });
}
Result<AnnotationId> resolveAnnotation(const Document& document, std::string_view selector) {
    return resolveDrawingObject<AnnotationId>(
        document, selector, "annotation",
        [](const Document& d, AnnotationId id) { return drawing::findAnnotation(d, id); });
}

// --- sheets ---------------------------------------------------------------------------------------

EditResult applySheetAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--name", true},
                                        {"--format", true},
                                        {"--orientation", true},
                                        {"--scale", true},
                                        {"--margin", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (!parsed->positional().empty()) {
        return std::unexpected(malformed(std::format("unexpected argument '{}'", parsed->positional().front())));
    }
    drawing::SheetDefinition definition{};
    if (const auto format = parsed->value("--format")) {
        auto parsedFormat = parseSheetFormat(*format);
        if (!parsedFormat) {
            return std::unexpected(fromParse("--format", parsedFormat.error()));
        }
        definition.format = *parsedFormat;
    }
    if (const auto orientation = parsed->value("--orientation")) {
        auto parsedOrientation = parseOrientation(*orientation);
        if (!parsedOrientation) {
            return std::unexpected(fromParse("--orientation", parsedOrientation.error()));
        }
        definition.orientation = *parsedOrientation;
    }
    if (const auto scale = parsed->value("--scale")) {
        auto parsedScale = parseScale(*scale);
        if (!parsedScale) {
            return std::unexpected(fromParse("--scale", parsedScale.error()));
        }
        definition.scale = *parsedScale;
    }
    auto margin = lengthOption(*parsed, "--margin");
    if (!margin) {
        return std::unexpected(fromParse("--margin", margin.error()));
    }
    if (*margin) {
        definition.margins = drawing::SheetMargins{**margin, **margin, **margin, **margin};
    }
    auto name = namedOr(*parsed, document, "Sheet");
    if (!name) {
        return std::unexpected(malformed(name.error().message));
    }

    drawing::CreateSheetCommand command{*name, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Created {} ({} {}, scale {})", label(document, ObjectId{command.sheetId()}),
                       drawing::toString(definition.format), drawing::toString(definition.orientation),
                       definition.scale.label());
}

EditResult applySheetSet(Document& document, Args args) {
    auto parsed = parseArguments(
        args, {{"--format", true}, {"--orientation", true}, {"--scale", true}, {"--margin", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    auto selector = onlySelector(*parsed, "sheet");
    if (!selector) {
        return std::unexpected(malformed(selector.error().message));
    }
    auto sheet = resolveSheet(document, *selector);
    if (!sheet) {
        return std::unexpected(fromParse(sheet.error()));
    }
    drawing::SheetDefinition definition = drawing::findSheet(document, *sheet)->definition();
    if (const auto format = parsed->value("--format")) {
        auto parsedFormat = parseSheetFormat(*format);
        if (!parsedFormat) {
            return std::unexpected(fromParse("--format", parsedFormat.error()));
        }
        definition.format = *parsedFormat;
    }
    if (const auto orientation = parsed->value("--orientation")) {
        auto parsedOrientation = parseOrientation(*orientation);
        if (!parsedOrientation) {
            return std::unexpected(fromParse("--orientation", parsedOrientation.error()));
        }
        definition.orientation = *parsedOrientation;
    }
    if (const auto scale = parsed->value("--scale")) {
        auto parsedScale = parseScale(*scale);
        if (!parsedScale) {
            return std::unexpected(fromParse("--scale", parsedScale.error()));
        }
        definition.scale = *parsedScale;
    }
    auto margin = lengthOption(*parsed, "--margin");
    if (!margin) {
        return std::unexpected(fromParse("--margin", margin.error()));
    }
    if (*margin) {
        definition.margins = drawing::SheetMargins{**margin, **margin, **margin, **margin};
    }

    drawing::SetSheetDefinitionCommand command{*sheet, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("{} is now {} {}, scale {}", label(document, ObjectId{*sheet}),
                       drawing::toString(definition.format), drawing::toString(definition.orientation),
                       definition.scale.label());
}

EditResult applySheetRemove(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    auto selector = onlySelector(*parsed, "sheet");
    if (!selector) {
        return std::unexpected(malformed(selector.error().message));
    }
    auto sheet = resolveSheet(document, *selector);
    if (!sheet) {
        return std::unexpected(fromParse(sheet.error()));
    }
    const std::string named = label(document, ObjectId{*sheet});

    drawing::DeleteSheetCommand command{*sheet};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Removed {}", named);
}

// --- views ------------------------------------------------------------------------------------------

EditResult applyViewAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--name", true},
                                        {"--sheet", true},
                                        {"--source", true},
                                        {"--assembly", false},
                                        {"--orientation", true},
                                        {"--parent", true},
                                        {"--direction", true},
                                        {"--spacing", true},
                                        {"--scale", true},
                                        {"--x", true},
                                        {"--y", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (!parsed->positional().empty()) {
        return std::unexpected(malformed(std::format("unexpected argument '{}'", parsed->positional().front())));
    }
    const auto sheetText = parsed->value("--sheet");
    if (!sheetText) {
        return std::unexpected(malformed("--sheet is required: the sheet this view sits on"));
    }
    auto sheet = resolveSheet(document, *sheetText);
    if (!sheet) {
        return std::unexpected(fromParse(sheet.error()));
    }

    drawing::ViewDefinition definition{.sheet = *sheet};
    if (const auto parentText = parsed->value("--parent")) {
        auto parent = resolveView(document, *parentText);
        if (!parent) {
            return std::unexpected(fromParse(parent.error()));
        }
        definition.kind = drawing::ViewKind::Projected;
        definition.parent = *parent;
        const auto directionText = parsed->value("--direction");
        if (!directionText) {
            return std::unexpected(malformed("--direction is required with --parent: which way the view "
                                             "is projected from it"));
        }
        auto direction = parseProjectedDirection(*directionText);
        if (!direction) {
            return std::unexpected(fromParse("--direction", direction.error()));
        }
        definition.direction = *direction;
        auto spacing = lengthOption(*parsed, "--spacing");
        if (!spacing) {
            return std::unexpected(fromParse("--spacing", spacing.error()));
        }
        if (!*spacing) {
            return std::unexpected(malformed("--spacing is required with --parent: how far from it the "
                                             "view sits"));
        }
        definition.spacing = **spacing;
    } else if (parsed->has("--assembly")) {
        definition.subject = drawing::ViewSubject::Assembly;
        definition.orientation = drawing::StandardView::Front;
    } else {
        const auto sourceText = parsed->value("--source");
        if (!sourceText) {
            return std::unexpected(malformed("--source is required: the object this view draws (or "
                                             "--assembly for the whole assembly, or --parent to project "
                                             "from another view)"));
        }
        auto source = resolveObject(document, *sourceText);
        if (!source) {
            return std::unexpected(fromParse(source.error()));
        }
        definition.source = ObjectReference{*source};
        definition.orientation = drawing::StandardView::Front;
    }

    if (const auto orientationText = parsed->value("--orientation")) {
        if (definition.parent) {
            return std::unexpected(malformed("a projected view takes its orientation from its parent "
                                             "and its direction, not --orientation"));
        }
        auto orientation = parseStandardView(*orientationText);
        if (!orientation) {
            return std::unexpected(fromParse("--orientation", orientation.error()));
        }
        definition.orientation = *orientation;
    }
    if (const auto scaleText = parsed->value("--scale")) {
        auto scale = parseScale(*scaleText);
        if (!scale) {
            return std::unexpected(fromParse("--scale", scale.error()));
        }
        definition.scale = *scale;
    }
    auto placement = placementOption(*parsed);
    if (!placement) {
        return std::unexpected(fromParse(placement.error()));
    }
    if (*placement) {
        definition.placement = **placement;
    }
    auto name = namedOr(*parsed, document, "View");
    if (!name) {
        return std::unexpected(malformed(name.error().message));
    }

    drawing::CreateViewCommand command{*name, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Created {} on {}", label(document, ObjectId{command.viewId()}),
                       label(document, ObjectId{*sheet}));
}

EditResult applyViewMove(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--x", true}, {"--y", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    auto selector = onlySelector(*parsed, "view");
    if (!selector) {
        return std::unexpected(malformed(selector.error().message));
    }
    auto view = resolveView(document, *selector);
    if (!view) {
        return std::unexpected(fromParse(view.error()));
    }
    auto placement = placementOption(*parsed);
    if (!placement) {
        return std::unexpected(fromParse(placement.error()));
    }
    if (!*placement) {
        return std::unexpected(malformed("--x and --y are required: where on the sheet to move it"));
    }

    drawing::MoveViewCommand command{*view, **placement};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Moved {} to {:.3g}, {:.3g} mm", label(document, ObjectId{*view}),
                       (*placement)->x.in(units::mm), (*placement)->y.in(units::mm));
}

EditResult applyViewSet(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--scale", true}, {"--spacing", true}, {"--sheet-scale", false}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    auto selector = onlySelector(*parsed, "view");
    if (!selector) {
        return std::unexpected(malformed(selector.error().message));
    }
    auto view = resolveView(document, *selector);
    if (!view) {
        return std::unexpected(fromParse(view.error()));
    }
    drawing::ViewDefinition definition = drawing::findView(document, *view)->definition();
    if (parsed->has("--sheet-scale") && parsed->value("--scale")) {
        return std::unexpected(malformed("--scale and --sheet-scale contradict each other"));
    }
    if (parsed->has("--sheet-scale")) {
        // Absent means "the sheet's", which is intent and not a missing value.
        definition.scale.reset();
    }
    if (const auto scaleText = parsed->value("--scale")) {
        auto scale = parseScale(*scaleText);
        if (!scale) {
            return std::unexpected(fromParse("--scale", scale.error()));
        }
        definition.scale = *scale;
    }
    auto spacing = lengthOption(*parsed, "--spacing");
    if (!spacing) {
        return std::unexpected(fromParse("--spacing", spacing.error()));
    }
    if (*spacing) {
        definition.spacing = **spacing;
    }

    drawing::SetViewDefinitionCommand command{*view, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Edited {}", label(document, ObjectId{*view}));
}

EditResult applyViewRemove(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    auto selector = onlySelector(*parsed, "view");
    if (!selector) {
        return std::unexpected(malformed(selector.error().message));
    }
    auto view = resolveView(document, *selector);
    if (!view) {
        return std::unexpected(fromParse(view.error()));
    }
    const std::string named = label(document, ObjectId{*view});

    drawing::DeleteViewCommand command{*view};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Removed {}", named);
}

// --- dimensions ---------------------------------------------------------------------------------------

EditResult applyDimensionAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--name", true},
                                        {"--view", true},
                                        {"--type", true},
                                        {"--from", true},
                                        {"--to", true},
                                        {"--decimals", true},
                                        {"--x", true},
                                        {"--y", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (!parsed->positional().empty()) {
        return std::unexpected(malformed(std::format("unexpected argument '{}'", parsed->positional().front())));
    }
    const auto viewText = parsed->value("--view");
    if (!viewText) {
        return std::unexpected(malformed("--view is required: the view this dimension is drawn on"));
    }
    auto view = resolveView(document, *viewText);
    if (!view) {
        return std::unexpected(fromParse(view.error()));
    }
    const auto typeText = parsed->value("--type");
    if (!typeText) {
        return std::unexpected(malformed("--type is required: linear, horizontal, vertical, aligned, "
                                         "angular, radius, diameter or ordinate"));
    }
    auto type = parseDimensionType(*typeText);
    if (!type) {
        return std::unexpected(fromParse("--type", type.error()));
    }
    const auto fromText = parsed->value("--from");
    if (!fromText) {
        return std::unexpected(malformed("--from is required: what the dimension measures from"));
    }
    auto from = parseDimensionTarget(document, *fromText);
    if (!from) {
        return std::unexpected(fromParse("--from", from.error()));
    }

    drawing::DimensionDefinition definition{.view = *view, .type = *type, .from = *from};
    if (const auto toText = parsed->value("--to")) {
        auto to = parseDimensionTarget(document, *toText);
        if (!to) {
            return std::unexpected(fromParse("--to", to.error()));
        }
        definition.to = *to;
    }
    if (const auto decimalsText = parsed->value("--decimals")) {
        std::uint32_t decimals = 0;
        const auto* const end = decimalsText->data() + decimalsText->size();
        const auto [stop, code] = std::from_chars(decimalsText->data(), end, decimals);
        if (code != std::errc{} || stop != end || decimals > 6) {
            return std::unexpected(
                malformed(std::format("--decimals: '{}' is not a digit count from 0 to 6", *decimalsText)));
        }
        definition.format.decimals = static_cast<std::uint8_t>(decimals);
    }
    auto placement = placementOption(*parsed);
    if (!placement) {
        return std::unexpected(fromParse(placement.error()));
    }
    if (*placement) {
        definition.placement = **placement;
    }
    auto name = namedOr(*parsed, document, "Dimension");
    if (!name) {
        return std::unexpected(malformed(name.error().message));
    }

    drawing::CreateDimensionCommand command{*name, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    // What it MEASURES is not printed here: that is derived, it is
    // regeneration's to produce, and `bettercad-cli drawing` reports it.
    return std::format("Created {} on {}", label(document, ObjectId{command.dimensionId()}),
                       label(document, ObjectId{*view}));
}

EditResult applyDimensionSet(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--decimals", true}, {"--x", true}, {"--y", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    auto selector = onlySelector(*parsed, "dimension");
    if (!selector) {
        return std::unexpected(malformed(selector.error().message));
    }
    auto dimension = resolveDimension(document, *selector);
    if (!dimension) {
        return std::unexpected(fromParse(dimension.error()));
    }
    drawing::DimensionDefinition definition = drawing::findDimension(document, *dimension)->definition();
    if (const auto decimalsText = parsed->value("--decimals")) {
        std::uint32_t decimals = 0;
        const auto* const end = decimalsText->data() + decimalsText->size();
        const auto [stop, code] = std::from_chars(decimalsText->data(), end, decimals);
        if (code != std::errc{} || stop != end || decimals > 6) {
            return std::unexpected(
                malformed(std::format("--decimals: '{}' is not a digit count from 0 to 6", *decimalsText)));
        }
        definition.format.decimals = static_cast<std::uint8_t>(decimals);
    }
    auto placement = placementOption(*parsed);
    if (!placement) {
        return std::unexpected(fromParse(placement.error()));
    }
    if (*placement) {
        definition.placement = **placement;
    }

    drawing::SetDimensionDefinitionCommand command{*dimension, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Edited {}", label(document, ObjectId{*dimension}));
}

EditResult applyDimensionRemove(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    auto selector = onlySelector(*parsed, "dimension");
    if (!selector) {
        return std::unexpected(malformed(selector.error().message));
    }
    auto dimension = resolveDimension(document, *selector);
    if (!dimension) {
        return std::unexpected(fromParse(dimension.error()));
    }
    const std::string named = label(document, ObjectId{*dimension});

    drawing::DeleteDimensionCommand command{*dimension};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Removed {}", named);
}

// --- annotations, which is also where a BOM table and a balloon live ---------------------------------

EditResult applyAnnotationAdd(Document& document, Args args) {
    auto parsed = parseArguments(args, {{"--name", true},
                                        {"--view", true},
                                        {"--type", true},
                                        {"--text", true},
                                        {"--target", true},
                                        {"--height", true},
                                        {"--x", true},
                                        {"--y", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    if (!parsed->positional().empty()) {
        return std::unexpected(malformed(std::format("unexpected argument '{}'", parsed->positional().front())));
    }
    const auto viewText = parsed->value("--view");
    if (!viewText) {
        return std::unexpected(malformed("--view is required: the view this annotation is drawn on"));
    }
    auto view = resolveView(document, *viewText);
    if (!view) {
        return std::unexpected(fromParse(view.error()));
    }
    const auto typeText = parsed->value("--type");
    if (!typeText) {
        return std::unexpected(malformed("--type is required: note, leader, centreline, centremark, "
                                         "hole_callout, surface_finish, datum, feature_control_frame, "
                                         "bom_table or balloon"));
    }
    auto type = parseAnnotationType(*typeText);
    if (!type) {
        return std::unexpected(fromParse("--type", type.error()));
    }

    drawing::AnnotationDefinition definition{.view = *view, .type = *type};
    if (const auto targetText = parsed->value("--target")) {
        auto target = parseAnnotationTarget(document, *targetText);
        if (!target) {
            return std::unexpected(fromParse("--target", target.error()));
        }
        definition.target = *target;
    }
    if (const auto text = parsed->value("--text")) {
        definition.text = std::string{*text};
    }
    auto height = lengthOption(*parsed, "--height");
    if (!height) {
        return std::unexpected(fromParse("--height", height.error()));
    }
    if (*height) {
        definition.style.height = **height;
    }
    auto placement = placementOption(*parsed);
    if (!placement) {
        return std::unexpected(fromParse(placement.error()));
    }
    if (*placement) {
        definition.placement = **placement;
    }
    auto name = namedOr(*parsed, document, "Annotation");
    if (!name) {
        return std::unexpected(malformed(name.error().message));
    }

    drawing::CreateAnnotationCommand command{*name, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Created {} ({}) on {}", label(document, ObjectId{command.annotationId()}),
                       drawing::toString(*type), label(document, ObjectId{*view}));
}

EditResult applyAnnotationSet(Document& document, Args args) {
    auto parsed = parseArguments(
        args, {{"--text", true}, {"--target", true}, {"--height", true}, {"--x", true}, {"--y", true}});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    auto selector = onlySelector(*parsed, "annotation");
    if (!selector) {
        return std::unexpected(malformed(selector.error().message));
    }
    auto annotation = resolveAnnotation(document, *selector);
    if (!annotation) {
        return std::unexpected(fromParse(annotation.error()));
    }
    drawing::AnnotationDefinition definition = drawing::findAnnotation(document, *annotation)->definition();
    if (const auto targetText = parsed->value("--target")) {
        // Retargeting is an EXPLICIT edit and never something that happens on
        // its own: nothing in the drawing layer rebinds a reference, and this
        // is the only way one moves (P14-STREF-001).
        auto target = parseAnnotationTarget(document, *targetText);
        if (!target) {
            return std::unexpected(fromParse("--target", target.error()));
        }
        definition.target = *target;
    }
    if (const auto text = parsed->value("--text")) {
        definition.text = std::string{*text};
    }
    auto height = lengthOption(*parsed, "--height");
    if (!height) {
        return std::unexpected(fromParse("--height", height.error()));
    }
    if (*height) {
        definition.style.height = **height;
    }
    auto placement = placementOption(*parsed);
    if (!placement) {
        return std::unexpected(fromParse(placement.error()));
    }
    if (*placement) {
        definition.placement = **placement;
    }

    drawing::SetAnnotationDefinitionCommand command{*annotation, definition};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Edited {}", label(document, ObjectId{*annotation}));
}

EditResult applyAnnotationRemove(Document& document, Args args) {
    auto parsed = parseArguments(args, {});
    if (!parsed) {
        return std::unexpected(malformed(parsed.error().message));
    }
    auto selector = onlySelector(*parsed, "annotation");
    if (!selector) {
        return std::unexpected(malformed(selector.error().message));
    }
    auto annotation = resolveAnnotation(document, *selector);
    if (!annotation) {
        return std::unexpected(fromParse(annotation.error()));
    }
    const std::string named = label(document, ObjectId{*annotation});

    drawing::DeleteAnnotationCommand command{*annotation};
    if (auto failed = execute(document, command)) {
        return std::unexpected(*failed);
    }
    return std::format("Removed {}", named);
}

// --- the table --------------------------------------------------------------------------------------

constexpr std::array kDrawingEdits{
    EditCommand{"sheet-add",
                "sheet-add <file.bcad> [--name <name>] [--format <A0..A4>] [--orientation "
                "<landscape|portrait>] [--scale <paper:model>] [--margin <length>]",
                "Add a drawing sheet. Prints the ID it was given.", &applySheetAdd},
    EditCommand{"sheet-set",
                "sheet-set <file.bcad> <selector> [--format <A0..A4>] [--orientation "
                "<landscape|portrait>] [--scale <paper:model>] [--margin <length>]",
                "Edit a sheet. What is not given is left as it was.", &applySheetSet},
    EditCommand{"sheet-remove", "sheet-remove <file.bcad> <selector>",
                "Remove a sheet. Views on it are left naming it, and regeneration reports them.",
                &applySheetRemove},
    EditCommand{"view-add",
                "view-add <file.bcad> --sheet <selector> [--name <name>] "
                "[--source <selector> | --assembly | --parent <selector> --direction "
                "<top|bottom|left|right> --spacing <length>] [--orientation <front|...|bottom>] "
                "[--scale <paper:model>] [--x <length> --y <length>]",
                "Add a view: of one object, of the whole assembly, or projected from another view.",
                &applyViewAdd},
    EditCommand{"view-move", "view-move <file.bcad> <selector> --x <length> --y <length>",
                "Move a view on its sheet. A projected view derives its place from its parent and is "
                "refused.",
                &applyViewMove},
    EditCommand{"view-set",
                "view-set <file.bcad> <selector> [--scale <paper:model> | --sheet-scale] "
                "[--spacing <length>]",
                "Edit a view's scale or its spacing from its parent.", &applyViewSet},
    EditCommand{"view-remove", "view-remove <file.bcad> <selector>",
                "Remove a view. Refused while another view is projected from it.", &applyViewRemove},
    EditCommand{"dimension-add",
                "dimension-add <file.bcad> --view <selector> --type <kind> --from <target> "
                "[--to <target>] [--name <name>] [--decimals <0-6>] [--x <length> --y <length>]",
                "Add a dimension. A target is origin:, datum:, csys:, face: or cylinder:.",
                &applyDimensionAdd},
    EditCommand{"dimension-set",
                "dimension-set <file.bcad> <selector> [--decimals <0-6>] [--x <length> --y <length>]",
                "Edit a dimension's formatting or where its text sits.", &applyDimensionSet},
    EditCommand{"dimension-remove", "dimension-remove <file.bcad> <selector>", "Remove a dimension.",
                &applyDimensionRemove},
    EditCommand{"annotation-add",
                "annotation-add <file.bcad> --view <selector> --type <kind> [--name <name>] "
                "[--text <text>] [--target <target>] [--height <length>] [--x <length> --y <length>]",
                "Add an annotation -- a note, a hole callout, a BOM table, a balloon and the rest.",
                &applyAnnotationAdd},
    EditCommand{"annotation-set",
                "annotation-set <file.bcad> <selector> [--text <text>] [--target <target>] "
                "[--height <length>] [--x <length> --y <length>]",
                "Edit an annotation. Retargeting one is explicit and never automatic.",
                &applyAnnotationSet},
    EditCommand{"annotation-remove", "annotation-remove <file.bcad> <selector>", "Remove an annotation.",
                &applyAnnotationRemove},
};

} // namespace

std::span<const EditCommand> drawingEditCommands() noexcept { return kDrawingEdits; }

} // namespace bettercad::cli
