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

/// How wide a line is drawn, ON PAPER, in sheet millimetres.
///
/// ISO 128 draws a technical drawing with two widths in a fixed 2:1 ratio --
/// a line group. `0.5` is the usual group for A3: visible edges and the
/// border at 0.5 mm, everything else at 0.25.
///
/// It is a LENGTH and not a class, because a writer must not decide what
/// "thick" means; and it never meets a view's scale, for the same reason a
/// 3.5 mm note does not.
namespace weights {
inline constexpr Length kThick = Length::fromSi(0.0005);  ///< 0.5 mm
inline constexpr Length kThin = Length::fromSi(0.00025);  ///< 0.25 mm
} // namespace weights

/// A polyline in sheet millimetres.
///
/// Two points for a leader segment, five for a box, however many a centre
/// mark's arms need.
struct SceneLine {
    std::vector<Point2D> points{};
    LineStyle style = LineStyle::Continuous;
    /// On paper, never scaled. Thin by default: an annotation's leaders and
    /// frames are thin, and the thick lines are the ones a view draws.
    Length width = weights::kThin;

    friend bool operator==(const SceneLine&, const SceneLine&) = default;
};

/// A circular arc in sheet millimetres, kept EXACT.
///
/// A projected circle arrives from hidden-line removal as both a polyline and
/// its exact curve (`DrawnEdge::curve`), and this is what carries the second
/// one to a writer that can use it: DXF has CIRCLE and ARC, SVG has `A`, and
/// PDF has Béziers that approximate a circle to far better than a plotter
/// resolves. Flattening every circle into short segments at the boundary
/// would throw that away for every format at once, which is precisely the
/// decision ADR-016 says the boundary must not make on a writer's behalf.
///
/// Angles are anticlockwise from the sheet's +X axis. `sweep` is signed and
/// may be a full turn, which is how a whole circle is written.
struct SceneArc {
    Point2D centre{};
    Length radius{};
    Angle start{};
    Angle sweep{};
    LineStyle style = LineStyle::Continuous;
    Length width = weights::kThin;

    friend bool operator==(const SceneArc&, const SceneArc&) = default;
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
    std::vector<SceneArc> arcs{};
    std::vector<SceneText> texts{};

    /// No export macro: this is DEFINED here, so every translation unit
    /// compiles its own copy and there is nothing to import. Marking an
    /// inline definition for export makes it `dllimport` in a shared build,
    /// and a dllimport function may not have a definition -- which only the
    /// debug-shared preset finds.
    [[nodiscard]] bool isEmpty() const noexcept {
        return lines.empty() && arcs.empty() && texts.empty();
    }
    /// Appends @p other's lines, arcs and text, keeping every order.
    BETTERCAD_DRAWING_EXPORT void append(const SceneItems& other);
};

/// Finite coordinates, a positive text height, no empty polyline, no text
/// with nothing in it, a positive radius and a positive line width.
///
/// Checked in `drawing`, once, so that every writer inherits the guarantee
/// rather than re-deriving it or trusting (ADR-016).
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const SceneItems& items);

/// ONE FINISHED SHEET, ready to be written out and nothing else (ADR-016).
///
/// This is the export boundary. A writer is handed one of these and needs
/// nothing else: no Document, no model, no solver, no resolver, no scale. If
/// a writer ever has to reach past it to work something out, the scene is
/// incomplete and the scene is what gets fixed.
///
/// THE COORDINATE SYSTEM, stated once so three writers cannot each decide:
///
///     millimetres, on the page
///     origin at the sheet's BOTTOM-LEFT corner
///     +X to the right, +Y UP
///     the page occupies [0, width] x [0, height]
///
/// +Y up is the drawing convention and the model's, and it is NOT what every
/// format uses: SVG's y grows downward, so its writer flips once, in one
/// place, and says so. PDF and DXF already agree with this.
struct DrawingScene {
    /// The page, on paper. Landscape A3 is 420 x 297.
    Length width{};
    Length height{};
    /// What is drawn, in the order it is drawn. Deterministic: sheets before
    /// views, views in ascending ID order, and within a view its edges in the
    /// order hidden-line removal canonicalised them.
    SceneItems items{};

    friend bool operator==(const DrawingScene&, const DrawingScene&) = default;
};

/// A positive page, and items that validate, and every item on the page.
///
/// "On the page" is a real check and not a nicety: a primitive at negative y
/// is a coordinate-system mistake -- almost always a forgotten flip -- and
/// catching it here is what stops three writers each producing a differently
/// wrong picture from it.
[[nodiscard]] BETTERCAD_DRAWING_EXPORT Result<void> validate(const DrawingScene& scene);

} // namespace bettercad::drawing
