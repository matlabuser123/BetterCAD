// P18-VIZ-001 -- the boundary glyphs: applied heat flux, prescribed temperature, convection, named sets; the
// recovered heat-flux vectors; and the classifier cardinality that stands behind every one of them.
//
// THREE DIFFERENT THINGS ARE DRAWN AND THEY ARE NOT THE SAME THING: the recovered conductive flux q (a vector field
// inside the solid, q = -k grad(T), pointing where heat FLOWS), the applied surface flux q'' (a scalar heat input at a
// boundary, POSITIVE INTO THE BODY) and a convection exchange. The tests hold each to its own convention and show that
// the first two point in OPPOSITE directions on a face where heat leaves.
//
// EVERY EXPECTED VALUE IS THE TEST'S OWN: facet counts and areas by coordinates and a written-out cross product,
// directions typed in, a unit normal checked against the owning tetrahedron's fourth vertex. The viewer is never asked
// for the answer it is judged against.

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
using renderer::BoundaryClassStatus;
using renderer::BoundaryTargetKind;

namespace {

constexpr double kPlane = 1.0e-9;   // metres: coordinates of a face are on its plane to far better than this

[[nodiscard]] meshing::BoundaryFacetSet resolvedWith(std::uint32_t facets, bool resolved = true) {
    meshing::BoundaryFacetSet set;
    meshing::FaceResolution one;
    one.reference = FaceName{ObjectId::fromValue(1), FaceSelector{.role = FaceRole::StartCap}};
    one.state = resolved ? meshing::MappingState::Resolved : meshing::MappingState::Unresolved;
    if (resolved) {
        one.faces = {0};
    }
    for (std::uint32_t i = 0; i < facets; ++i) {
        one.facets.push_back(meshing::ElementId::fromValue(100U + i));
        set.facets.push_back(meshing::ElementId::fromValue(100U + i));
    }
    set.requested.push_back(std::move(one));
    return set;
}

/// The boundary triangles whose three corners lie on the plane `coordinate(axis) == value` -- the TEST'S OWN
/// classification by coordinates, independent of P16's mapping.
[[nodiscard]] std::vector<meshing::ElementId> trianglesOnPlane(const meshing::Mesh& mesh, int axis, double value) {
    std::vector<meshing::ElementId> out;
    for (const meshing::Triangle& t : mesh.triangles()) {
        bool on = true;
        for (const meshing::NodeId id : t.nodes) {
            const meshing::Node* node = mesh.findNode(id);
            const double c = axis == 0 ? node->position.x.si() : (axis == 1 ? node->position.y.si() : node->position.z.si());
            on = on && std::abs(c - value) < kPlane;
        }
        if (on) {
            out.push_back(t.id);
        }
    }
    return out;
}

[[nodiscard]] double areaOf(const meshing::Mesh& mesh, const std::vector<meshing::ElementId>& facets) {
    double total = 0.0;
    for (const meshing::ElementId id : facets) {
        const meshing::Triangle* t = mesh.findTriangle(id);
        REQUIRE(t != nullptr);
        total += triangleAreaByHand(mesh.findNode(t->nodes[0])->position, mesh.findNode(t->nodes[1])->position,
                                    mesh.findNode(t->nodes[2])->position);
    }
    return total;
}

struct Dir3 {
    double x, y, z;
};

[[nodiscard]] Dir3 unitOf(const Point3D& from, const Point3D& to) {
    const double dx = to.x.si() - from.x.si();
    const double dy = to.y.si() - from.y.si();
    const double dz = to.z.si() - from.z.si();
    const double n = std::sqrt(dx * dx + dy * dy + dz * dz);
    return {dx / n, dy / n, dz / n};
}

/// The side face of the block whose outward normal is the given unit axis direction, found by asking the mesh.
[[nodiscard]] FaceName sideFacing(const VizStudy& study, double x, double y, double z) {
    for (std::size_t k = 0; k < 4; ++k) {
        const Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(study.map(), study.side(k));
        REQUIRE(facets.has_value());
        REQUIRE_FALSE(facets->facets.empty());
        const Result<Vector3D> n = renderer::outwardNormalOf(study.mesh(), facets->facets.front());
        REQUIRE(n.has_value());
        if (std::abs(n->x - x) < 1e-12 && std::abs(n->y - y) < 1e-12 && std::abs(n->z - z) < 1e-12) {
            return study.side(k);
        }
    }
    FAIL("no side face has that outward normal");
    return study.side(0);
}

[[nodiscard]] renderer::MeshView boundaryViewOf(const VizStudy& study) {
    Result<renderer::MeshView> view = renderer::MeshView::volumeBoundaryOf(study.mesh());
    REQUIRE(view.has_value());
    return std::move(*view);
}

} // namespace

// ===========================================================================
// The classifier cardinality
// ===========================================================================

TEST_CASE("ThermalViz_ClassifierCardinalityMatchesExpected", "[thermal][viz][classifier][cardinality]") {
    VizStudy study;
    const FaceName hotSide = sideFacing(study, 1.0, 0.0, 0.0);
    define(study, steadyDefinition(study, {surfaceLoad(1, hotSide, 1000.0), generationLoad(2, 1.0e4)},
                                   {temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const renderer::MeshView view = boundaryViewOf(study);
    const meshing::Mesh& mesh = study.mesh();

    // The test's own classification by coordinates: the start cap is z = 0, the end cap z = 0.02, the hot side x = 0.04.
    const auto capStart = trianglesOnPlane(mesh, 2, 0.0);
    const auto capEnd = trianglesOnPlane(mesh, 2, 0.020);
    const auto sideHot = trianglesOnPlane(mesh, 0, 0.040);
    REQUIRE_FALSE(capStart.empty());
    CHECK(capStart.size() == capEnd.size());
    CHECK_THAT(areaOf(mesh, capStart), WithinAbs(kCapArea, 1e-12));   // 40 x 30 mm, by a written-out cross product
    CHECK_THAT(areaOf(mesh, sideHot), WithinAbs(0.030 * 0.020, 1e-12));

    const auto applied = renderer::appliedHeatFluxGlyphs(view, mesh, study.map(), def.loads, *derived->loads);
    REQUIRE(applied.has_value());
    REQUIRE(applied->size() == 1);   // the volumetric load has no face and no glyph
    const renderer::AppliedHeatFluxGlyph& flux = applied->front();
    CHECK(flux.cardinality.kind == BoundaryTargetKind::AppliedHeatFlux);
    CHECK(flux.cardinality.expected == sideHot.size());     // P16's forward answer, against the coordinates
    CHECK(flux.cardinality.classified == sideHot.size());   // P18-LOAD's integration count
    CHECK(flux.cardinality.rendered == sideHot.size());     // the render triangles of the highlight
    CHECK(flux.cardinality.status == BoundaryClassStatus::Complete);
    CHECK(flux.cardinality.complete());
    CHECK(flux.facets == std::vector<meshing::ElementId>(sideHot.begin(), sideHot.end()));
    CHECK_THAT(flux.area.si(), WithinAbs(0.030 * 0.020, 1e-12));
    CHECK(flux.flux.si() == 1000.0);

    const auto held = renderer::prescribedTemperatureGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    REQUIRE(held.has_value());
    REQUIRE(held->size() == 1);
    CHECK(held->front().cardinality.expected == capStart.size());
    CHECK(held->front().cardinality.classified == capStart.size());
    CHECK(held->front().cardinality.rendered == capStart.size());
    CHECK(held->front().cardinality.complete());
    CHECK(held->front().value.si() == 350.0);   // kelvin; not 76.85

    const auto convecting = renderer::convectionGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    REQUIRE(convecting.has_value());
    REQUIRE(convecting->size() == 1);
    CHECK(convecting->front().cardinality.expected == capEnd.size());
    CHECK(convecting->front().cardinality.classified == capEnd.size());
    CHECK(convecting->front().cardinality.rendered == capEnd.size());
    CHECK(convecting->front().cardinality.complete());
    CHECK(convecting->front().coefficient.si() == 25.0);
    CHECK(convecting->front().ambient.si() == kAmbient);

    // A NAMED SET of three faces: the forward answer, the REVERSE linkage and the render triangles agree.
    meshing::NamedBoundarySet set;
    set.id = BoundarySetId::fromValue(1);
    set.name = "inlet";
    set.faces = {study.startCap(), hotSide, study.endCap()};
    const auto named = renderer::namedBoundaryHighlight(view, mesh, study.map(), set);
    REQUIRE(named.has_value());
    const std::size_t expected = capStart.size() + sideHot.size() + capEnd.size();
    CHECK(named->cardinality.kind == BoundaryTargetKind::NamedBoundarySet);
    CHECK(named->cardinality.expected == expected);
    CHECK(named->cardinality.classified == expected);   // counted from mesh to CAD, independently of the forward answer
    CHECK(named->cardinality.rendered == expected);
    CHECK(named->cardinality.complete());
    CHECK(named->highlight.fullyResolved());
    WARN(std::format("classifier cardinality: q'' {}/{}/{}, T {}/{}/{}, convection {}/{}/{}, named set {}/{}/{} (expected/classified/rendered)",
                     flux.cardinality.expected, flux.cardinality.classified, flux.cardinality.rendered,
                     held->front().cardinality.expected, held->front().cardinality.classified, held->front().cardinality.rendered,
                     convecting->front().cardinality.expected, convecting->front().cardinality.classified, convecting->front().cardinality.rendered,
                     named->cardinality.expected, named->cardinality.classified, named->cardinality.rendered));
}

TEST_CASE("ThermalViz_ZeroClassifiedEntitiesRejected", "[thermal][viz][classifier][zero]") {
    // THE P16 TRAP: the classifier SUCCEEDED and classified NOTHING. A mapping that resolved with zero facets, and a
    // core that integrated over zero facets of a mapping that has some, are failures -- never an empty highlight that
    // looks like a success.
    for (const BoundaryTargetKind kind : {BoundaryTargetKind::AppliedHeatFlux, BoundaryTargetKind::PrescribedTemperature,
                                          BoundaryTargetKind::Convection, BoundaryTargetKind::NamedBoundarySet}) {
        INFO(renderer::toString(kind));
        const renderer::BoundaryCardinality empty = renderer::classifyBoundaryCardinality(kind, 7, resolvedWith(0), 0, 0);
        CHECK(empty.status == BoundaryClassStatus::ZeroClassified);
        CHECK_FALSE(empty.complete());
        CHECK(empty.expected == 0);

        // The mapping has six facets and the OWNER of the physics integrated over none of them.
        const renderer::BoundaryCardinality notClassified = renderer::classifyBoundaryCardinality(kind, 7, resolvedWith(6), 0, 6);
        CHECK(notClassified.status == BoundaryClassStatus::ZeroClassified);
        CHECK_FALSE(notClassified.complete());
    }
    // The status names in the report.
    CHECK(renderer::toString(BoundaryClassStatus::ZeroClassified) == "zero_classified");
}

TEST_CASE("ThermalViz_PartialClassificationNotReportedComplete", "[thermal][viz][classifier][partial]") {
    const meshing::BoundaryFacetSet six = resolvedWith(6);
    const auto status = [&](std::size_t classified, std::size_t rendered) {
        return renderer::classifyBoundaryCardinality(BoundaryTargetKind::Convection, 3, six, classified, rendered).status;
    };
    CHECK(status(6, 6) == BoundaryClassStatus::Complete);
    // Fewer classified, or fewer rendered, than the mapping resolved: PARTIAL, whichever count is short.
    CHECK(status(5, 6) == BoundaryClassStatus::Partial);
    CHECK(status(6, 5) == BoundaryClassStatus::Partial);
    CHECK(status(5, 5) == BoundaryClassStatus::Partial);
    CHECK(status(1, 6) == BoundaryClassStatus::Partial);
    // MORE than the mapping resolved: the owners disagree.
    CHECK(status(7, 6) == BoundaryClassStatus::Inconsistent);
    CHECK(status(6, 7) == BoundaryClassStatus::Inconsistent);
    // Nothing resolved but something classified: inconsistent, not zero.
    CHECK(renderer::classifyBoundaryCardinality(BoundaryTargetKind::Convection, 3, resolvedWith(0), 3, 0).status == BoundaryClassStatus::Inconsistent);
    // A reference that does not resolve is reported as such and never as empty.
    const renderer::BoundaryCardinality unresolved = renderer::classifyBoundaryCardinality(BoundaryTargetKind::NamedBoundarySet, 3, resolvedWith(0, false), 0, 0);
    CHECK(unresolved.status == BoundaryClassStatus::Unresolved);
    CHECK_FALSE(unresolved.complete());
    // No reference at all is not "fully resolved" either.
    CHECK(renderer::classifyBoundaryCardinality(BoundaryTargetKind::NamedBoundarySet, 3, meshing::BoundaryFacetSet{}, 0, 0).status == BoundaryClassStatus::Unresolved);
    // Only Complete is complete; the accounting never lets a short count read as whole.
    for (std::size_t classified = 0; classified <= 8; ++classified) {
        for (std::size_t rendered = 0; rendered <= 8; ++rendered) {
            const renderer::BoundaryCardinality c = renderer::classifyBoundaryCardinality(BoundaryTargetKind::Convection, 3, six, classified, rendered);
            CHECK(c.complete() == (classified == 6 && rendered == 6));
        }
    }
}

TEST_CASE("ThermalViz_AnUnresolvedReferenceIsKeptAndNeverRebound", "[thermal][viz][classifier][unresolved]") {
    VizStudy study;
    const renderer::MeshView view = boundaryViewOf(study);
    // A face that does not exist: the set KEEPS the reference and reports it unresolved. It is not rebound to the nearest or
    // the most similar face, and it does not disappear.
    const FaceName missing{study.feature, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(987654)}};
    meshing::NamedBoundarySet set;
    set.id = BoundarySetId::fromValue(2);
    set.name = "gone";
    set.faces = {missing};
    const auto named = renderer::namedBoundaryHighlight(view, study.mesh(), study.map(), set);
    REQUIRE(named.has_value());
    CHECK(named->cardinality.status == BoundaryClassStatus::Unresolved);
    CHECK_FALSE(named->cardinality.complete());
    CHECK(named->highlight.empty());
    REQUIRE(named->highlight.mapping.requested.size() == 1);
    CHECK(named->highlight.mapping.requested[0].state == meshing::MappingState::Unresolved);
    CHECK(named->highlight.mapping.requested[0].facets.empty());
    CHECK_FALSE(named->highlight.fullyResolved());
    CHECK(named->cardinality.rendered == 0);

    // A set with one good face and one missing: the good one highlights, and the set is NOT complete.
    set.faces = {study.startCap(), missing};
    const auto half = renderer::namedBoundaryHighlight(view, study.mesh(), study.map(), set);
    REQUIRE(half.has_value());
    CHECK_FALSE(half->highlight.empty());
    CHECK(half->cardinality.status == BoundaryClassStatus::Unresolved);
    CHECK_FALSE(half->highlight.fullyResolved());
}

// ===========================================================================
// q'' and q are different things
// ===========================================================================

TEST_CASE("ThermalViz_PositiveAppliedHeatFluxArrowPointsIntoBody", "[thermal][viz][boundary][flux]") {
    VizStudy study;
    // The face whose outward normal is +x: q'' = +100 W/m^2 is heat INTO the body, so the arrow travels along -x.
    const FaceName plusX = sideFacing(study, 1.0, 0.0, 0.0);
    define(study, steadyDefinition(study, {surfaceLoad(1, plusX, 100.0)}, {temperatureBc(1, study.startCap(), 300.0)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->loads.has_value());
    const renderer::MeshView view = boundaryViewOf(study);
    const meshing::Mesh& mesh = study.mesh();
    const auto glyphs = renderer::appliedHeatFluxGlyphs(view, mesh, study.map(), study.definition().loads, *derived->loads);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == 1);
    CHECK(glyphs->front().hasDirection);
    const auto overlay = renderer::appliedHeatFluxOverlay(mesh, *glyphs, 0.01, 100.0);
    REQUIRE(overlay.has_value());
    const std::size_t facets = glyphs->front().facets.size();
    REQUIRE(overlay->glyphs == facets);
    REQUIRE(overlay->segments.size() == facets * renderer::kThermalSegmentsPerArrow);
    CHECK(overlay->skipped == 0);
    CHECK(overlay->role == renderer::ThermalOverlayRole::AppliedHeatFluxArrows);

    // Every arrow: the shaft is the first segment of its five; its HEAD is the facet centroid (the surface), its tail is OUTSIDE
    // (x > 0.04), and the direction tail -> head is (-1, 0, 0), the way the heat travels.
    for (std::size_t i = 0; i < facets; ++i) {
        const renderer::ThermalOverlaySegment& shaft = overlay->segments[i * renderer::kThermalSegmentsPerArrow];
        const Dir3 direction = unitOf(shaft.from, shaft.to);
        CHECK_THAT(direction.x, WithinAbs(-1.0, 1e-12));
        CHECK_THAT(direction.y, WithinAbs(0.0, 1e-12));
        CHECK_THAT(direction.z, WithinAbs(0.0, 1e-12));
        CHECK_THAT(shaft.to.x.si(), WithinAbs(0.040, 1e-12));      // the head is ON the +x face
        CHECK(shaft.from.x.si() > 0.040 + 1e-6);                   // the tail is outside the body
        CHECK_THAT(std::sqrt(std::pow(shaft.to.x.si() - shaft.from.x.si(), 2)), WithinAbs(0.01, 1e-12));   // |q''| / reference = 1
    }
    // The head lands exactly on a facet centroid, per facet.
    std::set<std::array<double, 3>> centroids;
    for (const meshing::ElementId f : glyphs->front().facets) {
        const Result<Point3D> c = renderer::facetCentroidOf(mesh, f);
        REQUIRE(c.has_value());
        centroids.insert({c->x.si(), c->y.si(), c->z.si()});
    }
    for (std::size_t i = 0; i < facets; ++i) {
        const Point3D& head = overlay->segments[i * renderer::kThermalSegmentsPerArrow].to;
        bool found = false;
        for (const auto& c : centroids) {
            found = found || (std::abs(c[0] - head.x.si()) < 1e-12 && std::abs(c[1] - head.y.si()) < 1e-12 && std::abs(c[2] - head.z.si()) < 1e-12);
        }
        CHECK(found);
    }
}

TEST_CASE("ThermalViz_NegativeAppliedHeatFluxArrowPointsOut", "[thermal][viz][boundary][flux]") {
    VizStudy study;
    const FaceName plusX = sideFacing(study, 1.0, 0.0, 0.0);
    define(study, steadyDefinition(study, {surfaceLoad(1, plusX, -100.0)}, {temperatureBc(1, study.startCap(), 300.0)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->loads.has_value());
    const renderer::MeshView view = boundaryViewOf(study);
    const auto glyphs = renderer::appliedHeatFluxGlyphs(view, study.mesh(), study.map(), study.definition().loads, *derived->loads);
    REQUIRE(glyphs.has_value());
    const auto overlay = renderer::appliedHeatFluxOverlay(study.mesh(), *glyphs, 0.01, 100.0);
    REQUIRE(overlay.has_value());
    REQUIRE_FALSE(overlay->empty());
    for (std::size_t i = 0; i < overlay->glyphs; ++i) {
        const renderer::ThermalOverlaySegment& shaft = overlay->segments[i * renderer::kThermalSegmentsPerArrow];
        const Dir3 direction = unitOf(shaft.from, shaft.to);
        // q'' = -100: heat leaves, so the arrow travels along +x -- out through the face -- and ends on it.
        CHECK_THAT(direction.x, WithinAbs(1.0, 1e-12));
        CHECK_THAT(shaft.to.x.si(), WithinAbs(0.040, 1e-12));
        CHECK(shaft.from.x.si() < 0.040 - 1e-6);   // the tail is INSIDE the body
    }

    // The two caps, whose outward normals are -z (start) and +z (end): a positive q'' travels along +z on the start cap and
    // along -z on the end cap -- the same convention on faces that face opposite ways.
    define(study, steadyDefinition(study, {surfaceLoad(1, study.startCap(), 40.0), surfaceLoad(2, study.endCap(), 40.0)},
                                   {temperatureBc(1, study.side(0), 300.0)}));
    const std::unique_ptr<Derived> d2 = derive(study);
    REQUIRE(d2->loads.has_value());
    const auto caps = renderer::appliedHeatFluxGlyphs(view, study.mesh(), study.map(), study.definition().loads, *d2->loads);
    REQUIRE(caps.has_value());
    REQUIRE(caps->size() == 2);
    const auto both = renderer::appliedHeatFluxOverlay(study.mesh(), *caps, 0.01, 40.0);
    REQUIRE(both.has_value());
    const std::size_t startFacets = (*caps)[0].facets.size();
    for (std::size_t i = 0; i < both->glyphs; ++i) {
        const Dir3 direction = unitOf(both->segments[i * renderer::kThermalSegmentsPerArrow].from, both->segments[i * renderer::kThermalSegmentsPerArrow].to);
        CHECK_THAT(direction.z, WithinAbs(i < startFacets ? 1.0 : -1.0, 1e-12));
    }
}

TEST_CASE("ThermalViz_AZeroAppliedFluxHasNoArrowAndNoInventedDirection", "[thermal][viz][boundary][flux][zero]") {
    VizStudy study;
    define(study, steadyDefinition(study, {surfaceLoad(1, study.endCap(), 0.0)}, {temperatureBc(1, study.startCap(), 300.0)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->loads.has_value());
    const renderer::MeshView view = boundaryViewOf(study);
    const auto glyphs = renderer::appliedHeatFluxGlyphs(view, study.mesh(), study.map(), study.definition().loads, *derived->loads);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == 1);
    CHECK_FALSE(glyphs->front().hasDirection);
    CHECK(glyphs->front().cardinality.complete());   // the target still resolves; only the direction is absent
    const auto overlay = renderer::appliedHeatFluxOverlay(study.mesh(), *glyphs, 0.01, 100.0);
    REQUIRE(overlay.has_value());
    CHECK(overlay->empty());
    CHECK(overlay->glyphs == 0);
    CHECK(overlay->skipped == glyphs->front().facets.size());
}

TEST_CASE("ThermalViz_HeatFluxVectorDirection", "[thermal][viz][vectors][direction]") {
    // The brief's flux q = (-36, 17.5, -57) W/m^2. The glyph is POST's vector; the arrow travels along q / |q|.
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {7, 104, 9001, 12000});
    const HandPost h = makeHandPost(std::move(hand), 5.0, {307.3, 307.3 + 7.2, 307.3 - 3.5, 307.3 + 11.4});
    const auto glyphs = renderer::heatFluxVectorGlyphs(h.hand.mesh, h.post, 0);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == 1);
    const renderer::HeatFluxVectorGlyph& g = glyphs->front();
    CHECK(g.hasDirection);
    const double magnitude = normOf(-36.0, 17.5, -57.0);
    CHECK_THAT(g.flux.x.si(), WithinAbs(-36.0, 1e-10));
    CHECK_THAT(g.flux.y.si(), WithinAbs(17.5, 1e-10));
    CHECK_THAT(g.flux.z.si(), WithinAbs(-57.0, 1e-10));
    CHECK_THAT(g.direction.x, WithinAbs(-36.0 / magnitude, 1e-12));
    CHECK_THAT(g.direction.y, WithinAbs(17.5 / magnitude, 1e-12));
    CHECK_THAT(g.direction.z, WithinAbs(-57.0 / magnitude, 1e-12));
    CHECK_THAT(normOf(g.direction.x, g.direction.y, g.direction.z), WithinAbs(1.0, 1e-14));

    const auto overlay = renderer::heatFluxVectorOverlay(*glyphs, 0.2, g.magnitude.si());
    REQUIRE(overlay.has_value());
    REQUIRE(overlay->segments.size() == renderer::kThermalSegmentsPerArrow);
    const renderer::ThermalOverlaySegment& shaft = overlay->segments.front();
    const Dir3 direction = unitOf(shaft.from, shaft.to);
    // THE DRAWN DIRECTION IS q / |q| -- not negated, not -k grad(T) re-derived: -36 in x means the arrow points toward -x.
    CHECK_THAT(direction.x, WithinAbs(-36.0 / magnitude, 1e-12));
    CHECK_THAT(direction.y, WithinAbs(17.5 / magnitude, 1e-12));
    CHECK_THAT(direction.z, WithinAbs(-57.0 / magnitude, 1e-12));
    CHECK(direction.x < 0.0);   // the sign, stated plainly
    CHECK(direction.y > 0.0);
    // The tail is the centroid and the head is `length` further along q.
    CHECK_THAT(shaft.from.x.si(), WithinAbs(0.25, 1e-12));
    CHECK_THAT(shaft.to.x.si() - shaft.from.x.si(), WithinAbs(0.2 * direction.x, 1e-12));
}

TEST_CASE("ThermalViz_RecoveredHeatFluxFlowsFromHotToColdOnARealSlab", "[thermal][viz][vectors][direction][slab]") {
    // 400 K on the start cap (z = 0) and 300 K on the end cap (z = 0.02): heat flows toward +z. Every vector glyph says so.
    VizStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const auto glyphs = renderer::heatFluxVectorGlyphs(study.mesh(), *derived->post, 0);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == study.mesh().tetrahedra().size());
    for (const renderer::HeatFluxVectorGlyph& g : *glyphs) {
        REQUIRE(g.hasDirection);
        CHECK(g.direction.z > 0.99999);   // down the gradient: from the hot cap toward the cold one
        CHECK(g.flux.z.si() > 0.0);
        // q = -k grad(T) with k = 45 and dT/dz = -100 K / 0.02 m = -5000 K/m: q_z = 225000 W/m^2, to rounding.
        CHECK_THAT(g.flux.z.si(), WithinAbs(45.0 * 5000.0, 1e-2));
    }
    const double reference = derived->post->states()[0].fields.heatFluxMagnitudes[0].si();
    const auto overlay = renderer::heatFluxVectorOverlay(*glyphs, 0.003, reference);
    REQUIRE(overlay.has_value());
    CHECK(overlay->glyphs == glyphs->size());
    for (std::size_t i = 0; i < overlay->glyphs; ++i) {
        const renderer::ThermalOverlaySegment& shaft = overlay->segments[i * renderer::kThermalSegmentsPerArrow];
        CHECK(shaft.to.z.si() > shaft.from.z.si());   // the head is further along +z than the tail
    }
}

TEST_CASE("ThermalViz_RecoveredQAndAppliedQDoublePrimeAreDistinct", "[thermal][viz][boundary][vectors][distinct]") {
    // On the END cap of the slab (outward normal +z) heat LEAVES: the recovered q points +z, out of the body. A POSITIVE
    // applied q'' on the same face is heat ENTERING, so its arrow travels along -z. Two quantities, opposite arrows, one face.
    VizStudy study;
    define(study, steadyDefinition(study, {surfaceLoad(1, study.endCap(), 500.0)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    REQUIRE(derived->loads.has_value());
    const renderer::MeshView view = boundaryViewOf(study);
    const auto applied = renderer::appliedHeatFluxGlyphs(view, study.mesh(), study.map(), study.definition().loads, *derived->loads);
    REQUIRE(applied.has_value());
    const auto appliedOverlay = renderer::appliedHeatFluxOverlay(study.mesh(), *applied, 0.005, 500.0);
    REQUIRE(appliedOverlay.has_value());
    REQUIRE_FALSE(appliedOverlay->empty());
    const Dir3 appliedDirection = unitOf(appliedOverlay->segments[0].from, appliedOverlay->segments[0].to);

    const auto vectors = renderer::heatFluxVectorGlyphs(study.mesh(), *derived->post, 0);
    REQUIRE(vectors.has_value());
    const renderer::HeatFluxVectorGlyph& recovered = vectors->front();
    // Opposite senses along z: the product of the two directions is negative.
    CHECK(appliedDirection.z < -0.99999);
    CHECK(recovered.direction.z > 0.99999);
    CHECK(appliedDirection.z * recovered.direction.z < 0.0);

    // And they are different overlays with different roles and different colours downstream.
    const auto vectorOverlay = renderer::heatFluxVectorOverlay(*vectors, 0.003, recovered.magnitude.si());
    REQUIRE(vectorOverlay.has_value());
    CHECK(vectorOverlay->role == renderer::ThermalOverlayRole::HeatFluxVectors);
    CHECK(appliedOverlay->role == renderer::ThermalOverlayRole::AppliedHeatFluxArrows);
    CHECK(vectorOverlay->role != appliedOverlay->role);
    WARN("q vs q'': on the end cap (n_out = +z) the recovered q travels +z (heat leaves) and a positive applied q'' travels -z (heat enters)");
}

TEST_CASE("ThermalViz_ZeroHeatFluxVector", "[thermal][viz][vectors][zero]") {
    // A uniform temperature: grad T = 0, q = 0. No direction, no arrow, no NaN.
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {7, 104, 9001, 12000});
    const HandPost h = makeHandPost(std::move(hand), 5.0, {320.0, 320.0, 320.0, 320.0});
    const auto glyphs = renderer::heatFluxVectorGlyphs(h.hand.mesh, h.post, 0);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == 1);
    CHECK_FALSE(glyphs->front().hasDirection);
    CHECK(glyphs->front().direction == Vector3D{});
    CHECK(glyphs->front().magnitude.si() == 0.0);
    const auto overlay = renderer::heatFluxVectorOverlay(*glyphs, 0.1, 1.0);
    REQUIRE(overlay.has_value());
    CHECK(overlay->empty());
    CHECK(overlay->skipped == 1);
    CHECK(overlay->glyphs == 0);
}

TEST_CASE("ThermalViz_VectorScalingDoesNotMutateResult", "[thermal][viz][vectors][readonly]") {
    const HandPost h = [] {
        const std::array<std::array<double, 3>, 4> g{{{3.0, 4.0, 0.0}, {0.0, 12.0, 0.0}, {2.0, 3.0, 6.0}, {0.0, 0.0, 12.0}}};
        std::vector<double> kelvin;
        for (const auto& v : g) {
            kelvin.insert(kelvin.end(), {300.0, 300.0 + v[0], 300.0 + v[1], 300.0 + v[2]});
        }
        return makeHandPost(fourTetrahedraHand(), 1.0, kelvin);
    }();
    const thermal::ThermalPostResult before = h.post;
    const auto glyphs = renderer::heatFluxVectorGlyphs(h.hand.mesh, h.post, 0);
    REQUIRE(glyphs.has_value());
    const auto small = renderer::heatFluxVectorOverlay(*glyphs, 0.1, 12.0);
    const auto large = renderer::heatFluxVectorOverlay(*glyphs, 0.4, 12.0);
    REQUIRE(small.has_value());
    REQUIRE(large.has_value());
    // The display length is proportional to the scale and to |q|: the longest arrow (|q| = 12) is exactly `scale` long.
    double longestSmall = 0.0;
    double longestLarge = 0.0;
    for (std::size_t i = 0; i < small->glyphs; ++i) {
        const auto& s = small->segments[i * renderer::kThermalSegmentsPerArrow];
        const auto& l = large->segments[i * renderer::kThermalSegmentsPerArrow];
        longestSmall = std::max(longestSmall, normOf(s.to.x.si() - s.from.x.si(), s.to.y.si() - s.from.y.si(), s.to.z.si() - s.from.z.si()));
        longestLarge = std::max(longestLarge, normOf(l.to.x.si() - l.from.x.si(), l.to.y.si() - l.from.y.si(), l.to.z.si() - l.from.z.si()));
    }
    CHECK_THAT(longestSmall, WithinAbs(0.1, 1e-12));
    CHECK_THAT(longestLarge, WithinAbs(0.4, 1e-12));
    // THE RESULT IS EXACTLY WHAT IT WAS, and the glyph's stored flux is POST's, not a display-scaled copy.
    CHECK(h.post == before);
    CHECK((*glyphs)[2].flux == before.states()[0].fields.heatFluxes[2]);
    CHECK_THAT((*glyphs)[2].magnitude.si(), WithinAbs(7.0, 1e-11));   // the physical |q| of the 2-3-6 tetrahedron: not 7 * 0.1/12
    // A refused scale or reference changes nothing either.
    CHECK_FALSE(renderer::heatFluxVectorOverlay(*glyphs, 0.0, 12.0).has_value());
    CHECK_FALSE(renderer::heatFluxVectorOverlay(*glyphs, -1.0, 12.0).has_value());
    CHECK_FALSE(renderer::heatFluxVectorOverlay(*glyphs, 0.1, 0.0).has_value());
    CHECK_FALSE(renderer::heatFluxVectorOverlay(*glyphs, std::numeric_limits<double>::quiet_NaN(), 12.0).has_value());
    CHECK(h.post == before);
}

TEST_CASE("ThermalViz_TheDisplayThresholdIsReadabilityNotPhysics", "[thermal][viz][vectors][threshold]") {
    const std::array<std::array<double, 3>, 4> g{{{3.0, 4.0, 0.0}, {0.0, 12.0, 0.0}, {2.0, 3.0, 6.0}, {0.0, 0.0, 12.0}}};
    std::vector<double> kelvin;
    for (const auto& v : g) {
        kelvin.insert(kelvin.end(), {300.0, 300.0 + v[0], 300.0 + v[1], 300.0 + v[2]});
    }
    const HandPost h = makeHandPost(fourTetrahedraHand(), 1.0, kelvin);
    const auto glyphs = renderer::heatFluxVectorGlyphs(h.hand.mesh, h.post, 0);
    REQUIRE(glyphs.has_value());
    // |q| = 5, 12, 7, 12 against a reference of 12 and a threshold of 0.5: 5/12 = 0.42 is suppressed, 7/12 = 0.58 is not.
    const auto overlay = renderer::heatFluxVectorOverlay(*glyphs, 0.1, 12.0, 0.5);
    REQUIRE(overlay.has_value());
    CHECK(overlay->belowThreshold == 1);
    CHECK(overlay->glyphs == 3);
    // THE ACCOUNTING INVARIANT: every glyph given is exactly one of drawn, skipped, too small or suppressed.
    CHECK(overlay->glyphs + overlay->skipped + overlay->tooSmall + overlay->belowThreshold + overlay->duplicatesGrouped == glyphs->size());
    // Suppression is a display choice: the inspector still reports the actual value of the suppressed element.
    const auto e = renderer::inspectThermalElement(h.hand.mesh, h.post, 0, h.post.elementIds()[0], renderer::ThermalResultState::Current);
    REQUIRE(e.has_value());
    CHECK_THAT(e->heatFluxMagnitude.si(), WithinAbs(5.0, 1e-11));
    CHECK_THAT((*glyphs)[0].magnitude.si(), WithinAbs(5.0, 1e-11));
    CHECK_FALSE(renderer::heatFluxVectorOverlay(*glyphs, 0.1, 12.0, 1.0).has_value());
    CHECK_FALSE(renderer::heatFluxVectorOverlay(*glyphs, 0.1, 12.0, -0.1).has_value());

    // A scale that underflows against the reference is counted too small, not drawn as a dot.
    const auto tiny = renderer::heatFluxVectorOverlay(*glyphs, 1.0e-300, 1.0e300);
    REQUIRE(tiny.has_value());
    CHECK(tiny->tooSmall == 4);
    CHECK(tiny->empty());
    CHECK(tiny->glyphs + tiny->skipped + tiny->tooSmall + tiny->belowThreshold == glyphs->size());
}

// ===========================================================================
// Symbols
// ===========================================================================

TEST_CASE("ThermalViz_PrescribedTemperatureSymbol", "[thermal][viz][boundary][temperature]") {
    VizStudy study;
    define(study, steadyDefinition(study, {}, {temperatureBc(1, study.startCap(), 350.0), temperatureBc(2, study.endCap(), 300.0)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->boundary.has_value());
    const renderer::MeshView view = boundaryViewOf(study);
    const meshing::Mesh& mesh = study.mesh();
    const auto glyphs = renderer::prescribedTemperatureGlyphs(view, mesh, study.map(), study.definition().boundaryConditions, *derived->boundary);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == 2);
    // THE CANONICAL VALUE, in kelvin, as the user gave it: 350 K, not 76.85 and not 623.15.
    CHECK((*glyphs)[0].value.si() == 350.0);
    CHECK((*glyphs)[1].value.si() == 300.0);
    CHECK((*glyphs)[0].condition == ThermalBoundaryConditionId::fromValue(1));

    constexpr double kLength = 0.004;
    const auto overlay = renderer::prescribedTemperatureOverlay(mesh, *glyphs, kLength);
    REQUIRE(overlay.has_value());
    const std::size_t facets = (*glyphs)[0].facets.size() + (*glyphs)[1].facets.size();
    CHECK(overlay->role == renderer::ThermalOverlayRole::PrescribedTemperatureSymbols);
    CHECK(overlay->glyphs == facets);
    CHECK(overlay->segments.size() == facets * renderer::kThermalSegmentsPerTemperatureSymbol);
    CHECK(overlay->duplicatesGrouped == 0);
    // Each stem stands on its facet centroid along the OUTWARD normal, kLength long: -z on the start cap, +z on the end cap.
    for (std::size_t i = 0; i < facets; ++i) {
        const renderer::ThermalOverlaySegment& stem = overlay->segments[i * renderer::kThermalSegmentsPerTemperatureSymbol];
        const Dir3 direction = unitOf(stem.from, stem.to);
        const bool onStart = i < (*glyphs)[0].facets.size();
        CHECK_THAT(direction.z, WithinAbs(onStart ? -1.0 : 1.0, 1e-12));
        CHECK_THAT(normOf(stem.to.x.si() - stem.from.x.si(), stem.to.y.si() - stem.from.y.si(), stem.to.z.si() - stem.from.z.si()), WithinAbs(kLength, 1e-12));
        CHECK_THAT(stem.from.z.si(), WithinAbs(onStart ? 0.0 : 0.020, 1e-12));
    }
    CHECK_FALSE(renderer::prescribedTemperatureOverlay(mesh, *glyphs, 0.0).has_value());
    CHECK_FALSE(renderer::prescribedTemperatureOverlay(mesh, *glyphs, std::numeric_limits<double>::infinity()).has_value());
}

TEST_CASE("ThermalViz_IdenticalPrescriptionsAreOneSymbolAndAdditiveLoadsAreBothDrawn", "[thermal][viz][boundary][duplicates]") {
    VizStudy study;
    // TWO conditions prescribing the SAME temperature on the same face: one mathematical constraint (P18-BC collapses it).
    define(study, steadyDefinition(study, {surfaceLoad(1, study.endCap(), 100.0), surfaceLoad(2, study.endCap(), 100.0)},
                                   {temperatureBc(1, study.startCap(), 350.0), temperatureBc(2, study.startCap(), 350.0),
                                    convectionBc(3, study.endCap(), 25.0, kAmbient), convectionBc(4, study.endCap(), 10.0, 280.0)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    const renderer::MeshView view = boundaryViewOf(study);
    const meshing::Mesh& mesh = study.mesh();
    const thermal::ThermalAnalysisDefinition def = study.definition();

    const auto held = renderer::prescribedTemperatureGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    REQUIRE(held.has_value());
    REQUIRE(held->size() == 2);   // both canonical conditions are listed ...
    const auto symbols = renderer::prescribedTemperatureOverlay(mesh, *held, 0.004);
    REQUIRE(symbols.has_value());
    // ... and the identical prescription of each facet is drawn ONCE, the repeat counted as grouped.
    const std::size_t facets = (*held)[0].facets.size();
    CHECK(symbols->glyphs == facets);
    CHECK(symbols->duplicatesGrouped == facets);
    CHECK(symbols->glyphs + symbols->skipped + symbols->tooSmall + symbols->belowThreshold + symbols->duplicatesGrouped == 2 * facets);

    // CONVECTION IS ADDITIVE: two conditions on the end cap are both drawn, and both keep their own h and Tinf.
    const auto convecting = renderer::convectionGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    REQUIRE(convecting.has_value());
    REQUIRE(convecting->size() == 2);
    CHECK((*convecting)[0].coefficient.si() == 25.0);
    CHECK((*convecting)[1].coefficient.si() == 10.0);
    CHECK((*convecting)[1].ambient.si() == 280.0);
    const auto waves = renderer::convectionOverlay(mesh, *convecting, 0.004);
    REQUIRE(waves.has_value());
    const std::size_t capFacets = (*convecting)[0].facets.size();
    CHECK(waves->glyphs == 2 * capFacets);
    CHECK(waves->segments.size() == 2 * capFacets * renderer::kThermalSegmentsPerConvectionSymbol);

    // TWO ADDITIVE FLUX LOADS on the end cap: both listed, both drawn, neither merged into a new canonical q''.
    const auto fluxes = renderer::appliedHeatFluxGlyphs(view, mesh, study.map(), def.loads, *derived->loads);
    REQUIRE(fluxes.has_value());
    REQUIRE(fluxes->size() == 2);
    CHECK((*fluxes)[0].flux.si() == 100.0);
    CHECK((*fluxes)[1].flux.si() == 100.0);
    CHECK((*fluxes)[0].load != (*fluxes)[1].load);
    const auto arrows = renderer::appliedHeatFluxOverlay(mesh, *fluxes, 0.005, 100.0);
    REQUIRE(arrows.has_value());
    CHECK(arrows->glyphs == 2 * (*fluxes)[0].facets.size());
    // The canonical definition is exactly what it was.
    CHECK(study.definition() == def);
}

TEST_CASE("ThermalViz_ConvectionSymbol", "[thermal][viz][boundary][convection]") {
    VizStudy study;
    define(study, steadyDefinition(study, {}, {temperatureBc(1, study.startCap(), 300.0), convectionBc(2, study.endCap(), 25.0, 300.0)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->boundary.has_value());
    const renderer::MeshView view = boundaryViewOf(study);
    const auto glyphs = renderer::convectionGlyphs(view, study.mesh(), study.map(), study.definition().boundaryConditions, *derived->boundary);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == 1);
    // h = 25 W/(m^2 K) and Tinf = 300 K, exactly as given: nothing was computed to draw the symbol.
    CHECK(glyphs->front().coefficient.si() == 25.0);
    CHECK(glyphs->front().ambient.si() == 300.0);
    constexpr double kLength = 0.004;
    const auto overlay = renderer::convectionOverlay(study.mesh(), *glyphs, kLength);
    REQUIRE(overlay.has_value());
    CHECK(overlay->role == renderer::ThermalOverlayRole::ConvectionSymbols);
    CHECK(overlay->glyphs == glyphs->front().facets.size());
    CHECK(overlay->segments.size() == overlay->glyphs * renderer::kThermalSegmentsPerConvectionSymbol);
    // The zigzag stands on the end cap (z = 0.02) and rises along the outward normal +z to kLength above it.
    for (std::size_t i = 0; i < overlay->glyphs; ++i) {
        const std::size_t at = i * renderer::kThermalSegmentsPerConvectionSymbol;
        CHECK_THAT(overlay->segments[at].from.z.si(), WithinAbs(0.020, 1e-12));
        CHECK_THAT(overlay->segments[at + 3].to.z.si(), WithinAbs(0.020 + kLength, 1e-12));
        for (std::size_t k = 0; k < 3; ++k) {   // a connected path
            CHECK(overlay->segments[at + k].to == overlay->segments[at + k + 1].from);
        }
    }
}

TEST_CASE("ThermalViz_BoundarySetHighlight", "[thermal][viz][boundary][highlight]") {
    VizStudy study;
    const renderer::MeshView view = boundaryViewOf(study);
    const meshing::Mesh& mesh = study.mesh();
    meshing::NamedBoundarySet set;
    set.id = BoundarySetId::fromValue(5);
    set.name = "ends";
    set.faces = {study.startCap(), study.endCap()};
    const auto named = renderer::namedBoundaryHighlight(view, mesh, study.map(), set);
    REQUIRE(named.has_value());
    CHECK(named->name == "ends");
    CHECK(named->set == BoundarySetId::fromValue(5));
    CHECK(named->highlight.fullyResolved());
    // THE HIGHLIGHT IS EXACTLY THE TWO CAPS: the test's own classification by coordinates.
    const auto startCap = trianglesOnPlane(mesh, 2, 0.0);
    const auto endCap = trianglesOnPlane(mesh, 2, 0.020);
    std::set<meshing::ElementId> expected(startCap.begin(), startCap.end());
    expected.insert(endCap.begin(), endCap.end());
    std::set<meshing::ElementId> actual;
    for (const std::size_t triangle : named->highlight.triangles) {
        const Result<meshing::ElementId> element = view.elementOfTriangle(triangle);
        REQUIRE(element.has_value());
        actual.insert(*element);
    }
    CHECK(actual == expected);
    CHECK(named->highlight.triangles.size() == expected.size());   // one render triangle per facet, none doubled
    CHECK(named->cardinality.complete());
    // Rendered, classified and expected are three numbers and they are the same number.
    CHECK(named->cardinality.expected == expected.size());
    CHECK(named->cardinality.classified == expected.size());
    CHECK(named->cardinality.rendered == expected.size());
}

TEST_CASE("ThermalViz_MultipleBoundariesDoNotOverwriteEachOther", "[thermal][viz][boundary][multiple]") {
    VizStudy study;
    const FaceName hotSide = sideFacing(study, 1.0, 0.0, 0.0);
    define(study, steadyDefinition(study, {surfaceLoad(1, hotSide, 800.0), surfaceLoad(2, study.side(0) == hotSide ? study.side(1) : study.side(0), -200.0)},
                                   {temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    const renderer::MeshView view = boundaryViewOf(study);
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const meshing::Mesh& mesh = study.mesh();
    const auto fluxes = renderer::appliedHeatFluxGlyphs(view, mesh, study.map(), def.loads, *derived->loads);
    const auto held = renderer::prescribedTemperatureGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    const auto convecting = renderer::convectionGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    REQUIRE(fluxes.has_value());
    REQUIRE(held.has_value());
    REQUIRE(convecting.has_value());
    REQUIRE(fluxes->size() == 2);
    for (const auto& g : *fluxes) {
        CHECK(g.cardinality.complete());
    }
    CHECK((*fluxes)[0].flux.si() == 800.0);
    CHECK((*fluxes)[1].flux.si() == -200.0);
    CHECK((*held)[0].cardinality.complete());
    CHECK((*convecting)[0].cardinality.complete());
    // The facets of the four targets are disjoint faces: no glyph set swallowed another.
    std::set<meshing::ElementId> all;
    std::size_t total = 0;
    for (const auto& g : *fluxes) {
        all.insert(g.facets.begin(), g.facets.end());
        total += g.facets.size();
    }
    all.insert((*held)[0].facets.begin(), (*held)[0].facets.end());
    total += (*held)[0].facets.size();
    all.insert((*convecting)[0].facets.begin(), (*convecting)[0].facets.end());
    total += (*convecting)[0].facets.size();
    CHECK(all.size() == total);
    // Each overlay is its own role.
    const auto a = renderer::appliedHeatFluxOverlay(mesh, *fluxes, 0.005, 800.0);
    const auto b = renderer::prescribedTemperatureOverlay(mesh, *held, 0.004);
    const auto c = renderer::convectionOverlay(mesh, *convecting, 0.004);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());
    CHECK(a->glyphs == (*fluxes)[0].facets.size() + (*fluxes)[1].facets.size());
    CHECK(b->glyphs == (*held)[0].facets.size());
    CHECK(c->glyphs == (*convecting)[0].facets.size());
}

// ===========================================================================
// Orientation and boundary-ness, checked against the owning tetrahedra
// ===========================================================================

TEST_CASE("ThermalViz_OutwardNormalMatchesTheOwningTetrahedron", "[thermal][viz][boundary][orientation]") {
    // The outward normal is the stored winding's `(p2 - p1) x (p3 - p1)`. Checked here against an INDEPENDENT fact: the
    // fourth vertex of the tetrahedron that owns the facet lies on the INNER side of it. If the winding were inward, or the
    // facet were an internal one, this would fail for it.
    VizStudy study;
    const meshing::Mesh& mesh = study.mesh();
    REQUIRE_FALSE(mesh.triangles().empty());
    std::size_t checked = 0;
    for (const meshing::Triangle& facet : mesh.triangles()) {
        const Result<Vector3D> n = renderer::outwardNormalOf(mesh, facet.id);
        const Result<Point3D> centroid = renderer::facetCentroidOf(mesh, facet.id);
        REQUIRE(n.has_value());
        REQUIRE(centroid.has_value());
        CHECK_THAT(normOf(n->x, n->y, n->z), WithinAbs(1.0, 1e-12));
        // The owning tetrahedron: the one holding all three of the facet's nodes (exactly one for a boundary facet).
        std::size_t owners = 0;
        for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
            std::size_t shared = 0;
            for (const meshing::NodeId id : facet.nodes) {
                shared += std::ranges::find(tet.nodes, id) != tet.nodes.end() ? 1U : 0U;
            }
            if (shared != 3U) {
                continue;
            }
            ++owners;
            meshing::NodeId fourth{};
            for (const meshing::NodeId id : tet.nodes) {
                if (std::ranges::find(facet.nodes, id) == facet.nodes.end()) {
                    fourth = id;
                }
            }
            const Point3D& p = mesh.findNode(fourth)->position;
            const double inward = ((p.x.si() - centroid->x.si()) * n->x) + ((p.y.si() - centroid->y.si()) * n->y) +
                                  ((p.z.si() - centroid->z.si()) * n->z);
            CHECK(inward < 0.0);   // the opposite vertex is BEHIND the outward normal
        }
        CHECK(owners == 1);        // a boundary facet belongs to exactly one tetrahedron; an interior face would have two
        ++checked;
    }
    CHECK(checked == mesh.triangles().size());
    // A tetrahedron's id is not a boundary triangle's: no normal, no centroid.
    CHECK_FALSE(renderer::outwardNormalOf(mesh, mesh.tetrahedra()[0].id).has_value());
    CHECK_FALSE(renderer::facetCentroidOf(mesh, meshing::ElementId::fromValue(987654)).has_value());
}

TEST_CASE("ThermalViz_NoInternalFaceIsDrawnAsAnExternalCondition", "[thermal][viz][boundary][internal]") {
    // Every facet of every glyph is a BOUNDARY triangle of the mesh (an element the mesh stores as a Triangle3), never the
    // face two tetrahedra share. The coordinates agree: each lies on its CAD face's plane.
    VizStudy study;
    define(study, steadyDefinition(study, {surfaceLoad(1, study.endCap(), 100.0)},
                                   {temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.side(0), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    const renderer::MeshView view = boundaryViewOf(study);
    const meshing::Mesh& mesh = study.mesh();
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const auto fluxes = renderer::appliedHeatFluxGlyphs(view, mesh, study.map(), def.loads, *derived->loads);
    const auto held = renderer::prescribedTemperatureGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    const auto convecting = renderer::convectionGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    REQUIRE(fluxes.has_value());
    REQUIRE(held.has_value());
    REQUIRE(convecting.has_value());
    std::vector<meshing::ElementId> everyFacet;
    for (const auto& g : *fluxes) {
        everyFacet.insert(everyFacet.end(), g.facets.begin(), g.facets.end());
    }
    for (const auto& g : *held) {
        everyFacet.insert(everyFacet.end(), g.facets.begin(), g.facets.end());
    }
    for (const auto& g : *convecting) {
        everyFacet.insert(everyFacet.end(), g.facets.begin(), g.facets.end());
    }
    REQUIRE_FALSE(everyFacet.empty());
    for (const meshing::ElementId id : everyFacet) {
        CHECK(mesh.findTriangle(id) != nullptr);          // a stored boundary triangle ...
        CHECK(mesh.findTetrahedron(id) == nullptr);       // ... and not a tetrahedron
    }
    for (const meshing::ElementId id : (*held)[0].facets) {
        const Result<Point3D> c = renderer::facetCentroidOf(mesh, id);
        REQUIRE(c.has_value());
        CHECK_THAT(c->z.si(), WithinAbs(0.0, 1e-12));     // on the start cap's plane
    }
}

TEST_CASE("ThermalViz_ACurvedWallHasOutwardNormalsAndACompleteClassification", "[thermal][viz][boundary][orientation][curved]") {
    // The block's planar faces are axis aligned, so they cannot tell a correct outward normal from a sign that happens to
    // suit them. A CYLINDER'S WALL has a normal that turns through every azimuth: each facet's normal must be radial and
    // outward, the owning tetrahedron's fourth vertex must lie behind it, and the classification of the wall must be complete.
    VizStudy study(3_mm, std::nullopt, true);
    const meshing::Mesh& mesh = study.mesh();
    REQUIRE(mesh.triangles().size() > 40U);   // a curved wall subdivides: many facets, not two per face
    const FaceName wall = study.side(0);
    define(study, steadyDefinition(study, {surfaceLoad(1, wall, 800.0)}, {temperatureBc(1, study.startCap(), 350.0)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    const renderer::MeshView view = boundaryViewOf(study);
    const auto glyphs = renderer::appliedHeatFluxGlyphs(view, mesh, study.map(), study.definition().loads, *derived->loads);
    REQUIRE(glyphs.has_value());
    REQUIRE(glyphs->size() == 1);
    const renderer::AppliedHeatFluxGlyph& flux = glyphs->front();
    CHECK(flux.cardinality.complete());
    CHECK(flux.facets.size() > 40U);
    // The wall's area, by the test's own sum of cross products, against the cylinder's 2 pi r L (a polygonal wall is a little less).
    const double exact = 2.0 * 3.14159265358979323846 * 0.006 * 0.020;
    const double summed = areaOf(mesh, flux.facets);
    CHECK(summed < exact);
    CHECK(summed > 0.97 * exact);
    CHECK_THAT(flux.area.si(), WithinAbs(summed, 1e-12));   // P18-LOAD integrated over the same facets

    std::size_t outwardRadial = 0;
    for (const meshing::ElementId id : flux.facets) {
        const Result<Vector3D> n = renderer::outwardNormalOf(mesh, id);
        const Result<Point3D> c = renderer::facetCentroidOf(mesh, id);
        REQUIRE(n.has_value());
        REQUIRE(c.has_value());
        // The wall's outward direction at a point is radial: (x, y, 0) / r. A tessellated wall differs by the facet's tilt.
        const double r = std::hypot(c->x.si(), c->y.si());
        REQUIRE(r > 0.0);
        const double radial = (n->x * c->x.si() + n->y * c->y.si()) / r;
        CHECK(radial > 0.99);          // pointing out, not in, and nearly radial
        CHECK_THAT(n->z, WithinAbs(0.0, 1e-6));   // the wall has no axial component
        outwardRadial += radial > 0.99 ? 1U : 0U;
    }
    CHECK(outwardRadial == flux.facets.size());

    // The arrows of a positive q'' travel INWARD on every facet of the wall: along the negative radial direction.
    const auto overlay = renderer::appliedHeatFluxOverlay(mesh, *glyphs, 0.004, 800.0);
    REQUIRE(overlay.has_value());
    REQUIRE(overlay->glyphs == flux.facets.size());
    for (std::size_t i = 0; i < overlay->glyphs; ++i) {
        const renderer::ThermalOverlaySegment& shaft = overlay->segments[i * renderer::kThermalSegmentsPerArrow];
        const Dir3 d = unitOf(shaft.from, shaft.to);
        const double r = std::hypot(shaft.to.x.si(), shaft.to.y.si());
        CHECK((d.x * shaft.to.x.si() + d.y * shaft.to.y.si()) / r < -0.99);
    }
    // And the same wall's boundary is outward by the owning-tetrahedron test, facet by facet.
    for (const meshing::ElementId id : flux.facets) {
        const meshing::Triangle* facet = mesh.findTriangle(id);
        REQUIRE(facet != nullptr);
        const Result<Vector3D> n = renderer::outwardNormalOf(mesh, id);
        const Result<Point3D> c = renderer::facetCentroidOf(mesh, id);
        for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
            std::size_t shared = 0;
            for (const meshing::NodeId node : facet->nodes) {
                shared += std::ranges::find(tet.nodes, node) != tet.nodes.end() ? 1U : 0U;
            }
            if (shared != 3U) {
                continue;
            }
            for (const meshing::NodeId node : tet.nodes) {
                if (std::ranges::find(facet->nodes, node) != facet->nodes.end()) {
                    continue;
                }
                const Point3D& p = mesh.findNode(node)->position;
                CHECK(((p.x.si() - c->x.si()) * n->x) + ((p.y.si() - c->y.si()) * n->y) + ((p.z.si() - c->z.si()) * n->z) < 0.0);
            }
        }
    }
}
// ---------------------------------------------------------------------------------------------------------------------
// Findings of the independent adversarial review, each closed by a test that failed before the change.
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("ThermalViz_AGlyphIsRefusedForLoadsThePreparedContributionsWereNotMadeFrom", "[thermal][viz][symbols][binding]") {
    VizStudy study;
    const FaceName hotSide = sideFacing(study, 1.0, 0.0, 0.0);
    define(study, steadyDefinition(study, {surfaceLoad(1, hotSide, 1000.0), generationLoad(2, 1.0e4)},
                                   {temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const renderer::MeshView view = boundaryViewOf(study);
    const meshing::Mesh& mesh = study.mesh();

    // The control: the loads and conditions the contributions were made from are accepted.
    REQUIRE(renderer::appliedHeatFluxGlyphs(view, mesh, study.map(), def.loads, *derived->loads).has_value());
    REQUIRE(renderer::prescribedTemperatureGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary).has_value());
    REQUIRE(renderer::convectionGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary).has_value());

    // A load with the same identity and the same number of loads, but another magnitude or another face: the counts
    // agree and the contributions belong to a different model. Mixing them would label a stale integral Complete.
    const std::vector<thermal::ThermalLoad> otherMagnitude{surfaceLoad(1, hotSide, 2000.0), generationLoad(2, 1.0e4)};
    const std::vector<thermal::ThermalLoad> otherFace{surfaceLoad(1, study.endCap(), 1000.0), generationLoad(2, 1.0e4)};
    CHECK_FALSE(renderer::appliedHeatFluxGlyphs(view, mesh, study.map(), otherMagnitude, *derived->loads).has_value());
    CHECK_FALSE(renderer::appliedHeatFluxGlyphs(view, mesh, study.map(), otherFace, *derived->loads).has_value());

    const std::vector<thermal::ThermalBoundaryCondition> otherTemperature{
        temperatureBc(1, study.startCap(), 351.0), convectionBc(2, study.endCap(), 25.0, kAmbient)};
    const std::vector<thermal::ThermalBoundaryCondition> otherCoefficient{
        temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.endCap(), 26.0, kAmbient)};
    CHECK_FALSE(renderer::prescribedTemperatureGlyphs(view, mesh, study.map(), otherTemperature, *derived->boundary).has_value());
    CHECK_FALSE(renderer::convectionGlyphs(view, mesh, study.map(), otherCoefficient, *derived->boundary).has_value());
}

TEST_CASE("ThermalViz_AnOverflowingArrowLengthIsRefusedAndNeverReachesTheGeometry", "[thermal][viz][vectors][overflow]") {
    // Every operand is finite and the quotient overflows: an infinite arrow is not a large one.
    CHECK_FALSE(renderer::thermalDisplayArrowLength(1.0e10, 1.0e-300, 1.0).has_value());
    CHECK(renderer::thermalDisplayArrowLength(1.0e10, 1.0e10, 0.5).value() == 0.5);

    renderer::HeatFluxVectorGlyph glyph;
    glyph.element = meshing::ElementId::fromValue(1);
    glyph.centroid = Point3D{};
    glyph.direction = Vector3D{.x = 1.0, .y = 0.0, .z = 0.0};
    glyph.magnitude = HeatFlux::fromSi(1.0e10);
    glyph.hasDirection = true;
    const std::vector<renderer::HeatFluxVectorGlyph> glyphs{glyph};
    const auto overlay = renderer::heatFluxVectorOverlay(glyphs, 1.0, 1.0e-300);
    CHECK_FALSE(overlay.has_value());
    const auto fine = renderer::heatFluxVectorOverlay(glyphs, 1.0, 1.0e10);
    REQUIRE(fine.has_value());
    CHECK(fine->glyphs == 1);
    for (const renderer::ThermalOverlaySegment& segment : fine->segments) {
        CHECK(std::isfinite(segment.from.x.si()));
        CHECK(std::isfinite(segment.to.x.si()));
    }
}

TEST_CASE("ThermalViz_IncompleteTargetsAreCountedByTheBoundaryOverlays", "[thermal][viz][classifier][overlay]") {
    VizStudy study;
    const FaceName hotSide = sideFacing(study, 1.0, 0.0, 0.0);
    define(study, steadyDefinition(study, {surfaceLoad(1, hotSide, 1000.0)},
                                   {temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const renderer::MeshView view = boundaryViewOf(study);
    const meshing::Mesh& mesh = study.mesh();

    auto applied = renderer::appliedHeatFluxGlyphs(view, mesh, study.map(), def.loads, *derived->loads);
    auto held = renderer::prescribedTemperatureGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    auto convecting = renderer::convectionGlyphs(view, mesh, study.map(), def.boundaryConditions, *derived->boundary);
    REQUIRE((applied.has_value() && held.has_value() && convecting.has_value()));

    // Complete targets: nothing is incomplete.
    CHECK(renderer::appliedHeatFluxOverlay(mesh, *applied, 0.005, 1000.0)->incompleteTargets == 0);
    CHECK(renderer::prescribedTemperatureOverlay(mesh, *held, 0.005)->incompleteTargets == 0);
    CHECK(renderer::convectionOverlay(mesh, *convecting, 0.005)->incompleteTargets == 0);

    // A glyph record that is not Complete is still drawn from what was classified, and the overlay SAYS SO.
    applied->front().cardinality.status = BoundaryClassStatus::Partial;
    held->front().cardinality.status = BoundaryClassStatus::Unresolved;
    convecting->front().cardinality.status = BoundaryClassStatus::ZeroClassified;
    const auto a = renderer::appliedHeatFluxOverlay(mesh, *applied, 0.005, 1000.0);
    const auto p = renderer::prescribedTemperatureOverlay(mesh, *held, 0.005);
    const auto c = renderer::convectionOverlay(mesh, *convecting, 0.005);
    REQUIRE((a.has_value() && p.has_value() && c.has_value()));
    CHECK(a->incompleteTargets == 1);
    CHECK(p->incompleteTargets == 1);
    CHECK(c->incompleteTargets == 1);
    CHECK(a->glyphs > 0);   // drawn, not hidden: the honest marker is the count, not a silent drop
}
