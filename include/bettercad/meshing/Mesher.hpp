#pragma once

// The derived meshes, held by a service and keyed by their control
// (P16-CMD-001, ADR-030).
//
//     "A generated mesh is derived state held by a Mesher service, keyed by
//      its control, exactly as the Regenerator holds bodies keyed by their
//      features. It is never a document object, has no ObjectId, and is not in
//      the dependency graph."                                       -- ADR-030
//
// WHY THIS EXISTS AT ALL, rather than the GUI holding the mesh it generated.
// Before this, "is the mesh stale?" was answered by comparing GeometryRevisions
// in the renderer -- which notices a GEOMETRY change and cannot notice a SIZING
// change, because the sizing was an argument nobody kept. So editing the
// element size left the mesh looking current. The brief's rule is that
// invalidation "must originate from canonical intent revision" and that a
// UI-owned markMeshStale() is not authority; a service keyed by the control,
// holding what it built from, is how that becomes true.
//
// IT IS NOT A CACHE OF CONVENIENCE. It holds the one mesh per control that was
// last generated, so that a stale mesh stays INSPECTABLE -- P16-VIZ-001's whole
// stale-state contract depends on the old mesh still being there to look at --
// and so that nothing has to regenerate just to answer a question about
// currency.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>

namespace bettercad::meshing {

/// Why a held mesh does or does not describe the model as it is now.
///
/// THE REASON IS PART OF THE ANSWER. "Stale" alone cannot tell a user whether
/// to expect a different shape or merely a different element size, and it
/// cannot tell the code which derived things actually need discarding. The
/// brief asks for invalidation precision rather than "invalidate everything",
/// and a single boolean could not express it.
enum class MeshCurrency : std::uint8_t {
    /// Nothing has been generated for this control.
    NoMesh,
    /// Built from the geometry and the meshing intent the document has now.
    Current,
    /// The meshing intent changed -- a global or local size. The geometry is
    /// the same, so the shape is unchanged; the discretisation is not.
    StaleIntent,
    /// The geometry changed. The mesh describes a body that no longer exists
    /// in that form.
    StaleGeometry,
    /// The most recent generation attempt failed. An earlier mesh may still be
    /// held, and if so it is stale by definition.
    GenerationFailed,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(MeshCurrency currency) noexcept;

/// Whether @p currency means the mesh may be used as a description of the
/// model. Only `Current` does.
///
/// NO EXPORT MACRO, AND IT MUST NOT HAVE ONE. It is defined here, so in a
/// shared build the macro expands to `dllimport` on an inline function, which
/// GCC rejects: "inline function ... declared as dllimport: attribute
/// ignored". Nothing is exported because nothing needs to be -- every caller
/// compiles the body. No other `constexpr` in `include/bettercad/` carries the
/// macro either; this one did until the pre-freeze shared build refused it.
[[nodiscard]] constexpr bool describesTheModel(MeshCurrency currency) noexcept {
    return currency == MeshCurrency::Current;
}

/// The generated meshes, one per control.
class BETTERCAD_MESHING_EXPORT Mesher {
public:
    /// Generates the mesh for @p control from the intent the document holds
    /// NOW, replacing whatever was held for it.
    ///
    /// COMMANDS DO NOT CALL THIS. An edit changes intent and marks the mesh
    /// stale; meshing is a separate, explicit request. That keeps undo cheap
    /// and deterministic -- an undo that silently remeshed would make every
    /// history step cost a mesh -- and it is what lets a failed generation
    /// leave the canonical intent and the history untouched.
    ///
    /// On failure the previously held mesh is KEPT and the failure recorded,
    /// so the old mesh stays inspectable and `currency` reports
    /// `GenerationFailed` rather than pretending nothing happened.
    [[nodiscard]] Result<const VolumeMesh*> generate(const Document& document,
                                                     const features::Regenerator& regenerator,
                                                     MeshControlId control);

    /// What the held mesh is, relative to the document as it is now.
    ///
    /// COMPARED BY VALUE, NOT BY THE CONTROL'S REVISION, and that distinction
    /// is the whole of this milestone's invalidation precision. A control's
    /// revision bumps for ANY edit of its definition -- including renaming a
    /// boundary set, which cannot change a single tetrahedron. Comparing the
    /// `VolumeMeshControls` the mesh was built from against the control's
    /// current ones answers the question actually being asked, so a rename
    /// does not remesh a hundred thousand elements.
    [[nodiscard]] MeshCurrency currency(const Document& document,
                                        MeshControlId control) const;

    /// The held mesh, whatever its currency, or nullptr if there is none.
    ///
    /// A STALE MESH IS STILL RETURNED. It is old, not wrong: P16-VIZ-001's
    /// inspection of a stale mesh depends on it still being here, and the
    /// caller asks `currency` to learn what it is looking at.
    [[nodiscard]] const VolumeMesh* mesh(MeshControlId control) const noexcept;

    /// The geometry-to-mesh mapping for the held mesh, or nullptr.
    [[nodiscard]] const GeometryMeshMap* map(MeshControlId control) const noexcept;

    /// The quality report for the held mesh, under the thresholds the control
    /// carried when it was generated, or nullptr.
    [[nodiscard]] const MeshQualityReport* quality(MeshControlId control) const noexcept;

    /// Whether the held quality report was computed under the control's
    /// current threshold policy.
    ///
    /// Separate from the mesh's currency, because a threshold edit changes how
    /// the SAME mesh is classified without making the mesh wrong.
    [[nodiscard]] bool qualityDescribesCurrentPolicy(const Document& document,
                                                     MeshControlId control) const;

    /// Why the most recent generation attempt failed, or nullptr.
    [[nodiscard]] const Error* lastFailure(MeshControlId control) const noexcept;

    /// Discards everything held for @p control. Called when a control is
    /// deleted; a mesh of a control that no longer exists is not stale, it is
    /// meaningless.
    void forget(MeshControlId control) noexcept;

    /// Discards everything. The mesher holds only derived state, so this is
    /// always safe and never loses intent.
    void clear() noexcept;

    [[nodiscard]] std::size_t heldMeshCount() const noexcept { return held_.size(); }

private:
    /// What was generated, and what it was generated FROM.
    ///
    /// The provenance is the point: without it, "is this still what the
    /// document asks for?" has no answer that does not involve regenerating.
    struct Held {
        VolumeMesh mesh;
        GeometryMeshMap map;
        MeshQualityReport quality;
        /// The body, and the discretisation and sizing, this was built from.
        ObjectId body{};
        VolumeMeshControls controls{};
        QualityThresholds thresholds{};
        /// The geometry stamp the body had at the time (P16-GEOM-001).
        GeometryRevision revision{};
    };

    std::map<MeshControlId::ValueType, Held> held_{};
    std::map<MeshControlId::ValueType, Error> failures_{};
};

} // namespace bettercad::meshing
