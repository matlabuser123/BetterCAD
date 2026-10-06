#include "reference/MeshTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

// P16-QUAL-001 — the final cross-milestone gates.
//
// These are NOT re-runs of the per-milestone suites, which the three-preset
// regression re-runs in full anyway. Each one here crosses milestone
// boundaries and is required by the qualification brief as a FINAL gate rather
// than as inherited evidence: a fact can hold in P16-VOL's tests, hold again in
// P16-CMD's and P16-PERSIST's, and still fail when generation, intent, history
// and the file are put in one sequence.
//
// The audit that preceded this file found that almost every gate the brief
// names is already covered, and names the covering test in
// docs/verification/P16-QUAL-001/MILESTONE_AUDIT.md. What is here is only what
// nothing covered:
//
//   the authority matrix, as one executable statement -- meshing must leave
//   the canonical document byte-identical
//   persistence independence from mesh DENSITY -- the brief's strongest
//   authority check, and the one a persisted node array would fail
//   surface orientation determinism across repeated generation
//   stale geometry through a whole sequence, including a FAILED regeneration
namespace {

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using namespace bettercad::test::meshref;
using bettercad::reference::MeshReferenceModelKind;
using Catch::Matchers::WithinRel;
using meshing::MeshCurrency;
using meshing::VolumeMesh;
using meshing::VolumeMeshControls;

/// A reference model, built and regenerated.
[[nodiscard]] MeshedReference referenceModel(MeshReferenceModelKind kind) {
    auto document = bettercad::reference::buildMeshReferenceModel(kind);
    REQUIRE(document.has_value());
    return MeshedReference(std::move(*document));
}

/// The bytes a document saves to.
[[nodiscard]] std::string savedBytes(Document& document, const std::filesystem::path& path) {
    auto saved = io::saveDocument(document, path);
    INFO((saved.has_value() ? std::string{"ok"} : saved.error().message));
    REQUIRE(saved.has_value());
    return readFile(path);
}

} // namespace

#ifdef BETTERCAD_TESTS_EXPECT_NETGEN

TEST_CASE("P16Qualification_GeneratingAMeshLeavesTheCanonicalDocumentByteIdentical",
          "[p16qual][authority]") {
    // THE AUTHORITY MATRIX, AS ONE EXECUTABLE STATEMENT.
    //
    // ADR-030 makes the generated mesh derived state held by a service, never a
    // document object. The brief's authority table says CAD geometry and
    // meshing intent are canonical and that nodes, Tet4 connectivity, boundary
    // facets, the quality report and the mapping are not.
    //
    // Every one of those is a claim about what the DOCUMENT contains, so the
    // sharpest check is the document itself: save it, generate a mesh, save it
    // again, and require the bytes to be identical. A mesh that leaked into
    // canonical state -- through a new field, a cache written on access, or a
    // feature added later -- changes them.
    TempDir directory;
    MeshedReference model = referenceModel(MeshReferenceModelKind::PlateWithHole);

    const std::string before = savedBytes(model.document(), directory.path() / "before.bcad");
    const std::size_t objectsBefore = model.document().objects().size();
    const std::uint64_t revisionBefore = model.document().revision();

    const VolumeMesh& mesh = model.require();
    CHECK(mesh.tetrahedronCount() > 0);
    CHECK(model.currency() == MeshCurrency::Current);
    // The map and the quality report exist too, so this is not a check that
    // nothing happened.
    CHECK(model.mesher().heldMeshCount() == 1);
    CHECK(model.map().report().complete());
    CHECK(model.quality().structurallyValid);

    const std::string after = savedBytes(model.document(), directory.path() / "after.bcad");
    INFO(std::format("{} bytes before, {} after, {} tetrahedra generated in between",
                     before.size(), after.size(), mesh.tetrahedronCount()));
    CHECK(after == before);
    // And nothing was added to the document, nor was its revision moved:
    // generation is not an edit.
    CHECK(model.document().objects().size() == objectsBefore);
    CHECK(model.document().revision() == revisionBefore);
}

TEST_CASE("P16Qualification_TheSavedDocumentIsIndependentOfMeshDensity",
          "[p16qual][authority][persist]") {
    // THE BRIEF'S STRONGEST AUTHORITY CHECK. Identical canonical intent, a
    // COARSE generated mesh and a FINE one, and the saved document must be
    // essentially independent of which.
    //
    // It is asserted as BYTE IDENTITY rather than "essentially independent",
    // because the derived-state rule makes the stronger statement true: the
    // mesh is not in the file at all, so density cannot reach it. A document
    // that persisted a node array would be some tens of kilobytes larger for
    // the fine mesh and would fail this outright.
    TempDir directory;
    MeshedReference model = referenceModel(MeshReferenceModelKind::Cylinder);

    VolumeMeshControls coarse = model.definition()->definition().mesh;
    coarse.sizing.globalTargetSize = 24_mm;
    VolumeMeshControls fine = coarse;
    fine.sizing.globalTargetSize = 6_mm;

    const VolumeMesh coarseMesh = model.requireWith(coarse);
    const std::string afterCoarse = savedBytes(model.document(), directory.path() / "coarse.bcad");

    const VolumeMesh fineMesh = model.requireWith(fine);
    const std::string afterFine = savedBytes(model.document(), directory.path() / "fine.bcad");

    WARN(std::format("RM-MESH-02 at 24 mm: {} nodes, {} tets. At 6 mm: {} nodes, {} tets. "
                     "Saved document: {} bytes either way.",
                     coarseMesh.nodeCount(), coarseMesh.tetrahedronCount(), fineMesh.nodeCount(),
                     fineMesh.tetrahedronCount(), afterCoarse.size()));
    // The two meshes really do differ, or the comparison below is vacuous.
    CHECK(fineMesh.tetrahedronCount() > 2 * coarseMesh.tetrahedronCount());
    CHECK(afterFine == afterCoarse);
    // A node array for the fine mesh alone would be far larger than the whole
    // file, which is what makes byte identity the decisive form of the check.
    CHECK(afterCoarse.size() < fineMesh.nodeCount() * 3 * 8);
}

TEST_CASE("P16Qualification_SurfaceOrientationIsOutwardAndDeterministic",
          "[p16qual][surface][determinism]") {
    // SURFACE ORIENTATION, REPEATED. The brief's final adversarial check asks
    // whether the winding can flip unpredictably between runs.
    //
    // Orientation is checked here from the stored node order with no absolute
    // value anywhere: the enclosed volume of the boundary triangles is positive
    // exactly when they face out of the material, and an inward-facing surface
    // gives a negative one. Measured over repeated generation, both the sign
    // and the value must be identical -- bitwise, because a deterministic
    // generator gives the same double.
    MeshedReference model = referenceModel(MeshReferenceModelKind::Tube);
    const VolumeMeshControls controls = model.definition()->definition().mesh;

    const VolumeMesh first = model.requireWith(controls);
    const double firstEnclosed = enclosedVolumeMm3(first.mesh());
    const std::vector<Vec3> firstNodes = canonicalNodes(first.mesh());
    CHECK(firstEnclosed > 0.0);

    for (int run = 1; run < 4; ++run) {
        INFO("run " << run + 1);
        const VolumeMesh again = model.requireWith(controls);
        REQUIRE(again.boundaryTriangleCount() == first.boundaryTriangleCount());
        // OUTWARD, every time, and by the same amount to the last bit.
        const double enclosed = enclosedVolumeMm3(again.mesh());
        CHECK(enclosed > 0.0);
        CHECK(enclosed == firstEnclosed);
        CHECK(canonicalNodes(again.mesh()) == firstNodes);
        // And the stored winding itself is identical, triangle by triangle --
        // which a flipped patch would break even if the total survived.
        for (std::size_t i = 0; i < again.mesh().triangles().size(); ++i) {
            if (again.mesh().triangles()[i].nodes != first.mesh().triangles()[i].nodes) {
                FAIL("boundary triangle " << i << " has a different winding on run " << run + 1);
            }
        }
    }
    WARN(std::format("RM-MESH-04 surface orientation: {} boundary triangles, enclosed volume "
                     "{:.17g} mm^3, identical over 4 generations",
                     first.boundaryTriangleCount(), firstEnclosed));
}

TEST_CASE("P16Qualification_StaleGeometryNeverYieldsACurrentMesh", "[p16qual][stale]") {
    // THE WHOLE STALE-GEOMETRY CONTRACT IN ONE SEQUENCE, including the case
    // that matters most and is hardest to reach: a regeneration that FAILS
    // after a mesh was already current.
    //
    // P16-GEOM's own tests cover each refusal reason in isolation. What this
    // adds is the order: current, then an edit, then a FAILED rebuild, and the
    // requirement that at no point does the held mesh describe the model.
    MeshedReference model = referenceModel(MeshReferenceModelKind::Block);
    const ObjectId solid = model.body();

    const VolumeMesh& first = model.require();
    const std::size_t tets = first.tetrahedronCount();
    CHECK(model.currency() == MeshCurrency::Current);
    CHECK(meshing::describesTheModel(model.currency()));

    // 1. AN EDIT, WITHOUT REGENERATING. The geometry the document implies is
    //    no longer the geometry in hand, and both the mesh and the geometry
    //    boundary must say so.
    const std::optional<ObjectId> parameter = model.document().findByName("block_a");
    REQUIRE(parameter.has_value());
    const std::optional<ParameterId> a = model.document().asParameter(*parameter);
    REQUIRE(a.has_value());
    REQUIRE(model.document().setParameterValue(*a, 150.0 * units::mm).has_value());

    CHECK(model.currency() == MeshCurrency::StaleGeometry);
    CHECK_FALSE(meshing::describesTheModel(model.currency()));
    CHECK(meshing::isStale(model.document(), first));
    // Meshing is REFUSED outright rather than quietly using the body in hand.
    auto refusedWhileStale = model.generate();
    REQUIRE_FALSE(refusedWhileStale.has_value());
    CHECK(meshing::geometryIneligibility(model.document(), model.regenerator(), solid) ==
          meshing::GeometryIneligibility::GeometryStale);
    // The old mesh is still HELD -- stale is old, not deleted -- and still not
    // current.
    CHECK(model.mesher().mesh(model.control()) != nullptr);
    CHECK(model.currency() == MeshCurrency::GenerationFailed);

    // 2. A REGENERATION THAT FAILS. A zero depth cannot be extruded, so the
    //    body is not rebuilt and there is no last-known-good geometry to be
    //    tempted by.
    const std::optional<ObjectId> depth = model.document().findByName("block_c");
    REQUIRE(depth.has_value());
    const std::optional<ParameterId> c = model.document().asParameter(*depth);
    REQUIRE(c.has_value());
    REQUIRE(model.document().setParameterValue(*c, 0.0 * units::mm).has_value());
    const features::RegenerationReport failed = model.regenerate();
    CHECK_FALSE(failed.succeeded());

    const std::optional<meshing::GeometryIneligibility> reason =
        meshing::geometryIneligibility(model.document(), model.regenerator(), solid);
    REQUIRE(reason.has_value());
    INFO("ineligibility after the failed rebuild: " << meshing::toString(*reason));
    // Failed or blocked -- either is a refusal, and neither is a current mesh.
    CHECK((*reason == meshing::GeometryIneligibility::RegenerationFailed ||
           *reason == meshing::GeometryIneligibility::RegenerationBlocked));
    auto refusedAfterFailure = model.generate();
    REQUIRE_FALSE(refusedAfterFailure.has_value());
    CHECK_FALSE(meshing::describesTheModel(model.currency()));

    // 3. AND A GOOD VALUE RECOVERS, so the refusals above are the contract and
    //    not a dead end.
    REQUIRE(model.document().setParameterValue(*c, 35.0 * units::mm).has_value());
    CHECK(model.regenerate().succeeded());
    const VolumeMesh& recovered = model.require();
    CHECK(model.currency() == MeshCurrency::Current);
    const double expected = analytic::blockVolumeMm3(150.0, 70.0, 35.0);
    CHECK_THAT(recovered.tetrahedralVolume().in(units::mm3), WithinRel(expected, 1e-9));
    WARN(std::format("RM-MESH-01 stale sequence: {} tets at 120 mm, refused while stale, refused "
                     "after a failed rebuild, {} tets at 150 mm ({:.17g} mm^3)",
                     tets, recovered.tetrahedronCount(),
                     recovered.tetrahedralVolume().in(units::mm3)));
}

TEST_CASE("P16Qualification_EveryReferenceDocumentRoundTripsItsIntentAndRegenerates",
          "[p16qual][persist]") {
    // BREADTH, WHICH IS THIS GATE'S WHOLE REASON FOR EXISTING.
    //
    // P16-REFMOD-001 round-trips THREE models, chosen deliberately -- the
    // block, the plate whose boundary intent is a hole wall, and the local
    // refinement model that carries global sizing, local sizing and a
    // GeometryReference together. That is the right depth for that milestone's
    // brief, which named those three.
    //
    // The FINAL reference matrix has a save/load column for every model, so it
    // needs every model. This is the breadth, and it is here rather than in
    // P16-REFMOD-001's suite so that milestone's evidence keeps saying what it
    // did.
    TempDir directory;
    std::size_t roundTripped = 0;
    std::size_t regenerated = 0;

    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        INFO(info.name);
        MeshedReference before = referenceModel(info.kind);
        const meshing::MeshControlDefinition intent = before.definition()->definition();

        const std::filesystem::path path =
            directory.path() / std::format("{}.bcad", info.fileStem);
        const std::string bytes = savedBytes(before.document(), path);
        // No mesh in the file, for every model and not only the sampled three.
        CHECK(bytes.find("\"nodes\"") == std::string::npos);
        CHECK(bytes.find("\"tetrahedra\"") == std::string::npos);
        CHECK(bytes.find("\"elements\"") == std::string::npos);

        auto loaded = io::loadDocument(path);
        REQUIRE(loaded.has_value());
        MeshedReference after(std::move(*loaded));

        // THE CANONICAL FINGERPRINT, which is the whole definition: the body,
        // the discretisation, the sizing, the quality policy and the boundary
        // sets with their identities.
        CHECK(after.definition()->definition() == intent);
        ++roundTripped;
        // Nothing was restored as a mesh.
        CHECK(after.mesher().heldMeshCount() == 0);
        CHECK(after.currency() == MeshCurrency::NoMesh);

        auto generated = after.generate();
        CHECK(generated.has_value() == info.expectMesh);
        if (!info.expectMesh) {
            // RM-MESH-08 still fails after a save and a load, which is what
            // makes its refusal a property of the document rather than of the
            // process that built it.
            CHECK(after.mesher().heldMeshCount() == 0);
            continue;
        }
        // And the regenerated mesh is the one the original process produced.
        const VolumeMesh& originalMesh = before.require();
        CHECK((*generated)->nodeCount() == originalMesh.nodeCount());
        CHECK((*generated)->tetrahedronCount() == originalMesh.tetrahedronCount());
        CHECK((*generated)->tetrahedralVolume().si() == originalMesh.tetrahedralVolume().si());
        ++regenerated;
    }

    CHECK(roundTripped == bettercad::reference::kMeshReferenceModels.size());
    CHECK(roundTripped == 9);
    CHECK(regenerated == 8);
    WARN(std::format("all {} reference documents round-tripped their canonical intent; {} "
                     "regenerated an identical mesh; RM-MESH-08 still refused after a load",
                     roundTripped, regenerated));
}

TEST_CASE("P16Qualification_AControlWhoseBodyIsDeletedIsUnresolvedAndMeshesNothing",
          "[p16qual][stale]") {
    // A FINDING LEFT "STILL OWED FROM EARLIER REVIEWS", closed here.
    //
    // P16-ARCH-001's review F6 required that a MeshControl whose body is
    // deleted "must become explicitly UNRESOLVED and must not mesh nothing and
    // report success". Both halves were satisfied by COMPOSITION --
    // requireMeshableGeometry returns ObjectNotFound, and
    // VolumeMesh_OfADeletedFeatureIsStale covers a deleted feature's mesh --
    // but no test put the control, the deleted body and the mesher in one
    // sequence. At a final phase gate, a requirement met by reasoning is not
    // met.
    MeshedReference model = referenceModel(MeshReferenceModelKind::Block);
    const ObjectId body = model.body();

    // A mesh exists and is current, so what follows is a transition and not an
    // empty starting state.
    const VolumeMesh& before = model.require();
    CHECK(before.tetrahedronCount() > 0);
    CHECK(model.currency() == MeshCurrency::Current);
    CHECK_FALSE(meshing::isStale(model.document(), before));

    // DELETE THE BODY, leaving the control behind. The control is still a
    // document object and still names the body it can no longer find.
    REQUIRE(model.document().removeObject(body).has_value());
    REQUIRE(model.document().findObject(body) == nullptr);
    REQUIRE(model.definition() != nullptr);
    CHECK(model.definition()->definition().body == body);

    // 1. EXPLICITLY UNRESOLVED, with a name rather than a silence.
    const std::optional<meshing::GeometryIneligibility> reason =
        meshing::geometryIneligibility(model.document(), model.regenerator(), body);
    REQUIRE(reason.has_value());
    INFO("ineligibility: " << meshing::toString(*reason));
    CHECK(*reason == meshing::GeometryIneligibility::ObjectNotFound);

    // 2. IT MESHES NOTHING, AND DOES NOT REPORT SUCCESS.
    auto refused = model.generate();
    REQUIRE_FALSE(refused.has_value());
    CHECK_FALSE(refused.error().message.empty());
    CHECK_FALSE(meshing::describesTheModel(model.currency()));

    // 3. AND THE MESH IT ALREADY HELD IS STALE -- a mesh of a body that no
    //    longer exists is as stale as a mesh can be. The source comes from the
    //    MESH, so this cannot be asked about the wrong body.
    CHECK(meshing::isStale(model.document(), before));

    // 4. The direct path refuses identically, so the refusal belongs to the
    //    geometry boundary and not to the service.
    auto direct = meshing::volumeMeshFor(model.document(), model.regenerator(), body);
    REQUIRE_FALSE(direct.has_value());

    WARN(std::format("a control whose body was deleted: ineligibility {}, generation refused, "
                     "the {} tetrahedra it held are stale",
                     meshing::toString(*reason), before.tetrahedronCount()));
}

#endif // BETTERCAD_TESTS_EXPECT_NETGEN
