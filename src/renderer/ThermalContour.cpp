// Colour banding, the legend and the contour cache for one classified thermal scalar field (P18-VIZ-001).
//
// THE BOUNDARIES ARE COMPUTED ONCE AND SHARED. Band `i` ends at exactly the same double that band `i + 1` begins
// at, because both read one entry of one array, so the partition has no gap and no overlap bitwise rather than to
// a tolerance. The endpoints are the displayed range's own numbers, not `minimum + n * width`, which rounds.

#include <bettercad/renderer/ThermalContour.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <span>

namespace bettercad::renderer {
namespace {

[[nodiscard]] constexpr ErrorCode codeFor(ThermalContourProblem problem) noexcept {
    switch (problem) {
    case ThermalContourProblem::BandCountUnusable:
    case ThermalContourProblem::RangeUnusable:
        return ErrorCode::InvalidArgument;
    // THIS LIBRARY'S OWN BUG: a value drawn from a field lies inside the field's range and the bands this file
    // built partition it, so reaching this means both checks passed and the partition is still wrong.
    case ThermalContourProblem::BandOutOfRange:
        return ErrorCode::Internal;
    case ThermalContourProblem::FieldIncomplete:
    case ThermalContourProblem::LegendMismatch:
        break;
    }
    return ErrorCode::FailedPrecondition;
}

[[nodiscard]] std::unexpected<Error> fail(ThermalContourProblem problem, std::string message) {
    return makeError(codeFor(problem), std::format("{}: {}", toString(problem), message));
}

/// The engineering ramp, blue through cyan, green and yellow to red, as five stops interpolated linearly in sRGB.
/// FIVE STOPS AND NOT A FORMULA, so the ramp is a table a reader can check against the legend.
[[nodiscard]] ThermalContourBand paint(ThermalContourBand band, double t) noexcept {
    constexpr std::size_t kStops = 5;
    constexpr double kRamp[kStops][3] = {
        {0.16, 0.26, 0.76}, // blue
        {0.17, 0.68, 0.80}, // cyan
        {0.25, 0.70, 0.30}, // green
        {0.95, 0.80, 0.20}, // yellow
        {0.85, 0.20, 0.16}, // red
    };
    const double clamped = std::clamp(t, 0.0, 1.0);
    const double scaled = clamped * static_cast<double>(kStops - 1);
    const auto lower = static_cast<std::size_t>(std::min(std::floor(scaled), static_cast<double>(kStops - 2)));
    const double local = scaled - static_cast<double>(lower);
    band.red = kRamp[lower][0] + local * (kRamp[lower + 1][0] - kRamp[lower][0]);
    band.green = kRamp[lower][1] + local * (kRamp[lower + 1][1] - kRamp[lower][1]);
    band.blue = kRamp[lower][2] + local * (kRamp[lower + 1][2] - kRamp[lower][2]);
    return band;
}

} // namespace

std::string_view toString(ThermalContourProblem problem) noexcept {
    switch (problem) {
    case ThermalContourProblem::FieldIncomplete:
        return "field_incomplete";
    case ThermalContourProblem::BandCountUnusable:
        return "band_count_unusable";
    case ThermalContourProblem::RangeUnusable:
        return "range_unusable";
    case ThermalContourProblem::LegendMismatch:
        return "legend_mismatch";
    case ThermalContourProblem::BandOutOfRange:
        return "band_out_of_range";
    }
    return "unknown";
}

std::string_view toString(ThermalRangeSource source) noexcept {
    switch (source) {
    case ThermalRangeSource::Auto:
        return "auto";
    case ThermalRangeSource::Manual:
        return "manual";
    }
    return "unknown";
}

std::size_t ThermalContourLegend::bandOf(double value) const noexcept {
    for (std::size_t i = 0U; i < bands.size(); ++i) {
        if (bands[i].contains(value)) {
            return i;
        }
    }
    // A MANUAL RANGE CLAMPS THE COLOUR. A value outside it takes the nearest end band; the value itself is
    // untouched, and an inspector still reports the number. A value that is not a number belongs to no band.
    if (rangeSource == ThermalRangeSource::Manual && !bands.empty() && std::isfinite(value)) {
        return value < bands.front().lower ? 0U : bands.size() - 1U;
    }
    return bands.size();
}

bool ThermalContourLegend::partitionsItsRange() const noexcept {
    if (bands.empty()) {
        return false;
    }
    if (bands.front().lower != minimum || bands.back().upper != maximum) {
        return false;
    }
    // EXACTLY ONE CLOSED BAND, and it is the last. Two closed bands would make their shared boundary belong to
    // both.
    for (std::size_t i = 0U; i < bands.size(); ++i) {
        const bool last = i + 1U == bands.size();
        if (bands[i].closedAbove != last) {
            return false;
        }
        if (!(bands[i].lower <= bands[i].upper)) {
            return false;
        }
        if (!last && bands[i].upper != bands[i + 1U].lower) {
            return false;
        }
    }
    return true;
}

Result<ThermalContourLegend> thermalContourLegend(const ThermalScalarField& field, std::size_t bands,
                                                  std::optional<ThermalDisplayRange> manual) {
    // THE P16 TRAP, FIRST. A partial classification coloured through a legend that claims the whole range
    // presents the entities that happened to classify as if they were the result.
    if (!field.complete()) {
        return fail(ThermalContourProblem::FieldIncomplete,
                    std::format("channel {} classified {} of {} entities, so it cannot be banded",
                                toString(field.channel), field.classified, field.expected));
    }
    if (bands < kMinimumThermalContourBands || bands > kMaximumThermalContourBands) {
        return fail(ThermalContourProblem::BandCountUnusable,
                    std::format("{} bands requested; the usable range is {} to {}", bands,
                                kMinimumThermalContourBands, kMaximumThermalContourBands));
    }
    if (manual.has_value() &&
        (!std::isfinite(manual->minimum) || !std::isfinite(manual->maximum) || manual->minimum > manual->maximum)) {
        return fail(ThermalContourProblem::RangeUnusable,
                    std::format("the display range [{}, {}] is not a finite, ordered interval", manual->minimum,
                                manual->maximum));
    }

    ThermalContourLegend out;
    out.channel = field.channel;
    const ThermalChannelDescriptor descriptor = descriptorOf(field.channel);
    out.name = descriptor.name;
    out.unit = descriptor.unit;
    out.rangeSource = manual.has_value() ? ThermalRangeSource::Manual : ThermalRangeSource::Auto;
    out.fieldMinimum = field.minimum;
    out.fieldMaximum = field.maximum;
    out.minimum = manual.has_value() ? manual->minimum : field.minimum;
    out.maximum = manual.has_value() ? manual->maximum : field.maximum;

    // A RANGE WITH NO DRAWABLE VARIATION IS ONE BAND, NOT N IDENTICAL ONES. `(v - min)/(max - min)` is 0/0 for a
    // uniform range, and a range too narrow to subdivide (`minimum + width == minimum`) would print N bands of
    // which only the last can ever hold a value. One band says what is true, painted from the MIDDLE of the ramp.
    const double span = out.maximum - out.minimum;
    const double width = span / static_cast<double>(bands);
    if (span == 0.0 || out.minimum + width == out.minimum) {
        out.uniform = true;
        ThermalContourBand only;
        only.lower = out.minimum;
        only.upper = out.maximum;
        only.closedAbove = true;
        out.bands.push_back(paint(only, 0.5));
        return out;
    }

    // ONE ARRAY OF BOUNDARIES, SHARED BY ADJACENT BANDS. The two ends are the range's own numbers, unrounded.
    std::vector<double> boundary(bands + 1U);
    boundary.front() = out.minimum;
    boundary.back() = out.maximum;
    for (std::size_t i = 1U; i < bands; ++i) {
        // Clamped so a denormal width cannot push a boundary outside the range, and monotonic against its
        // predecessor so the partition cannot invert.
        const double computed = out.minimum + (static_cast<double>(i) * width);
        boundary[i] = std::clamp(computed, boundary[i - 1U], out.maximum);
    }
    out.bands.reserve(bands);
    for (std::size_t i = 0U; i < bands; ++i) {
        ThermalContourBand band;
        band.lower = boundary[i];
        band.upper = boundary[i + 1U];
        band.closedAbove = (i + 1U == bands);
        // The band's own midpoint fraction, so the first band is not pure blue and the last not pure red.
        out.bands.push_back(paint(band, (static_cast<double>(i) + 0.5) / static_cast<double>(bands)));
    }
    return out;
}

std::string thermalLegendTitle(const ThermalContourLegend& legend, std::optional<double> timeSeconds) {
    std::string title = std::format("{} [{}]", legend.name, legend.unit);
    if (timeSeconds.has_value()) {
        title += std::format(" at t = {} s", *timeSeconds);
    }
    return title;
}

Result<std::vector<std::size_t>> thermalContourBandsOf(const ThermalScalarField& field,
                                                       const ThermalContourLegend& legend) {
    if (!field.complete()) {
        return fail(ThermalContourProblem::FieldIncomplete,
                    std::format("channel {} classified {} of {} entities", toString(field.channel),
                                field.classified, field.expected));
    }
    // THE LEGEND MUST BE THIS FIELD'S: the channel and both endpoints of the FIELD. Two solves of one channel can
    // share a name and differ in range, and banding through the wrong legend is wrong by a shift -- plausible
    // and false.
    if (legend.channel != field.channel || legend.fieldMinimum != field.minimum || legend.fieldMaximum != field.maximum) {
        return fail(ThermalContourProblem::LegendMismatch,
                    std::format("the legend is for channel {} over a field range [{}, {}] and the field is "
                                "channel {} over [{}, {}]",
                                toString(legend.channel), legend.fieldMinimum, legend.fieldMaximum,
                                toString(field.channel), field.minimum, field.maximum));
    }
    if (!legend.partitionsItsRange()) {
        return fail(ThermalContourProblem::BandOutOfRange, "the legend's bands do not partition its own range");
    }
    std::vector<std::size_t> out;
    out.reserve(field.values.size());
    for (const double value : field.values) {
        const std::size_t band = legend.bandOf(value);
        // UNREACHABLE, AND CHECKED: refused rather than clamped into the nearest band, which would colour a
        // defect as data.
        if (band >= legend.bands.size()) {
            return fail(ThermalContourProblem::BandOutOfRange,
                        std::format("value {} of channel {} falls in no band of [{}, {}]", value,
                                    toString(field.channel), legend.minimum, legend.maximum));
        }
        out.push_back(band);
    }
    return out;
}

namespace {

/// The index of @p entity in @p sorted, or `sorted.size()` when absent: a binary search of a documented-ascending
/// array, which the caller has checked.
template <typename Id>
[[nodiscard]] std::size_t indexOf(std::span<const Id> sorted, Id entity) {
    const auto at = std::ranges::lower_bound(sorted, entity);
    if (at == sorted.end() || *at != entity) {
        return sorted.size();
    }
    return static_cast<std::size_t>(std::distance(sorted.begin(), at));
}

} // namespace

Result<std::vector<std::size_t>> thermalTriangleCornerBands(const MeshView& view, const ThermalScalarField& field,
                                                            std::span<const std::size_t> entityBands) {
    if (!field.complete()) {
        return fail(ThermalContourProblem::FieldIncomplete,
                    std::format("channel {} classified {} of {} entities, so its bands cannot be mapped onto render "
                                "triangles",
                                toString(field.channel), field.classified, field.expected));
    }
    if (entityBands.size() != field.values.size()) {
        return fail(ThermalContourProblem::LegendMismatch,
                    std::format("the field has {} values and {} band indices were given", field.values.size(),
                                entityBands.size()));
    }
    if (view.triangleCount() == 0U) {
        return fail(ThermalContourProblem::FieldIncomplete,
                    "this mesh view has no triangles, so there is nothing to colour");
    }
    const bool nodal = field.location == thermal::FieldLocation::Node;
    // AN ELEMENT CHANNEL NEEDS A VIEW WHOSE FACETS NAME TETRAHEDRA. The boundary view of the Triangle3 elements
    // names triangles, which no element value belongs to; refused here by name rather than discovered as "no
    // value for element N" on the first triangle.
    if (!nodal && view.source() != MeshSource::VolumeBoundaryByTetrahedron &&
        view.source() != MeshSource::SelectedTetrahedra) {
        return fail(ThermalContourProblem::FieldIncomplete,
                    std::format("channel {} is an element channel and the view's facets name {} elements, not "
                                "tetrahedra",
                                toString(field.channel), toString(view.source())));
    }
    const std::span<const meshing::NodeId> nodes{field.nodes};
    const std::span<const meshing::ElementId> elements{field.elements};
    // THE DOCUMENTED ORDER, CHECKED: `indexOf` is a binary search, so an unsorted array would return a wrong
    // index rather than an error.
    if (nodal ? !std::ranges::is_sorted(nodes) : !std::ranges::is_sorted(elements)) {
        return fail(ThermalContourProblem::FieldIncomplete,
                    std::format("channel {}'s identity array is not in ascending order, which its own contract "
                                "requires",
                                toString(field.channel)));
    }

    const std::span<const std::uint32_t> indices = view.triangleIndices();
    std::vector<std::size_t> out;
    out.reserve(view.triangleCount() * 3U);
    for (std::size_t triangle = 0U; triangle < view.triangleCount(); ++triangle) {
        if (!nodal) {
            // ALL THREE CORNERS SHARE THE ELEMENT'S BAND, so the facet draws flat: an element-constant value IS
            // constant over the element, and interpolating it would invent a variation.
            const Result<meshing::ElementId> owner = view.elementOfTriangle(triangle);
            if (!owner.has_value()) {
                return fail(ThermalContourProblem::BandOutOfRange,
                            std::format("render triangle {} names no element", triangle));
            }
            const std::size_t at = indexOf(elements, *owner);
            if (at >= elements.size()) {
                return fail(ThermalContourProblem::BandOutOfRange,
                            std::format("element {} owns render triangle {} and channel {} has no value for it",
                                        owner->value(), triangle, toString(field.channel)));
            }
            out.push_back(entityBands[at]);
            out.push_back(entityBands[at]);
            out.push_back(entityBands[at]);
            continue;
        }
        // EACH CORNER TAKES ITS OWN NODE'S BAND; the interpolation across the facet is the element's linear shape
        // function, which is where a nodal field is defined -- not an average of anything.
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const std::size_t vertex = indices[(triangle * 3U) + corner];
            const Result<meshing::NodeId> node = view.nodeOfVertex(vertex);
            if (!node.has_value()) {
                return fail(ThermalContourProblem::BandOutOfRange,
                            std::format("render vertex {} names no node", vertex));
            }
            const std::size_t at = indexOf(nodes, *node);
            if (at >= nodes.size()) {
                return fail(ThermalContourProblem::BandOutOfRange,
                            std::format("node {} is a corner of render triangle {} and channel {} has no value "
                                        "for it",
                                        node->value(), triangle, toString(field.channel)));
            }
            out.push_back(entityBands[at]);
        }
    }
    return out;
}

Result<std::vector<double>> thermalContourCornerColours(const MeshView& view, const ThermalScalarField& field,
                                                        const ThermalContourLegend& legend) {
    // THE ORDER IS THE WHOLE VALUE OF THIS FUNCTION: band the field through the legend FIRST, so the
    // legend-mismatch and partition checks run before anything is mapped onto geometry.
    const Result<std::vector<std::size_t>> bands = thermalContourBandsOf(field, legend);
    if (!bands.has_value()) {
        return std::unexpected(bands.error());
    }
    const Result<std::vector<std::size_t>> corners = thermalTriangleCornerBands(view, field, *bands);
    if (!corners.has_value()) {
        return std::unexpected(corners.error());
    }
    std::vector<double> out;
    out.reserve(corners->size() * 3U);
    for (const std::size_t band : *corners) {
        if (band >= legend.bands.size()) {
            // UNREACHABLE, AND CHECKED: reading past the palette is the one mistake here that would not look
            // like a mistake.
            return fail(ThermalContourProblem::BandOutOfRange,
                        std::format("band {} is outside a legend of {} bands", band, legend.bands.size()));
        }
        out.push_back(legend.bands[band].red);
        out.push_back(legend.bands[band].green);
        out.push_back(legend.bands[band].blue);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The cache
// ---------------------------------------------------------------------------

namespace {

/// FNV-1a over the triangle index list: deterministic, order-sensitive, and cheap next to rebuilding a buffer.
[[nodiscard]] std::uint64_t fingerprintOf(std::span<const std::uint32_t> indices) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint32_t index : indices) {
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            hash ^= static_cast<std::uint64_t>((index >> shift) & 0xFFU);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

} // namespace

Result<const std::vector<double>*> ThermalContourCache::colours(
    const meshing::Mesh& mesh, const MeshView& view, const thermal::ThermalDofMap& map,
    const thermal::ThermalPostResult& post, std::size_t frame, ThermalChannel channel, std::size_t bands,
    std::optional<ThermalDisplayRange> range) {
    ThermalContourKey wanted;
    wanted.source = post.source();
    wanted.mesh = mesh.stamp();
    wanted.frame = frame;
    wanted.channel = channel;
    wanted.bands = bands;
    wanted.range = range;
    wanted.view = view.source();
    wanted.triangles = view.triangleCount();
    wanted.vertices = view.vertexCount();
    wanted.viewFingerprint = fingerprintOf(view.triangleIndices());

    if (key_.has_value() && *key_ == wanted && view.describes(mesh)) {
        ++hits_;
        return &colours_;
    }
    // A MISS REBUILDS FROM THE SOURCE, and a failed rebuild leaves nothing behind: the held buffer belonged to
    // another key and must not survive as the answer for this one.
    clear();
    const Result<ThermalResultView> result = ThermalResultView::of(mesh, view, map, post);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    const Result<ThermalScalarField> field = result->field(mesh, map, post, frame, channel);
    if (!field.has_value()) {
        return std::unexpected(field.error());
    }
    const Result<ThermalContourLegend> legend = thermalContourLegend(*field, bands, range);
    if (!legend.has_value()) {
        return std::unexpected(legend.error());
    }
    Result<std::vector<double>> built = thermalContourCornerColours(view, *field, *legend);
    if (!built.has_value()) {
        return std::unexpected(built.error());
    }
    colours_ = std::move(*built);
    key_ = std::move(wanted);
    ++builds_;
    return &colours_;
}

void ThermalContourCache::clear() noexcept {
    key_.reset();
    colours_.clear();
}

} // namespace bettercad::renderer
