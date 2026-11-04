// Build-failure tests for the CFD field layout (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which must
// compile -- so every failure below is attributable to its own line and not to a
// missing header.
//
// WHAT THESE PROVE is P19-DOF-001's structural half (ADR-066). The topology's
// cases proved that its four SUBSCRIPTS do not mix and the geometry's that the
// QUANTITIES do not; these prove the two claims a field layout rests on.
//
// FIRST, THAT NO MESH HANDLE CAN SUBSCRIPT A FIELD. P16 is explicit that handles
// may be sparse, so a field indexed by `ElementId::value()` is wrong the moment
// the allocator changes -- and the wrongness is invisible, because both are
// integers of the same type underneath. ADR-064 made the compact index a strong
// type for that reason, and this group proves the conversions do not exist in
// either direction for the subscript this milestone adds.
//
// SECOND, AND THE REASON THIS GROUP MATTERS MORE THAN ITS SIBLINGS: a
// `CellVectorIndex` and a `CellIndex` COINCIDE NUMERICALLY for component X
// (ADR-066, Decision 3 -- component-blocked layout puts X's block first, so
// `vectorIndex(cell, X).value() == cell.value()`). Nothing about the VALUES
// distinguishes a cell from its own X component, so "the type system separates
// them" cannot rest on a test that compares numbers. It rests on these cases.
//
// AND THREE ABSENCES ARE PROVED HERE, because an ADR saying a thing does not
// exist is not the same as the compiler saying so: the layout holds NO field
// (Decision 1 -- it is arithmetic, not storage), offers NO mesh handle
// (the checklist's "no raw mesh-ID indexing", made structural), and the canonical
// analysis definition has NO member for a layout (Decision 7).
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/units/Dimension.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <cstddef>
#include <optional>

using namespace bettercad;
using namespace bettercad::cfd;

namespace {

// EVERY ONE IS [[maybe_unused]], AND THAT IS NOT TIDYING. Each is called only
// from inside a `#if defined(BETTERCAD_CF_...)` block, so in the CONTROL
// configuration -- no macro, the build every preset runs -- they are unused
// anonymous-namespace functions and `-Wunused -Werror` makes each one a hard
// error. That is exactly the defect an adversarial review found in the cfdgeom
// group of P19-GEOM-001, where a green `ctest -R compile_fail.cfdgeom.` was
// consistent with a project that did not build, because the control is a build
// target and not a registered test.
[[maybe_unused]] void takesCell(CellIndex /*unused*/) {}
[[maybe_unused]] void takesCellVector(CellVectorIndex /*unused*/) {}
[[maybe_unused]] void takesFace(FaceIndex /*unused*/) {}
[[maybe_unused]] void takesBoundaryFace(BoundaryFaceIndex /*unused*/) {}
[[maybe_unused]] void takesPatch(PatchIndex /*unused*/) {}
[[maybe_unused]] void takesElement(meshing::ElementId /*unused*/) {}
[[maybe_unused]] void takesSize(std::size_t /*unused*/) {}
[[maybe_unused]] void takesDouble(double /*unused*/) {}
[[maybe_unused]] void takesBool(bool /*unused*/) {}
[[maybe_unused]] void takesLocation(CfdFieldLocation /*unused*/) {}
[[maybe_unused]] void takesVariable(CfdVariable /*unused*/) {}

/// What a consumer of a layout receives. Never called: these cases are about what
/// it CANNOT do, which the compiler decides without running it.
[[maybe_unused]] void reads(const CfdFieldMap& map, const CfdTopology& topology) {
    const CellIndex cell = CellIndex::fromValue(0);
    const FaceIndex face = FaceIndex::fromValue(0);
    const BoundaryFaceIndex boundary = BoundaryFaceIndex::fromValue(0);
    const PatchIndex patch = PatchIndex::fromValue(0);
    const CellVectorIndex component = CellVectorIndex::fromValue(0);
    // Used only inside the case blocks, so the CONTROL would warn on them.
    [[maybe_unused]] const meshing::ElementId element = meshing::ElementId::fromValue(1);
    [[maybe_unused]] const meshing::NodeId node = meshing::NodeId::fromValue(1);

    // The accessors, used correctly, so the control exercises every signature the
    // cases below misuse.
    [[maybe_unused]] const std::size_t cells = map.size(CfdFieldLocation::Cell);
    [[maybe_unused]] const std::size_t velocities = map.size(CfdVariable::Velocity);
    [[maybe_unused]] const std::optional<CellVectorIndex> subscript = map.vectorIndex(cell, VectorComponent::X);
    [[maybe_unused]] const std::optional<CellIndex> owner = map.cellOf(component);
    [[maybe_unused]] const std::optional<VectorComponent> which = map.componentOf(component);
    [[maybe_unused]] const std::optional<FaceIndex> slot = map.faceSlotOf(boundary);
    [[maybe_unused]] const std::optional<BoundaryFaceIndex> back = map.boundarySlotOf(face);
    [[maybe_unused]] const std::optional<PatchIndex> group = map.patchOf(boundary);
    [[maybe_unused]] const std::optional<CfdPatchFieldRange> range = map.patchRange(patch);
    [[maybe_unused]] const std::optional<std::size_t> cadFace = map.patchFace(patch);
    [[maybe_unused]] const CfdFieldCurrency currency = map.currency(topology);
    [[maybe_unused]] const bool internal = map.isInternalSlot(face);
    [[maybe_unused]] const std::optional<CfdFieldDescriptor> descriptor = descriptorOf(CfdVariable::Pressure);

    // ---- no mesh handle can subscript a field ----------------------------

#if defined(BETTERCAD_CF_AN_ELEMENT_ID_IS_NOT_A_CELL_INDEX)
    // THE DEFECT THE COMPACT INDEX EXISTS FOR. A mesh may hand out element
    // 900000 for its fourth tetrahedron, so a field sized or subscripted by the
    // handle is wrong and looks right.
    takesCell(element);
#endif

#if defined(BETTERCAD_CF_A_CELL_INDEX_IS_NOT_AN_ELEMENT_ID)
    takesElement(cell);
#endif

#if defined(BETTERCAD_CF_AN_ELEMENT_ID_IS_NOT_A_CELL_VECTOR_INDEX)
    takesCellVector(element);
#endif

#if defined(BETTERCAD_CF_A_NODE_ID_IS_NOT_A_CELL_INDEX)
    // A node handle either: a cell-centred field has one value per CELL, and a
    // Tet4 mesh has about a fifth as many cells as it has nodes.
    takesCell(node);
#endif

#if defined(BETTERCAD_CF_AN_ELEMENT_ID_CANNOT_BE_ASKED_OF_THE_LAYOUT)
    // The layout answers `contains` for its four subscripts and for nothing else.
    // The translation to a mesh handle stays in CfdTopology, where it is a binary
    // search over a sorted vector and not an index.
    takesBool(map.contains(element));
#endif

    // ---- the cell and its own X component do not interconvert ------------

#if defined(BETTERCAD_CF_A_CELL_INDEX_IS_NOT_A_CELL_VECTOR_INDEX)
    // ADR-066, Decision 3: these two COINCIDE for component X, so nothing about
    // the numbers separates them. This case is the separation.
    takesCellVector(cell);
#endif

#if defined(BETTERCAD_CF_A_CELL_VECTOR_INDEX_IS_NOT_A_CELL_INDEX)
    takesCell(component);
#endif

#if defined(BETTERCAD_CF_A_CELL_VECTOR_INDEX_IS_NOT_AN_INTEGER)
    takesSize(component);
#endif

#if defined(BETTERCAD_CF_AN_INTEGER_IS_NOT_A_CELL_VECTOR_INDEX)
    takesCellVector(0);
#endif

#if defined(BETTERCAD_CF_PASSING_A_CELL_WHERE_A_VECTOR_SUBSCRIPT_IS_WANTED)
    // `cellOf` translates a vector subscript back to its cell; handing it the cell
    // would be the identity a caller might expect from P17's interleaving.
    (void)map.cellOf(cell);
#endif

#if defined(BETTERCAD_CF_PASSING_A_VECTOR_SUBSCRIPT_WHERE_A_CELL_IS_WANTED)
    (void)map.vectorIndex(component, VectorComponent::X);
#endif

    // ---- the four subscripts do not mix at the layout's own accessors ----

#if defined(BETTERCAD_CF_A_FACE_INDEX_IS_NOT_A_BOUNDARY_FIELD_SLOT)
    // The boundary range's slots are counted from the start of the BOUNDARY, not
    // of the face array, which is why the translation is not the caller's to do.
    (void)map.faceSlotOf(face);
#endif

#if defined(BETTERCAD_CF_A_BOUNDARY_FIELD_SLOT_IS_NOT_A_FACE_INDEX)
    (void)map.boundarySlotOf(boundary);
#endif

#if defined(BETTERCAD_CF_A_PATCH_INDEX_IS_NOT_A_BOUNDARY_FIELD_SLOT)
    (void)map.patchOf(patch);
#endif

#if defined(BETTERCAD_CF_A_BOUNDARY_FIELD_SLOT_IS_NOT_A_PATCH_INDEX)
    (void)map.patchRange(boundary);
#endif

#if defined(BETTERCAD_CF_A_CELL_INDEX_IS_NOT_A_FACE_INDEX_FOR_A_SLOT)
    takesFace(cell);
#endif

#if defined(BETTERCAD_CF_A_FACE_INDEX_IS_NOT_A_CELL_INDEX_FOR_A_SLOT)
    takesCell(face);
#endif

    // ---- the enumerations do not mix -------------------------------------

#if defined(BETTERCAD_CF_A_VARIABLE_IS_NOT_A_LOCATION)
    // `size` is overloaded on both, and the two answers differ: a velocity array
    // is three times its location's length.
    takesLocation(CfdVariable::Velocity);
#endif

#if defined(BETTERCAD_CF_A_LOCATION_IS_NOT_A_VARIABLE)
    takesVariable(CfdFieldLocation::Cell);
#endif

#if defined(BETTERCAD_CF_A_COMPONENT_IS_NOT_AN_INTEGER)
    takesSize(VectorComponent::X);
#endif

#if defined(BETTERCAD_CF_AN_INTEGER_IS_NOT_A_COMPONENT)
    (void)map.vectorIndex(cell, 0);
#endif

#if defined(BETTERCAD_CF_A_CURRENCY_IS_NOT_A_BOOL)
    // THREE VALUES AND NOT TWO (ADR-066, Decision 5): a layout can be current for
    // its cells and stale for its boundary, so a verdict read as a bool would
    // collapse the partial case into one of the totals.
    takesBool(map.currency(topology));
#endif

#if defined(BETTERCAD_CF_A_DIMENSION_IS_NOT_A_BARE_NUMBER)
    takesDouble(descriptorOf(CfdVariable::Pressure)->dimension);
#endif

    // ---- the layout holds no field, and offers no handle ------------------

#if defined(BETTERCAD_CF_THE_LAYOUT_HAS_NO_PRESSURE_FIELD)
    // ADR-066, Decision 1: counts, translations and a binding. No values.
    takesDouble(map.pressure(cell));
#endif

#if defined(BETTERCAD_CF_THE_LAYOUT_HAS_NO_VELOCITY_FIELD)
    takesDouble(map.velocity(cell));
#endif

#if defined(BETTERCAD_CF_THE_LAYOUT_HAS_NO_MASS_FLUX)
    takesDouble(map.massFlux(face));
#endif

#if defined(BETTERCAD_CF_THE_LAYOUT_OFFERS_NO_ELEMENT_HANDLE)
    // The checklist's "no raw mesh-ID indexing", made structural: there is no
    // accessor here that hands one back, so a consumer cannot be tempted.
    takesElement(map.elementOf(cell));
#endif

#if defined(BETTERCAD_CF_THE_LAYOUT_HAS_NO_FLUID)
    // A layout is not a function of the fluid, which is why a material edit
    // leaves it equal (ADR-066, Decision 5).
    takesDouble(map.density());
#endif

#if defined(BETTERCAD_CF_THE_LAYOUT_CANNOT_BE_REBOUND_IN_PLACE)
    // Derived state is rebuilt, never re-pointed: there is no setter, so a layout
    // cannot be made to describe a mesh it was not built from.
    map.setMesh(topology.mesh());
#endif

#if defined(BETTERCAD_CF_THE_PATCH_RANGE_HAS_NO_LAST_SLOT)
    // A range is a first slot and a COUNT, so an empty patch is representable
    // without a past-the-end slot that would name another patch's value.
    takesBoundaryFace(map.patchRange(patch)->last);
#endif
}

/// What the canonical analysis definition cannot hold.
[[maybe_unused]] void canonical(const CfdAnalysisDefinition& definition) {
    [[maybe_unused]] const MeshControlId control = definition.mesh;

#if defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_FIELD_MAP)
    // ADR-061 holds intent only and ADR-058 forbids persisting derived state, so
    // the layout cannot be reached from canonical state at all.
    takesSize(definition.fieldMap.cells());
#endif

#if defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_CELL_COUNT)
    takesSize(definition.cells);
#endif

#if defined(BETTERCAD_CF_THE_DEFINITION_HAS_NO_VELOCITY_ARRAY)
    takesDouble(definition.velocities.front());
#endif
}

} // namespace

#if defined(BETTERCAD_CF_A_LAYOUT_CANNOT_BE_BUILT_WITHOUT_ITS_BUILDER)
// Possession of a CfdFieldMap is the evidence that it came from a topology with
// at least one cell (ADR-066, Decision 6), so the constructor is private and only
// the two builders are friends.
CfdFieldMap conjured() {
    return CfdFieldMap{};
}
#endif

#if defined(BETTERCAD_CF_A_CELL_VECTOR_INDEX_HAS_NO_ARITHMETIC)
// A subscript is not a number to do sums on: the next component of a cell is
// `cells` away, not one, so `index + 1` would be a plausible and wrong way to
// walk a vector field.
CellVectorIndex nextComponent(CellVectorIndex index) {
    return index + 1;
}
#endif
