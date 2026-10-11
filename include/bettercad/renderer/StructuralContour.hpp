#pragma once

// Colour banding and the legend for one classified scalar field (P17-VIZ-001).
//
// NO QT AND NO OCCT. This is the arithmetic a contour display needs, kept
// where it can be tested headlessly, because the interesting failures here are
// numerical and not graphical: a band set that leaves a gap, a value that lands
// in two bands or in none, a uniform field divided by a zero range, and a
// partial field coloured as though it were whole.
//
// NO ENGINEERING QUANTITY IS COMPUTED HERE EITHER. A band index is a function
// of a value and the field's own range, and nothing in this file reads a
// displacement, a stress or a mesh.
//
// THE RANGE COMES FROM THE FIELD, NOT FROM THE BANDS. `ScalarField` already
// measured its own minimum and maximum over the entities it classified, so the
// legend restates that range rather than re-scanning the values and possibly
// disagreeing with the extremes the navigation uses.

#include <bettercad/core/Error.hpp>
#include <bettercad/renderer/Export.hpp>
#include <bettercad/renderer/StructuralResultView.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace bettercad::renderer {

/// The fewest and most bands a legend may have.
///
/// TWO IS THE FLOOR because a one-band legend states nothing a uniform
/// colour does not, and it would make `bandOf` constant and hide a mapping
/// defect. The ceiling is a presentation limit: beyond it the bands are
/// narrower than a screen can distinguish and the legend becomes a gradient
/// with a misleading number of labels.
constexpr std::size_t kMinimumContourBands = 2;
constexpr std::size_t kMaximumContourBands = 64;

/// How values map onto bands.
///
/// ONE VALUE, DELIBERATELY. A logarithmic scale is wrong for a signed channel
/// and this project does not ship an option it has not validated; the enum
/// exists so that adding one later is an additive change rather than a
/// reinterpretation of every stored preference.
enum class ContourScale : std::uint8_t {
    /// Equal-width bands over the field's own closed range.
    Linear,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ContourScale scale) noexcept;

/// Why a field cannot be banded.
enum class ContourProblem : std::uint8_t {
    /// The field is not complete, so there is nothing legitimate to colour.
    ///
    /// THE P16 TRAP, ONE LAYER FURTHER DOWN. `field()` already refuses a
    /// partial classification, so a `ScalarField` in hand is normally whole --
    /// but a caller can hold one it built itself, and colouring the entities
    /// that happened to classify, in a legend that claims the whole range, is
    /// the same defect wearing a palette.
    FieldIncomplete,
    /// The requested band count is outside `[kMinimumContourBands,
    /// kMaximumContourBands]`.
    BandCountUnusable,
    /// The legend was not built from the field it is being used with.
    ///
    /// ITS OWN CODE, not folded into `FieldIncomplete`, because the field is
    /// not incomplete and the causes are different: two solves of the same
    /// channel can have the same name and different ranges, so banding through
    /// the wrong legend is wrong by a SHIFT -- every value still lands in a
    /// band and the picture is plausible and false.
    LegendMismatch,
    /// A band index does not belong to the legend it was used with.
    BandOutOfRange,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ContourProblem problem) noexcept;

/// One colour band: a half-open value interval and the colour it draws in.
///
/// HALF-OPEN `[lower, upper)` FOR EVERY BAND BUT THE LAST, which is closed at
/// `upper`. That is what makes the bands a partition: every value in the
/// field's range belongs to exactly one, the maximum included, and no value
/// falls between two bands or into both.
struct ContourBand {
    double lower = 0.0;
    double upper = 0.0;
    /// sRGB in `[0, 1]`, the same space `Quantity_Color` is constructed in by
    /// the OCCT adapter, so the adapter converts nothing.
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    /// Whether `upper` is included. True for the last band only.
    bool closedAbove = false;

    /// Whether @p value belongs to this band.
    [[nodiscard]] constexpr bool contains(double value) const noexcept {
        return value >= lower && (closedAbove ? value <= upper : value < upper);
    }

    friend bool operator==(const ContourBand&, const ContourBand&) = default;
};

/// The legend for one field: its range, its unit and its bands.
struct ContourLegend {
    ResultChannel channel = ResultChannel::DisplacementMagnitude;
    ContourScale scale = ContourScale::Linear;
    /// The channel's SI unit, from `descriptorOf`. Restated rather than
    /// re-derived so a legend cannot disagree with the channel table.
    std::string_view unit{};

    /// THE FIELD'S OWN RANGE, copied from `ScalarField::minimum` and
    /// `maximum`. Not re-scanned: the navigation's extremes and the legend's
    /// endpoints must be the same two numbers.
    double minimum = 0.0;
    double maximum = 0.0;

    /// Whether this legend has exactly ONE band because the field has no
    /// drawable variation. The band count the caller asked for is then
    /// deliberately NOT honoured, and this flag says so.
    ///
    /// TWO CAUSES, AND NEITHER IS AN ERROR:
    ///
    /// - the field is uniform (`minimum == maximum`). A uniform stress state
    ///   is ordinary, and it cannot be banded because
    ///   `(value - min) / (max - min)` is `0/0`.
    /// - the range is too narrow to subdivide: `minimum + width == minimum`,
    ///   so every interior boundary collapses onto the minimum and bands 0 to
    ///   N-2 become the empty interval `[min, max)`. Those bands can never
    ///   contain a value, so printing N of them implies a variation that
    ///   cannot be drawn.
    ///
    /// In both cases the single band spans `[minimum, maximum]` closed, which
    /// for the second cause is a range of a few units in the last place rather
    /// than a single value -- so `uniform` does NOT imply
    /// `minimum == maximum`. Ask the field for that.
    ///
    /// The band is painted from the MIDDLE of the ramp: a field with no
    /// variation is simultaneously its own minimum and maximum, so neither end
    /// of the ramp is the honest colour.
    bool uniform = false;

    std::vector<ContourBand> bands{};

    /// The band @p value belongs to, or `bands.size()` when it belongs to
    /// none -- which can only happen for a value outside `[minimum, maximum]`,
    /// i.e. one that did not come from this field.
    ///
    /// A LINEAR SEARCH OVER THE PARTITION, not `(value - min) / width`
    /// rounded. The arithmetic form disagrees with the stored boundaries at a
    /// band edge by one unit in the last place, and then the colour a value
    /// draws in and the band the legend prints for it are different bands.
    [[nodiscard]] BETTERCAD_RENDERER_EXPORT std::size_t bandOf(double value) const noexcept;

    /// Whether the bands partition `[minimum, maximum]`: contiguous, in
    /// ascending order, the first starting at `minimum` and the last ending at
    /// `maximum`, with no gap and no overlap.
    ///
    /// Checked by the legend itself so a caller does not have to trust it.
    [[nodiscard]] BETTERCAD_RENDERER_EXPORT bool partitionsItsRange() const noexcept;
};

/// Builds the legend for @p field with @p bands equal-width bands.
///
/// Refuses an incomplete field and an unusable band count. A uniform field is
/// accepted and yields one band with `uniform == true`.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ContourLegend>
contourLegend(const ScalarField& field, std::size_t bands,
              ContourScale scale = ContourScale::Linear);

/// The band index of every value of @p field, parallel to `field.values`.
///
/// Refuses when @p legend was not built from @p field -- compared on the
/// channel and on both endpoints, because a legend from a different solve can
/// have the same channel and a different range, and colouring through it would
/// be silently wrong rather than visibly wrong.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<std::vector<std::size_t>>
contourBandsOf(const ScalarField& field, const ContourLegend& legend);

} // namespace bettercad::renderer
