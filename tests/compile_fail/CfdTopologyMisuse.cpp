// Build-failure tests for the finite-volume topology's addressing (see
// CMakeLists.txt in this directory). Built once without any BETTERCAD_CF_* macro
// as a control, which must compile -- so every failure below is attributable to
// its own line and not to a missing header.
//
// WHAT THESE PROVE is P19-TOPO-001's `ElementId != CellIndex` and the three
// further confusions a second addressing space makes possible (ADR-064). The
// topology introduces four subscripts over one mesh, and three of them are
// `std::size_t` underneath:
//
//   CellIndex          a cell, 0 .. cells-1
//   FaceIndex          a face, 0 .. faces-1
//   BoundaryFaceIndex  a boundary face, 0 .. boundaryFaces-1
//   PatchIndex         a patch, 0 .. patches-1
//
// Nothing about the numbers distinguishes them: face 3 and cell 3 both exist in
// almost every mesh, and reading a field of one with the other's subscript is
// silent and wrong. So the separation cannot rest on naming, and these cases are
// the proof that it rests on the type system instead.
//
// AND ON THE OTHER SIDE, a topology subscript is not a MESH HANDLE. ADR-031
// explains that an ElementId is sparse, 1-based and scoped to one generation;
// ADR-064 adds that an index is dense and 0-based, so zero means "the first cell"
// here and "invalid" there. Mixing them is the error that would silently read
// cell 0 for an invalid handle, and both directions are refused.
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <cstddef>
#include <cstdint>

using namespace bettercad;
using namespace bettercad::cfd;

namespace {

void takesCell(CellIndex /*unused*/) {}
void takesFace(FaceIndex /*unused*/) {}
void takesBoundaryFace(BoundaryFaceIndex /*unused*/) {}
void takesPatch(PatchIndex /*unused*/) {}
void takesElement(meshing::ElementId /*unused*/) {}
void takesNode(meshing::NodeId /*unused*/) {}
void takesCount(std::size_t /*unused*/) {}

/// What a consumer of a built topology receives. Never called: these cases are
/// about what it CANNOT do, which the compiler decides without running it.
[[maybe_unused]] void reads(const CfdTopology& topology) {
    // The accessors, used correctly, so the control exercises every signature the
    // cases below misuse.
    [[maybe_unused]] const CellIndex owner = topology.owner(FaceIndex::fromValue(0));
    [[maybe_unused]] const auto neighbour = topology.neighbour(FaceIndex::fromValue(0));
    [[maybe_unused]] const auto nodes = topology.faceNodes(FaceIndex::fromValue(0));
    [[maybe_unused]] const auto faces = topology.cellFaces(CellIndex::fromValue(0));
    [[maybe_unused]] const auto patchFaces = topology.patchFaces(PatchIndex::fromValue(0));
    [[maybe_unused]] const PatchIndex patch = topology.patchOf(BoundaryFaceIndex::fromValue(0));
    [[maybe_unused]] const meshing::ElementId element = topology.cellOf(CellIndex::fromValue(0));

#if defined(BETTERCAD_CF_A_CELL_IS_NOT_A_FACE)
    // THE CONFUSION THE DESIGN IS BUILT AGAINST: owner() takes a face, and cell 3
    // and face 3 both exist in almost every mesh.
    [[maybe_unused]] const CellIndex wrong = topology.owner(CellIndex::fromValue(0));
#elif defined(BETTERCAD_CF_A_FACE_IS_NOT_A_CELL)
    [[maybe_unused]] const auto wrong = topology.cellFaces(FaceIndex::fromValue(0));
#elif defined(BETTERCAD_CF_A_FACE_IS_NOT_A_BOUNDARY_FACE)
    // A face index and a boundary-face index differ by internalFaces, and the
    // arithmetic is deliberately not the caller's to perform.
    [[maybe_unused]] const PatchIndex wrong = topology.patchOf(FaceIndex::fromValue(0));
#elif defined(BETTERCAD_CF_A_BOUNDARY_FACE_IS_NOT_A_FACE)
    [[maybe_unused]] const CellIndex wrong = topology.owner(BoundaryFaceIndex::fromValue(0));
#elif defined(BETTERCAD_CF_A_PATCH_IS_NOT_A_BOUNDARY_FACE)
    [[maybe_unused]] const PatchIndex wrong = topology.patchOf(PatchIndex::fromValue(0));
#elif defined(BETTERCAD_CF_A_BOUNDARY_FACE_IS_NOT_A_PATCH)
    [[maybe_unused]] const auto wrong = topology.patchFaces(BoundaryFaceIndex::fromValue(0));
#elif defined(BETTERCAD_CF_AN_ELEMENT_HANDLE_IS_NOT_A_CELL_INDEX)
    // ADR-031 and ADR-064: a handle is sparse and 1-based, an index is dense and
    // 0-based. This is the line that would silently read cell 0 for an invalid
    // handle.
    [[maybe_unused]] const auto wrong = topology.cellFaces(meshing::ElementId::fromValue(1));
#elif defined(BETTERCAD_CF_A_CELL_INDEX_IS_NOT_AN_ELEMENT_HANDLE)
    [[maybe_unused]] const auto wrong = topology.cellIndexOf(CellIndex::fromValue(0));
#elif defined(BETTERCAD_CF_A_NODE_HANDLE_IS_NOT_A_CELL_INDEX)
    [[maybe_unused]] const auto wrong = topology.cellFaces(meshing::NodeId::fromValue(1));
#elif defined(BETTERCAD_CF_AN_INTEGER_IS_NOT_A_FACE_INDEX)
    // No bare subscript becomes an index by accident, which is the whole reason
    // these are classes and not type aliases for std::size_t.
    [[maybe_unused]] const CellIndex wrong = topology.owner(0);
#elif defined(BETTERCAD_CF_AN_INDEX_IS_NOT_AN_INTEGER)
    // Nor does one decay to its underlying type: value() is the only way out.
    // (This case used to be `takesCount(cell)`, which is literally the same
    // expression as PASSING_A_CELL_WHERE_AN_INTEGER_IS_WANTED below -- two
    // registered cases proving one thing. A review found the duplication.)
    const std::size_t raw = CellIndex::fromValue(0);
#elif defined(BETTERCAD_CF_A_TOPOLOGY_CANNOT_BE_BUILT_WITHOUT_ITS_BUILDER)
    // POSSESSION IS THE PROOF. The constructor is private and has exactly two
    // friends, so holding a CfdTopology means the eight refusals were checked on
    // the mesh it came from. Without this case nothing proved the door is shut --
    // every comparable milestone in this repository has the equivalent.
    [[maybe_unused]] const CfdTopology conjured{};
#endif
}

} // namespace

int main() {
    const CellIndex cell = CellIndex::fromValue(0);
    const FaceIndex face = FaceIndex::fromValue(0);
    const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(0);
    const PatchIndex patch = PatchIndex::fromValue(0);
    const meshing::ElementId element = meshing::ElementId::fromValue(1);
    const meshing::NodeId node = meshing::NodeId::fromValue(1);

    // Each kind is usable as itself, so the control compiles.
    takesCell(cell);
    takesFace(face);
    takesBoundaryFace(boundaryFace);
    takesPatch(patch);
    takesElement(element);
    takesNode(node);
    takesCount(cell.value() + face.value() + boundaryFace.value() + patch.value());

    // ZERO IS A VALID INDEX HERE and the invalid state is a separate value, which
    // is the inversion of every mesh handle in BetterCAD. Both halves must keep
    // compiling, because the cases below are about conversions and not about this.
    static_assert(CellIndex::fromValue(0).isValid());
    static_assert(!CellIndex{}.isValid());
    static_assert(!meshing::ElementId{}.isValid());

#if defined(BETTERCAD_CF_PASSING_A_FACE_WHERE_A_CELL_IS_WANTED)
    // The four subscripts are four types, in every direction a call can confuse.
    takesCell(face);
#elif defined(BETTERCAD_CF_PASSING_A_CELL_WHERE_A_FACE_IS_WANTED)
    takesFace(cell);
#elif defined(BETTERCAD_CF_PASSING_A_BOUNDARY_FACE_WHERE_A_FACE_IS_WANTED)
    takesFace(boundaryFace);
#elif defined(BETTERCAD_CF_PASSING_A_PATCH_WHERE_A_BOUNDARY_FACE_IS_WANTED)
    takesBoundaryFace(patch);
#elif defined(BETTERCAD_CF_PASSING_AN_ELEMENT_WHERE_A_CELL_IS_WANTED)
    // The milestone's own checklist item, as a compiler error.
    takesCell(element);
#elif defined(BETTERCAD_CF_PASSING_A_CELL_WHERE_AN_ELEMENT_IS_WANTED)
    takesElement(cell);
#elif defined(BETTERCAD_CF_PASSING_A_NODE_WHERE_A_CELL_IS_WANTED)
    takesCell(node);
#elif defined(BETTERCAD_CF_PASSING_AN_INTEGER_WHERE_A_CELL_IS_WANTED)
    takesCell(0);
#elif defined(BETTERCAD_CF_PASSING_A_CELL_WHERE_AN_INTEGER_IS_WANTED)
    takesCount(cell);
#elif defined(BETTERCAD_CF_AN_INDEX_HAS_NO_ARITHMETIC)
    // A subscript is not a number: it cannot be advanced, differenced or scaled,
    // because "face + 1" over a range whose halves mean different things is how a
    // boundary index would be built out of a face index by accident.
    [[maybe_unused]] const CellIndex advanced = cell + 1;
#elif defined(BETTERCAD_CF_TWO_INDEX_KINDS_DO_NOT_COMPARE)
    // Nor do two kinds compare, which would be the silent form of the same bug.
    [[maybe_unused]] const bool same = cell == face;
#endif
    return 0;
}
