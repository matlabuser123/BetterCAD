// P18-VIZ-001 -- result currentness, the validation badge, inspection and playback, on REAL documents.
//
// THERE IS NO GUI STALE FLAG TO TEST, and that is the claim. Every state here is DERIVED from the core: P16's
// `Mesher::currency`, the thermal result's own `isCurrent` and `staleReasons` comparison, and the validation report's
// own verdict and currentness. The tests edit the document the way a user would and read what the derivation says.
//
// The hardest adversarial cases of the brief are here: a result held across a remesh that leaves every count and every
// id unchanged, a failed recompute that must not bring an old result back, and a transient whose frame times are the
// STORED times and not a count times a step.

#include "ThermalVizTestSupport.hpp"

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

using namespace vztest;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using renderer::ThermalResultState;

namespace {

[[nodiscard]] bool has(const renderer::ThermalDisplayState& state, thermal::TransientStaleReason reason) {
    return std::ranges::find(state.staleReasons, reason) != state.staleReasons.end();
}

[[nodiscard]] std::vector<meshing::NodeId> nodeIds(const meshing::Mesh& mesh) {
    std::vector<meshing::NodeId> out;
    for (const meshing::Node& n : mesh.nodes()) {
        out.push_back(n.id);
    }
    return out;
}

[[nodiscard]] std::vector<meshing::ElementId> tetIds(const meshing::Mesh& mesh) {
    std::vector<meshing::ElementId> out;
    for (const meshing::Tetrahedron& t : mesh.tetrahedra()) {
        out.push_back(t.id);
    }
    return out;
}

/// The depth of the block set to @p millimetres, regenerated and REMESHED: a thin plate P16 meshes with slender tetrahedra.
void thin(VizStudy& study, double millimetres) {
    REQUIRE(study.document
                .modifyObject<features::ExtrudeFeature>(study.feature,
                                                        [millimetres](features::ExtrudeFeature& extrude) -> Result<bool> {
                                                            auto definition = extrude.definition();
                                                            definition.depth = Length::fromSi(millimetres * 1.0e-3);
                                                            return extrude.setDefinition(definition).has_value();
                                                        })
                .has_value());
    requireReport(study.regenerator, study.document);
    study.remesh();
}

} // namespace

// ===========================================================================
// A current result displays normally
// ===========================================================================

TEST_CASE("ThermalViz_CurrentResultDisplaysNormally", "[thermal][viz][state][current]") {
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    REQUIRE(derived->post.has_value());
    const thermal::ThermalValidationReport report = validationOf(study, *derived);
    REQUIRE(report.accepted());

    const renderer::ThermalDisplayState state = displayStateOf(study, derived.get(), &report);
    CHECK(state.mesh == meshing::MeshCurrency::Current);
    CHECK(state.result == ThermalResultState::Current);
    CHECK(state.staleReasons.empty());
    CHECK_FALSE(state.inputProblem.has_value());
    CHECK_FALSE(state.failure.has_value());
    CHECK(state.resultMayDrawAsCurrent());
    CHECK(state.resultInspectable());
    CHECK(state.meshDescribesModel());
    CHECK(state.intentMayDraw());
    CHECK(state.badge == renderer::ThermalValidationBadge::Pass);
    REQUIRE(state.validation.has_value());
    CHECK(*state.validation == thermal::ValidationStatus::Pass);
    CHECK(renderer::thermalFieldAvailability(state) == renderer::ThermalFieldAvailability::Available);
    CHECK(renderer::thermalBanner(renderer::thermalFieldAvailability(state)).empty());

    // And the contours really are drawable from it, through the real mesh and the real numbering.
    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    const Result<renderer::ThermalResultView> result = renderer::ThermalResultView::of(mesh, view, derived->map, *derived->post);
    REQUIRE(result.has_value());
    for (const renderer::ThermalChannel channel : renderer::allThermalChannels()) {
        const auto field = result->field(mesh, derived->map, *derived->post, 0, channel);
        REQUIRE(field.has_value());
        CHECK(field->complete());
    }
}

TEST_CASE("ThermalViz_TheStatesBeforeAnyResultAreDistinct", "[thermal][viz][state][lifecycle]") {
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));

    // No result yet: the inputs resolve, so the canonical intent may draw, and nothing derived exists.
    const renderer::ThermalDisplayState none = displayStateOf(study, nullptr);
    CHECK(none.result == ThermalResultState::NoResult);
    CHECK(none.intentMayDraw());
    CHECK_FALSE(none.resultInspectable());
    CHECK(renderer::thermalFieldAvailability(none) == renderer::ThermalFieldAvailability::NoResult);
    CHECK(renderer::thermalBanner(renderer::ThermalFieldAvailability::NoResult) == "NO RESULT");

    // A solve that failed with nothing held.
    const Error failure{ErrorCode::FailedPrecondition, "the solve failed"};
    const renderer::ThermalDisplayState failed = displayStateOf(study, nullptr, nullptr, &failure);
    CHECK(failed.result == ThermalResultState::SolveFailed);
    REQUIRE(failed.failure.has_value());
    CHECK(failed.failure->message == "the solve failed");
    CHECK(renderer::thermalFieldAvailability(failed) == renderer::ThermalFieldAvailability::SolveFailed);

    // An analysis the document does not have.
    const renderer::ThermalDisplayState nobody = renderer::thermalDisplayState(
        study.document, study.regenerator, study.mesher, ThermalAnalysisId::fromValue(987654), nullptr, nullptr, nullptr);
    CHECK(nobody.result == ThermalResultState::NoAnalysis);
    CHECK_FALSE(nobody.intentMayDraw());
    CHECK(renderer::thermalFieldAvailability(nobody) == renderer::ThermalFieldAvailability::NoResult);

    // A geometry edit that was regenerated and NOT remeshed: the inputs no longer resolve, and the mesh says why.
    REQUIRE(study.document
                .modifyObject<features::ExtrudeFeature>(study.feature,
                                                        [](features::ExtrudeFeature& extrude) -> Result<bool> {
                                                            auto definition = extrude.definition();
                                                            definition.depth = Length::fromSi(0.035);
                                                            return extrude.setDefinition(definition).has_value();
                                                        })
                .has_value());
    requireReport(study.regenerator, study.document);
    const renderer::ThermalDisplayState stale = displayStateOf(study, nullptr);
    CHECK(stale.mesh == meshing::MeshCurrency::StaleGeometry);
    CHECK(stale.result == ThermalResultState::InputsUnavailable);
    REQUIRE(stale.inputProblem.has_value());
    CHECK(*stale.inputProblem == thermal::InputProblem::MeshStale);
    CHECK_FALSE(stale.intentMayDraw());   // intent is drawn on a mesh that describes the model, and this one does not
}

// ===========================================================================
// A stale result is never presented as current
// ===========================================================================

TEST_CASE("ThermalViz_StaleResultHasExplicitState", "[thermal][viz][state][stale]") {
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    REQUIRE(displayStateOf(study, derived.get()).result == ThermalResultState::Current);

    // A MATERIAL EDIT leaves the mesh current and the result stale -- and the state says exactly that, not "remesh".
    study.editConductivity(50.0);
    const renderer::ThermalDisplayState material = displayStateOf(study, derived.get());
    CHECK(material.mesh == meshing::MeshCurrency::Current);
    CHECK(material.result == ThermalResultState::Stale);
    CHECK(has(material, thermal::TransientStaleReason::Material));
    CHECK_FALSE(material.resultMayDrawAsCurrent());
    CHECK(material.resultInspectable());
    CHECK(material.meshDescribesModel());
    CHECK(renderer::thermalFieldAvailability(material) == renderer::ThermalFieldAvailability::StaleResult);
    CHECK(renderer::thermalBanner(renderer::thermalFieldAvailability(material)) == "STALE RESULT - recompute required");

    // A LOAD edit: the reason is the loads, and the material is not named.
    VizStudy second;
    define(second, steadyDefinition(second, {generationLoad(1, 1.0e5)}, slabConditions(second)));
    const std::unique_ptr<Derived> d2 = derive(second);
    define(second, steadyDefinition(second, {generationLoad(1, 2.0e5)}, slabConditions(second)));
    const renderer::ThermalDisplayState loads = displayStateOf(second, d2.get());
    CHECK(loads.result == ThermalResultState::Stale);
    CHECK(has(loads, thermal::TransientStaleReason::Loads));
    CHECK_FALSE(has(loads, thermal::TransientStaleReason::Material));

    // A CONDITION edit.
    define(second, steadyDefinition(second, {generationLoad(1, 1.0e5)},
                                    {temperatureBc(1, second.startCap(), 410.0), temperatureBc(2, second.endCap(), 300.0)}));
    const renderer::ThermalDisplayState conditions = displayStateOf(second, d2.get());
    CHECK(conditions.result == ThermalResultState::Stale);
    CHECK(has(conditions, thermal::TransientStaleReason::PrescribedTemperatures));

    // The edit undone: the result describes the model again, by IDENTITY, and the state follows the core.
    define(second, steadyDefinition(second, {generationLoad(1, 1.0e5)}, slabConditions(second)));
    CHECK(displayStateOf(second, d2.get()).result == ThermalResultState::Current);
}

TEST_CASE("ThermalViz_DoesNotMapStaleResultToNewMesh", "[thermal][viz][state][stale][remesh]") {
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> r1 = derive(study);
    REQUIRE(r1->post.has_value());
    const meshing::Mesh m1 = study.mesh();

    study.remesh();   // M2: a new generation of the same body
    const meshing::Mesh& m2 = study.mesh();

    // The old result is stale -- the mesh it was computed on is not the mesh the model has now.
    const renderer::ThermalDisplayState state = displayStateOf(study, r1.get());
    CHECK(state.mesh == meshing::MeshCurrency::Current);   // the NEW mesh is current
    CHECK(state.result == ThermalResultState::Stale);
    CHECK(has(state, thermal::TransientStaleReason::Mesh));
    CHECK(renderer::thermalFieldAvailability(state) == renderer::ThermalFieldAvailability::StaleResult);

    // NOTHING of the old result can be put onto the new mesh: not a contour, not a vector, not an inspection.
    Result<thermal::ThermalDofMap> map2 = thermal::buildThermalDofMap(m2);
    REQUIRE(map2.has_value());
    const renderer::MeshView view2 = byTetrahedron(m2);
    CHECK_FALSE(renderer::ThermalResultView::of(m2, view2, *map2, *r1->post).has_value());
    CHECK_FALSE(renderer::ThermalResultView::of(m2, view2, r1->map, *r1->post).has_value());   // nor through the OLD numbering
    CHECK_FALSE(renderer::heatFluxVectorGlyphs(m2, *r1->post, 0).has_value());
    CHECK_FALSE(renderer::inspectThermalNode(m2, r1->map, *r1->post, 0, m2.nodes()[0].id).has_value());
    CHECK_FALSE(renderer::inspectThermalNode(m2, *map2, *r1->post, 0, m2.nodes()[0].id).has_value());
    CHECK_FALSE(renderer::inspectThermalElement(m2, *r1->post, 0, m2.tetrahedra()[0].id).has_value());

    // A fresh derivation on the new mesh is current and displays.
    const std::unique_ptr<Derived> r2 = derive(study);
    REQUIRE(r2->post.has_value());
    CHECK(displayStateOf(study, r2.get()).result == ThermalResultState::Current);
    CHECK(renderer::ThermalResultView::of(m2, view2, r2->map, *r2->post).has_value());
    (void)m1;
}

TEST_CASE("ThermalViz_SameCountsNewMeshStillStale", "[thermal][viz][state][stale][remesh][counts]") {
    // THE ADVERSARY: a remesh of the same body with the same node count and the same tetrahedron count. The old result
    // has the right size, plausible numbers, and is stale. A viewer comparing sizes would draw it as current.
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> r1 = derive(study);
    REQUIRE(r1->post.has_value());
    const std::size_t nodes1 = study.mesh().nodes().size();
    const std::size_t tets1 = study.mesh().tetrahedra().size();
    const meshing::MeshStamp stamp1 = study.mesh().stamp();

    study.remesh();
    REQUIRE(study.mesh().nodes().size() == nodes1);
    REQUIRE(study.mesh().tetrahedra().size() == tets1);
    REQUIRE_FALSE(study.mesh().stamp() == stamp1);

    const renderer::ThermalDisplayState state = displayStateOf(study, r1.get());
    CHECK(state.result == ThermalResultState::Stale);
    CHECK_FALSE(state.resultMayDrawAsCurrent());
    CHECK(r1->post->nodeCount() == nodes1);                          // the sizes AGREE with the new mesh
    CHECK(r1->post->elementCount() == tets1);
    const renderer::MeshView view = byTetrahedron(study.mesh());
    CHECK_FALSE(renderer::ThermalResultView::of(study.mesh(), view, r1->map, *r1->post).has_value());   // and it is still refused
    WARN(std::format("remesh: {} nodes and {} tetrahedra in BOTH generations; the old result is stale ({}) and is not mapped",
                     nodes1, tets1, renderer::toString(state.result)));
}

TEST_CASE("ThermalViz_SameIdsNewMeshStillStale", "[thermal][viz][state][stale][remesh][ids]") {
    // The same adversary through the identities: the NodeIds and ElementIds of the new mesh are EXACTLY the old ones, so
    // every handle in the old result resolves in the new mesh -- to a different physical node and a different element.
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> r1 = derive(study);
    REQUIRE(r1->post.has_value());
    const std::vector<meshing::NodeId> nodes1 = nodeIds(study.mesh());
    const std::vector<meshing::ElementId> tets1 = tetIds(study.mesh());

    study.remesh();
    REQUIRE(nodeIds(study.mesh()) == nodes1);
    REQUIRE(tetIds(study.mesh()) == tets1);

    const renderer::ThermalDisplayState state = displayStateOf(study, r1.get());
    CHECK(state.result == ThermalResultState::Stale);
    for (const meshing::ElementId id : r1->post->elementIds()) {
        CHECK(study.mesh().findTetrahedron(id) != nullptr);   // every old handle resolves ...
    }
    CHECK_FALSE(renderer::inspectThermalElement(study.mesh(), *r1->post, 0, tets1.front()).has_value());   // ... and is still refused
    CHECK_FALSE(renderer::inspectThermalNode(study.mesh(), r1->map, *r1->post, 0, nodes1.front()).has_value());
    CHECK_FALSE(renderer::heatFluxVectorGlyphs(study.mesh(), *r1->post, 0).has_value());
}

TEST_CASE("ThermalViz_FailedRecomputeDoesNotMakeOldResultCurrent", "[thermal][viz][state][stale][failure]") {
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> r1 = derive(study);
    REQUIRE(r1->post.has_value());

    // R1 current; the source is edited; R1 is stale ...
    study.editConductivity(52.0);
    REQUIRE(displayStateOf(study, r1.get()).result == ThermalResultState::Stale);

    // ... and the new solve FAILS. The old result is not resurrected: it is stale and the failure is reported beside it.
    const Error failure{ErrorCode::FailedPrecondition, "the new solve failed"};
    const renderer::ThermalDisplayState after = displayStateOf(study, r1.get(), nullptr, &failure);
    CHECK(after.result == ThermalResultState::SolveFailed);
    CHECK_FALSE(after.resultMayDrawAsCurrent());
    CHECK(after.resultInspectable());
    CHECK(has(after, thermal::TransientStaleReason::Material));   // WHY the old result is stale is still reported
    REQUIRE(after.failure.has_value());
    CHECK(renderer::thermalFieldAvailability(after) == renderer::ThermalFieldAvailability::SolveFailed);
    CHECK(renderer::thermalBanner(renderer::ThermalFieldAvailability::SolveFailed) == "SOLVE FAILED - no current result");

    // A failure alone, with nothing held, is not "no result".
    CHECK(displayStateOf(study, nullptr, nullptr, &failure).result == ThermalResultState::SolveFailed);
}

// ===========================================================================
// The validation badge is the core's verdict, and only while it is current
// ===========================================================================

TEST_CASE("ThermalViz_TheValidationBadgeIsTheCoresVerdictAndGoesStaleWithTheModel", "[thermal][viz][state][validation]") {
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const thermal::ThermalValidationReport report = validationOf(study, *derived);
    REQUIRE(report.status() == thermal::ValidationStatus::Pass);

    CHECK(displayStateOf(study, derived.get(), &report).badge == renderer::ThermalValidationBadge::Pass);

    // A load edit: the report no longer describes the model. A green badge from a validation of a model that has since
    // been edited is the stale-result problem in a small colour -- so it is shown as STALE.
    define(study, steadyDefinition(study, {generationLoad(1, 3.0e5)}, slabConditions(study)));
    const renderer::ThermalDisplayState edited = displayStateOf(study, derived.get(), &report);
    CHECK(edited.badge == renderer::ThermalValidationBadge::Stale);
    CHECK_FALSE(edited.validation.has_value());   // the status is not shown at all
    CHECK(edited.result == ThermalResultState::Stale);
    CHECK_FALSE(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));

    // No report: no badge.
    CHECK(displayStateOf(study, derived.get()).badge == renderer::ThermalValidationBadge::None);
}

TEST_CASE("ThermalViz_TheValidationBadgeBelongsToItsOwnAnalysis", "[thermal][viz][state][validation]") {
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const thermal::ThermalValidationReport report = validationOf(study, *derived);
    REQUIRE(report.status() == thermal::ValidationStatus::Pass);
    REQUIRE(report.analysis.has_value());
    CHECK(displayStateOf(study, derived.get(), &report).badge == renderer::ThermalValidationBadge::Pass);

    // A report that is about ANOTHER analysis -- same model, same mesh, still "current" by the core's own comparison of
    // its own source -- is not this analysis's verdict: a green badge on a picture it never judged.
    thermal::ThermalValidationReport elsewhere = report;
    elsewhere.analysis = ThermalAnalysisId::fromValue(report.analysis->value() + 41U);
    const renderer::ThermalDisplayState shown = displayStateOf(study, derived.get(), &elsewhere);
    CHECK(shown.badge == renderer::ThermalValidationBadge::None);
    CHECK_FALSE(shown.validation.has_value());

    // ... and one that names no analysis at all.
    thermal::ThermalValidationReport nameless = report;
    nameless.analysis.reset();
    CHECK(displayStateOf(study, derived.get(), &nameless).badge == renderer::ThermalValidationBadge::None);

    // A report that recorded ANOTHER RESULT (another load, so another recovered source) is not a verdict about the held
    // result: it is shown as stale, not as the green badge it carries.
    VizStudy stronger;
    define(stronger, steadyDefinition(stronger, {generationLoad(1, 3.0e5)}, slabConditions(stronger)));
    const std::unique_ptr<Derived> other = derive(stronger);
    REQUIRE(other->post.has_value());
    REQUIRE_FALSE(other->post->source() == derived->post->source());
    thermal::ThermalValidationReport aboutAnother = report;
    aboutAnother.resultSource = other->post->source();
    const renderer::ThermalDisplayState mismatched = displayStateOf(study, derived.get(), &aboutAnother);
    CHECK(mismatched.badge == renderer::ThermalValidationBadge::Stale);
    CHECK_FALSE(mismatched.validation.has_value());
}

TEST_CASE("ThermalViz_AWarningStaysDrawnAndAFailureDoesNotPassAsAnAcceptedAnalysis", "[thermal][viz][state][validation]") {
    // A thin plate: structurally valid, past the qualified envelope, inside the accuracy bound -- accepted WITH WARNINGS.
    VizStudy plate;
    thin(plate, 0.3);
    define(plate, steadyDefinition(plate, {}, slabConditions(plate)));
    const std::unique_ptr<Derived> d = derive(plate);
    REQUIRE(d->post.has_value());
    const thermal::ThermalValidationReport warned = validationOf(plate, *d);
    REQUIRE(warned.status() == thermal::ValidationStatus::PassWithWarnings);
    const renderer::ThermalDisplayState state = displayStateOf(plate, d.get(), &warned);
    CHECK(state.badge == renderer::ThermalValidationBadge::PassWithWarnings);
    CHECK(state.result == ThermalResultState::Current);
    // A WARNING DOES NOT MAKE THE FIELDS UNAVAILABLE: it is an accepted result.
    CHECK(renderer::thermalFieldAvailability(state) == renderer::ThermalFieldAvailability::Available);

    // A current result whose validation FAILED is not presented as an accepted analysis (the badge is set by hand: a
    // real current result with a failed report is not constructible from one analysis, and the function is pure).
    renderer::ThermalDisplayState failed = state;
    failed.badge = renderer::ThermalValidationBadge::Fail;
    failed.validation = thermal::ValidationStatus::Fail;
    CHECK(renderer::thermalFieldAvailability(failed) == renderer::ThermalFieldAvailability::InvalidResult);
    CHECK(renderer::thermalBanner(renderer::ThermalFieldAvailability::InvalidResult) == "INVALID RESULT - validation failed");
    // The same badge on a STALE result is the stale story, not the invalid one.
    failed.result = ThermalResultState::Stale;
    CHECK(renderer::thermalFieldAvailability(failed) == renderer::ThermalFieldAvailability::StaleResult);

    // The REAL failing validation: a singular steady model. The report FAILS, and the viewer shows the core's verdict.
    VizStudy singular;
    define(singular, steadyDefinition(singular, {generationLoad(1, 1.0e5)}, {}));
    const std::unique_ptr<Derived> none = derive(singular);
    const thermal::ThermalValidationReport bad = validationOf(singular, *none);
    REQUIRE(bad.status() == thermal::ValidationStatus::Fail);
    const renderer::ThermalDisplayState badState = displayStateOf(singular, none.get(), &bad);
    CHECK(badState.badge == renderer::ThermalValidationBadge::Fail);
    REQUIRE(badState.validation.has_value());
    CHECK(*badState.validation == thermal::ValidationStatus::Fail);
    CHECK(badState.result == ThermalResultState::NoResult);   // the singular model produced no solution
}

TEST_CASE("ThermalViz_ViewStateChangesDoNotStaleThePhysics", "[thermal][viz][state][isolation]") {
    // The colour map (band count, display range), the field selection and the playback cursor are VIEW STATE: none is
    // a field of any source, so changing one cannot stale the solution, the recovered fields or the validation.
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const thermal::ThermalValidationReport report = validationOf(study, *derived);
    const renderer::ThermalDisplayState before = displayStateOf(study, derived.get(), &report);
    REQUIRE(before.result == ThermalResultState::Current);

    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    renderer::ThermalContourCache cache;
    Result<renderer::ThermalPlayback> playback = renderer::ThermalPlayback::of(*derived->post);
    REQUIRE(playback.has_value());
    for (const renderer::ThermalChannel channel : renderer::allThermalChannels()) {          // FIELD SELECTION
        for (const std::size_t bands : {2U, 8U, 33U, 64U}) {                                   // COLOUR MAP
            for (const std::optional<renderer::ThermalDisplayRange> range :                    // DISPLAY RANGE
                 {std::optional<renderer::ThermalDisplayRange>{}, std::optional{renderer::ThermalDisplayRange{250.0, 450.0}}}) {
                REQUIRE(cache.colours(mesh, view, derived->map, *derived->post, playback->frame(), channel, bands, range).has_value());
            }
        }
    }
    REQUIRE(playback->select(0).has_value());                                                   // PLAYBACK CURSOR
    playback->first();

    const renderer::ThermalDisplayState after = displayStateOf(study, derived.get(), &report);
    CHECK(after == before);
    CHECK(after.result == ThermalResultState::Current);
    CHECK(thermal::isCurrent(report, study.document, study.regenerator, study.mesher));
    CHECK(thermal::isCurrent(*derived->post, study.document, study.model(), study.definition().loads,
                             study.definition().boundaryConditions, study.definition().solver));
}

// ===========================================================================
// Inspection
// ===========================================================================

TEST_CASE("ThermalViz_NodeTemperatureInspection", "[thermal][viz][inspect][node]") {
    VizStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));   // 400 K and 300 K held on the caps
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->steady.has_value());
    const meshing::Mesh& mesh = study.mesh();
    for (const meshing::Node& node : mesh.nodes()) {
        const Result<renderer::ThermalNodeInspection> inspected =
            renderer::inspectThermalNode(mesh, derived->map, *derived->post, 0, node.id);
        REQUIRE(inspected.has_value());
        const Result<thermal::ThermalDofIndex> dof = derived->map.dofOf(node.id);
        REQUIRE(dof.has_value());
        CHECK(inspected->node == node.id);
        CHECK(inspected->dof == *dof);
        CHECK(inspected->position == node.position);   // the canonical position, metres, unit in the type
        // THE SOLVER'S OWN VALUE, bit for bit, through the numbering.
        CHECK(inspected->temperature.si() == derived->steady->temperatures()[dof->value()].si());
        CHECK(inspected->resultState == ThermalResultState::Current);
        CHECK_FALSE(inspected->time.has_value());
        CHECK(inspected->temperature.si() >= 300.0 - 1e-9);
        CHECK(inspected->temperature.si() <= 400.0 + 1e-9);
    }
    // A node the mesh does not hold, a state that is not stored, and a result that is not there are refused.
    CHECK_FALSE(renderer::inspectThermalNode(mesh, derived->map, *derived->post, 0, meshing::NodeId::fromValue(987654)).has_value());
    CHECK_FALSE(renderer::inspectThermalNode(mesh, derived->map, *derived->post, 3, mesh.nodes()[0].id).has_value());
    CHECK_FALSE(renderer::inspectThermalNode(mesh, derived->map, *derived->post, 0, mesh.nodes()[0].id, ThermalResultState::NoResult).has_value());
    CHECK_FALSE(renderer::inspectThermalNode(mesh, derived->map, *derived->post, 0, mesh.nodes()[0].id, ThermalResultState::InputsUnavailable).has_value());
}

TEST_CASE("ThermalViz_ElementThermalInspection", "[thermal][viz][inspect][element]") {
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const meshing::Mesh& mesh = study.mesh();
    std::size_t index = 0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const Result<renderer::ThermalElementInspection> e = renderer::inspectThermalElement(mesh, *derived->post, 0, tet.id);
        REQUIRE(e.has_value());
        CHECK(e->element == tet.id);
        CHECK(e->ordinal == index);
        CHECK(e->nodes == tet.nodes);
        CHECK(e->resultState == ThermalResultState::Current);
        // The stored vectors, untouched: q = -k grad(T) is POST's, NOT recomputed by the inspector.
        const thermal::ThermalElementFields& stored = derived->post->states()[0].fields;
        CHECK(e->gradient == stored.gradients[index]);
        CHECK(e->heatFlux == stored.heatFluxes[index]);
        CHECK(e->heatFluxMagnitude == stored.heatFluxMagnitudes[index]);
        // The test's own arithmetic for the display norm and for the sign of Fourier's law (k = 45 W/(m K)).
        CHECK_THAT(e->gradientMagnitude.si(), WithinAbs(normOf(e->gradient.x.si(), e->gradient.y.si(), e->gradient.z.si()), 1e-9));
        CHECK_THAT(e->heatFlux.x.si(), WithinAbs(-45.0 * e->gradient.x.si(), 1e-6));
        CHECK_THAT(e->heatFlux.y.si(), WithinAbs(-45.0 * e->gradient.y.si(), 1e-6));
        CHECK_THAT(e->heatFlux.z.si(), WithinAbs(-45.0 * e->gradient.z.si(), 1e-6));
        // And the vector heat flux points down the gradient: q . grad(T) <= 0.
        CHECK(e->heatFlux.x.si() * e->gradient.x.si() + e->heatFlux.y.si() * e->gradient.y.si() + e->heatFlux.z.si() * e->gradient.z.si() <= 1e-9);
        ++index;
    }
    CHECK_FALSE(renderer::inspectThermalElement(mesh, *derived->post, 0, meshing::ElementId::fromValue(987654)).has_value());
    CHECK_FALSE(renderer::inspectThermalElement(mesh, *derived->post, 9, mesh.tetrahedra()[0].id).has_value());
    // A BOUNDARY TRIANGLE IS NOT A TETRAHEDRON: its id resolves in the mesh and not in the result.
    CHECK_FALSE(renderer::inspectThermalElement(mesh, *derived->post, 0, mesh.triangles()[0].id).has_value());
}

TEST_CASE("ThermalViz_SparseNodeIdInspection", "[thermal][viz][inspect][sparse]") {
    // NodeIds 7, 104, 9001 and 12000 on one tetrahedron: numbering 0..3, and 9001 is unknown 2.
    AsmHand hand = sparseTetHand({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {7, 104, 9001, 12000});
    const HandPost h = makeHandPost(std::move(hand), 5.0, {310.0, 350.0, 287.0, 295.0});
    const Result<renderer::ThermalNodeInspection> inspected =
        renderer::inspectThermalNode(h.hand.mesh, h.hand.dofs, h.post, 0, meshing::NodeId::fromValue(9001));
    REQUIRE(inspected.has_value());
    CHECK(inspected->node == meshing::NodeId::fromValue(9001));
    CHECK(inspected->dof.value() == 2);
    CHECK(inspected->temperature.si() == 287.0);
    // The others, by the same route: never `T[node.value()]`, which would read past a four-element array.
    struct Row {
        std::uint32_t node;
        std::uint64_t dof;
        double kelvin;
    };
    for (const Row row : {Row{7, 0, 310.0}, Row{104, 1, 350.0}, Row{9001, 2, 287.0}, Row{12000, 3, 295.0}}) {
        const auto e = renderer::inspectThermalNode(h.hand.mesh, h.hand.dofs, h.post, 0, meshing::NodeId::fromValue(row.node));
        REQUIRE(e.has_value());
        CHECK(e->dof.value() == row.dof);
        CHECK(e->temperature.si() == row.kelvin);
    }
    CHECK(h.post.states()[0].temperatures.size() == 4);   // sized by the node count, not by the largest handle
    // A handle that is a number inside the array's range but not a node: refused, not read.
    CHECK_FALSE(renderer::inspectThermalNode(h.hand.mesh, h.hand.dofs, h.post, 0, meshing::NodeId::fromValue(2)).has_value());
}

TEST_CASE("ThermalViz_SparseElementIdInspection", "[thermal][viz][inspect][sparse]") {
    // Four tetrahedra with ids 17..20 (sixteen boundary triangles come first) and compact ordinals 0..3.
    const std::array<std::array<double, 3>, 4> gradients{{{3.0, 4.0, 0.0}, {0.0, 12.0, 0.0}, {2.0, 3.0, 6.0}, {0.0, 0.0, 12.0}}};
    std::vector<double> kelvin;
    for (const auto& g : gradients) {
        kelvin.insert(kelvin.end(), {300.0, 300.0 + g[0], 300.0 + g[1], 300.0 + g[2]});
    }
    const HandPost h = makeHandPost(fourTetrahedraHand(), 1.0, kelvin);
    for (std::size_t ordinal = 0; ordinal < 4; ++ordinal) {
        const meshing::ElementId id = h.post.elementIds()[ordinal];
        REQUIRE(id.value() == 17 + ordinal);
        const auto e = renderer::inspectThermalElement(h.hand.mesh, h.post, 0, id);
        REQUIRE(e.has_value());
        CHECK(e->ordinal == ordinal);                                   // the compact ordinal, not the id
        CHECK_THAT(e->gradient.x.si(), WithinAbs(gradients[ordinal][0], 1e-11));
        CHECK_THAT(e->gradient.y.si(), WithinAbs(gradients[ordinal][1], 1e-11));
        CHECK_THAT(e->gradient.z.si(), WithinAbs(gradients[ordinal][2], 1e-11));
        CHECK_THAT(e->heatFlux.x.si(), WithinAbs(-gradients[ordinal][0], 1e-11));   // k = 1
        CHECK_THAT(e->heatFluxMagnitude.si(), WithinAbs(normOf(gradients[ordinal][0], gradients[ordinal][1], gradients[ordinal][2]), 1e-11));
    }
    // The raw ids 17..20 as INDICES would read past a four-element array; the ordinals 0..3 are ids of boundary TRIANGLES.
    CHECK_FALSE(renderer::inspectThermalElement(h.hand.mesh, h.post, 0, meshing::ElementId::fromValue(1)).has_value());
    CHECK_FALSE(renderer::inspectThermalElement(h.hand.mesh, h.post, 0, meshing::ElementId::fromValue(21)).has_value());
}

// ===========================================================================
// Transient playback
// ===========================================================================

TEST_CASE("ThermalViz_TransientPlaybackUsesStoredTimes", "[thermal][viz][playback][time]") {
    // dt = 0.3 s to t = 1 s: the last step is the shortened remainder. The times are the STORED ones.
    VizStudy study;
    define(study, transientDefinition(study, {}, slabConditions(study), 300.0, 0.3, 1.0));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->stoppedBecause.empty());
    REQUIRE(derived->post.has_value());
    REQUIRE(derived->post->mode() == ThermalAnalysisMode::Transient);

    Result<renderer::ThermalPlayback> playback = renderer::ThermalPlayback::of(*derived->post);
    REQUIRE(playback.has_value());
    CHECK_FALSE(playback->isSteady());
    std::vector<double> displayed;
    for (std::size_t f = 0; f < playback->frameCount(); ++f) {
        REQUIRE(playback->select(f).has_value());
        REQUIRE(playback->time().has_value());
        displayed.push_back(*playback->time());
        // The frame's time IS the stored time of that state, bit for bit.
        CHECK(*playback->time() == derived->post->states()[f].time->si());
    }
    // The brief's expectation: 0, 0.3, 0.6, 0.9, 1.0 -- and NOT frame * 0.3 (which would end at 1.2).
    REQUIRE(displayed.size() == 5);
    const std::array<double, 5> expected{0.0, 0.3, 0.6, 0.9, 1.0};
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK_THAT(displayed[i], WithinAbs(expected[i], 1e-12));
    }
    CHECK(displayed.back() != 0.3 * 4.0);
    CHECK_THAT(displayed.back(), WithinAbs(1.0, 1e-12));
    // Strictly increasing, as the core guarantees and the viewer insists.
    for (std::size_t i = 1; i < displayed.size(); ++i) {
        CHECK(displayed[i] > displayed[i - 1]);
    }
    WARN(std::format("playback: stored times {} {} {} {} {} s; frame 4 is t = 1 s (a nominal 4 x 0.3 would be 1.2 s)",
                     displayed[0], displayed[1], displayed[2], displayed[3], displayed[4]));
}

TEST_CASE("ThermalViz_TransientFrameBindsAllFieldsToTheSameState", "[thermal][viz][playback][frame]") {
    VizStudy study;
    define(study, transientDefinition(study, {}, slabConditions(study), 300.0, 0.3, 1.0));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    const Result<renderer::ThermalResultView> result = renderer::ThermalResultView::of(mesh, view, derived->map, *derived->post);
    REQUIRE(result.has_value());
    REQUIRE(result->stateCount() == 5);

    // The frames differ from one another (a transient moves), so a field read from the wrong frame is detectable.
    std::vector<renderer::ThermalScalarField> temperature;
    std::vector<renderer::ThermalScalarField> magnitude;
    for (std::size_t f = 0; f < 5; ++f) {
        temperature.push_back(*result->field(mesh, derived->map, *derived->post, f, renderer::ThermalChannel::Temperature));
        magnitude.push_back(*result->field(mesh, derived->map, *derived->post, f, renderer::ThermalChannel::HeatFluxMagnitude));
    }
    CHECK(temperature[1] != temperature[2]);
    CHECK(temperature[3].values != temperature[4].values);
    CHECK(magnitude[1].values != magnitude[2].values);

    for (std::size_t f = 0; f < 5; ++f) {
        INFO("frame " << f);
        // EVERY FIELD OF FRAME f BELONGS TO STATE f, and carries that state's time.
        CHECK(temperature[f].state == f);
        CHECK(magnitude[f].state == f);
        CHECK(temperature[f].time == magnitude[f].time);
        CHECK(temperature[f].time.value() == derived->post->states()[f].time->si());
        const thermal::ThermalPostState& stored = derived->post->states()[f];
        for (std::size_t i = 0; i < stored.temperatures.size(); ++i) {
            CHECK(temperature[f].values[i] == stored.temperatures[i].si());
        }
        for (std::size_t i = 0; i < stored.fields.heatFluxMagnitudes.size(); ++i) {
            CHECK(magnitude[f].values[i] == stored.fields.heatFluxMagnitudes[i].si());
        }
        // The extrema are those of THIS frame, not of the whole history.
        const auto hottest = renderer::thermalExtremeOf(temperature[f], renderer::ThermalExtremeKind::Maximum);
        REQUIRE(hottest.has_value());
        const auto warmest = std::max_element(stored.temperatures.begin(), stored.temperatures.end(),
                                              [](Temperature a, Temperature b) { return a.si() < b.si(); });
        CHECK(hottest->value == warmest->si());
        // The vector glyphs of frame f carry frame f's flux.
        const auto glyphs = renderer::heatFluxVectorGlyphs(mesh, *derived->post, f);
        REQUIRE(glyphs.has_value());
        REQUIRE(glyphs->size() == stored.fields.heatFluxes.size());
        for (std::size_t i = 0; i < glyphs->size(); ++i) {
            CHECK((*glyphs)[i].flux == stored.fields.heatFluxes[i]);
        }
        // And an inspection at frame f.
        const auto node = renderer::inspectThermalNode(mesh, derived->map, *derived->post, f, mesh.nodes()[2].id);
        REQUIRE(node.has_value());
        CHECK(node->temperature.si() == stored.temperatures[2].si());
        CHECK(node->time->si() == stored.time->si());
    }
    // The cache hands back frame 2's buffer for frame 2 and not the previous frame's.
    // (An ELEMENT channel under a SHARED manual range, so the two frames' colours are comparable and the surface picture moves: a nodal
    // temperature on the exterior facets reads the corner nodes, which on a slab are all prescribed, so it would be identical in every
    // frame, and with an auto range each frame is normalised to its own extremes.)
    renderer::ThermalContourCache cache;
    const renderer::ThermalDisplayRange shared{0.0, 600000.0};
    const auto c1 = cache.colours(mesh, view, derived->map, *derived->post, 1, renderer::ThermalChannel::HeatFluxMagnitude, 12, shared);
    REQUIRE(c1.has_value());
    const std::vector<double> frame1 = **c1;
    const auto c2 = cache.colours(mesh, view, derived->map, *derived->post, 2, renderer::ThermalChannel::HeatFluxMagnitude, 12, shared);
    REQUIRE(c2.has_value());
    CHECK(cache.builds() == 2);
    const auto direct = renderer::thermalContourCornerColours(view, magnitude[2], *renderer::thermalContourLegend(magnitude[2], 12, shared));
    REQUIRE(direct.has_value());
    CHECK(**c2 == *direct);
    CHECK(**c2 != frame1);
}

TEST_CASE("ThermalViz_TransientPlaybackDoesNotInterpolatePhysics", "[thermal][viz][playback][interpolation]") {
    VizStudy study;
    define(study, transientDefinition(study, {}, slabConditions(study), 300.0, 0.3, 1.0));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    Result<renderer::ThermalPlayback> playback = renderer::ThermalPlayback::of(*derived->post);
    REQUIRE(playback.has_value());
    // A frame is an accepted state or it does not exist: there is no frame 2.5, and no frame past the last.
    CHECK(playback->frameCount() == derived->post->stateCount());
    CHECK_FALSE(playback->select(playback->frameCount()).has_value());
    CHECK_FALSE(playback->select(derived->post->stateCount() + 100).has_value());
    CHECK_FALSE(playback->timeOf(playback->frameCount()).has_value());
    // The cursor moves through frames and nothing else: stepping visits exactly the stored states, in order.
    playback->first();
    std::vector<std::size_t> visited{playback->frame()};
    while (playback->next()) {
        visited.push_back(playback->frame());
    }
    CHECK(visited == std::vector<std::size_t>{0, 1, 2, 3, 4});
    CHECK_FALSE(playback->next());        // no wrap by default, and no change at the end
    CHECK(playback->frame() == 4);
    CHECK(playback->next(true));          // loop wraps to the first
    CHECK(playback->frame() == 0);
    CHECK_FALSE(playback->previous());    // no wrap by default, and no change at the start
    CHECK(playback->frame() == 0);
    CHECK(playback->previous(true));
    CHECK(playback->frame() == 4);
    // Playing never touched the history.
    const thermal::ThermalPostResult copy = *derived->post;
    playback->first();
    while (playback->next()) {
    }
    CHECK(*derived->post == copy);
}

TEST_CASE("ThermalViz_ASteadyResultIsOneFrameWithoutATime", "[thermal][viz][playback][steady]") {
    VizStudy study;
    define(study, steadyDefinition(study, {}, slabConditions(study)));
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    Result<renderer::ThermalPlayback> playback = renderer::ThermalPlayback::of(*derived->post);
    REQUIRE(playback.has_value());
    CHECK(playback->isSteady());
    CHECK(playback->frameCount() == 1);
    CHECK_FALSE(playback->time().has_value());
    CHECK_FALSE(playback->next());
    CHECK_FALSE(playback->next(true));   // a loop of one frame goes nowhere
    CHECK_FALSE(playback->previous(true));
}

TEST_CASE("ThermalViz_AViewBuiltForOneResultRefusesAnotherResultOnTheSameMesh", "[thermal][viz][state][binding]") {
    // The same body, the same mesh generation, the same numbering, the same counts -- and two solves with different loads.
    // A view is bound to ONE result by its source, by value; the other result is refused, not read through the first's checks.
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> a = derive(study);
    REQUIRE(a->post.has_value());
    define(study, steadyDefinition(study, {generationLoad(1, 4.0e5)}, slabConditions(study)));
    const std::unique_ptr<Derived> b = derive(study);
    REQUIRE(b->post.has_value());
    REQUIRE_FALSE(a->post->source() == b->post->source());
    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    const Result<renderer::ThermalResultView> forA = renderer::ThermalResultView::of(mesh, view, a->map, *a->post);
    REQUIRE(forA.has_value());
    CHECK(forA->describes(mesh, a->map, *a->post));
    CHECK_FALSE(forA->describes(mesh, b->map, *b->post));
    CHECK(forA->field(mesh, a->map, *a->post, 0, renderer::ThermalChannel::Temperature).has_value());
    CHECK_FALSE(forA->field(mesh, b->map, *b->post, 0, renderer::ThermalChannel::Temperature).has_value());
}
TEST_CASE("ThermalViz_TransientCacheInvalidatesOnSourceChange", "[thermal][viz][playback][cache]") {
    VizStudy study;
    define(study, transientDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study), 300.0, 0.3, 1.0));
    const std::unique_ptr<Derived> a = derive(study);
    REQUIRE(a->post.has_value());
    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    renderer::ThermalContourCache cache;
    REQUIRE(cache.colours(mesh, view, a->map, *a->post, 3, renderer::ThermalChannel::Temperature, 12).has_value());
    REQUIRE(cache.colours(mesh, view, a->map, *a->post, 3, renderer::ThermalChannel::Temperature, 12).has_value());
    CHECK(cache.builds() == 1);
    CHECK(cache.hits() == 1);

    // A canonical edit and a new solve: a new source. The SAME frame number, channel and mesh must not hit.
    define(study, transientDefinition(study, {generationLoad(1, 4.0e5)}, slabConditions(study), 300.0, 0.3, 1.0));
    const std::unique_ptr<Derived> b = derive(study);
    REQUIRE(b->post.has_value());
    CHECK_FALSE(a->post->source() == b->post->source());
    REQUIRE(cache.colours(mesh, view, b->map, *b->post, 3, renderer::ThermalChannel::Temperature, 12).has_value());
    CHECK(cache.builds() == 2);
    CHECK(cache.hits() == 1);

    // The old playback no longer describes the new result.
    Result<renderer::ThermalPlayback> pa = renderer::ThermalPlayback::of(*a->post);
    Result<renderer::ThermalPlayback> pb = renderer::ThermalPlayback::of(*b->post);
    REQUIRE(pa.has_value());
    REQUIRE(pb.has_value());
    CHECK(pa->describes(*a->post));
    CHECK_FALSE(pa->describes(*b->post));
    CHECK(pb->describes(*b->post));
    // And the display state of the old result against the edited analysis is stale.
    define(study, transientDefinition(study, {generationLoad(1, 7.0e5)}, slabConditions(study), 300.0, 0.3, 1.0));
    CHECK(displayStateOf(study, a.get()).result == ThermalResultState::Stale);
    CHECK(displayStateOf(study, b.get()).result == ThermalResultState::Stale);
}

// ===========================================================================
// Determinism
// ===========================================================================

TEST_CASE("ThermalViz_DisplayStateAndInspectionAreDeterministic", "[thermal][viz][determinism]") {
    VizStudy study;
    define(study, steadyDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study)));
    study.editConductivity(48.0);
    const std::unique_ptr<Derived> derived = derive(study);
    REQUIRE(derived->post.has_value());
    const thermal::ThermalValidationReport report = validationOf(study, *derived);
    study.editConductivity(49.0);
    const renderer::ThermalDisplayState first = displayStateOf(study, derived.get(), &report);
    for (int run = 0; run < 10; ++run) {
        CHECK(displayStateOf(study, derived.get(), &report) == first);
    }
    const meshing::Mesh& mesh = study.mesh();
    const auto e = renderer::inspectThermalElement(mesh, *derived->post, 0, mesh.tetrahedra()[3].id);
    for (int run = 0; run < 10; ++run) {
        CHECK(renderer::inspectThermalElement(mesh, *derived->post, 0, mesh.tetrahedra()[3].id) == e);
    }
}

// ===========================================================================
// The visualization data is a function of the source and the view settings, and nothing else
// ===========================================================================

namespace {

struct Fnv {
    std::uint64_t hash = 14695981039346656037ULL;
    void byte(std::uint8_t b) {
        hash ^= b;
        hash *= 1099511628211ULL;
    }
    void word(std::uint64_t w) {
        for (int i = 0; i < 8; ++i) {
            byte(static_cast<std::uint8_t>((w >> (8 * i)) & 0xFFU));
        }
    }
    void real(double v) { word(std::bit_cast<std::uint64_t>(v)); }
    void text(std::string_view s) {
        for (const char c : s) {
            byte(static_cast<std::uint8_t>(c));
        }
        byte(0);
    }
};

/// A digest of EVERYTHING the viewer layer derives from one analysis state: the display state, every channel's field, legend,
/// corner colours and extremes, the vector glyphs and their arrows, the boundary glyphs, their cardinalities and symbols, and the
/// playback times. Discrete values are mixed as integers and continuous ones by their bits, so two presets agree only if they
/// agree exactly.
[[nodiscard]] std::uint64_t digestOf(const VizStudy& study, const Derived& derived, std::size_t frame) {
    Fnv f;
    const renderer::ThermalDisplayState state = displayStateOf(study, &derived);
    f.text(renderer::toString(state.result));
    f.text(meshing::toString(state.mesh));
    f.word(state.staleReasons.size());
    const meshing::Mesh& mesh = study.mesh();
    const renderer::MeshView view = byTetrahedron(mesh);
    const Result<renderer::ThermalResultView> result = renderer::ThermalResultView::of(mesh, view, derived.map, *derived.post);
    REQUIRE(result.has_value());
    for (const renderer::ThermalChannel channel : renderer::allThermalChannels()) {
        const auto field = result->field(mesh, derived.map, *derived.post, frame, channel);
        REQUIRE(field.has_value());
        f.text(renderer::toString(channel));
        f.word(field->expected);
        f.word(field->classified);
        for (const double v : field->values) {
            f.real(v);
        }
        for (const meshing::NodeId n : field->nodes) {
            f.word(n.value());
        }
        for (const meshing::ElementId e : field->elements) {
            f.word(e.value());
        }
        for (const renderer::ThermalExtremeKind kind : {renderer::ThermalExtremeKind::Maximum, renderer::ThermalExtremeKind::Minimum}) {
            const auto extreme = renderer::thermalExtremeOf(*field, kind);
            REQUIRE(extreme.has_value());
            f.word(extreme->index);
            f.word(extreme->node.value());
            f.word(extreme->element.value());
            f.real(extreme->value);
        }
        const auto legend = renderer::thermalContourLegend(*field, 12);
        REQUIRE(legend.has_value());
        for (const renderer::ThermalContourBand& band : legend->bands) {
            f.real(band.lower);
            f.real(band.upper);
        }
        const auto colours = renderer::thermalContourCornerColours(view, *field, *legend);
        REQUIRE(colours.has_value());
        for (const double c : *colours) {
            f.real(c);
        }
    }
    const auto glyphs = renderer::heatFluxVectorGlyphs(mesh, *derived.post, frame);
    REQUIRE(glyphs.has_value());
    for (const renderer::HeatFluxVectorGlyph& g : *glyphs) {
        f.word(g.element.value());
        f.real(g.direction.x);
        f.real(g.direction.y);
        f.real(g.direction.z);
        f.word(g.hasDirection ? 1U : 0U);
    }
    const auto arrows = renderer::heatFluxVectorOverlay(*glyphs, 0.002, derived.post->states()[frame].fields.heatFluxMagnitudes[0].si() + 1.0);
    REQUIRE(arrows.has_value());
    for (const renderer::ThermalOverlaySegment& s : arrows->segments) {
        f.real(s.from.x.si());
        f.real(s.to.z.si());
    }
    f.word(arrows->glyphs);
    f.word(arrows->skipped);
    const Result<renderer::MeshView> boundary = renderer::MeshView::volumeBoundaryOf(mesh);
    REQUIRE(boundary.has_value());
    const thermal::ThermalAnalysisDefinition def = study.definition();
    const auto fluxes = renderer::appliedHeatFluxGlyphs(*boundary, mesh, study.map(), def.loads, *derived.loads);
    const auto held = renderer::prescribedTemperatureGlyphs(*boundary, mesh, study.map(), def.boundaryConditions, *derived.boundary);
    const auto convecting = renderer::convectionGlyphs(*boundary, mesh, study.map(), def.boundaryConditions, *derived.boundary);
    REQUIRE(fluxes.has_value());
    REQUIRE(held.has_value());
    REQUIRE(convecting.has_value());
    const auto mixCardinality = [&f](const renderer::BoundaryCardinality& c) {
        f.text(renderer::toString(c.kind));
        f.word(c.id);
        f.word(c.expected);
        f.word(c.classified);
        f.word(c.rendered);
        f.text(renderer::toString(c.status));
    };
    for (const auto& g : *fluxes) {
        mixCardinality(g.cardinality);
        for (const meshing::ElementId id : g.facets) {
            f.word(id.value());
        }
    }
    for (const auto& g : *held) {
        mixCardinality(g.cardinality);
    }
    for (const auto& g : *convecting) {
        mixCardinality(g.cardinality);
    }
    if (!fluxes->empty()) {
        const auto drawn = renderer::appliedHeatFluxOverlay(mesh, *fluxes, 0.002, 1000.0);
        REQUIRE(drawn.has_value());
        for (const renderer::ThermalOverlaySegment& s : drawn->segments) {
            f.real(s.from.x.si());
            f.real(s.from.y.si());
            f.real(s.to.z.si());
        }
    }
    const Result<renderer::ThermalPlayback> playback = renderer::ThermalPlayback::of(*derived.post);
    REQUIRE(playback.has_value());
    f.word(playback->frameCount());
    for (std::size_t i = 0; i < playback->frameCount(); ++i) {
        f.real(playback->timeOf(i).value_or(-1.0));
    }
    return f.hash;
}

} // namespace

TEST_CASE("ThermalViz_VisualizationDataIsDeterministicAndFingerprinted", "[thermal][viz][determinism][fingerprint]") {
    // Rebuilt from the same source, the same view settings give the same data: not "close", the same bits. The fingerprint lines
    // are compared by the post-run script ACROSS debug-ext, release-ext and debug-shared-ext.
    {
        VizStudy study(20_mm, 6_mm);
        define(study, steadyDefinition(study, {surfaceLoad(1, study.endCap(), 800.0), generationLoad(2, 5.0e4)},
                                       {temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.side(0), 25.0, kAmbient)}));
        const std::unique_ptr<Derived> a = derive(study);
        REQUIRE(a->post.has_value());
        const std::uint64_t first = digestOf(study, *a, 0);
        for (int run = 0; run < 5; ++run) {
            CHECK(digestOf(study, *a, 0) == first);
        }
        // A rebuilt derivation of the same analysis (a new result of the same source) digests identically.
        const std::unique_ptr<Derived> b = derive(study);
        CHECK(digestOf(study, *b, 0) == first);
        WARN(std::format("VIZFP steady-block {:016x}", first));
    }
    {
        VizStudy study(20_mm, 6_mm);
        define(study, transientDefinition(study, {generationLoad(1, 1.0e5)}, slabConditions(study), 300.0, 0.3, 1.0));
        const std::unique_ptr<Derived> a = derive(study);
        REQUIRE(a->post.has_value());
        for (std::size_t frame = 0; frame < a->post->stateCount(); ++frame) {
            const std::uint64_t first = digestOf(study, *a, frame);
            CHECK(digestOf(study, *a, frame) == first);
            WARN(std::format("VIZFP transient-block frame {} {:016x}", frame, first));
        }
    }
    {
        VizStudy study(3_mm, std::nullopt, true);
        define(study, steadyDefinition(study, {surfaceLoad(1, study.side(0), 800.0)},
                                       {temperatureBc(1, study.startCap(), 350.0), convectionBc(2, study.endCap(), 25.0, kAmbient)}));
        const std::unique_ptr<Derived> a = derive(study);
        REQUIRE(a->post.has_value());
        const std::uint64_t first = digestOf(study, *a, 0);
        CHECK(digestOf(study, *a, 0) == first);
        WARN(std::format("VIZFP steady-cylinder {:016x}", first));
    }
}
