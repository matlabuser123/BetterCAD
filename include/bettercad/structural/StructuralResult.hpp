#pragma once

// P17-DATA-001 -- the derived structural result, and the one place that decides
// whether it still describes the model.
//
// A result is DERIVED STATE, in exactly the sense ADR-030 gave the word for a
// mesh: it is reproducible from canonical intent, it is never persisted as
// authority, and a reload regenerates rather than restores it. What makes that
// safe is that a result carries the identity of everything it was computed
// from, so it can say "I am not about this document any more" instead of being
// quietly read as though it were.
//
// ONE PLACE DECIDES. `resultCurrency` and `analysisState` are the only
// currentness answers in P17. A GUI that compared node counts, a CLI that
// compared an AnalysisId and a post-processor that trusted its caller would be
// three answers that drift, and the one that drifts towards "current" is the
// one that ships a wrong number.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralData.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::structural {

/// The solution of one structural analysis on one mesh.
///
/// IMMUTABLE, AND CONSTRUCTED ONLY BY A VALIDATING FACTORY. Every accessor is
/// const and returns a view or a value; there is no setter, and in particular
/// there is no way to reach the source stamp and rewrite it. A result whose
/// provenance could be edited would let stale output be made to look current
/// without recomputing anything, which is the one failure mode this whole
/// design is built against.
///
/// HOW THE ARRAYS ARE KEYED, and why each is keyed the way it is:
///
/// ```text
/// displacements   DENSE, parallel to mesh.nodes(). Entry i is the
///                 displacement of mesh.nodes()[i], whose NodeId is
///                 mesh.nodes()[i].id. Every node has one.
/// strains         DENSE, parallel to mesh.tetrahedra(). Constant within a
///                 Tet4, so one per element.
/// stresses        DENSE, likewise.
/// reactions       SPARSE, each carrying its NodeId, ascending. A reaction
///                 exists only where the model is restrained.
/// ```
///
/// NOT INDEXED BY RAW NodeId, and that is deliberate. P16 allocates node IDs
/// from 1 and `MeshBuilder::addNode(NodeId, ...)` lets a caller choose them, so
/// nothing guarantees they are 0..N-1. Indexing a vector by `id.value()` would
/// be correct for every mesh the current backend happens to produce and wrong
/// for the first one that is not. The mesh's own enumeration is the qualified
/// access model: `Mesh` documents that nodes are stored and enumerated in
/// ascending NodeId, with no unordered container anywhere, so the same
/// construction gives the same order in every preset.
///
/// STORAGE IS O(nodes + elements). The mesh is NOT copied in: a result records
/// the `MeshStamp` of the mesh it belongs to and nothing more. Duplicating a
/// hundred thousand nodes per solve would be the obvious way to make results
/// unusable at scale, and the stamp is twelve bytes.
class BETTERCAD_STRUCTURAL_EXPORT StructuralResult {
public:
    /// Builds a result, or fails saying which invariant it broke.
    ///
    /// Checks, all of them against @p mesh rather than against a promise:
    ///
    /// ```text
    /// the source names this exact mesh      source.mesh == mesh stamp
    /// one displacement per node             size == nodeCount
    /// one strain and one stress per Tet4    size == tetrahedronCount
    /// reactions ascending and unique        and every node present in the mesh
    /// every value finite                    no NaN, no infinity
    /// ```
    ///
    /// A failed solve must not call this with half its channels filled: a
    /// partially populated result is not a worse result, it is a result that
    /// claims to be a solution and is not. `P17-SOLVE-001` publishes a result
    /// only on success, and keeps the previous one as stale otherwise -- the
    /// behaviour `Mesher::generate` already has for a failed remesh.
    [[nodiscard]] static Result<StructuralResult> create(StructuralResultSource source,
                                                         const meshing::VolumeMesh& mesh,
                                                         std::vector<Translation3D> displacements,
                                                         std::vector<NodalReaction> reactions,
                                                         std::vector<Strain6> strains,
                                                         std::vector<Stress6> stresses);

    /// What this was computed from. There is no non-const overload, by design.
    [[nodiscard]] const StructuralResultSource& source() const noexcept { return source_; }

    /// Displacement of `mesh.nodes()[i]`, in metres. Length == node count.
    [[nodiscard]] std::span<const Translation3D> displacements() const noexcept {
        return displacements_;
    }

    /// Reactions at restrained nodes, in newtons, ascending by NodeId.
    [[nodiscard]] std::span<const NodalReaction> reactions() const noexcept { return reactions_; }

    /// Strain of `mesh.tetrahedra()[i]`, dimensionless, engineering shear.
    [[nodiscard]] std::span<const Strain6> strains() const noexcept { return strains_; }

    /// Stress of `mesh.tetrahedra()[i]`, in pascals.
    [[nodiscard]] std::span<const Stress6> stresses() const noexcept { return stresses_; }

    /// Whether this result belongs to @p mesh.
    ///
    /// The refusal a renderer or a post-processor asks for before reading a
    /// result against a mesh it was handed separately. Compares stamps, never
    /// counts: two meshes of the same body can have identical node and element
    /// counts and be different meshes, and a result read across that gap would
    /// map every value onto the wrong material.
    [[nodiscard]] bool describes(const meshing::VolumeMesh& mesh) const noexcept;

    /// The displacement of @p node, or an error if it is not @p mesh's node or
    /// @p mesh is not this result's mesh.
    ///
    /// Binary search over the mesh's ascending nodes, so it is O(log n) and
    /// needs no second index. Provided so that a caller never has to work out
    /// the position itself, which is where an assumption about dense IDs would
    /// otherwise creep in.
    [[nodiscard]] Result<Translation3D> displacementOf(const meshing::VolumeMesh& mesh,
                                                       meshing::NodeId node) const;

private:
    StructuralResult(StructuralResultSource source, std::vector<Translation3D> displacements,
                     std::vector<NodalReaction> reactions, std::vector<Strain6> strains,
                     std::vector<Stress6> stresses) noexcept;

    StructuralResultSource source_{};
    std::vector<Translation3D> displacements_{};
    std::vector<NodalReaction> reactions_{};
    std::vector<Strain6> strains_{};
    std::vector<Stress6> stresses_{};
};

// ---------------------------------------------------------------------------
// Currentness
// ---------------------------------------------------------------------------

/// What a held result is, relative to the document as it is now.
enum class ResultCurrency : std::uint8_t {
    /// Nothing has been solved.
    NoResult,
    /// Every dependency matches what it was computed from.
    Current,
    /// At least one dependency moved. `staleReasons` says which.
    Stale,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view
toString(ResultCurrency currency) noexcept;

/// Whether @p currency means the result may be read as describing the model.
///
/// Only `Current` does. The sibling of `meshing::describesTheModel`, and
/// deliberately the same shape: a caller that has internalised one has
/// internalised the other.
///
/// NO EXPORT MACRO, and it must not have one: it is defined here, so in a
/// shared build the macro expands to `dllimport` on an inline function, which
/// GCC rejects.
[[nodiscard]] constexpr bool describesTheModel(ResultCurrency currency) noexcept {
    return currency == ResultCurrency::Current;
}

/// The input state a result computed NOW would carry.
///
/// Fails for exactly the reasons `requireStructuralModel` fails, because it
/// calls it: this is where P17-DATA inherits every gate ADR-036 established
/// rather than re-asking any of them. On top of that it reads the analysis and
/// its material revision, which the input boundary has no reason to know about.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT Result<StructuralResultSource>
currentResultSource(const Document& document, const features::Regenerator& regenerator,
                    const meshing::Mesher& mesher, AnalysisId analysis);

/// Compares @p result's provenance against @p current.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT ResultCurrency
resultCurrency(const StructuralResult* result, const StructuralResultSource& current);

/// Where one structural analysis stands.
///
/// DERIVED, NEVER STORED. Every value is a function of the document, the
/// current mesh state and the result in hand, so there is no cached flag to
/// drift out of agreement with the thing it describes. P16 made the same choice
/// for `MeshCurrency` and the reasoning carries over unchanged.
enum class AnalysisState : std::uint8_t {
    /// The document has no analysis with that id.
    NoAnalysis,
    /// The analysis exists but its inputs do not resolve: the geometry is
    /// ineligible, no mesh is current, or the material is missing or unusable.
    /// `structuralInputProblem` says which.
    InputsUnavailable,
    /// Every input resolves and nothing has been solved yet. A perfectly
    /// ordinary state, and the reason result existence is not analysis
    /// identity.
    Ready,
    /// A result exists and every dependency matches.
    SolvedCurrent,
    /// A result exists and at least one dependency moved.
    SolvedStale,
    /// The last solve attempt failed. Its own state even when an older result
    /// survives, because reporting that result as merely stale would hide that
    /// the attempt to replace it failed -- the distinction `MeshCurrency` draws
    /// between `StaleIntent` and `GenerationFailed`.
    SolveFailed,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view toString(AnalysisState state) noexcept;

/// Evaluates the state of @p analysis.
///
/// @p lastResult and @p lastFailure are what a solver service holds for this
/// analysis; both may be null. They are parameters rather than a service
/// because the service is `P17-SOLVE-001`'s, and the shape mirrors
/// `Mesher::mesh()` and `Mesher::lastFailure()` so that passing them through
/// will be the obvious thing to do.
///
/// DETERMINISTIC: no wall clock, no pointer identity, no unordered iteration.
/// The same document, mesh state and result give the same state in every
/// preset and on every run.
[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT AnalysisState
analysisState(const Document& document, const features::Regenerator& regenerator,
              const meshing::Mesher& mesher, AnalysisId analysis,
              const StructuralResult* lastResult, const Error* lastFailure);

} // namespace bettercad::structural
