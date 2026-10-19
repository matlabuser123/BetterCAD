// Canonical thermal boundary conditions and their conversion to temperature
// constraints and convection terms (P18-BC-001).
//
// EVERY FACE TARGET IS RESOLVED THROUGH P16, and that is the central discipline
// of this file. `meshing::boundaryFacetsOf` decides which facets belong to a
// `FaceName`; nothing here measures a distance to a plane, compares a normal,
// tests a centroid or picks a nearest face. Facet geometry is read only AFTER
// P16 has answered, and then only to integrate.
//
// A PRESCRIBED TEMPERATURE IS A VALUE, NOT A BIT. P17's restraints hold a
// displacement at zero, so P17 records only WHICH unknowns are fixed. A
// temperature is arbitrary: 350 K stays 350 K through the whole chain, and a
// free node has no temperature, not 0 K.
//
// THE TWO KINDS COMBINE DIFFERENTLY. Prescriptions are deduplicated when they
// agree and refused when they do not (by P18-DOF-001's constraint map, which
// this file only feeds and annotates). Convection terms are never deduplicated:
// two conditions on a face are two exchanges and both are kept, in the order
// given, for the assembler to add.
//
// NOTHING MESH-LOCAL IS STORED IN A CONDITION. The canonical conditions carry a
// FaceName and the physical numbers; the facets, nodes, unknowns, constraints
// and terms are rebuilt on every call.
//
// NO UNORDERED CONTAINER, NO PARALLELISM, NO STATIC STATE. The only ordered
// associative container sorts conflict records by condition id, so a diagnostic
// does not depend on the order conditions were listed in.

#include <bettercad/thermal/ThermalBoundary.hpp>

#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/thermal/ThermalTarget.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <numeric>
#include <utility>

namespace bettercad::thermal {
namespace {

[[nodiscard]] BoundaryFailure failure(BoundaryProblem problem, ErrorCode code, std::string message) {
    return BoundaryFailure{.problem = problem, .error = makeError(code, std::move(message)).error(), .conflicts = {}};
}

/// Strictly ascending: no repeat and no reordering. P16 delivers its sets this
/// way, and a list that is not would count an entity twice.
template <typename Id>
[[nodiscard]] bool strictlyAscending(std::span<const Id> ids) noexcept {
    return std::ranges::adjacent_find(ids, [](const Id& a, const Id& b) { return !(a < b); }) ==
           ids.end();
}

/// The same failure, with the offending condition named in front of its message.
[[nodiscard]] BoundaryFailure named(ThermalBoundaryConditionId condition, BoundaryFailure inner) {
    inner.error.message = std::format("{}: {}", condition, inner.error.message);
    return inner;
}

} // namespace

std::string_view toString(BoundaryProblem problem) noexcept {
    switch (problem) {
    case BoundaryProblem::InvalidIdentity:
        return "invalid_identity";
    case BoundaryProblem::DuplicateConditionId:
        return "duplicate_condition_id";
    case BoundaryProblem::TemperatureInvalid:
        return "temperature_invalid";
    case BoundaryProblem::CoefficientInvalid:
        return "coefficient_invalid";
    case BoundaryProblem::AmbientInvalid:
        return "ambient_invalid";
    case BoundaryProblem::TargetInvalid:
        return "target_invalid";
    case BoundaryProblem::TargetUnresolved:
        return "target_unresolved";
    case BoundaryProblem::TargetWithoutFacets:
        return "target_without_facets";
    case BoundaryProblem::DegenerateFacet:
        return "degenerate_facet";
    case BoundaryProblem::EntityNotInMesh:
        return "entity_not_in_mesh";
    case BoundaryProblem::EntitiesNotUnique:
        return "entities_not_unique";
    case BoundaryProblem::DofMapMismatch:
        return "dof_map_mismatch";
    case BoundaryProblem::ConflictingTemperature:
        return "conflicting_temperature";
    case BoundaryProblem::NonFiniteResult:
        return "non_finite_result";
    }
    return "unknown";
}

std::string_view toString(BoundaryStaleReason reason) noexcept {
    switch (reason) {
    case BoundaryStaleReason::Body:
        return "body";
    case BoundaryStaleReason::Mesh:
        return "mesh";
    case BoundaryStaleReason::Geometry:
        return "geometry";
    case BoundaryStaleReason::Conditions:
        return "conditions";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Prescribed temperature
// ---------------------------------------------------------------------------

std::expected<std::vector<ThermalDofIndex>, BoundaryFailure>
uniqueFacetDofs(const meshing::Mesh& mesh, const ThermalDofMap& map, std::span<const meshing::ElementId> facets) {
    if (!map.describes(mesh)) {
        return std::unexpected(failure(BoundaryProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       "the thermal numbering was built for a different mesh, so its "
                                       "unknowns are not this mesh's nodes"));
    }
    // AN EMPTY TARGET IS NOT AN EMPTY CONDITION. A temperature the user asked
    // for that constrains nothing is a model whose attribution chain lost a face.
    if (facets.empty()) {
        return std::unexpected(failure(BoundaryProblem::TargetWithoutFacets, ErrorCode::FailedPrecondition,
                                       "the target resolved to no boundary facet, so there is no node to "
                                       "prescribe a temperature at"));
    }
    if (!strictlyAscending(facets)) {
        return std::unexpected(failure(BoundaryProblem::EntitiesNotUnique, ErrorCode::InvalidArgument,
                                       "the facet list is not strictly ascending, so a facet may be "
                                       "counted twice"));
    }

    std::vector<ThermalDofIndex> dofs;
    dofs.reserve(facets.size() * kTri3Nodes);
    for (const meshing::ElementId id : facets) {
        const meshing::Triangle* triangle = mesh.findTriangle(id);
        if (triangle == nullptr) {
            return std::unexpected(failure(BoundaryProblem::EntityNotInMesh, ErrorCode::NotFound,
                                           std::format("{} is not a boundary triangle of this mesh", id)));
        }
        for (std::size_t corner = 0; corner < kTri3Nodes; ++corner) {
            const Result<ThermalDofIndex> dof = map.dofOf(triangle->nodes[corner]);
            if (!dof.has_value()) {
                return std::unexpected(BoundaryFailure{.problem = BoundaryProblem::EntityNotInMesh,
                                                       .error = dof.error(),
                                                       .conflicts = {}});
            }
            dofs.push_back(*dof);
        }
    }
    // ONE CONSTRAINT PER UNKNOWN. Facets (n1 n2 n3) and (n1 n3 n4) give six
    // corners and four unknowns. Sorted by ThermalDofIndex, never by NodeId.
    std::ranges::sort(dofs, [](ThermalDofIndex a, ThermalDofIndex b) { return a.value() < b.value(); });
    const auto last = std::ranges::unique(dofs, [](ThermalDofIndex a, ThermalDofIndex b) { return a.value() == b.value(); });
    dofs.erase(last.begin(), last.end());
    return dofs;
}

namespace {

/// Every pair of requests on one unknown that disagree, once per pair of
/// conditions, ascending by (first, second). EVERY unknown is examined; the
/// summary hides none.
[[nodiscard]] std::vector<TemperatureConflict> conflictsOf(std::span<const TemperatureRequest> requests) {
    std::vector<std::size_t> order(requests.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) {
        return requests[a].dof.value() < requests[b].dof.value();
    });

    struct Counter {
        TemperatureConflict record;
        std::uint64_t lastDof = 0;
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, Counter> pairs;
    for (std::size_t begin = 0; begin < order.size();) {
        std::size_t end = begin;
        while (end < order.size() && requests[order[end]].dof.value() == requests[order[begin]].dof.value()) {
            ++end;
        }
        for (std::size_t i = begin; i < end; ++i) {
            for (std::size_t j = i + 1; j < end; ++j) {
                const TemperatureRequest* a = &requests[order[i]];
                const TemperatureRequest* b = &requests[order[j]];
                // `!=` is true for a NaN, which is correct: a NaN request never
                // agrees with anything.
                if (a->value.si() == b->value.si()) {
                    continue;
                }
                if (b->condition.value() < a->condition.value()) {
                    std::swap(a, b);
                }
                const auto key = std::make_pair(a->condition.value(), b->condition.value());
                const auto found = pairs.find(key);
                if (found == pairs.end()) {
                    pairs.emplace(key, Counter{.record = TemperatureConflict{.first = a->condition,
                                                                             .second = b->condition,
                                                                             .firstValue = a->value,
                                                                             .secondValue = b->value,
                                                                             .unknownCount = 1,
                                                                             .firstDof = a->dof},
                                               .lastDof = a->dof.value()});
                } else if (found->second.lastDof != a->dof.value()) {
                    ++found->second.record.unknownCount;
                    found->second.lastDof = a->dof.value();
                }
            }
        }
        begin = end;
    }
    std::vector<TemperatureConflict> out;
    out.reserve(pairs.size());
    for (const auto& [key, counter] : pairs) {
        out.push_back(counter.record);
    }
    return out;
}

} // namespace

std::expected<ResolvedTemperatureConstraints, BoundaryFailure>
resolveTemperatureConstraints(const ThermalDofMap& map, std::span<const TemperatureRequest> requests) {
    std::vector<ThermalTemperatureConstraint> wanted;
    wanted.reserve(requests.size());
    for (const TemperatureRequest& request : requests) {
        wanted.push_back(ThermalTemperatureConstraint{.dof = request.dof, .temperature = request.value});
    }

    Result<ThermalConstraintMap> built = buildThermalConstraintMap(map, wanted);
    if (!built.has_value()) {
        // P18-DOF-001 refused; classify with its own rule so the two cannot
        // drift apart. Contradiction is the case this layer adds detail to.
        const std::optional<ConstraintProblem> problem = thermalConstraintProblem(map, wanted);
        if (problem == ConstraintProblem::DofNotInMap) {
            return std::unexpected(
                BoundaryFailure{.problem = BoundaryProblem::EntityNotInMesh, .error = built.error(), .conflicts = {}});
        }
        if (problem == ConstraintProblem::TemperatureNotAbsolute) {
            return std::unexpected(BoundaryFailure{
                .problem = BoundaryProblem::TemperatureInvalid, .error = built.error(), .conflicts = {}});
        }
        BoundaryFailure out = failure(BoundaryProblem::ConflictingTemperature, ErrorCode::InvalidArgument, "");
        out.conflicts = conflictsOf(requests);
        std::string detail;
        constexpr std::size_t kNamed = 4;
        for (std::size_t i = 0; i < out.conflicts.size() && i < kNamed; ++i) {
            const TemperatureConflict& c = out.conflicts[i];
            if (!detail.empty()) {
                detail += "; ";
            }
            detail += std::format("{} ({} K) and {} ({} K) disagree on {} unknown(s), the first being {}",
                                  c.first, c.firstValue.si(), c.second, c.secondValue.si(), c.unknownCount,
                                  c.firstDof.value());
        }
        if (out.conflicts.size() > kNamed) {
            detail += std::format("; and {} more pair(s)", out.conflicts.size() - kNamed);
        }
        out.error.message = std::format("contradictory prescribed temperatures, none chosen over another: {}", detail);
        return std::unexpected(std::move(out));
    }

    // ATTRIBUTION, parallel to the map's own ascending order.
    std::vector<std::size_t> order(requests.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) {
        return requests[a].dof.value() < requests[b].dof.value();
    });
    std::vector<std::vector<ThermalBoundaryConditionId>> sources;
    sources.reserve(built->size());
    for (std::size_t begin = 0; begin < order.size();) {
        std::size_t end = begin;
        std::vector<ThermalBoundaryConditionId> ids;
        while (end < order.size() && requests[order[end]].dof.value() == requests[order[begin]].dof.value()) {
            const ThermalBoundaryConditionId id = requests[order[end]].condition;
            if (std::ranges::find(ids, id) == ids.end()) {
                ids.push_back(id);
            }
            ++end;
        }
        sources.push_back(std::move(ids));
        begin = end;
    }
    return ResolvedTemperatureConstraints{.constraints = std::move(*built), .sources = std::move(sources)};
}

// ---------------------------------------------------------------------------
// Convection
// ---------------------------------------------------------------------------

std::expected<std::vector<ConvectionFacet>, BoundaryFailure>
integrateConvection(const meshing::Mesh& mesh, const ThermalDofMap& map, std::span<const meshing::ElementId> facets,
                    HeatTransferCoefficient coefficient, Temperature ambient) {
    if (!map.describes(mesh)) {
        return std::unexpected(failure(BoundaryProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       "the thermal numbering was built for a different mesh, so its "
                                       "unknowns are not this mesh's nodes"));
    }
    // `!(h >= 0)` refuses a NaN as well as a negative; zero passes.
    if (!std::isfinite(coefficient.si()) || !(coefficient.si() >= 0.0)) {
        return std::unexpected(failure(BoundaryProblem::CoefficientInvalid, ErrorCode::InvalidArgument,
                                       std::format("a convection coefficient of {} W/(m^2 K) is not finite and "
                                                   "non-negative",
                                                   coefficient.si())));
    }
    if (!isAbsoluteTemperature(ambient)) {
        return std::unexpected(failure(BoundaryProblem::AmbientInvalid, ErrorCode::InvalidArgument,
                                       std::format("an ambient of {} K is not a finite absolute temperature",
                                                   ambient.si())));
    }
    if (facets.empty()) {
        return std::unexpected(failure(BoundaryProblem::TargetWithoutFacets, ErrorCode::FailedPrecondition,
                                       "the target resolved to no boundary facet, so there is nothing to "
                                       "exchange heat through"));
    }
    if (!strictlyAscending(facets)) {
        return std::unexpected(failure(BoundaryProblem::EntitiesNotUnique, ErrorCode::InvalidArgument,
                                       "the facet list is not strictly ascending, so a facet may be "
                                       "counted twice"));
    }

    std::vector<ConvectionFacet> out;
    out.reserve(facets.size());
    for (const meshing::ElementId id : facets) {
        const meshing::Triangle* triangle = mesh.findTriangle(id);
        if (triangle == nullptr) {
            return std::unexpected(failure(BoundaryProblem::EntityNotInMesh, ErrorCode::NotFound,
                                           std::format("{} is not a boundary triangle of this mesh", id)));
        }
        Tri3Corners corners{};
        std::array<ThermalDofIndex, kTri3Nodes> dofs{ThermalDofIndex::fromValue(0), ThermalDofIndex::fromValue(0),
                                                     ThermalDofIndex::fromValue(0)};
        for (std::size_t corner = 0; corner < kTri3Nodes; ++corner) {
            const meshing::Node* node = mesh.findNode(triangle->nodes[corner]);
            if (node == nullptr) {
                return std::unexpected(failure(BoundaryProblem::EntityNotInMesh, ErrorCode::NotFound,
                                               std::format("{} names {}, which is not a node of this mesh", id,
                                                           triangle->nodes[corner])));
            }
            corners[corner] = node->position;
            const Result<ThermalDofIndex> dof = map.dofOf(triangle->nodes[corner]);
            if (!dof.has_value()) {
                return std::unexpected(BoundaryFailure{.problem = BoundaryProblem::EntityNotInMesh,
                                                       .error = dof.error(),
                                                       .conflicts = {}});
            }
            dofs[corner] = *dof;
        }
        // P16's area, in square metres, unsigned. Zero, negative or non-finite is
        // a facet that cannot carry a term -- never a zero contribution. A
        // repeated corner gives zero here, so it is refused by the same test.
        const Area area = meshing::triangleArea(corners[0], corners[1], corners[2]);
        if (!std::isfinite(area.si()) || !(area.si() > 0.0)) {
            return std::unexpected(failure(BoundaryProblem::DegenerateFacet, ErrorCode::FailedPrecondition,
                                           std::format("boundary facet {} has area {} m^2, so it cannot "
                                                       "carry a convection term",
                                                       id, area.si())));
        }
        Result<Tri3Convection> term = computeTri3Convection(area, coefficient, ambient);
        if (!term.has_value()) {
            // h, the ambient and the area are all valid here, so the only way
            // to fail is an entry that overflowed.
            return std::unexpected(
                BoundaryFailure{.problem = BoundaryProblem::NonFiniteResult, .error = term.error(), .conflicts = {}});
        }
        out.push_back(ConvectionFacet{.facet = id, .dofs = dofs, .term = std::move(*term)});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Prepared records
// ---------------------------------------------------------------------------

Area PreparedConvectionBoundary::area() const noexcept {
    double total = 0.0;
    for (const ConvectionFacet& facet : facets) {
        total = total + facet.term.area().si();
    }
    return Area::fromSi(total);
}

ThermalConductance PreparedConvectionBoundary::conductance() const noexcept {
    double total = 0.0;
    for (const ConvectionFacet& facet : facets) {
        for (std::size_t row = 0; row < kTri3Nodes; ++row) {
            for (std::size_t column = 0; column < kTri3Nodes; ++column) {
                total = total + facet.term(row, column).si();
            }
        }
    }
    return ThermalConductance::fromSi(total);
}

Power PreparedConvectionBoundary::source() const noexcept {
    double total = 0.0;
    for (const ConvectionFacet& facet : facets) {
        for (std::size_t node = 0; node < kTri3Nodes; ++node) {
            total = total + facet.term.source(node).si();
        }
    }
    return Power::fromSi(total);
}

std::span<const ThermalBoundaryConditionId> PreparedThermalBoundary::constraintSources(std::size_t index) const noexcept {
    if (index >= constraintSources_.size()) {
        return {};
    }
    return constraintSources_[index];
}

Area PreparedThermalBoundary::convectionArea() const noexcept {
    double total = 0.0;
    for (const PreparedConvectionBoundary& convection : convections_) {
        total = total + convection.area().si();
    }
    return Area::fromSi(total);
}

ThermalConductance PreparedThermalBoundary::convectionConductance() const noexcept {
    double total = 0.0;
    for (const PreparedConvectionBoundary& convection : convections_) {
        total = total + convection.conductance().si();
    }
    return ThermalConductance::fromSi(total);
}

Power PreparedThermalBoundary::convectionSource() const noexcept {
    double total = 0.0;
    for (const PreparedConvectionBoundary& convection : convections_) {
        total = total + convection.source().si();
    }
    return Power::fromSi(total);
}

bool PreparedThermalBoundary::describes(const ThermalDofMap& map) const noexcept {
    return map.stamp() == source_.mesh && map.dofCount() == constraints_->dofCount();
}

bool PreparedThermalBoundary::describes(const meshing::Mesh& mesh) const noexcept {
    // The node count as well as the stamp, for the reason MeshDofMap and
    // ThermalDofMap record: a MeshStamp identifies the BUILDER, not the snapshot.
    return mesh.owns(source_.mesh) && mesh.nodes().size() == source_.nodeCount;
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

PreparedThermalBoundarySource currentBoundarySource(const ThermalModel& model,
                                                    std::span<const ThermalBoundaryCondition> conditions) {
    PreparedThermalBoundarySource source;
    source.body = model.body();
    source.mesh = model.mesh().mesh().stamp();
    source.nodeCount = model.mesh().mesh().nodes().size();
    source.geometry = model.geometryRevision();
    source.conditions.assign(conditions.begin(), conditions.end());
    return source;
}

std::vector<BoundaryStaleReason> staleReasons(const PreparedThermalBoundarySource& prepared,
                                              const PreparedThermalBoundarySource& current) {
    std::vector<BoundaryStaleReason> reasons;
    if (prepared.body != current.body) {
        reasons.push_back(BoundaryStaleReason::Body);
    }
    if (!(prepared.mesh == current.mesh) || prepared.nodeCount != current.nodeCount) {
        reasons.push_back(BoundaryStaleReason::Mesh);
    }
    if (!(prepared.geometry == current.geometry)) {
        reasons.push_back(BoundaryStaleReason::Geometry);
    }
    if (!(prepared.conditions == current.conditions)) {
        reasons.push_back(BoundaryStaleReason::Conditions);
    }
    return reasons;
}

bool isCurrent(const PreparedThermalBoundary& prepared, const ThermalModel& model,
               std::span<const ThermalBoundaryCondition> conditions) {
    return staleReasons(prepared.source(), currentBoundarySource(model, conditions)).empty();
}

Result<void> requireCurrent(const PreparedThermalBoundary& prepared, const ThermalModel& model,
                            std::span<const ThermalBoundaryCondition> conditions) {
    const std::vector<BoundaryStaleReason> reasons =
        staleReasons(prepared.source(), currentBoundarySource(model, conditions));
    if (reasons.empty()) {
        return Result<void>{};
    }
    std::string names;
    for (const BoundaryStaleReason reason : reasons) {
        if (!names.empty()) {
            names += ", ";
        }
        names += toString(reason);
    }
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("the prepared boundary no longer describes the model: {} changed. "
                                 "Prepare the boundary conditions again",
                                 names));
}

// ---------------------------------------------------------------------------
// Preparation
// ---------------------------------------------------------------------------

namespace {

/// Everything a successful preparation produces, before it is sealed into the
/// class. One pass, shared by `prepareThermalBoundary` and
/// `thermalBoundaryFailure` so the two cannot drift apart: the same checks in the
/// same order.
struct Built {
    ResolvedTemperatureConstraints constraints;
    std::vector<PreparedTemperatureBoundary> temperatures;
    std::vector<PreparedConvectionBoundary> convections;
};

[[nodiscard]] std::expected<Built, BoundaryFailure> run(const ThermalModel& model, const ThermalDofMap& map,
                                                        std::span<const ThermalBoundaryCondition> conditions) {
    const meshing::Mesh& mesh = model.mesh().mesh();

    // THE NUMBERING FIRST. Unknowns are what the constraints name; a numbering
    // for another mesh would name the wrong nodes.
    if (!map.describes(mesh)) {
        return std::unexpected(failure(BoundaryProblem::DofMapMismatch, ErrorCode::FailedPrecondition,
                                       "the thermal numbering was built for a different mesh than the model's "
                                       "current one, so its unknowns are not these nodes"));
    }

    // IDENTITY. Two records for one id means one would be silently ignored, and
    // nothing below could tell which.
    std::vector<ThermalBoundaryConditionId> seen;
    seen.reserve(conditions.size());
    for (const ThermalBoundaryCondition& condition : conditions) {
        if (!condition.id().isValid()) {
            return std::unexpected(failure(BoundaryProblem::InvalidIdentity, ErrorCode::InvalidArgument,
                                           "a boundary condition carries an invalid identity"));
        }
        if (std::ranges::find(seen, condition.id()) != seen.end()) {
            return std::unexpected(failure(BoundaryProblem::DuplicateConditionId, ErrorCode::InvalidArgument,
                                           std::format("{} appears more than once, so one record would be "
                                                       "ignored",
                                                       condition.id())));
        }
        seen.push_back(condition.id());
    }

    // THEN VALUES, before any resolution, by the CANONICAL rule (P18-DATA-001's
    // `validate`): a temperature is a finite absolute one, a coefficient is
    // finite and strictly positive, an ambient is a finite absolute one. The
    // ambient is checked whatever the coefficient is: a malformed value is
    // malformed whether or not it is multiplied by something small.
    for (const ThermalBoundaryCondition& condition : conditions) {
        if (const PrescribedTemperature* held = condition.prescribedTemperature(); held != nullptr) {
            if (!isAbsoluteTemperature(held->value)) {
                return std::unexpected(failure(BoundaryProblem::TemperatureInvalid, ErrorCode::InvalidArgument,
                                               std::format("{} prescribes {} K, which is not a finite "
                                                           "absolute temperature",
                                                           condition.id(), held->value.si())));
            }
        } else if (const ConvectionBoundary* convection = condition.convection(); convection != nullptr) {
            if (!std::isfinite(convection->coefficient.si()) || !(convection->coefficient.si() > 0.0)) {
                return std::unexpected(failure(BoundaryProblem::CoefficientInvalid, ErrorCode::InvalidArgument,
                                               std::format("{} has a convection coefficient of {} W/(m^2 K); "
                                                           "it must be finite and positive",
                                                           condition.id(), convection->coefficient.si())));
            }
            if (!isAbsoluteTemperature(convection->ambient)) {
                return std::unexpected(failure(BoundaryProblem::AmbientInvalid, ErrorCode::InvalidArgument,
                                               std::format("{} has an ambient of {} K, which is not a "
                                                           "finite absolute temperature",
                                                           condition.id(), convection->ambient.si())));
            }
        }
    }

    std::vector<TemperatureRequest> requests;
    std::vector<PreparedTemperatureBoundary> temperatures;
    std::vector<PreparedConvectionBoundary> convections;

    for (const ThermalBoundaryCondition& condition : conditions) {
        // A FACE TARGET. Its face is canonical; its facets are not.
        const FaceName& face = condition.face();
        if (const std::optional<TargetProblem> targetProblem = faceTargetProblem(model.map(), face);
            targetProblem.has_value()) {
            switch (*targetProblem) {
            case TargetProblem::SelectorInvalid:
                return std::unexpected(failure(
                    BoundaryProblem::TargetInvalid, ErrorCode::InvalidArgument,
                    std::format("{}: its target face selector is malformed: {}", condition.id(),
                                resolveFaceTarget(model.map(), face).error().message)));
            case TargetProblem::Unresolved:
                return std::unexpected(failure(
                    BoundaryProblem::TargetUnresolved, ErrorCode::NotFound,
                    std::format("{}: its target face names no face of the body as it is now, so the "
                                "condition has nothing to act on. Nothing nearby is substituted",
                                condition.id())));
            case TargetProblem::WithoutFacets:
                // Resolved, and the mapping gave it no facet. A condition that
                // acts on nothing is not an empty condition.
                return std::unexpected(failure(
                    BoundaryProblem::TargetWithoutFacets, ErrorCode::FailedPrecondition,
                    std::format("{}: its target face resolved but the mesh attributed no boundary facet "
                                "to it, so there is nothing to act on",
                                condition.id())));
            }
        }
        const Result<std::vector<meshing::ElementId>> facets = resolveFaceTarget(model.map(), face);

        if (const PrescribedTemperature* held = condition.prescribedTemperature(); held != nullptr) {
            std::expected<std::vector<ThermalDofIndex>, BoundaryFailure> dofs = uniqueFacetDofs(mesh, map, *facets);
            if (!dofs.has_value()) {
                return std::unexpected(named(condition.id(), std::move(dofs.error())));
            }
            for (const ThermalDofIndex dof : *dofs) {
                requests.push_back(TemperatureRequest{.condition = condition.id(), .dof = dof, .value = held->value});
            }
            temperatures.push_back(PreparedTemperatureBoundary{
                .condition = condition.id(), .value = held->value, .facetCount = facets->size(), .dofs = std::move(*dofs)});
        } else {
            const ConvectionBoundary* convection = condition.convection();
            std::expected<std::vector<ConvectionFacet>, BoundaryFailure> terms =
                integrateConvection(mesh, map, *facets, convection->coefficient, convection->ambient);
            if (!terms.has_value()) {
                return std::unexpected(named(condition.id(), std::move(terms.error())));
            }
            convections.push_back(PreparedConvectionBoundary{.condition = condition.id(),
                                                            .coefficient = convection->coefficient,
                                                            .ambient = convection->ambient,
                                                            .facets = std::move(*terms)});
        }
    }

    // CONTRADICTIONS LAST, over every prescription of every condition together:
    // two faces that meet along an edge disagree there even though each is fine
    // on its own.
    std::expected<ResolvedTemperatureConstraints, BoundaryFailure> resolved =
        resolveTemperatureConstraints(map, requests);
    if (!resolved.has_value()) {
        return std::unexpected(std::move(resolved.error()));
    }
    return Built{.constraints = std::move(*resolved),
                 .temperatures = std::move(temperatures),
                 .convections = std::move(convections)};
}

} // namespace

Result<PreparedThermalBoundary> prepareThermalBoundary(const ThermalModel& model, const ThermalDofMap& map,
                                                       std::span<const ThermalBoundaryCondition> conditions) {
    std::expected<Built, BoundaryFailure> built = run(model, map, conditions);
    if (!built.has_value()) {
        return std::unexpected(std::move(built.error().error));
    }
    PreparedThermalBoundary out;
    out.source_ = currentBoundarySource(model, conditions);
    out.constraints_ = std::move(built->constraints.constraints);
    out.constraintSources_ = std::move(built->constraints.sources);
    out.temperatures_ = std::move(built->temperatures);
    out.convections_ = std::move(built->convections);
    return out;
}

Result<PreparedThermalBoundary> prepareThermalBoundary(const ThermalModel& model, const ThermalDofMap& map,
                                                       const ThermalAnalysisDefinition& definition) {
    return prepareThermalBoundary(model, map, std::span<const ThermalBoundaryCondition>{definition.boundaryConditions});
}

std::optional<BoundaryFailure> thermalBoundaryFailure(const ThermalModel& model, const ThermalDofMap& map,
                                                      std::span<const ThermalBoundaryCondition> conditions) {
    std::expected<Built, BoundaryFailure> built = run(model, map, conditions);
    if (built.has_value()) {
        return std::nullopt;
    }
    return std::move(built.error());
}

std::optional<BoundaryProblem> thermalBoundaryProblem(const ThermalModel& model, const ThermalDofMap& map,
                                                      std::span<const ThermalBoundaryCondition> conditions) {
    const std::optional<BoundaryFailure> refused = thermalBoundaryFailure(model, map, conditions);
    if (!refused.has_value()) {
        return std::nullopt;
    }
    return refused->problem;
}

} // namespace bettercad::thermal
