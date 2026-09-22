#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/math/BoundingBox.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/drawing/Export.hpp>

#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// A drawing sheet (P14-SHEET-001, implementing ADR-010, ADR-011, ADR-013 and
// ADR-017).
//
// A sheet is a DocumentObject in the model's own document (ADR-010), so it
// gets identity, revisions, undo and the existing JSON envelope with no new
// machinery and no format version bump.
//
// What is stored here is INTENT: the format, which way up it is, the margins,
// the scale and the title-block text. What the sheet MEASURES -- its size, its
// border, the region a view may occupy -- is computed from that intent every
// time it is asked for, and none of it is written to the file (ADR-011).
// There is nothing to cache, invalidate or persist, so a sheet's geometry
// cannot be stale.
namespace bettercad::drawing {

/// A standard sheet size, plus Custom for one given explicitly.
///
/// The ISO 216 A series only. ANSI sizes are a later addition and are not
/// silently approximated here: an ANSI B sheet is not A3.
enum class SheetFormat : std::uint8_t {
    A0,
    A1,
    A2,
    A3,
    A4,
    Custom,
};

/// "A0" … "A4", "custom".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(SheetFormat format) noexcept;
/// The format @p text names, or nullopt. The inverse of toString().
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<SheetFormat> sheetFormatFromString(
    std::string_view text) noexcept;

/// Which way up the sheet is.
///
/// Portrait is the ISO 216 definition -- the short edge horizontal. Landscape
/// swaps the two, and swapping twice restores the original exactly, because
/// the stored value is the format and the orientation, never a width and a
/// height that could drift.
enum class SheetOrientation : std::uint8_t {
    Portrait,
    Landscape,
};

/// "portrait", "landscape".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(SheetOrientation orientation) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<SheetOrientation> sheetOrientationFromString(
    std::string_view text) noexcept;

/// The size of @p format in portrait, from ISO 216. Custom has no standard
/// size and returns nullopt.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<std::pair<Length, Length>> standardSize(
    SheetFormat format) noexcept;

/// Where a projected view is placed relative to the view it derives from
/// (ADR-018).
///
/// FirstAngle is ISO 128 -- used across Europe and Asia -- and places a Top
/// view BELOW its parent and a Right view to its LEFT. ThirdAngle is
/// ASME Y14.3 and places both on the opposite side. A drawing read in the
/// wrong convention is mirrored, not slightly wrong, which is why the
/// standard requires the convention to be shown on the sheet.
enum class ProjectionConvention : std::uint8_t {
    FirstAngle,
    ThirdAngle,
};

/// "first_angle", "third_angle".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(ProjectionConvention convention) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<ProjectionConvention> projectionConventionFromString(
    std::string_view text) noexcept;

/// A drawing scale, as an exact ratio of paper to model (ADR-013).
///
/// `1:2` is `{1, 2}` and halves; `2:1` is `{2, 1}` and doubles. The pair is
/// kept as written rather than reduced or turned into a double, because the
/// label is engineering intent -- a title block reads SCALE 1:2 -- and
/// because 1:3 has no exact double. `factor()` is the only place the division
/// happens.
struct DrawingScale {
    /// Millimetres on paper.
    std::uint32_t paper = 1;
    /// Millimetres of model they represent.
    std::uint32_t model = 1;

    /// paper / model. 0.5 for 1:2, 2.0 for 2:1.
    [[nodiscard]] double factor() const noexcept {
        return static_cast<double>(paper) / static_cast<double>(model);
    }
    /// "1:2", "2:1", "1:1".
    ///
    /// Inline, like every other member here: DrawingScale is a plain value
    /// type with no export macro, so an out-of-line definition is hidden
    /// under -fvisibility=hidden and does not link in a shared build. The
    /// debug-shared preset caught exactly that.
    [[nodiscard]] std::string label() const { return std::format("{}:{}", paper, model); }

    friend bool operator==(const DrawingScale&, const DrawingScale&) = default;
};

/// Both terms must be non-zero. They are unsigned, so a negative scale is not
/// representable rather than rejected at run time.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const DrawingScale& scale);

/// The unprinted border on each edge of the sheet.
///
/// Four independent lengths rather than one, because a drawing's binding edge
/// is conventionally wider than the other three.
struct SheetMargins {
    Length left{};
    Length right{};
    Length top{};
    Length bottom{};

    friend bool operator==(const SheetMargins&, const SheetMargins&) = default;
};

/// Every margin finite and not negative. Whether they fit the sheet cannot be
/// decided here -- that needs the sheet size -- and is checked by
/// validate(const SheetDefinition&).
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const SheetMargins& margins);

/// The semantic content of a title block: what it SAYS, never how it looks.
///
/// Every field is free text and optional. The block's lines, boxes and text
/// positions are derived (ADR-011) and belong to a later milestone; nothing
/// graphical is stored here.
///
/// Three things a title block shows are deliberately NOT fields, because they
/// are computed rather than chosen:
///
///     the scale text      the sheet's own DrawingScale::label()
///     the sheet number    its position among the document's sheets
///     the sheet count     how many there are
///
/// Storing any of them would let the file disagree with the document.
struct TitleBlock {
    std::string title{};
    std::string drawingNumber{};
    std::string revision{};
    std::string designer{};
    std::string checkedBy{};
    std::string approvedBy{};
    /// As text: a drawing date is what was issued, not a computed instant.
    std::string date{};
    std::string organization{};

    [[nodiscard]] bool empty() const noexcept {
        return title.empty() && drawingNumber.empty() && revision.empty() && designer.empty() &&
               checkedBy.empty() && approvedBy.empty() && date.empty() && organization.empty();
    }

    friend bool operator==(const TitleBlock&, const TitleBlock&) = default;
};

/// What a sheet is: the intent, and nothing derived.
struct SheetDefinition {
    SheetFormat format = SheetFormat::A3;
    SheetOrientation orientation = SheetOrientation::Landscape;
    /// Used only when `format` is Custom; ignored otherwise, and not written
    /// to the file for a standard format.
    Length customWidth{};
    Length customHeight{};
    SheetMargins margins{};
    DrawingScale scale{};
    /// Where projected views land relative to their parent (ADR-018).
    /// First angle by default, because the rest of the project is ISO.
    ProjectionConvention convention = ProjectionConvention::FirstAngle;
    TitleBlock titleBlock{};

    friend bool operator==(const SheetDefinition&, const SheetDefinition&) = default;
};

/// Checks a definition on its own: a known format, a custom size given and
/// positive when the format is Custom, valid margins that leave a usable
/// region, and a valid scale.
///
/// Fails with InvalidArgument, naming what is wrong. A definition that does
/// not validate is never stored, so a sheet in a document always has a usable
/// region.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const SheetDefinition& definition);

/// One sheet of a drawing (type name "sheet").
class BETTERCAD_DRAWING_EXPORT Sheet final : public DocumentObject {
public:
    using Definition = SheetDefinition;
    static constexpr std::string_view kTypeName = "sheet";

    [[nodiscard]] static Result<std::unique_ptr<Sheet>> create(std::string name,
                                                               const SheetDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    // dependencies() is the base's: a sheet depends on nothing. What it shows
    // is a view's business, and a view names its sheet rather than the other
    // way round (ADR-017), so the drawing sub-graph runs sheet <- view <-
    // dimension and cannot cycle.

    /// This sheet's ID, narrowed. Valid once the document owns it.
    [[nodiscard]] SheetId sheetId() const noexcept { return SheetId::fromValue(id().value()); }

    [[nodiscard]] const SheetDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition. Returns whether anything changed, so the
    /// document only bumps the revision on an effective change.
    Result<bool> setDefinition(const SheetDefinition& definition);

    // --- Derived geometry (ADR-011). Computed, never stored, never written.

    /// The sheet's width and height, with the orientation applied.
    [[nodiscard]] std::pair<Length, Length> size() const noexcept;
    /// The whole sheet, origin at its bottom-left corner, in millimetres.
    [[nodiscard]] BoundingBox2D bounds() const noexcept;
    /// The region inside the margins: where drawing content may go.
    [[nodiscard]] BoundingBox2D usableRegion() const noexcept;

private:
    Sheet(std::string name, const SheetDefinition& definition);

    SheetDefinition definition_;
};

} // namespace bettercad::drawing
