#pragma once

// P17-DATA-001 -- the canonical structural analysis, and what a result proves
// about the state it came from.
//
// A `StructuralAnalysis` is a document object, and that is the whole argument:
// ADR-030 made it for a meshing control -- "undo, redo, persistence and
// dependency invalidation come for free, because a control is an ordinary
// document object" -- and a structural analysis is the same kind of thing. It
// is canonical engineering intent that names CAD geometry, survives a remesh
// and belongs in a file. So it gets a revision, a name, a place in the
// dependency graph and a deep copy from machinery that already works.
//
// Its definition is deliberately small today. It names the meshing control it
// analyses, and nothing else, because loads are P17-LOAD-001's, restraints are
// P17-BC-001's and solver settings are P17-SOLVE-001's. Those three are added
// to the SAME definition, which is why one `revision()` covers all of them and
// no separate load or restraint counter exists to drift.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralData.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::structural {

/// What a structural analysis asks for.
///
/// ONE DEFINITION, ONE REVISION. Loads, restraints and solver settings all land
/// here when their milestones define them, so an edit to any of them moves the
/// owning object's revision and a result that was computed under the old intent
/// is detectably stale. Three independent counters would give three chances to
/// forget one.
struct StructuralAnalysisDefinition {
    /// The meshing control whose mesh is analysed. It names the body
    /// transitively, so the analysis does not hold a second opinion about
    /// which body it is for.
    MeshControlId mesh{};

    // Loads          -- P17-LOAD-001
    // Restraints     -- P17-BC-001
    // Solver settings -- P17-SOLVE-001
    //
    // Named rather than declared. An empty `std::vector<Load>` here would be a
    // placeholder for a type that does not exist, and a reader could not tell
    // an unimplemented field from an intentionally empty one.

    friend bool operator==(const StructuralAnalysisDefinition&,
                           const StructuralAnalysisDefinition&) = default;
};

/// Validates a definition on its own terms: a valid meshing control.
///
/// On its own terms ONLY. Whether that control exists, whether its body is
/// eligible and whether a mesh is current are questions about a document, and
/// `requireStructuralModel` is where they are asked (ADR-036).
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<void>
validate(const StructuralAnalysisDefinition& definition);

/// One structural analysis in a document.
///
/// The sibling of `MeshControl`, built the same way for the same reasons. It
/// holds INTENT and never a result: no displacement array, no stiffness matrix
/// and no stress lives on a document object, because those are derived state
/// that a reload must not restore and an undo must not resurrect.
class BETTERCAD_STRUCTURAL_EXPORT StructuralAnalysis final : public DocumentObject {
public:
    static constexpr std::string_view kTypeName = "structural-analysis";

    /// Fails if @p definition does not validate, so an invalid analysis cannot
    /// be constructed at all rather than existing and being rejected later.
    [[nodiscard]] static Result<std::unique_ptr<StructuralAnalysis>>
    create(std::string name, StructuralAnalysisDefinition definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }

    /// Its own identity, narrowed from the document object ID it was given.
    [[nodiscard]] AnalysisId analysisId() const noexcept {
        return AnalysisId::fromValue(id().value());
    }

    [[nodiscard]] const StructuralAnalysisDefinition& definition() const noexcept {
        return definition_;
    }

    /// Replaces the definition, reporting whether anything changed.
    ///
    /// Returns false for a no-op edit and does NOT move the revision, which is
    /// what keeps a result current across a rename or a re-set of the same
    /// value. The same contract `MeshControl::setDefinition` has.
    [[nodiscard]] Result<bool> setDefinition(StructuralAnalysisDefinition definition);

    /// The mesh control this analysis depends on, so the document's graph knows
    /// that editing the control reaches the analysis.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;

private:
    StructuralAnalysis(std::string name, StructuralAnalysisDefinition definition);

    StructuralAnalysisDefinition definition_{};
};

/// The analysis with @p id, or nullptr.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT const StructuralAnalysis*
findStructuralAnalysis(const Document& document, AnalysisId id) noexcept;

/// Every analysis in the document, in ascending ID order.
///
/// Ascending rather than insertion order, so that enumeration is the same in
/// every preset and after a reload.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::vector<AnalysisId>
structuralAnalysisIds(const Document& document);

// ---------------------------------------------------------------------------
// Result provenance
// ---------------------------------------------------------------------------

/// Exactly which input state a result was computed from.
///
/// EVERY FIELD IS A DEPENDENCY, and each one alone can make a result stale.
/// They are stored separately rather than mixed into one hash so that a stale
/// result can say WHICH of them moved -- a user told "the material changed"
/// knows what they did, and a user told "stale" does not.
///
/// WHAT EACH ONE IS, AND WHOSE IT IS:
///
/// ```text
/// body, control     which geometry and which meshing intent, by document
///                   identity
/// geometry          P16's GeometryRevision: a mix of the dependency revision
///                   counters of everything the body is built from, plus the
///                   active configuration. A MATERIAL EDIT DOES NOT MOVE IT --
///                   P16-GEOM-001 narrowed it deliberately and tests it, which
///                   is why material is a separate field here
/// mesh              P16's MeshStamp: which mesh, and which generation of it.
///                   Two meshes of the same body have different stamps, so
///                   equal node counts prove nothing
/// material          which material, and its document revision. The revision
///                   moves only on an effective change
/// analysis          which analysis, and its document revision -- which covers
///                   loads, restraints and solver settings, because all three
///                   live in one definition
/// ```
///
/// NO SOLVER METADATA. A solve duration, a residual or a backend name belongs
/// to a result's diagnostics and never to its identity: including anything
/// time-dependent would make two identical solves produce different provenance
/// and break determinism across presets.
struct StructuralResultSource {
    ObjectId body{};
    MeshControlId control{};
    meshing::GeometryRevision geometry{};
    meshing::MeshStamp mesh{};
    MaterialId material{};
    std::uint64_t materialRevision = 0;
    AnalysisId analysis{};
    std::uint64_t analysisRevision = 0;

    /// Field-complete, and defaulted on purpose: adding a dependency field
    /// without adding it to the comparison is then impossible, because there is
    /// no hand-written comparison to forget to update.
    friend constexpr bool operator==(const StructuralResultSource&,
                                     const StructuralResultSource&) noexcept = default;
};

/// Which dependency of a result no longer matches the document.
///
/// A SET, NOT THE FIRST MISMATCH. Comparison does not stop at the first
/// difference, because "the geometry and the material both changed" is a more
/// useful thing to be told than either half, and because a user fixing one of
/// two reasons should not have to re-run to discover the second.
enum class StaleReason : std::uint8_t {
    Body,
    Control,
    Geometry,
    Mesh,
    Material,
    Analysis,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view toString(StaleReason reason) noexcept;

/// Every way @p result differs from @p current, in the order of the enum.
///
/// Empty means the two are identical, which is the only thing that makes a
/// result current. Deterministic: the order is the enum's, never a container's.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::vector<StaleReason>
staleReasons(const StructuralResultSource& result, const StructuralResultSource& current);

} // namespace bettercad::structural
