#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/drawing/Export.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The neutral description of what a drawing shows (ADR-016).
//
// ADR-016 decided that the drawing module produces a backend-neutral scene
// in sheet coordinates, from which any writer emits its format by
// transcription alone -- so that a PDF, SVG or DXF writer never has to work
// out what anything MEANS.
//
//     drawing intent -> model -> DRAWING SCENE -> writer
//        canonical     derived     derived        io, layer 5
//
// WHAT IS HERE, AND WHAT IS NOT. This is the part of that scene annotations
// need: lines, text, and the styles that tell a reader which is which. The
// whole scene ADR-016 describes -- the sheet frame, layers, hatch regions,
// line weights, the self-validating assembly of a finished sheet -- is
// P14-EXPORT-001's, and is deliberately not built here. These types are named
// for what they are, carry no writer's name, and are what that scene will
// carry when it is assembled.
//
// EVERYTHING HERE IS IN SHEET MILLIMETRES, already scaled and placed. A
// writer multiplies nothing. That is the whole point of the boundary, and it
// is also what keeps a 3.5 mm note 3.5 mm on paper whatever scale its view is
// drawn at.
namespace bettercad::drawing {

/// How a line is drawn, in the vocabulary ISO 128 uses for drawings rather
/// than a renderer's.
enum class LineStyle : std::uint8_t {
    /// Visible edges, symbol outlines, leaders.
    Continuous,
    /// Hidden detail.
    Dashed,
    /// Long-dash-dot: axes, centrelines and centre marks.
    Centre,
    /// A continuous line drawn thin: extension and dimension lines.
    Thin,
};

/// "continuous", "dashed", "centre", "thin".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(LineStyle style) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<LineStyle> lineStyleFromString(
    std::string_view text) noexcept;

/// Where a text run sits relative to the point it is placed at.
enum class TextAnchor : std::uint8_t {
    BaselineLeft,
    BaselineCentre,
    BaselineRight,
    MiddleLeft,
    MiddleCentre,
    MiddleRight,
};

/// "baseline_left", ... "middle_right".
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::string_view toString(TextAnchor anchor) noexcept;
[[nodiscard]] BETTERCAD_DRAWING_EXPORT std::optional<TextAnchor> textAnchorFromString(
    std::string_view text) noexcept;

/// A polyline in sheet millimetres.
///
/// Two points for a leader segment, five for a box, however many a centre
/// mark's arms need. Arcs are not here: nothing an annotation draws is
/// curved, and inventing a curve primitive nothing uses would be inventing
/// the export boundary's decisions for it.
struct SceneLine {
    std::vector<Point2D> points{};
    LineStyle style = LineStyle::Continuous;

    friend bool operator==(const SceneLine&, const SceneLine&) = default;
};

/// A run of text in sheet millimetres.
///
/// `height` is the CAP HEIGHT ON PAPER, in sheet millimetres, and it has
/// already been decided: no writer and no view scale may change it. A 3.5 mm
/// note is 3.5 mm on a 1:1 drawing and 3.5 mm on a 1:10 drawing, because the
/// scale is a property of the view's geometry and not of its lettering.
struct SceneText {
    Point2D at{};
    std::string text{};
    Length height{};
    /// Degrees anticlockwise from the sheet's X axis.
    Angle rotation{};
    TextAnchor anchor = TextAnchor::BaselineLeft;

    friend bool operator==(const SceneText&, const SceneText&) = default;
};

/// What one annotation draws.
///
/// Derived on every request and never stored (ADR-011). Ordered: the lines in
/// the order they were produced, then the text, so two runs of the same
/// intent give the same sequence and a comparison means something.
struct SceneItems {
    std::vector<SceneLine> lines{};
    std::vector<SceneText> texts{};

    /// No export macro: this is DEFINED here, so every translation unit
    /// compiles its own copy and there is nothing to import. Marking an
    /// inline definition for export makes it `dllimport` in a shared build,
    /// and a dllimport function may not have a definition -- which only the
    /// debug-shared preset finds.
    [[nodiscard]] bool isEmpty() const noexcept { return lines.empty() && texts.empty(); }
    /// Appends @p other's lines and text, keeping both orders.
    BETTERCAD_DRAWING_EXPORT void append(const SceneItems& other);
};

/// Finite coordinates, a positive text height, no empty polyline, no text
/// with nothing in it.
///
/// Checked in `drawing`, once, so that every writer inherits the guarantee
/// rather than re-deriving it or trusting (ADR-016).
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const SceneItems& items);

} // namespace bettercad::drawing
