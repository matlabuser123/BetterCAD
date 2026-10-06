#include "reference/MeshTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/parameters/Parameter.hpp>
#include <bettercad/core/parameters/ParameterTable.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

// P16-REFMOD-001 -- the meshing reference models.
//
// WHAT MAKES THIS A QUALIFICATION SUITE RATHER THAN A SET OF DEMOS. Every
// expected number comes from a closed form applied to the model's OWN
// dimensions, read from its own parameters -- never from a volume BetterCAD
// computed. The chain is four-way, and each link catches a different mistake:
//
//   the model's dimensions, from its parameters
//       -> a closed form in tests/reference/Analytic.hpp   (independent)
//       -> the catalog's DECLARED analytic volume          (a typo in either)
//       -> the kernel's CAD volume                         (OCCT, as a second
//                                                           implementation)
//       -> the tetrahedral volume                          (the real gate)
//
// A suite that compared the mesh against `mesh.cadVolume()` would be asking
// OCCT to mark its own work. A suite that took the expected volume from the
// catalog alone could not tell a builder that builds the wrong block from a
// catalog that declares the wrong volume.
//
// AND EVERY STRUCTURAL FACT IS RECOMPUTED. `generateVolumeMesh` refuses an
// inverted, degenerate or duplicate element, so a published mesh cannot contain
// one -- which is exactly why the suite counts them itself, from the node
// coordinates, in `MeshTestSupport.hpp`. The claim under test is the refusal.
namespace {

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::meshref;
using bettercad::reference::MeshReferenceModelInfo;
using bettercad::reference::MeshReferenceModelKind;
using bettercad::reference::kMeshReferenceModels;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using meshing::MeshCurrency;
using meshing::VolumeMesh;
using meshing::VolumeMeshControls;

/// The tolerance for a PLANAR body's volume: the facets tile the exact solid,
/// so only floating-point accumulation separates the sum from the closed form.
/// The repository's figure for geometric accumulation is 1e-9; measured on
/// these models the worst is 2.4e-16, so this is four orders of margin and is
/// not the thing any test here is really measuring.
constexpr double kPlanarVolume = 1e-9;

/// The tolerance for a CAD volume against its closed form: the kernel
/// integrates planes and cylinders to rounding, and 1e-12 is the repository's
/// figure for well-conditioned double-precision algebra. Measured worst on
/// these models: 2.0e-16.
constexpr double kCadVolume = 1e-12;

/// The tolerance for a quantity that must be invariant under a RIGID transform
/// of the CAD body. A rotation is three multiplies and two adds per coordinate,
/// so the volume integral loses a few units in the last place and nothing more.
/// Measured: 0 for the analytic pair and 1.3e-16 relative for the kernel's.
///
/// DELIBERATELY NOT the meshing bound: the brief is explicit that mesh
/// approximation error must not be used to excuse rigid-transform drift in the
/// CAD oracle.
constexpr double kRigidInvariant = 1e-12;

/// The declared surface deflection of the suite's curved models, in mm. It is
/// what the analytic bounds are derived from; the models' own control carries
/// it, and `MeshCurved_DeclaresTheDeflectionTheBoundsAssume` checks that this
/// figure and the models' agree.
constexpr double kDeflectionMm = 0.25;

/// A length parameter's value in mm, read from the document.
[[nodiscard]] double mm(const Document& document, std::string_view name) {
    const std::optional<ObjectId> object = document.findByName(name);
    REQUIRE(object.has_value());
    const std::optional<ParameterId> id = document.asParameter(*object);
    REQUIRE(id.has_value());
    const Parameter* parameter = document.parameters().find(*id);
    REQUIRE(parameter != nullptr);
    return Length::fromSi(parameter->siValue()).in(units::mm);
}

/// The analytic CAD volume of @p kind, in mm^3, from the DOCUMENT'S OWN
/// dimensions through the independent closed forms.
///
/// This is the suite's oracle. Nothing in it reads a volume: it reads lengths
/// the model stores as parameters and multiplies them.
[[nodiscard]] double analyticVolumeOf(const Document& document, MeshReferenceModelKind kind) {
    using namespace bettercad::test::analytic;
    switch (kind) {
    case MeshReferenceModelKind::Block:
        return blockVolumeMm3(mm(document, "block_a"), mm(document, "block_b"), mm(document, "block_c"));
    case MeshReferenceModelKind::Cylinder:
        return cylinderVolumeMm3(mm(document, "cyl_r"), mm(document, "cyl_h"));
    case MeshReferenceModelKind::PlateWithHole:
        return plateWithHoleVolumeMm3(mm(document, "plate_l"), mm(document, "plate_w"),
                                      mm(document, "plate_t"), mm(document, "plate_hole_r"));
    case MeshReferenceModelKind::Tube:
        return tubeVolumeMm3(mm(document, "tube_ro"), mm(document, "tube_ri"), mm(document, "tube_h"));
    case MeshReferenceModelKind::ThinPlate:
        return blockVolumeMm3(mm(document, "thin_l"), mm(document, "thin_w"), mm(document, "thin_t"));
    case MeshReferenceModelKind::TransformedBase:
        return blockVolumeMm3(mm(document, "base_a"), mm(document, "base_b"), mm(document, "base_c"));
    case MeshReferenceModelKind::TransformedPlaced:
        return blockVolumeMm3(mm(document, "placed_a"), mm(document, "placed_b"),
                              mm(document, "placed_c"));
    case MeshReferenceModelKind::LocalRefinement:
        return blockVolumeMm3(mm(document, "local_a"), mm(document, "local_b"), mm(document, "local_c"));
    case MeshReferenceModelKind::OpenProfile:
        // No body, so no volume. Not zero-as-a-number: the model has none.
        return 0.0;
    }
    FAIL("unknown mesh reference model kind");
    return 0.0;
}

/// The whole structural audit, as a gate. Every item of the brief's shared
/// structural checks, measured in the test and reported before being judged.
void requireStructurallySound(const VolumeMesh& mesh, std::string_view model) {
    const Structure structure = auditStructure(mesh.mesh());
    INFO(std::format(
        "{}: {} nodes, {} tets, {} boundary triangles | orientation +{} 0{} -{} nonfinite {} | "
        "missing {} repeated {} duplicate {} regionless {} | faces interior {} boundary {} overused {} "
        "unmatched {}/{} | enclosed {:.17g} mm^3, signed sum {:.17g} mm^3, min tet {:.6g} mm^3",
        model, structure.nodes, structure.tets, structure.triangles, structure.orientation.positive,
        structure.orientation.zero, structure.orientation.negative, structure.orientation.nonFinite,
        structure.missingNodeReferences, structure.repeatedNodeReferences, structure.duplicateTets,
        structure.elementsWithoutRegion, structure.incidence.interiorFaces,
        structure.incidence.boundaryFaces, structure.incidence.overusedFaces,
        structure.incidence.boundaryFacesWithoutTriangle,
        structure.incidence.trianglesWithoutBoundaryFace, structure.enclosedVolume,
        structure.orientation.totalVolume, structure.orientation.minVolume));

    CHECK(structure.nodes > 0);
    CHECK(structure.tets > 0);
    CHECK(structure.triangles > 0);
    CHECK(structure.nonFiniteCoordinates == 0);
    CHECK(structure.missingNodeReferences == 0);
    CHECK(structure.repeatedNodeReferences == 0);
    CHECK(structure.duplicateTets == 0);
    CHECK(structure.elementsWithoutRegion == 0);
    CHECK(structure.unexpectedElementTypes == 0);
    // ORIENTATION, counted: zero and negative must both be zero, and every
    // tetrahedron must be accounted for.
    CHECK(structure.orientation.zero == 0);
    CHECK(structure.orientation.negative == 0);
    CHECK(structure.orientation.nonFinite == 0);
    CHECK(structure.orientation.positive == structure.tets);
    // MINIMUM ELEMENT VOLUME against the structural zero, not against a
    // quality threshold: P16-DATA-001 refuses an element whose signed volume is
    // exactly zero or not finite, and that is the bound this asserts.
    CHECK(structure.orientation.minVolume > 0.0);
    CHECK(std::isfinite(structure.orientation.minVolume));
    // The boundary of the tetrahedra, from their own incidence, IS the stored
    // triangle set -- computed here, and then compared with what the mesh
    // recorded about itself.
    CHECK(structure.incidence.overusedFaces == 0);
    CHECK(structure.incidence.boundaryFacesWithoutTriangle == 0);
    CHECK(structure.incidence.trianglesWithoutBoundaryFace == 0);
    CHECK(structure.incidence.boundaryFaces == structure.triangles);
    // Outward-facing: a positive enclosed volume.
    CHECK(structure.enclosedVolume > 0.0);
    CHECK(structure.sound());

    // AND NOW the production figures, as a cross-check rather than as the
    // verdict. They must agree with what the suite measured for itself.
    CHECK(meshing::validate(mesh.mesh()).dataValid());
    CHECK(mesh.conformity().conforms());
    CHECK(mesh.conformity().unmatchedBoundaryFaceCount == 0);
    CHECK(mesh.conformity().unmatchedSurfaceTriangleCount == 0);
    CHECK(mesh.boundaryTriangleCount() == structure.triangles);
    CHECK(mesh.tetrahedronCount() == structure.tets);
    CHECK_THAT(mesh.tetrahedralVolume().in(units::mm3),
               WithinRel(structure.orientation.totalVolume, 1e-12));
    CHECK_THAT(enclosedVolumeMm3(mesh.mesh()), WithinRel(structure.orientation.totalVolume, 1e-12));
}

/// The mapping gate: a complete partition, nothing unmapped, nothing
/// attributed twice, and every CAD face represented.
void requireMappingComplete(const meshing::GeometryMeshMap& map, std::size_t expectedFaces,
                            std::string_view model) {
    const meshing::GeometryMeshMappingReport& report = map.report();
    INFO(std::format("{}: {} CAD faces ({} named, {} unnamed), {} facets, {} mapped, {} unmapped, "
                     "{} attributed twice, {} faces without facets, {} issues",
                     model, report.cadFaceCount, report.namedFaceCount, report.unnamedFaceCount,
                     report.boundaryFacetCount, report.mappedFacetCount, report.unmappedFacetCount,
                     report.facetsWithSeveralFaces, report.facesWithoutFacets, report.issues.size()));
    CHECK(report.cadFaceCount == expectedFaces);
    CHECK(report.unmappedFacetCount == 0);
    CHECK(report.facetsWithSeveralFaces == 0);
    CHECK(report.facesWithoutFacets == 0);
    CHECK(report.mappedFacetCount == report.boundaryFacetCount);
    CHECK(report.complete());
    CHECK(report.issues.empty());
}

/// The facets a named reference resolves to, requiring that it resolves.
[[nodiscard]] std::vector<meshing::ElementId> facetsOf(const meshing::GeometryMeshMap& map,
                                                       const FaceName& reference) {
    auto resolved = meshing::boundaryFacetsOf(map, reference);
    REQUIRE(resolved.has_value());
    INFO("the reference did not resolve");
    REQUIRE(resolved->fullyResolved());
    CHECK_FALSE(resolved->facets.empty());
    return resolved->facets;
}

/// Whether two facet sets share anything. Leakage between two CAD faces'
/// facets is the defect the suite's region-by-region mapping exists to find.
[[nodiscard]] std::size_t sharedFacets(const std::vector<meshing::ElementId>& a,
                                       const std::vector<meshing::ElementId>& b) {
    std::set<meshing::ElementId::ValueType> left;
    for (const meshing::ElementId id : a) {
        left.insert(id.value());
    }
    std::size_t shared = 0;
    for (const meshing::ElementId id : b) {
        if (left.contains(id.value())) {
            ++shared;
        }
    }
    return shared;
}

/// The quality gate every valid model passes: structurally valid, no invalid
/// element, and the policy itself usable.
///
/// UNDER P16-QUALITY-001'S OWN DEFAULT POLICY, which carries no thresholds at
/// all -- so nothing here can be a Warning or a Failure, and `satisfiesPolicy`
/// reduces to structural validity. That is the honest state of the product: no
/// solver has stated a requirement, and the brief forbids inventing one for the
/// reference models.
void requireQualityReported(const meshing::MeshQualityReport& report, const VolumeMesh& mesh,
                            std::string_view model) {
    const ShapeExtremes shape = shapeExtremesOf(report);
    INFO(std::format("{}: {} tets {} triangles | valid {} warning {} failure {} invalid {} | "
                     "worst aspect {:.6g} worst radius ratio {:.6g} dihedral {:.6g}..{:.6g} deg | "
                     "min tet volume {:.6g} mm^3",
                     model, report.tetCount, report.triangleCount, report.validElements,
                     report.warningElements, report.failureElements, report.invalidElements,
                     shape.worstAspectRatio, shape.worstRadiusRatio, shape.minDihedralDeg,
                     shape.maxDihedralDeg, shape.minVolumeMm3));
    CHECK(report.structurallyValid);
    CHECK(report.invalidElements == 0);
    CHECK(report.satisfiesPolicy());
    CHECK_FALSE(report.thresholdPolicyError.has_value());
    CHECK(report.tetCount == mesh.tetrahedronCount());
    CHECK(report.triangleCount == mesh.boundaryTriangleCount());
    // Every element classified, and the classes summing to the element count.
    CHECK(report.validElements + report.warningElements + report.failureElements +
              report.invalidElements ==
          report.tetCount + report.triangleCount);
    // Every tetrahedron's metrics defined and finite. A near-degenerate element
    // would make them undefined, and the report would call it Invalid.
    for (const meshing::TetQuality& tet : report.tets) {
        CHECK(tet.defined);
        CHECK(std::isfinite(tet.aspectRatio));
        CHECK(std::isfinite(tet.radiusRatio));
        CHECK(tet.radiusRatio > 0.0);
        CHECK(tet.volume.si() > 0.0);
    }
}

} // namespace

#ifdef BETTERCAD_TESTS_EXPECT_NETGEN

// ---------------------------------------------------------------------------
// The suite itself: what it covers, and that it covers all of it

TEST_CASE("MeshReferenceSuite_DeclaresEightModelsInNineDocuments", "[refmod][mesh][suite]") {
    // EIGHT MANDATORY MODEL IDS, and nine catalog entries because RM-MESH-06
    // is a pair -- a body and the same body rigidly transformed -- and
    // comparing the two is the case.
    std::set<std::string_view> ids;
    std::set<std::string_view> names;
    std::set<std::string_view> stems;
    for (const MeshReferenceModelInfo& info : kMeshReferenceModels) {
        ids.insert(info.id);
        CHECK(names.insert(info.name).second);
        CHECK(stems.insert(info.fileStem).second);
        CHECK_FALSE(info.purpose.empty());
        CHECK_FALSE(info.mainParameter.empty());
        CHECK(info.mainParameterMm > 0.0);
    }
    CHECK(kMeshReferenceModels.size() == 9);
    CHECK(ids.size() == bettercad::reference::kMeshReferenceModelIdCount);
    CHECK(ids.size() == 8);
    for (int i = 1; i <= 8; ++i) {
        const std::string expected = std::format("RM-MESH-0{}", i);
        INFO("the suite must declare " << expected);
        CHECK(ids.contains(expected));
    }
    // Exactly one entry expects no mesh: RM-MESH-08.
    const auto refusals = std::ranges::count_if(
        kMeshReferenceModels, [](const MeshReferenceModelInfo& info) { return !info.expectMesh; });
    CHECK(refusals == 1);
    for (const MeshReferenceModelInfo& info : kMeshReferenceModels) {
        if (!info.expectMesh) {
            CHECK(info.id == "RM-MESH-08");
            // The failure model carries NO fabricated volume. The brief is
            // explicit: do not force a field to a fake zero -- and this zero is
            // the absence of a body, which the suite's oracle also returns.
            CHECK(info.analyticVolumeMm3 == 0.0);
        } else {
            CHECK(info.analyticVolumeMm3 > 0.0);
        }
    }
}

TEST_CASE("MeshReferenceSuite_EveryModelBuildsAndCarriesItsMeshingIntent", "[refmod][mesh][suite]") {
    std::set<ObjectId::ValueType> documentIds;
    for (const MeshReferenceModelInfo& info : kMeshReferenceModels) {
        INFO(info.name);
        auto document = bettercad::reference::buildMeshReferenceModel(info.kind);
        REQUIRE(document.has_value());
        CHECK(document->name() == info.name);
        // Every model's document ID is fixed and distinct, so a saved model is
        // the same document every time.
        CHECK(document->id().isValid());

        // THE INTENT IS A DOCUMENT OBJECT, in the dependency graph, naming the
        // body it meshes. That is what makes a geometry change able to
        // invalidate the derived mesh.
        const std::optional<ObjectId> control = document->findByName("Mesh");
        REQUIRE(control.has_value());
        const auto* definition = document->findObjectAs<meshing::MeshControl>(*control);
        REQUIRE(definition != nullptr);
        // THE LITERAL, NOT MeshControl::kTypeName. Binding a reference to a
        // dll-imported constexpr static does not link in a shared build, which
        // is the convention `MaterialTests.cpp` and `DocumentJson.cpp` already
        // record -- and which `MeshControlJson.cpp`'s own static_assert on
        // `kTypeName == "mesh-control"` keeps from drifting. Written as
        // `kTypeName`, this linked in both static presets and failed only in
        // `debug-shared-ext`.
        CHECK(definition->typeName() == "mesh-control");
        CHECK(definition->definition().body.isValid());
        CHECK(definition->dependencies() == std::vector<ObjectId>{definition->definition().body});
        CHECK(meshing::validate(definition->definition()).has_value());
        // It holds NO result: no nodes, no elements, no quality report. The
        // type makes that structural, and the suite states it because it is the
        // architectural rule the whole milestone rests on.
        CHECK(definition->definition().mesh.sizing.globalTargetSize.has_value());
        CHECK_FALSE(definition->definition().boundarySets.empty());
        // The policy is the product's own default: no thresholds.
        CHECK(definition->definition().quality == meshing::reportOnlyThresholds());
        CHECK(definition->definition().quality.limits.empty());
    }
}

TEST_CASE("MeshReferenceSuite_DeclaredVolumesAgreeWithTheIndependentClosedForms",
          "[refmod][mesh][suite][analytic]") {
    // LEG ONE AND TWO of the four-way chain: the model's own dimensions through
    // a closed form that knows nothing about BetterCAD, against the volume the
    // catalog declares. A typo in either is a failure here, before any mesh
    // exists.
    for (const MeshReferenceModelInfo& info : kMeshReferenceModels) {
        INFO(info.name);
        auto document = bettercad::reference::buildMeshReferenceModel(info.kind);
        REQUIRE(document.has_value());
        const double derived = analyticVolumeOf(*document, info.kind);
        INFO(std::format("declared {:.17g} mm^3, derived from the model's parameters {:.17g} mm^3",
                         info.analyticVolumeMm3, derived));
        if (!info.expectMesh) {
            CHECK(derived == 0.0);
            continue;
        }
        // Equal to the last bit: both sides are the same products of the same
        // doubles, so anything but agreement is a transcription error.
        CHECK_THAT(derived, WithinRel(info.analyticVolumeMm3, 1e-15));
    }
}

TEST_CASE("MeshReferenceSuite_EveryValidModelMeshesAndTheInvalidOneIsRefused",
          "[refmod][mesh][suite][gate]") {
    // THE EXECUTION GATE. Every one of the nine documents is built and put
    // through the whole stack here, in one case, so that "eight models
    // executed" is a measured count rather than an inventory of test names. A
    // model silently skipped would make this count wrong.
    std::set<std::string_view> executed;
    std::size_t meshed = 0;
    std::size_t refused = 0;
    std::vector<std::string> rows;
    std::vector<ReferenceMeshRun> runs;

    for (const MeshReferenceModelInfo& info : kMeshReferenceModels) {
        INFO(info.name);
        auto document = bettercad::reference::buildMeshReferenceModel(info.kind);
        REQUIRE(document.has_value());
        MeshedReference model(std::move(*document));
        executed.insert(info.id);

        ReferenceMeshRun run;
        run.modelId = std::string{info.id};
        run.modelName = std::string{info.name};
        run.expectedMesh = info.expectMesh;
        run.analyticVolumeMm3 = analyticVolumeOf(model.document(), info.kind);

        auto generated = model.generate();
        run.producedMesh = generated.has_value();
        CHECK(run.producedMesh == info.expectMesh);

        if (!info.expectMesh) {
            ++refused;
            // NOTHING IS PUBLISHED. Not a mesh with no elements, not a stale
            // one relabelled -- nothing.
            CHECK(model.mesher().heldMeshCount() == 0);
            CHECK(model.mesher().mesh(model.control()) == nullptr);
            CHECK(model.mesher().map(model.control()) == nullptr);
            CHECK(model.mesher().quality(model.control()) == nullptr);
            CHECK(model.currency() == MeshCurrency::GenerationFailed);
            run.diagnostic = generated.error().message;
            rows.push_back(resultRow(run));
            runs.push_back(run);
            continue;
        }

        ++meshed;
        const VolumeMesh& mesh = **generated;
        requireStructurallySound(mesh, info.name);
        requireQualityReported(model.quality(), mesh, info.name);

        run.cadVolumeMm3 = mesh.cadVolume().in(units::mm3);
        run.meshVolumeMm3 = mesh.tetrahedralVolume().in(units::mm3);
        run.relativeVolumeError =
            std::abs(run.meshVolumeMm3 - run.analyticVolumeMm3) / run.analyticVolumeMm3;
        run.nodeCount = mesh.nodeCount();
        run.elementCount = mesh.tetrahedronCount();
        run.boundaryFacetCount = mesh.boundaryTriangleCount();
        run.structure = auditStructure(mesh.mesh());
        run.minElementVolumeMm3 = run.structure.orientation.minVolume;
        run.shape = shapeExtremesOf(model.quality());
        run.invalidElements = model.quality().invalidElements;
        run.warningElements = model.quality().warningElements;
        run.failureElements = model.quality().failureElements;
        run.unmappedFacets = model.map().report().unmappedFacetCount;
        run.ambiguousFacets = model.map().report().facetsWithSeveralFaces;

        // LEG THREE: OCCT as a second implementation of the volume, against the
        // same closed form. Independent of the mesh entirely.
        CHECK_THAT(run.cadVolumeMm3, WithinRel(run.analyticVolumeMm3, kCadVolume));
        // A freshly generated mesh describes the model.
        CHECK(meshing::describesTheModel(model.currency()));
        CHECK_FALSE(meshing::isStale(model.document(), mesh));
        CHECK_FALSE(meshing::isStale(model.document(), mesh, mesh.controls()));
        // The mesh names the body it was built from, and the right one.
        CHECK(mesh.source() == model.body());
        CHECK(mesh.revision().isValid());
        // Every boundary set the model declares resolves against it.
        for (const meshing::NamedBoundarySet& set : model.definition()->orderedBoundarySets()) {
            INFO("boundary set " << set.name);
            auto resolved = meshing::resolveBoundarySet(set, model.map());
            REQUIRE(resolved.has_value());
            CHECK(resolved->id == set.id);
            CHECK(resolved->name == set.name);
            CHECK(resolved->fullyResolved());
            CHECK_FALSE(resolved->mapping.facets.empty());
        }
        rows.push_back(resultRow(run));
        runs.push_back(run);
    }

    // Eight IDs, nine documents, one refusal, eight meshes -- no silent
    // omission possible.
    CHECK(executed.size() == 8);
    CHECK(meshed == 8);
    CHECK(refused == 1);
    CHECK(rows.size() == 9);

    std::string table = "| Model | Outcome | Nodes | Tet4 | Vanalytic | Vmesh | RelError | "
                        "MinTetVol | Invalid | Warn | Fail | Facets |\n";
    for (const std::string& row : rows) {
        table += row + "\n";
    }
    WARN("P16-REFMOD-001 RESULTS\n" << table);

    // THE QUALITY MATRIX, with the WORST ELEMENT per metric -- which the brief
    // asks for by handle and not only by value. A report that named the worst
    // value without saying which element it belonged to could not be acted on.
    std::string matrix = "| Model | Tet4 | Invalid | Warn | Fail | WorstAspect | WorstRadiusRatio | "
                         "worst element | MinDihedral deg | worst element | MaxDihedral deg | "
                         "MinTetVol mm^3 | element |\n";
    for (const ReferenceMeshRun& run : runs) {
        if (!run.expectedMesh) {
            matrix += std::format("| {} | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | "
                                  "n/a | n/a | n/a |\n",
                                  run.modelId);
            continue;
        }
        matrix += std::format("| {} | {} | {} | {} | {} | {:.6g} | {:.6g} | {} | {:.6g} | {} | "
                              "{:.6g} | {:.6g} | {} |\n",
                              run.modelName, run.elementCount, run.invalidElements,
                              run.warningElements, run.failureElements, run.shape.worstAspectRatio,
                              run.shape.worstRadiusRatio,
                              run.shape.worstRadiusRatioElement.value(), run.shape.minDihedralDeg,
                              run.shape.minDihedralElement.value(), run.shape.maxDihedralDeg,
                              run.minElementVolumeMm3, run.structure.orientation.smallest.value());
    }
    WARN("P16-REFMOD-001 QUALITY MATRIX\n" << matrix);
}

TEST_CASE("MeshCurved_DeclaresTheDeflectionItsAnalyticBoundsAssume",
          "[refmod][mesh][suite][analytic]") {
    // THE BOUNDS BELOW ARE DERIVED FROM THIS NUMBER, so if a model's declared
    // deflection and the suite's constant ever part company, every curved
    // model's tolerance becomes a number with no derivation behind it. This is
    // the test that keeps them together.
    for (const MeshReferenceModelKind kind :
         {MeshReferenceModelKind::Cylinder, MeshReferenceModelKind::PlateWithHole,
          MeshReferenceModelKind::Tube}) {
        auto document = bettercad::reference::buildMeshReferenceModel(kind);
        REQUIRE(document.has_value());
        const std::optional<ObjectId> control = document->findByName("Mesh");
        REQUIRE(control.has_value());
        const auto* definition = document->findObjectAs<meshing::MeshControl>(*control);
        REQUIRE(definition != nullptr);
        CHECK_THAT(definition->definition().mesh.surface.linearDeflection.in(units::mm),
                   WithinRel(kDeflectionMm, 1e-15));
    }
}

// ---------------------------------------------------------------------------
// RM-MESH-01 -- rectangular block

TEST_CASE("MeshBlock_RecoversTheExactVolumeOfAnAsymmetricBlock", "[refmod][mesh][rm01]") {
    auto built = bettercad::reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const double a = mm(built->document, "block_a");
    const double b = mm(built->document, "block_b");
    const double c = mm(built->document, "block_c");
    // ASYMMETRIC, and the suite says so rather than trusting the builder: a
    // cube would hide an axis swap in every other check here.
    CHECK(a != b);
    CHECK(b != c);
    CHECK(a != c);
    const double expected = analytic::blockVolumeMm3(a, b, c);

    MeshedReference model(std::move(built->document));
    const VolumeMesh& mesh = model.require();
    requireStructurallySound(mesh, "RM-MESH-01");
    requireQualityReported(model.quality(), mesh, "RM-MESH-01");

    // PLANAR, so the facets tile the exact solid and the volume is recovered to
    // accumulation -- the one model in the suite where that is the right
    // expectation.
    INFO(std::format("analytic {:.17g}, CAD {:.17g}, mesh {:.17g}, boundary {:.17g} mm^3", expected,
                     mesh.cadVolume().in(units::mm3), mesh.tetrahedralVolume().in(units::mm3),
                     mesh.boundaryVolume().in(units::mm3)));
    CHECK_THAT(mesh.cadVolume().in(units::mm3), WithinRel(expected, kCadVolume));
    CHECK_THAT(mesh.tetrahedralVolume().in(units::mm3), WithinRel(expected, kPlanarVolume));
    CHECK_THAT(mesh.boundaryVolume().in(units::mm3), WithinRel(expected, kPlanarVolume));

    // The mesh occupies the block's own box, and the right way round: a
    // permuted axis would put 120 where 35 belongs.
    const std::optional<meshing::MeshBounds> bounds = mesh.mesh().bounds();
    REQUIRE(bounds.has_value());
    const Vec3 low = positionMm(bounds->min);
    const Vec3 high = positionMm(bounds->max);
    INFO(std::format("bounds {:.9g},{:.9g},{:.9g} .. {:.9g},{:.9g},{:.9g} mm", low.x, low.y, low.z,
                     high.x, high.y, high.z));
    CHECK_THAT(low.x, WithinAbs(0.0, 1e-9));
    CHECK_THAT(low.y, WithinAbs(0.0, 1e-9));
    CHECK_THAT(low.z, WithinAbs(0.0, 1e-9));
    CHECK_THAT(high.x, WithinAbs(a, 1e-9));
    CHECK_THAT(high.y, WithinAbs(b, 1e-9));
    CHECK_THAT(high.z, WithinAbs(c, 1e-9));
}

TEST_CASE("MeshBlock_PartitionsItsBoundaryAcrossAllSixNamedFaces", "[refmod][mesh][rm01][map]") {
    auto built = bettercad::reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    // The face NAMES, taken before the document moves: a `FaceName` is an
    // ObjectId and a selector, so it outlives the model struct that spelled it.
    const FaceName bottom = built->bottom();
    const FaceName top = built->top();
    const std::array<FaceName, 4> sides{built->side(0), built->side(1), built->side(2),
                                        built->side(3)};
    MeshedReference model(std::move(built->document));
    const VolumeMesh& mesh = model.require();
    requireMappingComplete(model.map(), 6, "RM-MESH-01");

    // ALL SIX FACES, BY NAME, and the union exactly the boundary -- which is
    // what a complete partition means and what a face index cannot express.
    const std::array<std::pair<const char*, FaceName>, 6> faces{{{"-z start cap", bottom},
                                                                 {"+z end cap", top},
                                                                 {"y=0 side", sides[0]},
                                                                 {"x=a side", sides[1]},
                                                                 {"y=b side", sides[2]},
                                                                 {"x=0 side", sides[3]}}};

    std::set<meshing::ElementId::ValueType> union_;
    std::size_t counted = 0;
    const auto positions = nodePositions(mesh.mesh());
    for (const auto& [label, reference] : faces) {
        INFO(label);
        const std::vector<meshing::ElementId> facets = facetsOf(model.map(), reference);
        counted += facets.size();
        for (const meshing::ElementId id : facets) {
            CHECK(union_.insert(id.value()).second);
        }
        // AND EVERY FACET ANSWERS IN REVERSE WITH THIS FACE'S NAME -- every
        // one, not the first. A map that attributed one facet of a face
        // correctly and the rest to a neighbour would pass a spot check.
        for (const meshing::ElementId id : facets) {
            auto source = meshing::sourceFaceOf(model.map(), id);
            REQUIRE(source.has_value());
            CHECK(source->facet == id);
            CHECK(std::ranges::find(source->names, reference) != source->names.end());
        }
    }
    CHECK(counted == mesh.boundaryTriangleCount());
    CHECK(union_.size() == mesh.boundaryTriangleCount());

    // Each planar face's facets are all on that face's plane, checked against
    // the block's own dimensions.
    const double a = mm(model.document(), "block_a");
    const double b = mm(model.document(), "block_b");
    const double c = mm(model.document(), "block_c");
    struct Plane {
        FaceName reference;
        int axis;
        double value;
    };
    const std::array<Plane, 6> planes{{{bottom, 2, 0.0},
                                       {top, 2, c},
                                       {sides[0], 1, 0.0},
                                       {sides[1], 0, a},
                                       {sides[2], 1, b},
                                       {sides[3], 0, 0.0}}};
    for (const Plane& plane : planes) {
        for (const meshing::ElementId id : facetsOf(model.map(), plane.reference)) {
            const meshing::Triangle* triangle = mesh.mesh().findTriangle(id);
            REQUIRE(triangle != nullptr);
            for (const meshing::NodeId node : triangle->nodes) {
                const auto found = positions.find(node.value());
                REQUIRE(found != positions.end());
                const std::array<double, 3> p{found->second.x, found->second.y, found->second.z};
                INFO(std::format("axis {} expected {:.9g}, node at {:.9g}", plane.axis, plane.value,
                                 p[static_cast<std::size_t>(plane.axis)]));
                CHECK_THAT(p[static_cast<std::size_t>(plane.axis)], WithinAbs(plane.value, 1e-9));
            }
        }
    }
}

TEST_CASE("MeshBlock_SizingLevelsAreBoundedByItsOwnBoundary", "[refmod][mesh][rm01][sizing]") {
    // COARSE, MEDIUM AND FINE -- AND THE HONEST RESULT, which is that they are
    // the same mesh.
    //
    // OCCT triangulates a PLANAR face with two triangles whatever the
    // deflection, so a block's boundary is irreducibly its own edge lengths and
    // a global target has nowhere to act. P16-SIZE-001 measured and pinned this
    // ("SizeGlobal_CannotBeFinerThanTheBoundaryItMustConformTo") and it is the
    // approved pipeline working as designed: a validated surface goes in and
    // the backend only fills it.
    //
    // So this model has no convergence to declare, and the suite says so with
    // the reason instead of quietly omitting the levels or -- worse -- claiming
    // convergence because an element count moved. The convergence study belongs
    // to RM-MESH-02, where the boundary leaves room for it.
    auto built = bettercad::reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    const double expected = analytic::blockVolumeMm3(mm(model.document(), "block_a"),
                                                     mm(model.document(), "block_b"),
                                                     mm(model.document(), "block_c"));

    struct Level {
        const char* label;
        Length target;
    };
    const std::array<Level, 3> levels{{{"coarse", 30_mm}, {"medium", 20_mm}, {"fine", 10_mm}}};
    std::string table = "| Level | Target mm | Nodes | Tet4 | Median edge mm | Volume error |\n";
    std::vector<std::size_t> tets;
    for (const Level& level : levels) {
        VolumeMeshControls controls;
        controls.sizing.globalTargetSize = level.target;
        const VolumeMesh mesh = model.requireWith(controls);
        const EdgeStats edges = edgeStatsOf(mesh.mesh());
        const double error =
            std::abs(mesh.tetrahedralVolume().in(units::mm3) - expected) / expected;
        table += std::format("| {} | {:.6g} | {} | {} | {:.6g} | {:.3e} |\n", level.label,
                             level.target.in(units::mm), mesh.nodeCount(), mesh.tetrahedronCount(),
                             edges.median, error);
        tets.push_back(mesh.tetrahedronCount());
        // THE VOLUME IS EXACT AT EVERY LEVEL, which is the real statement this
        // model makes about sizing: the discretisation cannot change the answer
        // for a body its facets tile exactly.
        CHECK_THAT(mesh.tetrahedralVolume().in(units::mm3), WithinRel(expected, kPlanarVolume));
        CHECK(mesh.sizing().globalTargetSize == level.target);
        CHECK_FALSE(mesh.sizing().globalIsDefault);
        requireStructurallySound(mesh, "RM-MESH-01 sizing level");
    }
    WARN("RM-MESH-01 sizing levels (a planar boundary bounds them)\n" << table);
    // The default, for the record: BetterCAD's own, and the mesh says so.
    const VolumeMesh byDefault = model.requireWith({});
    CHECK(byDefault.sizing().globalIsDefault);
    CHECK_THAT(byDefault.sizing().globalTargetSize.in(units::mm),
               WithinRel(std::sqrt(mm(model.document(), "block_a") * mm(model.document(), "block_a") +
                                   mm(model.document(), "block_b") * mm(model.document(), "block_b") +
                                   mm(model.document(), "block_c") * mm(model.document(), "block_c")),
                         1e-6));
}

// ---------------------------------------------------------------------------
// RM-MESH-02 -- cylinder

TEST_CASE("MeshCylinder_MeshedVolumeLiesInsideTheInscribedPolygonBound",
          "[refmod][mesh][rm02][analytic]") {
    auto built = bettercad::reference::buildMeshCylinderReferenceModel();
    REQUIRE(built.has_value());
    const FaceName wall = built->wall();
    const FaceName bottomCap = built->bottomCap();
    const FaceName topCap = built->topCap();
    MeshedReference model(std::move(built->document));
    const double r = mm(model.document(), "cyl_r");
    const double h = mm(model.document(), "cyl_h");
    // h != 2r, so a transposed radius and height could not give this volume.
    CHECK(h != 2.0 * r);
    const double expected = analytic::cylinderVolumeMm3(r, h);

    const VolumeMesh& mesh = model.require();
    requireStructurallySound(mesh, "RM-MESH-02");
    requireQualityReported(model.quality(), mesh, "RM-MESH-02");
    CHECK_THAT(mesh.cadVolume().in(units::mm3), WithinRel(expected, kCadVolume));

    // THE BOUND, DERIVED AND TWO-SIDED. The lateral wall is a chord polygon
    // inscribed in the circle, so the mesh is SMALLER than the solid, and by
    // at most what the declared deflection allows.
    const analytic::MeshedVolumeBound bound = analytic::cylinderMeshBound(r, h, kDeflectionMm);
    const double meshed = mesh.tetrahedralVolume().in(units::mm3);
    const double error = std::abs(meshed - expected) / expected;
    INFO(std::format("r {:.6g} h {:.6g} mm, deflection {:.6g} mm: analytic {:.17g}, mesh {:.17g}, "
                     "bound [{:.17g}, {:.17g}] (width {:.3e}), relative error {:.6e}",
                     r, h, kDeflectionMm, expected, meshed, bound.lower, bound.upper,
                     bound.relativeWidth(), error));
    CHECK(bound.contains(meshed));
    // AND THE DIRECTION, which is the part a one-sided tolerance would miss:
    // chords cut inside an arc, so the mesh cannot exceed the solid.
    CHECK(meshed < expected);
    CHECK(mesh.tetrahedralVolume().si() < mesh.cadVolume().si());
    // The boundary polyhedron and the tetrahedra agree to accumulation: the
    // tets tile exactly what the boundary bounds.
    CHECK_THAT(mesh.boundaryVolume().in(units::mm3), WithinRel(meshed, kPlanarVolume));
}

TEST_CASE("MeshCylinder_MapsItsCapsAndWallWithoutLeakage", "[refmod][mesh][rm02][map]") {
    auto built = bettercad::reference::buildMeshCylinderReferenceModel();
    REQUIRE(built.has_value());
    const FaceName wall = built->wall();
    const FaceName bottomCap = built->bottomCap();
    const FaceName topCap = built->topCap();
    MeshedReference model(std::move(built->document));
    const VolumeMesh& mesh = model.require();
    requireMappingComplete(model.map(), 3, "RM-MESH-02");

    const std::vector<meshing::ElementId> lateral = facetsOf(model.map(), wall);
    const std::vector<meshing::ElementId> bottom = facetsOf(model.map(), bottomCap);
    const std::vector<meshing::ElementId> top = facetsOf(model.map(), topCap);
    INFO(std::format("lateral {} facets, bottom cap {}, top cap {}, boundary {}", lateral.size(),
                     bottom.size(), top.size(), mesh.boundaryTriangleCount()));
    // NO LEAKAGE IN EITHER DIRECTION: the lateral set holds no cap facet and
    // neither cap holds a lateral one.
    CHECK(sharedFacets(lateral, bottom) == 0);
    CHECK(sharedFacets(lateral, top) == 0);
    CHECK(sharedFacets(bottom, top) == 0);
    CHECK(lateral.size() + bottom.size() + top.size() == mesh.boundaryTriangleCount());

    // CURVED CONFORMITY, inspected on the actual mesh and never repaired: every
    // node of a lateral facet is on the cylinder, within the deflection, and
    // between the two caps.
    const double r = mm(model.document(), "cyl_r");
    const double h = mm(model.document(), "cyl_h");
    const auto positions = nodePositions(mesh.mesh());
    double worstRadialDeviation = 0.0;
    for (const meshing::ElementId id : lateral) {
        const meshing::Triangle* triangle = mesh.mesh().findTriangle(id);
        REQUIRE(triangle != nullptr);
        for (const meshing::NodeId node : triangle->nodes) {
            const auto found = positions.find(node.value());
            REQUIRE(found != positions.end());
            const double radius = std::hypot(found->second.x, found->second.y);
            worstRadialDeviation = std::max(worstRadialDeviation, std::abs(radius - r));
            CHECK(found->second.z >= -1e-9);
            CHECK(found->second.z <= h + 1e-9);
        }
    }
    INFO(std::format("worst radial deviation of a lateral node: {:.6g} mm, declared deflection {:.6g} mm",
                     worstRadialDeviation, kDeflectionMm));
    // A lateral node lies ON the cylinder -- the kernel's triangulation puts
    // its vertices on the surface -- so the deviation is rounding, not the
    // deflection. Asserting the deflection here would pass a triangulation
    // that had moved every node by a quarter of a millimetre.
    CHECK(worstRadialDeviation < 1e-9);

    // AND THE DECLARED DEFLECTION IS HONOURED, which the node check above does
    // NOT show: the nodes are on the surface whatever the deflection, and what
    // the deflection bounds is how far the CHORD between them departs from it.
    //
    // Measured as the sagitta: for each lateral facet edge that spans the
    // circumference, the midpoint of the chord sits at radius r cos(alpha) and
    // the surface at r, so r - |midpoint| is the deviation the control is about.
    // Nothing is projected or repaired; this is the mesh as generated.
    double worstSagitta = 0.0;
    for (const meshing::ElementId id : lateral) {
        const meshing::Triangle* triangle = mesh.mesh().findTriangle(id);
        REQUIRE(triangle != nullptr);
        for (std::size_t i = 0; i < 3; ++i) {
            const auto a = positions.find(triangle->nodes[i].value());
            const auto b = positions.find(triangle->nodes[(i + 1) % 3].value());
            REQUIRE(a != positions.end());
            REQUIRE(b != positions.end());
            const double midX = 0.5 * (a->second.x + b->second.x);
            const double midY = 0.5 * (a->second.y + b->second.y);
            const double midRadius = std::hypot(midX, midY);
            // A vertical edge has both ends at the same angle, so its midpoint
            // is already at r and contributes nothing -- which is correct: it
            // spans no arc.
            worstSagitta = std::max(worstSagitta, r - midRadius);
        }
    }
    INFO(std::format("worst chord sagitta on the lateral wall: {:.6g} mm against a declared {:.6g} mm",
                     worstSagitta, kDeflectionMm));
    CHECK(worstSagitta > 0.0);
    CHECK(worstSagitta <= kDeflectionMm);
    WARN(std::format("RM-MESH-02 curved conformity: {} lateral facets, worst node radial deviation "
                     "{:.6g} mm, worst chord sagitta {:.6g} mm, declared deflection {:.6g} mm",
                     lateral.size(), worstRadialDeviation, worstSagitta, kDeflectionMm));

    // Cap facets are flat, each on its own plane.
    for (const auto& [facets, z] : std::array<std::pair<const std::vector<meshing::ElementId>*, double>, 2>{
             {{&bottom, 0.0}, {&top, h}}}) {
        for (const meshing::ElementId id : *facets) {
            const meshing::Triangle* triangle = mesh.mesh().findTriangle(id);
            REQUIRE(triangle != nullptr);
            for (const meshing::NodeId node : triangle->nodes) {
                const auto found = positions.find(node.value());
                REQUIRE(found != positions.end());
                CHECK_THAT(found->second.z, WithinAbs(z, 1e-9));
            }
        }
    }
}

TEST_CASE("MeshCylinder_ApproachesTheAnalyticVolumeAsTheSurfaceIsResolved",
          "[refmod][mesh][rm02][convergence]") {
    // THE CONVERGENCE STUDY BELONGS HERE AND NOT ON THE BLOCK, and it varies
    // the SURFACE DEFLECTION rather than the element size -- because for a
    // curved body the volume error is the chord error of the boundary, and the
    // volume target cannot touch it. That is the separation P16-SURF-001 and
    // P16-SIZE-001 keep: the boundary is fixed before the backend sees it.
    //
    // The claim is bounded improvement, not strict monotonicity: the brief says
    // "do not demand strict monotonicity without evidence", and the segment
    // count is a discrete decision of the kernel's.
    auto built = bettercad::reference::buildMeshCylinderReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    const double r = mm(model.document(), "cyl_r");
    const double h = mm(model.document(), "cyl_h");
    const double expected = analytic::cylinderVolumeMm3(r, h);

    struct Level {
        const char* label;
        double deflectionMm;
    };
    //
    // THE LEVELS MUST PRODUCE DIFFERENT BOUNDARIES, and that is asserted rather
    // than assumed. The first version of this table used 0.5, 0.25 and 0.1 mm
    // -- and 0.5 and 0.25 gave the SAME 140 boundary facets, because OCCT's
    // 20-degree angular limit was binding at both. The volumes then differed
    // only in their last two digits, so "the error fell" passed on
    // floating-point noise. The facet count is the guard against that, and it
    // is checked before the errors are compared.
    const std::array<Level, 3> levels{{{"coarse", 0.5}, {"medium", 0.1}, {"fine", 0.02}}};
    std::string table = "| Level | Deflection mm | Facets | Nodes | Tet4 | Vmesh mm^3 | RelError | "
                        "Bound width |\n";
    std::vector<double> errors;
    std::vector<std::size_t> facets;
    for (const Level& level : levels) {
        VolumeMeshControls controls;
        controls.surface.linearDeflection = Length::fromSi(level.deflectionMm * 1e-3);
        // A GLOBAL TARGET COARSER THAN THE BODY, deliberately, and not the
        // model's declared 12 mm. The volume error of a curved body is the
        // chord error of its BOUNDARY, so the volume target contributes nothing
        // to what this study measures -- and at 12 mm the finest level costs
        // 3772 tetrahedra and 245 seconds per run in a debug build, which this
        // suite would then pay in three presets and five repeats for ever.
        // Letting the boundary govern is both the honest configuration for the
        // measurement and the cheap one.
        controls.sizing.globalTargetSize = 80_mm;
        const VolumeMesh mesh = model.requireWith(controls);
        const double meshed = mesh.tetrahedralVolume().in(units::mm3);
        const double error = std::abs(meshed - expected) / expected;
        const analytic::MeshedVolumeBound bound =
            analytic::cylinderMeshBound(r, h, level.deflectionMm);
        table += std::format("| {} | {:.6g} | {} | {} | {} | {:.17g} | {:.3e} | {:.3e} |\n",
                             level.label, level.deflectionMm, mesh.boundaryTriangleCount(),
                             mesh.nodeCount(), mesh.tetrahedronCount(), meshed, error,
                             bound.relativeWidth());
        // EVERY LEVEL inside its OWN bound, and every level still a valid mesh.
        INFO(std::format("{}: mesh {:.17g}, bound [{:.17g}, {:.17g}]", level.label, meshed,
                         bound.lower, bound.upper));
        CHECK(bound.contains(meshed));
        CHECK(meshed < expected);
        requireStructurallySound(mesh, "RM-MESH-02 convergence level");
        errors.push_back(error);
        facets.push_back(mesh.boundaryTriangleCount());
    }
    WARN("RM-MESH-02 surface convergence\n" << table);
    // THE LEVELS ARE DIFFERENT MESHES: strictly more boundary facets each time.
    // Without this the comparison below could be between two identical
    // boundaries, which is how the first version of this table passed.
    CHECK(facets[1] > facets[0]);
    CHECK(facets[2] > facets[1]);
    // AND THE APPROXIMATION IMPROVES as the surface is resolved. Asserted on
    // the measured error against the ANALYTIC volume -- not on element count,
    // which the brief forbids as a convergence criterion. Measured, the error
    // falls from 5.07e-3 to 5.25e-4, a factor of ten, so the margin here is not
    // a last-digit difference.
    CHECK(errors[1] < errors[0] / 1.5);
    CHECK(errors[2] < errors[1] / 1.5);
}

// ---------------------------------------------------------------------------
// RM-MESH-03 -- plate with a through-hole

TEST_CASE("MeshPlateWithHole_LeavesTheHoleEmptyAndSaysSoInTheVolume",
          "[refmod][mesh][rm03][analytic]") {
    auto built = bettercad::reference::buildMeshPlateWithHoleReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    const double length = mm(model.document(), "plate_l");
    const double width = mm(model.document(), "plate_w");
    const double thickness = mm(model.document(), "plate_t");
    const double radius = mm(model.document(), "plate_hole_r");
    const double expected =
        analytic::plateWithHoleVolumeMm3(length, width, thickness, radius);
    const double gross = analytic::blockVolumeMm3(length, width, thickness);

    const VolumeMesh& mesh = model.require();
    requireStructurallySound(mesh, "RM-MESH-03");
    requireQualityReported(model.quality(), mesh, "RM-MESH-03");
    CHECK_THAT(mesh.cadVolume().in(units::mm3), WithinRel(expected, kCadVolume));

    // THE DIRECTION IS THE PREDICTION. The outer boundary is planar and exact;
    // the hole's wall is a chord polygon inscribed in the circle, so it removes
    // LESS material than the true circle and the mesh must come out ABOVE the
    // analytic volume. The opposite direction from the cylinder's, on the same
    // geometry primitive -- which is a check no single-sided tolerance makes.
    const analytic::MeshedVolumeBound bound =
        analytic::plateWithHoleMeshBound(length, width, thickness, radius, kDeflectionMm);
    const double meshed = mesh.tetrahedralVolume().in(units::mm3);
    INFO(std::format("plate {:.6g}x{:.6g}x{:.6g}, hole r {:.6g}: gross {:.17g}, analytic {:.17g}, "
                     "mesh {:.17g}, bound [{:.17g}, {:.17g}], relative error {:.6e}; a FILLED hole "
                     "would read {:.17g} ({:.4g}% high)",
                     length, width, thickness, radius, gross, expected, meshed, bound.lower,
                     bound.upper, std::abs(meshed - expected) / expected, gross,
                     100.0 * (gross - expected) / expected));
    CHECK(bound.contains(meshed));
    CHECK(meshed >= expected);
    // AND A FILLED HOLE IS NOWHERE NEAR THE BOUND, which is what makes the
    // bound decisive rather than permissive. Measured, a filled hole overshoots
    // the permitted upper bound by 29 times the bound's own width -- so the
    // tolerance is not a tolerance that would absorb the defect it exists to
    // catch. The gate is 20x, below the measured 29x and far above 1x.
    const double margin = (gross - bound.upper) / expected / bound.relativeWidth();
    INFO(std::format("a filled hole exceeds the upper bound by {:.4g} times the bound's width",
                     margin));
    CHECK(gross > bound.upper);
    CHECK(margin > 20.0);

    // THE HOLE IS STILL CONNECTED MATERIAL AROUND IT: one solid, one region.
    auto region = meshing::regionOf(model.map(), mesh.mesh());
    REQUIRE(region.has_value());
    CHECK(region->elements.size() == mesh.tetrahedronCount());
    CHECK(region->source == model.body());
}

TEST_CASE("MeshPlateWithHole_HasNoElementInsideTheHole", "[refmod][mesh][rm03][void]") {
    // VOLUME AGREEMENT ALONE IS NOT ENOUGH, which the brief says in as many
    // words: a topology error could fill the hole and compensate elsewhere. So
    // this is an INDEPENDENT occupancy test, with a radius derived from the
    // declared deflection and nothing else.
    auto built = bettercad::reference::buildMeshPlateWithHoleReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    const double radius = mm(model.document(), "plate_hole_r");
    const VolumeMesh& mesh = model.require();

    const VoidRegion hole{.axisX = 50.0, .axisY = 30.0, .radius = radius, .deflection = kDeflectionMm};
    const VoidOccupancy occupancy = checkVoid(mesh.mesh(), hole);
    INFO(std::format("hole axis (50, 30) r {:.6g} mm, safe radius {:.6g} mm: {} nodes inside, "
                     "{} centroids inside, {} centroids in the chord band, closest node {:.9g} mm",
                     radius, hole.safeRadius(), occupancy.nodesInside, occupancy.centroidsInside,
                     occupancy.centroidsInChordBand, occupancy.closestNode));
    CHECK(occupancy.violations() == 0);
    CHECK(occupancy.nodesInside == 0);
    CHECK(occupancy.centroidsInside == 0);
    // The check is NOT VACUOUS: nodes do reach the hole's wall, so the region
    // being tested is one the mesh actually comes up against.
    CHECK(occupancy.closestNode < radius + 1e-9);
    CHECK(occupancy.closestNode >= hole.safeRadius());
}

TEST_CASE("MeshPlateWithHole_MapsTheHoleWallWithItsNormalsFacingTheMaterial",
          "[refmod][mesh][rm03][map]") {
    auto built = bettercad::reference::buildMeshPlateWithHoleReferenceModel();
    REQUIRE(built.has_value());
    const FaceName holeWall = built->holeWall();
    const FaceName bottom = built->bottom();
    const FaceName top = built->top();
    const std::array<FaceName, 4> sides{built->side(0), built->side(1), built->side(2),
                                        built->side(3)};
    MeshedReference model(std::move(built->document));
    const VolumeMesh& mesh = model.require();
    // Seven CAD faces: four outer sides, two caps, and the hole's wall.
    requireMappingComplete(model.map(), 7, "RM-MESH-03");

    const std::vector<meshing::ElementId> wall = facetsOf(model.map(), holeWall);
    CHECK_FALSE(wall.empty());
    // The hole wall is its OWN face: it shares nothing with the outer surfaces.
    for (const FaceName& other : sides) {
        CHECK(sharedFacets(wall, facetsOf(model.map(), other)) == 0);
    }
    CHECK(sharedFacets(wall, facetsOf(model.map(), bottom)) == 0);
    CHECK(sharedFacets(wall, facetsOf(model.map(), top)) == 0);

    // Reverse mapping: every wall facet answers with the hole wall's name.
    for (const meshing::ElementId id : wall) {
        auto source = meshing::sourceFaceOf(model.map(), id);
        REQUIRE(source.has_value());
        CHECK(std::ranges::find(source->names, holeWall) != source->names.end());
    }

    // ORIENTATION, AND THE TRAP THE BRIEF NAMES. "Outward" for a hole's wall
    // means toward the axis, because the material is outside the hole. A check
    // that assumed radially-outward would pass a boss and fail a bore.
    const WallOrientation orientation = wallOrientation(mesh.mesh(), wall, 50.0, 30.0);
    INFO(std::format("hole wall: {} facets, {} facing the axis, {} facing away, {} neutral",
                     orientation.facets, orientation.facingAxis, orientation.facingAway,
                     orientation.radiallyNeutral));
    CHECK(orientation.facets == wall.size());
    CHECK(orientation.facingAxis == wall.size());
    CHECK(orientation.facingAway == 0);
    CHECK(orientation.radiallyNeutral == 0);

    // And an OUTER side face faces the other way, which is what proves the test
    // above is measuring direction rather than agreeing with everything.
    const WallOrientation outer = wallOrientation(mesh.mesh(), facetsOf(model.map(), sides[1]), 50.0, 30.0);
    CHECK(outer.facingAway == outer.facets);
    CHECK(outer.facingAxis == 0);

    // Each wall facet's owning tetrahedron is a real element of the mesh, and
    // exactly one -- an external facet cannot be shared.
    auto owners = meshing::owningTetrahedraOf(model.map(), mesh.mesh(), wall);
    REQUIRE(owners.has_value());
    CHECK_FALSE(owners->empty());
    CHECK(owners->size() <= wall.size());
    for (const meshing::ElementId id : *owners) {
        CHECK(mesh.mesh().findTetrahedron(id) != nullptr);
    }
    // And the nodes they derive are the wall's nodes, all on the wall.
    auto nodes = meshing::boundaryNodesOf(model.map(), mesh.mesh(), wall);
    REQUIRE(nodes.has_value());
    CHECK_FALSE(nodes->empty());
}

// ---------------------------------------------------------------------------
// RM-MESH-04 -- hollow tube

TEST_CASE("MeshTube_PreservesTheInnerVoid", "[refmod][mesh][rm04][void][analytic]") {
    auto built = bettercad::reference::buildMeshTubeReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    const double outer = mm(model.document(), "tube_ro");
    const double inner = mm(model.document(), "tube_ri");
    const double height = mm(model.document(), "tube_h");
    CHECK(outer > inner);
    CHECK(inner > 0.0);
    const double expected = analytic::tubeVolumeMm3(outer, inner, height);
    const double bounding = analytic::cylinderVolumeMm3(outer, height);

    const VolumeMesh& mesh = model.require();
    requireStructurallySound(mesh, "RM-MESH-04");
    requireQualityReported(model.quality(), mesh, "RM-MESH-04");
    CHECK_THAT(mesh.cadVolume().in(units::mm3), WithinRel(expected, kCadVolume));

    // The bound takes the extreme of each wall independently, because the outer
    // chords LOSE material and the inner chords GAIN it.
    const analytic::MeshedVolumeBound bound =
        analytic::tubeMeshBound(outer, inner, height, kDeflectionMm);
    const double meshed = mesh.tetrahedralVolume().in(units::mm3);
    INFO(std::format("Ro {:.6g} Ri {:.6g} h {:.6g}: analytic {:.17g}, mesh {:.17g}, "
                     "bound [{:.17g}, {:.17g}], relative error {:.6e}; a FILLED bore would read "
                     "{:.17g} ({:.4g}% high)",
                     outer, inner, height, expected, meshed, bound.lower, bound.upper,
                     std::abs(meshed - expected) / expected, bounding,
                     100.0 * (bounding - expected) / expected));
    CHECK(bound.contains(meshed));
    CHECK(bounding > bound.upper);

    // THE VOID, independently: nothing inside the bore.
    const VoidRegion bore{.axisX = 0.0, .axisY = 0.0, .radius = inner, .deflection = kDeflectionMm};
    const VoidOccupancy occupancy = checkVoid(mesh.mesh(), bore);
    INFO(std::format("bore r {:.6g} mm, safe radius {:.6g} mm: {} nodes inside, {} centroids "
                     "inside, {} in the chord band, closest node {:.9g} mm",
                     inner, bore.safeRadius(), occupancy.nodesInside, occupancy.centroidsInside,
                     occupancy.centroidsInChordBand, occupancy.closestNode));
    CHECK(occupancy.violations() == 0);
    CHECK(occupancy.closestNode >= bore.safeRadius());
    CHECK(occupancy.closestNode < inner + 1e-9);
}

TEST_CASE("MeshTube_MapsItsInnerAndOuterWallsToDifferentFaces", "[refmod][mesh][rm04][map]") {
    auto built = bettercad::reference::buildMeshTubeReferenceModel();
    REQUIRE(built.has_value());
    const FaceName outerWall = built->outerWall();
    const FaceName innerWall = built->innerWall();
    const FaceName bottomAnnulus = built->bottomAnnulus();
    const FaceName topAnnulus = built->topAnnulus();
    // The two walls are swept by DIFFERENT circles, so their names differ --
    // which is the property that makes them separable at all.
    CHECK_FALSE(outerWall == innerWall);
    MeshedReference model(std::move(built->document));
    const VolumeMesh& mesh = model.require();
    requireMappingComplete(model.map(), 4, "RM-MESH-04");

    const std::vector<meshing::ElementId> out = facetsOf(model.map(), outerWall);
    const std::vector<meshing::ElementId> in = facetsOf(model.map(), innerWall);
    const std::vector<meshing::ElementId> bottom = facetsOf(model.map(), bottomAnnulus);
    const std::vector<meshing::ElementId> top = facetsOf(model.map(), topAnnulus);
    INFO(std::format("outer {} facets, inner {}, bottom annulus {}, top annulus {}, boundary {}",
                     out.size(), in.size(), bottom.size(), top.size(), mesh.boundaryTriangleCount()));
    // FOUR DISJOINT REGIONS partitioning the boundary. No inner/outer leakage,
    // which would put a pressure load on the wrong surface.
    CHECK(sharedFacets(out, in) == 0);
    CHECK(sharedFacets(out, bottom) == 0);
    CHECK(sharedFacets(out, top) == 0);
    CHECK(sharedFacets(in, bottom) == 0);
    CHECK(sharedFacets(in, top) == 0);
    CHECK(sharedFacets(bottom, top) == 0);
    CHECK(out.size() + in.size() + bottom.size() + top.size() == mesh.boundaryTriangleCount());

    // AND THEY ARE AT DIFFERENT RADII, which is the geometric proof that the
    // names did not simply swap: a mapping that confused them would pass every
    // disjointness check above.
    const double outerRadius = mm(model.document(), "tube_ro");
    const double innerRadius = mm(model.document(), "tube_ri");
    const auto positions = nodePositions(mesh.mesh());
    auto radiiOf = [&](const std::vector<meshing::ElementId>& facets) {
        std::vector<double> radii;
        for (const meshing::ElementId id : facets) {
            const meshing::Triangle* triangle = mesh.mesh().findTriangle(id);
            REQUIRE(triangle != nullptr);
            for (const meshing::NodeId node : triangle->nodes) {
                const auto found = positions.find(node.value());
                REQUIRE(found != positions.end());
                radii.push_back(std::hypot(found->second.x, found->second.y));
            }
        }
        return statsOf(std::move(radii));
    };
    const EdgeStats outerRadii = radiiOf(out);
    const EdgeStats innerRadii = radiiOf(in);
    INFO(std::format("outer wall nodes at {:.9g}..{:.9g} mm, inner at {:.9g}..{:.9g} mm",
                     outerRadii.minimum, outerRadii.maximum, innerRadii.minimum, innerRadii.maximum));
    CHECK_THAT(outerRadii.minimum, WithinAbs(outerRadius, 1e-9));
    CHECK_THAT(outerRadii.maximum, WithinAbs(outerRadius, 1e-9));
    CHECK_THAT(innerRadii.minimum, WithinAbs(innerRadius, 1e-9));
    CHECK_THAT(innerRadii.maximum, WithinAbs(innerRadius, 1e-9));

    // ORIENTATION: the outer wall faces away from the axis, the inner wall
    // faces it. Both are "outward from the material"; only one is outward
    // radially.
    const WallOrientation outside = wallOrientation(mesh.mesh(), out, 0.0, 0.0);
    const WallOrientation inside = wallOrientation(mesh.mesh(), in, 0.0, 0.0);
    CHECK(outside.facingAway == outside.facets);
    CHECK(outside.facingAxis == 0);
    CHECK(inside.facingAxis == inside.facets);
    CHECK(inside.facingAway == 0);
}

// ---------------------------------------------------------------------------
// RM-MESH-05 -- thin feature

TEST_CASE("MeshThinPlate_MeshesWithMeasurablyWorseShapeThanTheBlock",
          "[refmod][mesh][rm05][quality]") {
    // TWO OUTCOMES ARE ACCEPTABLE, and which one occurs is a measurement rather
    // than a choice: either a structurally valid mesh whose shape metrics are
    // materially worse than a well-proportioned body's, or an explicit refusal
    // with a structured diagnostic. What is NOT acceptable is a quiet success
    // carrying inverted or degenerate cells -- and that is what the structural
    // audit below forbids.
    auto thinBuilt = bettercad::reference::buildMeshThinPlateReferenceModel();
    REQUIRE(thinBuilt.has_value());
    MeshedReference thin(std::move(thinBuilt->document));
    const double length = mm(thin.document(), "thin_l");
    const double width = mm(thin.document(), "thin_w");
    const double thickness = mm(thin.document(), "thin_t");
    // The model really is thin: the suite states the ratio rather than
    // trusting the name.
    INFO(std::format("{:.6g} x {:.6g} x {:.6g} mm, ratio {:.4g}:1", length, width, thickness,
                     std::min(length, width) / thickness));
    CHECK(std::min(length, width) / thickness > 50.0);

    auto generated = thin.generate();
    if (!generated) {
        // OUTCOME B. Recorded as a first-class result, with the structured
        // diagnostic the brief requires -- not a backend message and not a
        // process that otherwise succeeded.
        WARN("RM-MESH-05 OUTCOME B: refused -- " << generated.error().message);
        CHECK_FALSE(generated.error().message.empty());
        CHECK(thin.mesher().heldMeshCount() == 0);
        CHECK(thin.currency() == MeshCurrency::GenerationFailed);
        return;
    }

    // OUTCOME A. Structurally valid, and the policy outcome explicitly
    // reported.
    const VolumeMesh& mesh = **generated;
    requireStructurallySound(mesh, "RM-MESH-05");
    requireQualityReported(thin.quality(), mesh, "RM-MESH-05");
    const double expected = analytic::blockVolumeMm3(length, width, thickness);
    CHECK_THAT(mesh.cadVolume().in(units::mm3), WithinRel(expected, kCadVolume));
    // Planar, so even a hard case recovers the volume exactly: this model's
    // difficulty is the element SHAPE, and the suite keeps the two apart.
    CHECK_THAT(mesh.tetrahedralVolume().in(units::mm3), WithinRel(expected, kPlanarVolume));

    // AND IT IS MEASURABLY HARDER THAN RM-MESH-01, which is what makes the
    // fixture a stress and not just another box.
    auto blockBuilt = bettercad::reference::buildMeshBlockReferenceModel();
    REQUIRE(blockBuilt.has_value());
    MeshedReference block(std::move(blockBuilt->document));
    block.require();

    const ShapeExtremes thinShape = shapeExtremesOf(thin.quality());
    const ShapeExtremes blockShape = shapeExtremesOf(block.quality());
    WARN(std::format("RM-MESH-05 OUTCOME A -- shape against RM-MESH-01\n"
                     "| Metric | RM-MESH-01 | RM-MESH-05 |\n"
                     "| worst aspect ratio l_max/l_min | {:.6g} | {:.6g} |\n"
                     "| worst radius ratio 3r/R | {:.6g} | {:.6g} |\n"
                     "| min dihedral deg | {:.6g} | {:.6g} |\n"
                     "| max dihedral deg | {:.6g} | {:.6g} |\n"
                     "| min element volume mm^3 | {:.6g} | {:.6g} |\n"
                     "| invalid / warning / failure | {}/{}/{} | {}/{}/{} |",
                     blockShape.worstAspectRatio, thinShape.worstAspectRatio,
                     blockShape.worstRadiusRatio, thinShape.worstRadiusRatio,
                     blockShape.minDihedralDeg, thinShape.minDihedralDeg,
                     blockShape.maxDihedralDeg, thinShape.maxDihedralDeg,
                     blockShape.minVolumeMm3, thinShape.minVolumeMm3,
                     block.quality().invalidElements, block.quality().warningElements,
                     block.quality().failureElements, thin.quality().invalidElements,
                     thin.quality().warningElements, thin.quality().failureElements));

    // THE COMPARISON IS MADE ON THE METRICS THAT MEASURE THE ELEMENT.
    //
    // `TetAspectRatio` is l_max / l_min, and on a box tetrahedralisation that
    // is a property of the BOX rather than of the elements: it reads sqrt(3) on
    // a cube however slivery the tetrahedra are, and 80 on this plate because
    // the plate is 80:1 -- it would read 80 for a beautifully proportioned mesh
    // of the same plate too. The radius ratio 3r/R and the dihedral angles
    // measure the element itself, and they are far sharper here: 1355 times
    // worse against 20 times. So the gate is on those two, and the aspect ratio
    // is reported for completeness.
    CHECK(thinShape.worstRadiusRatio < blockShape.worstRadiusRatio / 100.0);
    CHECK(thinShape.minDihedralDeg < blockShape.minDihedralDeg / 5.0);
    CHECK(thinShape.maxDihedralDeg > blockShape.maxDihedralDeg);
    // INVALID IS STILL ZERO. A hard case may be poor; it may not be invalid.
    CHECK(thin.quality().invalidElements == 0);

    // SIZING SENSITIVITY, recorded whatever it shows. The brief forbids
    // changing settings until a PASS appears and then hiding the bad result, so
    // both cases are planned here and both are printed.
    std::string table = "| Case | Target mm | Nodes | Tet4 | Worst radius ratio | Min dihedral deg |\n";
    for (const double target : {20.0, 5.0, 1.0}) {
        VolumeMeshControls controls;
        controls.sizing.globalTargetSize = Length::fromSi(target * 1e-3);
        auto attempt = thin.meshWith(controls);
        if (!attempt) {
            table += std::format("| {:.6g} mm target | {:.6g} | refused | refused | - | - |\n", target,
                                 target);
            continue;
        }
        const meshing::MeshQualityReport report =
            meshing::evaluateMeshQuality(attempt->mesh(), meshing::reportOnlyThresholds());
        const ShapeExtremes shape = shapeExtremesOf(report);
        table += std::format("| {:.6g} mm target | {:.6g} | {} | {} | {:.6g} | {:.6g} |\n", target,
                             target, attempt->nodeCount(), attempt->tetrahedronCount(),
                             shape.worstRadiusRatio, shape.minDihedralDeg);
        // Whatever the sizing, a published mesh is structurally sound.
        requireStructurallySound(*attempt, "RM-MESH-05 sizing sensitivity");
        CHECK(report.invalidElements == 0);
    }
    WARN("RM-MESH-05 sizing sensitivity\n" << table);
}

// ---------------------------------------------------------------------------
// RM-MESH-06 -- transformed asymmetric solid

TEST_CASE("MeshTransformedBlock_KeepsItsVolumeAndShapeUnderARigidTransform",
          "[refmod][mesh][rm06][transform]") {
    const bettercad::reference::RigidPlacement placement =
        bettercad::reference::meshTransformedPlacement();
    // THE TRANSFORM IS CHECKED BEFORE IT IS USED. An oracle built on a triad
    // that is not orthonormal would be measuring the wrong thing, so the suite
    // verifies the three norms, the three dot products and the handedness here
    // -- by hand, from the numbers, not through Frame3D.
    const std::array<std::array<double, 3>, 3> axes{placement.xAxis, placement.yAxis,
                                                    placement.normal};
    for (const std::array<double, 3>& axis : axes) {
        const Vec3 v{axis[0], axis[1], axis[2]};
        CHECK_THAT(norm(v), WithinAbs(1.0, 1e-12));
    }
    const Vec3 x{placement.xAxis[0], placement.xAxis[1], placement.xAxis[2]};
    const Vec3 y{placement.yAxis[0], placement.yAxis[1], placement.yAxis[2]};
    const Vec3 n{placement.normal[0], placement.normal[1], placement.normal[2]};
    CHECK_THAT(dot(x, y), WithinAbs(0.0, 1e-12));
    CHECK_THAT(dot(x, n), WithinAbs(0.0, 1e-12));
    CHECK_THAT(dot(y, n), WithinAbs(0.0, 1e-12));
    const Vec3 handed = cross(x, y);
    CHECK_THAT(norm(minus(handed, n)), WithinAbs(0.0, 1e-12));
    // NOT A ROTATION ABOUT A COORDINATE AXIS: every axis moves, so an axis
    // permutation cannot survive it.
    CHECK(std::abs(x.x) < 1.0 - 1e-6);
    CHECK(std::abs(y.y) < 1.0 - 1e-6);
    CHECK(std::abs(n.z) < 1.0 - 1e-6);

    auto baseBuilt = bettercad::reference::buildMeshTransformedBaseReferenceModel();
    auto placedBuilt = bettercad::reference::buildMeshTransformedPlacedReferenceModel();
    REQUIRE(baseBuilt.has_value());
    REQUIRE(placedBuilt.has_value());
    const FaceName baseDatum = baseBuilt->datumFace();
    const FaceName placedDatum = placedBuilt->datumFace();
    MeshedReference base(std::move(baseBuilt->document));
    MeshedReference moved(std::move(placedBuilt->document));

    const double a = mm(base.document(), "base_a");
    const double b = mm(base.document(), "base_b");
    const double c = mm(base.document(), "base_c");
    // The same dimensions on both, so the only difference is the placement.
    CHECK_THAT(mm(moved.document(), "placed_a"), WithinRel(a, 1e-15));
    CHECK_THAT(mm(moved.document(), "placed_b"), WithinRel(b, 1e-15));
    CHECK_THAT(mm(moved.document(), "placed_c"), WithinRel(c, 1e-15));
    const double expected = analytic::blockVolumeMm3(a, b, c);

    const VolumeMesh& baseMesh = base.require();
    const VolumeMesh& movedMesh = moved.require();
    requireStructurallySound(baseMesh, "RM-MESH-06 base");
    requireStructurallySound(movedMesh, "RM-MESH-06 placed");
    requireQualityReported(base.quality(), baseMesh, "RM-MESH-06 base");
    requireQualityReported(moved.quality(), movedMesh, "RM-MESH-06 placed");

    // THE CAD VOLUME IS INVARIANT, to double precision and NOT to the meshing
    // bound: the brief forbids excusing rigid-transform drift in the CAD oracle
    // with mesh approximation error.
    INFO(std::format("CAD volume base {:.17g}, placed {:.17g}, analytic {:.17g} mm^3",
                     baseMesh.cadVolume().in(units::mm3), movedMesh.cadVolume().in(units::mm3),
                     expected));
    CHECK_THAT(baseMesh.cadVolume().in(units::mm3), WithinRel(expected, kCadVolume));
    CHECK_THAT(movedMesh.cadVolume().in(units::mm3), WithinRel(expected, kCadVolume));
    CHECK_THAT(movedMesh.cadVolume().in(units::mm3),
               WithinRel(baseMesh.cadVolume().in(units::mm3), kRigidInvariant));
    // And so is the meshed volume -- both planar, both exact.
    CHECK_THAT(baseMesh.tetrahedralVolume().in(units::mm3), WithinRel(expected, kPlanarVolume));
    CHECK_THAT(movedMesh.tetrahedralVolume().in(units::mm3), WithinRel(expected, kPlanarVolume));

    // THE MESH ACTUALLY MOVED. A transformed model whose mesh stayed at the
    // original coordinates is an automatic failure, and the bounding box is
    // where it would show: the placed block's box is not the base's.
    const std::optional<meshing::MeshBounds> baseBox = baseMesh.mesh().bounds();
    const std::optional<meshing::MeshBounds> movedBox = movedMesh.mesh().bounds();
    REQUIRE(baseBox.has_value());
    REQUIRE(movedBox.has_value());
    const Vec3 baseLow = positionMm(baseBox->min);
    const Vec3 movedLow = positionMm(movedBox->min);
    INFO(std::format("base box from {:.6g},{:.6g},{:.6g}; placed box from {:.6g},{:.6g},{:.6g}",
                     baseLow.x, baseLow.y, baseLow.z, movedLow.x, movedLow.y, movedLow.z));
    CHECK(norm(minus(movedLow, baseLow)) > 1.0);

    // NODE POSITIONS FOLLOW R x + t. Handles are mesh-local, so the comparison
    // is between canonicalised point SETS -- never an invented handle
    // correspondence.
    //
    // AND THE TWO POPULATIONS ARE JUDGED SEPARATELY, which is the finding this
    // model produced. A BOUNDARY node is a vertex of the kernel's triangulation
    // of a CAD face, so it must move with the body to rounding. The one
    // INTERIOR node is Netgen's own choice, computed from world coordinates by
    // an algorithm under no equivariance obligation -- and measured, it lands
    // 2.7e-4 mm from where the base mesh's interior node transforms to. One
    // tolerance over both would mean either failing a correct mesh or giving up
    // the check that proves the body moved at all.
    auto transformedNodes = [&](NodeSelection selection) {
        std::vector<Vec3> points;
        for (const Vec3& point : canonicalNodes(baseMesh.mesh(), selection)) {
            points.push_back(placed(point, placement));
        }
        return sortedByPosition(std::move(points));
    };
    const double boundaryGap = largestNodeGap(transformedNodes(NodeSelection::Boundary),
                                              canonicalNodes(movedMesh.mesh(), NodeSelection::Boundary));
    const double interiorGap = largestNodeGap(transformedNodes(NodeSelection::Interior),
                                              canonicalNodes(movedMesh.mesh(), NodeSelection::Interior));
    const double wholeGap =
        largestNodeGap(transformedNodes(NodeSelection::All), canonicalNodes(movedMesh.mesh()));
    const double diagonal = std::sqrt(a * a + b * b + c * c);
    INFO(std::format("base {} nodes ({} on the boundary) / {} tets, placed {} nodes ({} on the "
                     "boundary) / {} tets; largest gap after R x + t: boundary {:.6g} mm, interior "
                     "{:.6g} mm, overall {:.6g} mm, body diagonal {:.6g} mm",
                     baseMesh.nodeCount(), canonicalNodes(baseMesh.mesh(), NodeSelection::Boundary).size(),
                     baseMesh.tetrahedronCount(), movedMesh.nodeCount(),
                     canonicalNodes(movedMesh.mesh(), NodeSelection::Boundary).size(),
                     movedMesh.tetrahedronCount(), boundaryGap, interiorGap, wholeGap, diagonal));
    // MEASURED, THEN ASSERTED. At these dimensions the backend produces the
    // same counts in both placements. That is NOT a general guarantee -- the
    // same model at a = 110 mm gives 6 tetrahedra in one placement and 12 in
    // the other, which the runner's model-change step records -- so the
    // equality is a property of this configuration and the suite says so.
    CHECK(movedMesh.nodeCount() == baseMesh.nodeCount());
    CHECK(movedMesh.tetrahedronCount() == baseMesh.tetrahedronCount());
    // THE CAD-DETERMINED NODES FOLLOW THE TRANSFORM EXACTLY. This is the sharp
    // check, and the one a world/local-frame defect would fail outright.
    CHECK(boundaryGap < 1e-9);
    // The backend's interior node is placed to within a part in 1e5 of the
    // body, so the two meshes are the same discretisation rather than two
    // different ones. Measured 2.7e-4 mm on a 108 mm diagonal, which is 2.5e-6
    // relative; the gate is 1e-5, four times the measurement and five orders
    // below the element size.
    CHECK(interiorGap < 1e-5 * diagonal);

    // DIMENSIONLESS QUALITY IS INVARIANT. This is the strong check for a
    // world-frame bug: a metric computed in the wrong frame would move.
    //
    // THE TOLERANCE IS DERIVED FROM THE NODE DISCREPANCY ABOVE, not chosen. A
    // shape metric is Lipschitz in its nodes' positions, so two meshes
    // differing by 2.5e-6 relative in one node can differ by that order in a
    // metric -- measured, the radius ratio differs by 6.0e-6 relative and the
    // dihedral angles by 9e-5 degrees, while the aspect ratio agrees to 1e-15
    // because its worst element does not touch the interior node. The gate is
    // 1e-4, above every measurement and four orders below what a frame defect
    // would do: that moves a dihedral by DEGREES.
    const ShapeExtremes baseShape = shapeExtremesOf(base.quality());
    const ShapeExtremes movedShape = shapeExtremesOf(moved.quality());
    INFO(std::format("aspect {:.17g} vs {:.17g}; radius ratio {:.17g} vs {:.17g}; dihedral "
                     "{:.17g}..{:.17g} vs {:.17g}..{:.17g}; min volume {:.17g} vs {:.17g}",
                     baseShape.worstAspectRatio, movedShape.worstAspectRatio,
                     baseShape.worstRadiusRatio, movedShape.worstRadiusRatio,
                     baseShape.minDihedralDeg, baseShape.maxDihedralDeg, movedShape.minDihedralDeg,
                     movedShape.maxDihedralDeg, baseShape.minVolumeMm3, movedShape.minVolumeMm3));
    CHECK_THAT(movedShape.worstAspectRatio, WithinRel(baseShape.worstAspectRatio, 1e-4));
    CHECK_THAT(movedShape.worstRadiusRatio, WithinRel(baseShape.worstRadiusRatio, 1e-4));
    CHECK_THAT(movedShape.minDihedralDeg, WithinRel(baseShape.minDihedralDeg, 1e-4));
    CHECK_THAT(movedShape.maxDihedralDeg, WithinRel(baseShape.maxDihedralDeg, 1e-4));
    CHECK_THAT(movedShape.minVolumeMm3, WithinRel(baseShape.minVolumeMm3, 1e-4));

    // MAPPING SEMANTICS ARE PRESERVED. The same canonical intent -- the start
    // cap of the extrude -- resolves in both, with no world-axis assumption
    // anywhere: the placed model's datum face is not axis-aligned at all.
    requireMappingComplete(base.map(), 6, "RM-MESH-06 base");
    requireMappingComplete(moved.map(), 6, "RM-MESH-06 placed");
    const std::vector<meshing::ElementId> baseFacets = facetsOf(base.map(), baseDatum);
    const std::vector<meshing::ElementId> movedFacets = facetsOf(moved.map(), placedDatum);
    CHECK(baseFacets.size() == movedFacets.size());

    // AND THE MAPPED FACETS' NORMALS TRANSFORM, which is the mapping half of
    // the frame check and the part a count comparison cannot make. The datum
    // face is the extrude's START CAP, so in the base model it faces -Z; under
    // the placement it must face -N, and nothing in the suite assumes a world
    // axis for it.
    auto unitNormalOf = [](const meshing::Mesh& mesh, meshing::ElementId facet) {
        const meshing::Triangle* triangle = mesh.findTriangle(facet);
        REQUIRE(triangle != nullptr);
        const auto positions = nodePositions(mesh);
        std::array<Vec3, 3> p{};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto found = positions.find(triangle->nodes[i].value());
            REQUIRE(found != positions.end());
            p[i] = found->second;
        }
        const Vec3 normal = cross(minus(p[1], p[0]), minus(p[2], p[0]));
        const double length = norm(normal);
        REQUIRE(length > 0.0);
        return Vec3{normal.x / length, normal.y / length, normal.z / length};
    };
    // R applied to a DIRECTION: the same triad, with no translation.
    auto turned = [&](const Vec3& v) {
        return Vec3{v.x * placement.xAxis[0] + v.y * placement.yAxis[0] + v.z * placement.normal[0],
                    v.x * placement.xAxis[1] + v.y * placement.yAxis[1] + v.z * placement.normal[1],
                    v.x * placement.xAxis[2] + v.y * placement.yAxis[2] + v.z * placement.normal[2]};
    };
    const Vec3 baseNormal = unitNormalOf(baseMesh.mesh(), baseFacets.front());
    const Vec3 expectedNormal = turned(baseNormal);
    INFO(std::format("datum face normal: base ({:.9g}, {:.9g}, {:.9g}), R x base ({:.9g}, {:.9g}, "
                     "{:.9g})",
                     baseNormal.x, baseNormal.y, baseNormal.z, expectedNormal.x, expectedNormal.y,
                     expectedNormal.z));
    // The base's start cap faces -Z, as its construction requires.
    CHECK_THAT(baseNormal.z, WithinAbs(-1.0, 1e-9));
    // The placed one faces -N, and NOT any coordinate axis -- so a mapping that
    // had kept a world-axis assumption would show here.
    CHECK(std::abs(expectedNormal.x) < 1.0 - 1e-6);
    CHECK(std::abs(expectedNormal.z) < 1.0 - 1e-6);
    for (const meshing::ElementId facet : movedFacets) {
        const Vec3 actual = unitNormalOf(movedMesh.mesh(), facet);
        INFO(std::format("facet normal ({:.9g}, {:.9g}, {:.9g})", actual.x, actual.y, actual.z));
        CHECK(norm(minus(actual, expectedNormal)) < 1e-9);
    }
}

// ---------------------------------------------------------------------------
// RM-MESH-07 -- local refinement

TEST_CASE("MeshLocalRefinement_RefinesTheNamedFaceAndLeavesTheOppositeOneAlone",
          "[refmod][mesh][rm07][sizing]") {
    auto built = bettercad::reference::buildMeshLocalRefinementReferenceModel();
    REQUIRE(built.has_value());
    const FaceName refined = built->refined();
    const FaceName coarse = built->coarse();
    CHECK_FALSE(refined == coarse);
    MeshedReference model(std::move(built->document));

    const meshing::MeshControlDefinition& intent = model.definition()->definition();
    REQUIRE(intent.mesh.sizing.globalTargetSize.has_value());
    REQUIRE(intent.mesh.sizing.local.size() == 1);
    const double globalMm = intent.mesh.sizing.globalTargetSize->in(units::mm);
    const double localMm = intent.mesh.sizing.local.front().targetSize.in(units::mm);
    CHECK(intent.mesh.sizing.local.front().face == refined);
    CHECK(localMm < globalMm);

    // WITH the control, from the document's own intent.
    const VolumeMesh& withLocal = model.require();
    requireStructurallySound(withLocal, "RM-MESH-07 refined");
    requireQualityReported(model.quality(), withLocal, "RM-MESH-07 refined");
    requireMappingComplete(model.map(), 6, "RM-MESH-07 refined");
    // The control RESOLVED, and produced a slab: a point restriction alone
    // refines nothing, which is P16-SIZE-001's central finding.
    REQUIRE(withLocal.sizing().local.size() == 1);
    CHECK(withLocal.sizing().local.front().state == meshing::SizingSelectionState::Resolved);
    CHECK(withLocal.sizing().local.front().nodeCount > 0);
    CHECK(withLocal.sizing().unresolvedCount() == 0);
    CHECK(withLocal.sizing().regions.size() == 1);

    // WITHOUT it: the same geometry, the same global target, no local control.
    VolumeMeshControls globalOnly;
    globalOnly.sizing.globalTargetSize = *intent.mesh.sizing.globalTargetSize;
    const VolumeMesh plain = model.requireWith(globalOnly);
    requireStructurallySound(plain, "RM-MESH-07 global only");
    CHECK(plain.sizing().local.empty());
    CHECK(plain.sizing().regions.empty());

    // FIRST, THE P16-SIZE + P16-MAP INTEGRATION, which is what the FaceName
    // actually buys: the slab the control resolved to must be AT F. A
    // resolution that landed on the opposite face would refine the wrong half
    // of the body while satisfying every count in the suite.
    const double b = mm(model.document(), "local_b");
    const meshing::BoxSizeRestriction& slab = withLocal.sizing().regions.front();
    const Vec3 slabLow = positionMm(slab.min);
    const Vec3 slabHigh = positionMm(slab.max);
    INFO(std::format("slab {:.6g},{:.6g},{:.6g} .. {:.6g},{:.6g},{:.6g} mm at {:.6g} mm; F is y=0, "
                     "G is y={:.6g}",
                     slabLow.x, slabLow.y, slabLow.z, slabHigh.x, slabHigh.y, slabHigh.z,
                     slab.maxSize.in(units::mm), b));
    CHECK_THAT(slab.maxSize.in(units::mm), WithinRel(localMm, 1e-12));
    // It starts at F's plane and reaches INWARD, by about one target size -- not
    // outward, and nowhere near G. The micron of tolerance is the resolver's
    // own comparison padding (measured, 1e-6 mm); what this assertion is for is
    // that the slab is at y = 0 and not at y = 60.
    CHECK_THAT(slabLow.y, WithinAbs(0.0, 1e-3));
    CHECK(slabHigh.y > 0.0);
    CHECK(slabHigh.y < b / 2.0);

    // THE REFINEMENT IS MEASURED IN THE VOLUME NEAR F, not at F's facets.
    //
    // A planar face has exactly two boundary triangles whatever the deflection,
    // so the tetrahedra owning them span the face however fine the interior is:
    // measured here, the owners of F's two facets keep a 57.8 mm median edge in
    // a mesh whose element count the control multiplied by twenty-two. Local
    // sizing refines the volume near a face, which is what a slab means, so the
    // band is the instrument. The reach is twice the local target, so it passes
    // the slab and is not reading Netgen's transition elements alone.
    const double reach = 2.0 * localMm;
    auto bandOf = [&](const VolumeMesh& mesh, double plane) {
        const std::vector<meshing::ElementId> tets = tetsNearPlane(mesh.mesh(), 1, plane, reach);
        return std::tuple{edgeStatsOf(mesh.mesh(), tets), tets.size(),
                          nodesNearPlane(mesh.mesh(), 1, plane, reach)};
    };
    const auto [refinedAtF, refinedTetsF, refinedNodesF] = bandOf(withLocal, 0.0);
    const auto [refinedAtG, refinedTetsG, refinedNodesG] = bandOf(withLocal, b);
    const auto [plainAtF, plainTetsF, plainNodesF] = bandOf(plain, 0.0);
    const auto [plainAtG, plainTetsG, plainNodesG] = bandOf(plain, b);

    // AND THE DECISIVE TEST IS THE MIRROR. A third mesh refines G instead of F,
    // with everything else identical. Comparing the SAME band between the two
    // isolates the control from any global effect: a control that refined the
    // whole body, or that resolved to the wrong face, cannot pass both halves.
    VolumeMeshControls mirrored = globalOnly;
    mirrored.sizing.local.push_back(
        meshing::LocalMeshSizing{.face = coarse, .targetSize = intent.mesh.sizing.local.front().targetSize});
    const VolumeMesh refinedAtTheOtherFace = model.requireWith(mirrored);
    requireStructurallySound(refinedAtTheOtherFace, "RM-MESH-07 mirrored");
    REQUIRE(refinedAtTheOtherFace.sizing().local.size() == 1);
    CHECK(refinedAtTheOtherFace.sizing().local.front().state ==
          meshing::SizingSelectionState::Resolved);
    REQUIRE(refinedAtTheOtherFace.sizing().regions.size() == 1);
    const Vec3 mirroredLow = positionMm(refinedAtTheOtherFace.sizing().regions.front().min);
    const Vec3 mirroredHigh = positionMm(refinedAtTheOtherFace.sizing().regions.front().max);
    // The mirrored slab is at G, which is the other half of the wrong-region
    // protection: the two controls resolve to two different places.
    CHECK_THAT(mirroredHigh.y, WithinAbs(b, 1e-3));
    CHECK(mirroredLow.y > b / 2.0);
    const auto [mirroredAtF, mirroredTetsF, mirroredNodesF] = bandOf(refinedAtTheOtherFace, 0.0);
    const auto [mirroredAtG, mirroredTetsG, mirroredNodesG] = bandOf(refinedAtTheOtherFace, b);

    WARN(std::format(
        "RM-MESH-07 local refinement: global {:.6g} mm, local {:.6g} mm, band reach {:.6g} mm\n"
        "| Mesh | Nodes | Tet4 | F tets | F nodes | F median | F min | G tets | G nodes |"
        " G median | G min |\n"
        "| global only | {} | {} | {} | {} | {:.6g} | {:.6g} | {} | {} | {:.6g} | {:.6g} |\n"
        "| + local at F | {} | {} | {} | {} | {:.6g} | {:.6g} | {} | {} | {:.6g} | {:.6g} |\n"
        "| + local at G | {} | {} | {} | {} | {:.6g} | {:.6g} | {} | {} | {:.6g} | {:.6g} |\n"
        "F's two boundary facets keep a {:.6g} mm median edge even in the refined mesh, which is "
        "why the band and not the facets is the instrument.",
        globalMm, localMm, reach, plain.nodeCount(), plain.tetrahedronCount(), plainTetsF,
        plainNodesF, plainAtF.median, plainAtF.minimum, plainTetsG, plainNodesG, plainAtG.median,
        plainAtG.minimum, withLocal.nodeCount(), withLocal.tetrahedronCount(), refinedTetsF,
        refinedNodesF, refinedAtF.median, refinedAtF.minimum, refinedTetsG, refinedNodesG,
        refinedAtG.median, refinedAtG.minimum, refinedAtTheOtherFace.nodeCount(),
        refinedAtTheOtherFace.tetrahedronCount(), mirroredTetsF, mirroredNodesF, mirroredAtF.median,
        mirroredAtF.minimum, mirroredTetsG, mirroredNodesG, mirroredAtG.median, mirroredAtG.minimum,
        edgeStatsOf(withLocal.mesh(),
                    *meshing::owningTetrahedraOf(model.map(), withLocal.mesh(),
                                                 facetsOf(model.map(), refined)))
            .median));

    // 1. THE TARGET REGION IS REFINED, on the characteristic scale and on the
    //    density -- never on a total element count, which the brief forbids as
    //    the qualification.
    CHECK(refinedAtF.median < plainAtF.median);
    CHECK(refinedAtF.mean < plainAtF.mean);
    CHECK(refinedNodesF > plainNodesF);
    CHECK(refinedTetsF > plainTetsF);
    // 2. AND BY A MARGIN, not by a rounding: the band's characteristic scale
    //    falls by a factor of 2.1, measured, against a gate of 1.5.
    //
    //    WHY NOT "THE ELEMENTS THERE ARE AT THE 6 mm REQUESTED SIZE". Because
    //    they cannot be, and the reason is the same architectural bound: the
    //    face's own boundary triangles are 100 mm across and the elements
    //    touching them must span them. The band's median is 29 mm with a 6 mm
    //    request, and its SMALLEST edge reaches the global target -- so the
    //    honest absolute statement is about the minimum, and the honest
    //    statement about the bulk is relative. Asserting 6 mm here would be
    //    asserting something the pipeline does not promise.
    const double improvement = plainAtF.median / refinedAtF.median;
    INFO(std::format("band at F: median {:.6g} -> {:.6g} mm (factor {:.4g}), minimum {:.6g} -> "
                     "{:.6g} mm, against a {:.6g} mm request and a {:.6g} mm global target",
                     plainAtF.median, refinedAtF.median, improvement, plainAtF.minimum,
                     refinedAtF.minimum, localMm, globalMm));
    CHECK(improvement > 1.5);
    CHECK(refinedAtF.minimum < globalMm);
    CHECK(refinedAtF.minimum < plainAtF.minimum);
    // 3. THE NON-TARGET REGION IS NOT COLLAPSED TO THE FINE SIZE. It may grade
    //    -- Netgen inserts transition elements -- but it must stay coarser than
    //    the refined region.
    CHECK(refinedAtG.median > refinedAtF.median);
    // 4. WRONG-REGION PROTECTION, by the mirror: the band at F is finer when F
    //    is the target than when G is, AND the band at G is finer when G is the
    //    target than when F is. Both directions, so a control that refined
    //    everything would fail one of them.
    INFO(std::format("at F: {:.6g} mm when F is refined vs {:.6g} mm when G is; "
                     "at G: {:.6g} mm when G is refined vs {:.6g} mm when F is",
                     refinedAtF.median, mirroredAtF.median, mirroredAtG.median, refinedAtG.median));
    CHECK(refinedAtF.median < mirroredAtF.median);
    CHECK(mirroredAtG.median < refinedAtG.median);
    CHECK(refinedNodesF > mirroredNodesF);
    CHECK(mirroredNodesG > refinedNodesG);
    // 5. The refinement did not break anything: quality evaluated, and INVALID
    //    is mandatory zero.
    CHECK(model.quality().invalidElements == 0);
    CHECK(meshing::evaluateMeshQuality(refinedAtTheOtherFace.mesh()).invalidElements == 0);
}

TEST_CASE("MeshLocalRefinement_FollowsAnEditOfItsLocalSize", "[refmod][mesh][rm07][remesh]") {
    auto built = bettercad::reference::buildMeshLocalRefinementReferenceModel();
    REQUIRE(built.has_value());
    const FaceName refined = built->refined();
    MeshedReference model(std::move(built->document));

    // Measured in the BAND at F, for the reason the refinement test records: a
    // planar face's two boundary facets cannot shrink, so a whole-mesh median
    // is dominated by elements the control never reaches. Measured, the
    // whole-mesh median RISES from 16.2 mm to 16.2 mm either way between these
    // two requests while the band at F halves -- which is why the band is what
    // this asserts and the whole-mesh figure is only reported.
    const double reach = 12.0;
    auto bandMedian = [&](const VolumeMesh& mesh) {
        return edgeStatsOf(mesh.mesh(), tetsNearPlane(mesh.mesh(), 1, 0.0, reach)).median;
    };

    const VolumeMesh& first = model.require();
    const std::size_t firstTets = first.tetrahedronCount();
    const EdgeStats firstEdges = edgeStatsOf(first.mesh());
    const double firstBand = bandMedian(first);
    const std::size_t firstBandNodes = nodesNearPlane(first.mesh(), 1, 0.0, reach);
    CHECK(meshing::describesTheModel(model.currency()));

    // fine1 -> fine2, through the COMMAND, which is how canonical intent is
    // edited (P16-CMD-001): nothing in the suite reaches past the command layer
    // to set a definition directly.
    CommandHistory history;
    const auto executed = history.execute(
        model.document(), std::make_unique<meshing::EditLocalMeshSizingCommand>(
                              model.control(), refined, Length::fromSi(3.0e-3)));
    INFO((executed.has_value() ? std::string{"ok"} : executed.error().message));
    REQUIRE(executed.has_value());

    // STALE FOR THE RIGHT REASON. The geometry did not change, so this is
    // StaleIntent and not StaleGeometry -- the invalidation precision the
    // milestone was built for.
    CHECK(model.currency() == MeshCurrency::StaleIntent);
    CHECK_FALSE(meshing::describesTheModel(model.currency()));
    // The OLD mesh is still there to look at: stale is old, not wrong.
    CHECK(model.mesher().mesh(model.control()) != nullptr);
    CHECK(model.mesher().heldMeshCount() == 1);

    const VolumeMesh& second = model.require();
    CHECK(meshing::describesTheModel(model.currency()));
    const EdgeStats secondEdges = edgeStatsOf(second.mesh());
    const double secondBand = bandMedian(second);
    const std::size_t secondBandNodes = nodesNearPlane(second.mesh(), 1, 0.0, reach);
    WARN(std::format("RM-MESH-07 local size 6 mm -> 3 mm: {} -> {} tets; band at F median edge "
                     "{:.6g} -> {:.6g} mm, band nodes {} -> {}; whole-mesh median {:.6g} -> {:.6g} "
                     "mm (reported, not asserted -- the untouched bulk dominates it)",
                     firstTets, second.tetrahedronCount(), firstBand, secondBand, firstBandNodes,
                     secondBandNodes, firstEdges.median, secondEdges.median));
    // THE SAME GeometryReference, with the new semantics applied.
    REQUIRE(second.sizing().local.size() == 1);
    CHECK(second.sizing().local.front().face == refined);
    CHECK(second.sizing().local.front().state == meshing::SizingSelectionState::Resolved);
    CHECK_THAT(second.sizing().local.front().targetSize.in(units::mm), WithinRel(3.0, 1e-15));
    CHECK_THAT(second.sizing().regions.front().maxSize.in(units::mm), WithinRel(3.0, 1e-12));
    // THE NEW SEMANTICS ACTUALLY APPLIED, at F and on both measures.
    CHECK(secondBand < firstBand);
    CHECK(secondBandNodes > firstBandNodes);
    requireStructurallySound(second, "RM-MESH-07 after the size edit");

    // Undo puts the intent back, and the mesh is stale again -- for intent, not
    // for geometry.
    REQUIRE(history.undo(model.document()).has_value());
    CHECK(model.currency() == MeshCurrency::StaleIntent);
    const VolumeMesh& restored = model.require();
    REQUIRE(restored.sizing().local.size() == 1);
    CHECK_THAT(restored.sizing().local.front().targetSize.in(units::mm), WithinRel(6.0, 1e-15));
    CHECK(restored.tetrahedronCount() == firstTets);
}

// ---------------------------------------------------------------------------
// RM-MESH-08 -- invalid geometry

TEST_CASE("MeshOpenProfile_IsRefusedExplicitlyAndPublishesNothing", "[refmod][mesh][rm08][failure]") {
    auto built = bettercad::reference::buildMeshOpenProfileReferenceModel();
    REQUIRE(built.has_value());
    const ObjectId solid = built->solid;
    const ObjectId profile = built->sketch;
    MeshedReference model(std::move(built->document));

    // THE SKETCH SOLVES AND THE EXTRUDE FAILS, so the diagnostic is about the
    // real cause. The suite checks the states rather than only the message.
    const auto report = model.regenerate();
    CHECK_FALSE(report.succeeded());
    CHECK(model.regenerator().state(profile).has_value());
    CHECK(model.regenerator().body(solid) == nullptr);
    const std::optional<meshing::GeometryIneligibility> reason =
        meshing::geometryIneligibility(model.document(), model.regenerator(), solid);
    REQUIRE(reason.has_value());
    INFO("ineligibility: " << meshing::toString(*reason));
    CHECK(*reason == meshing::GeometryIneligibility::RegenerationFailed);

    // THE CONTROL IS THERE, so the refusal cannot be about a missing control.
    REQUIRE(model.definition() != nullptr);
    CHECK(model.definition()->definition().body == solid);

    auto generated = model.generate();
    REQUIRE_FALSE(generated.has_value());
    INFO("diagnostic: " << generated.error().message);
    // A STRUCTURED diagnostic: a code a caller can branch on, and a message
    // that names the failing object and the real cause.
    CHECK(generated.error().code == ErrorCode::FailedPrecondition);
    CHECK_FALSE(generated.error().message.empty());
    CHECK_THAT(generated.error().message, ContainsSubstring("OpenSolid"));

    // NOTHING WAS PUBLISHED. The brief's forbidden outcome is a reported
    // success carrying zero tetrahedra; what the suite checks is stronger --
    // there is no mesh at all, no map, no quality report, and the currency says
    // the attempt failed.
    CHECK(model.mesher().heldMeshCount() == 0);
    CHECK(model.mesher().mesh(model.control()) == nullptr);
    CHECK(model.mesher().map(model.control()) == nullptr);
    CHECK(model.mesher().quality(model.control()) == nullptr);
    CHECK(model.currency() == MeshCurrency::GenerationFailed);
    CHECK_FALSE(meshing::describesTheModel(model.currency()));
    const Error* failure = model.mesher().lastFailure(model.control());
    REQUIRE(failure != nullptr);
    CHECK(failure->message == generated.error().message);

    // Direct meshing is refused identically, so the refusal is the geometry
    // boundary's and not the service's.
    auto direct = meshing::volumeMeshFor(model.document(), model.regenerator(), solid);
    REQUIRE_FALSE(direct.has_value());
    CHECK(direct.error().code == ErrorCode::FailedPrecondition);

    // AND IT IS DETERMINISTIC. Five attempts, the same refusal, nothing
    // accumulating and nothing published on any of them.
    for (int attempt = 0; attempt < 5; ++attempt) {
        INFO("attempt " << attempt + 1);
        auto again = model.generate();
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().code == generated.error().code);
        CHECK(again.error().message == generated.error().message);
        CHECK(model.mesher().heldMeshCount() == 0);
    }
}

// ---------------------------------------------------------------------------
// Model-change remeshing (and the oracle that catches a reused mesh)

TEST_CASE("MeshReference_AGeometryEditRemeshesAgainstTheNewAnalyticVolume",
          "[refmod][mesh][remesh][analytic]") {
    // THE ORACLE IS THE NEW ANALYTIC VOLUME, which is what makes this catch a
    // stale mesh being handed back: a reused M1 would recover the OLD volume,
    // and the old volume is not within any tolerance of the new one.
    //
    // Two models, because the brief asks for at least two and because they fail
    // differently: the block's edit changes one edge length, the plate's
    // changes a thickness that also scales the hole's wall.
    SECTION("RM-MESH-01, a = 120 -> 150 mm") {
        auto built = bettercad::reference::buildMeshBlockReferenceModel();
        REQUIRE(built.has_value());
        const ParameterId a = built->a;
        MeshedReference model(std::move(built->document));
        const double before = analytic::blockVolumeMm3(mm(model.document(), "block_a"),
                                                       mm(model.document(), "block_b"),
                                                       mm(model.document(), "block_c"));
        const VolumeMesh& first = model.require();
        CHECK_THAT(first.tetrahedralVolume().in(units::mm3), WithinRel(before, kPlanarVolume));
        CHECK(model.currency() == MeshCurrency::Current);

        REQUIRE(model.document().setParameterValue(a, 150.0 * units::mm).has_value());
        // STALE FOR THE GEOMETRY, which is the distinction P16-CMD-001 built:
        // the shape changed, not merely the discretisation.
        CHECK(model.currency() == MeshCurrency::StaleGeometry);
        CHECK(meshing::isStale(model.document(), first));
        CHECK_FALSE(meshing::describesTheModel(model.currency()));

        const auto report = model.regenerate();
        CHECK(report.succeeded());
        const double after = analytic::blockVolumeMm3(mm(model.document(), "block_a"),
                                                      mm(model.document(), "block_b"),
                                                      mm(model.document(), "block_c"));
        CHECK_THAT(after, WithinRel(150.0 * 70.0 * 35.0, 1e-15));
        CHECK(after != before);

        const VolumeMesh& second = model.require();
        WARN(std::format("RM-MESH-01 geometry edit: analytic {:.17g} -> {:.17g} mm^3, mesh {:.17g} "
                         "-> {:.17g} mm^3, {} -> {} tets",
                         before, after, first.tetrahedralVolume().in(units::mm3),
                         second.tetrahedralVolume().in(units::mm3), first.tetrahedronCount(),
                         second.tetrahedronCount()));
        // M2 DESCRIBES G2 AND NOT G1.
        CHECK_THAT(second.tetrahedralVolume().in(units::mm3), WithinRel(after, kPlanarVolume));
        CHECK_THAT(second.cadVolume().in(units::mm3), WithinRel(after, kCadVolume));
        CHECK(std::abs(second.tetrahedralVolume().in(units::mm3) - before) / before > 0.1);
        CHECK(model.currency() == MeshCurrency::Current);
        requireStructurallySound(second, "RM-MESH-01 after the edit");
    }

    SECTION("RM-MESH-03, t = 12 -> 16 mm") {
        auto built = bettercad::reference::buildMeshPlateWithHoleReferenceModel();
        REQUIRE(built.has_value());
        const ParameterId thickness = built->thickness;
        MeshedReference model(std::move(built->document));
        auto volumeNow = [&] {
            return analytic::plateWithHoleVolumeMm3(mm(model.document(), "plate_l"),
                                                    mm(model.document(), "plate_w"),
                                                    mm(model.document(), "plate_t"),
                                                    mm(model.document(), "plate_hole_r"));
        };
        const double before = volumeNow();
        const VolumeMesh& first = model.require();
        const double firstMeshed = first.tetrahedralVolume().in(units::mm3);

        REQUIRE(model.document().setParameterValue(thickness, 16.0 * units::mm).has_value());
        CHECK(model.currency() == MeshCurrency::StaleGeometry);
        CHECK(model.regenerate().succeeded());
        const double after = volumeNow();
        CHECK(after > before);

        const VolumeMesh& second = model.require();
        const double secondMeshed = second.tetrahedralVolume().in(units::mm3);
        const analytic::MeshedVolumeBound bound = analytic::plateWithHoleMeshBound(
            mm(model.document(), "plate_l"), mm(model.document(), "plate_w"),
            mm(model.document(), "plate_t"), mm(model.document(), "plate_hole_r"), kDeflectionMm);
        WARN(std::format("RM-MESH-03 thickness edit: analytic {:.17g} -> {:.17g} mm^3, mesh {:.17g} "
                         "-> {:.17g} mm^3, bound now [{:.17g}, {:.17g}]",
                         before, after, firstMeshed, secondMeshed, bound.lower, bound.upper));
        CHECK(bound.contains(secondMeshed));
        CHECK(secondMeshed >= after);
        // And the hole is STILL a hole after the edit: the void check again, on
        // the new geometry.
        const VoidRegion hole{.axisX = 50.0,
                              .axisY = 30.0,
                              .radius = mm(model.document(), "plate_hole_r"),
                              .deflection = kDeflectionMm};
        CHECK(checkVoid(second.mesh(), hole).violations() == 0);
        requireStructurallySound(second, "RM-MESH-03 after the edit");
    }
}

TEST_CASE("MeshReference_ScalingAModelScalesItsVolumeByTheCube", "[refmod][mesh][scale][analytic]") {
    // SUPPLEMENTAL, and cheap: a model scaled by s must have s^3 times the
    // volume, and its DIMENSIONLESS quality should be about the same under
    // equivalently scaled controls. A unit-conversion defect anywhere in the
    // sizing chain would break the second half while leaving the first intact.
    auto built = bettercad::reference::buildMeshBlockReferenceModel();
    REQUIRE(built.has_value());
    const ParameterId a = built->a;
    const ParameterId b = built->b;
    const ParameterId c = built->c;
    MeshedReference model(std::move(built->document));
    const double baseVolume = analytic::blockVolumeMm3(mm(model.document(), "block_a"),
                                                       mm(model.document(), "block_b"),
                                                       mm(model.document(), "block_c"));
    VolumeMeshControls controls;
    controls.sizing.globalTargetSize = 20_mm;
    const VolumeMesh small = model.requireWith(controls);
    const ShapeExtremes smallShape =
        shapeExtremesOf(meshing::evaluateMeshQuality(small.mesh()));

    constexpr double s = 2.5;
    REQUIRE(model.document().setParameterValue(a, s * 120.0 * units::mm).has_value());
    REQUIRE(model.document().setParameterValue(b, s * 70.0 * units::mm).has_value());
    REQUIRE(model.document().setParameterValue(c, s * 35.0 * units::mm).has_value());
    CHECK(model.regenerate().succeeded());
    VolumeMeshControls scaled;
    scaled.sizing.globalTargetSize = Length::fromSi(s * 20.0e-3);
    const VolumeMesh large = model.requireWith(scaled);
    const ShapeExtremes largeShape =
        shapeExtremesOf(meshing::evaluateMeshQuality(large.mesh()));

    const double expected = baseVolume * s * s * s;
    WARN(std::format("scale {:.4g}: analytic {:.17g} -> {:.17g} mm^3, mesh {:.17g} -> {:.17g}; "
                     "worst radius ratio {:.6g} -> {:.6g}, min dihedral {:.6g} -> {:.6g} deg",
                     s, baseVolume, expected, small.tetrahedralVolume().in(units::mm3),
                     large.tetrahedralVolume().in(units::mm3), smallShape.worstRadiusRatio,
                     largeShape.worstRadiusRatio, smallShape.minDihedralDeg,
                     largeShape.minDihedralDeg));
    CHECK_THAT(large.cadVolume().in(units::mm3), WithinRel(expected, kCadVolume));
    CHECK_THAT(large.tetrahedralVolume().in(units::mm3), WithinRel(expected, kPlanarVolume));
    // Dimensionless metrics are scale-free. Asserted loosely, because the
    // backend's discrete choices need not be identical at two scales -- and
    // reported, because the number is the point.
    CHECK_THAT(largeShape.worstRadiusRatio, WithinRel(smallShape.worstRadiusRatio, 1e-6));
    CHECK_THAT(largeShape.minDihedralDeg, WithinRel(smallShape.minDihedralDeg, 1e-6));
}

// ---------------------------------------------------------------------------
// Settings-change remeshing, and undo/redo of the intent

TEST_CASE("MeshReference_ASettingsEditRemeshesAtTheNewCharacteristicScale",
          "[refmod][mesh][remesh][sizing]") {
    // ON THE CYLINDER, and the choice is forced rather than preferred. The
    // brief suggests RM-MESH-01, but a block's boundary is irreducible -- OCCT
    // triangulates a planar face with two triangles whatever the deflection --
    // so a global target has nowhere to act on it and this suite's own sizing
    // table shows the three levels giving one mesh. The cylinder is where a
    // global target can be proven, which is also where P16-SIZE-001 proved it.
    auto built = bettercad::reference::buildMeshCylinderReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    const double expected =
        analytic::cylinderVolumeMm3(mm(model.document(), "cyl_r"), mm(model.document(), "cyl_h"));

    const VolumeMesh& first = model.require();
    const EdgeStats firstEdges = edgeStatsOf(first.mesh());
    const std::size_t firstTets = first.tetrahedronCount();
    CHECK(model.currency() == MeshCurrency::Current);

    // S1 -> S2 THROUGH THE COMMAND: canonical intent is edited one way.
    CommandHistory history;
    const auto executed = history.execute(
        model.document(),
        std::make_unique<meshing::SetGlobalMeshSizeCommand>(model.control(), Length::fromSi(6.0e-3)));
    INFO((executed.has_value() ? std::string{"ok"} : executed.error().message));
    REQUIRE(executed.has_value());

    // STALE FOR THE INTENT, NOT THE GEOMETRY. The shape did not change, and a
    // single boolean could not say so.
    CHECK(model.currency() == MeshCurrency::StaleIntent);
    CHECK_FALSE(meshing::describesTheModel(model.currency()));
    CHECK(meshing::isStale(model.document(), first, model.definition()->definition().mesh));
    CHECK_FALSE(meshing::isStale(model.document(), first));
    // The old mesh is still inspectable, which the stale-state contract needs.
    CHECK(model.mesher().mesh(model.control()) != nullptr);

    const VolumeMesh& second = model.require();
    const EdgeStats secondEdges = edgeStatsOf(second.mesh());
    WARN(std::format("RM-MESH-02 global target 12 -> 6 mm: {} -> {} tets, median edge {:.6g} -> "
                     "{:.6g} mm, mean {:.6g} -> {:.6g} mm, volume {:.17g} -> {:.17g} mm^3",
                     firstTets, second.tetrahedronCount(), firstEdges.median, secondEdges.median,
                     firstEdges.mean, secondEdges.mean, first.tetrahedralVolume().in(units::mm3),
                     second.tetrahedralVolume().in(units::mm3)));
    // M2 WAS GENERATED UNDER S2, proven by the characteristic scale and by the
    // controls the mesh records -- not by an element-count ratio, which the
    // brief forbids.
    CHECK_THAT(second.sizing().globalTargetSize.in(units::mm), WithinRel(6.0, 1e-12));
    CHECK(secondEdges.median < firstEdges.median);
    CHECK(secondEdges.mean < firstEdges.mean);
    CHECK(model.currency() == MeshCurrency::Current);
    requireStructurallySound(second, "RM-MESH-02 after the settings edit");
    // The GEOMETRY did not move, so the CAD volume is untouched and the mesh is
    // still inside its bound.
    CHECK_THAT(second.cadVolume().in(units::mm3), WithinRel(expected, kCadVolume));
    CHECK(analytic::cylinderMeshBound(mm(model.document(), "cyl_r"), mm(model.document(), "cyl_h"),
                                      kDeflectionMm)
              .contains(second.tetrahedralVolume().in(units::mm3)));
}

TEST_CASE("MeshReference_UndoAndRedoOfTheIntentAreFollowedByTheMesh",
          "[refmod][mesh][undo][cross]") {
    // S1 -> edit S2 -> remesh -> undo -> remesh -> redo -> remesh, with every
    // mesh validated against the intent that is CURRENT at that point. A
    // reference suite should exercise the cross-milestone integration, and this
    // is where P16-CMD and P16-VOL meet.
    auto built = bettercad::reference::buildMeshCylinderReferenceModel();
    REQUIRE(built.has_value());
    MeshedReference model(std::move(built->document));
    CommandHistory history;

    auto sizeNow = [&] {
        const std::optional<Length> target =
            model.definition()->definition().mesh.sizing.globalTargetSize;
        REQUIRE(target.has_value());
        return target->in(units::mm);
    };
    auto remeshAndCheck = [&](double expectedTargetMm, const char* stage) {
        INFO(stage);
        CHECK_THAT(sizeNow(), WithinRel(expectedTargetMm, 1e-12));
        const VolumeMesh& mesh = model.require();
        CHECK(model.currency() == MeshCurrency::Current);
        // THE MESH WAS BUILT FROM THE INTENT THAT IS CURRENT NOW, which is the
        // claim: it records the target it used.
        CHECK_THAT(mesh.sizing().globalTargetSize.in(units::mm),
                   WithinRel(expectedTargetMm, 1e-12));
        CHECK(mesh.controls() == model.definition()->definition().mesh);
        requireStructurallySound(mesh, stage);
        return std::pair{mesh.tetrahedronCount(), edgeStatsOf(mesh.mesh()).median};
    };

    // S2 IS COARSER, NOT FINER, and the direction is chosen for cost rather
    // than for meaning: this case meshes four times, and what it has to prove
    // is that each mesh follows the intent that is current -- which two
    // DISTINGUISHABLE meshes establish whichever way round they are. Refining
    // to 6 mm gives 1977 tetrahedra and cost 53 seconds per run in a debug
    // build, for nothing this case asserts. The "finer" direction is proved by
    // `MeshReference_ASettingsEditRemeshesAtTheNewCharacteristicScale`, which
    // meshes twice and is about the scale.
    const auto [tetsS1, edgeS1] = remeshAndCheck(12.0, "S1");

    REQUIRE(history
                .execute(model.document(), std::make_unique<meshing::SetGlobalMeshSizeCommand>(
                                               model.control(), Length::fromSi(24.0e-3)))
                .has_value());
    CHECK(model.currency() == MeshCurrency::StaleIntent);
    const auto [tetsS2, edgeS2] = remeshAndCheck(24.0, "S2");

    REQUIRE(history.undo(model.document()).has_value());
    CHECK(model.currency() == MeshCurrency::StaleIntent);
    const auto [tetsUndone, edgeUndone] = remeshAndCheck(12.0, "undone to S1");

    REQUIRE(history.redo(model.document()).has_value());
    CHECK(model.currency() == MeshCurrency::StaleIntent);
    const auto [tetsRedone, edgeRedone] = remeshAndCheck(24.0, "redone to S2");

    WARN(std::format("RM-MESH-02 undo/redo: S1 {} tets ({:.6g} mm), S2 {} tets ({:.6g} mm), undone "
                     "{} tets ({:.6g} mm), redone {} tets ({:.6g} mm)",
                     tetsS1, edgeS1, tetsS2, edgeS2, tetsUndone, edgeUndone, tetsRedone, edgeRedone));
    // UNDO AND REDO ARE EXACT, not approximate: the same intent gives the same
    // mesh, element for element.
    CHECK(tetsUndone == tetsS1);
    CHECK(edgeUndone == edgeS1);
    CHECK(tetsRedone == tetsS2);
    CHECK(edgeRedone == edgeS2);
    CHECK(tetsS2 != tetsS1);
    // AND THE HISTORY CARRIED INTENT, NOT A MESH. One entry for one edit,
    // whatever the meshes cost.
    CHECK(history.undoCount() == 1);
    CHECK(history.canUndo());
}

// ---------------------------------------------------------------------------
// Save / load / regenerate

TEST_CASE("MeshReference_SaveLoadRegenerateKeepsTheIntentAndNotTheMesh",
          "[refmod][mesh][persist][cross]") {
    // THREE MODELS, as the brief names them: the block, the plate with the hole
    // (whose boundary intent is a hole wall) and the local-refinement model
    // (global sizing, local sizing and a GeometryReference together). RM-MESH-07
    // gives stronger coverage than a plain block, which is why it is included.
    TempDir directory;

    struct Case {
        const char* label;
        MeshReferenceModelKind kind;
    };
    const std::array<Case, 3> cases{{{"RM-MESH-01", MeshReferenceModelKind::Block},
                                     {"RM-MESH-03", MeshReferenceModelKind::PlateWithHole},
                                     {"RM-MESH-07", MeshReferenceModelKind::LocalRefinement}}};

    for (const Case& which : cases) {
        INFO(which.label);
        auto document = bettercad::reference::buildMeshReferenceModel(which.kind);
        REQUIRE(document.has_value());
        MeshedReference before(std::move(*document));

        // THE CANONICAL FINGERPRINT is the whole MeshControlDefinition: the
        // body, the discretisation, the sizing, the quality policy and the
        // boundary sets. Deliberately NOT the mesh, which is derived -- a
        // fingerprint that contained it could not tell a restored mesh from a
        // regenerated one.
        const meshing::MeshControlDefinition intent = before.definition()->definition();
        const VolumeMesh& original = before.require();
        const std::size_t originalNodes = original.nodeCount();
        const std::size_t originalTets = original.tetrahedronCount();
        const double originalVolume = original.tetrahedralVolume().in(units::mm3);
        const std::vector<Vec3> originalPoints = canonicalNodes(original.mesh());
        const meshing::GeometryMeshMappingReport originalMapping = before.map().report();

        const std::filesystem::path path =
            directory.path() / std::format("{}.bcad", which.label);
        REQUIRE(io::saveDocument(before.document(), path).has_value());

        // THE GENERATED MESH IS NOT IN THE FILE. Checked against the bytes, not
        // asserted: a serializer that wrote nodes and elements would show here
        // and nowhere else in the suite.
        const std::string contents = readFile(path);
        INFO(std::format("{} is {} bytes", path.filename().string(), contents.size()));
        CHECK_THAT(contents, !ContainsSubstring("\"nodes\""));
        CHECK_THAT(contents, !ContainsSubstring("\"tetrahedra\""));
        CHECK_THAT(contents, !ContainsSubstring("\"elements\""));
        CHECK_THAT(contents, ContainsSubstring("mesh-control"));
        // A mesh of even these small models is tens of kilobytes of
        // coordinates; the file is a description of intent and is far smaller.
        CHECK(contents.size() < 40000);

        auto loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());
        MeshedReference after(std::move(*loaded));

        // INTENT PRESERVED, exactly: the same body, the same sizes, the same
        // boundary-set IDENTITIES and the same face references.
        const meshing::MeshControlDefinition restored = after.definition()->definition();
        CHECK(restored == intent);
        CHECK(restored.body == intent.body);
        CHECK(restored.mesh.sizing == intent.mesh.sizing);
        CHECK(restored.mesh.surface == intent.mesh.surface);
        CHECK(restored.boundarySets.size() == intent.boundarySets.size());
        for (std::size_t i = 0; i < intent.boundarySets.size(); ++i) {
            INFO("boundary set " << i);
            CHECK(restored.boundarySets[i].id == intent.boundarySets[i].id);
            CHECK(restored.boundarySets[i].name == intent.boundarySets[i].name);
            CHECK(restored.boundarySets[i].faces == intent.boundarySets[i].faces);
        }
        // The local control's GeometryReference survived unchanged, which is
        // the part RM-MESH-07 is in this list for.
        CHECK(restored.mesh.sizing.local == intent.mesh.sizing.local);

        // NOTHING WAS RESTORED AS A MESH. The loaded document's mesher is
        // empty until something asks it to generate.
        CHECK(after.mesher().heldMeshCount() == 0);
        CHECK(after.mesher().mesh(after.control()) == nullptr);
        CHECK(after.currency() == MeshCurrency::NoMesh);

        // AND REGENERATION REPRODUCES THE SAME MESH, which is the whole point
        // of persisting intent rather than results.
        const VolumeMesh& again = after.require();
        WARN(std::format("{} save/load: {} bytes, {} nodes / {} tets / {:.17g} mm^3 before, "
                         "{} / {} / {:.17g} after",
                         which.label, contents.size(), originalNodes, originalTets, originalVolume,
                         again.nodeCount(), again.tetrahedronCount(),
                         again.tetrahedralVolume().in(units::mm3)));
        CHECK(again.nodeCount() == originalNodes);
        CHECK(again.tetrahedronCount() == originalTets);
        CHECK(again.tetrahedralVolume().si() == original.tetrahedralVolume().si());
        CHECK(canonicalNodes(again.mesh()) == originalPoints);
        CHECK(after.map().report() == originalMapping);
        requireStructurallySound(again, which.label);

        // THE CURRENT FACETS ARE NEW HANDLES, and that is allowed: facet IDs
        // are derived and a remesh reassigns them. What must survive is the
        // reference that produces them.
        for (const meshing::NamedBoundarySet& set : after.definition()->orderedBoundarySets()) {
            INFO("boundary set " << set.name);
            auto resolved = meshing::resolveBoundarySet(set, after.map());
            REQUIRE(resolved.has_value());
            CHECK(resolved->fullyResolved());
            CHECK_FALSE(resolved->mapping.facets.empty());
        }
    }
}

// ---------------------------------------------------------------------------
// Determinism

TEST_CASE("MeshReference_IsDeterministicOverRepeatedGeneration", "[refmod][mesh][determinism]") {
    // FIVE RUNS, which is the repeat count this repository's qualification
    // discipline already uses, and the standard is P16-VOL-001's own: counts,
    // the volume BITWISE, the sizing restrictions, and the element connectivity
    // index for index. Nothing weaker is invented here, and node POSITIONS are
    // added because a reference suite can afford to compare them.
    struct Case {
        const char* label;
        MeshReferenceModelKind kind;
    };
    const std::array<Case, 4> cases{{{"RM-MESH-01", MeshReferenceModelKind::Block},
                                     {"RM-MESH-03", MeshReferenceModelKind::PlateWithHole},
                                     {"RM-MESH-06", MeshReferenceModelKind::TransformedPlaced},
                                     {"RM-MESH-07", MeshReferenceModelKind::LocalRefinement}}};
    std::string table = "| Model | Runs | Nodes | Tet4 | Volume mm^3 | Stable |\n";
    for (const Case& which : cases) {
        INFO(which.label);
        auto document = bettercad::reference::buildMeshReferenceModel(which.kind);
        REQUIRE(document.has_value());
        MeshedReference model(std::move(*document));

        const VolumeMesh first = model.requireWith(model.definition()->definition().mesh);
        const std::vector<Vec3> firstPoints = canonicalNodes(first.mesh());
        const meshing::MeshQualityReport firstQuality = meshing::evaluateMeshQuality(first.mesh());
        bool stable = true;
        for (int run = 1; run < 5; ++run) {
            INFO("run " << run + 1);
            const VolumeMesh again = model.requireWith(model.definition()->definition().mesh);
            REQUIRE(again.nodeCount() == first.nodeCount());
            REQUIRE(again.tetrahedronCount() == first.tetrahedronCount());
            CHECK(again.boundaryTriangleCount() == first.boundaryTriangleCount());
            // BITWISE, not within a tolerance: a deterministic generator gives
            // the same double.
            CHECK(again.tetrahedralVolume().si() == first.tetrahedralVolume().si());
            CHECK(again.boundaryVolume().si() == first.boundaryVolume().si());
            CHECK(again.sizing().restrictions == first.sizing().restrictions);
            CHECK(again.sizing().regions == first.sizing().regions);
            CHECK(again.conformity() == first.conformity());
            // Connectivity index for index, which is P16-VOL's standard and
            // implies the handles were assigned the same way.
            for (std::size_t i = 0; i < again.mesh().tetrahedra().size(); ++i) {
                if (again.mesh().tetrahedra()[i].nodes != first.mesh().tetrahedra()[i].nodes) {
                    stable = false;
                }
            }
            CHECK(canonicalNodes(again.mesh()) == firstPoints);
            // The quality report is deterministic too -- its ORDER as well as
            // its numbers, which is what makes it diffable.
            const meshing::MeshQualityReport quality = meshing::evaluateMeshQuality(again.mesh());
            CHECK(quality.summaries == firstQuality.summaries);
            CHECK(quality.findings == firstQuality.findings);
            CHECK(quality.invalidElements == firstQuality.invalidElements);
            // And the mapping coverage.
            auto map = meshing::geometryMeshMapFor(model.document(), model.regenerator(),
                                                   model.body(), again);
            REQUIRE(map.has_value());
            CHECK(map->report().boundaryFacetCount == first.boundaryTriangleCount());
            CHECK(map->report().complete());
        }
        CHECK(stable);
        table += std::format("| {} | 5 | {} | {} | {:.17g} | {} |\n", which.label, first.nodeCount(),
                             first.tetrahedronCount(), first.tetrahedralVolume().in(units::mm3),
                             stable ? "yes" : "NO");
    }
    WARN("P16-REFMOD-001 determinism, 5 runs each\n" << table);
}

TEST_CASE("MeshReference_EveryModelsOutcomeIsStableOverRepeatedAttempts",
          "[refmod][mesh][determinism]") {
    // SUCCESS AND FAILURE STABILITY FOR ALL EIGHT, which is the other half of
    // the determinism requirement: the four models above get a full
    // fingerprint, and every model -- RM-MESH-08 included -- must come out the
    // same way every time.
    for (const MeshReferenceModelInfo& info : kMeshReferenceModels) {
        INFO(info.name);
        auto document = bettercad::reference::buildMeshReferenceModel(info.kind);
        REQUIRE(document.has_value());
        MeshedReference model(std::move(*document));
        std::optional<std::string> firstDiagnostic;
        for (int run = 0; run < 3; ++run) {
            INFO("run " << run + 1);
            auto generated = model.generate();
            REQUIRE(generated.has_value() == info.expectMesh);
            if (!info.expectMesh) {
                if (!firstDiagnostic) {
                    firstDiagnostic = generated.error().message;
                }
                CHECK(generated.error().message == *firstDiagnostic);
                CHECK(model.mesher().heldMeshCount() == 0);
            }
        }
    }
}

#endif // BETTERCAD_TESTS_EXPECT_NETGEN
