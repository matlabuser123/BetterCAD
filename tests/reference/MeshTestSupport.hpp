#pragma once

#include "reference/Analytic.hpp"

#include <MeshReferenceModels.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/MeshValidation.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <numeric>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Checks shared by the meshing reference models (P16-REFMOD-001).
//
// EVERY STRUCTURAL CHECK HERE IS COMPUTED FROM THE MESH'S OWN NODES AND
// ELEMENTS, and none of it reads what `VolumeMesh` recorded about itself. That
// is the whole point of a reference suite over a milestone whose central claim
// is that `generateVolumeMesh` REFUSES a mesh with an inverted, degenerate or
// duplicate element: a suite that asserted `mesh.conformity().conforms()` and
// stopped would be asking the implementation whether it had done its job.
//
// So the signed volumes are determinants worked out here, the boundary is
// derived from the tetrahedra's own face incidence here, and the enclosed
// volume is a divergence sum over the boundary triangles here. Where the
// production figure is also available it is COMPARED, not substituted.
//
// This is common infrastructure rather than per-model code, which the brief
// requires in as many words: "This is common reference infrastructure, not
// copy-pasted logic per model."
namespace bettercad::test::meshref {

/// Millimetres per metre. Reference dimensions are quoted in mm, mesh
/// coordinates are SI, and the conversion is written once.
inline constexpr double kMmPerM = 1.0e3;

/// A position in millimetres. A plain triple, because everything in this header
/// does arithmetic on coordinates and nothing here needs dimensioned types to
/// do it -- the one conversion from SI happens in `positionMm` and nowhere
/// else.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend bool operator==(const Vec3&, const Vec3&) = default;
};

[[nodiscard]] inline Vec3 positionMm(const Point3D& position) {
    return Vec3{position.x.si() * kMmPerM, position.y.si() * kMmPerM, position.z.si() * kMmPerM};
}

[[nodiscard]] inline Vec3 minus(const Vec3& a, const Vec3& b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] inline double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] inline double norm(const Vec3& a) {
    return std::sqrt(dot(a, a));
}
[[nodiscard]] inline bool finite(const Vec3& a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

/// The node positions of @p mesh, in millimetres, by handle.
[[nodiscard]] inline std::map<meshing::NodeId::ValueType, Vec3> nodePositions(const meshing::Mesh& mesh) {
    std::map<meshing::NodeId::ValueType, Vec3> positions;
    for (const meshing::Node& node : mesh.nodes()) {
        positions.emplace(node.id.value(), positionMm(node.position));
    }
    return positions;
}

/// The signed volume of a tetrahedron in mm^3,
///
///     V = 1/6 (p2 - p1) . ((p3 - p1) x (p4 - p1)),
///
/// worked out HERE rather than through `meshing::signedVolume`. The sign is the
/// only evidence that a generator produced an inverted element, so the suite
/// computes it independently and compares; a reference model that called the
/// production function would be asserting that the production function agrees
/// with itself.
[[nodiscard]] inline double signedVolumeMm3(const Vec3& p1, const Vec3& p2, const Vec3& p3,
                                            const Vec3& p4) {
    return dot(minus(p2, p1), cross(minus(p3, p1), minus(p4, p1))) / 6.0;
}

/// The orientation census the brief asks for: how many tetrahedra have
/// positive, exactly zero and negative signed volume.
struct Orientation {
    std::size_t positive = 0;
    std::size_t zero = 0;
    std::size_t negative = 0;
    std::size_t nonFinite = 0;
    /// mm^3, over the positive elements; zero when there are none.
    double minVolume = 0.0;
    double maxVolume = 0.0;
    /// The sum of the SIGNED volumes, so an inverted element drags it down.
    double totalVolume = 0.0;
    /// The element holding the smallest positive volume.
    meshing::ElementId smallest{};
};

[[nodiscard]] inline Orientation orientationCensus(const meshing::Mesh& mesh) {
    const auto positions = nodePositions(mesh);
    Orientation census;
    bool first = true;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        std::array<Vec3, 4> p{};
        bool resolved = true;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto found = positions.find(tet.nodes[i].value());
            if (found == positions.end()) {
                resolved = false;
                break;
            }
            p[i] = found->second;
        }
        if (!resolved) {
            ++census.nonFinite;
            continue;
        }
        const double volume = signedVolumeMm3(p[0], p[1], p[2], p[3]);
        if (!std::isfinite(volume)) {
            ++census.nonFinite;
            continue;
        }
        census.totalVolume += volume;
        if (volume > 0.0) {
            ++census.positive;
            if (first || volume < census.minVolume) {
                census.minVolume = volume;
                census.smallest = tet.id;
                first = false;
            }
            census.maxVolume = std::max(census.maxVolume, volume);
        } else if (volume < 0.0) {
            ++census.negative;
        } else {
            ++census.zero;
        }
    }
    return census;
}

/// The volume the boundary triangles enclose, in mm^3, by the divergence
/// theorem: V = 1/6 sum p1 . (p2 x p3) over the stored winding.
///
/// POSITIVE MEANS THE TRIANGLES FACE OUT OF THE MATERIAL, which is the property
/// the hole-wall normal check below depends on and which no field of
/// `VolumeMesh` states. Computed from the stored node order, never from a
/// canonicalised one -- sorting the handles would silently flip half of them.
[[nodiscard]] inline double enclosedVolumeMm3(const meshing::Mesh& mesh) {
    const auto positions = nodePositions(mesh);
    double sum = 0.0;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        std::array<Vec3, 3> p{};
        bool resolved = true;
        for (std::size_t i = 0; i < 3; ++i) {
            const auto found = positions.find(triangle.nodes[i].value());
            if (found == positions.end()) {
                resolved = false;
                break;
            }
            p[i] = found->second;
        }
        if (resolved) {
            sum += dot(p[0], cross(p[1], p[2])) / 6.0;
        }
    }
    return sum;
}

/// Whether every undirected face of the tetrahedra is used by one or two
/// tetrahedra, and whether the one-sided set is exactly the triangle set.
///
/// The boundary of a tetrahedralisation, derived from its OWN incidence -- the
/// independent form of the check `VolumeConformity` reports, so that the
/// suite's verdict does not come from the thing it is checking.
struct Incidence {
    std::size_t interiorFaces = 0;
    std::size_t boundaryFaces = 0;
    /// Faces used by three or more tetrahedra: a non-manifold interior.
    std::size_t overusedFaces = 0;
    /// Boundary faces that are not a stored Triangle3 of the mesh, and stored
    /// triangles that are not a boundary face of the tetrahedra.
    std::size_t boundaryFacesWithoutTriangle = 0;
    std::size_t trianglesWithoutBoundaryFace = 0;

    [[nodiscard]] bool consistent() const noexcept {
        return overusedFaces == 0 && boundaryFacesWithoutTriangle == 0 &&
               trianglesWithoutBoundaryFace == 0;
    }
};

[[nodiscard]] inline Incidence faceIncidence(const meshing::Mesh& mesh) {
    using Face = std::array<meshing::NodeId::ValueType, 3>;
    auto sorted = [](meshing::NodeId a, meshing::NodeId b, meshing::NodeId c) {
        Face face{a.value(), b.value(), c.value()};
        std::ranges::sort(face);
        return face;
    };
    // The four faces of a tetrahedron, as node ordinals.
    static constexpr std::array<std::array<std::size_t, 3>, 4> kFaces{
        {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};

    std::map<Face, std::size_t> uses;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        for (const std::array<std::size_t, 3>& face : kFaces) {
            ++uses[sorted(tet.nodes[face[0]], tet.nodes[face[1]], tet.nodes[face[2]])];
        }
    }
    std::set<Face> triangles;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        triangles.insert(sorted(triangle.nodes[0], triangle.nodes[1], triangle.nodes[2]));
    }

    Incidence incidence;
    std::set<Face> boundary;
    for (const auto& [face, count] : uses) {
        if (count == 1) {
            ++incidence.boundaryFaces;
            boundary.insert(face);
            if (!triangles.contains(face)) {
                ++incidence.boundaryFacesWithoutTriangle;
            }
        } else if (count == 2) {
            ++incidence.interiorFaces;
        } else {
            ++incidence.overusedFaces;
        }
    }
    for (const Face& face : triangles) {
        if (!boundary.contains(face)) {
            ++incidence.trianglesWithoutBoundaryFace;
        }
    }
    return incidence;
}

/// Everything the brief's shared structural checks ask for, measured.
struct Structure {
    std::size_t nodes = 0;
    std::size_t tets = 0;
    std::size_t triangles = 0;
    std::size_t nonFiniteCoordinates = 0;
    std::size_t missingNodeReferences = 0;
    std::size_t repeatedNodeReferences = 0;
    std::size_t duplicateTets = 0;
    std::size_t elementsWithoutRegion = 0;
    /// Elements whose type is neither Tet4 nor Triangle3. A volume mesh of this
    /// milestone has exactly those two, and an element of any other type would
    /// be a mesh this suite is not describing.
    std::size_t unexpectedElementTypes = 0;
    Orientation orientation{};
    Incidence incidence{};
    /// mm^3, from the boundary triangles' winding.
    double enclosedVolume = 0.0;

    /// Every structural invariant holds.
    [[nodiscard]] bool sound() const noexcept {
        return nodes > 0 && tets > 0 && triangles > 0 && nonFiniteCoordinates == 0 &&
               missingNodeReferences == 0 && repeatedNodeReferences == 0 && duplicateTets == 0 &&
               elementsWithoutRegion == 0 && unexpectedElementTypes == 0 && orientation.zero == 0 &&
               orientation.negative == 0 && orientation.nonFinite == 0 &&
               orientation.positive == tets && incidence.consistent() && enclosedVolume > 0.0;
    }
};

[[nodiscard]] inline Structure auditStructure(const meshing::Mesh& mesh) {
    Structure structure;
    structure.nodes = mesh.nodeCount();
    structure.tets = mesh.tetrahedra().size();
    structure.triangles = mesh.triangles().size();

    for (const meshing::Node& node : mesh.nodes()) {
        if (!finite(positionMm(node.position))) {
            ++structure.nonFiniteCoordinates;
        }
    }
    const auto positions = nodePositions(mesh);
    std::set<std::array<meshing::NodeId::ValueType, 4>> seen;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        std::array<meshing::NodeId::ValueType, 4> handles{};
        for (std::size_t i = 0; i < 4; ++i) {
            handles[i] = tet.nodes[i].value();
            if (!positions.contains(handles[i])) {
                ++structure.missingNodeReferences;
            }
        }
        std::array<meshing::NodeId::ValueType, 4> ordered = handles;
        std::ranges::sort(ordered);
        if (std::ranges::adjacent_find(ordered) != ordered.end()) {
            ++structure.repeatedNodeReferences;
        }
        if (!seen.insert(ordered).second) {
            ++structure.duplicateTets;
        }
        if (!tet.region.isValid()) {
            ++structure.elementsWithoutRegion;
        }
    }
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        std::array<meshing::NodeId::ValueType, 3> handles{};
        for (std::size_t i = 0; i < 3; ++i) {
            handles[i] = triangle.nodes[i].value();
            if (!positions.contains(handles[i])) {
                ++structure.missingNodeReferences;
            }
        }
        std::ranges::sort(handles);
        if (std::ranges::adjacent_find(handles) != handles.end()) {
            ++structure.repeatedNodeReferences;
        }
        if (!triangle.region.isValid()) {
            ++structure.elementsWithoutRegion;
        }
    }
    // ELEMENT TYPES, which the brief asks to be recorded: a Tet4 volume mesh
    // with a Triangle3 boundary has exactly those two, and an element of any
    // other type would make every count above describe something else.
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const std::optional<meshing::ElementType> type = mesh.elementType(tet.id);
        if (!type || *type != meshing::ElementType::Tetrahedron4) {
            ++structure.unexpectedElementTypes;
        }
    }
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        const std::optional<meshing::ElementType> type = mesh.elementType(triangle.id);
        if (!type || *type != meshing::ElementType::Triangle3) {
            ++structure.unexpectedElementTypes;
        }
    }
    structure.orientation = orientationCensus(mesh);
    structure.incidence = faceIncidence(mesh);
    structure.enclosedVolume = enclosedVolumeMm3(mesh);
    return structure;
}

/// Edge-length statistics in millimetres: the characteristic scale a sizing
/// control is about.
struct EdgeStats {
    std::size_t count = 0;
    double minimum = 0.0;
    double mean = 0.0;
    double median = 0.0;
    double maximum = 0.0;
};

[[nodiscard]] inline EdgeStats statsOf(std::vector<double> lengths) {
    if (lengths.empty()) {
        return EdgeStats{};
    }
    std::ranges::sort(lengths);
    EdgeStats stats;
    stats.count = lengths.size();
    stats.minimum = lengths.front();
    stats.maximum = lengths.back();
    stats.median = lengths[lengths.size() / 2];
    stats.mean = std::accumulate(lengths.begin(), lengths.end(), 0.0) /
                 static_cast<double>(lengths.size());
    return stats;
}

/// The six edges of a tetrahedron, as node ordinals.
inline constexpr std::array<std::array<std::size_t, 2>, 6> kTetEdges{
    {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}}};

/// Edge lengths, in mm, of the tetrahedra whose handles are in @p which -- or
/// of every tetrahedron when @p which is empty.
[[nodiscard]] inline EdgeStats edgeStatsOf(const meshing::Mesh& mesh,
                                           std::span<const meshing::ElementId> which = {}) {
    const auto positions = nodePositions(mesh);
    std::set<meshing::ElementId::ValueType> wanted;
    for (const meshing::ElementId id : which) {
        wanted.insert(id.value());
    }
    std::vector<double> lengths;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        if (!wanted.empty() && !wanted.contains(tet.id.value())) {
            continue;
        }
        for (const std::array<std::size_t, 2>& edge : kTetEdges) {
            const auto a = positions.find(tet.nodes[edge[0]].value());
            const auto b = positions.find(tet.nodes[edge[1]].value());
            if (a != positions.end() && b != positions.end()) {
                lengths.push_back(norm(minus(a->second, b->second)));
            }
        }
    }
    return statsOf(std::move(lengths));
}

/// The tetrahedra whose centroid lies within @p reach mm of the plane
/// {axis = value}, with axis 0, 1, 2 for x, y, z.
///
/// WHY A GEOMETRIC BAND AND NOT THE FACE'S OWN FACETS, which would read better.
/// OCCT triangulates a PLANAR face with two triangles whatever the deflection,
/// so a block's face has exactly two facets and the tetrahedra owning them must
/// span the whole face however fine the interior is. Measured on RM-MESH-07:
/// the owners of the refined face's two facets have a 57.8 mm median edge in a
/// mesh whose element count the control multiplied by twenty-two.
///
/// Local sizing refines the VOLUME near a face -- `BoxSizeRestriction` is a
/// slab reaching inward by one target size, which is P16-SIZE-001's central
/// finding -- so the volume near the face is where it has to be measured. The
/// band must reach inward past the slab, or it measures the transition elements
/// Netgen's grading inserts rather than the refined ones.
[[nodiscard]] inline std::vector<meshing::ElementId> tetsNearPlane(const meshing::Mesh& mesh, int axis,
                                                                   double value, double reach) {
    const auto positions = nodePositions(mesh);
    std::vector<meshing::ElementId> found;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        Vec3 sum;
        bool resolved = true;
        for (const meshing::NodeId id : tet.nodes) {
            const auto node = positions.find(id.value());
            if (node == positions.end()) {
                resolved = false;
                break;
            }
            sum.x += node->second.x;
            sum.y += node->second.y;
            sum.z += node->second.z;
        }
        if (!resolved) {
            continue;
        }
        const std::array<double, 3> centroid{sum.x / 4.0, sum.y / 4.0, sum.z / 4.0};
        if (std::abs(centroid[static_cast<std::size_t>(axis)] - value) <= reach) {
            found.push_back(tet.id);
        }
    }
    return found;
}

/// The nodes within @p reach mm of the plane {axis = value}: the density half
/// of the refinement measurement, which an edge-length median cannot show.
[[nodiscard]] inline std::size_t nodesNearPlane(const meshing::Mesh& mesh, int axis, double value,
                                                double reach) {
    std::size_t count = 0;
    for (const meshing::Node& node : mesh.nodes()) {
        const Vec3 p = positionMm(node.position);
        const std::array<double, 3> coordinates{p.x, p.y, p.z};
        if (std::abs(coordinates[static_cast<std::size_t>(axis)] - value) <= reach) {
            ++count;
        }
    }
    return count;
}

/// The centroid of a tetrahedron, in mm, given an already-built position map.
///
/// TAKES THE MAP rather than the mesh, and that is not a style choice: an
/// earlier version took the mesh and called `nodePositions` itself, so a caller
/// looping over elements rebuilt the whole map once per element. Measured, the
/// undo/redo case -- which meshes a cylinder four times, twice at about two
/// thousand tetrahedra -- spent most of 53 seconds in that loop.
[[nodiscard]] inline Vec3 centroidMm(const std::map<meshing::NodeId::ValueType, Vec3>& positions,
                                     const meshing::Tetrahedron& tet) {
    Vec3 sum;
    for (const meshing::NodeId id : tet.nodes) {
        const auto found = positions.find(id.value());
        if (found == positions.end()) {
            return Vec3{};
        }
        sum.x += found->second.x;
        sum.y += found->second.y;
        sum.z += found->second.z;
    }
    return Vec3{sum.x / 4.0, sum.y / 4.0, sum.z / 4.0};
}

/// The centroid of a tetrahedron of @p mesh, for a one-off caller.
[[nodiscard]] inline Vec3 centroidMm(const meshing::Mesh& mesh, const meshing::Tetrahedron& tet) {
    return centroidMm(nodePositions(mesh), tet);
}

/// A cylindrical region that must contain no material at all.
///
/// HOW THE SAFE RADIUS IS DERIVED, because this is the check that proves a hole
/// is a hole and the brief forbids resting on total volume alone. The void is
/// bounded by a POLYGON inscribed in the circle of radius r, so material does
/// legitimately come closer to the axis than r -- as close as the chords. A
/// chord whose deepest deviation from the arc is at most the declared surface
/// deflection d has its closest approach to the axis at r - d, so the void
/// certainly contains the disc of radius r - d.
///
/// Anything inside THAT disc is in the void with no appeal: not a tolerance
/// choice, a consequence of the deflection the model declares.
struct VoidRegion {
    /// Axis position in mm; the axis runs along Z.
    double axisX = 0.0;
    double axisY = 0.0;
    /// The true hole radius, mm.
    double radius = 0.0;
    /// The declared surface deflection, mm.
    double deflection = 0.0;

    [[nodiscard]] double safeRadius() const noexcept { return radius - deflection; }
};

struct VoidOccupancy {
    /// Nodes strictly inside the safe disc. Must be zero.
    std::size_t nodesInside = 0;
    /// Tetrahedron centroids strictly inside the safe disc. Must be zero.
    std::size_t centroidsInside = 0;
    /// Centroids between the safe radius and the true radius: legitimately
    /// material, because the void's boundary is a chord polygon. Reported so
    /// that a reader can see the check is not vacuous.
    std::size_t centroidsInChordBand = 0;
    /// The closest any node comes to the axis, mm.
    double closestNode = 0.0;

    [[nodiscard]] std::size_t violations() const noexcept { return nodesInside + centroidsInside; }
};

[[nodiscard]] inline VoidOccupancy checkVoid(const meshing::Mesh& mesh, const VoidRegion& region) {
    VoidOccupancy occupancy;
    bool first = true;
    auto radial = [&](const Vec3& p) { return std::hypot(p.x - region.axisX, p.y - region.axisY); };
    for (const meshing::Node& node : mesh.nodes()) {
        const double distance = radial(positionMm(node.position));
        if (first || distance < occupancy.closestNode) {
            occupancy.closestNode = distance;
            first = false;
        }
        if (distance < region.safeRadius()) {
            ++occupancy.nodesInside;
        }
    }
    // Built ONCE, for the whole element loop.
    const std::map<meshing::NodeId::ValueType, Vec3> positions = nodePositions(mesh);
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const double distance = radial(centroidMm(positions, tet));
        if (distance < region.safeRadius()) {
            ++occupancy.centroidsInside;
        } else if (distance < region.radius) {
            ++occupancy.centroidsInChordBand;
        }
    }
    return occupancy;
}

/// Whether every facet of a cylindrical wall faces the way the MATERIAL
/// requires, which for a hole's wall is TOWARD the axis.
///
/// The brief's warning in full: "Do not confuse radial outward from the
/// cylinder axis with outward from material." A bore's wall faces inward
/// radially, and a boss's wall faces outward, and a check that assumed the
/// radial direction would pass one and fail the other.
struct WallOrientation {
    std::size_t facets = 0;
    /// Facets whose outward normal points away from the axis.
    std::size_t facingAway = 0;
    /// Facets whose outward normal points toward the axis.
    std::size_t facingAxis = 0;
    /// Facets whose normal is (numerically) radial-neutral: a cap facet caught
    /// in a wall set would land here, which is why it is counted.
    std::size_t radiallyNeutral = 0;
};

[[nodiscard]] inline WallOrientation wallOrientation(const meshing::Mesh& mesh,
                                                     std::span<const meshing::ElementId> facets,
                                                     double axisX, double axisY) {
    const auto positions = nodePositions(mesh);
    WallOrientation orientation;
    for (const meshing::ElementId id : facets) {
        const meshing::Triangle* triangle = mesh.findTriangle(id);
        if (triangle == nullptr) {
            continue;
        }
        std::array<Vec3, 3> p{};
        bool resolved = true;
        for (std::size_t i = 0; i < 3; ++i) {
            const auto found = positions.find(triangle->nodes[i].value());
            if (found == positions.end()) {
                resolved = false;
                break;
            }
            p[i] = found->second;
        }
        if (!resolved) {
            continue;
        }
        ++orientation.facets;
        const Vec3 normal = cross(minus(p[1], p[0]), minus(p[2], p[0]));
        const Vec3 mid{(p[0].x + p[1].x + p[2].x) / 3.0, (p[0].y + p[1].y + p[2].y) / 3.0, 0.0};
        const Vec3 outward{mid.x - axisX, mid.y - axisY, 0.0};
        const double projection = dot(normal, outward) / (norm(normal) * norm(outward));
        if (projection > 1e-9) {
            ++orientation.facingAway;
        } else if (projection < -1e-9) {
            ++orientation.facingAxis;
        } else {
            ++orientation.radiallyNeutral;
        }
    }
    return orientation;
}

/// @p point turned by the placement's triad and moved by its origin:
/// R p + t, with R's COLUMNS the triad's axes, which is how a sketch plane maps
/// local (u, v, w) to model space.
///
/// The suite's own transform. It takes the placement as data, so it agrees with
/// the frame the model was built on by construction and not by coincidence.
[[nodiscard]] inline Vec3 placed(const Vec3& point, const reference::RigidPlacement& placement) {
    const std::array<double, 3>& x = placement.xAxis;
    const std::array<double, 3>& y = placement.yAxis;
    const std::array<double, 3>& n = placement.normal;
    return Vec3{point.x * x[0] + point.y * y[0] + point.z * n[0] + placement.origin[0],
                point.x * x[1] + point.y * y[1] + point.z * n[1] + placement.origin[1],
                point.x * x[2] + point.y * y[2] + point.z * n[2] + placement.origin[2]};
}

/// The handles of the nodes a boundary triangle references: the nodes whose
/// positions the CAD geometry determines.
///
/// THE DISTINCTION MATTERS FOR THE RIGID-TRANSFORM GATE. A boundary node is a
/// vertex of the kernel's triangulation of a CAD face, so under a rigid
/// transform of the body it must move with the body to rounding. An INTERIOR
/// node is the backend's own choice, computed from world coordinates by an
/// algorithm with no equivariance obligation -- measured on RM-MESH-06, Netgen
/// places the block's single interior node 2.7e-4 mm away from the transformed
/// position of the base mesh's. Comparing the two populations under one
/// tolerance would mean either failing a correct mesh or loosening the check
/// that actually proves the body moved.
[[nodiscard]] inline std::set<meshing::NodeId::ValueType> boundaryNodeHandles(
    const meshing::Mesh& mesh) {
    std::set<meshing::NodeId::ValueType> handles;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        for (const meshing::NodeId id : triangle.nodes) {
            handles.insert(id.value());
        }
    }
    return handles;
}

/// A mesh's node positions in a CANONICAL order -- lexicographic by coordinate,
/// not by handle.
///
/// Node handles are mesh-local and a remesh reassigns them, so two meshes can
/// only be compared as point SETS. Sorting by position is the canonicalisation
/// the determinism contract allows; inventing a handle correspondence is what
/// the brief forbids.
[[nodiscard]] inline std::vector<Vec3> sortedByPosition(std::vector<Vec3> points) {
    std::ranges::sort(points, [](const Vec3& a, const Vec3& b) {
        if (a.x != b.x) {
            return a.x < b.x;
        }
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.z < b.z;
    });
    return points;
}

/// Which nodes of a mesh to compare.
enum class NodeSelection {
    All,
    /// Only the nodes a boundary triangle references: the CAD-determined ones.
    Boundary,
    /// Only the nodes no boundary triangle references: the backend's own.
    Interior,
};

[[nodiscard]] inline std::vector<Vec3> canonicalNodes(const meshing::Mesh& mesh,
                                                      NodeSelection selection = NodeSelection::All) {
    const std::set<meshing::NodeId::ValueType> boundary =
        selection == NodeSelection::All ? std::set<meshing::NodeId::ValueType>{}
                                        : boundaryNodeHandles(mesh);
    std::vector<Vec3> points;
    points.reserve(mesh.nodeCount());
    for (const meshing::Node& node : mesh.nodes()) {
        const bool onBoundary = boundary.contains(node.id.value());
        if (selection == NodeSelection::Boundary && !onBoundary) {
            continue;
        }
        if (selection == NodeSelection::Interior && onBoundary) {
            continue;
        }
        points.push_back(positionMm(node.position));
    }
    return sortedByPosition(std::move(points));
}

/// The largest distance between corresponding entries of two canonical point
/// sets, in mm. Infinite when the sets differ in size, because there is then
/// no correspondence to measure and reporting a small number would be a lie.
[[nodiscard]] inline double largestNodeGap(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    if (a.size() != b.size()) {
        return std::numeric_limits<double>::infinity();
    }
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, norm(minus(a[i], b[i])));
    }
    return worst;
}

/// The dimensionless shape metrics of a mesh, as extremes.
///
/// WHY THESE THREE AND NOT THE ASPECT RATIO ALONE. `TetAspectRatio` is
/// l_max / l_min, which on a box saturates near sqrt(3) however badly shaped
/// the elements are -- so a suite that graded thinness by it would pass a mesh
/// of slivers. The radius ratio 3r/R and the extreme dihedral angles do see a
/// sliver, and they are what RM-MESH-05's comparison is made on. The aspect
/// ratio is carried for completeness, not as the gate.
struct ShapeExtremes {
    double worstAspectRatio = 0.0;   ///< largest l_max / l_min
    double worstRadiusRatio = 0.0;   ///< smallest 3r / R
    double minDihedralDeg = 0.0;     ///< smallest internal dihedral
    double maxDihedralDeg = 0.0;     ///< largest internal dihedral
    double minVolumeMm3 = 0.0;
    meshing::ElementId worstRadiusRatioElement{};
    meshing::ElementId minDihedralElement{};
};

/// Read from a quality report's own per-element metrics.
///
/// The report is the production answer and that is deliberate here: these are
/// QUALITY numbers, whose definitions are P16-QUALITY-001's qualified contract,
/// and a reference suite that redefined the dihedral angle would be measuring
/// something else. The structural facts -- the ones that decide whether a mesh
/// is admissible at all -- are the ones this header computes for itself.
[[nodiscard]] inline ShapeExtremes shapeExtremesOf(const meshing::MeshQualityReport& report) {
    ShapeExtremes extremes;
    bool first = true;
    for (const meshing::TetQuality& tet : report.tets) {
        if (!tet.defined) {
            continue;
        }
        const double dihedralMin = tet.minDihedral.si() * 180.0 / analytic::pi;
        const double dihedralMax = tet.maxDihedral.si() * 180.0 / analytic::pi;
        if (first) {
            extremes.worstAspectRatio = tet.aspectRatio;
            extremes.worstRadiusRatio = tet.radiusRatio;
            extremes.minDihedralDeg = dihedralMin;
            extremes.maxDihedralDeg = dihedralMax;
            extremes.minVolumeMm3 = tet.volume.si() * kMmPerM * kMmPerM * kMmPerM;
            extremes.worstRadiusRatioElement = tet.element;
            extremes.minDihedralElement = tet.element;
            first = false;
            continue;
        }
        extremes.worstAspectRatio = std::max(extremes.worstAspectRatio, tet.aspectRatio);
        if (tet.radiusRatio < extremes.worstRadiusRatio) {
            extremes.worstRadiusRatio = tet.radiusRatio;
            extremes.worstRadiusRatioElement = tet.element;
        }
        if (dihedralMin < extremes.minDihedralDeg) {
            extremes.minDihedralDeg = dihedralMin;
            extremes.minDihedralElement = tet.element;
        }
        extremes.maxDihedralDeg = std::max(extremes.maxDihedralDeg, dihedralMax);
        extremes.minVolumeMm3 =
            std::min(extremes.minVolumeMm3, tet.volume.si() * kMmPerM * kMmPerM * kMmPerM);
    }
    return extremes;
}

/// One reference model, regenerated and ready to mesh.
///
/// It owns its own document, regenerator and mesher, so no reference case can
/// depend on another having run -- which the brief requires: "Do not make
/// RM-MESH-03 depend on RM-MESH-01 having run first."
class MeshedReference {
public:
    explicit MeshedReference(Document document) : document_(std::move(document)) {
        regenerate();
        const std::optional<ObjectId> found = document_.findByName("Mesh");
        REQUIRE(found.has_value());
        control_ = *found;
        REQUIRE(definition() != nullptr);
    }

    /// Regenerates everything, without requiring success: RM-MESH-08's body is
    /// meant to fail.
    features::RegenerationReport regenerate() {
        auto report = regenerator_.regenerateAll(document_);
        REQUIRE(report.has_value());
        return *report;
    }

    [[nodiscard]] Document& document() noexcept { return document_; }
    [[nodiscard]] const Document& document() const noexcept { return document_; }
    [[nodiscard]] features::Regenerator& regenerator() noexcept { return regenerator_; }
    [[nodiscard]] meshing::Mesher& mesher() noexcept { return mesher_; }
    [[nodiscard]] ObjectId controlObject() const noexcept { return control_; }
    [[nodiscard]] MeshControlId control() const noexcept {
        return MeshControlId::fromValue(control_.value());
    }
    [[nodiscard]] const meshing::MeshControl* definition() const noexcept {
        return document_.findObjectAs<meshing::MeshControl>(control_);
    }
    /// The body the control meshes.
    [[nodiscard]] ObjectId body() const {
        const meshing::MeshControl* control = definition();
        REQUIRE(control != nullptr);
        return control->definition().body;
    }

    /// Generates the mesh from the document's own intent.
    [[nodiscard]] Result<const meshing::VolumeMesh*> generate() {
        return mesher_.generate(document_, regenerator_, control());
    }

    /// Generates and requires success.
    const meshing::VolumeMesh& require() {
        auto mesh = generate();
        if (!mesh) {
            FAIL("meshing refused: " << mesh.error().message);
        }
        return **mesh;
    }

    [[nodiscard]] meshing::MeshCurrency currency() const {
        return mesher_.currency(document_, control());
    }
    [[nodiscard]] const meshing::GeometryMeshMap& map() const {
        const meshing::GeometryMeshMap* found = mesher_.map(control());
        REQUIRE(found != nullptr);
        return *found;
    }
    [[nodiscard]] const meshing::MeshQualityReport& quality() const {
        const meshing::MeshQualityReport* found = mesher_.quality(control());
        REQUIRE(found != nullptr);
        return *found;
    }

    /// One mesh built with @p controls instead of the document's intent, so a
    /// test can vary the sizing without editing the model.
    ///
    /// The ORDINARY entry point, so the geometry preparation step cannot be
    /// skipped -- the same call the mesher itself makes.
    [[nodiscard]] Result<meshing::VolumeMesh> meshWith(const meshing::VolumeMeshControls& controls) const {
        return meshing::volumeMeshFor(document_, regenerator_, body(), controls);
    }

    [[nodiscard]] meshing::VolumeMesh requireWith(const meshing::VolumeMeshControls& controls) const {
        auto mesh = meshWith(controls);
        if (!mesh) {
            FAIL("meshing refused: " << mesh.error().message);
        }
        return std::move(*mesh);
    }

private:
    Document document_;
    features::Regenerator regenerator_;
    meshing::Mesher mesher_;
    ObjectId control_{};
};

/// The structured result the brief's result schema asks for, as one value a
/// test fills and the evidence tables are written from.
struct ReferenceMeshRun {
    std::string modelId;
    std::string modelName;
    bool expectedMesh = false;
    bool producedMesh = false;
    double analyticVolumeMm3 = 0.0;
    double cadVolumeMm3 = 0.0;
    double meshVolumeMm3 = 0.0;
    double relativeVolumeError = 0.0;
    std::size_t nodeCount = 0;
    std::size_t elementCount = 0;
    std::size_t boundaryFacetCount = 0;
    double minElementVolumeMm3 = 0.0;
    Structure structure{};
    ShapeExtremes shape{};
    std::size_t invalidElements = 0;
    std::size_t warningElements = 0;
    std::size_t failureElements = 0;
    std::size_t unmappedFacets = 0;
    std::size_t ambiguousFacets = 0;
    std::string diagnostic;
};

/// One row of RESULTS.md, so the evidence is transcribed rather than retyped.
[[nodiscard]] inline std::string resultRow(const ReferenceMeshRun& run) {
    if (!run.expectedMesh) {
        return std::format("| {} | refusal | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |",
                           run.modelId);
    }
    return std::format("| {} | mesh | {} | {} | {:.17g} | {:.17g} | {:.3e} | {:.6g} | {} | {} | {} | {} |",
                       run.modelId, run.nodeCount, run.elementCount, run.analyticVolumeMm3,
                       run.meshVolumeMm3, run.relativeVolumeError, run.minElementVolumeMm3,
                       run.invalidElements, run.warningElements, run.failureElements,
                       run.boundaryFacetCount);
}

} // namespace bettercad::test::meshref
