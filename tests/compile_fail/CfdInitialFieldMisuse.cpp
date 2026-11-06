// Build-failure tests for the CFD initial field (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line and not
// to a missing header.
//
// WHAT THESE PROVE is ADR-068's structural half, and it is a narrower claim than
// the boundary group's. There is no naming and no attribution here; there is one
// uniform record and an array that belongs to the caller. Three things have to
// be impossible rather than merely undocumented.
//
// FIRST, THAT THE CFD MODULE OWNS NO FIELD STORAGE (ADR-068 Decisions 2 and 3).
// "Expanded cell arrays remain derived" is a structural claim: if a
// `PreparedCfdInitialField` could hand out a span of values it owns, or a
// `CfdAnalysisDefinition` could hold one, then a later persistence milestone
// would have a per-cell array in reach of canonical intent. The mirror
// static_assert in the unit tests fixes the member list by size; these cases fix
// it by NAME, which is the half a resized member would slip past.
//
// SECOND, THAT A PREPARED FIELD CANNOT BE CONJURED. Possession of one is the
// evidence that the record is finite, the layout exists with at least one cell,
// and the layout is the model's own generation. That is worth nothing if a
// consumer can default-construct one -- and unlike `PreparedCfdBoundary`, this
// type has NO default constructor at all, so there is no empty state to
// fabricate either.
//
// THIRD, THAT A LENGTH IS NOT A VARIABLE AND A VARIABLE IS NOT A NUMBER.
// `size()` is asked per VARIABLE precisely so a velocity array cannot be sized
// as though it were a pressure array by arithmetic a caller performs itself
// (ADR-066). An enumerator that converted to an integer, or a size that could be
// passed where a variable belongs, would give that arithmetic back.
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdInitialField.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::cfd;

namespace {

// EVERY ONE IS [[maybe_unused]], AND THAT IS NOT TIDYING. Each is called only
// from inside a `#if defined(BETTERCAD_CF_...)` block, so in the CONTROL
// configuration -- no macro, the build every preset runs -- they are unused, and
// this translation unit is compiled with -Werror.

// --- the canonical record holds nothing mesh-local ------------------------

#if defined(BETTERCAD_CF_A_RECORD_HOLDS_A_CELL_ARRAY)
// ADR-061: a per-cell field inside canonical intent would be mesh-local state
// promoted to authority, and a remesh would orphan it with no way to say so.
// ADR-068 Decision 8 keeps that forbidden under any spelling.
[[maybe_unused]] std::size_t perCellValues() {
    CfdInitialCondition initial;
    return initial.values.size();
}
#endif

#if defined(BETTERCAD_CF_A_RECORD_HOLDS_A_CELL_COUNT)
// A record that knew how many cells it covered would be bound to one mesh.
[[maybe_unused]] std::size_t cellsOfRecord() {
    CfdInitialCondition initial;
    return initial.cells;
}
#endif

#if defined(BETTERCAD_CF_A_RECORD_HOLDS_A_MESH_STAMP)
[[maybe_unused]] meshing::MeshStamp stampOfRecord() {
    CfdInitialCondition initial;
    return initial.mesh;
}
#endif

// --- the definition holds no derived field --------------------------------

#if defined(BETTERCAD_CF_A_DEFINITION_HOLDS_A_PREPARED_FIELD)
// ADR-068 Decision 2: a prepared field is never a member of anything canonical,
// so a remesh cannot orphan one inside a document and a save cannot persist it.
[[maybe_unused]] std::size_t preparedInDefinition(const CfdAnalysisDefinition& definition) {
    return definition.preparedInitialField.cells();
}
#endif

#if defined(BETTERCAD_CF_A_DEFINITION_HOLDS_AN_EXPANDED_ARRAY)
[[maybe_unused]] double expandedInDefinition(const CfdAnalysisDefinition& definition) {
    return definition.initialPressureValues[0];
}
#endif

// --- the prepared field owns no array -------------------------------------

#if defined(BETTERCAD_CF_A_PREPARED_FIELD_HOLDS_AN_ARRAY)
// THE CENTRAL CASE OF THIS FILE. If this compiled, the cfd module would own
// field memory and ADR-068 Decision 2 would be a comment rather than a fact.
[[maybe_unused]] std::span<const double> valuesOfField(const PreparedCfdInitialField& field) {
    return field.values();
}
#endif

#if defined(BETTERCAD_CF_A_PREPARED_FIELD_HOLDS_A_PRESSURE_SPAN)
[[maybe_unused]] std::span<const double> pressureOfField(const PreparedCfdInitialField& field) {
    return field.pressure();
}
#endif

#if defined(BETTERCAD_CF_A_PREPARED_FIELD_SUBSCRIPTS_A_CELL)
// Not a container: there is no value at a cell to read, because there are no
// values here at all.
[[maybe_unused]] double valueAtCell(const PreparedCfdInitialField& field) {
    return field[CellIndex::fromValue(0)];
}
#endif

// --- a prepared field cannot be fabricated --------------------------------

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_PREPARED_FIELD)
// There is NO default constructor, not merely a private one: no empty state is
// representable.
[[maybe_unused]] PreparedCfdInitialField fabricate() {
    return PreparedCfdInitialField{};
}
#endif

#if defined(BETTERCAD_CF_ASSEMBLE_A_PREPARED_FIELD_FROM_PARTS)
// The converting constructor is private, with one friend.
[[maybe_unused]] PreparedCfdInitialField assemble(CfdInitialFieldSource source, CfdFieldMap layout) {
    return PreparedCfdInitialField{std::move(source), std::move(layout)};
}
#endif

// --- what is read-only stays read-only ------------------------------------

#if defined(BETTERCAD_CF_THE_SOURCE_IS_WRITABLE)
// A binding a consumer can edit is a binding that can be made to agree with
// anything, which is the opposite of what it is for.
[[maybe_unused]] void editSource(const PreparedCfdInitialField& field) {
    field.source().cells = 1;
}
#endif

#if defined(BETTERCAD_CF_THE_RECORD_IS_WRITABLE)
[[maybe_unused]] void editRecord(const PreparedCfdInitialField& field) {
    field.initialCondition().pressure = Pressure::fromSi(1.0);
}
#endif

// --- a variable is not a number, and a number is not a variable -----------

#if defined(BETTERCAD_CF_A_VARIABLE_IS_AN_INTEGER)
[[maybe_unused]] int variableAsInteger() {
    return CfdVariable::Pressure;
}
#endif

#if defined(BETTERCAD_CF_A_LENGTH_IS_A_VARIABLE)
// `size()` is asked per VARIABLE so that a length cannot be computed by a
// caller and passed back in as though it named one.
[[maybe_unused]] std::size_t sizeByLength(const PreparedCfdInitialField& field) {
    return field.size(std::size_t{3});
}
#endif

#if defined(BETTERCAD_CF_A_MESH_HANDLE_SIZES_A_FIELD)
// No mesh handle subscripts or sizes anything here, which is ADR-066's rule
// carried forward rather than restated.
[[maybe_unused]] std::size_t sizeByHandle(const PreparedCfdInitialField& field) {
    return field.size(meshing::ElementId::fromValue(1));
}
#endif

#if defined(BETTERCAD_CF_A_LOCATION_IS_A_VARIABLE)
// A location has no component count, so sizing by one would drop the factor of
// three that makes a velocity array a velocity array.
[[maybe_unused]] std::size_t sizeByLocation(const PreparedCfdInitialField& field) {
    return field.size(CfdFieldLocation::Cell);
}
#endif

// --- the typed values do not mix ------------------------------------------

#if defined(BETTERCAD_CF_A_PRESSURE_IS_A_VELOCITY)
[[maybe_unused]] void pressureFromVelocity() {
    CfdInitialCondition initial;
    initial.pressure = initial.velocity.x;
}
#endif

#if defined(BETTERCAD_CF_A_STALE_REASON_IS_A_PROBLEM)
// Two enums over two different questions. A refusal is not a staleness.
[[maybe_unused]] CfdInitialFieldProblem reasonAsProblem() {
    return CfdInitialFieldStaleReason::Mesh;
}
#endif

#if defined(BETTERCAD_CF_A_CELL_INDEX_IS_A_CELL_VECTOR_INDEX)
// ADR-066's blocked numbering means `[0, cells)` and `[0, 3 cells)` are
// different index spaces, and a cell is not a slot of the second.
[[maybe_unused]] CellVectorIndex cellAsSlot() {
    return CellIndex::fromValue(0);
}
#endif

// --- the expansion must be able to write ----------------------------------

#if defined(BETTERCAD_CF_A_CONST_ARRAY_IS_EXPANDABLE)
// The whole point of the signature is that it WRITES into the caller's memory,
// so a read-only array cannot be offered to it.
[[maybe_unused]] Result<void> expandIntoConst(const PreparedCfdInitialField& field,
                                              std::span<const double> values) {
    return expandCfdInitialField(field, CfdVariable::Pressure, values);
}
#endif

}   // namespace
