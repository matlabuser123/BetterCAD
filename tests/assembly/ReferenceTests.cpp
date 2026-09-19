#include "features/FeatureTestSupport.hpp"

#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/Uuid.hpp>
#include <bettercad/core/document/DependencyGraph.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/ObjectReference.hpp>
#include <bettercad/core/document/ReferenceResolver.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;

// P13-REF-001: reference identity and resolution, implementing the external
// reference contract ADR-003 recorded.
//
// The invariant the milestone exists to establish: an ObjectId is local
// identity and nothing else. A reference that names an object in another
// document carries that document's UUID, and a number that happens to match
// a local object is never good enough.

namespace {

struct PartDocument {
    Document document;
    ObjectId sketch{};
    ObjectId part{};

    explicit PartDocument(std::string name) : document(std::move(name)) {}
};

/// A document holding one part, named @p partName.
PartDocument makePart(std::string documentName = "Parts", std::string partName = "Block") {
    PartDocument p{std::move(documentName)};
    auto sketch = std::make_unique<sketch::Sketch>("BlockSketch", Frame3D::xy());
    addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    p.sketch = require(p.document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        std::move(partName), {.profile = SketchId::fromValue(p.sketch.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    p.part = require(p.document.addObject(std::move(*extrude)));
    return p;
}

/// A resolver that knows a fixed set of open documents, and nothing else --
/// no filesystem, no globals. That is the point of the interface: the tests
/// supply their own source of documents exactly as the CLI or a GUI would.
class OpenDocuments final : public ReferenceResolver {
public:
    void add(const Document& document) { documents_.push_back(&document); }
    void clear() noexcept { documents_.clear(); }

    [[nodiscard]] const Document* candidate(const ObjectReference& reference) const override {
        if (!reference.document) {
            return nullptr;
        }
        for (const Document* document : documents_) {
            if (document->id() == *reference.document) {
                return document;
            }
        }
        return nullptr;
    }

private:
    std::vector<const Document*> documents_;
};

/// A resolver that offers one document whatever it is asked for: a stand-in
/// for a locator that now points at a different file. It exists to prove the
/// caller checks identity rather than trusting what it is handed.
class AlwaysOffers final : public ReferenceResolver {
public:
    explicit AlwaysOffers(const Document& document) noexcept : document_(&document) {}
    [[nodiscard]] const Document* candidate(const ObjectReference&) const override { return document_; }

private:
    const Document* document_;
};

} // namespace

TEST_CASE("Reference_DistinguishesLocalIdentityFromDocumentIdentity", "[assembly][objectref][p13]") {
    PartDocument p = makePart();
    const DocumentId other = DocumentId::fromValue(Uuid::generateV4());

    const ObjectReference internal{p.part};
    const ObjectReference external{other, p.part};

    CHECK(isInternal(internal));
    CHECK_FALSE(isInternal(external));
    CHECK(localTarget(internal) == std::optional<ObjectId>{p.part});
    // The one that matters: an external reference offers the graph nothing,
    // because an ObjectId cannot name a node in another document.
    CHECK(localTarget(external) == std::nullopt);
    // Same number, different meaning.
    CHECK(internal.object == external.object);
    CHECK_FALSE(sameTarget(internal, external));
}

TEST_CASE("Reference_IdentityIsTheDocumentAndObject_NeverTheLocator", "[assembly][objectref][p13]") {
    const DocumentId document = DocumentId::fromValue(Uuid::generateV4());
    const ObjectId object = ObjectId::fromValue(7);

    const ObjectReference here{document, object, "parts/block.bcad"};
    const ObjectReference moved{document, object, "archive/2026/block.bcad"};
    const ObjectReference noLocator{document, object};

    // Moving the file changes where it is found, never which one is meant.
    CHECK(sameTarget(here, moved));
    CHECK(sameTarget(here, noLocator));
    // operator== compares everything, so a dropped hint is visible to a
    // save/load test even though it is not identity.
    CHECK_FALSE(here == moved);

    // A different document with the same object number is a different target.
    const ObjectReference elsewhere{DocumentId::fromValue(Uuid::generateV4()), object, "parts/block.bcad"};
    CHECK_FALSE(sameTarget(here, elsewhere));
}

TEST_CASE("Reference_RefusesMalformedIdentity", "[assembly][objectref][p13]") {
    SECTION("no object") {
        auto valid = validate(ObjectReference{});
        REQUIRE_FALSE(valid.has_value());
        CHECK(valid.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("a nil document UUID") {
        ObjectReference reference{ObjectId::fromValue(1)};
        reference.document = DocumentId::fromValue(Uuid{});
        auto valid = validate(reference);
        REQUIRE_FALSE(valid.has_value());
        CHECK_THAT(valid.error().message, ContainsSubstring("must name the document"));
    }
    SECTION("a locator on an internal reference") {
        ObjectReference reference{ObjectId::fromValue(1)};
        reference.hint = "somewhere.bcad";
        auto valid = validate(reference);
        REQUIRE_FALSE(valid.has_value());
        CHECK_THAT(valid.error().message, ContainsSubstring("nothing to locate"));
    }
}

TEST_CASE("Reference_ResolvesAnInternalTargetWithoutAResolver", "[assembly][objectref][p13]") {
    PartDocument p = makePart();

    const ResolvedReference found = resolve(ObjectReference{p.part}, p.document);
    CHECK(found.state == ReferenceState::Resolved);
    CHECK(found.object != nullptr);
    CHECK(found.document == &p.document);

    SECTION("an object this document does not have") {
        const ResolvedReference missing = resolve(ObjectReference{ObjectId::fromValue(9999)}, p.document);
        CHECK(missing.state == ReferenceState::ObjectMissing);
        CHECK(missing.object == nullptr);
    }
    SECTION("a deleted object") {
        REQUIRE(p.document.removeObject(p.part).has_value());
        const ResolvedReference gone = resolve(ObjectReference{p.part}, p.document);
        CHECK(gone.state == ReferenceState::ObjectMissing);
        CHECK(gone.object == nullptr);
    }
}

TEST_CASE("Reference_AForeignObjectIdNeverBindsToTheLocalObjectOfThatNumber", "[assembly][objectref][p13]") {
    // The defect P13-COMP-001 measured and recorded, now closed for
    // references that carry a document identity.
    //
    // Two documents whose parts genuinely share an ID number: both allocate
    // from 1, so this is the ordinary case, not a contrived one.
    PartDocument here = makePart("Here", "LocalBlock");
    PartDocument there = makePart("There", "ForeignBlock");
    REQUIRE(here.part == there.part);
    REQUIRE(here.document.id() != there.document.id());

    OpenDocuments resolver;
    resolver.add(there.document);

    // A reference meant for the other document, resolved against this one.
    const ObjectReference foreign{there.document.id(), there.part};
    const ResolvedReference found = resolve(foreign, here.document, &resolver);

    REQUIRE(found.state == ReferenceState::Resolved);
    // It found the other document's object, not the local one of that number.
    CHECK(found.document == &there.document);
    CHECK(found.object->name() == "ForeignBlock");
    CHECK(found.object != here.document.findObject(here.part));

    SECTION("and with no resolver it does not fall back to the local object") {
        const ResolvedReference alone = resolve(foreign, here.document);
        CHECK(alone.state == ReferenceState::DocumentUnavailable);
        CHECK(alone.object == nullptr);
    }
}

TEST_CASE("Reference_RejectsACandidateWhoseIdentityDoesNotMatch", "[assembly][objectref][p13]") {
    // "The same path now points at another file." The resolver offers a
    // document; resolution checks its identity and refuses it. A resolver is
    // never trusted to decide whether what it found is right.
    PartDocument here = makePart("Here");
    PartDocument wrong = makePart("Wrong");
    const DocumentId intended = DocumentId::fromValue(Uuid::generateV4());

    AlwaysOffers resolver{wrong.document};
    const ResolvedReference found = resolve(ObjectReference{intended, wrong.part, "parts/block.bcad"},
                                            here.document, &resolver);

    CHECK(found.state == ReferenceState::DocumentMismatch);
    CHECK(found.object == nullptr);
    // Note what did NOT happen: the offered document holds an object of that
    // very ID, and it was still refused.
    CHECK(wrong.document.findObject(wrong.part) != nullptr);
}

TEST_CASE("Reference_ReportsTheRightDocumentButAMissingObject", "[assembly][objectref][p13]") {
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There");
    OpenDocuments resolver;
    resolver.add(there.document);

    const ResolvedReference found =
        resolve(ObjectReference{there.document.id(), ObjectId::fromValue(9999)}, here.document, &resolver);

    CHECK(found.state == ReferenceState::ObjectMissing);
    CHECK(found.document == &there.document);
    CHECK(found.object == nullptr);
}

TEST_CASE("Reference_RecoversWhenTheSourceComesBack", "[assembly][objectref][p13]") {
    // Canonical identity does not change while the target is away, so the
    // very same reference resolves again when it returns.
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There", "ForeignBlock");
    const ObjectReference reference{there.document.id(), there.part, "parts/there.bcad"};

    OpenDocuments resolver;
    CHECK(resolve(reference, here.document, &resolver).state == ReferenceState::DocumentUnavailable);

    resolver.add(there.document);
    const ResolvedReference back = resolve(reference, here.document, &resolver);
    REQUIRE(back.state == ReferenceState::Resolved);
    CHECK(back.object->name() == "ForeignBlock");

    resolver.clear();
    CHECK(resolve(reference, here.document, &resolver).state == ReferenceState::DocumentUnavailable);

    // The reference itself was never rewritten to make any of that happen.
    CHECK(sameTarget(reference, ObjectReference{there.document.id(), there.part}));
}

TEST_CASE("Reference_ResolutionIsDeterministic", "[assembly][objectref][p13][determinism]") {
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There");
    OpenDocuments resolver;
    resolver.add(there.document);
    const ObjectReference reference{there.document.id(), there.part};

    const ResolvedReference first = resolve(reference, here.document, &resolver);
    const ResolvedReference second = resolve(reference, here.document, &resolver);
    CHECK(first.state == second.state);
    CHECK(first.object == second.object);
    CHECK(first.document == second.document);

    // Unresolved is just as reproducible as resolved.
    OpenDocuments empty;
    CHECK(resolve(reference, here.document, &empty).state == resolve(reference, here.document, &empty).state);
}

TEST_CASE("Reference_ChecksTheKindOfWhatItResolvedTo", "[assembly][objectref][p13]") {
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There");
    OpenDocuments resolver;
    resolver.add(there.document);

    SECTION("a part resolves") {
        auto part = assembly::resolvePart(here.document, ObjectReference{there.document.id(), there.part}, &resolver);
        REQUIRE(part.has_value());
        CHECK(*part != nullptr);
    }
    SECTION("a sketch is not a part, in another document as much as in this one") {
        auto part = assembly::resolvePart(here.document, ObjectReference{there.document.id(), there.sketch}, &resolver);
        REQUIRE_FALSE(part.has_value());
        CHECK(part.error().code == ErrorCode::InvalidArgument);
        CHECK_THAT(part.error().message, ContainsSubstring("produces no body"));
    }
    SECTION("an unresolved reference says which state it ended in") {
        auto part = assembly::resolvePart(here.document, ObjectReference{there.document.id(), there.part});
        REQUIRE_FALSE(part.has_value());
        CHECK(part.error().code == ErrorCode::NotFound);
        CHECK_THAT(part.error().message, ContainsSubstring("document unavailable"));
    }
}

TEST_CASE("Reference_AComponentWithAnUnresolvedPartFailsRatherThanLookingFine",
          "[assembly][objectref][p13][regeneration]") {
    // Without the handler this is the silent case: an external part is no
    // dependency edge, so the graph reports nothing missing and a
    // handler-less component regenerates as if all were well.
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There", "ForeignBlock");
    const ObjectReference external{there.document.id(), there.part, "parts/there.bcad"};
    const ComponentId id = require(assembly::createComponent(here.document, "Block1", {.part = external}));

    // The graph genuinely sees nothing to complain about.
    const DocumentGraph graph = buildDependencyGraph(here.document);
    CHECK(graph.missing.empty());
    CHECK(graph.graph.dependenciesOf(id).empty());

    SECTION("with no resolver the component fails") {
        features::Regenerator regenerator;
        assembly::registerHandlers(regenerator);
        const features::RegenerationReport report = requireReport(regenerator, here.document);
        CHECK_FALSE(report.succeeded());
        CHECK(regenerator.state(id) == features::NodeState::Failed);
        const Error* error = regenerator.error(id);
        REQUIRE(error != nullptr);
        CHECK_THAT(error->message, ContainsSubstring("document unavailable"));
    }
    SECTION("with a resolver that has the document it succeeds") {
        OpenDocuments resolver;
        resolver.add(there.document);
        features::Regenerator regenerator;
        assembly::registerHandlers(regenerator, &resolver);
        const features::RegenerationReport report = requireReport(regenerator, here.document);
        CHECK(report.succeeded());
    }
}

TEST_CASE("Reference_UnresolvedComponentsAreReportedRatherThanRefused", "[assembly][objectref][p13]") {
    // ADR-003: a document whose external parts are missing must still load
    // and report each unresolved reference.
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There");
    const ComponentId local = require(assembly::createComponent(here.document, "Local", {.part = here.part}));
    const ComponentId away =
        require(assembly::createComponent(here.document, "Away", {.part = {there.document.id(), there.part}}));

    OpenDocuments empty;
    const auto unresolved = assembly::unresolvedComponents(here.document, &empty);
    REQUIRE(unresolved.size() == 1);
    CHECK(unresolved.front().component == away);
    CHECK(unresolved.front().state == ReferenceState::DocumentUnavailable);

    OpenDocuments full;
    full.add(there.document);
    CHECK(assembly::unresolvedComponents(here.document, &full).empty());

    // A component whose internal part is deleted is reported too.
    REQUIRE(here.document.removeObject(here.part).has_value());
    const auto afterDelete = assembly::unresolvedComponents(here.document, &full);
    REQUIRE(afterDelete.size() == 1);
    CHECK(afterDelete.front().component == local);
    CHECK(afterDelete.front().state == ReferenceState::ObjectMissing);
}

TEST_CASE("Reference_InternalComponentsBehaveExactlyAsBefore", "[assembly][objectref][p13]") {
    // The compatibility claim: giving `part` a wider type changed nothing
    // about a component that names a local object.
    PartDocument p = makePart();
    const ComponentId id = require(assembly::createComponent(p.document, "Block1", {.part = p.part}));

    const assembly::Component* component = assembly::findComponent(p.document, id);
    REQUIRE(component != nullptr);
    CHECK(isInternal(component->definition().part));
    CHECK(component->definition().part.object == p.part);
    CHECK(component->dependencies() == std::vector<ObjectId>{p.part});

    const DocumentGraph graph = buildDependencyGraph(p.document);
    CHECK(graph.missing.empty());
    CHECK(graph.graph.dependentsOf(p.part).contains(ObjectId{id}));

    // And the old refusals still refuse.
    CHECK_FALSE(assembly::createComponent(p.document, "OnASketch", {.part = p.sketch}).has_value());
    CHECK_FALSE(assembly::createComponent(p.document, "Ghost", {.part = ObjectId::fromValue(9999)}).has_value());
}

TEST_CASE("Reference_CreatingAComponentWithAnExternalPartChangesNothingOnFailure",
          "[assembly][objectref][p13]") {
    PartDocument p = makePart();
    const auto objectsBefore = p.document.objectCount();
    const auto idBefore = p.document.lastAllocatedId();
    const auto revisionBefore = p.document.revision();

    // A malformed external reference: a nil document identity.
    ObjectReference bad{ObjectId::fromValue(1)};
    bad.document = DocumentId::fromValue(Uuid{});
    auto refused = assembly::createComponent(p.document, "Bad", {.part = bad});

    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::InvalidArgument);
    CHECK(p.document.objectCount() == objectsBefore);
    CHECK(p.document.lastAllocatedId() == idBefore);
    CHECK(p.document.revision() == revisionBefore);

    // The document still works.
    CHECK(assembly::createComponent(p.document, "Block1", {.part = p.part}).has_value());
}

TEST_CASE("Reference_NeverRebindsToAnObjectThatTookTheTargetsPlace", "[assembly][objectref][p13]") {
    // The sharpest form of "no silent rebinding". Delete the target, then add
    // another object: a reference must stay unresolved rather than quietly
    // pointing at whatever is there now. IDs are never reused, which is what
    // makes that safe -- and this fails loudly if that ever changes.
    PartDocument p = makePart();
    const ObjectReference reference{p.part};
    REQUIRE(resolve(reference, p.document).state == ReferenceState::Resolved);

    REQUIRE(p.document.removeObject(p.part).has_value());
    CHECK(resolve(reference, p.document).state == ReferenceState::ObjectMissing);

    auto replacement = features::ExtrudeFeature::create(
        "Replacement", {.profile = SketchId::fromValue(p.sketch.value()), .depth = 20_mm});
    REQUIRE(replacement.has_value());
    const ObjectId added = require(p.document.addObject(std::move(*replacement)));

    // The newcomer did not take the removed ID, and the reference did not
    // move to it.
    CHECK(added != p.part);
    CHECK(resolve(reference, p.document).state == ReferenceState::ObjectMissing);
}

TEST_CASE("Reference_IdentityDoesNotFollowNames", "[assembly][objectref][p13]") {
    // Names are for people. A reference names an object by identity, so
    // renaming the target moves nothing, and nothing can be captured by
    // taking a name.
    PartDocument here = makePart("Here");
    PartDocument there = makePart("There", "ForeignBlock");
    OpenDocuments resolver;
    resolver.add(there.document);
    const ObjectReference reference{there.document.id(), there.part};

    REQUIRE(resolve(reference, here.document, &resolver).state == ReferenceState::Resolved);

    REQUIRE(there.document.rename(there.part, "RenamedBlock").has_value());
    const ResolvedReference after = resolve(reference, here.document, &resolver);
    REQUIRE(after.state == ReferenceState::Resolved);
    CHECK(after.object->name() == "RenamedBlock");
    CHECK(after.object == there.document.findObject(there.part));

    // And a local object taking the old name captures nothing.
    auto decoy = features::ExtrudeFeature::create(
        "ForeignBlock", {.profile = SketchId::fromValue(here.sketch.value()), .depth = 5_mm});
    REQUIRE(decoy.has_value());
    REQUIRE(here.document.addObject(std::move(*decoy)).has_value());
    const ResolvedReference still = resolve(reference, here.document, &resolver);
    CHECK(still.document == &there.document);
    CHECK(still.object->name() == "RenamedBlock");
}
