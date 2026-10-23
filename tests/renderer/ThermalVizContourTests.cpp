// P18-VIZ-001 -- colour banding, the legend, the contour cache.
//
// THE COLOUR IS NOT THE RESULT. These tests hold the arithmetic of a contour -- that every value lands in exactly
// one band, that a corner takes the band of its own node (nodal) or of its owning tetrahedron (element), that a
// uniform field does not divide by zero, that a manual range clamps the COLOUR and never the value -- against the
// test's own independent expectation. Which pixel a GPU lights is supplemental and lives in ThermalVizViewerTests.

#include "ThermalVizTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>

using namespace vztest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

/// Four nodes at the brief's temperatures, sparse handles, one tetrahedron.
[[nodiscard]] HandPost briefPost(const std::vector<double>& kelvin = {310.0, 350.0, 295.0, 350.0}) {
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {7, 104, 9001, 12000});
    return makeHandPost(std::move(hand), 5.0, kelvin);
}

/// Four disjoint tetrahedra whose gradients are (3,4,0), (0,12,0), (2,3,6), (0,0,12): |grad| = 5, 12, 7, 12.
[[nodiscard]] HandPost fourPost() {
    const std::array<std::array<double, 3>, 4> gradients{{{3.0, 4.0, 0.0}, {0.0, 12.0, 0.0}, {2.0, 3.0, 6.0}, {0.0, 0.0, 12.0}}};
    std::vector<double> kelvin;
    for (const auto& g : gradients) {
        kelvin.push_back(300.0);
        kelvin.push_back(300.0 + g[0]);
        kelvin.push_back(300.0 + g[1]);
        kelvin.push_back(300.0 + g[2]);
    }
    return makeHandPost(fourTetrahedraHand(), 1.0, kelvin);
}

[[nodiscard]] renderer::ThermalScalarField fieldOf(const HandPost& h, const renderer::MeshView& view, renderer::ThermalChannel channel) {
    const Result<renderer::ThermalResultView> result = renderer::ThermalResultView::of(h.hand.mesh, view, h.hand.dofs, h.post);
    REQUIRE(result.has_value());
    const Result<renderer::ThermalScalarField> field = result->field(h.hand.mesh, h.hand.dofs, h.post, 0, channel);
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    return *field;
}

/// The band a value belongs to by the test's own arithmetic: `floor((v - lo) / width)`, the last band closed.
[[nodiscard]] std::size_t expectedBand(double value, double lo, double hi, std::size_t bands) {
    const double width = (hi - lo) / static_cast<double>(bands);
    return std::min(bands - 1U, static_cast<std::size_t>((value - lo) / width));
}

struct Rgb3 {
    double r, g, b;
    friend bool operator==(const Rgb3&, const Rgb3&) = default;
};

[[nodiscard]] Rgb3 colourOfBand(const renderer::ThermalContourLegend& legend, std::size_t band) {
    return {legend.bands[band].red, legend.bands[band].green, legend.bands[band].blue};
}

[[nodiscard]] Rgb3 cornerColour(const std::vector<double>& colours, std::size_t triangle, std::size_t corner) {
    const std::size_t at = (9 * triangle) + (3 * corner);
    return {colours[at], colours[at + 1], colours[at + 2]};
}

} // namespace

// ===========================================================================
// Contours consume the field
// ===========================================================================

TEST_CASE("ThermalViz_TemperatureContourUsesNodalValues", "[thermal][viz][contour][temperature]") {
    const HandPost h = briefPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::Temperature);
    constexpr std::size_t kBands = 8;
    const auto legend = renderer::thermalContourLegend(field, kBands);
    REQUIRE(legend.has_value());
    // THE AUTO RANGE IS THE SOURCE RANGE: [295, 350] K, taken from the values and not from any pixel.
    CHECK(legend->minimum == 295.0);
    CHECK(legend->maximum == 350.0);
    CHECK(legend->fieldMinimum == 295.0);
    CHECK(legend->rangeSource == renderer::ThermalRangeSource::Auto);
    CHECK(legend->partitionsItsRange());
    CHECK_FALSE(legend->uniform);

    const auto colours = renderer::thermalContourCornerColours(view, field, *legend);
    REQUIRE(colours.has_value());
    REQUIRE(colours->size() == 9 * view.triangleCount());
    REQUIRE(view.triangleCount() == 4);   // one tetrahedron, four exterior faces

    const std::array<double, 4> kelvinByHandle{310.0, 350.0, 295.0, 350.0};   // handles 7, 104, 9001, 12000
    const std::array<std::uint64_t, 4> handles{7, 104, 9001, 12000};
    std::set<std::size_t> distinctFacetPalettes;
    for (std::size_t t = 0; t < view.triangleCount(); ++t) {
        std::set<std::array<double, 3>> corners;
        for (std::size_t c = 0; c < 3; ++c) {
            const Result<meshing::NodeId> node = view.nodeOfVertex(view.triangleIndices()[(3 * t) + c]);
            REQUIRE(node.has_value());
            const auto at = std::ranges::find(handles, node->value());
            REQUIRE(at != handles.end());
            const double kelvin = kelvinByHandle[static_cast<std::size_t>(at - handles.begin())];
            // EACH CORNER TAKES THE BAND OF ITS OWN NODE'S TEMPERATURE.
            const std::size_t band = expectedBand(kelvin, 295.0, 350.0, kBands);
            CHECK(cornerColour(*colours, t, c) == colourOfBand(*legend, band));
            const Rgb3 got = cornerColour(*colours, t, c);
            corners.insert({got.r, got.g, got.b});
        }
        distinctFacetPalettes.insert(corners.size());
    }
    // A facet with corners at 295, 310 and 350 K is NOT flat: the nodal field interpolates by the facet's shape function.
    CHECK(distinctFacetPalettes.contains(3));
    // Min and max take the two ends of the ramp, in the legend's own order.
    CHECK(legend->bandOf(295.0) == 0);
    CHECK(legend->bandOf(350.0) == kBands - 1);
}

TEST_CASE("ThermalViz_GradientMagnitudeContourUsesElementResult", "[thermal][viz][contour][gradient]") {
    const HandPost h = fourPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::TemperatureGradientMagnitude);
    constexpr std::size_t kBands = 4;
    const auto legend = renderer::thermalContourLegend(field, kBands);
    REQUIRE(legend.has_value());
    CHECK(legend->minimum == field.minimum);
    CHECK_THAT(legend->minimum, WithinAbs(5.0, 1e-11));
    CHECK_THAT(legend->maximum, WithinAbs(12.0, 1e-11));
    const auto colours = renderer::thermalContourCornerColours(view, field, *legend);
    REQUIRE(colours.has_value());
    REQUIRE(view.triangleCount() == 16);   // four tetrahedra, four exterior faces each

    const std::array<double, 4> typed{5.0, 12.0, 7.0, 12.0};
    for (std::size_t t = 0; t < view.triangleCount(); ++t) {
        const Result<meshing::ElementId> owner = view.elementOfTriangle(t);
        REQUIRE(owner.has_value());
        const std::optional<std::size_t> ordinal = h.post.ordinalOf(*owner);
        REQUIRE(ordinal.has_value());
        // ALL THREE CORNERS OF A FACET SHARE THE OWNING TETRAHEDRON'S BAND: the value is element-constant, drawn flat.
        const Rgb3 first = cornerColour(*colours, t, 0);
        CHECK(cornerColour(*colours, t, 1) == first);
        CHECK(cornerColour(*colours, t, 2) == first);
        CHECK(first == colourOfBand(*legend, expectedBand(typed[*ordinal], 5.0, 12.0, kBands)));
    }
}

TEST_CASE("ThermalViz_HeatFluxMagnitudeContourUsesElementResult", "[thermal][viz][contour][heatflux]") {
    const HandPost h = fourPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::HeatFluxMagnitude);
    constexpr std::size_t kBands = 6;
    const auto legend = renderer::thermalContourLegend(field, kBands);
    REQUIRE(legend.has_value());
    const auto colours = renderer::thermalContourCornerColours(view, field, *legend);
    REQUIRE(colours.has_value());
    const std::array<double, 4> typed{5.0, 12.0, 7.0, 12.0};   // k = 1, so |q| = |grad|
    std::array<std::optional<Rgb3>, 4> colourOfElement;
    for (std::size_t t = 0; t < view.triangleCount(); ++t) {
        const Result<meshing::ElementId> owner = view.elementOfTriangle(t);
        REQUIRE(owner.has_value());
        const std::size_t ordinal = *h.post.ordinalOf(*owner);
        const Rgb3 colour = cornerColour(*colours, t, 0);
        CHECK(colour == colourOfBand(*legend, expectedBand(typed[ordinal], 5.0, 12.0, kBands)));
        // Every facet of one tetrahedron has the one colour.
        if (colourOfElement[ordinal].has_value()) {
            CHECK(*colourOfElement[ordinal] == colour);
        }
        colourOfElement[ordinal] = colour;
    }
    // The two tetrahedra at 12 W/m^2 share the top band's colour; the one at 5 has the bottom's.
    CHECK(*colourOfElement[1] == *colourOfElement[3]);
    CHECK(*colourOfElement[0] == colourOfBand(*legend, 0));
    CHECK(*colourOfElement[1] == colourOfBand(*legend, kBands - 1));
    CHECK_FALSE(*colourOfElement[0] == *colourOfElement[1]);
}

TEST_CASE("ThermalViz_AnElementChannelNeedsAViewThatNamesTetrahedra", "[thermal][viz][contour][element]") {
    // The Triangle3 boundary view names boundary TRIANGLES, which no per-tetrahedron value belongs to. Refused by name,
    // not discovered as "no value for element N" on the first triangle.
    const HandPost h = fourPost();
    const renderer::MeshView byTet = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, byTet, renderer::ThermalChannel::HeatFluxMagnitude);
    const auto legend = renderer::thermalContourLegend(field, 4);
    REQUIRE(legend.has_value());

    const Result<renderer::MeshView> boundary = renderer::MeshView::volumeBoundaryOf(h.hand.mesh);
    REQUIRE(boundary.has_value());
    REQUIRE(boundary->source() == renderer::MeshSource::VolumeBoundary);
    const auto refused = renderer::thermalContourCornerColours(*boundary, field, *legend);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("not tetrahedra"));
    // A NODAL channel works on either view.
    const renderer::ThermalScalarField temperature = fieldOf(h, byTet, renderer::ThermalChannel::Temperature);
    const auto tLegend = renderer::thermalContourLegend(temperature, 4);
    REQUIRE(tLegend.has_value());
    CHECK(renderer::thermalContourCornerColours(*boundary, temperature, *tLegend).has_value());
}

// ===========================================================================
// A range with nothing to draw
// ===========================================================================

TEST_CASE("ThermalViz_ConstantScalarRangeHandled", "[thermal][viz][contour][constant]") {
    // A UNIFORM TEMPERATURE: min == max. (v - min) / (max - min) is 0/0. One band, painted from the middle of the ramp,
    // deterministic, finite -- and the field is a valid one.
    const HandPost h = briefPost({320.0, 320.0, 320.0, 320.0});
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::Temperature);
    CHECK(field.uniform());
    CHECK(field.complete());
    const auto legend = renderer::thermalContourLegend(field, 16);
    REQUIRE(legend.has_value());
    CHECK(legend->uniform);
    REQUIRE(legend->bands.size() == 1);
    CHECK(legend->partitionsItsRange());
    CHECK(legend->minimum == 320.0);
    CHECK(legend->maximum == 320.0);
    CHECK(legend->bands[0].contains(320.0));
    // Painted from the middle of the five-stop ramp: the green stop, exactly.
    CHECK(colourOfBand(*legend, 0) == Rgb3{0.25, 0.70, 0.30});

    const auto colours = renderer::thermalContourCornerColours(view, field, *legend);
    REQUIRE(colours.has_value());
    for (const double c : *colours) {
        CHECK(std::isfinite(c));
        CHECK(c >= 0.0);
        CHECK(c <= 1.0);
    }
    for (std::size_t t = 0; t < view.triangleCount(); ++t) {
        for (std::size_t c = 0; c < 3; ++c) {
            CHECK(cornerColour(*colours, t, c) == Rgb3{0.25, 0.70, 0.30});
        }
    }
    // And a uniform heat flux: four tetrahedra with the same gradient.
    std::vector<double> kelvin;
    for (std::size_t t = 0; t < 4; ++t) {
        kelvin.insert(kelvin.end(), {300.0, 310.0, 300.0, 300.0});
    }
    const HandPost same = makeHandPost(fourTetrahedraHand(), 5.0, kelvin);
    const renderer::MeshView sameView = byTetrahedron(same.hand.mesh);
    const renderer::ThermalScalarField q = fieldOf(same, sameView, renderer::ThermalChannel::HeatFluxMagnitude);
    const auto qLegend = renderer::thermalContourLegend(q, 8);
    REQUIRE(qLegend.has_value());
    CHECK(qLegend->bands.size() == 1);
    const auto qColours = renderer::thermalContourCornerColours(sameView, q, *qLegend);
    REQUIRE(qColours.has_value());
    for (const double c : *qColours) {
        CHECK(std::isfinite(c));
    }
}

TEST_CASE("ThermalViz_ARangeTooNarrowToSubdivideIsOneBand", "[thermal][viz][contour][constant]") {
    // Two temperatures one unit in the last place apart: minimum + width == minimum, so every interior boundary would
    // collapse onto the minimum and only the last of N bands could ever hold a value. One band says what is true.
    const double a = 300.0;
    const double b = std::nextafter(a, 400.0);
    REQUIRE(b > a);
    const HandPost h = briefPost({a, b, a, b});
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::Temperature);
    CHECK_FALSE(field.uniform());   // the values are NOT equal
    const auto legend = renderer::thermalContourLegend(field, 16);
    REQUIRE(legend.has_value());
    CHECK(legend->uniform);
    CHECK(legend->bands.size() == 1);
    CHECK(legend->bands[0].contains(a));
    CHECK(legend->bands[0].contains(b));
    CHECK(renderer::thermalContourCornerColours(view, field, *legend).has_value());
}

// ===========================================================================
// The display range is view state
// ===========================================================================

TEST_CASE("ThermalViz_ManualRangeClampsTheColourAndNeverTheValue", "[thermal][viz][contour][range]") {
    const HandPost h = briefPost();   // 310, 350, 295, 350 K
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::Temperature);
    const renderer::ThermalScalarField before = field;

    constexpr std::size_t kBands = 4;
    const auto legend = renderer::thermalContourLegend(field, kBands, renderer::ThermalDisplayRange{300.0, 340.0});
    REQUIRE(legend.has_value());
    CHECK(legend->rangeSource == renderer::ThermalRangeSource::Manual);
    CHECK(legend->minimum == 300.0);
    CHECK(legend->maximum == 340.0);
    CHECK(legend->fieldMinimum == 295.0);   // the field's own range is kept, and is not the displayed one
    CHECK(legend->fieldMaximum == 350.0);
    CHECK(legend->partitionsItsRange());

    // 295 K is below the range and 350 K above: they take the NEAREST END's colour.
    CHECK(legend->bandOf(295.0) == 0);
    CHECK(legend->bandOf(350.0) == kBands - 1);
    CHECK(legend->bandOf(310.0) == 1);   // (310 - 300) / 10 = 1
    CHECK(legend->bandOf(std::numeric_limits<double>::quiet_NaN()) == legend->bands.size());   // a NaN is in no band
    const auto colours = renderer::thermalContourCornerColours(view, field, *legend);
    REQUIRE(colours.has_value());

    // THE VALUES ARE UNTOUCHED: the field, its extremes and the inspector still report 295 and 350.
    CHECK(field == before);
    CHECK(field.minimum == 295.0);
    CHECK(field.maximum == 350.0);
    const auto lowest = renderer::thermalExtremeOf(field, renderer::ThermalExtremeKind::Minimum);
    REQUIRE(lowest.has_value());
    CHECK(lowest->value == 295.0);
    const Result<renderer::ThermalNodeInspection> node =
        renderer::inspectThermalNode(h.hand.mesh, h.hand.dofs, h.post, 0, meshing::NodeId::fromValue(9001));
    REQUIRE(node.has_value());
    CHECK(node->temperature.si() == 295.0);

    // The auto and the manual colour of the same value DIFFER where the range differs: the manual one is not a no-op.
    const auto autoLegend = renderer::thermalContourLegend(field, kBands);
    REQUIRE(autoLegend.has_value());
    CHECK(autoLegend->bandOf(310.0) == 1);   // (310 - 295) / 13.75 = 1.09
    CHECK(colourOfBand(*autoLegend, autoLegend->bandOf(320.0)) != colourOfBand(*legend, legend->bandOf(320.0)));
}

TEST_CASE("ThermalViz_RejectsAnUnusableDisplayRangeOrBandCount", "[thermal][viz][contour][range]") {
    const HandPost h = briefPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::Temperature);
    const auto inverted = renderer::thermalContourLegend(field, 8, renderer::ThermalDisplayRange{340.0, 300.0});
    REQUIRE_FALSE(inverted.has_value());
    CHECK_THAT(inverted.error().message, ContainsSubstring("range_unusable"));
    CHECK_FALSE(renderer::thermalContourLegend(field, 8, renderer::ThermalDisplayRange{std::numeric_limits<double>::quiet_NaN(), 340.0}).has_value());
    CHECK_FALSE(renderer::thermalContourLegend(field, 8, renderer::ThermalDisplayRange{300.0, std::numeric_limits<double>::infinity()}).has_value());
    // A zero-width manual range is a valid single colour.
    const auto flat = renderer::thermalContourLegend(field, 8, renderer::ThermalDisplayRange{320.0, 320.0});
    REQUIRE(flat.has_value());
    CHECK(flat->uniform);

    CHECK_FALSE(renderer::thermalContourLegend(field, renderer::kMinimumThermalContourBands - 1).has_value());
    CHECK_FALSE(renderer::thermalContourLegend(field, renderer::kMaximumThermalContourBands + 1).has_value());
    CHECK(renderer::thermalContourLegend(field, renderer::kMinimumThermalContourBands).has_value());
    CHECK(renderer::thermalContourLegend(field, renderer::kMaximumThermalContourBands).has_value());
}

TEST_CASE("ThermalViz_TheBandsPartitionTheRangeAtEveryCount", "[thermal][viz][contour][partition]") {
    const HandPost h = fourPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::HeatFluxMagnitude);
    for (std::size_t n = renderer::kMinimumThermalContourBands; n <= renderer::kMaximumThermalContourBands; ++n) {
        const auto legend = renderer::thermalContourLegend(field, n);
        REQUIRE(legend.has_value());
        INFO("bands " << n);
        CHECK(legend->bands.size() == n);
        CHECK(legend->partitionsItsRange());
        // Every value of the field lies in EXACTLY one band, the maximum included.
        for (const double value : field.values) {
            std::size_t holders = 0;
            for (const renderer::ThermalContourBand& band : legend->bands) {
                holders += band.contains(value) ? 1U : 0U;
            }
            CHECK(holders == 1);
        }
        // The band edges are shared bitwise: no gap, no overlap.
        for (std::size_t i = 0; i + 1 < legend->bands.size(); ++i) {
            CHECK(legend->bands[i].upper == legend->bands[i + 1].lower);
        }
    }
}

TEST_CASE("ThermalViz_TheSameScalarGetsTheSameColourAndTheEndsAreTheEnds", "[thermal][viz][contour][integrity]") {
    const HandPost h = fourPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::HeatFluxMagnitude);
    const auto legend = renderer::thermalContourLegend(field, 8);
    REQUIRE(legend.has_value());
    const auto a = renderer::thermalContourCornerColours(view, field, *legend);
    const auto b = renderer::thermalContourCornerColours(view, field, *legend);
    REQUIRE(a.has_value());
    CHECK(*a == *b);   // same scalar, same scale: same colour, run to run
    // The minimum takes the bottom of the ramp and the maximum the top: the first and last bands of the legend.
    CHECK(legend->bandOf(field.minimum) == 0);
    CHECK(legend->bandOf(field.maximum) == legend->bands.size() - 1);
    CHECK(colourOfBand(*legend, 0) != colourOfBand(*legend, legend->bands.size() - 1));
}

TEST_CASE("ThermalViz_ALegendOfAnotherFieldIsRefused", "[thermal][viz][contour][legend]") {
    const HandPost h = briefPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::Temperature);
    const HandPost other = briefPost({300.0, 360.0, 290.0, 360.0});
    const renderer::MeshView otherView = byTetrahedron(other.hand.mesh);
    const renderer::ThermalScalarField otherField = fieldOf(other, otherView, renderer::ThermalChannel::Temperature);
    const auto otherLegend = renderer::thermalContourLegend(otherField, 8);
    REQUIRE(otherLegend.has_value());
    // Same channel, same band count, DIFFERENT range: banding through it would be wrong by a shift, plausible and false.
    const auto refused = renderer::thermalContourBandsOf(field, *otherLegend);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("legend_mismatch"));
    // A legend of another channel.
    const renderer::ThermalScalarField q = fieldOf(h, view, renderer::ThermalChannel::HeatFluxMagnitude);
    const auto qLegend = renderer::thermalContourLegend(q, 8);
    REQUIRE(qLegend.has_value());
    CHECK_FALSE(renderer::thermalContourBandsOf(field, *qLegend).has_value());
    // A legend whose bands do not partition its range.
    renderer::ThermalContourLegend broken = *renderer::thermalContourLegend(field, 8);
    broken.bands[3].upper += 1.0;
    CHECK_FALSE(broken.partitionsItsRange());
    CHECK_FALSE(renderer::thermalContourBandsOf(field, broken).has_value());
}

TEST_CASE("ThermalViz_APartialFieldIsNeverColoured", "[thermal][viz][contour][trap]") {
    // THE P16 TRAP, ONE LAYER DOWN: a field that classified fewer entities than its domain holds is not a result, and a
    // legend over it would present the entities that happened to classify as the whole range.
    const HandPost h = briefPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    renderer::ThermalScalarField field = fieldOf(h, view, renderer::ThermalChannel::Temperature);
    const auto whole = renderer::thermalContourLegend(field, 8);
    REQUIRE(whole.has_value());
    field.classified = 3;
    CHECK_FALSE(field.complete());
    const auto legend = renderer::thermalContourLegend(field, 8);
    REQUIRE_FALSE(legend.has_value());
    CHECK_THAT(legend.error().message, ContainsSubstring("field_incomplete"));
    CHECK_FALSE(renderer::thermalContourBandsOf(field, *whole).has_value());
    field.classified = 4;
    field.skipped = 1;
    CHECK_FALSE(field.complete());
    field.skipped = 0;
    field.nonFinite = 1;
    CHECK_FALSE(field.complete());
    field.nonFinite = 0;
    // And an identity array that is not ascending cannot be searched.
    std::swap(field.nodes[0], field.nodes[3]);
    const auto bands = renderer::thermalContourBandsOf(field, *whole);
    REQUIRE(bands.has_value());
    CHECK_FALSE(renderer::thermalTriangleCornerBands(view, field, *bands).has_value());
}

// ===========================================================================
// The legend states the field, the unit and the stored time
// ===========================================================================

TEST_CASE("ThermalViz_TheLegendNamesTheFieldTheUnitAndTheStoredTime", "[thermal][viz][contour][legend]") {
    const HandPost h = fourPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    struct Row {
        renderer::ThermalChannel channel;
        const char* expected;
    };
    for (const Row row : {Row{renderer::ThermalChannel::Temperature, "Temperature [K]"},
                          Row{renderer::ThermalChannel::TemperatureGradientMagnitude, "Temperature Gradient Magnitude [K/m]"},
                          Row{renderer::ThermalChannel::HeatFluxMagnitude, "Heat Flux Magnitude [W/m^2]"}}) {
        const renderer::ThermalScalarField field = fieldOf(h, view, row.channel);
        const auto legend = renderer::thermalContourLegend(field, 8);
        REQUIRE(legend.has_value());
        CHECK(renderer::thermalLegendTitle(*legend, std::nullopt) == row.expected);
        CHECK(renderer::thermalLegendTitle(*legend, 0.6) == std::string{row.expected} + " at t = 0.6 s");
    }
    // The title carries the STORED time verbatim -- 1 for a final partial step, never a count times a step.
    const renderer::ThermalScalarField t = fieldOf(h, view, renderer::ThermalChannel::Temperature);
    const auto legend = renderer::thermalContourLegend(t, 8);
    REQUIRE(legend.has_value());
    CHECK(renderer::thermalLegendTitle(*legend, 1.0) == "Temperature [K] at t = 1 s");
}

// ===========================================================================
// The cache is derived and keyed on everything
// ===========================================================================

TEST_CASE("ThermalViz_TheContourCacheIsKeyedOnTheWholeSource", "[thermal][viz][contour][cache]") {
    const HandPost h = fourPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    renderer::ThermalContourCache cache;
    CHECK(cache.key() == nullptr);
    CHECK(cache.builds() == 0);

    const auto first = cache.colours(h.hand.mesh, view, h.hand.dofs, h.post, 0, renderer::ThermalChannel::Temperature, 8);
    REQUIRE(first.has_value());
    CHECK(cache.builds() == 1);
    CHECK(cache.hits() == 0);
    REQUIRE(cache.key() != nullptr);
    const std::vector<double> held = **first;

    // The identical question is answered from the held buffer.
    const auto again = cache.colours(h.hand.mesh, view, h.hand.dofs, h.post, 0, renderer::ThermalChannel::Temperature, 8);
    REQUIRE(again.has_value());
    CHECK(cache.builds() == 1);
    CHECK(cache.hits() == 1);
    CHECK(**again == held);

    // Every part of the key forces a rebuild: the channel, the band count, the display range.
    REQUIRE(cache.colours(h.hand.mesh, view, h.hand.dofs, h.post, 0, renderer::ThermalChannel::HeatFluxMagnitude, 8).has_value());
    CHECK(cache.builds() == 2);
    REQUIRE(cache.colours(h.hand.mesh, view, h.hand.dofs, h.post, 0, renderer::ThermalChannel::HeatFluxMagnitude, 9).has_value());
    CHECK(cache.builds() == 3);
    REQUIRE(cache.colours(h.hand.mesh, view, h.hand.dofs, h.post, 0, renderer::ThermalChannel::HeatFluxMagnitude, 9,
                          renderer::ThermalDisplayRange{4.0, 20.0})
                .has_value());
    CHECK(cache.builds() == 4);

    // A NEW RESULT with the same counts, the same handles and the same channel is a different source: a rebuild, not a hit.
    const HandPost twin = fourPost();
    const renderer::MeshView twinView = byTetrahedron(twin.hand.mesh);
    REQUIRE(cache.colours(twin.hand.mesh, twinView, twin.hand.dofs, twin.post, 0, renderer::ThermalChannel::HeatFluxMagnitude, 9,
                          renderer::ThermalDisplayRange{4.0, 20.0})
                .has_value());
    CHECK(cache.builds() == 5);
    CHECK(cache.hits() == 1);

    // A frame the result does not hold fails, and a failure leaves no stale buffer behind as the answer for that key.
    const auto missing = cache.colours(twin.hand.mesh, twinView, twin.hand.dofs, twin.post, 7, renderer::ThermalChannel::Temperature, 8);
    REQUIRE_FALSE(missing.has_value());
    CHECK(cache.key() == nullptr);
    cache.clear();
    CHECK(cache.key() == nullptr);
}

TEST_CASE("ThermalViz_ColourBuffersAreDeterministicAcrossRebuilds", "[thermal][viz][contour][determinism]") {
    const HandPost h = fourPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    for (const renderer::ThermalChannel channel : renderer::allThermalChannels()) {
        const renderer::ThermalScalarField field = fieldOf(h, view, channel);
        const auto legend = renderer::thermalContourLegend(field, 12);
        REQUIRE(legend.has_value());
        const auto a = renderer::thermalContourCornerColours(view, field, *legend);
        const auto b = renderer::thermalContourCornerColours(view, field, *legend);
        REQUIRE(a.has_value());
        INFO(renderer::toString(channel));
        CHECK(*a == *b);
        CHECK(a->size() == 9 * view.triangleCount());
    }
}

TEST_CASE("ThermalViz_TheContourCacheIsKeyedOnTheViewAsWellAsTheSource", "[thermal][viz][contour][cache]") {
    // One mesh, one result, one frame, one channel, one band count -- and two VIEWS of the mesh. A colour buffer is
    // parallel to a view's triangles, so a hit across views would hand back a buffer in the wrong order or length.
    const HandPost h = fourPost();
    const renderer::MeshView tetrahedra = byTetrahedron(h.hand.mesh);
    const Result<renderer::MeshView> plain = renderer::MeshView::volumeBoundaryOf(h.hand.mesh);
    REQUIRE(plain.has_value());
    REQUIRE(plain->source() != tetrahedra.source());

    renderer::ThermalContourCache cache;
    REQUIRE(cache.colours(h.hand.mesh, tetrahedra, h.hand.dofs, h.post, 0, renderer::ThermalChannel::Temperature, 8).has_value());
    CHECK(cache.builds() == 1);
    const auto other = cache.colours(h.hand.mesh, *plain, h.hand.dofs, h.post, 0, renderer::ThermalChannel::Temperature, 8);
    REQUIRE(other.has_value());
    CHECK(cache.builds() == 2);   // a REBUILD for the other view, not a hit
    CHECK(cache.hits() == 0);
    CHECK((*other)->size() == plain->triangleCount() * 9U);
    REQUIRE(cache.key() != nullptr);
    CHECK(cache.key()->view == plain->source());
    CHECK(cache.key()->triangles == plain->triangleCount());

    // The same view again IS a hit.
    REQUIRE(cache.colours(h.hand.mesh, *plain, h.hand.dofs, h.post, 0, renderer::ThermalChannel::Temperature, 8).has_value());
    CHECK(cache.hits() == 1);
}
