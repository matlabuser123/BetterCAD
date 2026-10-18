// Canonical heat loads and their conversion to a nodal heat input
// (P18-LOAD-001).
//
// EVERY FACE TARGET IS RESOLVED THROUGH P16, and that is the central
// discipline of this file. `meshing::boundaryFacetsOf` decides which facets
// belong to a `FaceName` and `meshing::regionOf` decides which elements are the
// body; nothing here measures a distance to a plane, compares a normal, tests a
// centroid or picks a nearest face. Facet and element geometry is read only
// AFTER P16 has answered, and then only to integrate.
//
// THE INTEGRALS ARE EXACT AND THE SIGN IS THE SCALAR'S. Qi = q'' A / 3 per
// facet corner and Qi = q''' V / 4 per element corner, summing to q'' A and
// q''' V. No normal is computed, so no winding can change a contribution, and
// no absolute value is taken of a load or of a volume.
//
// NOTHING MESH-LOCAL IS STORED IN A LOAD. The canonical loads carry a FaceName
// and a value; the facets, elements, nodes, unknowns and the vector are rebuilt
// on every call.
//
// NO UNORDERED CONTAINER, NO PARALLELISM, NO STATIC STATE. Accumulation is one
// serial pass into a dense vector indexed by ThermalDofIndex, in load order,
// facet-ascending order and stored corner order, so the same inputs give the
// same bits in every build configuration.

#include <bettercad/thermal/ThermalLoadVector.hpp>

#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/thermal/ThermalElement.hpp>
#include <bettercad/thermal/ThermalTarget.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::thermal {
namespace {

[[nodiscard]] LoadFailure failure(LoadProblem problem, ErrorCode code, std::string message) {
    return LoadFailure{.problem = problem, .error = makeError(code, std::move(message)).error()};
}

/// Strictly ascending: no repeat and no reordering. P16 delivers its sets this
/// way, and a list that is not would count an entity twice.
template <typename Id>
[[nodiscard]] bool strictlyAscending(std::span<const Id> ids) noexcept {
    return std::ranges::adjacent_find(ids, [](const Id& a, const Id& b) { return !(a < b); }) ==
           ids.end();
}

[[nodiscard]] std::optional<LoadFailure> checkOutput(const meshing::Mesh& mesh, const ThermalDofMap& map,
                                                     std::span<const Power> heatInput) {
    if (!map.describes(mesh)) {
        return failure(LoadProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                       "the thermal numbering was built for a different mesh, so its unknowns are "
                       "not this mesh's nodes");
    }
    if (static_cast<ThermalDofIndex::ValueType>(heatInput.size()) != map.dofCount()) {
        return failure(LoadProblem::DofMapMismatch, ErrorCode::InvalidArgument,
                       std::format("the heat-input vector has {} entries for {} unknowns; it is "
                                   "indexed by unknown and must be exactly that long",
                                   heatInput.size(), map.dofCount()));
    }
    return std::nullopt;
}

[[nodiscard]] Result<ThermalDofIndex> dofOfNode(const ThermalDofMap& map, meshing::NodeId node) {
    return map.dofOf(node);
}

/// ADDS, never assigns: a node shared by several facets, or reached by several
/// loads, receives a contribution from each. Returns false, leaving the entry
/// infinite, if the sum overflows -- the caller refuses.
[[nodiscard]] bool addTo(std::span<Power> heatInput, ThermalDofIndex dof, double share) noexcept {
    Power& entry = heatInput[static_cast<std::size_t>(dof.value())];
    entry = Power::fromSi(entry.si() + share);
    return std::isfinite(entry.si());
}

} // namespace

std::string_view toString(LoadProblem problem) noexcept {
    switch (problem) {
    case LoadProblem::DuplicateLoadId:
        return "duplicate_load_id";
    case LoadProblem::NonFiniteValue:
        return "non_finite_value";
    case LoadProblem::TargetInvalid:
        return "target_invalid";
    case LoadProblem::TargetUnresolved:
        return "target_unresolved";
    case LoadProblem::TargetWithoutFacets:
        return "target_without_facets";
    case LoadProblem::BodyWithoutElements:
        return "body_without_elements";
    case LoadProblem::DegenerateFacet:
        return "degenerate_facet";
    case LoadProblem::InvalidElement:
        return "invalid_element";
    case LoadProblem::EntityNotInMesh:
        return "entity_not_in_mesh";
    case LoadProblem::EntitiesNotUnique:
        return "entities_not_unique";
    case LoadProblem::DofMapMismatch:
        return "dof_map_mismatch";
    case LoadProblem::NonFiniteResult:
        return "non_finite_result";
    }
    return "unknown";
}

std::string_view toString(LoadStaleReason reason) noexcept {
    switch (reason) {
    case LoadStaleReason::Body:
        return "body";
    case LoadStaleReason::Mesh:
        return "mesh";
    case LoadStaleReason::Geometry:
        return "geometry";
    case LoadStaleReason::Loads:
        return "loads";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// The integrals
// ---------------------------------------------------------------------------

Power facetNodalHeat(Area area, HeatFlux flux) noexcept {
    // q'' A / 3 per corner. The consistent load vector of a three-node linear
    // triangle under a constant load: each shape function integrates to A/3
    // over its own triangle, so the three thirds sum to q'' A exactly.
    //
    // FORMED AS q'' * (A / 3), NOT (q'' * A) / 3. The two agree to an ulp, but
    // the second overflows whenever q'' A does even when the SHARE q'' A / 3 is
    // perfectly representable; the first overflows only when the share does. (The
    // first mutation batch found the difference: a test meant to reach the
    // running-sum gate was stopped one step earlier by that product.)
    return Power::fromSi(flux.si() * (area.si() / 3.0));
}

Power elementNodalHeat(Volume volume, VolumetricHeatSource generation) noexcept {
    // q''' V / 4 per corner. Each Tet4 shape function integrates to V/4 over
    // its own element, so the four quarters sum to q''' V exactly. Formed as
    // q''' * (V / 4) for the reason `facetNodalHeat` gives; division by four is
    // exact, so only the overflow behaviour differs.
    return Power::fromSi(generation.si() * (volume.si() / 4.0));
}

std::expected<IntegratedHeat, LoadFailure>
integrateSurfaceHeatFlux(const meshing::Mesh& mesh, const ThermalDofMap& map,
                         std::span<const meshing::ElementId> facets, HeatFlux flux,
                         std::span<Power> heatInput) {
    if (std::optional<LoadFailure> bad = checkOutput(mesh, map, heatInput); bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    if (!std::isfinite(flux.si())) {
        return std::unexpected(failure(LoadProblem::NonFiniteValue, ErrorCode::InvalidArgument,
                                       "the heat flux is not finite"));
    }
    // AN EMPTY TARGET IS NOT A ZERO LOAD. A flux the user asked for that
    // integrates over nothing is a model whose attribution chain lost a face.
    if (facets.empty()) {
        return std::unexpected(failure(LoadProblem::TargetWithoutFacets, ErrorCode::FailedPrecondition,
                                       "the target resolved to no boundary facet, so there is nothing "
                                       "to integrate the heat flux over"));
    }
    if (!strictlyAscending(facets)) {
        return std::unexpected(failure(LoadProblem::EntitiesNotUnique, ErrorCode::InvalidArgument,
                                       "the facet list is not strictly ascending, so a facet may be "
                                       "counted twice"));
    }

    IntegratedHeat out;
    double total = 0.0;
    double area = 0.0;
    for (const meshing::ElementId id : facets) {
        const meshing::Triangle* triangle = mesh.findTriangle(id);
        if (triangle == nullptr) {
            return std::unexpected(failure(LoadProblem::EntityNotInMesh, ErrorCode::NotFound,
                                           std::format("{} is not a boundary triangle of this mesh", id)));
        }
        std::array<Point3D, 3> corners{};
        std::array<ThermalDofIndex, 3> dofs{ThermalDofIndex::fromValue(0), ThermalDofIndex::fromValue(0),
                                            ThermalDofIndex::fromValue(0)};
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const meshing::Node* node = mesh.findNode(triangle->nodes[corner]);
            if (node == nullptr) {
                return std::unexpected(
                    failure(LoadProblem::EntityNotInMesh, ErrorCode::NotFound,
                            std::format("{} names {}, which is not a node of this mesh", id,
                                        triangle->nodes[corner])));
            }
            corners[corner] = node->position;
            const Result<ThermalDofIndex> dof = dofOfNode(map, triangle->nodes[corner]);
            if (!dof.has_value()) {
                return std::unexpected(LoadFailure{.problem = LoadProblem::EntityNotInMesh, .error = dof.error()});
            }
            dofs[corner] = *dof;
        }

        // P16's area, in square metres, unsigned. A zero, negative or
        // non-finite area is a facet that cannot carry a flux; a repeated
        // corner gives zero here, so it is refused by the same test.
        const double facetArea = meshing::triangleArea(corners[0], corners[1], corners[2]).si();
        if (!std::isfinite(facetArea) || !(facetArea > 0.0)) {
            return std::unexpected(failure(LoadProblem::DegenerateFacet, ErrorCode::FailedPrecondition,
                                           std::format("boundary facet {} has area {} m^2, so it cannot "
                                                       "carry a heat flux",
                                                       id, facetArea)));
        }

        const double share = facetNodalHeat(Area::fromSi(facetArea), flux).si();
        if (!std::isfinite(share)) {
            return std::unexpected(failure(LoadProblem::NonFiniteResult, ErrorCode::FailedPrecondition,
                                           std::format("the share of facet {} is not finite: {} W/m^2 over "
                                                       "{} m^2",
                                                       id, flux.si(), facetArea)));
        }
        for (const ThermalDofIndex dof : dofs) {
            if (!addTo(heatInput, dof, share)) {
                return std::unexpected(failure(LoadProblem::NonFiniteResult, ErrorCode::FailedPrecondition,
                                               std::format("superposing facet {} overflowed the heat "
                                                           "input of unknown {}",
                                                           id, dof.value())));
            }
            total = total + share;
        }
        area = area + facetArea;
    }
    out.entities = facets.size();
    out.area = Area::fromSi(area);
    out.heatRate = Power::fromSi(total);
    return out;
}

std::expected<IntegratedHeat, LoadFailure>
integrateVolumetricHeatSource(const meshing::Mesh& mesh, const ThermalDofMap& map,
                              std::span<const meshing::ElementId> elements,
                              VolumetricHeatSource generation, std::span<Power> heatInput) {
    if (std::optional<LoadFailure> bad = checkOutput(mesh, map, heatInput); bad.has_value()) {
        return std::unexpected(std::move(*bad));
    }
    if (!std::isfinite(generation.si())) {
        return std::unexpected(failure(LoadProblem::NonFiniteValue, ErrorCode::InvalidArgument,
                                       "the volumetric heat generation is not finite"));
    }
    // Likewise: a generation in a body with no element is a silent zero.
    if (elements.empty()) {
        return std::unexpected(failure(LoadProblem::BodyWithoutElements, ErrorCode::FailedPrecondition,
                                       "the target holds no volume element, so there is nothing to "
                                       "generate heat in"));
    }
    if (!strictlyAscending(elements)) {
        return std::unexpected(failure(LoadProblem::EntitiesNotUnique, ErrorCode::InvalidArgument,
                                       "the element list is not strictly ascending, so an element may "
                                       "be counted twice"));
    }

    IntegratedHeat out;
    double total = 0.0;
    double volume = 0.0;
    for (const meshing::ElementId id : elements) {
        const meshing::Tetrahedron* tet = mesh.findTetrahedron(id);
        if (tet == nullptr) {
            return std::unexpected(failure(LoadProblem::EntityNotInMesh, ErrorCode::NotFound,
                                           std::format("{} is not a tetrahedron of this mesh", id)));
        }
        const Result<Tet4Corners> corners = cornersOf(mesh, *tet);
        if (!corners.has_value()) {
            return std::unexpected(LoadFailure{.problem = LoadProblem::EntityNotInMesh, .error = corners.error()});
        }
        // P18-ELEM-001's geometry gate: finite, not degenerate, positively
        // oriented. Its volume is the SIGNED volume of a validated element, so
        // an inverted one is refused here rather than corrected by a magnitude.
        // Only the generic geometry is used; no Ke, no Ce.
        const Result<Tet4Geometry> geometry = computeTet4Geometry(*corners);
        if (!geometry.has_value()) {
            return std::unexpected(failure(LoadProblem::InvalidElement, geometry.error().code,
                                           std::format("tetrahedron {} cannot carry a generation: {}", id,
                                                       geometry.error().message)));
        }
        std::array<ThermalDofIndex, 4> dofs{ThermalDofIndex::fromValue(0), ThermalDofIndex::fromValue(0),
                                            ThermalDofIndex::fromValue(0), ThermalDofIndex::fromValue(0)};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const Result<ThermalDofIndex> dof = dofOfNode(map, tet->nodes[corner]);
            if (!dof.has_value()) {
                return std::unexpected(LoadFailure{.problem = LoadProblem::EntityNotInMesh, .error = dof.error()});
            }
            dofs[corner] = *dof;
        }

        const double elementVolume = geometry->volume().si();
        const double share = elementNodalHeat(geometry->volume(), generation).si();
        if (!std::isfinite(share)) {
            return std::unexpected(failure(LoadProblem::NonFiniteResult, ErrorCode::FailedPrecondition,
                                           std::format("the share of tetrahedron {} is not finite: {} "
                                                       "W/m^3 over {} m^3",
                                                       id, generation.si(), elementVolume)));
        }
        for (const ThermalDofIndex dof : dofs) {
            if (!addTo(heatInput, dof, share)) {
                return std::unexpected(failure(LoadProblem::NonFiniteResult, ErrorCode::FailedPrecondition,
                                               std::format("superposing tetrahedron {} overflowed the heat "
                                                           "input of unknown {}",
                                                           id, dof.value())));
            }
            total = total + share;
        }
        volume = volume + elementVolume;
    }
    out.entities = elements.size();
    out.volume = Volume::fromSi(volume);
    out.heatRate = Power::fromSi(total);
    return out;
}

// ---------------------------------------------------------------------------
// PreparedThermalLoads
// ---------------------------------------------------------------------------

std::optional<Power> PreparedThermalLoads::heatInputAt(ThermalDofIndex dof) const noexcept {
    if (dof.value() >= heatInput_.size()) {
        return std::nullopt;
    }
    return heatInput_[static_cast<std::size_t>(dof.value())];
}

Power PreparedThermalLoads::totalHeatRate() const noexcept {
    double total = 0.0;
    for (const Power entry : heatInput_) {
        total = total + entry.si();
    }
    return Power::fromSi(total);
}

Power PreparedThermalLoads::appliedSurfaceHeatRate() const noexcept {
    double total = 0.0;
    for (const ThermalLoadContribution& contribution : contributions_) {
        if (contribution.kind == ThermalLoadKind::SurfaceHeatFlux) {
            total = total + contribution.integrated.heatRate.si();
        }
    }
    return Power::fromSi(total);
}

Power PreparedThermalLoads::volumetricGenerationRate() const noexcept {
    double total = 0.0;
    for (const ThermalLoadContribution& contribution : contributions_) {
        if (contribution.kind == ThermalLoadKind::VolumetricHeatSource) {
            total = total + contribution.integrated.heatRate.si();
        }
    }
    return Power::fromSi(total);
}

bool PreparedThermalLoads::describes(const ThermalDofMap& map) const noexcept {
    return map.stamp() == source_.mesh &&
           map.dofCount() == static_cast<ThermalDofIndex::ValueType>(heatInput_.size());
}

bool PreparedThermalLoads::describes(const meshing::Mesh& mesh) const noexcept {
    // The node count as well as the stamp, for the reason MeshDofMap and
    // ThermalDofMap record: a MeshStamp identifies the BUILDER, not the
    // snapshot.
    return mesh.owns(source_.mesh) && mesh.nodes().size() == source_.nodeCount;
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

PreparedThermalLoadsSource currentLoadSource(const ThermalModel& model, std::span<const ThermalLoad> loads) {
    PreparedThermalLoadsSource source;
    source.body = model.body();
    source.mesh = model.mesh().mesh().stamp();
    source.nodeCount = model.mesh().mesh().nodes().size();
    source.geometry = model.geometryRevision();
    source.loads.assign(loads.begin(), loads.end());
    return source;
}

std::vector<LoadStaleReason> staleReasons(const PreparedThermalLoadsSource& prepared,
                                          const PreparedThermalLoadsSource& current) {
    std::vector<LoadStaleReason> reasons;
    if (prepared.body != current.body) {
        reasons.push_back(LoadStaleReason::Body);
    }
    if (!(prepared.mesh == current.mesh) || prepared.nodeCount != current.nodeCount) {
        reasons.push_back(LoadStaleReason::Mesh);
    }
    if (!(prepared.geometry == current.geometry)) {
        reasons.push_back(LoadStaleReason::Geometry);
    }
    if (!(prepared.loads == current.loads)) {
        reasons.push_back(LoadStaleReason::Loads);
    }
    return reasons;
}

bool isCurrent(const PreparedThermalLoads& prepared, const ThermalModel& model,
               std::span<const ThermalLoad> loads) {
    return staleReasons(prepared.source(), currentLoadSource(model, loads)).empty();
}

Result<void> requireCurrent(const PreparedThermalLoads& prepared, const ThermalModel& model,
                            std::span<const ThermalLoad> loads) {
    const std::vector<LoadStaleReason> reasons =
        staleReasons(prepared.source(), currentLoadSource(model, loads));
    if (reasons.empty()) {
        return Result<void>{};
    }
    std::string names;
    for (const LoadStaleReason reason : reasons) {
        if (!names.empty()) {
            names += ", ";
        }
        names += toString(reason);
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the prepared heat input no longer describes the model: {} changed. "
                                 "Prepare the loads again",
                                 names));
}

// ---------------------------------------------------------------------------
// Preparation
// ---------------------------------------------------------------------------

namespace {

/// One pass, shared by `prepareThermalLoads` and `thermalLoadProblem` so the two
/// cannot drift apart: the same checks in the same order.
struct Prepared {
    std::optional<LoadProblem> problem{};
    Error error{};
    std::vector<Power> heatInput{};
    std::vector<ThermalLoadContribution> contributions{};
};

[[nodiscard]] Prepared fail(LoadProblem problem, Error error) {
    return Prepared{.problem = problem, .error = std::move(error)};
}

[[nodiscard]] bool valueIsFinite(const ThermalLoad& load) noexcept {
    if (const SurfaceHeatFluxLoad* surface = load.surfaceHeatFlux(); surface != nullptr) {
        return std::isfinite(surface->flux.si());
    }
    if (const VolumetricHeatSourceLoad* volumetric = load.volumetricHeatSource(); volumetric != nullptr) {
        return std::isfinite(volumetric->generation.si());
    }
    return false;
}

[[nodiscard]] Prepared run(const ThermalModel& model, const ThermalDofMap& map,
                           std::span<const ThermalLoad> loads) {
    const meshing::Mesh& mesh = model.mesh().mesh();

    // THE NUMBERING FIRST. Entries are indexed by unknown; a numbering for
    // another mesh would index them by the wrong thing.
    if (!map.describes(mesh)) {
        return fail(LoadProblem::DofMapMismatch,
                    makeError(ErrorCode::FailedPrecondition,
                              "the thermal numbering was built for a different mesh than the model's "
                              "current one, so its unknowns are not these nodes")
                        .error());
    }

    // DUPLICATE IDENTITY. Two records for one ThermalLoadId means one of them
    // would be silently ignored, and nothing below could tell which.
    std::vector<ThermalLoadId> seen;
    seen.reserve(loads.size());
    for (const ThermalLoad& load : loads) {
        if (std::ranges::find(seen, load.id()) != seen.end()) {
            return fail(LoadProblem::DuplicateLoadId,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("{} appears more than once, so one record would be ignored",
                                              load.id()))
                            .error());
        }
        seen.push_back(load.id());
    }

    // THEN VALUES, before any integration, so nothing non-finite can reach the
    // vector. A negative or a zero value is valid and passes.
    for (const ThermalLoad& load : loads) {
        if (!valueIsFinite(load)) {
            return fail(LoadProblem::NonFiniteValue,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("{} carries a value that is not finite", load.id()))
                            .error());
        }
    }

    Prepared out;
    out.heatInput.assign(static_cast<std::size_t>(map.dofCount()), Power{});
    out.contributions.reserve(loads.size());

    // The mapped body's elements, resolved once and only if a volumetric load
    // asks for them.
    std::optional<meshing::MappedRegion> region;

    for (const ThermalLoad& load : loads) {
        ThermalLoadContribution contribution{.load = load.id(), .kind = load.kind()};
        std::expected<IntegratedHeat, LoadFailure> integrated = std::unexpected(LoadFailure{});

        if (const SurfaceHeatFluxLoad* surface = load.surfaceHeatFlux(); surface != nullptr) {
            // A FACE LOAD. Its target is canonical; its facets are not.
            const std::optional<TargetProblem> targetProblem = faceTargetProblem(model.map(), surface->face);
            if (targetProblem.has_value()) {
                switch (*targetProblem) {
                case TargetProblem::SelectorInvalid:
                    return fail(LoadProblem::TargetInvalid,
                                makeError(ErrorCode::InvalidArgument,
                                          std::format("{}: its target face selector is malformed: {}",
                                                      load.id(), resolveFaceTarget(model.map(), surface->face).error().message))
                                    .error());
                case TargetProblem::Unresolved:
                    return fail(LoadProblem::TargetUnresolved,
                                makeError(ErrorCode::NotFound,
                                          std::format("{}: its target face names no face of the body as "
                                                      "it is now, so the load has nothing to act on. "
                                                      "Nothing nearby is substituted",
                                                      load.id()))
                                    .error());
                case TargetProblem::WithoutFacets:
                    // Resolved, and the mapping gave it no facet. A load that
                    // integrates over nothing is not a zero load.
                    return fail(LoadProblem::TargetWithoutFacets,
                                makeError(ErrorCode::FailedPrecondition,
                                          std::format("{}: its target face resolved but the mesh "
                                                      "attributed no boundary facet to it, so there is "
                                                      "nothing to integrate over",
                                                      load.id()))
                                    .error());
                }
            }
            const Result<std::vector<meshing::ElementId>> facets = resolveFaceTarget(model.map(), surface->face);
            integrated = integrateSurfaceHeatFlux(mesh, map, *facets, surface->flux, out.heatInput);
        } else if (const VolumetricHeatSourceLoad* volumetric = load.volumetricHeatSource();
                   volumetric != nullptr) {
            if (!region.has_value()) {
                Result<meshing::MappedRegion> mapped = meshing::regionOf(model.map(), mesh);
                if (!mapped.has_value()) {
                    return fail(LoadProblem::BodyWithoutElements,
                                makeError(mapped.error().code,
                                          std::format("{}: the mapped body has no volume element to "
                                                      "generate heat in: {}",
                                                      load.id(), mapped.error().message))
                                    .error());
                }
                region = std::move(*mapped);
            }
            if (region->elements.empty()) {
                return fail(LoadProblem::BodyWithoutElements,
                            makeError(ErrorCode::FailedPrecondition,
                                      std::format("{}: the mapped body has no volume element, so a "
                                                  "generation would be a silent zero",
                                                  load.id()))
                                .error());
            }
            integrated = integrateVolumetricHeatSource(mesh, map, region->elements,
                                                       volumetric->generation, out.heatInput);
        } else {
            return fail(LoadProblem::TargetUnresolved,
                        makeError(ErrorCode::InvalidArgument,
                                  std::format("{} carries no payload", load.id()))
                            .error());
        }

        if (!integrated.has_value()) {
            // Name the load; keep the classification and the cause.
            return fail(integrated.error().problem,
                        makeError(integrated.error().error.code,
                                  std::format("{}: {}", load.id(), integrated.error().error.message))
                            .error());
        }
        contribution.integrated = *integrated;
        out.contributions.push_back(contribution);
    }

    // EVERY ENTRY IS FINITE, BY CONSTRUCTION. Each share is checked finite
    // before it is added, and `addTo` refuses an addition whose SUM overflows, so
    // no entry of the vector can have become infinite or NaN by the time every
    // load has been integrated. A final scan over the vector would be dead code:
    // no input can reach it (the first mutation batch found exactly that - the
    // scan could be deleted and no test could tell). The two gates that CAN fire
    // each have a test: a share that overflows, and a running sum that does.
    return out;
}

} // namespace

Result<PreparedThermalLoads> prepareThermalLoads(const ThermalModel& model, const ThermalDofMap& map,
                                                 std::span<const ThermalLoad> loads) {
    Prepared prepared = run(model, map, loads);
    if (prepared.problem.has_value()) {
        return std::unexpected(std::move(prepared.error));
    }
    PreparedThermalLoads out;
    out.source_ = currentLoadSource(model, loads);
    out.heatInput_ = std::move(prepared.heatInput);
    out.contributions_ = std::move(prepared.contributions);
    return out;
}

Result<PreparedThermalLoads> prepareThermalLoads(const ThermalModel& model, const ThermalDofMap& map,
                                                 const ThermalAnalysisDefinition& definition) {
    return prepareThermalLoads(model, map, std::span<const ThermalLoad>{definition.loads});
}

std::optional<LoadProblem> thermalLoadProblem(const ThermalModel& model, const ThermalDofMap& map,
                                              std::span<const ThermalLoad> loads) {
    return run(model, map, loads).problem;
}

} // namespace bettercad::thermal
