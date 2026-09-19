#pragma once

#include <bettercad/assembly/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/ObjectReference.hpp>
#include <bettercad/core/document/Placement.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Assembly components (P13-COMP-001, implementing ADR-002 and ADR-003).
//
// A component is one placement of a part in an assembly: "this bracket,
// here". It is a DocumentObject, so it lives in the ordinary Document
// alongside the sketches and features that define the parts, and inherits
// their stable IDs, revisions, dependency edges, commands, undo and
// persistence. A document is not typed "part" or "assembly" -- it is
// whatever objects it holds.
//
// What a component is NOT:
//
// * It is not a copy of a part. It names the part by ObjectId and owns no
//   geometry of its own. Three components of one part are one definition and
//   three identities; the part's body is built once, by the part's own
//   features, exactly as it is without any assembly.
// * It is not a solved position. P13-XFORM-001 gives a component its
//   `placement`, which is INTENT: ADR-005 persists that and derives the
//   RigidTransform3D from it every time, so there is still no transform
//   field here and none in the file.
// * It may now name a part in ANOTHER document (P13-REF-001), through an
//   ObjectReference carrying that document's UUID. Such a reference is not a
//   dependency edge: the graph speaks in ObjectId, which means nothing
//   outside one document, so ADR-003 keeps external references out of
//   regeneration until the graph has a wider node identity. What stops that
//   being silent is the regeneration handler, which fails a component whose
//   part does not resolve.
namespace bettercad::assembly {

/// What a component instance is made of: the part it places, whether it is
/// suppressed, and where it sits.
///
/// `part` names the feature whose body is the part being placed. An
/// ObjectId converts to one implicitly, so an internal reference reads and
/// writes exactly as it did before; an external one also carries the UUID of
/// the document that owns the object.
///
/// An internal `part`, and the parameters `placement` is driven by, are the
/// component's dependencies, so dirty propagation, ordering and blocking
/// work with no new machinery. An external `part` contributes no edge
/// (ADR-003), and is reported by the regeneration handler instead.
///
/// A suppressed component stays in the document with its ID and its
/// reference intact; suppression is engineering intent ("not in this
/// build"), not deletion.
struct ComponentDefinition {
    ObjectReference part{};
    bool suppressed = false;
    /// Where this instance sits, as intent (P13-XFORM-001). Two components
    /// of one part hold their own, which is what makes them instances
    /// rather than copies. The identity placement is the default, and the
    /// transform it means is derived by assembly::placementOf(), never
    /// stored here or in the file.
    ComponentPlacement placement{};

    friend bool operator==(const ComponentDefinition&, const ComponentDefinition&) = default;
};

/// Checks the definition on its own: a valid part ID and a placement whose
/// literals are finite. Whether that object exists, and whether it is a kind
/// that can be placed, is checked against the document by checkComponent(),
/// because a definition alone cannot know; whether a placement's parameters
/// exist and are the right dimension is checked by placementOf(), because a
/// definition cannot read them either.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<void> validate(const ComponentDefinition& definition);

/// One placement of a part in an assembly (type name "component").
///
/// The component's own ID identifies the INSTANCE. Two components of one
/// part have different IDs and the same `part`; that distinction is the
/// whole point of an instance and is what lets a later milestone give them
/// different transforms.
class BETTERCAD_ASSEMBLY_EXPORT Component final : public DocumentObject {
public:
    using Definition = ComponentDefinition;
    static constexpr std::string_view kTypeName = "component";

    [[nodiscard]] static Result<std::unique_ptr<Component>> create(std::string name,
                                                                   const ComponentDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The part this component places, and every parameter its placement
    /// is driven by. A component is blocked when its part fails, fails when
    /// its part is gone, and moves when a parameter it is placed by changes.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    /// This component's ID, narrowed. Valid once the document owns it.
    [[nodiscard]] ComponentId componentId() const noexcept { return ComponentId::fromValue(id().value()); }

    [[nodiscard]] const ComponentDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition. Returns whether anything changed, so the
    /// document only bumps revisions on an effective change.
    Result<bool> setDefinition(const ComponentDefinition& definition);

private:
    Component(std::string name, const ComponentDefinition& definition);

    ComponentDefinition definition_;
};

} // namespace bettercad::assembly
