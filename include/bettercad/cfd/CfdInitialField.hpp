#pragma once

// P19-IC-001 -- the CFD initial field: one uniform canonical record expanded
// into an array the CALLER owns (ADR-068).
//
// WHAT THIS ADDS, AND WHAT IT DELIBERATELY DOES NOT. `P19-DATA-001` already
// defined the canonical record, and this file does not declare it again:
//
// ```cpp
// struct CfdInitialCondition {
//     Velocity3 velocity{};
//     Pressure  pressure{};
// };
// ```
//
// It is a member of `CfdAnalysisDefinition`, `validate` already rejects a
// non-finite value, `canonicalText` already writes it into the definition
// fingerprint, and `core/Id.hpp` already records that there is no
// `CfdInitialConditionId` because there is one record per analysis. So the
// canonical half of this milestone was largely already there; what was missing
// is every step from that record to a particular mesh.
//
// THERE IS STILL NO FIELD STORAGE (ADR-068, Decision 2), and that is the whole
// shape of this file. ADR-066 Decision 1 deferred the ownership of field values
// to "the first milestone that computes a field, WITH A USE IN HAND". This is
// that milestone, and there is no use in hand: the only consumer of an initial
// field is a solve, and P19-STEADY-001, P19-TRANSIENT-001 and P19-LINSOLVE-001
// are all later and none is authorized. So the answer is that ownership stays
// with the consumer:
//
// ```cpp
// std::vector<double> p(layout.size(CfdVariable::Pressure));
// expandCfdInitialField(field, CfdVariable::Pressure, p);   // writes into p
// ```
//
// `PreparedCfdInitialField` HOLDS NO ARRAY. It holds the canonical record, the
// layout and the source binding. A mirror `static_assert` in the tests fixes its
// member list, so an added member breaks the build -- which is what makes
// "expanded cell arrays remain derived" a structural fact rather than a promise:
// the cfd module owns no field memory, so there is no field for a later edit to
// persist by accident.
//
// A VELOCITY EXPANDS COMPONENT-BLOCKED, `comp * cells + cell` (ADR-066), and
// this file does NOT perform that arithmetic. It asks
// `CfdFieldMap::vectorIndex` for every slot it writes, so one copy of the
// blocked numbering exists in this repository and it is `CfdFields.cpp`'s. A
// reader who expects P17's INTERLEAVING gets a different answer, which is the
// hazard ADR-066 Decision 3 recorded.
//
// A UNIFORM FIELD IS UNIFORM: GRAVITY DOES NOT ENTER IT (ADR-068, Decision 6),
// and the asymmetry with ADR-067 is deliberate rather than an omission. A
// prescribed BOUNDARY pressure is hydrostatic there, because it is a constraint
// the solve must satisfy along a face that is not level. An initial field is a
// starting guess whose only requirement is to be a legal state, the record is
// authorized UNIFORM (ADR-061), and making it hydrostatic would make density
// and gravity dependencies of a value they provably cannot change. The
// accepted consequence: a gravity-driven case starts further from its solution
// than it needs to, which is the solve's convergence problem and not a
// correctness problem here.
//
// MASS FLUX IS REFUSED BY NAME (ADR-068, Decision 5). `phi` is face-centred,
// and ADR-067's frozen table already makes it `Computed` at every boundary
// kind: it is an outcome of the velocity field, never a datum.
//
// WHAT THIS FILE DOES NOT DO. It implements no file format and no command.
// `P19-PERSIST-001` owns persisting initial conditions -- the words are in its
// own Persist block -- and `P19-CMD-001` owns "Undo exact" and "Redo exact".
// Both are later and neither is authorized. What this milestone does for those
// two is make them POSSIBLE AND TESTABLE: the canonical record is a pure value,
// `canonicalText` is LOSSLESS for it and consistent with equality, and the
// derived field is in neither the definition nor the fingerprint, so the
// canonical / derived boundary a persistence milestone must respect is already
// exact (ADR-068, Decision 9).
//
// THE LOSSLESS CLAIM IS ABOUT THE TEXT, NOT THE DIGEST, and the distinction is a
// review's. `canonicalText` writes each double with `std::format("{}", v)`, the
// shortest decimal that reads back as the same double, so it loses nothing.
// `fingerprint` is 64-bit FNV-1a OVER that text: a digest, which cannot be
// lossless and distinguishes two records only to the width of the hash. The
// tests assert distinct canonical TEXTS, which is where the strong claim lives.

#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/meshing/Mesh.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace bettercad::cfd {

class CfdModel;

// ---------------------------------------------------------------------------
// What can go wrong
// ---------------------------------------------------------------------------

/// Why an initial field cannot be prepared, or cannot be expanded into an
/// array.
///
/// DECLARATION ORDER IS PHASE ORDER, as `CfdBoundaryProblem`'s is: the first two
/// are decided while preparing, the last three while expanding. A test asserts
/// the earlier refusal wins when a call is faulty in two ways at once, so the
/// diagnostic a user sees is the first thing wrong and not whichever check
/// happened to run first.
enum class CfdInitialFieldProblem : std::uint8_t {
    /// The definition names a different mesh control than the model was built
    /// for, so this is the initial condition of another analysis.
    ///
    /// FIRST, BECAUSE IT IS THE MOST FUNDAMENTAL THING THAT CAN BE WRONG: if the
    /// caller handed over the wrong analysis entirely, complaining that its
    /// pressure is NaN answers the wrong question. An adversarial review found
    /// this check missing and gave the case -- re-point an analysis at another
    /// mesh control and the field reports itself CURRENT, for an analysis whose
    /// field must now cover a different number of cells. `definition.mesh` is
    /// canonical intent, it moves the fingerprint, and it provably changes the
    /// expanded array's LENGTH, so it is the one member of the definition that
    /// could not be omitted from the source binding with a reason.
    DefinitionNamesAnotherMesh,
    /// The canonical record holds a NaN or an infinity. `validate`'s refusal,
    /// not a second opinion: this milestone does not re-derive the rule.
    InitialConditionNotFinite,
    /// The field layout could not be built: the topology holds no cells, or it
    /// was derived from a different mesh generation than the model holds.
    /// `requireCfdFieldMap`'s refusal, passed through.
    LayoutUnavailable,
    /// An enumerator this build does not define.
    ///
    /// CHECKED BEFORE THE LENGTH, SO THE TAG NAMES THE VARIABLE AND NOT THE
    /// LENGTH. An earlier version of this comment claimed the order prevented a
    /// "silent no-op", on the reasoning that `size()` answers 0 for an
    /// unrecognised variable and so a zero-length array would pass a
    /// length-first check and succeed having written nothing. A review MEASURED
    /// that and it is false twice over: with the checks swapped, `(unknown, 0)`
    /// still reports this problem, and even with this check removed entirely the
    /// expansion's switch has no `default`, so an unrecognised enumerator falls
    /// out to a named refusal rather than to success. What the order actually
    /// buys is the honest tag: without it, `(unknown, 17)` would be reported as
    /// `ArrayLengthWrong`, blaming a length when the variable is the fault.
    VariableIsUnknown,
    /// The variable is not cell-centred, which today means `MassFlux`. A mass
    /// flux is an outcome of the velocity field (ADR-067's `Computed`), so
    /// initialising it would prescribe a value the solve derives.
    VariableIsNotCellCentred,
    /// The array is not `size(variable)` long. Both too short and too long: a
    /// longer array would leave a tail this call did not write, and a consumer
    /// that sized it from the wrong variable is the likely cause.
    ArrayLengthWrong,
};

/// The six, in declaration order.
inline constexpr std::array<CfdInitialFieldProblem, 6> kCfdInitialFieldProblems{
    CfdInitialFieldProblem::DefinitionNamesAnotherMesh,
    CfdInitialFieldProblem::InitialConditionNotFinite, CfdInitialFieldProblem::LayoutUnavailable,
    CfdInitialFieldProblem::VariableIsUnknown, CfdInitialFieldProblem::VariableIsNotCellCentred,
    CfdInitialFieldProblem::ArrayLengthWrong};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdInitialFieldProblem problem) noexcept;

/// A refusal and the tag that names it.
///
/// THE TAG EXISTS SO A TEST NEED NOT MATCH A MESSAGE, which is the lesson
/// P19-BC-001's adversarial review recorded: eight of its refusals could have
/// had their tags swapped for one another with every message assertion still
/// passing.
struct CfdInitialFieldFailure {
    CfdInitialFieldProblem problem{};
    Error error{};
};

// ---------------------------------------------------------------------------
// Source binding
// ---------------------------------------------------------------------------

/// Everything an expanded initial field depends on (ADR-068, Decision 7).
///
/// A VALUE, NOT A HASH: no pointer, no address, no timestamp. Equality is
/// field-complete by `= default`, so a dependency added here without being
/// compared is not possible, and `staleReasons` can say WHICH moved.
///
/// FOUR MEMBERS, AND THE OMISSIONS ARE THE DESIGN. The boundary's source
/// carries nine; this carries four, and each absence is a provable statement
/// about what cannot change the expanded array:
///
/// ```text
/// no geometry revision   the array depends on the layout and the values; a
///                        geometry edit that does not remesh changes neither.
///                        CfdFieldMap::currency does not compare the revision
///                        either, so carrying it here would let a field be
///                        stale against the very layout it was built from
/// no boundary faces      ADR-066 Decision 5: a cell field survives
/// no patch partition     BoundaryLayoutChanged. A field that staled on a patch
///                        reorder would claim a dependency it does not have
/// no fluid               density and viscosity do not enter a uniform field,
///                        which follows from Decision 6 and would be FALSE
///                        under the rejected hydrostatic option
/// no gravity             as above
/// no reference pressure  a datum fixes the LEVEL the solve converges to, not
///                        where it starts
/// ```
struct CfdInitialFieldSource {
    /// Which solid. The field's subject: a diagnostic that said "Mesh" for an
    /// analysis repointed at another body would mislead.
    ObjectId body{};
    /// WHICH MESH AND WHICH GENERATION. A remesh of the same body with the same
    /// counts has a different stamp, and that alone stales a prepared field.
    meshing::MeshStamp mesh{};
    /// A stamp identifies the BUILDER and not the snapshot -- the lesson
    /// `MeshDofMap::describes` learned -- so the count is compared as well. One
    /// count and not three: an initial field is cell-centred, so the face
    /// counts cannot change what it holds.
    std::size_t cells = 0;
    /// The canonical record it was prepared FROM, by value.
    CfdInitialCondition initialCondition{};

    friend bool operator==(const CfdInitialFieldSource&, const CfdInitialFieldSource&) = default;
};

/// Which dependency moved, in the enum's order.
enum class CfdInitialFieldStaleReason : std::uint8_t {
    Body,
    Mesh,
    InitialCondition,
};

/// The three, in declaration order. THREE AND NOT THE BOUNDARY'S SIX, for the
/// reasons tabulated on `CfdInitialFieldSource`.
inline constexpr std::array<CfdInitialFieldStaleReason, 3> kCfdInitialFieldStaleReasons{
    CfdInitialFieldStaleReason::Body, CfdInitialFieldStaleReason::Mesh,
    CfdInitialFieldStaleReason::InitialCondition};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdInitialFieldStaleReason reason) noexcept;

// ---------------------------------------------------------------------------
// The prepared initial field
// ---------------------------------------------------------------------------

/// One analysis's initial condition, bound to one mesh's field layout.
///
/// POSSESSION IS THE EVIDENCE: the record is finite, the layout exists and has
/// at least one cell, and the layout belongs to the model's own mesh
/// generation. There is no public constructor and one assembler, so a consumer
/// cannot be handed a partly prepared field.
///
/// AND NO EMPTY STATE IS REPRESENTABLE, which is a deliberate improvement on
/// `PreparedCfdBoundary`. That class has a private DEFAULT constructor and
/// fills itself in afterwards, so a default-constructed one is a legal object
/// that describes nothing. This one has a private CONVERTING constructor and no
/// default, which `CfdFieldMap` forces anyway -- its own default constructor is
/// private -- and the result is that every `PreparedCfdInitialField` that
/// exists is fully formed.
///
/// IT HOLDS NO ARRAY (ADR-068, Decisions 2 and 3). The expansion writes into
/// the caller's memory; see `expandCfdInitialField`.
class BETTERCAD_CFD_EXPORT PreparedCfdInitialField {
public:
    /// The canonical record this field was prepared from. Still uniform, still
    /// typed, still the only authority for an initial state.
    [[nodiscard]] const CfdInitialCondition& initialCondition() const noexcept {
        return source_.initialCondition;
    }

    /// The layout the expansion indexes through.
    [[nodiscard]] const CfdFieldMap& layout() const noexcept { return layout_; }

    /// How many cells this field covers. The LAYOUT's count, which is the
    /// authoritative one; `source().cells` carries the same number as
    /// corroboration and a test asserts the two agree.
    [[nodiscard]] std::size_t cells() const noexcept { return layout_.cells(); }

    /// How long an array of @p variable must be, or 0 for a variable this field
    /// cannot initialise -- including one it could not initialise anyway,
    /// because `MassFlux` is refused.
    ///
    /// NOT A SECOND LAYOUT CALCULATION: it forwards to `CfdFieldMap::size` for
    /// a cell-centred variable and answers 0 otherwise, so a caller cannot size
    /// an array for a variable the expansion will then refuse.
    [[nodiscard]] std::size_t size(CfdVariable variable) const noexcept;

    [[nodiscard]] const CfdInitialFieldSource& source() const noexcept { return source_; }

    friend bool operator==(const PreparedCfdInitialField&, const PreparedCfdInitialField&) = default;

private:
    PreparedCfdInitialField(CfdInitialFieldSource source, CfdFieldMap layout)
        : source_(std::move(source)), layout_(std::move(layout)) {}

    /// The one assembler. Possession is the evidence only because this is all
    /// there is.
    friend struct CfdInitialFieldAssembler;

    CfdInitialFieldSource source_;
    CfdFieldMap layout_;
};

/// Every dependency of @p field that differs from @p source, in the enum's
/// order. Empty if the prepared field still describes it.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::vector<CfdInitialFieldStaleReason>
staleReasons(const PreparedCfdInitialField& field, const CfdInitialFieldSource& source);

/// Whether @p field still describes @p source.
///
/// A consumer calls this BEFORE expanding or reading a value. Currentness is
/// never inferred from the size of anything.
///
/// IT DOES NOT SUBSUME THE MESH'S OWN CURRENCY, and a consumer needs both. A
/// geometry edit that does not remesh leaves this answering TRUE, correctly:
/// the array still describes the cells it was expanded for. Whether that mesh
/// still describes the geometry is `meshing`'s question and `CfdModel`'s gate,
/// exactly as it is for `CfdFieldMap`, whose `currency` also declines to
/// compare the geometry revision.
[[nodiscard]] BETTERCAD_CFD_EXPORT bool isCurrent(const PreparedCfdInitialField& field,
                                                  const CfdInitialFieldSource& source);

/// The source a prepared field would have for @p model, @p topology and
/// @p definition, so a consumer can compare without preparing again.
[[nodiscard]] BETTERCAD_CFD_EXPORT CfdInitialFieldSource
currentInitialFieldSource(const CfdModel& model, const CfdTopology& topology,
                          const CfdAnalysisDefinition& definition);

// ---------------------------------------------------------------------------
// Preparing one
// ---------------------------------------------------------------------------

/// Binds @p definition's initial condition to @p topology's field layout.
///
/// ATOMIC: one refusal refuses the whole field. There is no partially prepared
/// initial state, because a field that describes some of the cells is not a
/// smaller model, it is a model that claims to be the user's and is not.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<PreparedCfdInitialField>
prepareCfdInitialField(const CfdModel& model, const CfdTopology& topology,
                       const CfdAnalysisDefinition& definition);

/// The tagged refusal `prepareCfdInitialField` would give, or none if it would
/// succeed.
///
/// THE SAME CODE PATH, not a second opinion: `prepareCfdInitialField` is
/// implemented in terms of this, so the two cannot drift. A test asserts they
/// agree on every case the suite presents.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdInitialFieldFailure>
cfdInitialFieldFailure(const CfdModel& model, const CfdTopology& topology,
                       const CfdAnalysisDefinition& definition);

/// Just the tag, for a test that should not depend on a message's wording.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdInitialFieldProblem>
cfdInitialFieldProblem(const CfdModel& model, const CfdTopology& topology,
                       const CfdAnalysisDefinition& definition);

// ---------------------------------------------------------------------------
// Expanding one into the caller's array
// ---------------------------------------------------------------------------

/// Writes the initial values of @p variable into @p values, which the CALLER
/// owns and sizes from `field.size(variable)`.
///
/// ```text
/// Pressure   values[cell]                        = pressure, in Pa
/// Velocity   values[comp * cells + cell]         = velocity[comp], in m/s
/// MassFlux   refused: face-centred, and an outcome
/// ```
///
/// EVERY SLOT IS WRITTEN, OR NONE IS AND THE ARRAY IS UNTOUCHED. Every
/// reachable refusal -- the variable and the length -- is decided before the
/// first write, which a test asserts on a pre-poisoned array, because a partial
/// write would leave a consumer holding a field initialised in part and garbage
/// in the rest. The one exception is the UNREACHABLE internal error inside the
/// velocity loop, which returns after writing the cells before it; the sentence
/// is qualified rather than absolute because a review pointed out that the code
/// is not, even on a path nothing can reach.
///
/// SI AT THIS BOUNDARY, which is where the conversion belongs: the canonical
/// record carries `Pressure` and `Velocity3`, and the array carries `double` in
/// Pa and m/s. No other site converts.
///
/// THE BLOCKED SUBSCRIPT COMES FROM THE LAYOUT. This function calls
/// `CfdFieldMap::vectorIndex` for every component slot rather than computing
/// `comp * cells + cell`, so there is one copy of that arithmetic and it is not
/// here.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void>
expandCfdInitialField(const PreparedCfdInitialField& field, CfdVariable variable,
                      std::span<double> values);

/// The tagged refusal an expansion of @p variable into an array of @p length
/// would give, or none if it would succeed.
///
/// TAKES A LENGTH AND NOT A SPAN, so a test can ask about a length without
/// owning memory of it -- including lengths no machine would allocate.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdInitialFieldFailure>
cfdInitialFieldExpansionFailure(const PreparedCfdInitialField& field, CfdVariable variable,
                                std::size_t length);

/// Just the tag.
[[nodiscard]] BETTERCAD_CFD_EXPORT std::optional<CfdInitialFieldProblem>
cfdInitialFieldExpansionProblem(const PreparedCfdInitialField& field, CfdVariable variable,
                                std::size_t length) noexcept;

}   // namespace bettercad::cfd
