#pragma once

// Colour banding, the legend and the contour cache for one classified thermal scalar field (P18-VIZ-001).
//
// NO Qt AND NO OCCT. This is the arithmetic a contour display needs, kept where it can be tested headlessly,
// because the interesting failures here are numerical and not graphical: a band set that leaves a gap, a value
// that lands in two bands or in none, a uniform field divided by a zero range, a manual range that clamps the
// COLOUR and silently alters the VALUE, and a partial field coloured as though it were whole.
//
// NO ENGINEERING QUANTITY IS COMPUTED HERE. A band index is a function of a value and a range, and nothing in
// this file reads a temperature, a gradient, a flux or a mesh quantity beyond what the field already holds.
//
// A DISPLAY RANGE IS VIEW STATE. Auto takes the field's own minimum and maximum over the classified entities (the
// source values, never rendered pixels). Manual is whatever the user typed; a value outside it takes the colour
// of the nearest end -- the COLOUR clamps, the value does not, and an inspector still returns the actual number.

#include <bettercad/core/Error.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/renderer/Export.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/renderer/ThermalResultView.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalPost.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::renderer {

/// The fewest and most bands a legend may have. Two is the floor because a one-band legend states nothing a
/// uniform colour does not; the ceiling is a presentation limit.
constexpr std::size_t kMinimumThermalContourBands = 2;
constexpr std::size_t kMaximumThermalContourBands = 64;

/// Why a field cannot be banded.
enum class ThermalContourProblem : std::uint8_t {
    /// The field is not complete, so there is nothing legitimate to colour.
    FieldIncomplete,
    /// The requested band count is outside `[kMinimumThermalContourBands, kMaximumThermalContourBands]`.
    BandCountUnusable,
    /// A manual display range is not finite or is inverted.
    RangeUnusable,
    /// The legend was not built from the field it is being used with.
    LegendMismatch,
    /// A band index does not belong to the legend it was used with.
    BandOutOfRange,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ThermalContourProblem problem) noexcept;

/// One colour band: a half-open value interval `[lower, upper)`, closed at `upper` for the last band only, so the
/// bands partition the displayed range (every value belongs to exactly one, the maximum included).
struct ThermalContourBand {
    double lower = 0.0;
    double upper = 0.0;
    /// sRGB in `[0, 1]`.
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    bool closedAbove = false;

    [[nodiscard]] constexpr bool contains(double value) const noexcept {
        return value >= lower && (closedAbove ? value <= upper : value < upper);
    }

    friend bool operator==(const ThermalContourBand&, const ThermalContourBand&) = default;
};

/// A manual display range. View state only: it never touches a field.
struct ThermalDisplayRange {
    double minimum = 0.0;
    double maximum = 0.0;

    friend bool operator==(const ThermalDisplayRange&, const ThermalDisplayRange&) = default;
};

enum class ThermalRangeSource : std::uint8_t {
    /// The field's own range over its classified entities.
    Auto,
    /// A range the user chose.
    Manual,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ThermalRangeSource source) noexcept;

/// The legend for one field: its channel, unit, displayed range and bands.
struct ThermalContourLegend {
    ThermalChannel channel = ThermalChannel::Temperature;
    /// The channel's name and SI unit, from `descriptorOf`, restated so a legend cannot disagree with the table.
    std::string_view name{};
    std::string_view unit{};
    ThermalRangeSource rangeSource = ThermalRangeSource::Auto;

    /// THE FIELD'S OWN RANGE, copied from the field. The navigation's extremes and the legend's endpoints (auto)
    /// are the same two numbers.
    double fieldMinimum = 0.0;
    double fieldMaximum = 0.0;
    /// The DISPLAYED range: the field's for Auto, the user's for Manual.
    double minimum = 0.0;
    double maximum = 0.0;

    /// Whether the legend has exactly ONE band because the displayed range has no drawable variation (a uniform
    /// field, or a range too narrow to subdivide). Neither is an error; the band is painted from the middle of
    /// the ramp.
    bool uniform = false;

    std::vector<ThermalContourBand> bands{};

    /// The band @p value belongs to. For an Auto legend a value outside the range belongs to none
    /// (`bands.size()`); for a Manual one it takes the nearest end band -- the colour clamps, the value does not.
    /// A linear search over the partition, never `(v - min) / width` rounded.
    [[nodiscard]] BETTERCAD_RENDERER_EXPORT std::size_t bandOf(double value) const noexcept;

    /// Whether the bands partition `[minimum, maximum]` with no gap and no overlap.
    [[nodiscard]] BETTERCAD_RENDERER_EXPORT bool partitionsItsRange() const noexcept;
};

/// Builds the legend for @p field with @p bands equal-width bands over its own range (@p manual empty) or over
/// @p manual.
///
/// Refuses an incomplete field, an unusable band count and an unusable manual range.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ThermalContourLegend>
thermalContourLegend(const ThermalScalarField& field, std::size_t bands,
                     std::optional<ThermalDisplayRange> manual = std::nullopt);

/// The text a legend prints: `"Temperature [K]"`, and with a time `"Temperature [K] at t = 0.6 s"`. The time is the
/// STORED time of the displayed state, never a frame index times a step.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string thermalLegendTitle(const ThermalContourLegend& legend,
                                                                      std::optional<double> timeSeconds);

/// The band index of every value of @p field, parallel to `field.values`. Refuses when @p legend was not built
/// from @p field -- compared on the channel and on the FIELD's endpoints.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<std::vector<std::size_t>>
thermalContourBandsOf(const ThermalScalarField& field, const ThermalContourLegend& legend);

/// The band of every CORNER of every render triangle of @p view: `3 * view.triangleCount()` entries.
///
/// PER CORNER, so one mechanism serves both locations honestly: an ELEMENT channel gives all three corners of a
/// facet the band of the tetrahedron that owns it (the facet is flat, which is the truth about element-constant
/// values, not a smoothing of them), and the NODAL channel gives each corner its own node's band (the facet
/// interpolates by its shape function, where a nodal field is defined). An element channel needs a view whose
/// facets name Tet4 owners (`MeshSource::VolumeBoundaryByTetrahedron`) and is refused on any other.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<std::vector<std::size_t>>
thermalTriangleCornerBands(const MeshView& view, const ThermalScalarField& field,
                           std::span<const std::size_t> entityBands);

/// The sRGB triple of every corner of every render triangle: `9 * view.triangleCount()` doubles. The whole
/// composition in one call: it bands the field through the legend, maps the bands onto corners and reads each
/// band's colour. This is what `Viewer::setMeshContour` takes.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<std::vector<double>>
thermalContourCornerColours(const MeshView& view, const ThermalScalarField& field, const ThermalContourLegend& legend);

// ---------------------------------------------------------------------------
// The cache
// ---------------------------------------------------------------------------

/// Everything a contour buffer is a function of. A cache entry is valid exactly while the key it was built under
/// still equals the key asked for -- the result's SOURCE (by value), the mesh generation, the displayed frame, the
/// channel and the two display settings. It is never keyed on a frame number, a node count or a field name alone.
struct ThermalContourKey {
    thermal::ThermalPostSource source{};
    meshing::MeshStamp mesh{};
    std::size_t frame = 0;
    ThermalChannel channel = ThermalChannel::Temperature;
    std::size_t bands = 0;
    std::optional<ThermalDisplayRange> range{};
    /// THE VIEW THE BUFFER WAS BUILT FOR. A colour buffer is parallel to a view's triangles, so two views of one mesh
    /// (the boundary, the boundary by tetrahedron, a selection of tetrahedra) are two keys: the kind, the sizes and a
    /// fingerprint of the triangle index list, which differs for two selections of equal size.
    MeshSource view = MeshSource::VolumeBoundary;
    std::size_t triangles = 0;
    std::size_t vertices = 0;
    std::uint64_t viewFingerprint = 0;

    friend bool operator==(const ThermalContourKey&, const ThermalContourKey&) = default;
};

/// A display-buffer cache: DERIVED, NEVER AUTHORITY. It can make a redraw cheaper; it cannot make a result current.
class BETTERCAD_RENDERER_EXPORT ThermalContourCache {
public:
    /// The corner colours for @p frame of @p post, rebuilt unless the cache already holds exactly this key.
    /// The pointer stays valid until the next call. Refuses a result that does not describe the mesh.
    [[nodiscard]] Result<const std::vector<double>*>
    colours(const meshing::Mesh& mesh, const MeshView& view, const thermal::ThermalDofMap& map,
            const thermal::ThermalPostResult& post, std::size_t frame, ThermalChannel channel, std::size_t bands,
            std::optional<ThermalDisplayRange> range = std::nullopt);

    /// How many buffers were built, and how many calls were answered from the held one.
    [[nodiscard]] std::size_t builds() const noexcept { return builds_; }
    [[nodiscard]] std::size_t hits() const noexcept { return hits_; }
    /// The key of the held buffer, or null when empty.
    [[nodiscard]] const ThermalContourKey* key() const noexcept { return key_.has_value() ? &*key_ : nullptr; }
    void clear() noexcept;

private:
    std::optional<ThermalContourKey> key_{};
    std::vector<double> colours_{};
    std::size_t builds_ = 0;
    std::size_t hits_ = 0;
};

} // namespace bettercad::renderer
