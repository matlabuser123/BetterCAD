#pragma once

// P19-DATA-001 -- the canonical CFD analysis as a document object, its semantic
// fingerprint, and the provenance record a result carries back to it.
//
// WHY A DOCUMENT OBJECT. The argument ADR-030 made for a meshing control and
// P17-DATA-001 and P18-DATA-001 repeated: a revision, undo and redo, a place in
// the dependency graph and (later) a file representation all come from
// machinery every other document object already uses, and only a graph node can
// express "editing the body reaches this analysis".
//
// WHAT IT HOLDS, AND WHAT IT CANNOT. A `CfdAnalysisDefinition` is intent and
// nothing else. There is no pressure, velocity or flux in it, no cell, face,
// matrix, residual or mesh handle, and NO FLUID: the fluid is the document's P15
// material assignment (ADR-060), and a material id here would be a second
// authority that could disagree with it (ADR-061). `CfdResult.hpp` is not
// included here, so a result cannot even be NAMED from the definition's header
// -- the property `CfdData_TheDefinitionHoldsOnlyIntent` asserts by size and by
// non-constructibility, and the compile-fail group asserts by trying.
//
// MODE-COMPLETE BY VALIDATION. A steady definition carries steady settings and
// no transient settings; a transient one the reverse. The initial condition is
// present in both modes (see `CfdInitialCondition`).

#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/meshing/GeometryPreparation.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad {
class Document;
} // namespace bettercad

namespace bettercad::cfd {

/// Everything a user decides about one CFD analysis. Intent only.
///
/// `boundaryConditions` keeps the order the user gave; that order is canonical
/// state like the rest, compared by `operator==` and covered by the
/// fingerprint. Identities must be unique, and `validate` refuses a duplicate.
struct CfdAnalysisDefinition {
    /// The meshing control whose current mesh is analysed. Its body is the
    /// fluid region (ADR-058).
    MeshControlId mesh{};
    CfdAnalysisMode mode = CfdAnalysisMode::Steady;
    std::vector<CfdBoundaryCondition> boundaryConditions{};
    /// Both modes: the starting point of the iteration, and in a transient
    /// analysis the state at t = 0.
    CfdInitialCondition initialCondition{};
    /// Present only when no boundary condition fixes the pressure; see
    /// `pressureDatumProblem`.
    std::optional<CfdReferencePressure> referencePressure{};
    /// Absent: no body force.
    std::optional<CfdGravity> gravity{};
    CfdIterationSettings iteration{};
    CfdConvergenceSettings convergence{};
    CfdLinearSolverSettings linearSolver{};
    /// Required for a steady analysis; must be absent for a transient one.
    std::optional<CfdSteadySettings> steady = CfdSteadySettings{};
    /// Required for a transient analysis; must be absent for a steady one.
    std::optional<CfdTransientSettings> transient{};
    CfdOutputSettings output{};

    friend bool operator==(const CfdAnalysisDefinition&, const CfdAnalysisDefinition&) = default;
};

/// What is wrong with a definition's pressure datum, if anything (ADR-058).
enum class PressureDatumProblem : std::uint8_t {
    /// No boundary condition fixes the pressure and there is no reference
    /// pressure: the pressure is determined only up to a constant.
    Missing,
    /// A boundary condition fixes the pressure AND a reference pressure is
    /// given: the reference would be ignored, or would contradict the boundary.
    Overdetermined,
};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(PressureDatumProblem problem) noexcept;

/// The pressure datum of @p definition, judged from the definition alone: a
/// reference pressure is required IF AND ONLY IF no boundary condition fixes the
/// pressure.
///
/// ONE RULE, TWO PLACES THAT ASK IT, AND THEY ASK THIS. `validate(definition)`
/// refuses `Overdetermined`: a reference beside a pressure boundary is inert or
/// contradictory intent, wrong whatever else is added later. It does NOT refuse
/// `Missing`: a definition is built one condition at a time (the commands of
/// P19-CMD-001 add them singly), and "nothing fixes the pressure yet" is the
/// state of every definition before its outlet is added. Missing is an input
/// problem of the PROBLEM, refused before a solve by the input gate that also
/// refuses unassigned boundary facets (P19-BC-001), by calling this function.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<PressureDatumProblem>
pressureDatumProblem(const CfdAnalysisDefinition& definition) noexcept;

/// Checks a definition on its own terms: a valid control handle, a known mode,
/// every setting valid, every boundary condition valid with a unique identity,
/// the initial condition, reference pressure and gravity valid, a pressure datum
/// that is not overdetermined, and the mode's required settings present and
/// the other mode's absent.
///
/// Does not consult the document. Whether the control exists, the faces resolve,
/// every boundary facet has one condition or the reference point lies in the
/// fluid are questions about a document and a mesh.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdAnalysisDefinition& definition);

// ---------------------------------------------------------------------------
// The semantic fingerprint
// ---------------------------------------------------------------------------

/// A 64-bit digest of what a definition MEANS.
///
/// ```text
/// equal definitions        equal fingerprints, always: the text below is a
///                          function of operator=='s inputs and nothing else
///                          (-0.0 and +0.0 compare equal and are written alike)
/// any field changed        a different text, so -- up to a 2^-64 FNV-1a
///                          collision -- a different fingerprint
/// not in it                the object's name, its id, its revision, any
///                          address, any time, any locale, the build preset
/// ```
///
/// WHAT IT IS FOR. It lets a result say WHAT intent it was computed from,
/// readable without the document, and it is the content key a derived object
/// can be keyed by. It is NOT what decides currentness on its own: a result's
/// source records it beside the analysis revision, and both must match
/// (ADR-061; the P18 owner decision D1 about the two regimes is not taken
/// here).
struct CfdDefinitionFingerprint {
    std::uint64_t value = 0;

    friend constexpr bool operator==(const CfdDefinitionFingerprint&, const CfdDefinitionFingerprint&) = default;
};

/// `cfd-definition-v1:<16 lowercase hex digits>`.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::string toString(CfdDefinitionFingerprint fingerprint);

/// The canonical text of @p definition: one `key=value` line per field in
/// declaration order, every boundary condition in the given order with its kind,
/// identity, face name and values, every double in its shortest exact decimal
/// form, every enumerator by its name. Deterministic: no address, no locale, no
/// clock. Exposed so the evidence can show what was digested.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::string canonicalText(const CfdAnalysisDefinition& definition);

/// FNV-1a over `canonicalText(definition)`.
[[nodiscard]] BETTERCAD_CFD_EXPORT CfdDefinitionFingerprint fingerprint(const CfdAnalysisDefinition& definition);

// ---------------------------------------------------------------------------
// The document object
// ---------------------------------------------------------------------------

/// The canonical CFD analysis: a document object holding a definition.
///
/// Its revision moves on every effective `setDefinition`, which is what lets a
/// result say "the analysis I was computed from has since been edited".
class BETTERCAD_CFD_EXPORT CfdAnalysis final : public DocumentObject {
public:
    static constexpr std::string_view kTypeName = "cfd-analysis";

    /// The only way to make one, so an invalid definition never enters a
    /// document.
    [[nodiscard]] static Result<std::unique_ptr<CfdAnalysis>> create(std::string name,
                                                                     CfdAnalysisDefinition definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    /// This object's identity, narrowed to its own kind.
    [[nodiscard]] CfdAnalysisId analysisId() const noexcept { return CfdAnalysisId::fromValue(id().value()); }

    [[nodiscard]] const CfdAnalysisDefinition& definition() const noexcept { return definition_; }

    /// Replaces the definition. Returns false, and moves no revision, when the
    /// new definition equals the old one: re-setting the same value is not an
    /// edit and must not stale a result. Call through `Document::modifyObject`
    /// so the change is tracked.
    [[nodiscard]] Result<bool> setDefinition(CfdAnalysisDefinition definition);

private:
    CfdAnalysis(std::string name, CfdAnalysisDefinition definition);
    CfdAnalysis(const CfdAnalysis&) = default;

    CfdAnalysisDefinition definition_{};
};

/// The analysis with @p id, or nullptr if there is none or the object with that
/// number is of another kind.
[[nodiscard]] BETTERCAD_CFD_EXPORT const CfdAnalysis* findCfdAnalysis(const Document& document,
                                                                     CfdAnalysisId id) noexcept;

/// Every CFD analysis in @p document, ascending by identity, whatever order the
/// objects were added in.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::vector<CfdAnalysisId> cfdAnalysisIds(const Document& document);

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

/// The identity of everything a CFD result was computed from.
///
/// A RECORD OF IDENTITIES AND REVISIONS, NOT A HASH OF BYTES. Separate fields,
/// so that a stale result can say WHICH dependency moved, and nothing derived
/// from an address, padding, a timestamp or an iteration order. Field-complete
/// equality by `= default`.
///
/// ```text
/// body, control         which geometry and which meshing intent
/// geometry              P16's geometry revision: moves on a CAD edit
/// mesh                  WHICH MESH AND WHICH GENERATION; a remesh with equal
///                       counts and equal NodeIds is still a different stamp
/// fluid + revision      which P15 material is the fluid, and whether it was
///                       edited (its density or its viscosity)
/// analysis + revision   which analysis, and whether its definition was edited
/// definition            the semantic fingerprint of the definition consumed:
///                       what the intent WAS, readable without the document
/// mode                  steady or transient, so a consumer can tell a sample
///                       from a steady solution without the document
/// ```
struct CfdResultSource {
    ObjectId body{};
    MeshControlId control{};
    meshing::GeometryRevision geometry{};
    meshing::MeshStamp mesh{};
    MaterialId fluid{};
    std::uint64_t fluidRevision = 0;
    CfdAnalysisId analysis{};
    std::uint64_t analysisRevision = 0;
    CfdDefinitionFingerprint definition{};
    CfdAnalysisMode mode = CfdAnalysisMode::Steady;

    friend bool operator==(const CfdResultSource&, const CfdResultSource&) = default;
};

/// Which dependency of a result no longer matches the document.
enum class StaleReason : std::uint8_t {
    Body,
    Control,
    Geometry,
    Mesh,
    Fluid,
    Analysis,
    Definition,
    Mode,
};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(StaleReason reason) noexcept;

/// Every field of @p result that differs from @p current, in the enum's order.
/// Empty exactly when the two sources are equal.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::vector<StaleReason> staleReasons(const CfdResultSource& result,
                                                                        const CfdResultSource& current);

} // namespace bettercad::cfd
