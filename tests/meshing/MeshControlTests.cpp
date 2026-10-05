// P16-CMD-001: the canonical meshing intent as a document object.
//
// THE FIRST HALF OF THIS FILE IS A PROBE, and it is a test rather than a
// throwaway because the thing it establishes is the premise of the whole
// milestone: ADR-030 says undo, redo, persistence and dependency invalidation
// "come for free, because a control is an ordinary document object". That is a
// claim about the generic machinery, and the material precedent that ADR-030
// points to says it "was verified by probe before any of this was written, not
// assumed". So it is verified here, against a real MeshControl, before a
// single meshing command exists.
//
// The second half tests the object's own contract: it holds only intent, it
// refuses an invalid definition by delegating to the validators that own the
// rules, it reports a no-op edit as no change, and it enumerates
// deterministically.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshingCommands.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using meshing::LocalMeshSizing;
using meshing::MeshControl;
using meshing::MeshControlDefinition;
using meshing::NamedBoundarySet;

namespace {

/// A document with one extruded block, so a control has a real body to name.
struct Part {
    Document document{"Part"};
    features::Regenerator regenerator;
    ObjectId feature{};
    std::array<EntityId, 4> lines{};

    Part() {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, 30_mm, 20_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 10_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));
        requireReport(regenerator, document);
    }

    [[nodiscard]] FaceName top() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines[which]}};
    }

    [[nodiscard]] MeshControlDefinition definition() const {
        return MeshControlDefinition{.body = feature};
    }
};

template <typename CommandType, typename... Args>
CommandType* run(CommandHistory& history, Document& document, Args&&... args) {
    auto command = std::make_unique<CommandType>(std::forward<Args>(args)...);
    CommandType* raw = command.get();
    const Result<void> result = history.execute(document, std::move(command));
    INFO((result ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
    return raw;
}

void requireUndo(CommandHistory& history, Document& document) {
    const Result<void> result = history.undo(document);
    INFO((result ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
}

void requireRedo(CommandHistory& history, Document& document) {
    const Result<void> result = history.redo(document);
    INFO((result ? std::string{} : result.error().message));
    REQUIRE(result.has_value());
}

[[nodiscard]] const MeshControl* controlIn(const Document& document, ObjectId id) {
    for (const DocumentObject& object : document.objects()) {
        if (object.id() == id) {
            return dynamic_cast<const MeshControl*>(&object);
        }
    }
    return nullptr;
}

[[nodiscard]] std::size_t controlCount(const Document& document) {
    std::size_t count = 0U;
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const MeshControl*>(&object) != nullptr) {
            ++count;
        }
    }
    return count;
}

} // namespace

// ---------------------------------------------------------------------------
// The probe: what the generic machinery already does for a MeshControl
// ---------------------------------------------------------------------------

TEST_CASE("MeshControlProbe_TheGenericAddCommandUndoesAndRedoesWithTheSameId",
          "[meshing][meshcontrol][probe]") {
    // ADR-030's claim, tested rather than trusted. If this fails, the
    // milestone needs its own create/delete commands; if it passes, writing
    // them would be reimplementing working machinery, and the material
    // precedent wraps instead.
    Part part;
    CommandHistory history;
    CHECK(controlCount(part.document) == 0U);

    auto control = MeshControl::create("Mesh", part.definition());
    REQUIRE(control.has_value());
    AddObjectCommand* add = run<AddObjectCommand>(history, part.document, std::move(*control));
    const ObjectId id = add->objectId();
    REQUIRE(id.isValid());
    REQUIRE(controlCount(part.document) == 1U);
    const MeshControlDefinition s1 = controlIn(part.document, id)->definition();

    requireUndo(history, part.document);
    CHECK(controlCount(part.document) == 0U);
    CHECK(controlIn(part.document, id) == nullptr);

    requireRedo(history, part.document);
    REQUIRE(controlCount(part.document) == 1U);
    const MeshControl* restored = controlIn(part.document, id);
    REQUIRE(restored != nullptr);
    // THE SAME ID, which is what makes a control referenceable: a redo that
    // minted a new one would leave every reference to the original dangling.
    CHECK(restored->id() == id);
    CHECK(restored->meshControlId() == MeshControlId::fromValue(id.value()));
    CHECK(restored->definition() == s1);
}

TEST_CASE("MeshControlProbe_TheGenericDeleteCommandRestoresTheWholeControl",
          "[meshing][meshcontrol][probe]") {
    Part part;
    CommandHistory history;
    auto control = MeshControl::create("Mesh", part.definition());
    REQUIRE(control.has_value());
    const ObjectId id =
        run<AddObjectCommand>(history, part.document, std::move(*control))->objectId();
    const MeshControlDefinition before = controlIn(part.document, id)->definition();

    (void)run<DeleteObjectCommand>(history, part.document, id);
    CHECK(controlIn(part.document, id) == nullptr);

    requireUndo(history, part.document);
    const MeshControl* restored = controlIn(part.document, id);
    REQUIRE(restored != nullptr);
    CHECK(restored->id() == id);
    CHECK(restored->name() == "Mesh");
    CHECK(restored->definition() == before);
}

TEST_CASE("MeshControlProbe_ModifyObjectBumpsTheRevisionOnlyOnAnEffectiveChange",
          "[meshing][meshcontrol][probe]") {
    // The no-op contract this milestone needs, and it is already implemented:
    // Document::modifyObject bumps the object and document revisions only when
    // the mutation returns true.
    Part part;
    auto control = MeshControl::create("Mesh", part.definition());
    REQUIRE(control.has_value());
    const ObjectId id = require(part.document.addObject(std::move(*control)));
    const std::uint64_t first = part.document.revisionOf(id).value();

    // An effective change.
    MeshControlDefinition finer = part.definition();
    finer.mesh.sizing.globalTargetSize = 3_mm;
    const Result<bool> changed = part.document.modifyObject<MeshControl>(
        id, [&finer](MeshControl& object) { return object.setDefinition(finer); });
    REQUIRE(changed.has_value());
    CHECK(*changed);
    const std::uint64_t second = part.document.revisionOf(id).value();
    CHECK(second > first);

    // The same value again: no change, no revision bump.
    const Result<bool> again = part.document.modifyObject<MeshControl>(
        id, [&finer](MeshControl& object) { return object.setDefinition(finer); });
    REQUIRE(again.has_value());
    CHECK_FALSE(*again);
    CHECK(part.document.revisionOf(id).value() == second);

    // And a wrong type is refused by the document, naming what the object is.
    const Result<bool> wrongType = part.document.modifyObject<features::ExtrudeFeature>(
        id, [](features::ExtrudeFeature&) -> Result<bool> { return true; });
    REQUIRE_FALSE(wrongType.has_value());
    CHECK(wrongType.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(wrongType.error().message, ContainsSubstring("mesh-control"));
}

// ---------------------------------------------------------------------------
// The object's own contract
// ---------------------------------------------------------------------------

TEST_CASE("MeshControl_DependsOnTheBodyItMeshes", "[meshing][meshcontrol]") {
    // THE EDGE THAT MAKES INVALIDATION POSSIBLE. ADR-030: changing a body must
    // be able to invalidate a derived mesh, "and only a graph node can express
    // that". Without this dependency the document's graph could not know that
    // editing the solid reaches the control.
    Part part;
    auto control = MeshControl::create("Mesh", part.definition());
    REQUIRE(control.has_value());
    const std::vector<ObjectId> dependencies = (*control)->dependencies();
    REQUIRE(dependencies.size() == 1U);
    CHECK(dependencies.front() == part.feature);
}

TEST_CASE("MeshControl_RefusesADefinitionTheOwningValidatorsReject",
          "[meshing][meshcontrol][validation]") {
    // DELEGATED, NOT RESTATED. Each refusal below is P16-SIZE-001's or
    // P16-QUALITY-001's rule, reached through their own validators, so the
    // command layer cannot drift from them.
    Part part;

    CHECK(errorCode(MeshControl::create("Mesh", MeshControlDefinition{})) ==
          ErrorCode::InvalidArgument);

    MeshControlDefinition zero = part.definition();
    zero.mesh.sizing.globalTargetSize = Length::fromSi(0.0);
    CHECK(errorCode(MeshControl::create("Mesh", zero)) == ErrorCode::InvalidArgument);

    MeshControlDefinition negative = part.definition();
    negative.mesh.sizing.globalTargetSize = Length::fromSi(-1e-3);
    CHECK(errorCode(MeshControl::create("Mesh", negative)) == ErrorCode::InvalidArgument);

    MeshControlDefinition notFinite = part.definition();
    notFinite.mesh.sizing.globalTargetSize =
        Length::fromSi(std::numeric_limits<double>::quiet_NaN());
    CHECK(errorCode(MeshControl::create("Mesh", notFinite)) == ErrorCode::InvalidArgument);

    // TWO CONTROLS ON ONE FACE is P16-SIZE-001's DuplicateFaceControl, and it
    // is the rule that makes a FaceName an identity.
    MeshControlDefinition duplicated = part.definition();
    duplicated.mesh.sizing.local.push_back(LocalMeshSizing{.face = part.top(), .targetSize = 2_mm});
    duplicated.mesh.sizing.local.push_back(LocalMeshSizing{.face = part.top(), .targetSize = 1_mm});
    const Result<std::unique_ptr<MeshControl>> twoOnOneFace =
        MeshControl::create("Mesh", duplicated);
    REQUIRE_FALSE(twoOnOneFace.has_value());
    CHECK(twoOnOneFace.error().code == ErrorCode::InvalidArgument);

    // Two boundary sets sharing an identity.
    MeshControlDefinition sameSetId = part.definition();
    sameSetId.boundarySets.push_back(NamedBoundarySet{
        .id = BoundarySetId::fromValue(1U), .name = "fixed", .faces = {part.top()}});
    sameSetId.boundarySets.push_back(NamedBoundarySet{
        .id = BoundarySetId::fromValue(1U), .name = "loaded", .faces = {part.side(0)}});
    CHECK(errorCode(MeshControl::create("Mesh", sameSetId)) == ErrorCode::AlreadyExists);
}

TEST_CASE("MeshControl_HoldsOnlyIntentAndComparesByIt", "[meshing][meshcontrol]") {
    Part part;
    MeshControlDefinition definition = part.definition();
    definition.mesh.sizing.globalTargetSize = 4_mm;
    definition.mesh.sizing.local.push_back(
        LocalMeshSizing{.face = part.top(), .targetSize = 1.5_mm});
    definition.boundarySets.push_back(NamedBoundarySet{
        .id = BoundarySetId::fromValue(7U), .name = "fixed_end", .faces = {part.side(0)}});

    auto control = MeshControl::create("Mesh", definition);
    REQUIRE(control.has_value());
    CHECK((*control)->typeName() == "mesh-control");
    CHECK((*control)->definition() == definition);

    // A clone is content-equal: that is what undo restores.
    const std::unique_ptr<DocumentObject> copy = (*control)->clone();
    REQUIRE(copy != nullptr);
    CHECK((*control)->contentEquals(*copy));
    CHECK(copy->contentEquals(**control));

    // And a different intent is not equal. The comparison is of the intent,
    // field by field, because there is nothing else in the object to compare.
    MeshControlDefinition other = definition;
    other.mesh.sizing.globalTargetSize = 5_mm;
    auto different = MeshControl::create("Mesh", other);
    REQUIRE(different.has_value());
    CHECK_FALSE((*control)->contentEquals(**different));

    // A control is not content-equal to another kind of object.
    CHECK_FALSE((*control)->contentEquals(*part.document.objects().begin()));
}

TEST_CASE("MeshControl_EnumeratesItsControlsDeterministically", "[meshing][meshcontrol]") {
    // The stored order carries no meaning (P16-SIZE-001), so an enumeration
    // that exposed it would make a panel's or a file's output depend on the
    // order of past edits. Ascending FaceName, and ascending BoundarySetId.
    Part part;
    MeshControlDefinition definition = part.definition();
    definition.mesh.sizing.local.push_back(LocalMeshSizing{.face = part.side(2), .targetSize = 3_mm});
    definition.mesh.sizing.local.push_back(LocalMeshSizing{.face = part.top(), .targetSize = 1_mm});
    definition.mesh.sizing.local.push_back(LocalMeshSizing{.face = part.side(0), .targetSize = 2_mm});
    definition.boundarySets.push_back(NamedBoundarySet{
        .id = BoundarySetId::fromValue(9U), .name = "b", .faces = {part.top()}});
    definition.boundarySets.push_back(NamedBoundarySet{
        .id = BoundarySetId::fromValue(2U), .name = "a", .faces = {part.side(1)}});

    auto control = MeshControl::create("Mesh", definition);
    REQUIRE(control.has_value());

    const std::vector<LocalMeshSizing> ordered = (*control)->orderedLocalSizing();
    REQUIRE(ordered.size() == 3U);
    CHECK(std::ranges::is_sorted(ordered, [](const LocalMeshSizing& a, const LocalMeshSizing& b) {
        return a.face < b.face;
    }));

    const std::vector<NamedBoundarySet> sets = (*control)->orderedBoundarySets();
    REQUIRE(sets.size() == 2U);
    CHECK(sets.front().id.value() == 2U);
    CHECK(sets.back().id.value() == 9U);

    // And a face names at most one control, which is what makes it a key.
    const LocalMeshSizing* top = (*control)->localSizing(part.top());
    REQUIRE(top != nullptr);
    CHECK(top->targetSize == 1_mm);
    CHECK((*control)->localSizing(part.side(3)) == nullptr);
    CHECK((*control)->boundarySet(BoundarySetId::fromValue(2U)) != nullptr);
    CHECK((*control)->boundarySet(BoundarySetId::fromValue(3U)) == nullptr);
}

TEST_CASE("MeshControl_NowSavesAndLoads", "[meshing][meshcontrol][persistence]") {
    // THE BOUNDARY WITH P16-PERSIST-001 HAS MOVED, ON PURPOSE AND VISIBLY.
    //
    // This test used to be MeshControl_CannotYetBeSavedAndSaysSo. It asserted
    // that io's objectToJson refused a control by name, because the schema was
    // another milestone's decision, and it said in its own comment: "When
    // P16-PERSIST-001 lands, this test fails and is replaced by its round
    // trip."
    //
    // P16-PERSIST-001 landed, this test failed, and this is the replacement.
    // The gap was closed where it was marked rather than being discovered by
    // someone wondering why a save had started working.
    //
    // The round trip is covered in detail by tests/io/MeshControlFileTests.cpp
    // -- the schema, the units, determinism, malformed input, regeneration
    // after load. What belongs HERE is only that a control reaches the file at
    // all, which is the fact this test was created to track.
    Part part;
    auto control = MeshControl::create("Mesh", part.definition());
    REQUIRE(control.has_value());
    (void)require(part.document.addObject(std::move(*control)));

    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "bettercad-meshcontrol-save.bcad";
    std::filesystem::remove(file);
    const Result<void> saved = io::saveDocument(part.document, file);
    INFO((saved ? std::string{} : saved.error().message));
    REQUIRE(saved.has_value());

    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    const std::vector<MeshControlId> controls = meshing::meshControls(*loaded);
    REQUIRE(controls.size() == 1U);
    const MeshControl* restored = meshing::findMeshControl(*loaded, controls.front());
    REQUIRE(restored != nullptr);
    CHECK(restored->name() == "Mesh");
    CHECK(restored->definition() == part.definition());
    std::filesystem::remove(file);
}
