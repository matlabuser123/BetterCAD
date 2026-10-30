// The CFD mesh contract and its gate (P19-MESH-001, ADR-063).

#include <bettercad/cfd/CfdMesh.hpp>

#include <bettercad/cfd/CfdAnalysis.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace bettercad::cfd {
namespace {

using meshing::ElementId;
using meshing::NodeId;

/// A face's identity in the census: its three node handles, sorted. Two cells that
/// share a face produce the same key, whatever order each lists the nodes in.
using FaceKey = std::array<NodeId, 3>;

[[nodiscard]] FaceKey keyOf(std::array<NodeId, 3> nodes) noexcept {
    std::ranges::sort(nodes);
    return nodes;
}

/// What the census remembers about a face: how many cells claim it, and -- from
/// the FIRST claimant, in cell order -- which cell (its handle and its position in
/// the mesh's cell order) and which vertex lies opposite.
struct FaceRecord {
    std::size_t claims = 0;
    ElementId owner{};
    std::size_t ownerSlot = 0;
    NodeId apex{};
    /// The face as the owner winds it (its local table, outward of the owner).
    std::array<NodeId, 3> wound{};
};

/// Whether @p other traverses the same three nodes in the OPPOSITE direction to
/// @p wound: a cyclic rotation of (wound[0], wound[2], wound[1]). Exact and
/// combinatorial; the caller has already established the node sets are equal.
[[nodiscard]] bool reversed(const std::array<NodeId, 3>& wound, const std::array<NodeId, 3>& other) noexcept {
    const std::array<NodeId, 3> back{wound[0], wound[2], wound[1]};
    for (std::size_t shift = 0; shift < 3; ++shift) {
        if (other[0] == back[shift] && other[1] == back[(shift + 1) % 3] && other[2] == back[(shift + 2) % 3]) {
            return true;
        }
    }
    return false;
}

/// Union-find over cell positions, for face-connectivity. The root of a set is
/// its LOWEST position (a union always keeps the lower root), so the result does
/// not depend on the order the unions arrive in.
class CellSets {
public:
    explicit CellSets(std::size_t cells) : parent_(cells) {
        for (std::size_t i = 0; i < cells; ++i) {
            parent_[i] = i;
        }
    }

    [[nodiscard]] std::size_t root(std::size_t cell) noexcept {
        while (parent_[cell] != cell) {
            parent_[cell] = parent_[parent_[cell]];
            cell = parent_[cell];
        }
        return cell;
    }

    void join(std::size_t a, std::size_t b) noexcept {
        const std::size_t ra = root(a);
        const std::size_t rb = root(b);
        if (ra < rb) {
            parent_[rb] = ra;
        } else if (rb < ra) {
            parent_[ra] = rb;
        }
    }

private:
    std::vector<std::size_t> parent_;
};

/// A tally of one problem, keeping the first occurrence's description.
struct Tally {
    std::size_t count = 0;
    std::string first{};

    void add(std::string description, std::size_t occurrences = 1) {
        if (count == 0) {
            first = std::move(description);
        }
        count += occurrences;
    }
};

[[nodiscard]] std::array<double, 3> si(const Point3D& p) noexcept { return {p.x.si(), p.y.si(), p.z.si()}; }

[[nodiscard]] std::array<double, 3> minus(const std::array<double, 3>& a, const std::array<double, 3>& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

[[nodiscard]] std::array<double, 3> cross(const std::array<double, 3>& a, const std::array<double, 3>& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

[[nodiscard]] double dot(const std::array<double, 3>& a, const std::array<double, 3>& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

[[nodiscard]] std::string triple(const FaceKey& key) {
    return std::format("({}, {}, {})", key[0].value(), key[1].value(), key[2].value());
}

} // namespace

std::string_view toString(CfdMeshProblem problem) noexcept {
    switch (problem) {
    case CfdMeshProblem::EmptyMesh:
        return "empty_mesh";
    case CfdMeshProblem::NonPositiveCellVolume:
        return "non_positive_cell_volume";
    case CfdMeshProblem::NonManifoldFace:
        return "non_manifold_face";
    case CfdMeshProblem::InconsistentFaceOrientation:
        return "inconsistent_face_orientation";
    case CfdMeshProblem::DisconnectedCells:
        return "disconnected_cells";
    case CfdMeshProblem::InvalidFaceOwnership:
        return "invalid_face_ownership";
    case CfdMeshProblem::InwardBoundaryFace:
        return "inward_boundary_face";
    case CfdMeshProblem::UnclosedDomain:
        return "unclosed_domain";
    case CfdMeshProblem::UnattributedBoundary:
        return "unattributed_boundary";
    }
    return "unknown";
}

CfdMeshInspection inspectCfdMesh(const meshing::Mesh& mesh, const meshing::GeometryMeshMappingReport& mapping) {
    CfdMeshInspection inspection;
    inspection.census.cells = mesh.tetrahedra().size();
    inspection.census.boundaryTriangles = mesh.triangles().size();

    Tally empty;
    Tally volume;
    Tally manifold;
    Tally orientation;
    Tally disconnected;
    Tally ownership;
    Tally inward;
    Tally unclosed;
    Tally attribution;

    if (mesh.tetrahedra().empty()) {
        empty.add("the mesh has no tetrahedra, so there is no cell");
    }

    // ONE PASS OVER THE CELLS, in the mesh's own ascending ElementId order: the
    // volume of each, and every one of its four faces into an ORDERED map keyed by
    // the sorted node handles. Nothing here depends on an address or a hash.
    std::map<FaceKey, FaceRecord> faces;
    CellSets sets(mesh.tetrahedra().size());
    std::size_t slot = 0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const std::size_t cell = slot++;
        std::array<const meshing::Node*, 4> nodes{};
        for (std::size_t i = 0; i < 4; ++i) {
            nodes[i] = mesh.findNode(tet.nodes[i]);
        }
        // MeshBuilder refuses an element naming a missing node, so this cannot be
        // null for a built mesh; checked rather than dereferenced on faith.
        if (std::ranges::any_of(nodes, [](const meshing::Node* node) { return node == nullptr; })) {
            volume.add(std::format("tetrahedron {} names a node the mesh does not hold", tet.id.value()));
            continue;
        }
        const Volume v = meshing::signedVolume(nodes[0]->position, nodes[1]->position, nodes[2]->position,
                                               nodes[3]->position);
        // NO ABSOLUTE VALUE: a negative volume is an inverted cell, not a small one.
        if (!std::isfinite(v.si()) || v.si() <= 0.0) {
            volume.add(std::format("tetrahedron {} has a signed volume of {} m^3", tet.id.value(), v.si()));
        }
        for (std::size_t k = 0; k < kTetFaces.size(); ++k) {
            const auto& local = kTetFaces[k];
            const std::array<NodeId, 3> wound{tet.nodes[local[0]], tet.nodes[local[1]], tet.nodes[local[2]]};
            const FaceKey key = keyOf(wound);
            FaceRecord& record = faces[key];
            if (record.claims++ == 0) {
                record.owner = tet.id;
                record.ownerSlot = cell;
                record.apex = tet.nodes[kTetFaceOpposite[k]];
                record.wound = wound;
            } else {
                // A shared face joins its cells. (A third claimant is refused
                // below as non-manifold; joining it too does not hide that.)
                sets.join(record.ownerSlot, cell);
                // The NEIGHBOUR must traverse the face against the owner: both
                // wind it outward of themselves, so if they agree, both lie on
                // the same side of it. Checked for the second claimant; a third
                // is already non-manifold.
                if (record.claims == 2 && !reversed(record.wound, wound)) {
                    // Worded as the combinatorial fact: with an inverted cell
                    // the two may lie on opposite sides and still agree.
                    orientation.add(std::format("cells {} and {} traverse their shared face {} in the same direction",
                                                record.owner.value(), tet.id.value(), triple(key)));
                }
            }
        }
    }

    for (const auto& [key, record] : faces) {
        if (record.claims == 1) {
            ++inspection.census.boundaryFaces;
        } else if (record.claims == 2) {
            ++inspection.census.interiorFaces;
        } else {
            ++inspection.census.nonManifoldFaces;
            manifold.add(std::format("the face {} is shared by {} cells", triple(key), record.claims));
        }
    }

    // ONE FACE-CONNECTED SET OF CELLS. Counted over every cell, in the mesh's
    // order; the first cell outside the first cell's set is the one named.
    std::optional<std::size_t> stray;
    for (std::size_t cell = 0; cell < mesh.tetrahedra().size(); ++cell) {
        const std::size_t root = sets.root(cell);
        if (root == cell) {
            ++inspection.census.components;
        }
        if (root != 0 && !stray) {
            stray = cell;
        }
    }
    if (inspection.census.components > 1) {
        disconnected.add(std::format("the cells form {} face-connected pieces; cell {} shares no chain of faces "
                                     "with cell {}",
                                     inspection.census.components, mesh.tetrahedra()[*stray].id.value(),
                                     mesh.tetrahedra().front().id.value()),
                         inspection.census.components - 1);
    }

    // EVERY BOUNDARY TRIANGLE MUST BE A FACE OF EXACTLY ONE CELL, CLAIMED ONCE, AND
    // WOUND OUT OF THAT CELL.
    std::map<FaceKey, ElementId> covered;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        const FaceKey key = keyOf(triangle.nodes);
        const auto face = faces.find(key);
        if (face == faces.end()) {
            ownership.add(std::format("boundary triangle {} is a face of no cell", triangle.id.value()));
            continue;
        }
        if (face->second.claims != 1) {
            ownership.add(std::format("boundary triangle {} lies on a face shared by {} cells", triangle.id.value(),
                                      face->second.claims));
            continue;
        }
        if (const auto [it, inserted] = covered.emplace(key, triangle.id); !inserted) {
            ownership.add(std::format("boundary triangles {} and {} claim the same face", it->second.value(),
                                      triangle.id.value()));
            continue;
        }
        // Outward means the triangle's right-hand normal points AWAY from the
        // owning cell's opposite vertex: n . (apex - p1) < 0. Zero (or not a
        // number) is a degenerate triangle against a flat cell: not outward
        // either, and said so in its own words.
        const meshing::Node* p1 = mesh.findNode(triangle.nodes[0]);
        const meshing::Node* p2 = mesh.findNode(triangle.nodes[1]);
        const meshing::Node* p3 = mesh.findNode(triangle.nodes[2]);
        const meshing::Node* apex = mesh.findNode(face->second.apex);
        if (p1 == nullptr || p2 == nullptr || p3 == nullptr || apex == nullptr) {
            ownership.add(std::format("boundary triangle {} names a node the mesh does not hold", triangle.id.value()));
            continue;
        }
        const auto a = si(p1->position);
        const auto normal = cross(minus(si(p2->position), a), minus(si(p3->position), a));
        const double toward = dot(normal, minus(si(apex->position), a));
        if (toward > 0.0) {
            inward.add(std::format("boundary triangle {} is wound into cell {}", triangle.id.value(),
                                   face->second.owner.value()));
        } else if (!(toward < 0.0)) {
            inward.add(std::format("boundary triangle {} is degenerate against cell {}: it has no outward side",
                                   triangle.id.value(), face->second.owner.value()));
        }
    }

    // A face one cell owns that no boundary triangle covers: the domain is open.
    for (const auto& [key, record] : faces) {
        if (record.claims == 1 && !covered.contains(key)) {
            unclosed.add(std::format("the face {} of cell {} is on no boundary triangle", triple(key),
                                     record.owner.value()));
        }
    }

    // THE ATTRIBUTION, AS THE MAP REPORTED IT. Exact-position attribution is P16's;
    // what is refused here is any gap in it, because the alternative -- giving an
    // unattributed facet to the nearest CAD face -- is the silent rebind ADR-032
    // forbids.
    if (!mapping.complete()) {
        // At least 1: a report with no facet at all is incomplete with every
        // defect count zero.
        const std::size_t defects =
            mapping.unmappedFacetCount + mapping.facetsWithSeveralFaces + mapping.facesWithoutFacets;
        attribution.add(std::format("{} boundary facet(s) attributed to no CAD face, {} to several, {} CAD face(s) "
                                    "with no facet, {} boundary facet(s) in all",
                                    mapping.unmappedFacetCount, mapping.facetsWithSeveralFaces,
                                    mapping.facesWithoutFacets, mapping.boundaryFacetCount),
                        std::max<std::size_t>(defects, 1));
    } else if (mapping.boundaryFacetCount != mesh.triangles().size()) {
        attribution.add(std::format("the map attributes {} boundary facet(s) but the mesh holds {}",
                                    mapping.boundaryFacetCount, mesh.triangles().size()));
    }

    const std::array<std::pair<CfdMeshProblem, Tally*>, 9> tallies{{
        {CfdMeshProblem::EmptyMesh, &empty},
        {CfdMeshProblem::NonPositiveCellVolume, &volume},
        {CfdMeshProblem::NonManifoldFace, &manifold},
        {CfdMeshProblem::InconsistentFaceOrientation, &orientation},
        {CfdMeshProblem::DisconnectedCells, &disconnected},
        {CfdMeshProblem::InvalidFaceOwnership, &ownership},
        {CfdMeshProblem::InwardBoundaryFace, &inward},
        {CfdMeshProblem::UnclosedDomain, &unclosed},
        {CfdMeshProblem::UnattributedBoundary, &attribution},
    }};
    for (const auto& [problem, tally] : tallies) {
        if (tally->count > 0) {
            inspection.findings.push_back(CfdMeshFinding{
                .problem = problem,
                .count = tally->count,
                .message = std::format("{} ({}): {}", toString(problem), tally->count, tally->first)});
        }
    }
    return inspection;
}

Error cfdMeshRefusal(const CfdMeshInspection& inspection, MeshControlId control) {
    std::string joined;
    for (const CfdMeshFinding& finding : inspection.findings) {
        if (!joined.empty()) {
            joined += "; ";
        }
        joined += finding.message;
    }
    return Error{ErrorCode::FailedPrecondition,
                 std::format("the mesh of meshing control {} does not satisfy the CFD mesh contract: {}",
                             control.value(), joined)};
}

Result<CfdMeshContract> requireCfdMesh(const CfdModel& model) {
    const meshing::Mesh& mesh = model.mesh().mesh();
    const meshing::GeometryMeshMap& map = model.map();
    const CfdMeshInspection inspection = inspectCfdMesh(mesh, map.report());
    if (!inspection.satisfiesContract()) {
        return std::unexpected(cfdMeshRefusal(inspection, model.control()));
    }
    return CfdMeshContract(mesh.stamp(), model.geometryRevision(), inspection.census, map.faces().size(),
                           map.report().unnamedFaceCount);
}

std::optional<CfdMeshProblem> cfdMeshProblem(const CfdModel& model) {
    return inspectCfdMesh(model.mesh().mesh(), model.map().report()).firstProblem();
}

} // namespace bettercad::cfd
