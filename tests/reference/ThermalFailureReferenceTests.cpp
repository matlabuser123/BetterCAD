// RM-THERM-09 and RM-THERM-10 -- the reference models whose correct outcome is a REFUSAL (P18-REFMOD-001).
//
//   RM-THERM-09  a steady conduction problem with NO temperature datum. The temperature is determined only up to a constant, so the
//                solve must be refused explicitly. Forbidden: any finite temperature field returned as a success -- by a weak spring,
//                a pinned node, a pseudo-inverse. Two forms: net heat in (no solution exists) and net heat ZERO (a family of solutions
//                exists, and a solver that returns one member of it has invented a datum).
//   RM-THERM-10  a stale mesh (counts and ids unchanged), and a target that resolves to nothing, three ways. The refusal comes
//                BEFORE assembly where the architecture allows it, and nothing nearby is substituted.
//
// "Expected failure verified" is a PASS of these tests: the model is refused for the right reason, at the right stage, and
// nothing is published.

#include "reference/ThermalReferenceSupport.hpp"

#include <bettercad/meshing/GeometryMeshMap.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

#include <functional>
#include <set>

namespace {

using namespace bettercad;
using namespace bettercad::test::thermalref;

/// Replaces the definition of the run's analysis (a document edit, as any user's would be) WITHOUT running it.
void editDefinition(Run& run, const std::function<void(thermal::ThermalAnalysisDefinition&)>& change) {
    const thermal::ThermalAnalysis* object = thermal::findThermalAnalysis(run.document(), run.analysis());
    REQUIRE(object != nullptr);
    thermal::ThermalAnalysisDefinition definition = object->definition();
    change(definition);
    const Result<bool> set = run.document().modifyObject<thermal::ThermalAnalysis>(
        ObjectId::fromValue(run.analysis().value()),
        [&definition](thermal::ThermalAnalysis& a) -> Result<bool> { return a.setDefinition(definition); });
    REQUIRE(set.has_value());
}

[[nodiscard]] thermal::ThermalAnalysisDefinition definitionOf(Run& run) {
    const thermal::ThermalAnalysis* object = thermal::findThermalAnalysis(run.document(), run.analysis());
    REQUIRE(object != nullptr);
    return object->definition();
}

/// What a refused run must NOT have published: a solution, recovered fields, a balance.
void requireNothingPublished(const thermal::ThermalAnalysisRun& run) {
    CHECK_FALSE(run.accepted());
    CHECK_FALSE(run.steady.has_value());
    CHECK_FALSE(run.transient.has_value());
    CHECK_FALSE(run.post.has_value());
    CHECK_FALSE(run.balance.has_value());
}

/// The validation of an older run against the document as it is NOW.
[[nodiscard]] thermal::ThermalValidationReport judge(Run& run, const thermal::ThermalAnalysisRun& older) {
    return thermal::validateThermalAnalysis(run.document(), run.ref().regenerator(), run.ref().mesher(), run.analysis(),
                                            older.derivedState());
}

/// Sets the named parameter, in millimetres, and regenerates (the geometry moves; the mesh is NOT rebuilt).
void setParameterMm(Run& run, const char* name, double millimetres) {
    const auto item = run.document().findByName(name);
    REQUIRE(item.has_value());
    const auto parameter = run.document().asParameter(*item);
    REQUIRE(parameter.has_value());
    REQUIRE(run.document().setParameterValue(*parameter, millimetres * units::mm).has_value());
    const features::RegenerationReport report = run.ref().regenerate();
    REQUIRE(report.succeeded());
}

} // namespace

// ===========================================================================
// RM-THERM-09
// ===========================================================================

TEST_CASE("ThermalReference_RM09_PureNeumannSteadyFails", "[refmod][thermal][rm09]") {
    // Uniform generation 1e5 W/m^3 on a 60 x 40 x 25 mm block with every face insulated: 6 W with nowhere to go.
    auto run = runOf(reference::buildThermalSingularReferenceModel());
    const thermal::ThermalAnalysisRun& r = run->run();
    INFO(describe(r.validation));
    requireNothingPublished(r);
    REQUIRE(r.stoppedAt.has_value());
    CHECK(*r.stoppedAt == thermal::ThermalRunStage::Solve);
    REQUIRE(r.failure.has_value());
    // The refusal NAMES THE CAUSE (a missing datum), not a numerical symptom.
    CHECK_THAT(r.failure->message, Catch::Matchers::ContainsSubstring("datum"));
    CHECK(hasFinding(r.validation, thermal::ValidationCode::NoTemperatureDatum));
    CHECK(r.validation.statusOf(thermal::ValidationRule::WellPosed) == thermal::CheckStatus::Fail);
    CHECK(r.validation.status() == thermal::ValidationStatus::Fail);
    // and the inputs were fine: it is refused as UNDERDETERMINED, not as malformed
    CHECK(r.validation.statusOf(thermal::ValidationRule::Loads) == thermal::CheckStatus::Pass);
    CHECK(r.validation.statusOf(thermal::ValidationRule::MeshCurrent) == thermal::CheckStatus::Pass);
    evidence("RM-THERM-09 singular: stopped at " + std::string{thermal::toString(*r.stoppedAt)} + ", diagnostic: " + r.failure->message);
}

TEST_CASE("ThermalReference_RM09_NoArbitraryFiniteSolutionAccepted", "[refmod][thermal][rm09]") {
    // WHY ANY FINITE "SOLUTION" WOULD BE ARBITRARY -- shown with the independent solver, the one thing here that may pin a node:
    // with net heat in and one node held, the held node must remove exactly the generated heat WHATEVER temperature it is held
    // at. The pin does not make the problem solvable, it only moves the whole field with it: the datum is the pin's invention.
    auto run = runOf(reference::buildThermalSingularReferenceModel());
    const heat::HeatMesh plain = plainMesh(run->mesh());
    std::vector<std::vector<double>> fields;
    for (const double pin : {300.0, 500.0}) {
        std::vector<std::optional<double>> held(plain.nodes.size());
        held[0] = pin;
        const heat::HeatSolution pinned = heat::solveConduction(plain, 5.0, 1.0e5, held);
        requireClose(pinned.reaction[0], -pinned.generated, Tolerance{1e-9, 1e-10}, "the pin removes the whole net heat, whatever its value");
        requireClose(pinned.generated, 1.0e5 * 0.060 * 0.040 * 0.025, Tolerance{0.0, 1e-12}, "the generated heat, 1e5 x 6e-5 m^3 = 6 W");
        fields.push_back(pinned.temperature);
        evidence("RM-THERM-09 pinned at " + format(pin) + " K: reaction " + format(pinned.reaction[0]) + " W");
    }
    // the two pinned fields differ by exactly the difference of the pins, at every node: the shape is determined, the level is not
    for (std::size_t i = 0; i < plain.nodes.size(); ++i) {
        requireClose(fields[1][i] - fields[0][i], 200.0, Tolerance{1e-9, 0.0}, "the two pinned fields differ by the difference of the pins");
    }
    // and the production run published NOTHING: no field to be mistaken for the answer
    requireNothingPublished(run->run());
}

TEST_CASE("ThermalReference_RM09_BalancedNeumannStillMissingDatum", "[refmod][thermal][rm09]") {
    // +1000 W/m^2 in at the start cap and -1000 W/m^2 out at the end cap: the caps have the same area, so the net heat is ZERO and
    // the equations are CONSISTENT -- and the temperature is still undetermined up to a constant. A solver that returns one
    // member of that family (a pseudo-inverse, a pinned node, a tiny diagonal) is wrong in a way no residual shows.
    auto built = reference::buildThermalSingularBalancedReferenceModel();
    REQUIRE(built.has_value());
    const FaceName startFace = built->start();
    const FaceName endFace = built->end();
    auto run = std::make_unique<Run>(std::move(built->document));
    const thermal::ThermalAnalysisRun& r = run->run();
    INFO(describe(r.validation));
    requireNothingPublished(r);
    REQUIRE(r.stoppedAt.has_value());
    CHECK(*r.stoppedAt == thermal::ThermalRunStage::Solve);
    CHECK(hasFinding(r.validation, thermal::ValidationCode::NoTemperatureDatum));

    // The net heat IS zero: the two caps' areas, from the mesh's own boundary triangles at z = 0 and z = 25 mm.
    const meshing::Mesh& mesh = run->mesh();
    double startArea = 0.0;
    double endArea = 0.0;
    for (const meshing::Triangle& tri : mesh.triangles()) {
        std::array<Vec3, 3> p{};
        for (std::size_t c = 0; c < 3; ++c) {
            const meshing::Node* node = mesh.findNode(tri.nodes[c]);
            REQUIRE(node != nullptr);
            p[c] = positionOf(node->position);
        }
        const double area = 0.5 * heat::norm(heat::cross(heat::minus(p[1], p[0]), heat::minus(p[2], p[0])));
        const auto allAt = [&](double z) {
            return std::abs(p[0][2] - z) < 1e-9 && std::abs(p[1][2] - z) < 1e-9 && std::abs(p[2][2] - z) < 1e-9;
        };
        if (allAt(0.0)) {
            startArea += area;
        } else if (allAt(0.025)) {
            endArea += area;
        }
    }
    requireClose(startArea, 0.060 * 0.040, Tolerance{0.0, 1e-12}, "the start cap's area, 60 x 40 mm");
    requireClose(endArea, startArea, Tolerance{0.0, 1e-12}, "the end cap's area");
    // The net heat is read from the loads the MODEL DECLARES (flux x the area of the face it names), not assumed from the brief: a
    // model whose two fluxes differ is a different problem (net heat in, no solution) and must not pass as this one.
    double net = 0.0;
    std::size_t surfaceLoads = 0;
    for (const thermal::ThermalLoad& load : definitionOf(*run).loads) {
        const thermal::SurfaceHeatFluxLoad* surface = load.surfaceHeatFlux();
        REQUIRE(surface != nullptr);
        ++surfaceLoads;
        if (surface->face == startFace) {
            net += surface->flux.si() * startArea;
        } else if (surface->face == endFace) {
            net += surface->flux.si() * endArea;
        } else {
            FAIL("a load on a face that is neither cap");
        }
    }
    CHECK(surfaceLoads == 2);
    requireClose(net, 0.0, Tolerance{1e-12, 0.0}, "the net heat in");

    // A FAMILY OF SOLUTIONS EXISTS. With no source the homogeneous problem K T = 0 is solved by ANY constant: pin one node at
    // 300 K and at 500 K (independent solver) and both are valid, differing by exactly 200 K at every node -- the equations cannot
    // choose a level, so a solver that "returned" one would have chosen it itself.
    const heat::HeatMesh plain = plainMesh(mesh);
    std::vector<std::optional<double>> pin300(plain.nodes.size());
    std::vector<std::optional<double>> pin500(plain.nodes.size());
    pin300[0] = 300.0;
    pin500[0] = 500.0;
    const heat::HeatSolution a = heat::solveConduction(plain, 5.0, 0.0, pin300);
    const heat::HeatSolution b = heat::solveConduction(plain, 5.0, 0.0, pin500);
    for (std::size_t i = 0; i < plain.nodes.size(); ++i) {
        requireClose(a.temperature[i], 300.0, Tolerance{1e-9, 0.0}, "the sourceless solution with one node at 300 K is 300 K everywhere");
        requireClose(b.temperature[i] - a.temperature[i], 200.0, Tolerance{1e-9, 0.0}, "and shifts by exactly the pin's difference");
    }
    evidence("RM-THERM-09 balanced: net heat " + format(net) + " W; two pins differ by " + format(b.temperature[1] - a.temperature[1]) + " K everywhere");
}

TEST_CASE("ThermalReference_RM09_AFailedSolveLeavesTheOldResultStale", "[refmod][thermal][rm09]") {
    // THE LIFECYCLE. R1 is a valid, current result. The model is edited into a singular one (the held caps are removed and
    // generation added): R1 is STALE, the new solve FAILS, and the result of the failed attempt is NOTHING -- R1 must not come
    // back as current, and no partial result may appear.
    auto run = runOf(reference::buildThermalPatchReferenceModel());
    REQUIRE(run->run().accepted());
    const std::unique_ptr<thermal::ThermalAnalysisRun> r1 = run->takeRun();
    // R1 is current while the model is unchanged
    CHECK(judge(*run, *r1).accepted());

    editDefinition(*run, [](thermal::ThermalAnalysisDefinition& d) {
        d.boundaryConditions.clear();
        d.loads.clear();
        d.loads.push_back(thermal::ThermalLoad{ThermalLoadId::fromValue(1),
                                               thermal::VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(1.0e5)}});
    });
    // R1 is stale now
    const thermal::ThermalValidationReport stale = judge(*run, *r1);
    INFO(describe(stale));
    CHECK_FALSE(stale.accepted());
    CHECK(stale.statusOf(thermal::ValidationRule::SourceCompatibility) == thermal::CheckStatus::Fail);
    // the new solve fails and publishes nothing
    run->rerun();
    requireNothingPublished(run->run());
    CHECK(hasFinding(run->run().validation, thermal::ValidationCode::NoTemperatureDatum));
    // and R1 is STILL stale after the failed attempt
    const thermal::ThermalValidationReport after = judge(*run, *r1);
    CHECK_FALSE(after.accepted());
    CHECK(after.statusOf(thermal::ValidationRule::SourceCompatibility) == thermal::CheckStatus::Fail);
}

// ===========================================================================
// RM-THERM-10
// ===========================================================================

TEST_CASE("ThermalReference_RM10_StaleMeshRejected", "[refmod][thermal][rm10]") {
    // R1 on the mesh M1; the depth is edited (30 -> 36 mm) and the model regenerated, but NOT remeshed: the held mesh describes a
    // body that no longer exists. The run is refused at the MODEL stage -- before any load is resolved, before assembly -- and says
    // why; R1 is not presentable as current.
    auto run = runOf(reference::buildThermalStaleMeshReferenceModel());
    REQUIRE(run->run().accepted());
    const std::unique_ptr<thermal::ThermalAnalysisRun> r1 = run->takeRun();
    CHECK(run->ref().currency() == meshing::MeshCurrency::Current);

    setParameterMm(*run, "stale_l", 36.0);
    CHECK(run->ref().currency() == meshing::MeshCurrency::StaleGeometry);

    run->rerun();
    const thermal::ThermalAnalysisRun& r2 = run->run();
    INFO(describe(r2.validation));
    requireNothingPublished(r2);
    REQUIRE(r2.stoppedAt.has_value());
    CHECK(*r2.stoppedAt == thermal::ThermalRunStage::Model); // before the dof map, the loads, the boundary and the assembly
    CHECK_FALSE(r2.dofMap.has_value());
    CHECK_FALSE(r2.system.has_value());
    CHECK(hasFinding(r2.validation, thermal::ValidationCode::MeshStale));
    CHECK(r2.validation.statusOf(thermal::ValidationRule::MeshCurrent) == thermal::CheckStatus::Fail);
    // R1 is not current against the model as it now is
    CHECK_FALSE(judge(*run, *r1).accepted());
}

TEST_CASE("ThermalReference_RM10_SameCountsAndSameIdsStillStale", "[refmod][thermal][rm10]") {
    // THE ADVERSARIAL CASE. After the edit the model is REMESHED: M2 has exactly the same node and tetrahedron COUNTS as M1 and the
    // very same numeric NodeIds and ElementIds. Counts are not provenance and ids are not provenance: R1, bound to M1, is stale
    // against M2.
    auto run = runOf(reference::buildThermalStaleMeshReferenceModel());
    REQUIRE(run->run().accepted());
    const std::unique_ptr<thermal::ThermalAnalysisRun> r1 = run->takeRun();
    const auto idsOf = [](const meshing::Mesh& mesh) {
        std::set<std::uint64_t> nodes;
        std::set<std::uint64_t> tets;
        for (const meshing::Node& n : mesh.nodes()) {
            nodes.insert(n.id.value());
        }
        for (const meshing::Tetrahedron& t : mesh.tetrahedra()) {
            tets.insert(t.id.value());
        }
        return std::pair{nodes, tets};
    };
    const std::size_t nodes1 = run->mesh().nodes().size();
    const std::size_t tets1 = run->mesh().tetrahedra().size();
    const auto ids1 = idsOf(run->mesh());
    double qmax1 = 0.0;
    for (const HeatFlux& m : r1->post->finalState().fields.heatFluxMagnitudes) {
        qmax1 = std::max(qmax1, m.si());
    }

    setParameterMm(*run, "stale_l", 36.0);
    run->remesh();
    // M2: the same counts, the same ids -- asserted, because the whole point is that they are indistinguishable by these
    CHECK(run->mesh().nodes().size() == nodes1);
    CHECK(run->mesh().tetrahedra().size() == tets1);
    const auto ids2 = idsOf(run->mesh());
    CHECK(ids2.first == ids1.first);
    CHECK(ids2.second == ids1.second);
    CHECK(run->ref().currency() == meshing::MeshCurrency::Current);

    // R1 against the current model: STALE, by its source, whatever its counts and ids say.
    const thermal::ThermalValidationReport old = judge(*run, *r1);
    INFO(describe(old));
    CHECK_FALSE(old.accepted());
    CHECK(old.statusOf(thermal::ValidationRule::SourceCompatibility) == thermal::CheckStatus::Fail);

    // the model on M2 is a different problem and runs: its heat flux is k dT/L with L = 36 mm instead of 30 mm
    run->rerun();
    REQUIRE(run->run().accepted());
    double qmax2 = 0.0;
    for (const HeatFlux& m : run->post().finalState().fields.heatFluxMagnitudes) {
        qmax2 = std::max(qmax2, m.si());
    }
    requireClose(qmax1, 5.0 * 100.0 / 0.030, Tolerance{1e-9, 1e-9}, "the old flux, k dT/L with L = 30 mm");
    requireClose(qmax2, 5.0 * 100.0 / 0.036, Tolerance{1e-9, 1e-9}, "the new flux, k dT/L with L = 36 mm");
    CHECK(qmax2 != qmax1);
    evidence("RM-THERM-10 same counts " + std::to_string(nodes1) + "/" + std::to_string(tets1) + " same ids; flux " + format(qmax1) +
             " -> " + format(qmax2) + "; R1 stale: " + describe(old));
}

TEST_CASE("ThermalReference_RM10_RemeshOfAnUneditedModelStillStalesTheOldResult", "[refmod][thermal][rm10]") {
    // THE PURE FORM OF THE TRAP: nothing about the model is edited -- no parameter, no setting, no intent -- and the mesh is simply
    // generated again. The new mesh has the same counts and the same ids and, for a deterministic mesher, the same coordinates. The
    // old result is bound to the OLD mesh object (its stamp), and nothing else distinguishes the two: a check that compared counts,
    // ids or geometry revisions would pass it as current, and only a check of the mesh's own identity refuses it.
    auto run = runOf(reference::buildThermalStaleMeshReferenceModel());
    REQUIRE(run->run().accepted());
    const std::unique_ptr<thermal::ThermalAnalysisRun> r1 = run->takeRun();
    CHECK(judge(*run, *r1).accepted()); // current before the remesh
    const std::vector<double> before = finalTemperatures(*r1->post);
    const std::size_t nodes = run->mesh().nodes().size();
    const std::size_t tets = run->mesh().tetrahedra().size();

    run->remeshOnly(); // no regenerate: the geometry revision, the document and the intent are exactly what they were
    CHECK(run->mesh().nodes().size() == nodes);
    CHECK(run->mesh().tetrahedra().size() == tets);
    const thermal::ThermalValidationReport old = judge(*run, *r1);
    INFO(describe(old));
    CHECK_FALSE(old.accepted());
    CHECK(old.statusOf(thermal::ValidationRule::SourceCompatibility) == thermal::CheckStatus::Fail);
    CHECK(hasFinding(old, thermal::ValidationCode::SourceStale));

    // the new run is accepted and gives the same physics: the refusal was about PROVENANCE, not about the numbers
    run->rerun();
    REQUIRE(run->run().accepted());
    CHECK(maxAbsDifference(finalTemperatures(run->post()), before) <= 1e-9);
}

TEST_CASE("ThermalReference_RM10_UnresolvedTargetsAreRefusedBeforeAssembly", "[refmod][thermal][rm10]") {
    struct Case {
        reference::ThermalUnresolvedTarget target;
        thermal::ThermalRunStage stage;
        thermal::ValidationCode code;
        const char* name;
    };
    for (const Case& c : {Case{reference::ThermalUnresolvedTarget::Load, thermal::ThermalRunStage::Loads, thermal::ValidationCode::LoadsInvalid, "load"},
                          Case{reference::ThermalUnresolvedTarget::Temperature, thermal::ThermalRunStage::Boundary,
                               thermal::ValidationCode::BoundaryConditionsInvalid, "held temperature"},
                          Case{reference::ThermalUnresolvedTarget::Convection, thermal::ThermalRunStage::Boundary,
                               thermal::ValidationCode::BoundaryConditionsInvalid, "convection"}}) {
        INFO(c.name);
        auto built = reference::buildThermalUnresolvedReferenceModel(c.target);
        REQUIRE(built.has_value());
        const FaceName missing = built->missing();
        const FaceName start = built->start();
        const FaceName end = built->end();
        Run run(std::move(built->document));
        const thermal::ThermalAnalysisDefinition before = definitionOf(run);

        const thermal::ThermalAnalysisRun& r = run.run();
        INFO(describe(r.validation));
        requireNothingPublished(r);
        REQUIRE(r.stoppedAt.has_value());
        CHECK(*r.stoppedAt == c.stage);
        CHECK(hasFinding(r.validation, c.code));
        // REFUSED BEFORE ASSEMBLY: no system was built, and (for a load) no boundary was even prepared
        CHECK_FALSE(r.system.has_value());
        if (c.target == reference::ThermalUnresolvedTarget::Load) {
            CHECK_FALSE(r.boundary.has_value());
        }
        // THE REFUSAL SAYS THE TARGET IS UNRESOLVED, by the cause upstream assigned
        bool unresolved = false;
        for (const thermal::ValidationFinding& f : r.validation.findings) {
            unresolved = unresolved || (f.cause == "target_unresolved");
        }
        CHECK(unresolved);

        // NO NEAREST-FACE REBINDING: the missing face resolves to NO facet at all, the real faces to their two triangles each, and
        // the canonical definition is bit for bit what it was.
        Result<meshing::BoundaryFacetSet> none = meshing::boundaryFacetsOf(run.ref().map(), missing);
        REQUIRE(none.has_value());
        CHECK_FALSE(none->fullyResolved());
        CHECK(none->facets.empty());
        REQUIRE(none->requested.size() == 1);
        CHECK(none->requested.front().state == meshing::MappingState::Unresolved);
        for (const FaceName& real : {start, end}) {
            Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(run.ref().map(), real);
            REQUIRE(facets.has_value());
            CHECK(facets->fullyResolved());
            CHECK(facets->facets.size() == 2);
        }
        const thermal::ThermalAnalysisDefinition afterwards = definitionOf(run);
        CHECK(afterwards == before);
        evidence(std::string{"RM-THERM-10 unresolved "} + c.name + ": stopped at " + std::string{thermal::toString(*r.stoppedAt)} + ", " +
                 std::string{thermal::toString(c.code)});

        // and WITHOUT the unresolvable target the very same document is accepted: the refusal is about that target and nothing else
        editDefinition(run, [&](thermal::ThermalAnalysisDefinition& d) {
            std::erase_if(d.loads, [&](const thermal::ThermalLoad& l) { return l.target().has_value() && *l.target() == missing; });
            std::erase_if(d.boundaryConditions, [&](const thermal::ThermalBoundaryCondition& b) { return b.face() == missing; });
        });
        run.rerun();
        INFO(describe(run.run().validation));
        // the valid held temperature and convection remain, so the document is now ACCEPTED: the refusal was about that target alone
        CHECK(run.run().accepted());
        CHECK_FALSE(hasFinding(run.run().validation, c.code));
    }
}
