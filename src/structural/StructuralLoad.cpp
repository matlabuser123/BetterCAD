// Canonical structural loads and their conversion to nodal forces
// (P17-LOAD-001).
//
// EVERY FACE TARGET IS RESOLVED THROUGH P16, and that is the central
// discipline of this file. `meshing::boundaryFacetsOf` decides which facets
// belong to a `FaceName`; nothing here measures a distance to a plane, compares
// a normal, tests a centroid or picks a nearest face. Facet geometry is read
// only after the mapping has answered, and then only to integrate.
//
// NOTHING MESH-LOCAL IS STORED IN A LOAD. The canonical face loads carry a
// `FaceName`; the facets and the nodal forces are rebuilt on every call.
//
// NO UNORDERED CONTAINER. Accumulation uses std::map keyed on NodeId, so the
// emitted field is ascending and the floating-point sums are formed in the same
// order in every build configuration.

#include <bettercad/structural/StructuralLoadVector.hpp>

#include <bettercad/structural/StructuralTarget.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <utility>

namespace bettercad::structural {
namespace {

/// The three corner positions of a boundary facet, in P16's stored winding.
///
/// The winding is the one the tetrahedra imply, so the area vector derived
/// from it points out of the material -- see `facetAreaVector`.
struct Facet {
    std::array<meshing::NodeId, 3> nodes{};
    std::array<Point3D, 3> positions{};
};

[[nodiscard]] Result<Facet> facetOf(const meshing::Mesh& mesh, meshing::ElementId id) {
    const meshing::Triangle* triangle = mesh.findTriangle(id);
    if (triangle == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("{} is not a boundary triangle of this mesh", id));
    }
    Facet facet;
    facet.nodes = triangle->nodes;
    for (std::size_t corner = 0; corner < 3; ++corner) {
        const meshing::Node* node = mesh.findNode(triangle->nodes[corner]);
        if (node == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} names {}, which is not a node of this mesh", id,
                                         triangle->nodes[corner]));
        }
        facet.positions[corner] = node->position;
    }
    return facet;
}

[[nodiscard]] bool isFiniteValue(const StructuralLoad& load) noexcept {
    if (const NodalForceLoad* nodal = load.nodalForce(); nodal != nullptr) {
        return isFinite(nodal->force);
    }
    if (const SurfaceTractionLoad* traction = load.traction(); traction != nullptr) {
        return isFinite(traction->traction);
    }
    if (const PressureLoad* pressure = load.pressure(); pressure != nullptr) {
        return isFinite(pressure->magnitude);
    }
    if (const GravityLoad* gravity = load.gravity(); gravity != nullptr) {
        return isFinite(gravity->acceleration);
    }
    return false;
}

/// One load's resolved facets, or the reason it has none.
struct ResolvedTarget {
    std::vector<meshing::ElementId> facets{};
    std::optional<LoadProblem> problem{};
    Error error{};
};

/// THE THREE CHECKS ARE `structural::resolveFaceTarget`'s, NOT THIS FILE'S.
/// They moved to `StructuralTarget.hpp` when P17-BC-001 needed the same
/// resolution for a restraint: copying them would have put P16's mapping
/// semantics in two places. What stays here is the mapping onto this module's
/// own diagnostics, because a load "has nothing to act on" and a restraint has
/// nothing to constrain, and each names its own identity.
[[nodiscard]] ResolvedTarget resolveTarget(const meshing::GeometryMeshMap& map, LoadId load,
                                           const FaceName& face) {
    ResolvedTarget out;
    const std::optional<TargetProblem> problem = faceTargetProblem(map, face);
    if (!problem.has_value()) {
        Result<std::vector<meshing::ElementId>> facets = resolveFaceTarget(map, face);
        out.facets = std::move(*facets);
        return out;
    }
    switch (*problem) {
    case TargetProblem::SelectorInvalid:
        out.problem = LoadProblem::TargetInvalid;
        out.error = resolveFaceTarget(map, face).error();
        return out;
    case TargetProblem::Unresolved:
        out.problem = LoadProblem::TargetUnresolved;
        out.error = makeError(ErrorCode::NotFound,
                              std::format("{}: its target face names no face of the body as "
                                          "it is now, so the load has nothing to act on. Nothing "
                                          "nearby is substituted",
                                          load))
                        .error();
        return out;
    case TargetProblem::WithoutFacets:
        // Resolved, and the mapping gave it no facet. A load that integrates
        // over nothing is not a zero load.
        out.problem = LoadProblem::TargetWithoutFacets;
        out.error =
            makeError(ErrorCode::FailedPrecondition,
                      std::format("{}: its target face resolved but the mesh attributed no "
                                  "boundary facet to it, so there is nothing to integrate over",
                                  load))
                .error();
        return out;
    }
    return out;
}

/// Accumulates nodal forces in ascending NodeId order.
class Accumulator {
public:
    void add(meshing::NodeId node, const Force3D& force) {
        // ADDS, NEVER OVERWRITES. A node shared by several loaded facets
        // receives a contribution from each, and a node targeted by two loads
        // receives both -- superposition is the semantics of linear statics.
        Force3D& total = totals_[node];
        total = total + force;
    }

    [[nodiscard]] std::vector<NodalLoad> take() const {
        std::vector<NodalLoad> out;
        out.reserve(totals_.size());
        for (const auto& [node, force] : totals_) {
            out.push_back(NodalLoad{.node = node, .force = force});
        }
        return out;
    }

private:
    /// std::map, so iteration is ascending by handle and identical in every
    /// preset. An unordered_map here would make the emitted order -- and so
    /// every floating-point sum over it -- depend on a hash.
    std::map<meshing::NodeId, Force3D> totals_{};
};

} // namespace

std::string_view toString(LoadKind kind) noexcept {
    switch (kind) {
    case LoadKind::NodalForce:
        return "nodal_force";
    case LoadKind::SurfaceTraction:
        return "surface_traction";
    case LoadKind::Pressure:
        return "pressure";
    case LoadKind::Gravity:
        return "gravity";
    }
    return "unknown";
}

std::string_view toString(LoadProblem problem) noexcept {
    switch (problem) {
    case LoadProblem::DuplicateLoadId:
        return "duplicate_load_id";
    case LoadProblem::NonFiniteValue:
        return "non_finite_value";
    case LoadProblem::TargetUnresolved:
        return "target_unresolved";
    case LoadProblem::TargetInvalid:
        return "target_invalid";
    case LoadProblem::TargetWithoutFacets:
        return "target_without_facets";
    case LoadProblem::NodeNotInMesh:
        return "node_not_in_mesh";
    case LoadProblem::DegenerateFacet:
        return "degenerate_facet";
    case LoadProblem::DensityMissing:
        return "density_missing";
    }
    return "unknown";
}

LoadKind StructuralLoad::kind() const noexcept {
    if (nodalForce() != nullptr) {
        return LoadKind::NodalForce;
    }
    if (traction() != nullptr) {
        return LoadKind::SurfaceTraction;
    }
    if (pressure() != nullptr) {
        return LoadKind::Pressure;
    }
    return LoadKind::Gravity;
}

std::optional<FaceName> StructuralLoad::target() const noexcept {
    if (const SurfaceTractionLoad* load = traction(); load != nullptr) {
        return load->face;
    }
    if (const PressureLoad* load = pressure(); load != nullptr) {
        return load->face;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Facet integration
// ---------------------------------------------------------------------------

Vector3D facetAreaVector(const Point3D& p1, const Point3D& p2, const Point3D& p3) noexcept {
    const double ux = p2.x.si() - p1.x.si();
    const double uy = p2.y.si() - p1.y.si();
    const double uz = p2.z.si() - p1.z.si();
    const double vx = p3.x.si() - p1.x.si();
    const double vy = p3.y.si() - p1.y.si();
    const double vz = p3.z.si() - p1.z.si();
    // (1/2) u x v. Half, because the cross product of two edges spans the
    // PARALLELOGRAM: the single commonest way to apply a factor of two to a
    // pressure without noticing.
    return Vector3D{0.5 * (uy * vz - uz * vy), 0.5 * (uz * vx - ux * vz),
                    0.5 * (ux * vy - uy * vx)};
}

Area facetArea(const Point3D& p1, const Point3D& p2, const Point3D& p3) noexcept {
    const Vector3D a = facetAreaVector(p1, p2, p3);
    return Area::fromSi(std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z));
}

Force3D facetNodalForce(Area area, const Traction3D& traction) noexcept {
    // A t / 3 per corner. The consistent load vector of a three-node linear
    // triangle under a constant load: each shape function integrates to A/3
    // over its own triangle, so the three thirds sum to A t exactly.
    const double third = area.si() / 3.0;
    return Force3D{Force::fromSi(traction.x.si() * third),
                   Force::fromSi(traction.y.si() * third),
                   Force::fromSi(traction.z.si() * third)};
}

// ---------------------------------------------------------------------------
// PreparedLoads
// ---------------------------------------------------------------------------

bool PreparedLoads::describes(const meshing::Mesh& mesh) const noexcept {
    // The node count as well as the stamp, for the reason MeshDofMap records:
    // MeshBuilder sets its stamp in its CONSTRUCTOR and build() is a snapshot,
    // so a stamp identifies the builder and not the snapshot.
    return mesh.owns(mesh_) && mesh.nodes().size() == nodeCount_;
}

Force3D PreparedLoads::resultantForce() const noexcept {
    std::vector<Force3D> forces;
    forces.reserve(nodal_.size());
    for (const NodalLoad& load : nodal_) {
        forces.push_back(load.force);
    }
    // sum() in the field's own ascending order, which is what makes the answer
    // reproducible: core's sum() documents that the order is the caller's
    // because floating-point addition is not associative.
    return sum(std::span<const Force3D>{forces});
}

Result<Moment3D> PreparedLoads::resultantMomentAbout(const meshing::Mesh& mesh,
                                                     const Point3D& origin) const {
    if (!describes(mesh)) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the mesh is not the one this load field was prepared against, so its "
                         "node positions do not belong to these forces");
    }
    Moment3D total{};
    for (const NodalLoad& load : nodal_) {
        const meshing::Node* node = mesh.findNode(load.node);
        if (node == nullptr) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} carries a load and is not a node of this mesh",
                                         load.node));
        }
        const Translation3D lever{node->position.x - origin.x, node->position.y - origin.y,
                                  node->position.z - origin.z};
        total = total + momentOf(lever, load.force);
    }
    return total;
}

// ---------------------------------------------------------------------------
// Preparation
// ---------------------------------------------------------------------------

namespace {

/// One pass, shared by `prepareStructuralLoads` and `structuralLoadProblem` so
/// the two cannot drift apart: the same checks in the same order.
struct Prepared {
    std::optional<LoadProblem> problem{};
    Error error{};
    std::vector<NodalLoad> nodal{};
    std::vector<LoadContribution> contributions{};
};

[[nodiscard]] Prepared fail(LoadProblem problem, Error error) {
    return Prepared{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] Prepared run(const StructuralModel& model, const StructuralMaterial& material,
                           std::span<const StructuralLoad> loads) {
    const meshing::Mesh& mesh = model.mesh().mesh();
    const meshing::GeometryMeshMap& map = model.map();

    // DUPLICATE IDENTITY FIRST. Two records for one LoadId means one of them
    // would be silently ignored, and nothing below could tell which.
    std::vector<LoadId> seen;
    seen.reserve(loads.size());
    for (const StructuralLoad& load : loads) {
        if (std::ranges::find(seen, load.id()) != seen.end()) {
            return fail(LoadProblem::DuplicateLoadId,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("{} appears more than once, so one record "
                                              "would be ignored",
                                              load.id()))
                            .error());
        }
        seen.push_back(load.id());
    }

    // THEN VALUES, before any integration, so nothing non-finite can reach a
    // force.
    for (const StructuralLoad& load : loads) {
        if (!isFiniteValue(load)) {
            return fail(LoadProblem::NonFiniteValue,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("{} carries a value that is not finite", load.id()))
                            .error());
        }
    }

    Accumulator accumulator;
    Prepared out;
    out.contributions.reserve(loads.size());

    for (const StructuralLoad& load : loads) {
        LoadContribution contribution{.load = load.id(), .kind = load.kind()};

        if (const NodalForceLoad* nodal = load.nodalForce(); nodal != nullptr) {
            // MESH-LOCAL, AND CHECKED AS SUCH. The stamp first, so a handle
            // from another mesh is refused even when this mesh happens to have
            // a node of the same number.
            if (!mesh.owns(nodal->mesh)) {
                return fail(LoadProblem::NodeNotInMesh,
                            makeError(ErrorCode::FailedPrecondition,
                                      std::format("{}: its {} belongs to a different mesh "
                                                  "generation, so it names no node of this one",
                                                  load.id(), nodal->node))
                                .error());
            }
            if (mesh.findNode(nodal->node) == nullptr) {
                return fail(LoadProblem::NodeNotInMesh,
                            makeError(ErrorCode::NotFound,
                                      std::format("{}: {} is not a node of this mesh, and "
                                                  "nothing nearby is substituted",
                                                  load.id(), nodal->node))
                                .error());
            }
            accumulator.add(nodal->node, nodal->force);
            contribution.resultant = nodal->force;
            out.contributions.push_back(contribution);
            continue;
        }

        if (const GravityLoad* gravity = load.gravity(); gravity != nullptr) {
            // THE DENSITY IS P15'S. StructuralMaterial carries one exactly
            // when the analysis mode is LinearStaticWithGravity, because
            // P17-MAT-001 reads P15's requirement table to decide. There is no
            // default here and no density field on the load.
            if (!material.density().has_value()) {
                return fail(LoadProblem::DensityMissing,
                            makeError(ErrorCode::FailedPrecondition,
                                      std::format("{} is a gravity load and the resolved "
                                                  "material carries no density, so there is no "
                                                  "mass to accelerate. Set the analysis mode to "
                                                  "self-weight so the material is resolved for it",
                                                  load.id()))
                                .error());
            }
            const double density = material.density()->si();
            Force3D resultant{};
            for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
                std::array<Point3D, 4> corners{};
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    const meshing::Node* node = mesh.findNode(tet.nodes[corner]);
                    if (node == nullptr) {
                        return fail(LoadProblem::NodeNotInMesh,
                                    makeError(ErrorCode::NotFound,
                                              std::format("element {} names a node this mesh does "
                                                          "not have",
                                                          tet.id))
                                        .error());
                    }
                    corners[corner] = node->position;
                }
                // P16's signed volume, with no absolute value: a
                // StructuralModel proves every element is positively
                // oriented, so taking a magnitude here could only hide a
                // violation of that.
                const double volume =
                    meshing::signedVolume(corners[0], corners[1], corners[2], corners[3]).si();
                // b = rho g, and the consistent load vector of a Tet4 under a
                // constant body force gives each of the four nodes a quarter:
                // each shape function integrates to V/4 over its own element.
                const double share = density * volume / 4.0;
                const Force3D quarter{Force::fromSi(share * gravity->acceleration.x),
                                      Force::fromSi(share * gravity->acceleration.y),
                                      Force::fromSi(share * gravity->acceleration.z)};
                for (const meshing::NodeId node : tet.nodes) {
                    accumulator.add(node, quarter);
                }
                resultant = resultant + quarter + quarter + quarter + quarter;
            }
            contribution.elements = mesh.tetrahedra().size();
            contribution.resultant = resultant;
            out.contributions.push_back(contribution);
            continue;
        }

        // A FACE LOAD. Its target is canonical; its facets are not.
        const std::optional<FaceName> face = load.target();
        if (!face.has_value()) {
            return fail(LoadProblem::TargetUnresolved,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("{} targets no face", load.id()))
                            .error());
        }
        ResolvedTarget target = resolveTarget(map, load.id(), *face);
        if (target.problem.has_value()) {
            return fail(*target.problem, std::move(target.error));
        }

        Area total{};
        Force3D resultant{};
        for (const meshing::ElementId facetId : target.facets) {
            Result<Facet> facet = facetOf(mesh, facetId);
            if (!facet.has_value()) {
                return fail(LoadProblem::DegenerateFacet, facet.error());
            }
            const Vector3D areaVector =
                facetAreaVector(facet->positions[0], facet->positions[1], facet->positions[2]);
            const double magnitude = std::sqrt(areaVector.x * areaVector.x +
                                               areaVector.y * areaVector.y +
                                               areaVector.z * areaVector.z);
            if (!std::isfinite(magnitude) || magnitude == 0.0) {
                return fail(LoadProblem::DegenerateFacet,
                            makeError(ErrorCode::FailedPrecondition,
                                      std::format("{}: boundary facet {} has area {}, so it "
                                                  "cannot carry a distributed load",
                                                  load.id(), facetId, magnitude))
                                .error());
            }

            Traction3D traction{};
            if (const SurfaceTractionLoad* applied = load.traction(); applied != nullptr) {
                // A GLOBAL VECTOR. The facet's orientation is irrelevant to a
                // traction -- only its positive area is -- which is exactly
                // what distinguishes it from a pressure.
                traction = applied->traction;
            } else {
                // PRESSURE: t = -p n_out. The area vector already carries both
                // the outward direction and the area, so dividing by the
                // magnitude recovers the unit normal and the area is applied
                // once, by facetNodalForce. Applying the area vector AND the
                // area would square it.
                const double p = load.pressure()->magnitude.si();
                traction = Traction3D{Pressure::fromSi(-p * areaVector.x / magnitude),
                                      Pressure::fromSi(-p * areaVector.y / magnitude),
                                      Pressure::fromSi(-p * areaVector.z / magnitude)};
            }

            const Area area = Area::fromSi(magnitude);
            const Force3D perNode = facetNodalForce(area, traction);
            for (const meshing::NodeId node : facet->nodes) {
                accumulator.add(node, perNode);
            }
            total = total + area;
            resultant = resultant + perNode + perNode + perNode;
        }
        contribution.elements = target.facets.size();
        contribution.area = total;
        contribution.resultant = resultant;
        out.contributions.push_back(contribution);
    }

    out.nodal = accumulator.take();
    return out;
}

} // namespace

Result<PreparedLoads> prepareStructuralLoads(const StructuralModel& model,
                                             const StructuralMaterial& material,
                                             std::span<const StructuralLoad> loads) {
    Prepared prepared = run(model, material, loads);
    if (prepared.problem.has_value()) {
        return std::unexpected(std::move(prepared.error));
    }
    PreparedLoads out;
    out.mesh_ = model.mesh().mesh().stamp();
    out.nodeCount_ = model.mesh().mesh().nodes().size();
    out.nodal_ = std::move(prepared.nodal);
    out.contributions_ = std::move(prepared.contributions);
    return out;
}

std::optional<LoadProblem> structuralLoadProblem(const StructuralModel& model,
                                                 const StructuralMaterial& material,
                                                 std::span<const StructuralLoad> loads) {
    return run(model, material, loads).problem;
}

} // namespace bettercad::structural
