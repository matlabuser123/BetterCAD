// P18-VIZ-001 -- the thermal result view: channels, units, classification, extremes, sources.
//
// WHERE THIS MILESTONE IS VERIFIED. Every gate the brief calls hard is assertable without a display: the values a
// contour is built from, their units and locations, the cardinality of each field, the extremes, the identity and
// the source guards. A GPU and a driver are not an engineering gate, so pixel comparison is supplemental and lives
// in ThermalVizViewerTests.cpp.
//
// THE FIXTURES ARE QUALIFIED RESULTS (see ThermalVizTestSupport.hpp): a hand mesh whose every nodal temperature the
// test chose, so the gradient and the flux are known from the test's own arithmetic, and a real document. Nothing
// here asks the viewer for the number it is judged against.

#include "ThermalVizTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <bit>
#include <cmath>
#include <limits>
#include <string>

using namespace vztest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kK = 5.0;   // W/(m K)
constexpr double kA = 307.3; // K
constexpr double kB = 7.2;   // K/m
constexpr double kC = -3.5;  // K/m
constexpr double kD = 11.4;  // K/m

/// The unit tetrahedron with the gradient (kB, kC, kD): T = kA + kB x + kC y + kD z at its four corners.
/// Handles are sparse (7, 104, 9001, 12000), so the numbering is 0..3 and the handles are none of them.
[[nodiscard]] HandPost affinePost() {
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {7, 104, 9001, 12000});
    // Numbering order = ascending handle = corner order here: 7, 104, 9001, 12000.
    return makeHandPost(std::move(hand), kK, {kA, kA + kB, kA + kC, kA + kD});
}

/// Four disjoint tetrahedra whose gradients are typed in, so that |grad| is 5, 12, 7, 12 (a 3-4-5 triple, a pure y
/// gradient of 12, 2-3-6, and a pure z gradient of 12), and with k = 1 the heat flux magnitudes are the same four
/// numbers. Element ids are 17..20 (the sixteen boundary triangles come first).
[[nodiscard]] HandPost fourGradientsPost() {
    const std::array<std::array<double, 3>, 4> gradients{{{3.0, 4.0, 0.0}, {0.0, 12.0, 0.0}, {2.0, 3.0, 6.0}, {0.0, 0.0, 12.0}}};
    AsmHand hand = fourTetrahedraHand();
    std::vector<double> kelvin;
    for (std::size_t t = 0; t < 4; ++t) {
        const auto& g = gradients[t];
        // T = 300 + g . (x - x0) at the corners (0,0,0), (1,0,0), (0,1,0), (0,0,1) of the t-th unit tetrahedron.
        kelvin.push_back(300.0);
        kelvin.push_back(300.0 + g[0]);
        kelvin.push_back(300.0 + g[1]);
        kelvin.push_back(300.0 + g[2]);
    }
    return makeHandPost(std::move(hand), 1.0, kelvin);
}

[[nodiscard]] renderer::ThermalScalarField fieldOf(const HandPost& h, renderer::ThermalChannel channel) {
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const Result<renderer::ThermalResultView> result = renderer::ThermalResultView::of(h.hand.mesh, view, h.hand.dofs, h.post);
    INFO((result.has_value() ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
    const Result<renderer::ThermalScalarField> field = result->field(h.hand.mesh, h.hand.dofs, h.post, 0, channel);
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    return *field;
}

} // namespace

// ===========================================================================
// The channels consume the qualified values
// ===========================================================================

TEST_CASE("ThermalViz_ConsumesQualifiedTemperature", "[thermal][viz][source][temperature]") {
    const HandPost h = affinePost();
    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::Temperature);

    // THE FIELD IS POST'S TEMPERATURE VECTOR, bit for bit, in numbering order.
    REQUIRE(field.values.size() == 4);
    const std::array<double, 4> expected{kA, kA + kB, kA + kC, kA + kD};
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(std::bit_cast<std::uint64_t>(field.values[i]) == std::bit_cast<std::uint64_t>(h.post.states()[0].temperatures[i].si()));
        CHECK(std::bit_cast<std::uint64_t>(field.values[i]) == std::bit_cast<std::uint64_t>(expected[i]));
    }
    CHECK(field.location == thermal::FieldLocation::Node);
    CHECK(field.complete());
    CHECK(field.expected == 4);
    CHECK(field.classified == 4);
    CHECK(field.skipped == 0);
    CHECK(field.nonFinite == 0);
    // The entities are the numbering's nodes, sparse handles and all, ascending.
    REQUIRE(field.nodes.size() == 4);
    CHECK(field.nodes[0] == meshing::NodeId::fromValue(7));
    CHECK(field.nodes[1] == meshing::NodeId::fromValue(104));
    CHECK(field.nodes[2] == meshing::NodeId::fromValue(9001));
    CHECK(field.nodes[3] == meshing::NodeId::fromValue(12000));
    CHECK(field.elements.empty());
    CHECK_FALSE(field.time.has_value());   // a steady result has no time
    WARN("temperature field: nodes 7, 104, 9001, 12000 carry 307.3, 314.5, 303.8, 318.7 K, bitwise POST's vector, sizes 4 (not 12001)");
}

TEST_CASE("ThermalViz_ConsumesQualifiedGradient", "[thermal][viz][source][gradient]") {
    const HandPost h = affinePost();
    // POST's own vector first: the typed affine field has grad T = (7.2, -3.5, 11.4) K/m. That is the QUALIFIED value.
    const Result<thermal::TemperatureGradient3> stored = h.post.gradientOf(0, h.post.elementIds()[0]);
    REQUIRE(stored.has_value());
    CHECK_THAT(stored->x.si(), WithinAbs(kB, 1e-11));
    CHECK_THAT(stored->y.si(), WithinAbs(kC, 1e-11));
    CHECK_THAT(stored->z.si(), WithinAbs(kD, 1e-11));

    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::TemperatureGradientMagnitude);
    REQUIRE(field.values.size() == 1);
    // The display scalar is the norm of that stored vector, against the test's own arithmetic: sqrt(7.2^2 + 3.5^2 + 11.4^2).
    const double expected = std::sqrt(kB * kB + kC * kC + kD * kD);
    CHECK_THAT(field.values[0], WithinRel(expected, 1e-12));
    CHECK_THAT(field.values[0], WithinAbs(13.93, 0.01));   // sqrt(194.05), typed rounded cross-check
    CHECK(field.location == thermal::FieldLocation::Element);
    REQUIRE(field.elements.size() == 1);
    CHECK(field.elements[0] == h.post.elementIds()[0]);
    CHECK(field.nodes.empty());
}

TEST_CASE("ThermalViz_TheGradientMagnitudeIsTheNormOfTheStoredVector", "[thermal][viz][source][gradient]") {
    // Every element of a scattered field: the channel is exactly the norm of POST's stored vector, element by element,
    // and the stored vector itself is untouched. The one place the viewer takes a norm, held to the test's own.
    const HandPost h = fourGradientsPost();
    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::TemperatureGradientMagnitude);
    REQUIRE(field.values.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        const thermal::TemperatureGradient3& g = h.post.states()[0].fields.gradients[i];
        CHECK_THAT(field.values[i], WithinRel(normOf(g.x.si(), g.y.si(), g.z.si()), 1e-14));
        CHECK(std::bit_cast<std::uint64_t>(field.values[i]) ==
              std::bit_cast<std::uint64_t>(renderer::gradientDisplayNorm(g)));
    }
    const std::array<double, 4> typed{5.0, 12.0, 7.0, 12.0};
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK_THAT(field.values[i], WithinAbs(typed[i], 1e-11));
    }
}

TEST_CASE("ThermalViz_ConsumesQualifiedHeatFlux", "[thermal][viz][source][heatflux]") {
    const HandPost h = affinePost();
    // q = -k grad(T) = (-36, 17.5, -57) W/m^2 with k = 5: POST's stored vector and its stored magnitude.
    const Result<thermal::HeatFlux3> q = h.post.heatFluxOf(0, h.post.elementIds()[0]);
    REQUIRE(q.has_value());
    CHECK_THAT(q->x.si(), WithinAbs(-36.0, 1e-10));
    CHECK_THAT(q->y.si(), WithinAbs(17.5, 1e-10));
    CHECK_THAT(q->z.si(), WithinAbs(-57.0, 1e-10));

    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::HeatFluxMagnitude);
    REQUIRE(field.values.size() == 1);
    const Result<HeatFlux> stored = h.post.heatFluxMagnitudeOf(0, h.post.elementIds()[0]);
    REQUIRE(stored.has_value());
    // THE VALUE IS POST'S STORED |q|, BIT FOR BIT: the viewer takes no norm of its own here.
    CHECK(std::bit_cast<std::uint64_t>(field.values[0]) == std::bit_cast<std::uint64_t>(stored->si()));
    CHECK_THAT(field.values[0], WithinRel(normOf(-36.0, 17.5, -57.0), 1e-12));
    CHECK(field.location == thermal::FieldLocation::Element);
}

TEST_CASE("ThermalViz_ChannelsDescribeThemselvesWithTheCoreDescriptors", "[thermal][viz][source][units]") {
    // The location and the unit of a channel are POST's own descriptor values, read and not restated.
    struct Row {
        renderer::ThermalChannel channel;
        thermal::ThermalPostField field;
        const char* unit;
        thermal::FieldLocation location;
    };
    const std::array<Row, 3> rows{{{renderer::ThermalChannel::Temperature, thermal::ThermalPostField::Temperature, "K", thermal::FieldLocation::Node},
                                   {renderer::ThermalChannel::TemperatureGradientMagnitude, thermal::ThermalPostField::TemperatureGradient, "K/m",
                                    thermal::FieldLocation::Element},
                                   {renderer::ThermalChannel::HeatFluxMagnitude, thermal::ThermalPostField::HeatFluxMagnitude, "W/m^2",
                                    thermal::FieldLocation::Element}}};
    REQUIRE(renderer::allThermalChannels().size() == rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const renderer::ThermalChannelDescriptor d = renderer::descriptorOf(rows[i].channel);
        INFO(renderer::toString(rows[i].channel));
        CHECK(renderer::allThermalChannels()[i] == rows[i].channel);
        CHECK(d.channel == rows[i].channel);
        CHECK(d.source == rows[i].field);
        CHECK(d.unit == rows[i].unit);
        CHECK(d.location == rows[i].location);
        CHECK(d.unit == thermal::descriptorOf(rows[i].field).unit);          // read from the core, not restated
        CHECK(d.location == thermal::descriptorOf(rows[i].field).location);
        CHECK_FALSE(d.mayBeNegative);
        CHECK_FALSE(d.name.empty());
    }
    WARN("channel table: Temperature K NODE; TemperatureGradientMagnitude K/m ELEMENT; HeatFluxMagnitude W/m^2 ELEMENT (POST's descriptors)");
}

// ===========================================================================
// The units never change
// ===========================================================================

TEST_CASE("ThermalViz_TemperatureRemainsKelvin", "[thermal][viz][units][temperature]") {
    // 350 K stays 350 K: not 76.85 (Celsius), not 623.15, not 0.35.
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {1, 2, 3, 4});
    const HandPost h = makeHandPost(std::move(hand), kK, {350.0, 350.0, 280.0, 350.0});
    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::Temperature);
    CHECK(field.maximum == 350.0);
    CHECK(field.minimum == 280.0);
    CHECK(renderer::descriptorOf(renderer::ThermalChannel::Temperature).unit == "K");
    for (const double v : field.values) {
        CHECK(v >= 280.0);
        CHECK(v <= 350.0);
    }
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const auto legend = renderer::thermalContourLegend(field, 8);
    REQUIRE(legend.has_value());
    CHECK(legend->unit == "K");
    CHECK(legend->minimum == 280.0);
    CHECK(legend->maximum == 350.0);
    const std::string title = renderer::thermalLegendTitle(*legend, std::nullopt);
    CHECK(title == "Temperature [K]");
    CHECK_THAT(title, !ContainsSubstring("C]"));
    WARN("temperature stays kelvin: source 350 / 280 K -> field 350 / 280 K -> legend range [280, 350] K, title \"Temperature [K]\"");
}

TEST_CASE("ThermalViz_GradientRemainsKelvinPerMetre", "[thermal][viz][units][gradient]") {
    // A gradient of exactly 10 K/m: T = 300 + 10 x on the unit tetrahedron. The engineering value is 10 K/m. It is not
    // 0.01 K/mm because the display happens to be in millimetres.
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {1, 2, 3, 4});
    const HandPost h = makeHandPost(std::move(hand), kK, {300.0, 310.0, 300.0, 300.0});
    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::TemperatureGradientMagnitude);
    REQUIRE(field.values.size() == 1);
    CHECK_THAT(field.values[0], WithinAbs(10.0, 1e-11));
    CHECK(field.values[0] > 1.0);   // 0.01 would be K/mm
    CHECK(renderer::descriptorOf(renderer::ThermalChannel::TemperatureGradientMagnitude).unit == "K/m");
    const auto legend = renderer::thermalContourLegend(field, 4);
    REQUIRE(legend.has_value());
    CHECK(legend->unit == "K/m");
    CHECK(renderer::thermalLegendTitle(*legend, std::nullopt) == "Temperature Gradient Magnitude [K/m]");
    // The inspector says the same.
    const Result<renderer::ThermalElementInspection> inspected =
        renderer::inspectThermalElement(h.hand.mesh, h.post, 0, h.post.elementIds()[0]);
    REQUIRE(inspected.has_value());
    CHECK_THAT(inspected->gradientMagnitude.si(), WithinAbs(10.0, 1e-11));
    WARN("gradient stays K/m: source 10 K/m -> field 10 -> legend unit K/m -> inspector 10 (not 0.01 K/mm)");
}

TEST_CASE("ThermalViz_HeatFluxRemainsWattPerSquareMetre", "[thermal][viz][units][heatflux]") {
    // k = 5 W/(m K) and a gradient of 10 K/m: |q| = 50 W/m^2. Not 0.05 W/mm^2.
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {1, 2, 3, 4});
    const HandPost h = makeHandPost(std::move(hand), kK, {300.0, 310.0, 300.0, 300.0});
    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::HeatFluxMagnitude);
    REQUIRE(field.values.size() == 1);
    CHECK_THAT(field.values[0], WithinAbs(50.0, 1e-10));
    CHECK(renderer::descriptorOf(renderer::ThermalChannel::HeatFluxMagnitude).unit == "W/m^2");
    const Result<renderer::ThermalElementInspection> inspected =
        renderer::inspectThermalElement(h.hand.mesh, h.post, 0, h.post.elementIds()[0]);
    REQUIRE(inspected.has_value());
    CHECK_THAT(inspected->heatFluxMagnitude.si(), WithinAbs(50.0, 1e-10));
    CHECK_THAT(inspected->heatFlux.x.si(), WithinAbs(-50.0, 1e-10));   // down the gradient: heat flows toward the cold
}

TEST_CASE("ThermalViz_EverythingLeavingThisLayerIsSiMetres", "[thermal][viz][units][metres]") {
    // THE THREE SPACES. The mesh and every position the viewer layer produces are SI METRES; OCCT model space is
    // millimetres; the single conversion lives in the presentation adapter (`toModelLength`, which is
    // `geometry::occt::toModel`) and is exercised by the offscreen tests, because nothing in the public API returns a
    // model-space coordinate -- by design (P17-VIZ-001's UNIT_BOUNDARY.md): an accessor for tests only is forbidden.
    //
    // WHAT CAN BE HELD HEADLESSLY IS THAT NOTHING HERE CONVERTS. The unit tetrahedron is ONE METRE across: if this
    // layer secretly produced millimetres, its centroid would be 250 and not 0.25, and the rest of the test would
    // not notice, so the sizes are asserted first.
    const HandPost h = affinePost();
    const meshing::Node* far = h.hand.mesh.findNode(meshing::NodeId::fromValue(104));
    REQUIRE(far != nullptr);
    REQUIRE(far->position.x.si() == 1.0);   // metres, in the canonical mesh

    const auto glyphs = renderer::heatFluxVectorGlyphs(h.hand.mesh, h.post, 0);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == 1);
    // The centroid of (0,0,0), (1,0,0), (0,1,0), (0,0,1) is (0.25, 0.25, 0.25) -- metres, exactly.
    CHECK((*glyphs)[0].centroid.x.si() == 0.25);
    CHECK((*glyphs)[0].centroid.y.si() == 0.25);
    CHECK((*glyphs)[0].centroid.z.si() == 0.25);

    // An overlay segment's head is the centroid advanced by `length` along the direction: still metres.
    const auto overlay = renderer::heatFluxVectorOverlay(*glyphs, 0.5, kK * normOf(kB, kC, kD));
    REQUIRE(overlay.has_value());
    REQUIRE(overlay->segments.size() == renderer::kThermalSegmentsPerArrow);
    const renderer::ThermalOverlaySegment& shaft = overlay->segments.front();
    CHECK_THAT(shaft.from.x.si(), WithinAbs(0.25, 1e-15));   // the tail is the centroid, in metres
    CHECK_THAT(shaft.from.y.si(), WithinAbs(0.25, 1e-15));
    CHECK_THAT(shaft.from.z.si(), WithinAbs(0.25, 1e-15));
    const double length = normOf(shaft.to.x.si() - 0.25, shaft.to.y.si() - 0.25, shaft.to.z.si() - 0.25);
    CHECK_THAT(length, WithinAbs(0.5, 1e-12));   // scale 0.5 m for the longest arrow, in metres

    // The mesh view's buffer is the canonical coordinate, component for component.
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    for (std::size_t v = 0; v < view.vertexCount(); ++v) {
        const Result<meshing::NodeId> node = view.nodeOfVertex(v);
        REQUIRE(node.has_value());
        const meshing::Node* canonical = h.hand.mesh.findNode(*node);
        REQUIRE(canonical != nullptr);
        CHECK(view.positions()[(3 * v) + 0] == canonical->position.x.si());
        CHECK(view.positions()[(3 * v) + 1] == canonical->position.y.si());
        CHECK(view.positions()[(3 * v) + 2] == canonical->position.z.si());
    }
    WARN("layer output is SI metres: unit tetrahedron 1 m across, centroid 0.25 m (not 250), arrow 0.5 m (not 500), view buffer == canonical node positions");
}
// ===========================================================================
// Cardinality: no zero fill, no truncation, no non-finite value
// ===========================================================================

namespace {

struct Corruptible {
    HandPost h;
    std::vector<meshing::NodeId> nodes;
    std::vector<meshing::ElementId> elements;
    thermal::ThermalPostState state;
};

[[nodiscard]] Corruptible corruptible() {
    HandPost h = fourGradientsPost();
    Corruptible out{std::move(h), {}, {}, {}};
    out.nodes.assign(out.h.hand.dofs.nodes().begin(), out.h.hand.dofs.nodes().end());
    out.elements.assign(out.h.post.elementIds().begin(), out.h.post.elementIds().end());
    out.state = out.h.post.states()[0];
    return out;
}

} // namespace

TEST_CASE("ThermalViz_RejectsTemperatureCardinalityMismatch", "[thermal][viz][cardinality][temperature]") {
    Corruptible c = corruptible();
    REQUIRE(renderer::classifyThermalState(renderer::ThermalChannel::Temperature, c.state, c.nodes, c.elements, 0).has_value());

    // SHORT: one temperature missing. NEVER filled with zero (a zero would be a valid Kelvin-less 0 K and a field of
    // silence would read as a valid uniform result), never padded, never read past its end.
    thermal::ThermalPostState shortState = c.state;
    shortState.temperatures.pop_back();
    const auto shortField = renderer::classifyThermalState(renderer::ThermalChannel::Temperature, shortState, c.nodes, c.elements, 0);
    REQUIRE_FALSE(shortField.has_value());
    CHECK_THAT(shortField.error().message, ContainsSubstring("cardinality_mismatch"));
    CHECK_THAT(shortField.error().message, ContainsSubstring("15 temperatures for 16 nodes"));

    // LONG: one extra. Never truncated to fit.
    thermal::ThermalPostState longState = c.state;
    longState.temperatures.push_back(Temperature::fromSi(300.0));
    const auto longField = renderer::classifyThermalState(renderer::ThermalChannel::Temperature, longState, c.nodes, c.elements, 0);
    REQUIRE_FALSE(longField.has_value());
    CHECK_THAT(longField.error().message, ContainsSubstring("17 temperatures for 16 nodes"));

    // EMPTY: no temperature at all.
    thermal::ThermalPostState emptyState = c.state;
    emptyState.temperatures.clear();
    CHECK_FALSE(renderer::classifyThermalState(renderer::ThermalChannel::Temperature, emptyState, c.nodes, c.elements, 0).has_value());
}

TEST_CASE("ThermalViz_RejectsElementGradientCardinalityMismatch", "[thermal][viz][cardinality][gradient]") {
    Corruptible c = corruptible();
    REQUIRE(renderer::classifyThermalState(renderer::ThermalChannel::TemperatureGradientMagnitude, c.state, c.nodes, c.elements, 0).has_value());
    thermal::ThermalPostState shortState = c.state;
    shortState.fields.gradients.pop_back();
    const auto field =
        renderer::classifyThermalState(renderer::ThermalChannel::TemperatureGradientMagnitude, shortState, c.nodes, c.elements, 0);
    REQUIRE_FALSE(field.has_value());
    CHECK_THAT(field.error().message, ContainsSubstring("cardinality_mismatch"));
    CHECK_THAT(field.error().message, ContainsSubstring("3 gradients"));
    thermal::ThermalPostState longState = c.state;
    longState.fields.gradients.push_back(thermal::TemperatureGradient3{});
    CHECK_FALSE(renderer::classifyThermalState(renderer::ThermalChannel::TemperatureGradientMagnitude, longState, c.nodes, c.elements, 0).has_value());
}

TEST_CASE("ThermalViz_RejectsHeatFluxCardinalityMismatch", "[thermal][viz][cardinality][heatflux]") {
    Corruptible c = corruptible();
    REQUIRE(renderer::classifyThermalState(renderer::ThermalChannel::HeatFluxMagnitude, c.state, c.nodes, c.elements, 0).has_value());
    // The magnitudes alone are short: the channel reads them, and a flux array of the right length does not excuse it.
    thermal::ThermalPostState shortMagnitudes = c.state;
    shortMagnitudes.fields.heatFluxMagnitudes.pop_back();
    const auto field = renderer::classifyThermalState(renderer::ThermalChannel::HeatFluxMagnitude, shortMagnitudes, c.nodes, c.elements, 0);
    REQUIRE_FALSE(field.has_value());
    CHECK_THAT(field.error().message, ContainsSubstring("3 flux magnitudes"));
    thermal::ThermalPostState shortVectors = c.state;
    shortVectors.fields.heatFluxes.pop_back();
    CHECK_FALSE(renderer::classifyThermalState(renderer::ThermalChannel::HeatFluxMagnitude, shortVectors, c.nodes, c.elements, 0).has_value());
}

TEST_CASE("ThermalViz_RejectsANonFiniteValueInsteadOfMappingItToAColour", "[thermal][viz][cardinality][finite]") {
    Corruptible c = corruptible();
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
        INFO("value " << bad);
        thermal::ThermalPostState t = c.state;
        t.temperatures[5] = Temperature::fromSi(bad);
        const auto temperature = renderer::classifyThermalState(renderer::ThermalChannel::Temperature, t, c.nodes, c.elements, 0);
        REQUIRE_FALSE(temperature.has_value());
        CHECK_THAT(temperature.error().message, ContainsSubstring("non_finite_value"));

        thermal::ThermalPostState g = c.state;
        g.fields.gradients[1].y = TemperatureGradient::fromSi(bad);
        CHECK_FALSE(renderer::classifyThermalState(renderer::ThermalChannel::TemperatureGradientMagnitude, g, c.nodes, c.elements, 0).has_value());

        thermal::ThermalPostState q = c.state;
        q.fields.heatFluxMagnitudes[2] = HeatFlux::fromSi(bad);
        CHECK_FALSE(renderer::classifyThermalState(renderer::ThermalChannel::HeatFluxMagnitude, q, c.nodes, c.elements, 0).has_value());
    }
}

TEST_CASE("ThermalViz_AnEmptyDomainIsNotAUniformField", "[thermal][viz][cardinality][trap]") {
    // THE P16 TRAP. A classifier handed zero entities must not return a complete, valid, uniform field.
    Corruptible c = corruptible();
    const auto noNodes = renderer::classifyThermalState(renderer::ThermalChannel::Temperature, c.state, {}, c.elements, 0);
    REQUIRE_FALSE(noNodes.has_value());
    CHECK_THAT(noNodes.error().message, ContainsSubstring("empty_classification_domain"));
    const auto noElements = renderer::classifyThermalState(renderer::ThermalChannel::HeatFluxMagnitude, c.state, c.nodes, {}, 0);
    REQUIRE_FALSE(noElements.has_value());
    CHECK_THAT(noElements.error().message, ContainsSubstring("empty_classification_domain"));

    // And a hand-built field that classified nothing is not complete, so no legend, no extreme and no colour.
    renderer::ThermalScalarField nothing;
    nothing.channel = renderer::ThermalChannel::Temperature;
    CHECK_FALSE(nothing.complete());
    CHECK_FALSE(nothing.uniform());
    CHECK_FALSE(renderer::thermalExtremeOf(nothing, renderer::ThermalExtremeKind::Maximum).has_value());
    CHECK_FALSE(renderer::thermalContourLegend(nothing, 8).has_value());
}

// ===========================================================================
// Sources: a result belongs to the mesh it was recovered on
// ===========================================================================

TEST_CASE("ThermalViz_ARecoveredResultIsBoundToItsOwnMeshAndNumbering", "[thermal][viz][source]") {
    const HandPost h = fourGradientsPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const Result<renderer::ThermalResultView> result = renderer::ThermalResultView::of(h.hand.mesh, view, h.hand.dofs, h.post);
    REQUIRE(result.has_value());
    CHECK(result->describes(h.hand.mesh, h.hand.dofs, h.post));
    CHECK(result->nodeCount() == 16);
    CHECK(result->elementCount() == 4);
    CHECK(result->stateCount() == 1);
    CHECK(result->domainSize(thermal::FieldLocation::Node) == 16);
    CHECK(result->domainSize(thermal::FieldLocation::Element) == 4);

    // ANOTHER MESH WITH THE SAME COUNTS: sixteen nodes, four tetrahedra, the same handles -- and a different generation.
    const HandPost twin = fourGradientsPost();
    REQUIRE(twin.hand.mesh.nodes().size() == h.hand.mesh.nodes().size());
    REQUIRE(twin.hand.mesh.tetrahedra().size() == h.hand.mesh.tetrahedra().size());
    CHECK_FALSE(twin.hand.mesh.stamp() == h.hand.mesh.stamp());
    CHECK_FALSE(result->describes(twin.hand.mesh, twin.hand.dofs, twin.post));
    // ... and the twin's result cannot be rendered through the first mesh and numbering.
    CHECK_FALSE(renderer::ThermalResultView::of(h.hand.mesh, view, h.hand.dofs, twin.post).has_value());
    const Result<renderer::ThermalScalarField> across = result->field(twin.hand.mesh, twin.hand.dofs, twin.post, 0, renderer::ThermalChannel::Temperature);
    REQUIRE_FALSE(across.has_value());
    CHECK_THAT(across.error().message, ContainsSubstring("result_source_mismatch"));
    // The mesh view of one mesh is not the view of the other.
    CHECK_FALSE(renderer::ThermalResultView::of(twin.hand.mesh, view, twin.hand.dofs, twin.post).has_value());
}

TEST_CASE("ThermalViz_RejectsAStateThatIsNotStored", "[thermal][viz][source]") {
    const HandPost h = fourGradientsPost();
    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const Result<renderer::ThermalResultView> result = renderer::ThermalResultView::of(h.hand.mesh, view, h.hand.dofs, h.post);
    REQUIRE(result.has_value());
    const auto field = result->field(h.hand.mesh, h.hand.dofs, h.post, 1, renderer::ThermalChannel::Temperature);
    REQUIRE_FALSE(field.has_value());
    CHECK_THAT(field.error().message, ContainsSubstring("state_out_of_range"));
}

// ===========================================================================
// Extremes, searched in the source values, ties to the first entity
// ===========================================================================

TEST_CASE("ThermalViz_HighestTemperatureNavigation", "[thermal][viz][extrema][temperature]") {
    // The brief's vector [310, 350, 295, 350] K on nodes 7, 104, 9001, 12000.
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {7, 104, 9001, 12000});
    const HandPost h = makeHandPost(std::move(hand), kK, {310.0, 350.0, 295.0, 350.0});
    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::Temperature);
    const auto highest = renderer::thermalExtremeOf(field, renderer::ThermalExtremeKind::Maximum);
    REQUIRE(highest.has_value());
    CHECK(highest->value == 350.0);
    // TWO NODES SHARE 350 K; the FIRST in the numbering wins: node 104 (unknown 1), not node 12000 (unknown 3).
    CHECK(highest->index == 1);
    CHECK(highest->node == meshing::NodeId::fromValue(104));
    CHECK(highest->location == thermal::FieldLocation::Node);
    CHECK(field.maximumNode == meshing::NodeId::fromValue(104));
}

TEST_CASE("ThermalViz_LowestTemperatureNavigation", "[thermal][viz][extrema][temperature]") {
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {7, 104, 9001, 12000});
    const HandPost h = makeHandPost(std::move(hand), kK, {310.0, 350.0, 295.0, 350.0});
    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::Temperature);
    const auto lowest = renderer::thermalExtremeOf(field, renderer::ThermalExtremeKind::Minimum);
    REQUIRE(lowest.has_value());
    CHECK(lowest->value == 295.0);
    CHECK(lowest->index == 2);
    CHECK(lowest->node == meshing::NodeId::fromValue(9001));
    CHECK(field.minimumNode == meshing::NodeId::fromValue(9001));
    // Never zero: the field is seeded from its first value, and a field of kelvin never reports 0.0 as an extreme.
    CHECK(lowest->value > 0.0);
}

TEST_CASE("ThermalViz_HighestHeatFluxNavigation", "[thermal][viz][extrema][heatflux]") {
    // |q| = [5, 12, 7, 12] W/m^2 on four tetrahedra whose ids are their ordinals plus sixteen.
    const HandPost h = fourGradientsPost();
    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::HeatFluxMagnitude);
    const std::array<double, 4> typed{5.0, 12.0, 7.0, 12.0};
    REQUIRE(field.values.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK_THAT(field.values[i], WithinAbs(typed[i], 1e-11));
    }
    const auto highest = renderer::thermalExtremeOf(field, renderer::ThermalExtremeKind::Maximum);
    REQUIRE(highest.has_value());
    CHECK_THAT(highest->value, WithinAbs(12.0, 1e-11));
    // TIE between the second and the fourth: the FIRST in POST's element order. The element is found by IDENTITY.
    CHECK(highest->index == 1);
    CHECK(highest->element == h.post.elementIds()[1]);
    CHECK(highest->element.value() == 18);                     // ordinal 1, after sixteen boundary triangles: ids 17..20
    CHECK(highest->element != h.post.elementIds()[3]);
    CHECK(highest->location == thermal::FieldLocation::Element);
    CHECK(highest->node == meshing::NodeId{});
    const auto lowest = renderer::thermalExtremeOf(field, renderer::ThermalExtremeKind::Minimum);
    REQUIRE(lowest.has_value());
    CHECK_THAT(lowest->value, WithinAbs(5.0, 1e-11));
    CHECK(lowest->element == h.post.elementIds()[0]);
}

TEST_CASE("ThermalViz_ExtremaTieBreakDeterministic", "[thermal][viz][extrema][determinism]") {
    // The same field, asked repeatedly and in the reverse construction order: the same entity every time, and it is the
    // first in the domain's ascending order -- not whichever an unordered container or a sort happened to put first.
    const HandPost h = fourGradientsPost();
    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::HeatFluxMagnitude);
    std::optional<renderer::ThermalExtreme> first;
    for (int run = 0; run < 25; ++run) {
        const auto highest = renderer::thermalExtremeOf(field, renderer::ThermalExtremeKind::Maximum);
        REQUIRE(highest.has_value());
        if (!first.has_value()) {
            first = *highest;
        }
        CHECK(*highest == *first);
    }
    // A uniform field: every entity ties, and the first wins for both kinds.
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {7, 104, 9001, 12000});
    const HandPost flat = makeHandPost(std::move(hand), kK, {320.0, 320.0, 320.0, 320.0});
    const renderer::ThermalScalarField uniform = fieldOf(flat, renderer::ThermalChannel::Temperature);
    CHECK(uniform.uniform());
    const auto hi = renderer::thermalExtremeOf(uniform, renderer::ThermalExtremeKind::Maximum);
    const auto lo = renderer::thermalExtremeOf(uniform, renderer::ThermalExtremeKind::Minimum);
    REQUIRE(hi.has_value());
    REQUIRE(lo.has_value());
    CHECK(hi->node == meshing::NodeId::fromValue(7));
    CHECK(lo->node == meshing::NodeId::fromValue(7));
    CHECK(hi->index == 0);
    CHECK(lo->index == 0);
}

// ===========================================================================
// Read-only
// ===========================================================================

TEST_CASE("ThermalViz_DoesNotMutateThermalResult", "[thermal][viz][readonly]") {
    HandPost h = fourGradientsPost();
    const thermal::ThermalPostResult postBefore = h.post;
    const meshing::Mesh meshBefore = h.hand.mesh;

    const renderer::MeshView view = byTetrahedron(h.hand.mesh);
    const Result<renderer::ThermalResultView> result = renderer::ThermalResultView::of(h.hand.mesh, view, h.hand.dofs, h.post);
    REQUIRE(result.has_value());
    for (const renderer::ThermalChannel channel : renderer::allThermalChannels()) {
        const auto field = result->field(h.hand.mesh, h.hand.dofs, h.post, 0, channel);
        REQUIRE(field.has_value());
        const auto legend = renderer::thermalContourLegend(*field, 8);
        REQUIRE(legend.has_value());
        const auto colours = renderer::thermalContourCornerColours(view, *field, *legend);
        REQUIRE(colours.has_value());
        REQUIRE(renderer::thermalExtremeOf(*field, renderer::ThermalExtremeKind::Maximum).has_value());
    }
    const auto glyphs = renderer::heatFluxVectorGlyphs(h.hand.mesh, h.post, 0);
    REQUIRE(glyphs.has_value());
    REQUIRE(renderer::heatFluxVectorOverlay(*glyphs, 0.5, 12.0).has_value());
    REQUIRE(renderer::inspectThermalElement(h.hand.mesh, h.post, 0, h.post.elementIds()[2]).has_value());
    REQUIRE(renderer::inspectThermalNode(h.hand.mesh, h.hand.dofs, h.post, 0, h.hand.dofs.nodes()[3]).has_value());
    const Result<renderer::ThermalPlayback> playback = renderer::ThermalPlayback::of(h.post);
    REQUIRE(playback.has_value());

    // NOTHING WAS TOUCHED: the result, the mesh and the numbering compare equal to their copies, bit for bit.
    CHECK(h.post == postBefore);
    CHECK(h.hand.mesh == meshBefore);
    CHECK(h.post.states()[0].temperatures.size() == postBefore.states()[0].temperatures.size());
    for (std::size_t i = 0; i < postBefore.states()[0].fields.heatFluxes.size(); ++i) {
        // q was never normalised in place for a glyph and never sign-flipped.
        CHECK(h.post.states()[0].fields.heatFluxes[i] == postBefore.states()[0].fields.heatFluxes[i]);
        CHECK(h.post.states()[0].fields.heatFluxMagnitudes[i] == postBefore.states()[0].fields.heatFluxMagnitudes[i]);
    }
}

TEST_CASE("ThermalViz_FieldExtractionIsDeterministic", "[thermal][viz][determinism]") {
    const HandPost h = fourGradientsPost();
    for (const renderer::ThermalChannel channel : renderer::allThermalChannels()) {
        const renderer::ThermalScalarField a = fieldOf(h, channel);
        const renderer::ThermalScalarField b = fieldOf(h, channel);
        INFO(renderer::toString(channel));
        CHECK(a == b);
    }
}

TEST_CASE("ThermalViz_NavigationLooksAtTheEntityThatHoldsTheExtreme", "[thermal][viz][extrema][navigation]") {
    // The hottest node of the brief's field is node 104 at (1, 0, 0); the highest-flux tetrahedron of the four is the second,
    // translated by 10 m along x, whose centroid is (10.25, 0.25, 0.25).
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {7, 104, 9001, 12000});
    const HandPost h = makeHandPost(std::move(hand), kK, {310.0, 350.0, 295.0, 350.0});
    const renderer::ThermalScalarField t = fieldOf(h, renderer::ThermalChannel::Temperature);
    const auto hottest = renderer::thermalExtremeOf(t, renderer::ThermalExtremeKind::Maximum);
    REQUIRE(hottest.has_value());
    const Result<Point3D> where = renderer::thermalNavigationTarget(h.hand.mesh, *hottest);
    REQUIRE(where.has_value());
    CHECK(where->x.si() == 1.0);
    CHECK(where->y.si() == 0.0);
    CHECK(where->z.si() == 0.0);
    const HandPost four = fourGradientsPost();
    const renderer::ThermalScalarField q = fieldOf(four, renderer::ThermalChannel::HeatFluxMagnitude);
    const auto strongest = renderer::thermalExtremeOf(q, renderer::ThermalExtremeKind::Maximum);
    REQUIRE(strongest.has_value());
    const Result<Point3D> centre = renderer::thermalNavigationTarget(four.hand.mesh, *strongest);
    REQUIRE(centre.has_value());
    CHECK_THAT(centre->x.si(), WithinAbs(10.25, 1e-12));
    CHECK_THAT(centre->y.si(), WithinAbs(0.25, 1e-12));
    CHECK_THAT(centre->z.si(), WithinAbs(0.25, 1e-12));
    // An extreme from ANOTHER mesh generation names nothing here: refused, not guessed.
    renderer::ThermalExtreme foreign = *strongest;
    foreign.element = meshing::ElementId::fromValue(987654);
    CHECK_FALSE(renderer::thermalNavigationTarget(four.hand.mesh, foreign).has_value());
    renderer::ThermalExtreme lost = *hottest;
    lost.node = meshing::NodeId::fromValue(2);
    CHECK_FALSE(renderer::thermalNavigationTarget(h.hand.mesh, lost).has_value());
}
TEST_CASE("ThermalViz_TheLowestValueTieAlsoResolvesToTheFirstEntity", "[thermal][viz][extrema][ties]") {
    // 295 K at nodes 7 and 9001 (unknowns 0 and 2), 350 K at nodes 104 and 12000: both extremes tie, and the FIRST entity of the
    // numbering wins for both -- in the classifier's own record of the extremes as well as in the extreme query.
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {7, 104, 9001, 12000});
    const HandPost h = makeHandPost(std::move(hand), kK, {295.0, 350.0, 295.0, 350.0});
    const renderer::ThermalScalarField field = fieldOf(h, renderer::ThermalChannel::Temperature);
    CHECK(field.minimumNode == meshing::NodeId::fromValue(7));
    CHECK(field.maximumNode == meshing::NodeId::fromValue(104));
    const auto lowest = renderer::thermalExtremeOf(field, renderer::ThermalExtremeKind::Minimum);
    const auto highest = renderer::thermalExtremeOf(field, renderer::ThermalExtremeKind::Maximum);
    REQUIRE(lowest.has_value());
    REQUIRE(highest.has_value());
    CHECK(lowest->index == 0);
    CHECK(lowest->node == meshing::NodeId::fromValue(7));
    CHECK(highest->index == 1);
    CHECK(highest->node == meshing::NodeId::fromValue(104));
}

TEST_CASE("ThermalViz_VectorGlyphsCarryTheOrdinalAndNotTheRawElementId", "[thermal][viz][vectors][sparse]") {
    // Tetrahedra ids 17..20 (sixteen boundary triangles come first), compact ordinals 0..3.
    const HandPost h = fourGradientsPost();
    const auto glyphs = renderer::heatFluxVectorGlyphs(h.hand.mesh, h.post, 0);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == 4);
    for (std::size_t i = 0; i < glyphs->size(); ++i) {
        CHECK((*glyphs)[i].ordinal == i);
        CHECK((*glyphs)[i].element == h.post.elementIds()[i]);
        CHECK((*glyphs)[i].element.value() == 17 + i);
    }
}
