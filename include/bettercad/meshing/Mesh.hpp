#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// The engineering mesh data model (P16-DATA-001).
//
// NOT geometry::Mesh. That one (core/geometry/Mesh.hpp) is the kernel's surface
// triangulation of a Body: a triangle soup whose vertices are duplicated per
// face, produced for STL export. This one is the engineering mesh -- identified
// nodes, typed and oriented elements, regions, and validation. P16-ARCH-001
// found that confusing the two is the standard way to get a "mesh" that looks
// right and cannot carry an analysis, so they are different types in different
// modules and neither converts to the other here.
//
// What this header does NOT do, deliberately:
//   * it does not GENERATE a mesh -- P16-SURF-001 and P16-VOL-001 do that;
//   * it holds no CAD reference: a boundary facet's attribution to a CAD face is
//     P16-MAP-001, so nothing here names a FaceName or an ObjectId;
//   * it holds no material value (ADR-028, ADR-032);
//   * it decides nothing about mesh QUALITY beyond data validity -- a sliver is
//     a quality finding (P16-QUALITY-001), a zero-volume or inverted element is
//     a data defect and is refused here;
//   * it computes no staleness: that is the mesher's (ADR-030).
namespace bettercad::meshing {

/// The element types P16 supports.
///
/// ADR-031: "P16 ships Tet4 only, and an element carries its type so that a
/// higher order is an addition rather than a rewrite." Tet4 is the only VOLUME
/// element; Triangle3 is the surface element that carries a boundary.
///
/// Tet10, Hex8 and Wedge6 are deliberately ABSENT rather than reserved. A type
/// in this enum implies a representation this module understands and validates,
/// and an enumerator with no arity, no orientation convention and no validation
/// would be a promise the code does not keep.
enum class ElementType : std::uint8_t {
    /// Three nodes. A surface element.
    Triangle3,
    /// Four nodes. The linear tetrahedron, and P16's only volume element.
    Tetrahedron4,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(ElementType type) noexcept;

/// How many nodes an element of @p type references. Total, not a minimum.
[[nodiscard]] BETTERCAD_MESHING_EXPORT std::size_t nodeCount(ElementType type) noexcept;

/// One node: an identity and a position, and nothing else.
///
/// It carries NO CAD reference. A node lying on a CAD face does not make the
/// face part of the node -- that correspondence is P16-MAP-001's, and putting an
/// ObjectId here would make every node a CAD identity, which ADR-031 forbids.
struct Node {
    NodeId id{};
    /// In the mesh's one coordinate frame (see Mesh). Length per component, SI
    /// internally, so a coordinate cannot be a bare number of unknown unit.
    Point3D position{};

    friend bool operator==(const Node&, const Node&) = default;
};

/// A surface element: three distinct nodes.
///
/// ORIENTATION IS MEANINGFUL AND IS NEVER CANONICALISED. The normal follows the
/// right-hand rule on the stored order,
///
///     n proportional to (p2 - p1) x (p3 - p1)
///
/// so sorting the three handles would silently flip half the triangles of a
/// surface. P16-DATA fixes only this convention; whether a given triangle faces
/// out of the material is decided against the CAD face in P16-MAP-001.
struct Triangle {
    ElementId id{};
    std::array<NodeId, 3> nodes{};
    RegionId region{};

    friend bool operator==(const Triangle&, const Triangle&) = default;
};

/// A volume element: the 4-node linear tetrahedron, four distinct nodes.
///
/// ORIENTATION IS MEANINGFUL AND IS NEVER CANONICALISED, for the same reason and
/// a sharper one: the sign of the signed volume is the only evidence that a
/// generator produced an inverted element, and reordering the handles to make it
/// positive would destroy exactly that evidence. ADR-032 requires positive
/// Jacobians and says a violation is a failure, so an inverted tetrahedron is
/// REFUSED, never repaired.
struct Tetrahedron {
    ElementId id{};
    std::array<NodeId, 4> nodes{};
    RegionId region{};

    friend bool operator==(const Tetrahedron&, const Tetrahedron&) = default;
};

/// The axis-aligned box a mesh's nodes occupy, in the mesh's frame.
struct MeshBounds {
    Point3D min{};
    Point3D max{};

    friend bool operator==(const MeshBounds&, const MeshBounds&) = default;
};

/// Signed volume of the tetrahedron p1 p2 p3 p4.
///
///     V = 1/6 * (p2 - p1) . ((p3 - p1) x (p4 - p1))
///
/// POSITIVE IS THE VALID ORIENTATION (ADR-032: "element Jacobians are
/// positive"). There is no absolute value anywhere in this function or its
/// callers: taking one would make an inverted element indistinguishable from a
/// correct one, which is the defect the sign exists to catch.
///
/// Returns a Volume, so the dimension is checked rather than asserted in a
/// comment: Length^3. The arithmetic is done on SI values and wrapped once,
/// which is the same boundary distance(Point3D, Point3D) already uses.
///
/// Not finite in, not finite out: the caller validates, and validation treats a
/// non-finite result as a failure rather than as a volume.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Volume signedVolume(const Point3D& p1, const Point3D& p2,
                                                           const Point3D& p3, const Point3D& p4) noexcept;

/// Area of the triangle p1 p2 p3: 1/2 ||(p2 - p1) x (p3 - p1)||.
///
/// Unsigned, because a triangle in three dimensions has no sign -- it has a
/// normal, which is the cross product itself and is what orientation means here.
/// Zero area is degeneracy and is refused; which way the normal points is
/// P16-MAP-001's question.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Area triangleArea(const Point3D& p1, const Point3D& p2,
                                                         const Point3D& p3) noexcept;

/// An engineering mesh: nodes, elements, regions.
///
/// IMMUTABLE BY API. Every accessor is const and returns a view or a value;
/// there is no method that moves a node, adds or removes an element, or changes
/// connectivity. A mesh is produced by MeshBuilder and is then finished. This is
/// what discharges P16's requirement that a solver can read a mesh and cannot
/// mutate its topology: P17 holds a `const Mesh&` and there is nothing on it to
/// call that would change the mesh.
///
/// It is deliberately NOT named or marked "solver ready". Data validity is what
/// this module can establish (see MeshValidation.hpp); conformance to geometry
/// and quality are later P16 milestones, and ADR-030's ValidatedMesh -- the
/// token a solver actually takes -- is constructed by the mesher's validating
/// path, not here.
///
/// ORDERING IS DEFINED AND DETERMINISTIC. Nodes are stored and enumerated in
/// ascending NodeId; triangles and tetrahedra each in ascending ElementId; the
/// canonical whole-mesh element order is every Triangle3 then every
/// Tetrahedron4. Nothing here is an unordered container, so the same
/// construction gives the same enumeration in Debug, Release and Debug-shared.
///
/// ONE COORDINATE FRAME. Every node position is in the frame of the body the
/// mesh was built from (ADR-032: "A mesh is in its body's coordinate frame and
/// carries no transform"). There is deliberately no transform field and no
/// per-node frame: a mesh with some nodes local and some world is not
/// representable.
///
/// Copying a mesh is a value copy of the same topology with the same handles and
/// the same MeshStamp. It is not a remesh and does not renumber anything.
///
/// EQUALITY IS EXACT REPRESENTATION EQUALITY, not semantic equivalence: the same
/// stamp, the same handles in the same order, and bit-identical coordinates. Two
/// meshes that discretise the same body identically but were built separately
/// have different MeshIds and so compare unequal. That is the useful meaning
/// here, because construction is deterministic and the property tests want to
/// know that a rebuild produced the same thing; a geometric
/// "is this the same mesh really" comparison would need a tolerance and a node
/// correspondence, and nothing needs it yet.
///
/// THREAD SAFETY, documented rather than built: a finished Mesh is immutable, so
/// any number of threads may read one concurrently. MeshBuilder is not
/// thread-safe and is not meant to be -- one mesh is built by one thread. The
/// only shared state in the module is the MeshId counter, which is atomic
/// because two builders on two threads must not receive the same MeshId.
class BETTERCAD_MESHING_EXPORT Mesh {
public:
    /// An empty mesh with an invalid stamp. Valid as a container; rejected by
    /// solver-facing validation, which is a different question (see
    /// MeshValidation.hpp).
    Mesh() = default;

    /// Which mesh and generation this mesh's handles belong to.
    [[nodiscard]] const MeshStamp& stamp() const noexcept { return stamp_; }

    /// Whether a handle carrying @p stamp may be used against this mesh.
    ///
    /// The refusal ADR-031 requires: a caller that kept a handle across a
    /// possible remesh asks this first, and gets a false it must handle rather
    /// than a silent reinterpretation against different geometry.
    [[nodiscard]] bool owns(const MeshStamp& stamp) const noexcept { return stamp_.isValid() && stamp == stamp_; }

    [[nodiscard]] std::span<const Node> nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::span<const Triangle> triangles() const noexcept { return triangles_; }
    [[nodiscard]] std::span<const Tetrahedron> tetrahedra() const noexcept { return tetrahedra_; }
    /// The regions present, ascending and without repeats.
    [[nodiscard]] std::span<const RegionId> regions() const noexcept { return regions_; }

    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }
    /// Triangles plus tetrahedra.
    [[nodiscard]] std::size_t elementCount() const noexcept { return triangles_.size() + tetrahedra_.size(); }
    [[nodiscard]] bool isEmpty() const noexcept { return nodes_.empty() && triangles_.empty() && tetrahedra_.empty(); }

    /// The node @p id names, or nullptr.
    ///
    /// Resolution is BY IDENTITY, with a binary search over the ascending node
    /// storage -- never `nodes_[id.value()]`. Handles may be sparse (1, 4, 10),
    /// so indexing by value would silently resolve to the wrong node, which is
    /// the defect this lookup exists to make impossible.
    [[nodiscard]] const Node* findNode(NodeId id) const noexcept;

    /// The type of the element @p id names, or nullopt if there is none.
    [[nodiscard]] std::optional<ElementType> elementType(ElementId id) const noexcept;

    /// The triangle @p id names, or nullptr -- including when @p id names a
    /// tetrahedron, because an element of the wrong type is not the element
    /// that was asked for. The siblings of findNode(), resolved the same way:
    /// by identity over ascending storage, never by indexing with the handle's
    /// value.
    [[nodiscard]] const Triangle* findTriangle(ElementId id) const noexcept;
    /// The tetrahedron @p id names, or nullptr. See findTriangle().
    [[nodiscard]] const Tetrahedron* findTetrahedron(ElementId id) const noexcept;

    /// The axis-aligned bounds of the nodes, or nullopt for a mesh with no
    /// nodes.
    ///
    /// Nullopt rather than a zero box: a box at the origin is a real answer for
    /// a mesh holding one node at the origin, so fabricating one for an empty
    /// mesh would make the two indistinguishable.
    [[nodiscard]] std::optional<MeshBounds> bounds() const noexcept;

    friend bool operator==(const Mesh&, const Mesh&) = default;

private:
    friend class MeshBuilder;

    MeshStamp stamp_{};
    std::vector<Node> nodes_{};
    std::vector<Triangle> triangles_{};
    std::vector<Tetrahedron> tetrahedra_{};
    std::vector<RegionId> regions_{};
};

/// The only way to build a Mesh.
///
/// Every addition is CHECKED and ATOMIC: a rejected node or element leaves the
/// builder exactly as it was, so a caller that ignores a failure gets a mesh
/// missing that element, never a half-added one.
///
/// HANDLES ARE STRICTLY INCREASING. A caller may let the builder allocate, or
/// supply a handle that is greater than the last one of its kind. That single
/// rule delivers several properties at once and is cheap to check: storage stays
/// contiguous and ascending so lookup is a binary search; a duplicate handle is
/// impossible rather than merely rejected; and gaps are allowed, so a generator
/// that skips numbers is representable and is forced through identity lookup
/// rather than indexing.
class BETTERCAD_MESHING_EXPORT MeshBuilder {
public:
    /// Starts a mesh with a fresh MeshId from a process-local counter.
    /// @p generation records which build from the same source this is.
    explicit MeshBuilder(std::uint32_t generation = 0);

    /// Adds a node at @p position with the next handle.
    ///
    /// Fails with InvalidArgument if any coordinate is not finite. A NaN or an
    /// infinity never enters the mesh, so nothing downstream has to defend
    /// against one arriving through a comparison that quietly answered false.
    [[nodiscard]] Result<NodeId> addNode(const Point3D& position);

    /// Adds a node with the caller's handle.
    ///
    /// Fails with InvalidArgument for an invalid handle or a non-finite
    /// coordinate, and with AlreadyExists if @p id is not greater than the last
    /// node added -- which covers a repeat of the same handle and an
    /// out-of-order one with the same diagnostic.
    [[nodiscard]] Result<NodeId> addNode(NodeId id, const Point3D& position);

    /// Adds a triangle on three nodes of this mesh, in @p region.
    ///
    /// Fails with InvalidArgument for an invalid or repeated node handle or an
    /// invalid region, and with NotFound if a handle names no node of this mesh.
    /// The three handles are NOT reordered.
    [[nodiscard]] Result<ElementId> addTriangle(const std::array<NodeId, 3>& nodes, RegionId region);

    /// Adds a tetrahedron on four nodes of this mesh, in @p region.
    ///
    /// The same failures as addTriangle. Connectivity only: whether the four
    /// points enclose a positive volume is geometry, and is reported by
    /// validation over the finished mesh rather than here, so that one
    /// tetrahedron cannot be accepted or refused on a rule the whole-mesh report
    /// would state differently.
    ///
    /// A TOPOLOGICAL DUPLICATE IS NOT REJECTED, and that is a decision rather
    /// than an omission. Two tetrahedra on the same four nodes describe the same
    /// region of space, so at most one of them can be right -- but which, and
    /// whether the pair is a backend defect or a deliberate overlap, is not
    /// answerable from connectivity alone: one ordering may be inverted, and
    /// detecting the pair needs a canonical key over node sets that is a
    /// whole-mesh question. Refusing it here would also make the cost of adding
    /// an element depend on how many elements are already present. It belongs
    /// with the other whole-mesh, threshold-free-but-global checks, and
    /// P16-VOL-001 put it there: `MeshIssueKind::DuplicateTetrahedron`, beside
    /// `NodeSharedBetweenRegions`. This note stays so that the silence here is
    /// not mistaken for an oversight.
    [[nodiscard]] Result<ElementId> addTetrahedron(const std::array<NodeId, 4>& nodes, RegionId region);

    [[nodiscard]] std::size_t nodeCount() const noexcept { return mesh_.nodeCount(); }
    [[nodiscard]] std::size_t elementCount() const noexcept { return mesh_.elementCount(); }
    [[nodiscard]] const MeshStamp& stamp() const noexcept { return mesh_.stamp(); }

    /// The finished mesh. The builder may be inspected afterwards but building
    /// continues to be possible; `build()` is a snapshot, which is what makes a
    /// mesh a value rather than a handle to a living object.
    [[nodiscard]] Mesh build() const;

private:
    [[nodiscard]] Result<void> checkNodes(std::span<const NodeId> nodes) const;
    void noteRegion(RegionId region);

    Mesh mesh_{};
    NodeId::ValueType lastNode_ = 0;
    ElementId::ValueType lastElement_ = 0;
};

} // namespace bettercad::meshing
