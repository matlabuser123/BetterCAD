#pragma once

// The canonical meshing intent, as a document object (P16-CMD-001, ADR-030).
//
//     canonical            a MeshControl: which body, how finely, which
//                          regions matter. A document object, in the
//                          dependency graph, edited only through commands.
//     derived              the generated mesh, held by the Mesher service,
//                          keyed by its control. Never a document object,
//                          never persisted, never restored by undo.
//
// ADR-030 decided this, and said why: "changing a density must be able to
// invalidate a derived mass" -- changing a body must be able to invalidate a
// derived mesh, "and only a graph node can express that".
//
// WHAT IT HOLDS IS WHAT WAS ASKED FOR, AND NOTHING THAT CAME BACK. No nodes,
// no elements, no backend handle, no quality report, no mapping, no result of
// any kind. That is not a style preference: a document object is cloned for
// undo, compared for equality, and will be persisted, and any derived field
// would be a second authority that could disagree with the mesher.
//
// IT REUSES THE EXISTING VALUE TYPES rather than restating them.
// `VolumeMeshControls` is exactly what `volumeMeshFor` takes, so a control can
// hand its own intent to the mesher without translation -- and a translation
// layer is where a third spelling of "element size" would eventually appear.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::meshing {

/// Everything a control asks for, as one value.
///
/// ONE VALUE, so a command can carry a before and an after of it. The material
/// precedent does the same with `MaterialDefinition`: a before/after pair of a
/// small value object is exact by construction, where per-field inversion is a
/// list of things to forget.
struct MeshControlDefinition {
    /// The feature whose body is meshed.
    ///
    /// AN ObjectId, AND THE REASON THE CONTROL IS A GRAPH NODE. `dependencies()`
    /// reports it, so the document's own dependency graph knows that editing
    /// the body reaches the control -- which is how a geometry change can
    /// invalidate a mesh without anything in the GUI noticing first.
    ObjectId body{};
    /// How the boundary is discretised and how the volume is sized: exactly
    /// what `volumeMeshFor` takes.
    VolumeMeshControls mesh{};
    /// The policy a quality report is classified under. Intent, not a result:
    /// the report itself is derived and lives nowhere near here.
    QualityThresholds quality{};
    /// Named boundary intent, for the solver to come. Each set names CAD faces
    /// by `FaceName`; the facets they resolve to are derived and change with
    /// every remesh, so they are not here.
    std::vector<NamedBoundarySet> boundarySets{};

    friend bool operator==(const MeshControlDefinition&, const MeshControlDefinition&) = default;
};

/// Validates @p definition on its own terms.
///
/// Delegates to the owning milestones' validators -- `meshing::validate` for
/// the sizing controls and for each boundary set -- rather than restating their
/// rules. A command layer with its own copy of "a size must be positive" is a
/// second policy that will eventually disagree with the first.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<void> validate(
    const MeshControlDefinition& definition);

/// The canonical meshing intent for one body.
class BETTERCAD_MESHING_EXPORT MeshControl final : public DocumentObject {
public:
    using Definition = MeshControlDefinition;
    static constexpr std::string_view kTypeName = "mesh-control";

    [[nodiscard]] static Result<std::unique_ptr<MeshControl>> create(
        std::string name, MeshControlDefinition definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;

    /// The body this control meshes.
    ///
    /// THE EDGE THAT MAKES INVALIDATION WORK. Without it the graph could not
    /// know that a body edit reaches this control, and a mesh of changed
    /// geometry could keep looking current.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    /// This control's ID, narrowed. Valid once the document owns it.
    [[nodiscard]] MeshControlId meshControlId() const noexcept {
        return MeshControlId::fromValue(id().value());
    }

    [[nodiscard]] const MeshControlDefinition& definition() const noexcept { return definition_; }

    /// Replaces the definition.
    ///
    /// Returns whether anything changed, so `Document::modifyObject` bumps the
    /// revision only on an effective change -- which is what makes a no-op
    /// edit cost no history and no mesh invalidation.
    ///
    /// The ID is not touched, and cannot be: nothing here can reach it.
    [[nodiscard]] Result<bool> setDefinition(MeshControlDefinition definition);

    // --- the local sizing controls, keyed by the face they refine ----------
    //
    // A LOCAL CONTROL'S IDENTITY IS ITS FaceName, and that is P16-SIZE-001's
    // contract rather than a choice made here. It refuses two controls on one
    // face -- `SizingIssueKind::DuplicateFaceControl`, "two sizes for one face
    // is a modelling mistake the author should see" -- and it states that the
    // order of `MeshSizingControls::local` carries no meaning. A face
    // therefore names at most one control, which is exactly what an identity
    // is, and inventing a `LocalSizingId` beside it would be a second identity
    // system for the same thing.

    /// The local control for @p face, if there is one.
    [[nodiscard]] const LocalMeshSizing* localSizing(const FaceName& face) const noexcept;

    /// The local controls in a deterministic order: ascending `FaceName`.
    ///
    /// Not the stored order, which carries no meaning, and not a hash order.
    /// A caller that enumerates them -- a panel, a CLI, a persisted file --
    /// must see the same sequence every time or its own output stops being
    /// reproducible.
    [[nodiscard]] std::vector<LocalMeshSizing> orderedLocalSizing() const;

    /// The named boundary set with @p id, if there is one.
    [[nodiscard]] const NamedBoundarySet* boundarySet(BoundarySetId id) const noexcept;

    /// The boundary sets in a deterministic order: ascending `BoundarySetId`.
    [[nodiscard]] std::vector<NamedBoundarySet> orderedBoundarySets() const;

private:
    MeshControl(std::string name, MeshControlDefinition definition);

    MeshControlDefinition definition_{};
};

} // namespace bettercad::meshing
