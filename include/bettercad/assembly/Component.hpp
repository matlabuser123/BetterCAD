#pragma once

#include <bettercad/assembly/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>

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
// * It is not a placement yet. Component transforms are P13-XFORM-001 and
//   deliberately absent here: ADR-005 decided that placement INTENT is
//   persisted and the solved transform is derived, and neither exists until
//   that milestone. There is no transform field, not even an unused one.
// * It does not reference another document. ADR-003 scoped P13 to parts in
//   the same document, because the dependency graph is single-document:
//   dependencies() speaks in ObjectId, which means nothing elsewhere. No
//   field anticipates the external form.
namespace bettercad::assembly {

/// What a component instance is made of: the part it places, and whether it
/// is suppressed.
///
/// `part` is the ObjectId of an object in the SAME document -- the feature
/// whose body is the part being placed. It is the component's only
/// dependency, so dirty propagation, ordering and blocking work with no new
/// machinery (ADR-003).
///
/// A suppressed component stays in the document with its ID and its
/// reference intact; suppression is engineering intent ("not in this
/// build"), not deletion.
struct ComponentDefinition {
    ObjectId part{};
    bool suppressed = false;

    friend bool operator==(const ComponentDefinition&, const ComponentDefinition&) = default;
};

/// Checks the definition on its own: a valid part ID. Whether that object
/// exists, and whether it is a kind that can be placed, is checked against
/// the document by checkComponent(), because a definition alone cannot know.
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
    /// The part this component places. Exactly one edge: a component is
    /// blocked when its part fails, and fails when its part is gone.
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
