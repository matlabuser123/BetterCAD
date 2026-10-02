#pragma once

#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Data-level validation of an engineering mesh (P16-DATA-001).
//
// THE BOUNDARY THIS FILE KEEPS. A mesh can be wrong in two quite different
// ways, and collapsing them is how a mesher's defects get reported as a user's
// modelling problem:
//
//   DATA DEFECT (here)          a reference to a node that does not exist, a
//                               repeated handle, a zero volume, an inverted
//                               element, a non-finite coordinate. The mesh does
//                               not describe a body at all. Always a failure.
//
//   QUALITY FINDING (P16-QUALITY-001)
//                               a sliver, a bad aspect ratio, a small dihedral
//                               angle. The mesh is a valid discretisation that
//                               will solve badly. A matter of degree, and of a
//                               threshold someone chose.
//
// So there is no tolerance in this file. An element is refused for being
// degenerate when its volume or area is exactly zero, or not finite -- not for
// being thin. A thin tetrahedron is data-valid and is P16-QUALITY-001's to
// complain about.
namespace bettercad::meshing {

/// What a mesh validation report is complaining about.
enum class MeshIssueKind : std::uint8_t {
    /// A node coordinate is NaN or infinite.
    NonFiniteCoordinate,
    /// An element names a handle that no node of this mesh carries.
    MissingNodeReference,
    /// An element names the same node more than once.
    RepeatedNodeReference,
    /// A triangle's three nodes are collinear or coincident: zero area.
    DegenerateTriangle,
    /// A tetrahedron's four nodes are coplanar: zero signed volume.
    DegenerateTetrahedron,
    /// A tetrahedron has negative signed volume: its nodes are ordered the wrong
    /// way round, and ADR-032 requires positive Jacobians.
    InvertedTetrahedron,
    /// Two tetrahedra occupy the same four nodes, in any order.
    ///
    /// A DATA defect, not a quality finding, and it needs no tolerance: two
    /// tetrahedra on one node set describe the same region of space, so at
    /// most one of them can be right. P16-DATA-001 left this to
    /// P16-QUALITY-001 on the grounds that it is a whole-mesh question rather
    /// than a per-element one -- which is true, and is why it lives HERE,
    /// where the other whole-mesh check (NodeSharedBetweenRegions) already
    /// does, rather than with the threshold-bearing quality metrics. TODO.md
    /// places "no duplicate tetrahedra" in P16-VOL-001.
    ///
    /// The issue names the SECOND tetrahedron found, the one that conflicts
    /// with an element already accepted, matching NodeSharedBetweenRegions'
    /// convention.
    DuplicateTetrahedron,
    /// A node is referenced by elements of two different regions. ADR-032: "A
    /// mesh has one region per solid, shares no node between regions."
    NodeSharedBetweenRegions,
    /// An element carries no valid region.
    MissingRegion,
    /// The mesh has no elements. A container may legitimately be empty; a mesh
    /// offered to a consumer may not.
    EmptyMesh,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(MeshIssueKind kind) noexcept;

/// One structured complaint.
///
/// Structured rather than a sentence, so a GUI or a CLI can present or translate
/// it without parsing text; `message` is a fallback for logs, not the payload.
/// This follows the shape P15-PROV-001 established for MaterialIssue.
struct MeshIssue {
    MeshIssueKind kind = MeshIssueKind::EmptyMesh;
    /// Which node, when the issue is about one.
    std::optional<NodeId> node;
    /// Which element, when the issue is about one.
    std::optional<ElementId> element;
    /// Which region, for a region issue. For NodeSharedBetweenRegions this is
    /// the SECOND region found, the one that conflicts with the first.
    std::optional<RegionId> region;
    std::string message;

    friend bool operator==(const MeshIssue&, const MeshIssue&) = default;
};

/// What is wrong with a mesh at the data level, and nothing about its quality.
///
/// ORDERING IS DEFINED AND DETERMINISTIC: issues appear in MeshIssueKind
/// enumeration order, and within a kind by node handle then element handle
/// ascending. Nothing is built from an unordered container, so the same mesh
/// gives the same report -- in the same order -- in Debug, Release and
/// Debug-shared. Structured output later depends on that.
///
/// EVERY issue is collected rather than the first being thrown. A mesher that
/// produced one inverted element usually produced several, and a report that
/// stops at the first turns one debugging session into twenty.
struct MeshValidationReport {
    std::vector<MeshIssue> issues;

    /// No issues at all. Data-valid.
    ///
    /// Deliberately NOT called "solver ready": this says the mesh describes a
    /// body coherently, not that it is a good discretisation of the right body.
    /// Conformance is P16-MAP-001's, quality is P16-QUALITY-001's, and the token
    /// a solver takes is ADR-030's ValidatedMesh, which the mesher's validating
    /// path constructs.
    [[nodiscard]] bool dataValid() const noexcept { return issues.empty(); }

    friend bool operator==(const MeshValidationReport&, const MeshValidationReport&) = default;
};

/// Checks @p mesh against every data-level invariant this module defines.
///
/// Checked: coordinates finite; element node handles resolve; node handles
/// within an element distinct; each element carries a valid region; no node
/// shared between regions; triangle area non-zero; tetrahedron signed volume
/// positive, so zero is degenerate and negative is inverted; the mesh has at
/// least one element.
///
/// Handle uniqueness and ascending order are NOT checked here because
/// MeshBuilder makes them unrepresentable -- a handle must be greater than the
/// last, so a duplicate cannot be stored to be found later.
[[nodiscard]] BETTERCAD_MESHING_EXPORT MeshValidationReport validate(const Mesh& mesh);

} // namespace bettercad::meshing
