// The finite-volume topology (P19-TOPO-001, ADR-064).
//
// ONE WALK PER CALL, over the cells and then the boundary triangles, both in the
// mesh's own ascending ElementId order, with the faces keyed by their sorted node
// handles in an ORDERED map. Nothing here depends on an unordered container, a
// hash or an address, so two builds of one mesh are equal field for field.
//
// WHY `walk` EXISTS rather than `buildCfdTopology` calling `inspectCfdTopology`.
// The first draft did exactly that, and `requireCfdTopology` then inspected as
// well, so one topology cost THREE full map builds of 4 * cells insertions. The
// inspection and the assembly need the same walk, so they share one.

#include <bettercad/cfd/CfdTopology.hpp>

#include <bettercad/cfd/CfdAnalysis.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace bettercad::cfd {
namespace {

using meshing::ElementId;
using meshing::NodeId;

/// A face's identity: its three node handles, sorted. ADR-063's key, unchanged --
/// two cells that share a face produce the same one whatever order each lists
/// its nodes in.
using FaceKey = std::array<NodeId, 3>;

[[nodiscard]] FaceKey keyOf(std::array<NodeId, 3> nodes) noexcept {
    std::ranges::sort(nodes);
    return nodes;
}

/// What the walk remembers about a face before the order is decided.
struct FaceEntry {
    std::size_t claims = 0;
    /// The FIRST claimant, in ascending cell order, which is ADR-063's owner.
    std::size_t ownerSlot = 0;
    /// The second claimant, if any.
    std::size_t neighbourSlot = CellIndex::kInvalid;
    /// The face as its OWNER winds it: outward of the owner (ADR-063).
    std::array<NodeId, 3> wound{};
    /// The boundary triangle covering it, if one does.
    ElementId triangle{};
    bool covered = false;
    /// The patch of that triangle.
    std::size_t patch = PatchIndex::kInvalid;
    /// Assigned once the order is decided.
    std::size_t index = FaceIndex::kInvalid;
};

/// A tally of one problem, keeping the first occurrence's description. ADR-063's
/// shape, so a reader who knows one gate knows this one -- MINUS its
/// `occurrences` parameter, which exists there because two of its nine problems
/// are not per-entity. All eight problems here ARE per-entity, one occurrence per
/// call, so the parameter would be dead code.
struct Tally {
    std::size_t count = 0;
    std::string first{};

    void add(std::string description) {
        if (count == 0) {
            first = std::move(description);
        }
        ++count;
    }
};

[[nodiscard]] std::string triple(const FaceKey& key) {
    return std::format("({}, {}, {})", key[0].value(), key[1].value(), key[2].value());
}

/// Whether @p other traverses the same three nodes in the OPPOSITE direction to
/// @p wound: a cyclic rotation of (wound[0], wound[2], wound[1]). Exact and
/// combinatorial, as ADR-063's own check is; the caller has already established
/// that the node sets are equal.
[[nodiscard]] bool reversed(const std::array<NodeId, 3>& wound, const std::array<NodeId, 3>& other) noexcept {
    const std::array<NodeId, 3> back{wound[0], wound[2], wound[1]};
    for (std::size_t shift = 0; shift < 3; ++shift) {
        if (other[0] == back[shift] && other[1] == back[(shift + 1) % 3] && other[2] == back[(shift + 2) % 3]) {
            return true;
        }
    }
    return false;
}

/// Everything one pass over the mesh produces: the face table, the cells' handles
/// and the inspection.
struct Walk {
    std::map<FaceKey, FaceEntry> faces{};
    std::vector<ElementId> cellOf{};
    CfdTopologyInspection inspection{};
};

[[nodiscard]] Walk walk(const meshing::Mesh& mesh, std::span<const meshing::MappedFace> patches) {
    Walk out;
    out.inspection.census.cells = mesh.tetrahedra().size();
    out.inspection.census.boundaryTriangles = mesh.triangles().size();
    out.inspection.census.patches = patches.size();
    out.cellOf.reserve(mesh.tetrahedra().size());

    Tally volume;
    Tally manifold;
    Tally orientation;
    Tally missing;
    Tally unowned;
    Tally twoOwners;
    Tally duplicate;
    Tally unpatched;

    // AN EMPTY MESH IS NOT REFUSED HERE. `EmptyMesh` is the mesh contract's
    // refusal (ADR-063), and `requireCfdTopology` cannot be reached without a
    // contract, so a second copy of that rule would be a second thing to keep
    // true. An empty mesh yields an empty topology, which is what it is.

    // ONE PASS OVER THE CELLS, in the mesh's own ascending ElementId order: each
    // cell's signed volume, and each of its four faces into the ordered map.
    std::size_t slot = 0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const std::size_t cell = slot++;
        out.cellOf.push_back(tet.id);

        // THE VOLUME IS CHECKED HERE, AND THE WINDING GUARANTEE IS WHY. `kTetFaces`
        // is wound outward only of a POSITIVELY oriented tetrahedron (ADR-063), so
        // `faceNodes`'s promise -- that every face's right-hand normal points owner
        // -> neighbour, or out of the fluid -- is false for an inverted cell. An
        // adversarial review found that this file made that promise
        // unconditionally while checking nothing, so a plain-built topology could
        // hand P19-GEOM-001 area vectors pointing the wrong way, silently. The
        // promise is now enforced rather than documented.
        std::array<const meshing::Node*, 4> nodes{};
        for (std::size_t i = 0; i < 4; ++i) {
            nodes[i] = mesh.findNode(tet.nodes[i]);
        }
        if (std::ranges::any_of(nodes, [](const meshing::Node* node) { return node == nullptr; })) {
            // MeshBuilder refuses an element naming a missing node, so this cannot
            // happen for a built mesh; checked rather than dereferenced on faith.
            volume.add(std::format("tetrahedron {} names a node the mesh does not hold", tet.id.value()));
            continue;
        }
        const Volume signed_ = meshing::signedVolume(nodes[0]->position, nodes[1]->position, nodes[2]->position,
                                                     nodes[3]->position);
        // NO ABSOLUTE VALUE: a negative volume is an inverted cell, not a small one.
        if (!std::isfinite(signed_.si()) || signed_.si() <= 0.0) {
            volume.add(std::format("tetrahedron {} has a signed volume of {} m^3, so its faces are not wound "
                                   "outward of it",
                                   tet.id.value(), signed_.si()));
        }

        for (std::size_t k = 0; k < kTetFaces.size(); ++k) {
            const auto& local = kTetFaces[k];
            const std::array<NodeId, 3> wound{tet.nodes[local[0]], tet.nodes[local[1]], tet.nodes[local[2]]};
            FaceEntry& entry = out.faces[keyOf(wound)];
            if (entry.claims++ == 0) {
                entry.ownerSlot = cell;
                entry.wound = wound;
            } else if (entry.claims == 2) {
                entry.neighbourSlot = cell;
                // THE SECOND HALF OF THE WINDING GUARANTEE. Both cells wind a shared
                // face outward of themselves, so if they agree they lie on the SAME
                // side of it, folded over one another -- and then the owner's winding
                // does not point owner -> neighbour, however positive both volumes
                // are. Checked combinatorially, exactly, on the node order; no
                // tolerance. ADR-063 refuses the same configuration by name.
                if (!reversed(entry.wound, wound)) {
                    orientation.add(
                        std::format("cells {} and {} traverse their shared face {} in the same direction, so it "
                                    "is not wound from owner to neighbour",
                                    out.cellOf[entry.ownerSlot].value(), tet.id.value(),
                                    triple(keyOf(wound))));
                }
            }
            // A third or later claimant is refused as non-manifold below; it is
            // counted by `claims` and deliberately does not overwrite the neighbour.
        }
    }

    // WHICH PATCH EACH BOUNDARY TRIANGLE BELONGS TO, from the mapping's own
    // per-face facet lists. A facet named twice is refused rather than resolved:
    // choosing between the claims is the silent rebind ADR-032 forbids.
    std::map<ElementId, std::size_t> patchOfTriangle;
    std::map<ElementId, std::size_t> alreadyReported;
    for (std::size_t p = 0; p < patches.size(); ++p) {
        for (const ElementId facet : patches[p].facets) {
            const auto [it, inserted] = patchOfTriangle.emplace(facet, p);
            if (!inserted && !alreadyReported.contains(facet)) {
                alreadyReported.emplace(facet, p);
                unpatched.add(it->second == p
                                  ? std::format("boundary triangle {} is attributed twice to patch {}",
                                                facet.value(), p)
                                  : std::format("boundary triangle {} is attributed to patches {} and {}",
                                                facet.value(), it->second, p));
            }
        }
    }

    // EVERY BOUNDARY TRIANGLE MUST BE A FACE OF EXACTLY ONE CELL, CLAIMED ONCE,
    // AND ON EXACTLY ONE PATCH.
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        const FaceKey key = keyOf(triangle.nodes);
        const auto face = out.faces.find(key);
        if (face == out.faces.end()) {
            unowned.add(std::format("boundary triangle {} is a face of no cell", triangle.id.value()));
            continue;
        }
        FaceEntry& entry = face->second;
        if (entry.claims != 1) {
            twoOwners.add(std::format("boundary triangle {} lies on the face {}, which {} cells share",
                                      triangle.id.value(), triple(key), entry.claims));
            continue;
        }
        if (entry.covered) {
            duplicate.add(std::format("boundary triangles {} and {} claim the same face {}",
                                      entry.triangle.value(), triangle.id.value(), triple(key)));
            continue;
        }
        // COVERED BEFORE THE PATCH IS LOOKED UP, and that order matters. A triangle
        // rejected for having no patch still COVERS its face, so recording the
        // coverage only on success made the face look uncovered and the loop below
        // then reported `MissingNeighbour` for it too -- "no second cell and no
        // boundary triangle", which is false, because the face has a triangle. One
        // defect became two findings and the second was a lie. The tests caught it
        // by asserting the EXACT problem list, not merely that the build refused.
        entry.covered = true;
        entry.triangle = triangle.id;
        const auto patch = patchOfTriangle.find(triangle.id);
        if (patch == patchOfTriangle.end()) {
            unpatched.add(std::format("boundary triangle {} is attributed to no patch", triangle.id.value()));
            continue;
        }
        entry.patch = patch->second;
    }

    // THE FACES THEMSELVES: three or more claimants has no single neighbour, and
    // one claimant with no triangle has nothing on its other side and no patch
    // through which a condition could reach it.
    for (const auto& [key, entry] : out.faces) {
        if (entry.claims >= 3) {
            manifold.add(std::format("the face {} is shared by {} cells, so cell {} has no single neighbour "
                                     "across it",
                                     triple(key), entry.claims, out.cellOf[entry.ownerSlot].value()));
        } else if (entry.claims == 1 && !entry.covered) {
            missing.add(std::format("the face {} of cell {} has no second cell and no boundary triangle",
                                    triple(key), out.cellOf[entry.ownerSlot].value()));
        }
    }

    const std::array<std::pair<CfdTopologyProblem, Tally*>, 8> tallies{{
        {CfdTopologyProblem::NonPositiveCellVolume, &volume},
        {CfdTopologyProblem::NonManifoldFace, &manifold},
        {CfdTopologyProblem::InconsistentFaceOrientation, &orientation},
        {CfdTopologyProblem::MissingNeighbour, &missing},
        {CfdTopologyProblem::UnownedBoundaryFace, &unowned},
        {CfdTopologyProblem::BoundaryFaceWithTwoOwners, &twoOwners},
        {CfdTopologyProblem::DuplicateBoundaryFace, &duplicate},
        {CfdTopologyProblem::UnpatchedBoundaryFace, &unpatched},
    }};
    for (const auto& [problem, tally] : tallies) {
        if (tally->count > 0) {
            out.inspection.findings.push_back(CfdTopologyFinding{
                .problem = problem,
                .count = tally->count,
                .message = std::format("{} ({}): {}", toString(problem), tally->count, tally->first)});
        }
    }

    // The census is only meaningful for a mesh that yields a topology; for a
    // refused one the face counts are reported as far as they were classified,
    // which is what a diagnostic reader wants.
    for (const auto& [key, entry] : out.faces) {
        if (entry.claims == 2) {
            ++out.inspection.census.internalFaces;
        } else if (entry.claims == 1 && entry.covered) {
            ++out.inspection.census.boundaryFaces;
        }
    }
    out.inspection.census.faces = out.inspection.census.internalFaces + out.inspection.census.boundaryFaces;
    return out;
}

/// A face and its sort keys, carried together so the comparator needs NO map
/// lookup. An earlier draft called `faces.at(key)` twice per comparison, which
/// made the sort cost ~2 F log F tree traversals -- strictly worse than the
/// per-face map lookup ADR-064 rejected candidate C for, inside this file's own
/// hot loop. The pointer is stable: the map is not modified while sorting.
struct Ordered {
    std::size_t first = 0;  ///< owner slot (internal) or patch (boundary)
    std::size_t second = 0; ///< neighbour slot (internal) or owner slot (boundary)
    FaceKey key{};
    FaceEntry* entry = nullptr;

    [[nodiscard]] auto order() const noexcept { return std::tie(first, second, key); }
};

[[nodiscard]] std::string joined(const CfdTopologyInspection& inspection) {
    std::string out;
    for (const CfdTopologyFinding& finding : inspection.findings) {
        if (!out.empty()) {
            out += "; ";
        }
        out += finding.message;
    }
    return out;
}

} // namespace

/// Assembles the topology from a CLEAN walk. Precondition: `w.inspection` has no
/// findings, so every face has either exactly two claimants or exactly one
/// claimant with one triangle on exactly one patch.
///
/// A NAMED FRIEND, not a free function in the anonymous namespace: CfdTopology's
/// constructor is private, because possession of one is proof that the eight
/// refusals were checked, and only a friend may assemble one.
struct CfdTopologyAssembler {
    [[nodiscard]] static CfdTopology assemble(Walk& w, const meshing::Mesh& mesh,
                                              std::span<const meshing::MappedFace> patches) {
    // THE ORDER (ADR-064, Decision 3). Internal faces by (owner, neighbour), then
    // boundary faces by (patch, owner). The face key is the final tiebreaker in
    // both, so each order is TOTAL: two tetrahedra on the same four nodes share
    // all four faces and tie on (owner, neighbour), and the key breaks it.
    std::vector<Ordered> internal;
    std::vector<Ordered> boundary;
    internal.reserve(w.faces.size());
    boundary.reserve(mesh.triangles().size());
    for (auto& [key, entry] : w.faces) {
        if (entry.claims == 2) {
            internal.push_back(Ordered{entry.ownerSlot, entry.neighbourSlot, key, &entry});
        } else {
            boundary.push_back(Ordered{entry.patch, entry.ownerSlot, key, &entry});
        }
    }
    const auto byOrder = [](const Ordered& a, const Ordered& b) { return a.order() < b.order(); };
    std::ranges::sort(internal, byOrder);
    std::ranges::sort(boundary, byOrder);

    CfdTopology topology;
    topology.census_ = w.inspection.census;
    topology.mesh_ = mesh.stamp();
    topology.cellOf_ = w.cellOf;

    const std::size_t cells = w.cellOf.size();
    const std::size_t faceCount = internal.size() + boundary.size();
    topology.owner_.resize(faceCount);
    topology.neighbour_.resize(faceCount);
    topology.faceNodes_.resize(faceCount);

    std::size_t next = 0;
    for (const Ordered& face : internal) {
        face.entry->index = next;
        topology.owner_[next] = CellIndex::fromValue(face.entry->ownerSlot);
        topology.neighbour_[next] = CellIndex::fromValue(face.entry->neighbourSlot);
        topology.faceNodes_[next] = face.entry->wound;
        ++next;
    }
    topology.triangleOf_.reserve(boundary.size());
    topology.patchOf_.reserve(boundary.size());
    topology.patchFaceList_.reserve(boundary.size());
    for (const Ordered& face : boundary) {
        face.entry->index = next;
        topology.owner_[next] = CellIndex::fromValue(face.entry->ownerSlot);
        topology.neighbour_[next] = CellIndex{}; // invalid: a boundary face has none
        topology.faceNodes_[next] = face.entry->wound;
        topology.triangleOf_.push_back(face.entry->triangle);
        topology.patchOf_.push_back(PatchIndex::fromValue(face.entry->patch));
        topology.patchFaceList_.push_back(BoundaryFaceIndex::fromValue(next - internal.size()));
        ++next;
    }

    // CELL -> ITS FOUR FACES, in cell-major order, each cell's four ascending.
    topology.cellFaces_.assign(4 * cells, FaceIndex{});
    {
        std::size_t slot = 0;
        for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
            const std::size_t cell = slot++;
            for (std::size_t k = 0; k < kTetFaces.size(); ++k) {
                const auto& local = kTetFaces[k];
                const FaceKey key = keyOf({tet.nodes[local[0]], tet.nodes[local[1]], tet.nodes[local[2]]});
                topology.cellFaces_[4 * cell + k] = FaceIndex::fromValue(w.faces.at(key).index);
            }
            std::ranges::sort(std::span(topology.cellFaces_).subspan(4 * cell, 4));
        }
    }

    // THE SPARSE DIRECTIONS. Sorted vectors, searched by binary search; NEVER a
    // vector indexed by a handle's value (ADR-064, Decision 5). The cell vector is
    // already sorted when built, because CellIndex is assigned in ascending
    // ElementId order; a test asserts that ascending order through `cellOf`.
    topology.cellByElement_.reserve(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        topology.cellByElement_.emplace_back(w.cellOf[c], CellIndex::fromValue(c));
    }
    topology.boundaryByTriangle_.reserve(topology.triangleOf_.size());
    for (std::size_t b = 0; b < topology.triangleOf_.size(); ++b) {
        topology.boundaryByTriangle_.emplace_back(topology.triangleOf_[b], BoundaryFaceIndex::fromValue(b));
    }
    // Sorted explicitly: the boundary order is by patch, not by handle.
    std::ranges::sort(topology.boundaryByTriangle_,
                      [](const auto& a, const auto& b) { return a.first < b.first; });

    // THE PATCH CSR. The boundary range is ordered by patch, so each patch's
    // faces are contiguous and the starts are a prefix count.
    topology.patchStart_.assign(patches.size() + 1, 0);
    for (const PatchIndex patch : topology.patchOf_) {
        ++topology.patchStart_[patch.value() + 1];
    }
    for (std::size_t p = 0; p < patches.size(); ++p) {
        topology.patchStart_[p + 1] += topology.patchStart_[p];
    }
    topology.patchFace_.reserve(patches.size());
    for (const meshing::MappedFace& patch : patches) {
        topology.patchFace_.push_back(patch.index);
    }
    return topology;
    }
};

std::string_view toString(CfdTopologyProblem problem) noexcept {
    switch (problem) {
    case CfdTopologyProblem::NonPositiveCellVolume:
        return "non_positive_cell_volume";
    case CfdTopologyProblem::NonManifoldFace:
        return "non_manifold_face";
    case CfdTopologyProblem::InconsistentFaceOrientation:
        return "inconsistent_face_orientation";
    case CfdTopologyProblem::MissingNeighbour:
        return "missing_neighbour";
    case CfdTopologyProblem::UnownedBoundaryFace:
        return "unowned_boundary_face";
    case CfdTopologyProblem::BoundaryFaceWithTwoOwners:
        return "boundary_face_with_two_owners";
    case CfdTopologyProblem::DuplicateBoundaryFace:
        return "duplicate_boundary_face";
    case CfdTopologyProblem::UnpatchedBoundaryFace:
        return "unpatched_boundary_face";
    }
    return "unknown";
}

CfdTopologyInspection inspectCfdTopology(const meshing::Mesh& mesh, std::span<const meshing::MappedFace> patches) {
    return walk(mesh, patches).inspection;
}

Error cfdTopologyRefusal(const CfdTopologyInspection& inspection, MeshControlId control) {
    return Error{ErrorCode::FailedPrecondition,
                 std::format("the mesh of meshing control {} does not yield a finite-volume topology: {}",
                             control.value(), joined(inspection))};
}

std::optional<CfdTopologyProblem> cfdTopologyProblem(const meshing::Mesh& mesh,
                                                     std::span<const meshing::MappedFace> patches) {
    return inspectCfdTopology(mesh, patches).firstProblem();
}

Result<CfdTopology> buildCfdTopology(const meshing::Mesh& mesh, std::span<const meshing::MappedFace> patches) {
    Walk w = walk(mesh, patches);
    if (!w.inspection.buildable()) {
        // No control to name here: a plain mesh has none. The staged gate supplies
        // one, and this message says what it can.
        return std::unexpected(Error{ErrorCode::FailedPrecondition,
                                     std::format("the mesh does not yield a finite-volume topology: {}",
                                                 joined(w.inspection))});
    }
    return CfdTopologyAssembler::assemble(w, mesh, patches);
}

Result<CfdTopology> requireCfdTopology(const CfdModel& model, const CfdMeshContract& contract) {
    const meshing::Mesh& mesh = model.mesh().mesh();
    // THE STAMP BINDING, which is the whole reason CfdMeshContract carries a
    // stamp (ADR-063). Counts are never compared for identity: two generations
    // can agree on every count and share no node.
    if (!contract.describes(mesh)) {
        return std::unexpected(
            Error{ErrorCode::FailedPrecondition,
                  std::format("the CFD mesh contract for meshing control {} was proved for mesh generation {}.{}, "
                              "but the model now holds {}.{}: a contract for one generation says nothing about "
                              "the next",
                              model.control().value(), contract.mesh().mesh.value(), contract.mesh().generation,
                              mesh.stamp().mesh.value(), mesh.stamp().generation)});
    }
    Walk w = walk(mesh, model.map().faces());
    if (!w.inspection.buildable()) {
        // Named with the control, which the plain builder cannot do. On a mesh
        // holding a contract this is unreachable (ADR-064, Decision 7); it is here
        // so that if it ever becomes reachable the diagnostic names the object.
        return std::unexpected(cfdTopologyRefusal(w.inspection, model.control()));
    }
    CfdTopology topology = CfdTopologyAssembler::assemble(w, mesh, model.map().faces());
    topology.geometry_ = model.geometryRevision();
    return topology;
}

// ---------------------------------------------------------------------------
// Accessors. Every one is total: an index this topology does not hold yields an
// invalid index, an empty optional or an empty span, never undefined behaviour.
// ---------------------------------------------------------------------------

CellIndex CfdTopology::owner(FaceIndex face) const noexcept {
    if (!face.isValid() || face.value() >= owner_.size()) {
        return CellIndex{};
    }
    return owner_[face.value()];
}

std::optional<CellIndex> CfdTopology::neighbour(FaceIndex face) const noexcept {
    if (!face.isValid() || face.value() >= neighbour_.size()) {
        return std::nullopt;
    }
    const CellIndex cell = neighbour_[face.value()];
    if (!cell.isValid()) {
        return std::nullopt;
    }
    return cell;
}

std::array<meshing::NodeId, 3> CfdTopology::faceNodes(FaceIndex face) const noexcept {
    if (!face.isValid() || face.value() >= faceNodes_.size()) {
        return {};
    }
    return faceNodes_[face.value()];
}

std::span<const FaceIndex> CfdTopology::cellFaces(CellIndex cell) const noexcept {
    if (!cell.isValid() || cell.value() >= census_.cells) {
        return {};
    }
    return std::span(cellFaces_).subspan(4 * cell.value(), 4);
}

std::optional<BoundaryFaceIndex> CfdTopology::boundaryFaceOf(FaceIndex face) const noexcept {
    if (!isBoundary(face)) {
        return std::nullopt;
    }
    return BoundaryFaceIndex::fromValue(face.value() - census_.internalFaces);
}

FaceIndex CfdTopology::faceOf(BoundaryFaceIndex boundaryFace) const noexcept {
    if (!boundaryFace.isValid() || boundaryFace.value() >= census_.boundaryFaces) {
        return FaceIndex{};
    }
    return FaceIndex::fromValue(census_.internalFaces + boundaryFace.value());
}

PatchIndex CfdTopology::patchOf(BoundaryFaceIndex boundaryFace) const noexcept {
    if (!boundaryFace.isValid() || boundaryFace.value() >= patchOf_.size()) {
        return PatchIndex{};
    }
    return patchOf_[boundaryFace.value()];
}

std::span<const BoundaryFaceIndex> CfdTopology::patchFaces(PatchIndex patch) const noexcept {
    if (!patch.isValid() || patch.value() + 1 >= patchStart_.size()) {
        return {};
    }
    const std::size_t begin = patchStart_[patch.value()];
    const std::size_t end = patchStart_[patch.value() + 1];
    return std::span(patchFaceList_).subspan(begin, end - begin);
}

std::optional<std::size_t> CfdTopology::patchFace(PatchIndex patch) const noexcept {
    if (!patch.isValid() || patch.value() >= patchFace_.size()) {
        return std::nullopt;
    }
    return patchFace_[patch.value()];
}

meshing::ElementId CfdTopology::cellOf(CellIndex cell) const noexcept {
    if (!cell.isValid() || cell.value() >= cellOf_.size()) {
        return ElementId{};
    }
    return cellOf_[cell.value()];
}

std::optional<CellIndex> CfdTopology::cellIndexOf(meshing::ElementId element) const noexcept {
    const auto found = std::ranges::lower_bound(cellByElement_, element, {},
                                                [](const auto& pair) { return pair.first; });
    if (found == cellByElement_.end() || found->first != element) {
        return std::nullopt;
    }
    return found->second;
}

meshing::ElementId CfdTopology::triangleOf(BoundaryFaceIndex boundaryFace) const noexcept {
    if (!boundaryFace.isValid() || boundaryFace.value() >= triangleOf_.size()) {
        return ElementId{};
    }
    return triangleOf_[boundaryFace.value()];
}

std::optional<BoundaryFaceIndex> CfdTopology::boundaryFaceOfTriangle(meshing::ElementId element) const noexcept {
    const auto found = std::ranges::lower_bound(boundaryByTriangle_, element, {},
                                                [](const auto& pair) { return pair.first; });
    if (found == boundaryByTriangle_.end() || found->first != element) {
        return std::nullopt;
    }
    return found->second;
}

} // namespace bettercad::cfd
