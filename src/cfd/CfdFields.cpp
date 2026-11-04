// The CFD field layout (P19-DOF-001, ADR-066).
//
// ARITHMETIC, NOT A TABLE. Every translation below is a comparison, a
// multiplication or a division over three counts, so there is no per-cell and no
// per-face array to build and nothing to keep in step. The only stored vectors are
// the patch partition, which is read off the topology's own CSR rather than
// recomputed from the faces.
//
// ONE INVARIANT IS INHERITED AND NOT RE-DERIVED: that every boundary face lies in
// exactly one patch, so the per-patch counts sum to `boundaryFaces` and a
// partition is enough to answer `patchOf`. That is ADR-064's, enforced by its
// `UnpatchedBoundaryFace` refusal -- "`patchOf` must be total" -- so possession of
// a `CfdTopology` is what makes it true here. The tests check the consequence on
// every reference mesh (every boundary face has a patch, and the ranges cover the
// boundary exactly) rather than this file guarding a condition the type system
// already pays for.
//
// NO ELEMENT OR NODE HANDLE IS READ, not even to copy. The builder asks the
// topology for counts, for `patchFaces(p).size()` and for `patchFace(p)`; it never
// asks for an ElementId or a NodeId, which is why no container here can be sized
// by one. The MeshId inside the stamp is copied, and formatted into one refusal,
// as an IDENTITY -- the distinction the header draws, and the reason this sentence
// no longer says "no mesh handle is read".

#include <bettercad/cfd/CfdFields.hpp>

#include <bettercad/cfd/CfdAnalysis.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::cfd {
namespace {

/// The value a patch's CAD face index takes if the topology could not name it.
///
/// UNREACHABLE: `CfdTopology::patchFace` is total over `[0, patches)`, which is
/// the same totality `patchOf` has. A DISTINGUISHABLE sentinel rather than 0, so
/// that if it ever did fire the binding comparison would notice instead of
/// agreeing with a real CAD face 0.
constexpr std::size_t kUnnamedPatchFace = std::numeric_limits<std::size_t>::max();

} // namespace

std::string_view toString(CfdFieldLocation location) noexcept {
    switch (location) {
    case CfdFieldLocation::Cell:
        return "cell-centred";
    case CfdFieldLocation::Face:
        return "face-centred";
    case CfdFieldLocation::BoundaryFace:
        return "boundary-face-centred";
    }
    return "unknown field location";
}

std::string_view toString(CfdVariable variable) noexcept {
    switch (variable) {
    case CfdVariable::Pressure:
        return "pressure";
    case CfdVariable::Velocity:
        return "velocity";
    case CfdVariable::MassFlux:
        return "mass flux";
    }
    return "unknown variable";
}

std::string_view toString(CfdFieldMapProblem problem) noexcept {
    switch (problem) {
    case CfdFieldMapProblem::TopologyHasNoCells:
        return "the finite-volume topology holds no cells, so there is nothing to number";
    case CfdFieldMapProblem::TopologyIsOfAnotherMesh:
        return "the finite-volume topology was derived from another mesh generation";
    }
    return "unknown field layout problem";
}

std::string_view toString(CfdFieldCurrency currency) noexcept {
    switch (currency) {
    case CfdFieldCurrency::Current:
        return "current";
    case CfdFieldCurrency::BoundaryLayoutChanged:
        return "the boundary layout changed";
    case CfdFieldCurrency::MeshChanged:
        return "the mesh changed";
    }
    return "unknown field currency";
}

// ---------------------------------------------------------------------------
// Lengths
// ---------------------------------------------------------------------------

std::size_t CfdFieldMap::size(CfdFieldLocation location) const noexcept {
    switch (location) {
    case CfdFieldLocation::Cell:
        return cells_;
    case CfdFieldLocation::Face:
        return faces_;
    case CfdFieldLocation::BoundaryFace:
        return boundaryFaces();
    }
    // An unrecognised enumerator is representable and names no slots. Not an
    // assertion: a length of zero is the answer a caller can act on.
    return 0;
}

std::size_t CfdFieldMap::size(CfdVariable variable) const noexcept {
    const std::optional<CfdFieldDescriptor> descriptor = descriptorOf(variable);
    if (!descriptor) {
        return 0;
    }
    return descriptor->components * size(descriptor->location);
}

// ---------------------------------------------------------------------------
// The cell-centred vector
// ---------------------------------------------------------------------------

std::optional<CellVectorIndex> CfdFieldMap::vectorIndex(CellIndex cell, VectorComponent component) const noexcept {
    if (!contains(cell)) {
        return std::nullopt;
    }
    const auto offset = static_cast<std::size_t>(component);
    if (offset >= kVectorComponents.size()) {
        return std::nullopt;
    }
    // ADR-066, Decision 3: component * cells + cell. The multiplication cannot
    // overflow -- the static_assert in the header proves 3 * cells is inside a
    // size_t for any mesh a 32-bit handle space can hold.
    return CellVectorIndex::fromValue(offset * cells_ + cell.value());
}

bool CfdFieldMap::contains(CellVectorIndex index) const noexcept {
    return index.isValid() && index.value() < kVectorComponents.size() * cells_;
}

std::optional<CellIndex> CfdFieldMap::cellOf(CellVectorIndex index) const noexcept {
    if (!contains(index)) {
        return std::nullopt;
    }
    return CellIndex::fromValue(index.value() % cells_);
}

std::optional<VectorComponent> CfdFieldMap::componentOf(CellVectorIndex index) const noexcept {
    if (!contains(index)) {
        return std::nullopt;
    }
    // The QUOTIENT, not a modulo: blocking puts the component in the high part.
    return kVectorComponents[index.value() / cells_];
}

// ---------------------------------------------------------------------------
// The face and boundary slots
// ---------------------------------------------------------------------------

std::optional<FaceIndex> CfdFieldMap::faceSlotOf(BoundaryFaceIndex boundaryFace) const noexcept {
    if (!contains(boundaryFace)) {
        return std::nullopt;
    }
    return FaceIndex::fromValue(internalFaces_ + boundaryFace.value());
}

std::optional<BoundaryFaceIndex> CfdFieldMap::boundarySlotOf(FaceIndex face) const noexcept {
    if (!contains(face) || face.value() < internalFaces_) {
        return std::nullopt;
    }
    return BoundaryFaceIndex::fromValue(face.value() - internalFaces_);
}

std::optional<PatchIndex> CfdFieldMap::patchOf(BoundaryFaceIndex boundaryFace) const noexcept {
    // THREE OF THE FOUR GUARDS BELOW ARE UNREACHABLE FOR A LAYOUT THIS FILE
    // BUILT, and each says which it is and why. P19-GEOM-001's reviews found the
    // opposite error -- a header calling four guards unreachable when they were
    // reachable -- so the status of a guard is written where the guard is, and the
    // reason is an invariant rather than an impression.
    //
    //   !contains(boundaryFace)     REACHABLE, and tested: the invalid slot, the
    //                               slot one past the end, and a slot of another
    //                               layout
    //   patchStart_.size() < 2      unreachable. `||` short-circuits, so this is
    //                               asked only when `contains` is true, which
    //                               needs boundaryFaces() > 0; ADR-064 refuses
    //                               UnpatchedBoundaryFace, so a boundary face
    //                               implies at least one patch, so patchStart_
    //                               holds at least {0, boundaryFaces}
    //   above == begin()            unreachable. patchStart_[0] is always 0 and
    //                               upper_bound returns begin() only when the
    //                               first element EXCEEDS the value; 0 exceeds no
    //                               std::size_t
    //   patch >= patchFace_.size()  unreachable. `contains` bounds the slot below
    //                               patchStart_.back(), so some element exceeds
    //                               it and upper_bound never returns end()
    //
    // They are kept for the reason ADR-065 kept its unreachable guards: the
    // invariants are ADR-064's, held by the topology and not by this file, and a
    // total accessor that cannot be made to return a wrong patch is worth three
    // comparisons.
    if (!contains(boundaryFace) || patchStart_.size() < 2) {
        return std::nullopt;
    }
    // The LAST patch whose start is at or below the slot. With an empty patch the
    // offsets repeat, and taking the last keeps the non-empty patch that actually
    // holds the slot -- which is why this is `upper_bound` and not `lower_bound`,
    // and why a LEADING empty patch is one of the tested cases.
    const auto above = std::upper_bound(patchStart_.begin(), patchStart_.end(), boundaryFace.value());
    if (above == patchStart_.begin()) {
        return std::nullopt;
    }
    const auto patch = static_cast<std::size_t>(std::distance(patchStart_.begin(), above) - 1);
    if (patch >= patchFace_.size()) {
        return std::nullopt;
    }
    return PatchIndex::fromValue(patch);
}

std::optional<CfdPatchFieldRange> CfdFieldMap::patchRange(PatchIndex patch) const noexcept {
    if (!patch.isValid() || patch.value() >= patchFace_.size()) {
        return std::nullopt;
    }
    const std::size_t first = patchStart_[patch.value()];
    const std::size_t last = patchStart_[patch.value() + 1];
    CfdPatchFieldRange range;
    range.count = last - first;
    if (range.count != 0) {
        range.first = BoundaryFaceIndex::fromValue(first);
    }
    return range;
}

std::optional<std::size_t> CfdFieldMap::patchFace(PatchIndex patch) const noexcept {
    if (!patch.isValid() || patch.value() >= patchFace_.size()) {
        return std::nullopt;
    }
    return patchFace_[patch.value()];
}

// ---------------------------------------------------------------------------
// The binding
// ---------------------------------------------------------------------------

CfdFieldCurrency CfdFieldMap::currency(const CfdTopology& topology) const noexcept {
    // THE MESH PART. The stamp is the identity; the three counts corroborate it,
    // because a caller who got this wrong would otherwise index past the end of an
    // array.
    //
    // THE THREE COUNT COMPARISONS NEVER DECIDE A VERDICT, and that is recorded
    // rather than implied: one mesh generation yields one set of counts, so a
    // topology agreeing on the stamp agrees on all three, and `||` short-circuits
    // past them whenever the stamp differs. No public API produces the pair that
    // would exercise them -- every builder derives the counts from the mesh -- so
    // they are corroboration, as ADR-065 said of the same comparison, and not a
    // branch any test can reach.
    //
    // THE GEOMETRY REVISION IS NOT COMPARED, and the reason is not symmetry with
    // the counts: the staged gate takes the revision from the MODEL and the plain
    // builder from the topology, so a layout built by the gate from a plain
    // topology legitimately carries a revision the topology does not have.
    // Comparing it would make such a layout disagree with the very topology it was
    // built from, which `CfdFields_TheStagedGateReplacesThePlainTopologysGeometryRevision`
    // constructs and would fail on.
    if (!(topology.mesh() == mesh_) || topology.cells() != cells_ || topology.faces() != faces_ ||
        topology.internalFaces() != internalFaces_) {
        return CfdFieldCurrency::MeshChanged;
    }

    // THE BOUNDARY PART (ADR-066, Decision 5). Two topologies of ONE generation
    // can differ in their patch attribution: same stamp, same counts, a boundary
    // range in another order. Cell fields survive that; boundary fields do not.
    if (topology.patches() != patchFace_.size()) {
        return CfdFieldCurrency::BoundaryLayoutChanged;
    }
    for (std::size_t p = 0; p < patchFace_.size(); ++p) {
        const PatchIndex patch = PatchIndex::fromValue(p);
        if (topology.patchFaces(patch).size() != patchStart_[p + 1] - patchStart_[p]) {
            return CfdFieldCurrency::BoundaryLayoutChanged;
        }
        if (topology.patchFace(patch).value_or(kUnnamedPatchFace) != patchFace_[p]) {
            return CfdFieldCurrency::BoundaryLayoutChanged;
        }
    }
    return CfdFieldCurrency::Current;
}

// ---------------------------------------------------------------------------
// Building one
// ---------------------------------------------------------------------------

std::optional<CfdFieldMapProblem> cfdFieldMapProblem(const CfdTopology& topology) noexcept {
    if (topology.cells() == 0) {
        return CfdFieldMapProblem::TopologyHasNoCells;
    }
    return std::nullopt;
}

Result<CfdFieldMap> buildCfdFieldMap(const CfdTopology& topology) {
    if (const std::optional<CfdFieldMapProblem> problem = cfdFieldMapProblem(topology)) {
        return std::unexpected(Error{ErrorCode::FailedPrecondition,
                                     std::format("the CFD field layout cannot be built: {}", toString(*problem))});
    }

    CfdFieldMap map;
    map.mesh_ = topology.mesh();
    map.geometry_ = topology.geometry();
    map.cells_ = topology.cells();
    map.faces_ = topology.faces();
    map.internalFaces_ = topology.internalFaces();

    const std::size_t patches = topology.patches();
    map.patchStart_.reserve(patches + 1);
    map.patchFace_.reserve(patches);
    std::size_t start = 0;
    map.patchStart_.push_back(start);
    for (std::size_t p = 0; p < patches; ++p) {
        const PatchIndex patch = PatchIndex::fromValue(p);
        start += topology.patchFaces(patch).size();
        map.patchStart_.push_back(start);
        map.patchFace_.push_back(topology.patchFace(patch).value_or(kUnnamedPatchFace));
    }
    return map;
}

Result<CfdFieldMap> requireCfdFieldMap(const CfdModel& model, const CfdTopology& topology) {
    const meshing::Mesh& mesh = model.mesh().mesh();
    if (!topology.describes(mesh)) {
        return std::unexpected(
            Error{ErrorCode::FailedPrecondition,
                  std::format("the finite-volume topology for meshing control {} was derived from mesh generation "
                              "{}.{}, but the model now holds {}.{}: {}",
                              model.control().value(), topology.mesh().mesh.value(), topology.mesh().generation,
                              mesh.stamp().mesh.value(), mesh.stamp().generation,
                              toString(CfdFieldMapProblem::TopologyIsOfAnotherMesh))});
    }
    // UNREACHABLE, AND KEPT: the only refusal `buildCfdFieldMap` makes is
    // TopologyHasNoCells, and a topology of no cells cannot describe the mesh of a
    // CfdModel -- a model holds a P16-validated mesh, whose cells the contract
    // counted, so the stamp check above fires first. The wrap stays because the
    // builder's refusal set is not this function's to assume, and a layout that
    // failed here without naming the control would be a diagnostic a reader of the
    // failure could not place.
    Result<CfdFieldMap> map = buildCfdFieldMap(topology);
    if (!map) {
        return std::unexpected(Error{map.error().code,
                                     std::format("the CFD field layout of meshing control {}: {}",
                                                 model.control().value(), map.error().message)});
    }
    // The model's revision, not the topology's copy of it: the staged gate is what
    // has a CfdModel to ask, exactly as requireCfdTopology and requireCfdGeometry
    // do. The fluid is deliberately not consulted -- it is not an input to a
    // layout, so a material edit cannot change one.
    map->geometry_ = model.geometryRevision();
    return map;
}

} // namespace bettercad::cfd
