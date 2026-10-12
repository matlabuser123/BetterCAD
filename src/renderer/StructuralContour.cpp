// Colour banding and the legend for one classified scalar field (P17-VIZ-001).
//
// THE BOUNDARIES ARE COMPUTED ONCE AND SHARED. Band `i` ends at exactly the
// same double that band `i + 1` begins at, because both read one entry of one
// array -- so the partition has no gap and no overlap BITWISE rather than to a
// tolerance. Two independent expressions for the same boundary disagree by a
// unit in the last place, and then one value colours as band `i` while the
// legend prints it as band `i + 1`.
//
// AND THE ENDPOINTS ARE THE FIELD'S OWN. The first band's lower bound is
// `field.minimum` itself and the last band's upper bound is `field.maximum`
// itself, not `minimum + n * width`, which rounds.

#include <bettercad/renderer/StructuralContour.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <ranges>
#include <span>

namespace bettercad::renderer {
namespace {

/// A written-out switch, so a ContourProblem added to the enum and forgotten
/// here is visible at the point of the omission.
[[nodiscard]] constexpr ErrorCode codeFor(ContourProblem problem) noexcept {
    switch (problem) {
    case ContourProblem::BandCountUnusable:
        return ErrorCode::InvalidArgument;
    // THIS LIBRARY'S OWN BUG rather than the caller's: a value drawn from a
    // field cannot fall outside that field's own range, and a legend this file
    // built always partitions its range -- so reaching it means both checks
    // above passed and the partition is still wrong. No argument the caller
    // could pass would fix it.
    case ContourProblem::BandOutOfRange:
        return ErrorCode::Internal;
    case ContourProblem::FieldIncomplete:
    case ContourProblem::LegendMismatch:
        break;
    }
    return ErrorCode::FailedPrecondition;
}

[[nodiscard]] std::unexpected<Error> fail(ContourProblem problem, std::string message) {
    return makeError(codeFor(problem), std::format("{}: {}", toString(problem), message));
}

/// The engineering ramp, blue through cyan, green and yellow to red, as five
/// stops interpolated linearly in sRGB.
///
/// FIVE STOPS AND NOT A FORMULA, so the ramp is a table a reader can check
/// against the legend they are looking at. @p t is clamped, which matters only
/// for the uniform case below.
[[nodiscard]] ContourBand paint(ContourBand band, double t) noexcept {
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
    // The last stop must not index past the table when `clamped` is exactly 1.
    const auto lower = static_cast<std::size_t>(
        std::min(std::floor(scaled), static_cast<double>(kStops - 2)));
    const double local = scaled - static_cast<double>(lower);
    band.red = kRamp[lower][0] + local * (kRamp[lower + 1][0] - kRamp[lower][0]);
    band.green = kRamp[lower][1] + local * (kRamp[lower + 1][1] - kRamp[lower][1]);
    band.blue = kRamp[lower][2] + local * (kRamp[lower + 1][2] - kRamp[lower][2]);
    return band;
}

} // namespace

std::string_view toString(ContourScale scale) noexcept {
    switch (scale) {
    case ContourScale::Linear:
        return "linear";
    }
    return "unknown";
}

std::string_view toString(ContourProblem problem) noexcept {
    switch (problem) {
    case ContourProblem::FieldIncomplete:
        return "field_incomplete";
    case ContourProblem::BandCountUnusable:
        return "band_count_unusable";
    case ContourProblem::LegendMismatch:
        return "legend_mismatch";
    case ContourProblem::BandOutOfRange:
        return "band_out_of_range";
    }
    return "unknown";
}

std::size_t ContourLegend::bandOf(double value) const noexcept {
    for (std::size_t i = 0; i < bands.size(); ++i) {
        if (bands[i].contains(value)) {
            return i;
        }
    }
    return bands.size();
}

bool ContourLegend::partitionsItsRange() const noexcept {
    if (bands.empty()) {
        return false;
    }
    if (bands.front().lower != minimum || bands.back().upper != maximum) {
        return false;
    }
    // EXACTLY ONE CLOSED BAND, and it is the last. Two closed bands would make
    // their shared boundary belong to both.
    for (std::size_t i = 0; i < bands.size(); ++i) {
        const bool last = i + 1 == bands.size();
        if (bands[i].closedAbove != last) {
            return false;
        }
        if (!(bands[i].lower <= bands[i].upper)) {
            return false;
        }
        if (!last && bands[i].upper != bands[i + 1].lower) {
            return false;
        }
    }
    return true;
}

Result<ContourLegend> contourLegend(const ScalarField& field, std::size_t bands,
                                    ContourScale scale) {
    // THE P16 TRAP, FIRST AGAIN. A partial classification coloured through a
    // legend that claims the whole range presents the entities that happened
    // to classify as if they were the result.
    if (!field.complete()) {
        return fail(ContourProblem::FieldIncomplete,
                    std::format("channel {} classified {} of {} {}s, so it cannot be banded",
                                toString(field.channel), field.classified, field.expected,
                                toString(field.domain)));
    }
    if (bands < kMinimumContourBands || bands > kMaximumContourBands) {
        return fail(ContourProblem::BandCountUnusable,
                    std::format("{} bands requested; the usable range is {} to {}", bands,
                                kMinimumContourBands, kMaximumContourBands));
    }

    ContourLegend out;
    out.channel = field.channel;
    out.scale = scale;
    out.unit = descriptorOf(field.channel).unit;
    out.minimum = field.minimum;
    out.maximum = field.maximum;

    // A UNIFORM FIELD IS ONE BAND, NOT N IDENTICAL ONES. `(v - min)/(max-min)`
    // is 0/0 here, and N bands of zero width would imply a variation the
    // result does not have while making every boundary equal. The band is
    // painted from the MIDDLE of the ramp: a uniform field is simultaneously
    // its own minimum and maximum, so neither end is the honest colour.
    //
    // AND SO IS A RANGE TOO NARROW TO SUBDIVIDE, which is not the same
    // condition. If `minimum + width == minimum` -- the range is a few units
    // in the last place, or `width` underflowed -- then every interior
    // boundary collapses onto the minimum, bands 0 to N-2 become the empty
    // interval `[min, min)` and only the last can ever be drawn. The
    // partition would still be valid and the legend would still print N
    // bands, which is the quiet kind of wrong: a variation implied across
    // bands that cannot contain a value. One band says what is true.
    const double span = out.maximum - out.minimum;
    const double trialWidth = span / static_cast<double>(bands);
    if (field.uniform() || out.minimum + trialWidth == out.minimum) {
        out.uniform = true;
        ContourBand only;
        only.lower = out.minimum;
        only.upper = out.maximum;
        only.closedAbove = true;
        out.bands.push_back(paint(only, 0.5));
        return out;
    }

    // ONE ARRAY OF BOUNDARIES, SHARED BY ADJACENT BANDS. The interior ones are
    // computed; the two ends are the field's own numbers, unrounded.
    std::vector<double> boundary(bands + 1);
    boundary.front() = out.minimum;
    boundary.back() = out.maximum;
    // THE SAME width ALREADY TESTED ABOVE, reused rather than recomputed: two
    // expressions for one quantity is what the header of this file warns
    // against, and the test above is only meaningful if it tested THIS value.
    const double width = trialWidth;
    for (std::size_t i = 1; i < bands; ++i) {
        // Clamped so a denormal width cannot push an interior boundary outside
        // the range, and monotonic against its predecessor so the partition
        // cannot invert.
        const double computed = out.minimum + (static_cast<double>(i) * width);
        boundary[i] = std::clamp(computed, boundary[i - 1], out.maximum);
    }

    out.bands.reserve(bands);
    for (std::size_t i = 0; i < bands; ++i) {
        ContourBand band;
        band.lower = boundary[i];
        band.upper = boundary[i + 1];
        band.closedAbove = (i + 1 == bands);
        // The band's own midpoint fraction, so the first band is not pure blue
        // and the last not pure red -- the extremes of the ramp belong to the
        // ends of the RANGE, which the legend prints, not to a band's fill.
        const double t = (static_cast<double>(i) + 0.5) / static_cast<double>(bands);
        out.bands.push_back(paint(band, t));
    }
    return out;
}

Result<std::vector<std::size_t>> contourBandsOf(const ScalarField& field,
                                                const ContourLegend& legend) {
    if (!field.complete()) {
        return fail(ContourProblem::FieldIncomplete,
                    std::format("channel {} classified {} of {} {}s", toString(field.channel),
                                field.classified, field.expected, toString(field.domain)));
    }
    // THE LEGEND MUST BE THIS FIELD'S. Compared on the channel AND on both
    // endpoints: two solves of the same channel can have different ranges, and
    // banding through the wrong legend is wrong by a shift rather than
    // obviously broken.
    if (legend.channel != field.channel || legend.minimum != field.minimum ||
        legend.maximum != field.maximum) {
        return fail(ContourProblem::LegendMismatch,
                    std::format("the legend is for channel {} over [{}, {}] and the field is "
                                "channel {} over [{}, {}]",
                                toString(legend.channel), legend.minimum, legend.maximum,
                                toString(field.channel), field.minimum, field.maximum));
    }
    if (!legend.partitionsItsRange()) {
        return fail(ContourProblem::BandOutOfRange,
                    "the legend's bands do not partition its own range");
    }

    std::vector<std::size_t> out;
    out.reserve(field.values.size());
    for (const double value : field.values) {
        const std::size_t band = legend.bandOf(value);
        // UNREACHABLE, AND CHECKED ANYWAY. A value drawn from this field lies
        // in [minimum, maximum] by construction, and the bands partition that
        // interval -- so a miss means the two checks above passed and the
        // partition is still wrong. Refused rather than clamped into the
        // nearest band, which would colour a defect as data.
        if (band >= legend.bands.size()) {
            return fail(ContourProblem::BandOutOfRange,
                        std::format("value {} of channel {} falls in no band of [{}, {}]", value,
                                    toString(field.channel), legend.minimum, legend.maximum));
        }
        out.push_back(band);
    }
    return out;
}

namespace {

/// The index of @p entity in @p sorted, or `sorted.size()` when absent.
///
/// A BINARY SEARCH, and the caller has already checked that @p sorted really
/// is sorted. `ScalarField` documents its id arrays as ascending, and
/// `triangleCornerBands` checks that rather than trusting it -- a linear scan
/// would be correct whatever the order, but it would also hide an unsorted
/// field that every other consumer of that documented guarantee relies on.
template <typename Id>
[[nodiscard]] std::size_t indexOf(std::span<const Id> sorted, Id entity) {
    const auto at = std::ranges::lower_bound(sorted, entity);
    if (at == sorted.end() || *at != entity) {
        return sorted.size();
    }
    return static_cast<std::size_t>(std::distance(sorted.begin(), at));
}

} // namespace

Result<std::vector<std::size_t>> triangleCornerBands(const MeshView& view,
                                                     const ScalarField& field,
                                                     std::span<const std::size_t> entityBands) {
    if (!field.complete()) {
        return fail(ContourProblem::FieldIncomplete,
                    std::format("channel {} classified {} of {} {}s, so its bands cannot be "
                                "mapped onto render triangles",
                                toString(field.channel), field.classified, field.expected,
                                toString(field.domain)));
    }
    if (entityBands.size() != field.values.size()) {
        return fail(ContourProblem::LegendMismatch,
                    std::format("the field has {} values and {} band indices were given",
                                field.values.size(), entityBands.size()));
    }
    if (view.triangleCount() == 0U) {
        return fail(ContourProblem::FieldIncomplete,
                    "this mesh view has no triangles, so there is nothing to colour");
    }

    const bool nodal = field.domain == ChannelDomain::Node;
    const std::span<const meshing::NodeId> nodes{field.nodes};
    const std::span<const meshing::ElementId> elements{field.elements};
    // THE DOCUMENTED ORDER, CHECKED. `indexOf` is a binary search, so an
    // unsorted array would return a wrong index rather than an error -- which
    // is the quiet kind of wrong this layer exists to avoid.
    if (nodal ? !std::ranges::is_sorted(nodes) : !std::ranges::is_sorted(elements)) {
        return fail(ContourProblem::FieldIncomplete,
                    std::format("channel {}'s identity array is not in ascending order, which "
                                "its own contract requires",
                                toString(field.channel)));
    }

    const std::span<const std::uint32_t> indices = view.triangleIndices();
    std::vector<std::size_t> out;
    out.reserve(view.triangleCount() * 3U);

    for (std::size_t triangle = 0U; triangle < view.triangleCount(); ++triangle) {
        if (!nodal) {
            // ALL THREE CORNERS SHARE THE ELEMENT'S BAND, so the facet draws
            // flat. Element-constant stress IS constant over the element, and
            // interpolating it would invent a variation.
            const Result<meshing::ElementId> owner = view.elementOfTriangle(triangle);
            if (!owner.has_value()) {
                return fail(ContourProblem::BandOutOfRange,
                            std::format("render triangle {} names no element", triangle));
            }
            const std::size_t at = indexOf(elements, *owner);
            if (at >= elements.size()) {
                return fail(ContourProblem::BandOutOfRange,
                            std::format("element {} owns render triangle {} and channel {} has "
                                        "no value for it",
                                        owner->value(), triangle, toString(field.channel)));
            }
            out.push_back(entityBands[at]);
            out.push_back(entityBands[at]);
            out.push_back(entityBands[at]);
            continue;
        }
        // EACH CORNER TAKES ITS OWN NODE'S BAND. The interpolation across the
        // facet is the element's linear shape function, which is where a
        // nodal field is defined -- not an average of anything.
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const std::size_t vertex = indices[(triangle * 3U) + corner];
            const Result<meshing::NodeId> node = view.nodeOfVertex(vertex);
            if (!node.has_value()) {
                return fail(ContourProblem::BandOutOfRange,
                            std::format("render vertex {} names no node", vertex));
            }
            const std::size_t at = indexOf(nodes, *node);
            if (at >= nodes.size()) {
                return fail(ContourProblem::BandOutOfRange,
                            std::format("node {} is a corner of render triangle {} and channel "
                                        "{} has no value for it",
                                        node->value(), triangle, toString(field.channel)));
            }
            out.push_back(entityBands[at]);
        }
    }
    return out;
}

Result<std::vector<double>> contourCornerColours(const MeshView& view, const ScalarField& field,
                                                 const ContourLegend& legend) {
    // THE ORDER IS THE WHOLE VALUE OF THIS FUNCTION: band the field through
    // the legend FIRST, so the legend-mismatch and partition checks run before
    // anything is mapped onto geometry.
    const Result<std::vector<std::size_t>> bands = contourBandsOf(field, legend);
    if (!bands.has_value()) {
        return std::unexpected(bands.error());
    }
    const Result<std::vector<std::size_t>> corners = triangleCornerBands(view, field, *bands);
    if (!corners.has_value()) {
        return std::unexpected(corners.error());
    }

    std::vector<double> out;
    out.reserve(corners->size() * 3U);
    for (const std::size_t band : *corners) {
        if (band >= legend.bands.size()) {
            // UNREACHABLE, AND CHECKED: `contourBandsOf` already refused a
            // value outside the legend, and `triangleCornerBands` only copies
            // indices it produced. Kept because reading past the palette is
            // the one mistake here that would not look like a mistake.
            return fail(ContourProblem::BandOutOfRange,
                        std::format("band {} is outside a legend of {} bands", band,
                                    legend.bands.size()));
        }
        out.push_back(legend.bands[band].red);
        out.push_back(legend.bands[band].green);
        out.push_back(legend.bands[band].blue);
    }
    return out;
}

} // namespace bettercad::renderer
