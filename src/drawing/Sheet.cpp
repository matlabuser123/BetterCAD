#include <bettercad/drawing/Sheet.hpp>

#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

/// ISO 216 A-series sizes, in portrait: short edge first.
///
/// These are the standard's own rounded millimetre values, which is what a
/// sheet is actually cut to and what a title block claims. They are written
/// as exact integers rather than computed from A0's area and the sqrt(2)
/// ratio, because the standard rounds at every step and recomputing would
/// disagree with it by up to half a millimetre.
struct StandardSize {
    SheetFormat format;
    double widthMm;
    double heightMm;
};

constexpr std::array kStandardSizes{
    StandardSize{SheetFormat::A0, 841.0, 1189.0},
    StandardSize{SheetFormat::A1, 594.0, 841.0},
    StandardSize{SheetFormat::A2, 420.0, 594.0},
    StandardSize{SheetFormat::A3, 297.0, 420.0},
    StandardSize{SheetFormat::A4, 210.0, 297.0},
};

[[nodiscard]] std::unexpected<Error> wrong(std::string message) {
    return makeError(ErrorCode::InvalidArgument, std::move(message));
}

[[nodiscard]] bool isFiniteAndNotNegative(Length value) noexcept {
    return std::isfinite(value.si()) && value.si() >= 0.0;
}

} // namespace

std::string_view toString(SheetFormat format) noexcept {
    switch (format) {
    case SheetFormat::A0:
        return "A0";
    case SheetFormat::A1:
        return "A1";
    case SheetFormat::A2:
        return "A2";
    case SheetFormat::A3:
        return "A3";
    case SheetFormat::A4:
        return "A4";
    case SheetFormat::Custom:
        return "custom";
    }
    return "unknown";
}

std::optional<SheetFormat> sheetFormatFromString(std::string_view text) noexcept {
    for (const SheetFormat format : {SheetFormat::A0, SheetFormat::A1, SheetFormat::A2, SheetFormat::A3,
                                     SheetFormat::A4, SheetFormat::Custom}) {
        if (toString(format) == text) {
            return format;
        }
    }
    return std::nullopt;
}

std::string_view toString(SheetOrientation orientation) noexcept {
    switch (orientation) {
    case SheetOrientation::Portrait:
        return "portrait";
    case SheetOrientation::Landscape:
        return "landscape";
    }
    return "unknown";
}

std::optional<SheetOrientation> sheetOrientationFromString(std::string_view text) noexcept {
    if (text == "portrait") {
        return SheetOrientation::Portrait;
    }
    if (text == "landscape") {
        return SheetOrientation::Landscape;
    }
    return std::nullopt;
}

std::optional<std::pair<Length, Length>> standardSize(SheetFormat format) noexcept {
    for (const StandardSize& size : kStandardSizes) {
        if (size.format == format) {
            return std::pair{size.widthMm * units::mm, size.heightMm * units::mm};
        }
    }
    return std::nullopt;
}

Result<void> validate(const DrawingScale& scale) {
    // Unsigned, so a negative scale cannot be expressed. Zero can, and a
    // sheet at 0:1 or 1:0 draws nothing or divides by nothing.
    if (scale.paper == 0 || scale.model == 0) {
        return wrong(std::format("a drawing scale has no zero term (got {}:{})", scale.paper, scale.model));
    }
    return {};
}

Result<void> validate(const SheetMargins& margins) {
    const std::array<std::pair<std::string_view, Length>, 4> edges{{
        {"left", margins.left},
        {"right", margins.right},
        {"top", margins.top},
        {"bottom", margins.bottom},
    }};
    for (const auto& [name, value] : edges) {
        if (!isFiniteAndNotNegative(value)) {
            return wrong(std::format("a sheet's {} margin must be finite and not negative", name));
        }
    }
    return {};
}

Result<void> validate(const SheetDefinition& definition) {
    if (toString(definition.format) == "unknown") {
        return wrong("a sheet must have a known format");
    }
    if (toString(definition.orientation) == "unknown") {
        return wrong("a sheet must have a known orientation");
    }
    if (auto valid = validate(definition.scale); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = validate(definition.margins); !valid) {
        return std::unexpected(valid.error());
    }

    Length width{};
    Length height{};
    if (definition.format == SheetFormat::Custom) {
        width = definition.customWidth;
        height = definition.customHeight;
        for (const auto& [name, value] : std::array<std::pair<std::string_view, Length>, 2>{
                 {{"width", width}, {"height", height}}}) {
            if (!std::isfinite(value.si()) || value.si() <= 0.0) {
                return wrong(std::format("a custom sheet's {} must be finite and greater than zero", name));
            }
        }
    } else {
        const auto standard = standardSize(definition.format);
        if (!standard) {
            return wrong(std::format("no standard size for format '{}'", toString(definition.format)));
        }
        width = standard->first;
        height = standard->second;
    }
    if (definition.orientation == SheetOrientation::Landscape) {
        std::swap(width, height);
    }

    // The margins must leave something to draw on. This is the check that
    // needs both halves of the definition, which is why it cannot live in
    // validate(const SheetMargins&).
    const Length horizontal = definition.margins.left + definition.margins.right;
    const Length vertical = definition.margins.top + definition.margins.bottom;
    if (horizontal.si() >= width.si()) {
        return wrong(std::format(
            "a sheet's left and right margins ({} mm) leave no usable width on a {} mm sheet",
            horizontal.in(units::mm), width.in(units::mm)));
    }
    if (vertical.si() >= height.si()) {
        return wrong(std::format(
            "a sheet's top and bottom margins ({} mm) leave no usable height on a {} mm sheet",
            vertical.in(units::mm), height.in(units::mm)));
    }
    return {};
}

Sheet::Sheet(std::string name, const SheetDefinition& definition)
    : DocumentObject(std::move(name)), definition_(definition) {}

Result<std::unique_ptr<Sheet>> Sheet::create(std::string name, const SheetDefinition& definition) {
    if (auto valid = validateObjectName(name); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    return std::unique_ptr<Sheet>(new Sheet(std::move(name), definition));
}

std::unique_ptr<DocumentObject> Sheet::clone() const {
    return std::unique_ptr<Sheet>(new Sheet(*this));
}

bool Sheet::contentEquals(const DocumentObject& other) const {
    const auto* sheet = dynamic_cast<const Sheet*>(&other);
    return sheet != nullptr && sheet->definition_ == definition_;
}

Result<bool> Sheet::setDefinition(const SheetDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return std::unexpected(valid.error());
    }
    if (definition == definition_) {
        return false;
    }
    definition_ = definition;
    return true;
}

std::pair<Length, Length> Sheet::size() const noexcept {
    Length width = definition_.customWidth;
    Length height = definition_.customHeight;
    // validate() refuses a format with no standard size and a Sheet cannot
    // hold a definition that did not validate, so the lookup always succeeds
    // today. It is still checked rather than dereferenced blind: a format
    // added to the enum without a size would otherwise be undefined
    // behaviour here rather than a zero-sized sheet a test would catch.
    if (const auto standard = standardSize(definition_.format)) {
        width = standard->first;
        height = standard->second;
    }
    if (definition_.orientation == SheetOrientation::Landscape) {
        std::swap(width, height);
    }
    return {width, height};
}

BoundingBox2D Sheet::bounds() const noexcept {
    const auto [width, height] = size();
    return BoundingBox2D{Point2D{}, Point2D{width, height}};
}

BoundingBox2D Sheet::usableRegion() const noexcept {
    const auto [width, height] = size();
    return BoundingBox2D{Point2D{definition_.margins.left, definition_.margins.bottom},
                         Point2D{width - definition_.margins.right, height - definition_.margins.top}};
}

} // namespace bettercad::drawing
