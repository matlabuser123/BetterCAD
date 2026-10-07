#pragma once

// P17-LOAD-001 -- canonical structural load intent, and its conversion into
// current-mesh nodal forces.
//
// THE AUTHORITY CHAIN, AND THE ONE RULE THAT MATTERS MOST.
//
//     canonical load intent          LoadId + FaceName + a physical value
//              |
//     P16's GeometryMeshMap          boundaryFacetsOf(map, FaceName)
//              |
//     current boundary facets        ElementIds of THIS mesh, derived
//              |
//     equivalent nodal forces        NodeId -> Force3D, derived
//
// **A FACET HANDLE IS NEVER LOAD AUTHORITY.** A `FaceName` is canonical and
// survives a remesh; a `BoundaryFacet` handle is a mesh-local index that a
// remesh invalidates. So a `SurfaceTractionLoad` and a `PressureLoad` store a
// `FaceName` and nothing else about the mesh, and the facets are resolved
// again every time the loads are prepared. Searched and asserted: there is no
// `ElementId`, no facet index and no `NodeId` anywhere in the canonical face
// load types.
//
// AND P17 DOES NOT CLASSIFY GEOMETRY. P16-MAP-001 owns the question of which
// triangles lie on which CAD face, and this module asks it rather than
// answering it again: no distance-to-plane tolerance, no nearest face, no
// normal-similarity threshold, no centroid test. Facet geometry is read only
// AFTER P16 has resolved the mapping, and then only to integrate over it.
//
// WHAT IS MESH-LOCAL, AND SAYS SO. `NodalForceLoad` names a `NodeId`, which is
// a handle into one generation of one mesh. It is therefore explicitly
// mesh-local -- it carries the `MeshStamp` it belongs to and is refused
// against any other mesh -- and it is NOT durable CAD intent. It exists for
// element and solver fixtures, where a test wants a force at a known node, and
// `LOAD_SCHEMA.md` records that choice and its cost.
//
// THIS HEADER IS CANONICAL INTENT ONLY, and the split is forced rather than
// stylistic. The loads live in `StructuralAnalysisDefinition`, so the document
// object's header includes this one -- and if the derived machinery were here
// too, every document object would pull in the `Mesher`, the
// `GeometryMeshMap` and the whole input boundary. The conversion to nodal
// forces is in `StructuralLoadVector.hpp`, which depends on this file and not
// the other way round. It is also the separation the physics has: intent
// survives a remesh, and a nodal force field does not.
//
// WHAT NEITHER FILE DOES. There is no global right-hand side -- the output is
// a nodal force field, and mapping it into equation space is
// P17-ASSEMBLY-001's through P17-DOF's `MeshDofMap`. There are no restraints
// (P17-BC-001), no solve (P17-SOLVE-001), no stress recovery (P17-POST-001),
// no persistence (P17-PERSIST-001) and no CLI or GUI. Element stiffness is not
// needed either: a surface load is an integral over the boundary, and gravity
// needs a tetrahedron's volume but not its `Ke`.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/structural/Export.hpp>
#include <bettercad/structural/StructuralAnalysis.hpp>
#include <bettercad/structural/StructuralMaterial.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>
#include <vector>

namespace bettercad::structural {

// ---------------------------------------------------------------------------
// Canonical load payloads
// ---------------------------------------------------------------------------

/// A force applied directly at one mesh node.
///
/// MESH-LOCAL BY DECLARATION, and that is a decision rather than an
/// oversight. A `NodeId` is a handle into one generation of one mesh
/// (ADR-031), so this load cannot track a CAD edit and must not pretend to:
/// it carries the `MeshStamp` it was created against and is refused against
/// any other mesh, including one with the same node count and the same numeric
/// handles. What it is for is fixtures -- an element test, a solver check, a
/// headless numerical case that wants a force at a known node.
///
/// A durable point load on the model would be a load on a CAD VERTEX, which
/// needs a canonical vertex reference P16 does not provide. That is recorded
/// as a limitation, not worked around.
struct NodalForceLoad {
    /// The mesh this node belongs to. Without it the handle means nothing.
    meshing::MeshStamp mesh{};
    meshing::NodeId node{};
    Force3D force{};

    friend bool operator==(const NodalForceLoad&, const NodalForceLoad&) = default;
};

/// A distributed force per unit area on a CAD face, as a vector.
///
/// THE DIRECTION IS GLOBAL AND DOES NOT FOLLOW THE SURFACE. `traction` is
/// resolved on the model's own X, Y and Z, so rotating the body does not
/// rotate the load -- which is the whole difference from a pressure. A
/// traction of `[0, 0, -1] Pa` keeps pulling along -Z whatever the face does.
///
/// The target is a `FaceName` and nothing else: no facet handles, no node
/// handles, no snapshot of a resolved set.
struct SurfaceTractionLoad {
    FaceName face{};
    Traction3D traction{};

    friend bool operator==(const SurfaceTractionLoad&, const SurfaceTractionLoad&) = default;
};

/// A scalar pressure on a CAD face, acting along the current surface normal.
///
/// POSITIVE PRESSURE ACTS INWARD, into the material. With P16's outward
/// boundary orientation that makes the equivalent traction
///
/// ```text
///     t = -p n_out
/// ```
///
/// so a positive `magnitude` compresses the body. The convention is frozen
/// here and has its own analytical test and its own mutation probe.
///
/// SIGNED, DELIBERATELY. A negative pressure is outward suction and is
/// mathematically ordinary in linear statics, so it is accepted rather than
/// clamped or refused -- a clamp would silently change the user's model.
///
/// STORED AS A SCALAR, NEVER AS A FROZEN FORCE OR TRACTION VECTOR. The
/// direction comes from the normal of whatever mesh is current, so a load
/// created before a remesh, a geometry edit or a rigid transform still means
/// the right thing afterwards. Freezing a vector at creation time is the
/// defect this representation exists to prevent.
struct PressureLoad {
    FaceName face{};
    Pressure magnitude{};

    friend bool operator==(const PressureLoad&, const PressureLoad&) = default;
};

/// Self-weight: a uniform acceleration acting on the body's own mass.
///
/// THE LOAD CARRIES THE ACCELERATION, NOT THE DENSITY AND NOT THE FORCE. The
/// density is P15's canonical material data, resolved through
/// `StructuralMaterial` for `ConsumerKind::FeaLinearStaticWithGravity`, and
/// ADR-028 forbids a solver holding material data of its own -- so there is no
/// density field here and no default. The body force is `b = rho g`, in N/m^3,
/// formed at preparation time.
///
/// THE DIRECTION IS EXPLICIT. There is no implicit `-Z`: a model may be built
/// in any orientation, and an environmental constant baked into the solver is
/// not a fact about the user's part. A caller that wants earth gravity passes
/// `[0, 0, -9.80665] m/s^2`; `kStandardGravity` is offered for that and is a
/// convenience, not a default.
struct GravityLoad {
    Vector3D acceleration{};

    friend bool operator==(const GravityLoad&, const GravityLoad&) = default;
};

/// Standard gravity, for a caller that wants it. `9.80665 m/s^2` is the CGPM
/// definition. Offered, never assumed.
inline constexpr double kStandardGravity = 9.80665;

/// Which kind of load a `StructuralLoad` carries.
enum class LoadKind : std::uint8_t {
    NodalForce,
    SurfaceTraction,
    Pressure,
    Gravity,
};

[[nodiscard]] BETTERCAD_STRUCTURAL_EXPORT std::string_view toString(LoadKind kind) noexcept;

/// One canonical load: an identity and exactly one strongly-typed payload.
///
/// A VARIANT RATHER THAN A TAG AND A BAG OF DOUBLES. The four payloads have
/// different units -- N, Pa as a vector, Pa as a scalar, m/s^2 -- and
/// different targets, so a `struct { int type; double a, b, c, d; }` would
/// make a pressure assignable from a traction and lose every unit. Each
/// payload is its own type and the compiler keeps them apart.
class BETTERCAD_STRUCTURAL_EXPORT StructuralLoad {
public:
    StructuralLoad(LoadId id, NodalForceLoad load) : id_(id), payload_(load) {}
    StructuralLoad(LoadId id, SurfaceTractionLoad load) : id_(id), payload_(std::move(load)) {}
    StructuralLoad(LoadId id, PressureLoad load) : id_(id), payload_(std::move(load)) {}
    StructuralLoad(LoadId id, GravityLoad load) : id_(id), payload_(load) {}

    /// The persisted document identity, from P17-DATA-001. There is no second
    /// load identifier in the tree.
    [[nodiscard]] LoadId id() const noexcept { return id_; }
    [[nodiscard]] LoadKind kind() const noexcept;

    /// The payload, or nullptr if this load is of another kind.
    [[nodiscard]] const NodalForceLoad* nodalForce() const noexcept {
        return std::get_if<NodalForceLoad>(&payload_);
    }
    [[nodiscard]] const SurfaceTractionLoad* traction() const noexcept {
        return std::get_if<SurfaceTractionLoad>(&payload_);
    }
    [[nodiscard]] const PressureLoad* pressure() const noexcept {
        return std::get_if<PressureLoad>(&payload_);
    }
    [[nodiscard]] const GravityLoad* gravity() const noexcept {
        return std::get_if<GravityLoad>(&payload_);
    }

    /// The CAD face this load targets, or nullopt for a load that targets no
    /// face. The canonical target, and the only one.
    [[nodiscard]] std::optional<FaceName> target() const noexcept;

    friend bool operator==(const StructuralLoad&, const StructuralLoad&) = default;

private:
    LoadId id_{};
    std::variant<NodalForceLoad, SurfaceTractionLoad, PressureLoad, GravityLoad> payload_;
};

} // namespace bettercad::structural
