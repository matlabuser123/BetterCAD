#pragma once

// P19-DATA-001 -- the derived CFD result, the one place that decides whether it
// still describes the model, the structured failure of an attempt, and the
// lifecycle of what a solver service holds for one analysis.
//
// A result is DERIVED STATE in exactly the sense ADR-030 gave the word for a
// mesh and P17 and P18 gave it for their results: reproducible from canonical
// intent, never persisted as authority, regenerated rather than restored. What
// makes that safe is that a result carries the identity of everything it was
// computed from, so it can say "I am not about this document any more" instead
// of being quietly read as though it were.
//
// CURRENTNESS COMPARES SOURCE IDENTITY, NEVER ARRAYS. Two results with identical
// velocities on two generations of a mesh are a current one and a stale one.
// `resultCurrency` and `analysisState` are the only currentness answers in P19;
// a viewer that compared cell counts and a CLI that compared an analysis id
// would be two answers that drift.
//
// WHAT A RESULT HELD UNTIL P19-STEADY-001: ITS PROVENANCE AND ITS METADATA, AND
// NOT YET ITS FIELDS (ADR-061). The fields of a finite-volume solution are p and
// U per cell and phi per face, keyed by CellIndex and FaceIndex -- types
// P19-TOPO-001 and P19-DOF-001 define from a face enumeration that does not exist
// yet. A container with p and U but no phi would claim to be a solution and not
// be one (its mass balance could not be checked), and keying p by a tetrahedron's
// position would decide the numbering of the field maps here by accident. So the
// container is defined complete in everything but the arrays.
//
// P19-DOF-001 DEFINED THAT NUMBERING AND DELIBERATELY DID NOT ADD THE ARRAYS,
// which this paragraph said it would (ADR-066, Decision 1). Its authorization is a
// checklist about locations, indexing, binding and invalidation, and the arrays are
// not on it; and nothing in the tree yet PRODUCES a field, so arrays added there
// would have been validated against no producer, with `CfdResult::create` taking a
// topology to check their lengths for a caller that does not exist. They arrive
// with the first milestone that computes a field, keyed by `CfdFieldMap`'s index
// space, and `CfdData_AResultHoldsProvenanceAndMetadataOnly`'s mirror is where the
// addition is stated.
//
// THE ARRAYS ARE HERE NOW, AND THE TWO PARAGRAPHS ABOVE ARE KEPT RATHER THAN
// DELETED BECAUSE THE DEFERRAL IS PART OF THE RECORD (P19-STEADY-001, ADR-077
// Decision 2). `solveCfdSteady` is that producer, `CfdResultSlot`'s first in the
// tree, so the cost ADR-066 declined to pay for a caller that did not exist is now
// paid for one that does: `create` takes a `CfdFieldMap` and `CfdResultFields`, and
// the mirror that asserted this container's size -- which is what stopped the build
// and forced this paragraph to be written -- is extended and renamed.
//
// WHAT CHANGED THAT THE DEFERRAL DID NOT FORESEE: nothing. Both reasons it gave
// have expired together, which is why the arrays arrive in one step rather than
// by halves. The alternative was a result that publishes provenance while no code
// anywhere can read `p`, so `analysisState` would report SolvedCurrent with no
// solution in the tree -- the limit of this file's own "a partially populated
// result is not a worse result, it is a claim that is false".

#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::features {
class Regenerator;
} // namespace bettercad::features

namespace bettercad::cfd {

/// What an accepted solve reports about itself, beyond its fields.
///
/// ```text
/// outerIterations   the outer iterations of the steady solve, or of the step
///                   that produced this transient sample; at least 1
/// residuals         the normalised residuals at the last iteration
/// massBalance       ADR-058's two measures, relative to the throughput
/// time              NONE for steady; the sample time, finite and > 0, for a
///                   transient sample (t = 0 is the initial condition, not a
///                   solve)
/// step              NONE for steady; the 1-based step index of the sample
/// ```
///
/// CONVERGENCE IS RECORDED, NOT RE-JUDGED HERE. Whether these numbers are good
/// enough is validation's question (P19-VALID-001); the container refuses only
/// what cannot be a measurement at all.
struct CfdResultMetadata {
    std::uint32_t outerIterations = 0;
    CfdResiduals residuals{};
    CfdMassBalance massBalance{};
    std::optional<Time> time{};
    std::optional<std::uint32_t> step{};

    friend bool operator==(const CfdResultMetadata&, const CfdResultMetadata&) = default;
};

/// The three fields of a finite-volume solution, keyed by `ADR-066`'s index
/// space and no other.
///
/// ```text
/// pressure   p,   one per cell,            Pa,   size(CfdVariable::Pressure)
/// velocity   U,   component-blocked,       m/s,  size(CfdVariable::Velocity)
/// massFlux   phi, one per face, signed
///                 out of the owner,        kg/s, size(CfdVariable::MassFlux)
/// ```
///
/// POSSESSION IS THE EVIDENCE (`ADR-036`). There is no default constructor and
/// no setter, so holding one of these proves its three lengths were checked
/// against a `CfdFieldMap` and that every value in it is finite. That is the
/// same reason `CfdSparseMatrix` keeps its default constructor private, and it
/// is why `CfdResult` can take these by value without re-validating them.
///
/// ALL THREE OR NONE. `CfdResult.hpp`'s own long-standing reason: "A container
/// with p and U but no phi would claim to be a solution and not be one (its
/// mass balance could not be checked)." The factory takes all three and there
/// is no way to supply two.
class BETTERCAD_CFD_EXPORT CfdResultFields {
public:
    /// Builds the field set, or fails saying which length or value broke:
    ///
    /// ```text
    /// the layout describes at least one cell
    /// each array's length == layout.size(variable), named individually
    /// every value finite -- the first offending index is in the diagnostic
    /// ```
    ///
    /// THE FINITENESS CHECK IS A TYPE INVARIANT AND NOT A REDUNDANT GUARD.
    /// A published result must not be able to carry a NaN whatever produced it,
    /// and that stands on its own without reference to any particular caller.
    ///
    /// THIS PARAGRAPH PREVIOUSLY GAVE A FALSE REASON, and an adversarial review
    /// found it: it said "`solveCfdSteady` rejects a non-finite field as
    /// `CfdFailureStage::NonFiniteField` before it ever reaches here". That
    /// check was removed as unreachable (`ADR-077` Decision 4) and the sentence
    /// outlived it, so a reader auditing this guard was sent to code that does
    /// not exist. What actually makes it unreachable from `solveCfdSteady` is
    /// one layer further down: `applyCfdCouplingCorrection` refuses non-finite
    /// corrected fields as `CfdCouplingProblem::ResultIsNotFinite`, so
    /// `advanceCfdCoupling` never hands one back. Every other caller can still
    /// fire it, which is why it is here.
    [[nodiscard]] static Result<CfdResultFields> create(const CfdFieldMap& layout, std::vector<double> pressure,
                                                       std::vector<double> velocity, std::vector<double> massFlux);

    [[nodiscard]] std::span<const double> pressure() const noexcept { return pressure_; }
    [[nodiscard]] std::span<const double> velocity() const noexcept { return velocity_; }
    [[nodiscard]] std::span<const double> massFlux() const noexcept { return massFlux_; }

    /// The counts the arrays were validated against, read back off the arrays
    /// rather than stored twice.
    [[nodiscard]] std::size_t cells() const noexcept { return pressure_.size(); }
    [[nodiscard]] std::size_t faces() const noexcept { return massFlux_.size(); }

    /// Value equality over the bits, for the determinism tests. Compares the
    /// arrays, which is exactly what a determinism claim is about.
    friend bool operator==(const CfdResultFields&, const CfdResultFields&) = default;

private:
    CfdResultFields(std::vector<double> pressure, std::vector<double> velocity, std::vector<double> massFlux) noexcept;

    std::vector<double> pressure_{};
    std::vector<double> velocity_{};
    std::vector<double> massFlux_{};
};

/// The accepted solution of one CFD analysis on one mesh, at one time level.
///
/// IMMUTABLE, AND CONSTRUCTED ONLY BY A VALIDATING FACTORY. Every accessor is
/// const; there is no setter and no way to reach the source and rewrite it. A
/// result whose provenance could be edited would let stale output be made to
/// look current without recomputing anything.
///
/// THE MESH IS NOT COPIED IN: a result records the `MeshStamp` of the mesh it
/// belongs to.
///
/// ITS SIZE DID NOT DEPEND ON THE MESH UNTIL P19-STEADY-001, AND NOW IT DOES.
/// That sentence used to end "and its size does not depend on the mesh", which
/// was true while the container held provenance and metadata only. It holds its
/// fields since `ADR-077` Decision 2, so `pressure().size() == cells` by
/// construction. The first clause is untouched: no mesh is copied in, and a
/// result is still compared to one by stamp and never by count.
class BETTERCAD_CFD_EXPORT CfdResult {
public:
    /// Builds a result, or fails saying which invariant it broke:
    ///
    /// ```text
    /// the source names this exact mesh        source.mesh == mesh stamp
    /// the layout describes this exact mesh    layout.mesh() == mesh stamp
    /// the mesh is not empty
    /// the fields were built against THIS layout, by cell count
    /// at least one outer iteration
    /// residuals and mass balance finite and not negative
    /// steady:    no time, no step
    /// transient: a finite time > 0 s and a step >= 1
    /// ```
    ///
    /// THE LAYOUT IS TAKEN AS WELL AS THE MESH, AND BOTH ARE CHECKED AGAINST
    /// THE SOURCE. `CfdResultFields` already proves its lengths match SOME
    /// layout; what this adds is that the layout is the one this result claims,
    /// which no field set can prove about itself. Two stamps and one count is
    /// cheaper than re-deriving a topology here.
    ///
    /// Only an accepted solve calls this. A failed one records a
    /// `CfdSolveFailure` instead; a partially populated result is not a worse
    /// result, it is a claim that is false.
    [[nodiscard]] static Result<CfdResult> create(CfdResultSource source, const meshing::VolumeMesh& mesh,
                                                  const CfdFieldMap& layout, CfdResultMetadata metadata,
                                                  CfdResultFields fields);

    /// What this was computed from. There is no non-const overload, by design.
    [[nodiscard]] const CfdResultSource& source() const noexcept { return source_; }
    [[nodiscard]] const CfdResultMetadata& metadata() const noexcept { return metadata_; }

    /// The solution itself. `ADR-077` Decision 2.
    [[nodiscard]] const CfdResultFields& fields() const noexcept { return fields_; }

    /// Whether this result belongs to @p mesh. Compares stamps, never counts.
    [[nodiscard]] bool describes(const meshing::VolumeMesh& mesh) const noexcept;

private:
    CfdResult(CfdResultSource source, CfdResultMetadata metadata, CfdResultFields fields) noexcept;

    CfdResultSource source_{};
    CfdResultMetadata metadata_{};
    CfdResultFields fields_;
};

// ---------------------------------------------------------------------------
// The failed attempt
// ---------------------------------------------------------------------------

/// Where a solve attempt stopped (ADR-058 "Failure"). Every stage is a named
/// failure; none publishes anything.
enum class CfdFailureStage : std::uint8_t {
    /// The inputs were refused: a document, mesh or fluid gate, an unresolved
    /// boundary reference, a pressure datum missing, unbalanced prescribed flux.
    InputRefused,
    /// The mesh was refused for finite-volume use (P19-MESH-001's measures).
    MeshRefused,
    /// The assembled system is not well posed.
    IllPosed,
    /// A linear solve inside the iteration failed or missed its tolerance.
    LinearSolveFailed,
    /// A residual grew past the divergence factor.
    Diverged,
    /// The iteration budget ran out before convergence.
    IterationLimitReached,
    /// A field value became NaN or infinite.
    NonFiniteField,
    /// Continuity was not closed.
    ContinuityNotClosed,
    /// The mass balance failed validation.
    MassBalanceFailed,
};

/// The nine, in declaration order.
inline constexpr std::array<CfdFailureStage, 9> kCfdFailureStages{
    CfdFailureStage::InputRefused,      CfdFailureStage::MeshRefused,
    CfdFailureStage::IllPosed,          CfdFailureStage::LinearSolveFailed,
    CfdFailureStage::Diverged,          CfdFailureStage::IterationLimitReached,
    CfdFailureStage::NonFiniteField,    CfdFailureStage::ContinuityNotClosed,
    CfdFailureStage::MassBalanceFailed};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdFailureStage stage) noexcept;

/// The latest failed attempt, as structured data rather than a string.
///
/// ```text
/// stage            where it stopped
/// error            the diagnostic, naming the specific reason
/// source           what the attempt was computed against, when its inputs
///                  resolved far enough to say; NONE for an input refusal
/// outerIterations  how far the iteration got, when it started
/// ```
///
/// A failure is NEVER A RESULT: it has no fields and no metadata to be read as
/// a solution, and holding one never makes a result current.
struct CfdSolveFailure {
    CfdFailureStage stage = CfdFailureStage::InputRefused;
    Error error{};
    std::optional<CfdResultSource> source{};
    std::optional<std::uint32_t> outerIterations{};

    friend bool operator==(const CfdSolveFailure&, const CfdSolveFailure&) = default;
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

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(ResultCurrency currency) noexcept;

/// Whether @p currency means the result may be read as describing the model.
/// Only `Current` does. No export macro: an inline function needs none.
[[nodiscard]] constexpr bool describesTheModel(ResultCurrency currency) noexcept {
    return currency == ResultCurrency::Current;
}

/// The source a result computed NOW would carry.
///
/// Fails for the reasons `requireCfdModel` fails, because it calls it: every
/// gate ADR-060 established is inherited rather than re-asked. It ALSO fails
/// when the definition's pressure datum is missing (`pressureDatumProblem`), the
/// one input problem a definition can carry while being storable. It then reads
/// the fluid's identity and revision (from the `CfdFluid` the boundary resolved,
/// which requires a usable density AND viscosity since P19-FLUID-001), the
/// analysis and its revision, and the fingerprint of the definition as it stands.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdResultSource>
currentResultSource(const Document& document, const features::Regenerator& regenerator,
                    const meshing::Mesher& mesher, CfdAnalysisId analysis);

/// Compares @p result's provenance against @p current. Identity only; no
/// array, no count, no value is read.
[[nodiscard]] BETTERCAD_CFD_EXPORT ResultCurrency resultCurrency(const CfdResult* result,
                                                                const CfdResultSource& current);

/// Where one CFD analysis stands.
///
/// DERIVED, NEVER STORED. Every value is a function of the document, the mesh
/// state and what is held for the analysis; there is no cached flag.
enum class AnalysisState : std::uint8_t {
    /// The document has no CFD analysis with that id.
    NoAnalysis,
    /// The analysis is defined but its inputs do not resolve: the geometry is
    /// ineligible, no mesh is current, the fluid is missing or unusable
    /// (`cfdInputProblem` names which), or nothing fixes the pressure
    /// (`pressureDatumProblem`).
    InputsUnavailable,
    /// Every input resolves and nothing has been solved yet.
    Ready,
    /// A result exists and every dependency matches.
    SolvedCurrent,
    /// A result exists and at least one dependency moved. Readable as what the
    /// model WAS, never as what it is.
    SolvedStale,
    /// The last attempt failed. Its own state whether or not an older result
    /// survives, and it outranks every state but `NoAnalysis`: the ANALYSIS is not solved-current
    /// while its latest attempt failed, even when the surviving result's source
    /// still equals the current one (`resultCurrency` then says Current, which is
    /// a statement about that result's inputs, not about the analysis). The state
    /// stays until a success or a reset; the failure's `source` is diagnostic and
    /// is not compared. The same rule as P18's `AnalysisState::SolveFailed`.
    SolveFailed,
};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(AnalysisState state) noexcept;

/// Evaluates the state of @p analysis from what a solver service holds for it.
/// `CfdResultSlot`'s two accessors are the intended arguments. Deterministic:
/// no wall clock, no pointer identity, no unordered iteration.
[[nodiscard]] BETTERCAD_CFD_EXPORT AnalysisState analysisState(const Document& document,
                                                              const features::Regenerator& regenerator,
                                                              const meshing::Mesher& mesher, CfdAnalysisId analysis,
                                                              const CfdResult* lastResult,
                                                              const CfdSolveFailure* lastFailure);

// ---------------------------------------------------------------------------
// The lifecycle
// ---------------------------------------------------------------------------

/// What a solver service holds for one analysis: the current accepted result
/// (at most one) and the latest failed attempt (at most one).
///
/// ```text
/// recordSuccess(result)   the result replaces whatever was held; the failure
///                         is cleared. The ONLY way a result becomes held.
/// recordFailure(failure)  the failure is held; the result, if any, is KEPT
///                         UNTOUCHED -- not discarded, not re-stamped -- so a
///                         failed attempt can neither erase what the model was
///                         nor make an old result current.
/// reset()                 nothing held.
/// ```
///
/// Nothing here consults the document: currentness is `analysisState`'s to
/// decide, and the slot never caches it.
class BETTERCAD_CFD_EXPORT CfdResultSlot {
public:
    CfdResultSlot() = default;

    /// The last accepted result, or nullptr. Current or stale is not the slot's
    /// to say.
    [[nodiscard]] const CfdResult* result() const noexcept { return result_.has_value() ? &*result_ : nullptr; }

    /// The latest failed attempt, or nullptr if the last attempt succeeded or
    /// none was made.
    [[nodiscard]] const CfdSolveFailure* failure() const noexcept {
        return failure_.has_value() ? &*failure_ : nullptr;
    }

    void recordSuccess(CfdResult result);
    void recordFailure(CfdSolveFailure failure);
    void reset() noexcept;

private:
    std::optional<CfdResult> result_{};
    std::optional<CfdSolveFailure> failure_{};
};

} // namespace bettercad::cfd
