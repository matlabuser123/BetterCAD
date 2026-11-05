// The CFD boundary conditions of one analysis, resolved against one mesh
// (P19-BC-001, ADR-067).
//
// EVERY FACE TARGET IS RESOLVED THROUGH P16, and that is the central discipline
// of this file. `cfd::resolveFaceTarget` reads P16's answer; nothing here
// measures a distance to a plane, compares a normal to choose a face, tests a
// centroid for membership of a face or picks a nearest face. Facet geometry is
// read only AFTER P16 has answered, and then only to build a prescribed value
// or to refuse a condition the geometry refutes.
//
// NO UNORDERED CONTAINER, NO PARALLELISM, NO STATIC STATE. Conditions are
// processed in the order given, which is the definition's canonical order;
// facets come from P16's ascending deduplicated sets; boundary faces are walked
// in BoundaryFaceIndex order. So the same inputs give the same bits in every
// build configuration.
//
// ATOMIC. One refusal refuses the whole set, and the prepared value is
// assembled only after every check has passed.

#include <bettercad/cfd/CfdBoundary.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace bettercad::cfd {

std::string_view toString(CfdBoundaryPolicy policy) noexcept {
    switch (policy) {
    case CfdBoundaryPolicy::FixedValue:
        return "fixed_value";
    case CfdBoundaryPolicy::ZeroGradient:
        return "zero_gradient";
    case CfdBoundaryPolicy::Symmetry:
        return "symmetry";
    case CfdBoundaryPolicy::Computed:
        return "computed";
    }
    return "unknown";
}

std::string_view toString(CfdBoundaryProblem problem) noexcept {
    switch (problem) {
    case CfdBoundaryProblem::TopologyOfAnotherMesh:
        return "topology_of_another_mesh";
    case CfdBoundaryProblem::GeometryOfAnotherTopology:
        return "geometry_of_another_topology";
    case CfdBoundaryProblem::UnnamedPatch:
        return "unnamed_patch";
    case CfdBoundaryProblem::ConditionInvalid:
        return "condition_invalid";
    case CfdBoundaryProblem::DuplicateConditionId:
        return "duplicate_condition_id";
    case CfdBoundaryProblem::TargetUnresolved:
        return "target_unresolved";
    case CfdBoundaryProblem::TargetWithoutFacets:
        return "target_without_facets";
    case CfdBoundaryProblem::TargetFacetsNotUnique:
        return "target_facets_not_unique";
    case CfdBoundaryProblem::FacetNotInTopology:
        return "facet_not_in_topology";
    case CfdBoundaryProblem::FacetClaimedTwice:
        return "facet_claimed_twice";
    case CfdBoundaryProblem::FacetWithoutCondition:
        return "facet_without_condition";
    case CfdBoundaryProblem::DegenerateFacet:
        return "degenerate_facet";
    case CfdBoundaryProblem::NonFiniteBoundaryValue:
        return "non_finite_boundary_value";
    case CfdBoundaryProblem::WallTargetNotPlanar:
        return "wall_target_not_planar";
    case CfdBoundaryProblem::WallVelocityNotTangential:
        return "wall_velocity_not_tangential";
    case CfdBoundaryProblem::DatumInvalid:
        return "datum_invalid";
    case CfdBoundaryProblem::PressureDatumMissing:
        return "pressure_datum_missing";
    case CfdBoundaryProblem::PressureDatumOverdetermined:
        return "pressure_datum_overdetermined";
    case CfdBoundaryProblem::HydrostaticDensityMissing:
        return "hydrostatic_density_missing";
    case CfdBoundaryProblem::ReferencePointOutsideFluid:
        return "reference_point_outside_fluid";
    }
    return "unknown";
}

std::string_view toString(CfdBoundaryStaleReason reason) noexcept {
    switch (reason) {
    case CfdBoundaryStaleReason::Body:
        return "body";
    case CfdBoundaryStaleReason::Mesh:
        return "mesh";
    case CfdBoundaryStaleReason::Geometry:
        return "geometry";
    case CfdBoundaryStaleReason::Conditions:
        return "conditions";
    case CfdBoundaryStaleReason::Datum:
        return "datum";
    case CfdBoundaryStaleReason::Fluid:
        return "fluid";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

CfdBoundaryTreatment treatmentOf(CfdBoundaryConditionKind kind) noexcept {
    using P = CfdBoundaryPolicy;
    switch (kind) {
    case CfdBoundaryConditionKind::VelocityInlet:
        // The velocity is prescribed normal to the face and the mass flux
        // follows from it; the pressure there is an outcome of the solve.
        return {.pressure = P::ZeroGradient, .velocity = P::FixedValue, .massFlux = P::FixedValue};
    case CfdBoundaryConditionKind::PressureInlet:
    case CfdBoundaryConditionKind::PressureOutlet:
        // ADR-058: "the flow rate is an OUTCOME of the solve, not an input".
        // The same statement as admitting backflow at an outlet without
        // clamping it (ADR-067 Decision 9).
        return {.pressure = P::FixedValue, .velocity = P::ZeroGradient, .massFlux = P::Computed};
    case CfdBoundaryConditionKind::NoSlipWall:
    case CfdBoundaryConditionKind::MovingWall:
        // A wall carries no through-flow, so the mass flux is fixed at zero;
        // the wall's own velocity is prescribed (zero for no-slip).
        return {.pressure = P::ZeroGradient, .velocity = P::FixedValue, .massFlux = P::FixedValue};
    case CfdBoundaryConditionKind::Slip:
        // NEITHER of the first two for the velocity: the normal component is
        // zero and the tangential ones are zero-gradient.
        return {.pressure = P::ZeroGradient, .velocity = P::Symmetry, .massFlux = P::FixedValue};
    }
    // An enumerator this build does not define. The walls' treatment is the
    // most constraining, so an unknown kind cannot become an open boundary by
    // accident; it is unreachable, and `toString` has the same fallback.
    return {.pressure = P::ZeroGradient, .velocity = P::FixedValue, .massFlux = P::FixedValue};
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

namespace {

/// Six times the signed volume of the tetrahedron a b c d, in cubic metres.
/// `(b - a) . ((c - a) x (d - a))`, which is the determinant form.
[[nodiscard]] double sixSignedVolume(const Point3D& a, const Point3D& b, const Point3D& c,
                                     const Point3D& d) noexcept {
    const double ux = (b.x - a.x).si();
    const double uy = (b.y - a.y).si();
    const double uz = (b.z - a.z).si();
    const double vx = (c.x - a.x).si();
    const double vy = (c.y - a.y).si();
    const double vz = (c.z - a.z).si();
    const double wx = (d.x - a.x).si();
    const double wy = (d.y - a.y).si();
    const double wz = (d.z - a.z).si();
    return ux * (vy * wz - vz * wy) - uy * (vx * wz - vz * wx) + uz * (vx * wy - vy * wx);
}

/// The four vertices of @p cell, or nothing if the mesh does not hold them.
[[nodiscard]] std::optional<std::array<Point3D, 4>>
cellVertices(const CfdTopology& topology, const meshing::Mesh& mesh, CellIndex cell) {
    const meshing::ElementId element = topology.cellOf(cell);
    if (!element.isValid()) {
        return std::nullopt;
    }
    const meshing::Tetrahedron* tet = mesh.findTetrahedron(element);
    if (tet == nullptr) {
        return std::nullopt;
    }
    std::array<Point3D, 4> p{};
    for (std::size_t k = 0; k < 4; ++k) {
        const meshing::Node* node = mesh.findNode(tet->nodes[k]);
        if (node == nullptr) {
            return std::nullopt;
        }
        p[k] = node->position;
    }
    return p;
}

} // namespace

std::optional<CellIndex> locatePoint(const CfdTopology& topology, const meshing::Mesh& mesh,
                                     const Point3D& point) {
    // THE LOWEST CellIndex THAT CONTAINS IT, so a point on a shared face, edge
    // or vertex resolves to one cell and the same cell in every build. The scan
    // runs in CellIndex order and returns at the first hit, which IS that rule.
    for (std::size_t c = 0; c < topology.cells(); ++c) {
        const CellIndex cell = CellIndex::fromValue(c);
        const std::optional<std::array<Point3D, 4>> v = cellVertices(topology, mesh, cell);
        if (!v.has_value()) {
            continue;
        }
        const double total = sixSignedVolume((*v)[0], (*v)[1], (*v)[2], (*v)[3]);
        if (!std::isfinite(total) || total == 0.0) {
            continue;
        }
        // Each sub-volume replaces one vertex by the point. All four share the
        // sign of the whole when the point is inside. The tolerance is
        // PROPORTIONAL to the cell's own volume: a cell's size is set by the
        // mesh, so an absolute epsilon would be generous in a coarse mesh and
        // meaningless in a fine one.
        const double sign = total > 0.0 ? 1.0 : -1.0;
        const double tolerance = 1e-9 * std::abs(total);
        const std::array<double, 4> sub{
            sixSignedVolume(point, (*v)[1], (*v)[2], (*v)[3]),
            sixSignedVolume((*v)[0], point, (*v)[2], (*v)[3]),
            sixSignedVolume((*v)[0], (*v)[1], point, (*v)[3]),
            sixSignedVolume((*v)[0], (*v)[1], (*v)[2], point),
        };
        bool inside = true;
        for (const double s : sub) {
            if (!std::isfinite(s) || sign * s < -tolerance) {
                inside = false;
                break;
            }
        }
        if (inside) {
            return cell;
        }
    }
    return std::nullopt;
}

std::optional<Length> heightAlongGravity(const Point3D& point, const Acceleration3& gravity) {
    const double gx = gravity.x.si();
    const double gy = gravity.y.si();
    const double gz = gravity.z.si();
    const double magnitude = std::hypot(gx, gy, gz);
    if (!std::isfinite(magnitude) || magnitude == 0.0) {
        return std::nullopt;
    }
    // h = -(x . g_hat): it increases UPWARD, whatever direction gravity points.
    const double h = -(point.x.si() * gx + point.y.si() * gy + point.z.si() * gz) / magnitude;
    if (!std::isfinite(h)) {
        return std::nullopt;
    }
    return Length::fromSi(h);
}

std::optional<Pressure> hydrostaticPressure(Pressure prescribed, const Point3D& centroid,
                                            const std::optional<CfdGravity>& gravity,
                                            const std::optional<Density>& density) {
    if (!gravity.has_value()) {
        // WITHOUT GRAVITY THERE IS NO HEIGHT and a boundary pressure is
        // uniform (CfdData.hpp:370). The density is not consulted, which is
        // why the source binding carries it only when gravity is present.
        return prescribed;
    }
    if (!density.has_value()) {
        return std::nullopt;
    }
    const std::optional<Length> h = heightAlongGravity(centroid, gravity->acceleration);
    if (!h.has_value()) {
        return std::nullopt;
    }
    const double g = gravity->acceleration.magnitude().si();
    const double drop = density->si() * g * (h->si() - gravity->referenceHeight.si());
    const double value = prescribed.si() - drop;
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    return Pressure::fromSi(value);
}

// ---------------------------------------------------------------------------
// The accessors
// ---------------------------------------------------------------------------

std::optional<CfdBoundaryFace> PreparedCfdBoundary::face(BoundaryFaceIndex boundaryFace) const
    noexcept {
    if (!boundaryFace.isValid() || boundaryFace.value() >= faces_.size()) {
        return std::nullopt;
    }
    return faces_[boundaryFace.value()];
}

std::optional<CfdBoundaryTreatment>
PreparedCfdBoundary::treatment(BoundaryFaceIndex boundaryFace) const noexcept {
    const std::optional<CfdBoundaryFace> f = face(boundaryFace);
    if (!f.has_value()) {
        return std::nullopt;
    }
    return treatmentOf(f->kind);
}

// ---------------------------------------------------------------------------
// The one assembler
// ---------------------------------------------------------------------------

/// The only thing permitted to build a `PreparedCfdBoundary`.
struct CfdBoundaryAssembler {
    [[nodiscard]] static PreparedCfdBoundary
    assemble(CfdBoundarySource source, std::vector<CfdBoundaryFace> faces,
             std::vector<CfdBoundaryCardinality> cardinality,
             std::optional<CellIndex> referenceCell) {
        PreparedCfdBoundary prepared;
        prepared.source_ = std::move(source);
        prepared.faces_ = std::move(faces);
        prepared.cardinality_ = std::move(cardinality);
        prepared.referenceCell_ = referenceCell;
        return prepared;
    }
};

namespace {

/// The tolerance on a wall velocity's normal component, as a dimensionless
/// cosine (ADR-067 Decision 5).
///
/// NOT 1e-12. The dot product itself is well-conditioned double-precision
/// algebra, but `n_hat` is the unit normal of ADR-065's area vector of a MESHED
/// facet, and a curved wall's facets approximate the CAD surface -- so the
/// normal carries the mesh's geometric approximation and not only round-off.
/// `1e-9` is this repository's geometric-accumulation class, used here for that
/// reason and not tuned to make a test pass: a flat wall is exact to round-off
/// and passes with orders of magnitude to spare, and a genuinely oblique
/// velocity fails by a wide margin.
constexpr double kTangentialCosine = 1e-9;

/// How far a moving wall's facet normals may differ from the first one's, as
/// `1 - n_f . n_0` (ADR-067 Decision 5, as the review amended it).
///
/// A PLANAR CAD FACE'S FACETS ALL LIE IN ITS PLANE, so their unit normals are
/// identical to round-off and this quantity is of the order of 1e-16. A meshed
/// curved face is nothing like that: on a tet-meshed cylinder with a
/// tessellation angle of 0.3 rad, `1 - n_f . n_0` is about 0.045 -- more than
/// seven orders of magnitude away. The threshold sits between the two with room
/// on both sides, and the suite measures both margins rather than asserting
/// this sentence.
constexpr double kPlanarCosine = 1e-9;

/// Prefixes @p inner with the condition's identity, so a seam-level message
/// becomes attributable without the seam knowing about identities.
[[nodiscard]] Error named(CfdBoundaryConditionId condition, const Error& inner) {
    return Error{inner.code, std::format("{}: {}", condition, inner.message)};
}

[[nodiscard]] CfdBoundaryFailure fail(CfdBoundaryProblem problem, ErrorCode code,
                                      std::string message) {
    return CfdBoundaryFailure{.problem = problem, .error = Error{code, std::move(message)}};
}

/// Every condition on its own terms, and the identities unique.
///
/// ONE PLACE, called by the staged gate BEFORE any target is resolved and by
/// the seam at the same point in its own order, so the two cannot disagree
/// about which of two faults is reported. The order matters: an adversarial
/// pass found the gate resolving targets first, so a definition with both a
/// malformed condition and an unresolvable target reported the target while the
/// header claimed otherwise.
[[nodiscard]] std::optional<CfdBoundaryFailure>
checkConditions(std::span<const CfdBoundaryCondition> conditions) {
    for (std::size_t i = 0; i < conditions.size(); ++i) {
        const CfdBoundaryCondition& condition = conditions[i];
        // P19-DATA-001'S OWN RULE, which already applies core's
        // validate(FaceSelector) -- the same check boundaryFacetsOf applies.
        // That is why there is no TargetInvalid: a malformed selector is
        // refused here, with the message core wrote.
        if (const Result<void> valid = validate(condition); !valid.has_value()) {
            return CfdBoundaryFailure{.problem = CfdBoundaryProblem::ConditionInvalid,
                                      .error = named(condition.id(), valid.error())};
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (conditions[j].id() == condition.id()) {
                return fail(CfdBoundaryProblem::DuplicateConditionId, ErrorCode::InvalidArgument,
                            std::format("{} appears twice among the boundary conditions; refused "
                                        "rather than merged, because one of them would be "
                                        "silently ignored",
                                        condition.id()));
            }
        }
    }
    return std::nullopt;
}

/// One pass, shared by `bindCfdBoundary`, `cfdBoundaryFailure` and
/// `cfdBoundaryProblem`, so the three cannot drift apart: the same checks in
/// the same order.
struct Bound {
    std::optional<CfdBoundaryFailure> failure{};
    std::vector<CfdBoundaryFace> faces{};
    std::vector<CfdBoundaryCardinality> cardinality{};
    std::optional<CellIndex> referenceCell{};
};

[[nodiscard]] Bound bindTargets(const CfdTopology& topology, const CfdGeometry& geometry,
                         std::span<const CfdResolvedTarget> targets, std::size_t unnamedPatches,
                         const std::optional<CfdReferencePressure>& referencePressure,
                         const std::optional<CfdGravity>& gravity,
                         const std::optional<Density>& density, const meshing::Mesh& mesh) {
    Bound out;

    // --- the binding between the two derived values ------------------------
    if (!geometry.describes(topology)) {
        out.failure = fail(CfdBoundaryProblem::GeometryOfAnotherTopology,
                           ErrorCode::FailedPrecondition,
                           "the finite-volume geometry was not computed from this topology, so "
                           "its normals and centroids are not about these faces");
        return out;
    }

    // --- the mesh's own naming --------------------------------------------
    if (unnamedPatches != 0) {
        out.failure = fail(
            CfdBoundaryProblem::UnnamedPatch, ErrorCode::FailedPrecondition,
            std::format("{} patch(es) of the fluid body carry no face name, so no geometry "
                        "reference can reach them and no boundary condition can cover them; "
                        "every boundary facet needs exactly one condition",
                        unnamedPatches));
        return out;
    }

    // --- every condition on its own terms, before any geometry is read ----
    //
    // The staged gate has already run these, so through it this is a no-op; it
    // is here for a seam caller, whose targets have not been through the gate.
    std::vector<CfdBoundaryCondition> conditions;
    conditions.reserve(targets.size());
    for (const CfdResolvedTarget& target : targets) {
        conditions.push_back(target.condition);
    }
    if (std::optional<CfdBoundaryFailure> bad = checkConditions(conditions); bad.has_value()) {
        out.failure = std::move(bad);
        return out;
    }

    // --- resolution: a target that covers nothing is not an empty target ---
    for (const CfdResolvedTarget& target : targets) {
        if (target.facets.empty()) {
            out.failure =
                CfdBoundaryFailure{.problem = CfdBoundaryProblem::TargetWithoutFacets,
                                   .error = named(target.condition.id(),
                                                  Error{ErrorCode::FailedPrecondition,
                                                        "its target face resolved but the mesh "
                                                        "attributed no boundary facet to it, so "
                                                        "there is nothing to act on"})};
            return out;
        }
    }

    // --- the datum records on their own terms -------------------------------
    //
    // P19-DATA-001'S OWN RULES. Checked here because this gate READS them: a
    // review found a zero gravity vector -- which `validate(CfdGravity)`
    // refuses -- reaching the hydrostatic arithmetic and being reported as a
    // non-finite product, which named the wrong cause.
    if (referencePressure.has_value()) {
        if (const Result<void> valid = validate(*referencePressure); !valid.has_value()) {
            out.failure = CfdBoundaryFailure{.problem = CfdBoundaryProblem::DatumInvalid,
                                             .error = valid.error()};
            return out;
        }
    }
    if (gravity.has_value()) {
        if (const Result<void> valid = validate(*gravity); !valid.has_value()) {
            out.failure = CfdBoundaryFailure{.problem = CfdBoundaryProblem::DatumInvalid,
                                             .error = valid.error()};
            return out;
        }
    }

    // --- a target's own facet list ------------------------------------------
    //
    // STRICTLY ASCENDING: no repeat and no reordering. P16 delivers its sets
    // this way, and a list that is not would count a facet twice -- and would
    // then be reported as two conditions claiming it, which names the wrong
    // cause. A review found that misreport ("claimed by both 3 and 3").
    for (const CfdResolvedTarget& target : targets) {
        for (std::size_t k = 1; k < target.facets.size(); ++k) {
            if (!(target.facets[k - 1] < target.facets[k])) {
                out.failure = CfdBoundaryFailure{
                    .problem = CfdBoundaryProblem::TargetFacetsNotUnique,
                    .error = named(target.condition.id(),
                                   Error{ErrorCode::InvalidArgument,
                                         std::format("its resolved facets are not strictly "
                                                     "ascending at position {}: facet {} does not "
                                                     "precede facet {}",
                                                     k, target.facets[k - 1], target.facets[k])})};
                return out;
            }
        }
    }

    // --- coverage: exactly one condition per boundary face -----------------
    const std::size_t boundaryFaces = topology.boundaryFaces();
    constexpr std::size_t kUnclaimed = static_cast<std::size_t>(-1);
    std::vector<std::size_t> claimant(boundaryFaces, kUnclaimed);
    out.cardinality.reserve(targets.size());

    for (std::size_t i = 0; i < targets.size(); ++i) {
        const CfdResolvedTarget& target = targets[i];
        for (const meshing::ElementId facet : target.facets) {
            const std::optional<BoundaryFaceIndex> boundaryFace =
                topology.boundaryFaceOfTriangle(facet);
            if (!boundaryFace.has_value()) {
                out.failure = CfdBoundaryFailure{
                    .problem = CfdBoundaryProblem::FacetNotInTopology,
                    .error = named(target.condition.id(),
                                   Error{ErrorCode::FailedPrecondition,
                                         std::format("facet {} is not a boundary face of this "
                                                     "topology",
                                                     facet)})};
                return out;
            }
            std::size_t& owner = claimant[boundaryFace->value()];
            if (owner != kUnclaimed) {
                out.failure = fail(
                    CfdBoundaryProblem::FacetClaimedTwice, ErrorCode::InvalidArgument,
                    std::format("boundary facet {} is claimed by both {} and {}; no two boundary "
                                "conditions superpose, so neither is chosen over the other",
                                facet, targets[owner].condition.id(), target.condition.id()));
                return out;
            }
            owner = i;
        }
        out.cardinality.push_back(CfdBoundaryCardinality{.condition = target.condition.id(),
                                                         .expected = target.facets.size(),
                                                         .assigned = 0});
    }

    // TOTAL RATHER THAN GUARDED FROM A DISTANCE. The bound is `targets.size()`
    // and not `kUnclaimed`, so an out-of-range claimant is refused by the same
    // comparison that refuses an unclaimed one, and the increment below cannot
    // be out of bounds. Mutation analysis found the earlier shape: it compared
    // against `kUnclaimed` alone, and the loop that reads `targets[claimant[b]]`
    // further down was then relying on this loop having returned -- correct, but
    // undefined behaviour rather than a diagnostic if it ever stopped being so.
    for (std::size_t b = 0; b < boundaryFaces; ++b) {
        const std::size_t owner = claimant[b];
        if (owner >= targets.size()) {
            out.failure = fail(
                CfdBoundaryProblem::FacetWithoutCondition, ErrorCode::FailedPrecondition,
                std::format("boundary facet {} carries no condition, so a flux would leave the "
                            "domain through a face nothing prescribes",
                            topology.triangleOf(BoundaryFaceIndex::fromValue(b))));
            return out;
        }
        ++out.cardinality[owner].assigned;
    }

    // --- the density a hydrostatic prescribed pressure needs ---------------
    //
    // Refused BY NAME, because "the hydrostatic product is not finite" names
    // the wrong cause for an argument that was not supplied.
    if (gravity.has_value() && !density.has_value() &&
        std::ranges::any_of(targets, [](const CfdResolvedTarget& target) {
            return target.condition.fixesPressure();
        })) {
        out.failure = fail(CfdBoundaryProblem::HydrostaticDensityMissing,
                           ErrorCode::FailedPrecondition,
                           "gravity is present and a boundary fixes the pressure, so a prescribed "
                           "pressure varies hydrostatically and needs the fluid's density, and "
                           "none was supplied");
        return out;
    }

    // --- a moving wall's target, as a whole --------------------------------
    //
    // PER TARGET AND NOT PER FACET, which is the review's correction. Tangency
    // is a statement about a SURFACE, and a facet normal on a meshed curved
    // surface carries the tessellation angle: at any tolerance tight enough to
    // catch a real normal component, a per-facet test refuses the one constant
    // velocity that is tangential to a cylinder. So the target must be planar,
    // and tangency is then decided against its one normal.
    for (const CfdResolvedTarget& target : targets) {
        const MovingWall* wall = target.condition.movingWall();
        if (wall == nullptr) {
            continue;
        }
        std::optional<Direction3D> common{};
        for (const meshing::ElementId facet : target.facets) {
            const std::optional<BoundaryFaceIndex> boundaryFace =
                topology.boundaryFaceOfTriangle(facet);
            if (!boundaryFace.has_value()) {
                continue; // already refused above; the loop is total anyway
            }
            const std::optional<Direction3D> normal =
                geometry.boundaryOutwardNormal(*boundaryFace);
            if (!normal.has_value()) {
                out.failure = CfdBoundaryFailure{
                    .problem = CfdBoundaryProblem::DegenerateFacet,
                    .error = named(target.condition.id(),
                                   Error{ErrorCode::FailedPrecondition,
                                         std::format("boundary facet {} has no usable outward "
                                                     "normal, so a moving wall cannot be placed "
                                                     "on it",
                                                     facet)})};
                return out;
            }
            if (!common.has_value()) {
                common = normal;
                continue;
            }
            const double deviation = 1.0 - common->dot(*normal);
            if (!(deviation <= kPlanarCosine)) {
                out.failure = CfdBoundaryFailure{
                    .problem = CfdBoundaryProblem::WallTargetNotPlanar,
                    .error = named(
                        target.condition.id(),
                        Error{ErrorCode::FailedPrecondition,
                              std::format("its target is not planar: facet {} deviates from the "
                                          "first facet's normal by {:.3e}, and tangency has no "
                                          "meaning on a curved target, so it is refused rather "
                                          "than approximated",
                                          facet, deviation)})};
                return out;
            }
        }
        if (!common.has_value()) {
            continue; // an empty target is already refused above
        }
        const double wx = wall->velocity.x.si();
        const double wy = wall->velocity.y.si();
        const double wz = wall->velocity.z.si();
        const double speed = std::hypot(wx, wy, wz);
        // `validate(condition)` already refused a zero or non-finite wall
        // velocity, so `speed > 0` here; the ratio is formed without a fallback
        // so a diagnostic cannot carry an invented number.
        const double along = wx * common->x() + wy * common->y() + wz * common->z();
        const double ratio = std::abs(along) / speed;
        if (!(ratio <= kTangentialCosine)) {
            out.failure = CfdBoundaryFailure{
                .problem = CfdBoundaryProblem::WallVelocityNotTangential,
                .error = named(target.condition.id(),
                               Error{ErrorCode::InvalidArgument,
                                     std::format("its wall velocity is not tangential to its "
                                                 "target: the normal component is {:.3e} of the "
                                                 "speed, and the wall does not move",
                                                 ratio)})};
            return out;
        }
    }

    // --- the prescribed values, which is where geometry is first read -----
    out.faces.resize(boundaryFaces);
    for (std::size_t b = 0; b < boundaryFaces; ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const std::size_t owner = claimant[b];
        if (owner >= targets.size()) {
            // Unreachable: the coverage loop above returned for every facet no
            // target claims. Checked anyway, so this is a diagnostic and not
            // undefined behaviour.
            out.failure = fail(
                CfdBoundaryProblem::FacetWithoutCondition, ErrorCode::FailedPrecondition,
                std::format("boundary facet {} carries no condition",
                            topology.triangleOf(boundaryFace)));
            return out;
        }
        const CfdResolvedTarget& target = targets[owner];
        const CfdBoundaryCondition& condition = target.condition;

        const std::optional<Direction3D> normal = geometry.boundaryOutwardNormal(boundaryFace);
        if (!normal.has_value()) {
            out.failure = CfdBoundaryFailure{
                .problem = CfdBoundaryProblem::DegenerateFacet,
                .error = named(condition.id(),
                               Error{ErrorCode::FailedPrecondition,
                                     std::format("boundary facet {} has no usable outward normal, "
                                                 "so it cannot carry a boundary value",
                                                 topology.triangleOf(boundaryFace))})};
            return out;
        }

        CfdBoundaryFace record{.condition = condition.id(), .kind = condition.kind()};
        const CfdBoundaryTreatment treatment = treatmentOf(record.kind);

        if (treatment.velocity == CfdBoundaryPolicy::FixedValue) {
            if (const VelocityInlet* inlet = condition.velocityInlet(); inlet != nullptr) {
                // POSITIVE IS INTO THE DOMAIN, and the outward normal points
                // out of it, so the prescribed velocity is `-v n_hat`. The
                // direction is CONSTRUCTED from the normal rather than supplied,
                // so the inflow sense is true by construction and there is
                // nothing to refute per facet beyond the normal being usable.
                const double v = inlet->normalSpeed.si();
                record.velocity = Velocity3{.x = Velocity::fromSi(-v * normal->x()),
                                            .y = Velocity::fromSi(-v * normal->y()),
                                            .z = Velocity::fromSi(-v * normal->z())};
            } else if (const MovingWall* wall = condition.movingWall(); wall != nullptr) {
                // Tangency and planarity were decided for the whole target
                // above, so this is the wall's own velocity, unchanged.
                record.velocity = wall->velocity;
            } else {
                // A no-slip wall: at rest, exactly.
                record.velocity = Velocity3{};
            }
        }

        if (treatment.pressure == CfdBoundaryPolicy::FixedValue) {
            const std::optional<Point3D> centroid =
                geometry.faceCentroid(topology.faceOf(boundaryFace));
            if (!centroid.has_value()) {
                out.failure = CfdBoundaryFailure{
                    .problem = CfdBoundaryProblem::DegenerateFacet,
                    .error = named(condition.id(),
                                   Error{ErrorCode::FailedPrecondition,
                                         std::format("boundary facet {} has no centroid, so a "
                                                     "hydrostatic boundary pressure cannot be "
                                                     "placed on it",
                                                     topology.triangleOf(boundaryFace))})};
                return out;
            }
            const Pressure prescribed = condition.pressureInlet() != nullptr
                                            ? condition.pressureInlet()->staticPressure
                                            : condition.pressureOutlet()->staticPressure;
            const std::optional<Pressure> value =
                hydrostaticPressure(prescribed, *centroid, gravity, density);
            if (!value.has_value()) {
                out.failure = CfdBoundaryFailure{
                    .problem = CfdBoundaryProblem::NonFiniteBoundaryValue,
                    .error = named(condition.id(),
                                   Error{ErrorCode::FailedPrecondition,
                                         std::format("the hydrostatic boundary pressure at facet "
                                                     "{} is not a finite value",
                                                     topology.triangleOf(boundaryFace))})};
                return out;
            }
            record.pressure = *value;
        }

        out.faces[b] = record;
    }

    // --- the pressure datum ------------------------------------------------
    //
    // ONE IMPLEMENTATION OF ADR-058's RULE IN THIS FILE, asked here because
    // CfdAnalysisObject.hpp:100 says the gate that refuses unassigned facets is
    // the one that refuses a missing datum. The rule is `pressureDatumProblem`'s
    // Missing arm: a reference pressure is required if and only if no boundary
    // condition fixes the pressure. A test asserts the two agree on a
    // definition, so this is a second ASKING and not a second rule.
    const bool boundaryFixesPressure =
        std::ranges::any_of(targets, [](const CfdResolvedTarget& target) {
            return target.condition.fixesPressure();
        });
    // BOTH ARMS, which is `pressureDatumProblem`'s rule and not half of it. A
    // review found the gate accepting an Overdetermined definition -- an outlet
    // AND a reference pressure -- and locating the point anyway, so a consumer
    // applying both datums would have double-fixed the level.
    if (!boundaryFixesPressure && !referencePressure.has_value()) {
        out.failure = fail(CfdBoundaryProblem::PressureDatumMissing, ErrorCode::FailedPrecondition,
                           "no boundary condition fixes the pressure and there is no reference "
                           "pressure, so the pressure is determined only up to a constant");
        return out;
    }
    if (boundaryFixesPressure && referencePressure.has_value()) {
        out.failure = fail(CfdBoundaryProblem::PressureDatumOverdetermined,
                           ErrorCode::InvalidArgument,
                           "a reference pressure is given although a pressure inlet or outlet "
                           "already fixes the pressure; the datum would be overdetermined");
        return out;
    }
    if (referencePressure.has_value()) {
        const std::optional<CellIndex> cell =
            locatePoint(topology, mesh, referencePressure->location);
        if (!cell.has_value()) {
            out.failure = fail(
                CfdBoundaryProblem::ReferencePointOutsideFluid, ErrorCode::FailedPrecondition,
                std::format("the reference pressure's point ({} m, {} m, {} m) lies in no cell of "
                            "the fluid, so it cannot fix the pressure level",
                            referencePressure->location.x.si(), referencePressure->location.y.si(),
                            referencePressure->location.z.si()));
            return out;
        }
        out.referenceCell = cell;
    }

    return out;
}

/// The source a prepared boundary gets, built in one place so the gate and
/// `currentBoundarySource` cannot disagree.
[[nodiscard]] CfdBoundarySource sourceOf(const CfdModel& model, const CfdTopology& topology,
                                         const CfdAnalysisDefinition& definition) {
    const bool fixesPressure =
        std::ranges::any_of(definition.boundaryConditions, [](const CfdBoundaryCondition& c) {
            return c.fixesPressure();
        });
    CfdBoundarySource source{
        .body = model.body(),
        .mesh = model.mesh().mesh().stamp(),
        .cells = topology.cells(),
        .boundaryFaces = topology.boundaryFaces(),
        .geometry = model.geometryRevision(),
        .conditions = definition.boundaryConditions,
        .referencePressure = definition.referencePressure,
        .gravity = definition.gravity,
        .density = std::nullopt,
    };
    // PRESENT EXACTLY WHEN IT ENTERED A VALUE. Without gravity a prescribed
    // pressure is uniform and rho never appears, so carrying it would stale the
    // boundary on a material edit that provably cannot change anything here.
    if (definition.gravity.has_value() && fixesPressure) {
        source.density = model.density();
    }
    return source;
}

} // namespace

// ---------------------------------------------------------------------------
// Building one
// ---------------------------------------------------------------------------

Result<PreparedCfdBoundary>
bindCfdBoundary(const CfdTopology& topology, const CfdGeometry& geometry,
                const meshing::Mesh& mesh, std::span<const CfdResolvedTarget> targets,
                std::size_t unnamedPatches,
                const std::optional<CfdReferencePressure>& referencePressure,
                const std::optional<CfdGravity>& gravity, const std::optional<Density>& density) {
    Bound bound = bindTargets(topology, geometry, targets, unnamedPatches, referencePressure,
                              gravity, density, mesh);
    if (bound.failure.has_value()) {
        return std::unexpected(std::move(bound.failure->error));
    }
    return CfdBoundaryAssembler::assemble(CfdBoundarySource{}, std::move(bound.faces),
                                          std::move(bound.cardinality), bound.referenceCell);
}

std::optional<CfdBoundaryFailure>
bindCfdBoundaryFailure(const CfdTopology& topology, const CfdGeometry& geometry,
                       const meshing::Mesh& mesh, std::span<const CfdResolvedTarget> targets,
                       std::size_t unnamedPatches,
                       const std::optional<CfdReferencePressure>& referencePressure,
                       const std::optional<CfdGravity>& gravity,
                       const std::optional<Density>& density) {
    return bindTargets(topology, geometry, targets, unnamedPatches, referencePressure, gravity,
                       density, mesh)
        .failure;
}

namespace {

/// The staged gate's one pass: the two bindings it alone can check, P16's
/// resolution, then the shared `bind`.
struct Staged {
    std::optional<CfdBoundaryFailure> failure{};
    Bound bound{};
    CfdBoundarySource source{};
};

[[nodiscard]] Staged stage(const CfdModel& model, const CfdTopology& topology,
                           const CfdGeometry& geometry, const CfdAnalysisDefinition& definition) {
    Staged out;
    const meshing::Mesh& mesh = model.mesh().mesh();

    // STAMPS, NEVER COUNTS. A contract for one generation says nothing about
    // the next, however equal their counts.
    if (!topology.describes(mesh)) {
        out.failure = fail(CfdBoundaryProblem::TopologyOfAnotherMesh, ErrorCode::FailedPrecondition,
                           std::format("the finite-volume topology was not derived from the mesh "
                                       "meshing control {} now holds, so its boundary faces are "
                                       "not these faces",
                                       model.control()));
        return out;
    }

    // THE GEOMETRY'S BINDING BEFORE ANYTHING IS READ OFF IT, so the phase order
    // the header declares is the order that actually runs.
    if (!geometry.describes(topology)) {
        out.failure = fail(CfdBoundaryProblem::GeometryOfAnotherTopology,
                           ErrorCode::FailedPrecondition,
                           "the finite-volume geometry was not computed from this topology, so "
                           "its normals and centroids are not about these faces");
        return out;
    }
    if (const std::size_t unnamed = model.map().report().unnamedFaceCount; unnamed != 0) {
        out.failure = fail(
            CfdBoundaryProblem::UnnamedPatch, ErrorCode::FailedPrecondition,
            std::format("{} patch(es) of the fluid body carry no face name, so no geometry "
                        "reference can reach them and no boundary condition can cover them; "
                        "every boundary facet needs exactly one condition",
                        unnamed));
        return out;
    }
    // EVERY CONDITION BEFORE ANY TARGET IS RESOLVED (thermal's order).
    if (std::optional<CfdBoundaryFailure> bad = checkConditions(definition.boundaryConditions);
        bad.has_value()) {
        out.failure = std::move(bad);
        return out;
    }

    // Resolution through P16 alone, in the definition's canonical order. ONE
    // resolution per condition: `faceTargetProblem` is asked only when the
    // resolution has already failed, which is where its classification is
    // wanted, so the success path does not resolve twice and no `.error()` is
    // read off a Result that has not been checked.
    std::vector<CfdResolvedTarget> targets;
    targets.reserve(definition.boundaryConditions.size());
    for (const CfdBoundaryCondition& condition : definition.boundaryConditions) {
        Result<std::vector<meshing::ElementId>> facets =
            resolveFaceTarget(model.map(), condition.face());
        if (!facets.has_value()) {
            // Classify with P16's own rule, so the two cannot drift apart. A
            // malformed selector cannot arrive here -- `checkConditions` above
            // refused it -- so `WithoutFacets` is the remaining alternative to
            // `Unresolved` and the mapping is total.
            const std::optional<TargetProblem> problem =
                faceTargetProblem(model.map(), condition.face());
            const CfdBoundaryProblem mapped = problem == TargetProblem::Unresolved
                                                  ? CfdBoundaryProblem::TargetUnresolved
                                                  : CfdBoundaryProblem::TargetWithoutFacets;
            out.failure = CfdBoundaryFailure{.problem = mapped,
                                             .error = named(condition.id(), facets.error())};
            return out;
        }
        targets.push_back(CfdResolvedTarget{.condition = condition, .facets = std::move(*facets)});
    }

    out.source = sourceOf(model, topology, definition);
    out.bound = bindTargets(topology, geometry, targets, model.map().report().unnamedFaceCount,
                            definition.referencePressure, definition.gravity, out.source.density,
                            mesh);
    out.failure = out.bound.failure;
    if (out.failure.has_value()) {
        return out;
    }

    // THE REVERSE READING, which only this path can take: of the boundary faces
    // each condition was given, how many sit on a CAD face whose names include
    // that condition's own FaceName. `sourceFaceOf` is the other direction of
    // P16's map, so this is an independent count and not a restatement of the
    // forward one -- the distinction a review showed the first two counts could
    // not make.
    for (CfdBoundaryCardinality& record : out.bound.cardinality) {
        const CfdBoundaryCondition* condition = nullptr;
        for (const CfdBoundaryCondition& candidate : definition.boundaryConditions) {
            if (candidate.id() == record.condition) {
                condition = &candidate;
                break;
            }
        }
        if (condition == nullptr) {
            continue;
        }
        std::size_t attributed = 0;
        for (std::size_t b = 0; b < out.bound.faces.size(); ++b) {
            if (out.bound.faces[b].condition != record.condition) {
                continue;
            }
            const meshing::ElementId facet =
                topology.triangleOf(BoundaryFaceIndex::fromValue(b));
            const Result<meshing::FacetSource> source = meshing::sourceFaceOf(model.map(), facet);
            if (!source.has_value()) {
                continue;
            }
            if (std::ranges::find(source->names, condition->face()) != source->names.end()) {
                ++attributed;
            }
        }
        record.attributed = attributed;
    }
    return out;
}

} // namespace

Result<PreparedCfdBoundary> prepareCfdBoundary(const CfdModel& model, const CfdTopology& topology,
                                               const CfdGeometry& geometry,
                                               const CfdAnalysisDefinition& definition) {
    Staged staged = stage(model, topology, geometry, definition);
    if (staged.failure.has_value()) {
        return std::unexpected(std::move(staged.failure->error));
    }
    return CfdBoundaryAssembler::assemble(std::move(staged.source), std::move(staged.bound.faces),
                                          std::move(staged.bound.cardinality),
                                          staged.bound.referenceCell);
}

std::optional<CfdBoundaryFailure> cfdBoundaryFailure(const CfdModel& model,
                                                     const CfdTopology& topology,
                                                     const CfdGeometry& geometry,
                                                     const CfdAnalysisDefinition& definition) {
    return stage(model, topology, geometry, definition).failure;
}

std::optional<CfdBoundaryProblem> cfdBoundaryProblem(const CfdModel& model,
                                                     const CfdTopology& topology,
                                                     const CfdGeometry& geometry,
                                                     const CfdAnalysisDefinition& definition) {
    const std::optional<CfdBoundaryFailure> failure =
        cfdBoundaryFailure(model, topology, geometry, definition);
    if (!failure.has_value()) {
        return std::nullopt;
    }
    return failure->problem;
}

// ---------------------------------------------------------------------------
// Currentness
// ---------------------------------------------------------------------------

CfdBoundarySource currentBoundarySource(const CfdModel& model, const CfdTopology& topology,
                                        const CfdAnalysisDefinition& definition) {
    return sourceOf(model, topology, definition);
}

std::vector<CfdBoundaryStaleReason> staleReasons(const PreparedCfdBoundary& boundary,
                                                 const CfdBoundarySource& source) {
    const CfdBoundarySource& held = boundary.source();
    std::vector<CfdBoundaryStaleReason> reasons;
    if (held.body != source.body) {
        reasons.push_back(CfdBoundaryStaleReason::Body);
    }
    // The stamp AND the counts: a stamp identifies the BUILDER, not the
    // snapshot, which is the lesson MeshDofMap recorded first.
    if (!(held.mesh == source.mesh) || held.cells != source.cells ||
        held.boundaryFaces != source.boundaryFaces) {
        reasons.push_back(CfdBoundaryStaleReason::Mesh);
    }
    if (!(held.geometry == source.geometry)) {
        reasons.push_back(CfdBoundaryStaleReason::Geometry);
    }
    if (held.conditions != source.conditions) {
        reasons.push_back(CfdBoundaryStaleReason::Conditions);
    }
    if (held.referencePressure != source.referencePressure || held.gravity != source.gravity) {
        reasons.push_back(CfdBoundaryStaleReason::Datum);
    }
    if (held.density != source.density) {
        reasons.push_back(CfdBoundaryStaleReason::Fluid);
    }
    return reasons;
}

bool isCurrent(const PreparedCfdBoundary& boundary, const CfdBoundarySource& source) {
    return staleReasons(boundary, source).empty();
}

} // namespace bettercad::cfd
