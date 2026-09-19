#include "features/FeatureTestSupport.hpp"
#include "support/BracketModel.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <set>
#include <memory>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;

// P13-COMP-001: component definitions and instances, implementing ADR-002
// (components are document objects) and ADR-003 (their part is in the same
// document). Everything here goes through the public API.

namespace {

/// A document holding one part: a 40 x 30 x 10 mm block. Returns the
/// extrude, which is the part a component places.
struct PartDocument {
    Document document{"Parts"};
    ObjectId sketch{};
    ObjectId part{};
};

PartDocument makePart(std::string partName = "Block") {
    PartDocument p;
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    p.sketch = require(p.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        std::move(partName), {.profile = SketchId::fromValue(p.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    p.part = require(p.document.addObject(std::move(*extrude)));
    return p;
}

ComponentId addComponent(Document& document, std::string name, ObjectId part) {
    auto id = assembly::createComponent(document, std::move(name), {.part = part});
    if (!id) {
        FAIL(id.error().message);
    }
    return *id;
}

} // namespace

TEST_CASE("Component_IsADocumentObjectWithItsOwnIdentity", "[assembly][component][p13]") {
    // ADR-002: a component is an ordinary document object, so it gets an ID
    // from the document's one allocator and appears in the object list.
    PartDocument p = makePart();
    const auto before = p.document.objectCount();

    const ComponentId id = addComponent(p.document, "Block1", p.part);

    CHECK(id.isValid());
    CHECK(p.document.objectCount() == before + 1);
    const assembly::Component* component = assembly::findComponent(p.document, id);
    REQUIRE(component != nullptr);
    CHECK(component->typeName() == "component");
    CHECK(component->name() == "Block1");
    CHECK(component->definition().part == p.part);
    CHECK_FALSE(component->definition().suppressed);
    // The narrowed ID and the object ID are the same number, and the
    // component is reachable as a plain object too.
    CHECK(component->componentId() == id);
    CHECK(p.document.findObject(id) != nullptr);
    CHECK(p.document.contains(id));
}

TEST_CASE("Component_IdentifiesTheInstanceNotThePart", "[assembly][component][p13]") {
    // The core assembly invariant: one part definition, several identities.
    PartDocument p = makePart();
    const ComponentId a = addComponent(p.document, "Block1", p.part);
    const ComponentId b = addComponent(p.document, "Block2", p.part);
    const ComponentId c = addComponent(p.document, "Block3", p.part);

    // Three different instance identities...
    CHECK(a != b);
    CHECK(b != c);
    CHECK(a != c);
    // ...all naming the one part.
    for (const ComponentId id : {a, b, c}) {
        const assembly::Component* component = assembly::findComponent(p.document, id);
        REQUIRE(component != nullptr);
        CHECK(component->definition().part == p.part);
    }
    // And the part itself was not duplicated: the document holds one sketch,
    // one extrude and three components.
    CHECK(p.document.objectCount() == 5);
    const auto all = assembly::components(p.document);
    CHECK(all == std::vector<ComponentId>{a, b, c});
    CHECK(assembly::componentsOf(p.document, p.part) == all);
}

TEST_CASE("Component_DependsOnThePartItPlaces", "[assembly][component][p13]") {
    // ADR-003: one edge, into the existing graph, so the existing dirty
    // propagation and blocking apply with no new machinery.
    PartDocument p = makePart();
    const ComponentId id = addComponent(p.document, "Block1", p.part);

    const assembly::Component* component = assembly::findComponent(p.document, id);
    REQUIRE(component != nullptr);
    CHECK(component->dependencies() == std::vector<ObjectId>{p.part});

    // And the document's graph agrees: the component is a dependent of the
    // part, with nothing missing.
    const DocumentGraph graph = buildDependencyGraph(p.document);
    CHECK(graph.missing.empty());
    CHECK(graph.graph.dependenciesOf(id) == std::set<ObjectId>{p.part});
    CHECK(graph.graph.dependentsOf(p.part).contains(ObjectId{id}));
}

TEST_CASE("Component_RefusesWhatItCannotPlace", "[assembly][component][p13]") {
    PartDocument p = makePart();

    SECTION("an object ID this document does not have") {
        auto id = assembly::createComponent(p.document, "Ghost", {.part = ObjectId::fromValue(9999)});
        REQUIRE_FALSE(id.has_value());
        CHECK(id.error().code == ErrorCode::NotFound);
        CHECK_THAT(id.error().message, ContainsSubstring("not an object of this document"));
    }
    SECTION("an object of a kind that produces no body") {
        // A sketch is not a part.
        auto id = assembly::createComponent(p.document, "OnASketch", {.part = p.sketch});
        REQUIRE_FALSE(id.has_value());
        CHECK(id.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(id.error().message, ContainsSubstring("produces no body"));
    }
    SECTION("a datum, which is also not a part") {
        auto datum = features::DatumPlane::create("Plane", {.kind = features::DatumPlaneKind::Offset,
                                                            .base = PlaneReference{.plane = PrincipalPlane::XY},
                                                            .offset = 5_mm});
        REQUIRE(datum.has_value());
        const ObjectId plane = require(p.document.addObject(std::move(*datum)));
        auto id = assembly::createComponent(p.document, "OnADatum", {.part = plane});
        REQUIRE_FALSE(id.has_value());
        CHECK(id.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("no part at all") {
        auto id = assembly::createComponent(p.document, "Nothing", {.part = ObjectId{}});
        REQUIRE_FALSE(id.has_value());
        CHECK(id.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(id.error().message, ContainsSubstring("must name the part"));
    }
    SECTION("a component of a component") {
        const ComponentId first = addComponent(p.document, "Block1", p.part);
        auto id = assembly::createComponent(p.document, "Nested", {.part = ObjectId{first}});
        REQUIRE_FALSE(id.has_value());
        CHECK(id.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("a name already taken") {
        addComponent(p.document, "Block1", p.part);
        auto id = assembly::createComponent(p.document, "Block1", {.part = p.part});
        REQUIRE_FALSE(id.has_value());
    }
}

TEST_CASE("Component_FailedCreationChangesNothing", "[assembly][component][p13]") {
    // Atomicity: a refused component leaves no object, consumes no ID, and
    // leaves the document usable.
    PartDocument p = makePart();
    const auto objectsBefore = p.document.objectCount();
    const auto idBefore = p.document.lastAllocatedId();
    const auto revisionBefore = p.document.revision();

    for (int attempt = 0; attempt < 3; ++attempt) {
        CHECK_FALSE(assembly::createComponent(p.document, "Bad", {.part = p.sketch}).has_value());
    }

    CHECK(p.document.objectCount() == objectsBefore);
    // No ID was consumed by the failures: the next component takes the very
    // next ID the allocator would have given.
    CHECK(p.document.lastAllocatedId() == idBefore);
    CHECK(p.document.revision() == revisionBefore);

    // And the document still works.
    const ComponentId good = addComponent(p.document, "Block1", p.part);
    CHECK(good.value() == idBefore + 1);
    CHECK(assembly::findComponent(p.document, good) != nullptr);
}

TEST_CASE("Component_IsRemovedWithoutTouchingItsPart", "[assembly][component][p13]") {
    PartDocument p = makePart();
    const ComponentId a = addComponent(p.document, "Block1", p.part);
    const ComponentId b = addComponent(p.document, "Block2", p.part);

    REQUIRE(assembly::removeComponent(p.document, a).has_value());

    CHECK(assembly::findComponent(p.document, a) == nullptr);
    CHECK(p.document.findObject(a) == nullptr);
    CHECK_FALSE(p.document.contains(a));
    // The part and the other component are untouched.
    CHECK(p.document.findObject(p.part) != nullptr);
    CHECK(assembly::findComponent(p.document, b) != nullptr);
    CHECK(assembly::components(p.document) == std::vector<ComponentId>{b});

    // Removing it again fails cleanly rather than doing anything.
    auto again = assembly::removeComponent(p.document, a);
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().code == ErrorCode::NotFound);

    // An ID is never reused: a new component gets a fresh one.
    const ComponentId c = addComponent(p.document, "Block3", p.part);
    CHECK(c != a);
    CHECK(c.value() > b.value());
}

TEST_CASE("Component_WhosePartIsDeletedFailsAndSaysSo", "[assembly][component][p13][regeneration]") {
    // ADR-003: a missing part is reported, never quietly resolved to another
    // body. The component needs no regeneration handler for this: the
    // dependency graph records the missing reference and the regenerator
    // fails the node.
    PartDocument p = makePart();
    addComponent(p.document, "Block1", p.part);
    features::Regenerator regenerator;
    REQUIRE(requireReport(regenerator, p.document).succeeded());

    REQUIRE(p.document.removeObject(p.part).has_value());

    const DocumentGraph graph = buildDependencyGraph(p.document);
    CHECK(graph.missing.size() == 1);
    const features::RegenerationReport report = requireReport(regenerator, p.document);
    CHECK_FALSE(report.succeeded());
    INFO(bettercad::test::describe(report));
    CHECK_THAT(bettercad::test::describe(report), ContainsSubstring("does not exist"));
}

TEST_CASE("Component_IsBlockedWhenItsPartFails", "[assembly][component][p13][regeneration]") {
    // The other half of graph participation: a part that fails blocks its
    // components rather than leaving them looking fine.
    PartDocument p = makePart();
    const ComponentId id = addComponent(p.document, "Block1", p.part);
    features::Regenerator regenerator;
    REQUIRE(requireReport(regenerator, p.document).succeeded());

    // Break the part by taking away the sketch it extrudes: the extrude then
    // has a missing reference and fails, which is upstream of the component.
    REQUIRE(p.document.removeObject(p.sketch).has_value());

    const features::RegenerationReport report = requireReport(regenerator, p.document);
    CHECK_FALSE(report.succeeded());
    CHECK(regenerator.state(id) == features::NodeState::Blocked);
}

TEST_CASE("Component_SuppressionIsIntentNotDeletion", "[assembly][component][p13]") {
    PartDocument p = makePart();
    const ComponentId id = addComponent(p.document, "Block1", p.part);

    REQUIRE(p.document
                .modifyObject<assembly::Component>(
                    id,
                    [&](assembly::Component& component) {
                        auto definition = component.definition();
                        definition.suppressed = true;
                        return component.setDefinition(definition).value_or(false);
                    })
                .has_value());

    const assembly::Component* component = assembly::findComponent(p.document, id);
    REQUIRE(component != nullptr);
    CHECK(component->definition().suppressed);
    // Still present, still named, still depending on its part.
    CHECK(component->definition().part == p.part);
    CHECK(component->dependencies() == std::vector<ObjectId>{p.part});
    CHECK(assembly::components(p.document) == std::vector<ComponentId>{id});
}

TEST_CASE("Component_SettingTheSameDefinitionChangesNothing", "[assembly][component][p13]") {
    PartDocument p = makePart();
    const ComponentId id = addComponent(p.document, "Block1", p.part);
    const auto revision = p.document.revision();

    REQUIRE(p.document
                .modifyObject<assembly::Component>(
                    id,
                    [&](assembly::Component& component) {
                        return component.setDefinition(component.definition()).value_or(true);
                    })
                .has_value());

    // No effective change, so no revision bump: the component follows the
    // same rule as every other object.
    CHECK(p.document.revision() == revision);
}

TEST_CASE("Component_UndoAndRedoRestoreTheSameIdentity", "[assembly][component][p13][undo]") {
    // ADR-002 claims components inherit undo/redo from AddObjectCommand.
    // The command contract requires redo to reproduce the same ID.
    PartDocument p = makePart();
    CommandHistory history;
    auto component = assembly::Component::create("Block1", {.part = p.part});
    REQUIRE(component.has_value());
    REQUIRE(history.execute(p.document, std::make_unique<AddObjectCommand>(std::move(*component))).has_value());

    const auto created = assembly::components(p.document);
    REQUIRE(created.size() == 1);
    const ComponentId id = created.front();

    REQUIRE(history.undo(p.document).has_value());
    CHECK(assembly::components(p.document).empty());
    CHECK(assembly::findComponent(p.document, id) == nullptr);

    REQUIRE(history.redo(p.document).has_value());
    CHECK(assembly::components(p.document) == std::vector<ComponentId>{id});
    const assembly::Component* back = assembly::findComponent(p.document, id);
    REQUIRE(back != nullptr);
    CHECK(back->definition().part == p.part);
}

TEST_CASE("Component_BuildingTheSameDocumentTwiceGivesTheSameIds", "[assembly][component][p13][determinism]") {
    // Determinism at the level this milestone introduces: IDs come from the
    // document's one allocator in creation order, so the same sequence of
    // public calls gives the same identities.
    const auto build = [] {
        PartDocument p = makePart();
        std::vector<ComponentId> ids;
        for (const char* name : {"Block1", "Block2", "Block3"}) {
            ids.push_back(addComponent(p.document, name, p.part));
        }
        return ids;
    };
    CHECK(build() == build());
}

TEST_CASE("Component_ForeignObjectIdsAreRejectedOnlyWhenTheyAreAbsent", "[assembly][component][p13]") {
    // The exact, honest contract for "foreign-document rejection", and its
    // limit. An ObjectId carries no document identity: it is a number from
    // its own document's allocator. So a foreign ID is one of two things,
    // and only one of them is detectable.
    PartDocument here = makePart();
    PartDocument there = makePart("FarBlock");

    SECTION("absent here: rejected") {
        // Give the other document more objects, so its part's ID is one
        // this document has never allocated.
        for (int i = 0; i < 5; ++i) {
            auto extra = std::make_unique<sketch::Sketch>(std::format("Extra{}", i), Frame3D::xy());
            REQUIRE(there.document.addObject(std::move(extra)).has_value());
        }
        auto second = features::ExtrudeFeature::create(
            "FarBlock2", {.profile = SketchId::fromValue(there.sketch.value()), .depth = 10_mm});
        REQUIRE(second.has_value());
        const ObjectId farPart = require(there.document.addObject(std::move(*second)));
        REQUIRE_FALSE(here.document.contains(farPart));

        auto id = assembly::createComponent(here.document, "Ghost", {.part = farPart});
        REQUIRE_FALSE(id.has_value());
        CHECK(id.error().code == ErrorCode::NotFound);
        CHECK_THAT(id.error().message, ContainsSubstring("not an object of this document"));
    }

    SECTION("colliding with a local ID: NOT detectable, and binds locally") {
        // Both documents allocate from 1, so the other document's part has
        // the same number as this one's. There is no information anywhere
        // that distinguishes them.
        REQUIRE(there.part.value() == here.part.value());

        auto id = assembly::createComponent(here.document, "Twin", {.part = there.part});
        REQUIRE(id.has_value());
        // What matters is that it bound to the LOCAL object of that ID, not
        // to anything in the other document: nothing crossed a boundary.
        const assembly::Component* component = assembly::findComponent(here.document, *id);
        REQUIRE(component != nullptr);
        CHECK(component->definition().part == here.part);
        CHECK(here.document.findObject(component->definition().part) != nullptr);
        CHECK(here.document.nameOf(component->definition().part) == "Block");
    }
}


TEST_CASE("Component_RepointingIsCheckedLikeCreation", "[assembly][component][p13]") {
    // The modify path runs the same document-level check as creation, so a
    // component cannot be quietly repointed at something that is not a part.
    PartDocument p = makePart();
    const ComponentId id = addComponent(p.document, "Block1", p.part);
    auto second = features::ExtrudeFeature::create(
        "Block2", {.profile = SketchId::fromValue(p.sketch.value()), .depth = 20_mm});
    REQUIRE(second.has_value());
    const ObjectId otherPart = require(p.document.addObject(std::move(*second)));

    SECTION("to another part: allowed") {
        auto changed = assembly::setComponentDefinition(p.document, id, {.part = otherPart});
        REQUIRE(changed.has_value());
        CHECK(*changed);
        CHECK(assembly::findComponent(p.document, id)->definition().part == otherPart);
        CHECK(assembly::findComponent(p.document, id)->dependencies() == std::vector<ObjectId>{otherPart});
    }
    SECTION("to a sketch: refused") {
        auto changed = assembly::setComponentDefinition(p.document, id, {.part = p.sketch});
        REQUIRE_FALSE(changed.has_value());
        CHECK(changed.error().code == ErrorCode::InvalidArgument);
        // And nothing moved.
        CHECK(assembly::findComponent(p.document, id)->definition().part == p.part);
    }
    SECTION("to itself: refused, with a message that says why") {
        auto changed = assembly::setComponentDefinition(p.document, id, {.part = ObjectId{id}});
        REQUIRE_FALSE(changed.has_value());
        CHECK_THAT(changed.error().message, ContainsSubstring("cannot place itself"));
        CHECK(assembly::findComponent(p.document, id)->definition().part == p.part);
    }
    SECTION("suppression through the same path") {
        auto changed = assembly::setComponentDefinition(p.document, id, {.part = p.part, .suppressed = true});
        REQUIRE(changed.has_value());
        CHECK(*changed);
        CHECK(assembly::findComponent(p.document, id)->definition().suppressed);
        // Setting the same thing again changes nothing.
        auto again = assembly::setComponentDefinition(p.document, id, {.part = p.part, .suppressed = true});
        REQUIRE(again.has_value());
        CHECK_FALSE(*again);
    }
    SECTION("a component that does not exist") {
        auto changed = assembly::setComponentDefinition(p.document, ComponentId::fromValue(9999),
                                                        {.part = p.part});
        REQUIRE_FALSE(changed.has_value());
        CHECK(changed.error().code == ErrorCode::NotFound);
    }
}

TEST_CASE("Component_SelfReferenceIsCaughtAsACycle", "[assembly][component][p13][regeneration]") {
    // Belt and braces. setComponentDefinition refuses self-reference with a
    // clear message, but if one is reached past it -- through
    // Document::modifyObject, which sees only the definition's own
    // validation -- the dependency graph still catches it.
    PartDocument p = makePart();
    const ComponentId id = addComponent(p.document, "Block1", p.part);
    features::Regenerator regenerator;
    REQUIRE(requireReport(regenerator, p.document).succeeded());

    REQUIRE(p.document
                .modifyObject<assembly::Component>(
                    id,
                    [&](assembly::Component& c) {
                        return c.setDefinition({.part = ObjectId{id}}).value_or(false);
                    })
                .has_value());

    const features::RegenerationReport report = requireReport(regenerator, p.document);
    CHECK_FALSE(report.succeeded());
    CHECK_THAT(bettercad::test::describe(report), ContainsSubstring("dependency cycle"));
}
