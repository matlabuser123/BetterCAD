#include <bettercad/cfd/CfdDiffusion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

// THE ASSEMBLY IS TWO PASSES OVER FACES, AND BOTH VISIT EACH FACE ONCE.
//
//   SYMBOLIC   walk the faces and record, per row, which columns that row will
//              ever receive: its own diagonal, and one neighbour per internal
//              face. Sort and unique each row, then lay out the CSR. A triplet
//              staging would have cost four records per internal face and a
//              reduction whose summation order is not specified
//   NUMERIC    walk them again and accumulate `values[slot] += v`. There is
//              exactly ONE slot per (row, column), so a repeated contribution
//              is a sum by construction
//
// A `slotOf` miss is NOT written past: it is an internal inconsistency, because
// the symbolic pass reserved every pair the numeric pass can ask for, and it is
// reported rather than ignored.
//
// THE FACE ORDER IS `CfdTopology`'S, ascending `FaceIndex`, and the summation
// order within a row is ascending column. Both are numerical contracts: changing
// either changes the last bits of a sum. Nothing here is an unordered container.

namespace bettercad::cfd {
namespace {

/// A plain SI triple, for the arithmetic inside the loop. The typed accessors
/// are read at the boundary and the answers are put back into typed values; in
/// between, one unit.
struct Vec {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] constexpr double dotOf(const Vec& a, const Vec& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] double normOf(const Vec& v) noexcept { return std::hypot(v.x, v.y, v.z); }

[[nodiscard]] Vec siOf(const AreaVector3D& s) noexcept { return {s.x.si(), s.y.si(), s.z.si()}; }
[[nodiscard]] Vec siOf(const Translation3D& t) noexcept { return {t.x.si(), t.y.si(), t.z.si()}; }

[[nodiscard]] AreaVector3D areaVectorOf(const Vec& v) noexcept {
    return {Area::fromSi(v.x), Area::fromSi(v.y), Area::fromSi(v.z)};
}

/// The over-relaxed arms, in SI.
///
/// ONE COPY OF THE ARITHMETIC, and that is a correction. This function and the
/// public `cfdFaceDecomposition` held the same expressions written out twice --
/// in a file whose own header justifies exposing the decomposition on the
/// grounds that "computing it again outside would be a second copy of the
/// arithmetic that could disagree with the assembly's". A review pointed out
/// that there were two copies INSIDE, with two different definitions of
/// "internal" between them. So this is now a thin conversion of the public
/// accessor's answer, and there is one definition of both.
struct Arms {
    Vec implicitArm{};
    Vec deferredArm{};
    double deferredFraction = 0.0;
};

[[nodiscard]] std::optional<Arms> armsOf(const CfdGeometry& geometry, FaceIndex face) noexcept {
    const std::optional<CfdFaceDecomposition> split = cfdFaceDecomposition(geometry, face);
    if (!split.has_value()) {
        return std::nullopt;
    }
    // `Area::fromSi` and `si()` round-trip exactly, so this conversion loses
    // nothing: the arms are the accessor's own doubles.
    return Arms{siOf(split->implicitArm), siOf(split->deferredArm), split->deferredFraction};
}

/// The compressed-sparse-row pattern, and a slot lookup within it.
struct Layout {
    std::vector<CfdSparseIndex> rowStart{};
    std::vector<CfdSparseIndex> inner{};
};

[[nodiscard]] Layout layoutOf(std::vector<std::vector<CfdSparseIndex>>& pattern) {
    Layout out;
    out.rowStart.reserve(pattern.size() + 1);
    out.rowStart.push_back(0);
    for (std::vector<CfdSparseIndex>& row : pattern) {
        std::ranges::sort(row);
        row.erase(std::ranges::unique(row).begin(), row.end());
        out.inner.insert(out.inner.end(), row.begin(), row.end());
        out.rowStart.push_back(static_cast<CfdSparseIndex>(out.inner.size()));
    }
    return out;
}

[[nodiscard]] std::optional<std::size_t> slotOf(const Layout& layout, std::size_t row,
                                                std::size_t column) noexcept {
    if (row + 1 >= layout.rowStart.size()) {
        return std::nullopt;
    }
    const auto first = static_cast<std::ptrdiff_t>(layout.rowStart[row]);
    const auto last = static_cast<std::ptrdiff_t>(layout.rowStart[row + 1]);
    const auto begin = std::next(layout.inner.begin(), first);
    const auto end = std::next(layout.inner.begin(), last);
    const auto found = std::lower_bound(begin, end, static_cast<CfdSparseIndex>(column));
    if (found == end || *found != static_cast<CfdSparseIndex>(column)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(layout.inner.begin(), found));
}

template <typename Describe>
[[nodiscard]] std::optional<CfdDiffusionProblem> refuse(CfdDiffusionProblem problem, std::string* message,
                                                       Describe describe) {
    if (message != nullptr) {
        *message = describe();
    }
    return problem;
}

/// Whether any boundary face's velocity policy is @p policy.
[[nodiscard]] bool anyVelocityPolicy(const CfdTopology& topology, const PreparedCfdBoundary& boundary,
                                     CfdBoundaryPolicy policy) noexcept {
    for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
        const std::optional<CfdBoundaryTreatment> treatment =
            boundary.treatment(BoundaryFaceIndex::fromValue(b));
        if (treatment.has_value() && treatment->velocity == policy) {
            return true;
        }
    }
    return false;
}

/// THE decision path, shared by the assembly and both predictors. The message
/// is built only when a caller passes somewhere to put it, which is what makes
/// `cfdDiffusionProblem`'s `noexcept` a fact rather than a hope.
///
/// `AssembledMatrixInconsistent` is NOT decided here: it can only be known by
/// assembling, so the predictors report none for an input that would reach it.
/// That is stated in the header and is the one place a predictor and the path
/// legitimately differ.
[[nodiscard]] std::optional<CfdDiffusionProblem> problemOf(const CfdTopology& topology,
                                                          const CfdGeometry& geometry,
                                                          const PreparedCfdBoundary& boundary,
                                                          const CfdDiffusionInput& input,
                                                          std::string* message) {
    // PHASE 1 -- the metrics and the topology, before either is read.
    if (!geometry.describes(topology)) {
        return refuse(CfdDiffusionProblem::GeometryDescribesAnotherTopology, message, [&] {
            return std::format("the metrics describe a mesh of {} cells and {} faces and the topology has "
                               "{} and {}",
                               geometry.cells(), geometry.faces(), topology.cells(), topology.faces());
        });
    }

    // PHASE 2 -- the boundary, which is a third value prepared separately.
    //
    // TWO ARMS, AND THE SECOND IS CONDITIONAL FOR A REASON WORTH READING.
    // `bindCfdBoundary` -- the seam every hand-built fixture goes through --
    // deliberately returns a value whose `source()` is DEFAULT-CONSTRUCTED,
    // because the seam is handed no model and so has no body, stamp or geometry
    // revision to record. A first draft of this phase compared the stamp and
    // the counts unconditionally, which would have refused every seam-built
    // boundary and so made the orthogonal two-cell fixture and the analytic
    // face pair -- the only places the arithmetic can be checked in closed form
    // -- unreachable. Found by reading that contract rather than by a test.
    //
    // So: the STRUCTURAL agreement is always required, because the assembly
    // indexes the boundary by the topology's own `BoundaryFaceIndex` and a
    // disagreement there is an out-of-range read waiting to happen. The
    // CURRENCY claim is checked only when the boundary makes one. A seam-built
    // boundary asserts nothing about which mesh it came from, which is the
    // conservative direction BC-001 chose for it, and the document path's own
    // gate is what establishes currency for a caller that has a model.
    if (boundary.boundaryFaces() != topology.boundaryFaces()) {
        return refuse(CfdDiffusionProblem::BoundaryDescribesAnotherTopology, message, [&] {
            return std::format("the prepared boundary holds {} boundary faces and this topology has {}",
                               boundary.boundaryFaces(), topology.boundaryFaces());
        });
    }
    const CfdBoundarySource& source = boundary.source();
    if (!(source == CfdBoundarySource{}) &&
        (!(source.mesh == topology.mesh()) || source.cells != topology.cells() ||
         source.boundaryFaces != topology.boundaryFaces())) {
        return refuse(CfdDiffusionProblem::BoundaryDescribesAnotherTopology, message, [&] {
            return std::format("the prepared boundary was built for {} cells and {} boundary faces and "
                               "this topology has {} and {}",
                               source.cells, source.boundaryFaces, topology.cells(),
                               topology.boundaryFaces());
        });
    }

    // PHASE 3 -- the viscosity. Finite and strictly positive, refused and never
    // repaired.
    if (!isFinite(input.viscosity) || !(input.viscosity.si() > 0.0)) {
        return refuse(CfdDiffusionProblem::ViscosityNotUsable, message, [&] {
            return std::format("the dynamic viscosity is {} Pa s, which is not finite and positive",
                               input.viscosity.si());
        });
    }

    // PHASE 4 -- a slip wall needs the current velocity for its cross terms.
    // Checked before the LENGTH, so an assembly with a symmetry face and no
    // velocity at all is reported as the missing dependency rather than as a
    // length of zero.
    const bool hasSymmetry = anyVelocityPolicy(topology, boundary, CfdBoundaryPolicy::Symmetry);
    if (hasSymmetry && input.currentVelocity.empty()) {
        return refuse(CfdDiffusionProblem::SymmetryNeedsTheCurrentVelocity, message, [&] {
            return std::format("a symmetry boundary face is present, so the cross terms need the current "
                               "velocity of all {} components over {} cells",
                               kVectorComponents.size(), topology.cells());
        });
    }

    // PHASE 5 -- and if supplied, at the component-blocked length.
    const std::size_t expectedVelocity = kVectorComponents.size() * topology.cells();
    if (!input.currentVelocity.empty() && input.currentVelocity.size() != expectedVelocity) {
        return refuse(CfdDiffusionProblem::CurrentVelocityLengthWrong, message, [&] {
            return std::format("a component-blocked velocity over {} cells needs {} values, not {}",
                               topology.cells(), expectedVelocity, input.currentVelocity.size());
        });
    }

    // PHASE 6 -- and finite. CHECKED FOR THE SAME REASON THE VISCOSITY IS: a
    // review found both caller-supplied arrays validated for LENGTH and never
    // for VALUE, so one NaN flowed through the cross terms into the source
    // while the matrix stayed provably finite. Length first, so a wrong length
    // is reported as a length and not as whatever happens to sit in it.
    for (std::size_t i = 0; i < input.currentVelocity.size(); ++i) {
        if (!std::isfinite(input.currentVelocity[i])) {
            return refuse(CfdDiffusionProblem::CurrentVelocityNotFinite, message, [&] {
                return std::format("the current velocity holds {} at index {}, which is not finite",
                                   input.currentVelocity[i], i);
            });
        }
    }

    // PHASE 7 -- and the correction, one gradient per face.
    if (!input.faceGradient.empty() && input.faceGradient.size() != topology.faces()) {
        return refuse(CfdDiffusionProblem::CorrectionLengthWrong, message, [&] {
            return std::format("a face gradient needs one value per face, {} of them, not {}",
                               topology.faces(), input.faceGradient.size());
        });
    }

    // PHASE 8 -- and finite, for the same reason.
    for (std::size_t i = 0; i < input.faceGradient.size(); ++i) {
        if (!isFinite(input.faceGradient[i])) {
            return refuse(CfdDiffusionProblem::CorrectionNotFinite, message, [&] {
                return std::format("the face gradient at face {} is not finite", i);
            });
        }
    }

    // PHASE 9 -- an outcome policy on the velocity. UNREACHABLE through the
    // document path: down the `U` column of ADR-067's table the only values are
    // FixedValue, ZeroGradient and Symmetry. Reached only by a hand-built
    // boundary, and refused rather than silently assembled as zero flux.
    if (anyVelocityPolicy(topology, boundary, CfdBoundaryPolicy::Computed)) {
        return refuse(CfdDiffusionProblem::VelocityPolicyIsAnOutcome, message, [&] {
            return std::format("a boundary face gives the velocity the Computed policy, which is an "
                               "outcome of a solve and not a diffusion boundary condition");
        });
    }

    return std::nullopt;
}

} // namespace

std::string_view toString(CfdDiffusionProblem problem) noexcept {
    switch (problem) {
    case CfdDiffusionProblem::GeometryDescribesAnotherTopology:
        return "geometry_describes_another_topology";
    case CfdDiffusionProblem::BoundaryDescribesAnotherTopology:
        return "boundary_describes_another_topology";
    case CfdDiffusionProblem::ViscosityNotUsable:
        return "viscosity_not_usable";
    case CfdDiffusionProblem::SymmetryNeedsTheCurrentVelocity:
        return "symmetry_needs_the_current_velocity";
    case CfdDiffusionProblem::CurrentVelocityLengthWrong:
        return "current_velocity_length_wrong";
    case CfdDiffusionProblem::CurrentVelocityNotFinite:
        return "current_velocity_not_finite";
    case CfdDiffusionProblem::CorrectionLengthWrong:
        return "correction_length_wrong";
    case CfdDiffusionProblem::CorrectionNotFinite:
        return "correction_not_finite";
    case CfdDiffusionProblem::VelocityPolicyIsAnOutcome:
        return "velocity_policy_is_an_outcome";
    }
    // Not `default:`, so a tenth enumerator is a -Wswitch error.
    return "unknown";
}

std::optional<double> cfdFaceInterpolationWeight(const CfdGeometry& geometry,
                                                 FaceIndex face) noexcept {
    // NORMAL PROJECTIONS, NOT RAW DISTANCES, and that is a correction found by
    // this function's own test. ADR-069 Decision 7 first wrote the weight as
    // `|C_N - C_f| / |C_N - C_P|`, and on a SKEWED face that is not bounded by
    // one: the face centroid does not lie on the segment joining the two cell
    // centroids, which is exactly what skewness measures, and ADR-065 recorded
    // it as high as 6.5 on RM-MESH-03. The sweep measured 3.61 on the Cylinder.
    // A number above one is not a convex weight, so the distance ratio does not
    // define an interpolation at all.
    //
    // The projections on the face normal do:
    //     a = (C_f - C_P) . n     the owner's normal distance to the face
    //     b = (C_N - C_f) . n     the neighbour's normal distance from it
    //     a + b = (C_N - C_P) . n = orthogonalDistance(face)
    // and ADR-065 Decision 5 proves that sum is the quarter-sum of the two apex
    // heights, so BOTH are strictly positive and `w = b / (a + b)` is strictly
    // inside (0, 1) for every held face. The owner's share, so a face close to
    // the owner gives a weight near one.
    const double total = geometry.orthogonalDistance(face).si();
    if (!(total > 0.0)) {
        return std::nullopt;
    }
    const std::optional<Translation3D> span = geometry.ownerToNeighbour(face);
    if (!span.has_value()) {
        // A boundary face has no neighbour to interpolate with, so the owner's
        // value IS the face value. (`orthogonalDistance` already answered
        // positive, so the face is held.)
        return 1.0;
    }
    const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
    if (!normal.has_value()) {
        return std::nullopt;
    }
    const Vec toFace = siOf(geometry.cellToFace(face));
    const Vec n{normal->x(), normal->y(), normal->z()};
    const double owner = dotOf(toFace, n);
    return (total - owner) / total;
}

DynamicViscosity cfdFaceViscosity(const CfdGeometry& geometry, FaceIndex face,
                                  DynamicViscosity viscosity) noexcept {
    const std::optional<double> weight = cfdFaceInterpolationWeight(geometry, face);
    if (!weight.has_value()) {
        return viscosity;
    }
    // `mu_f = mu_N + w (mu_P - mu_N)`, WHICH IS THE NUMERICALLY STABLE FORM OF
    // THE SAME CONVEX COMBINATION, and the difference is not cosmetic. Written
    // as `w mu_P + (1 - w) mu_N` it is NOT exactly `mu` when the two
    // viscosities are equal -- the two products round and the sum reassociates
    // -- and this function's own test caught that, failing an exact comparison
    // whose two sides printed identically. In this form the difference is
    // exactly zero, so `mu_f` is exactly `mu` for every weight, and the
    // assembled coefficient carries no rounding that depends on a face's
    // skewness for no physical reason.
    const double w = *weight;
    const double ownerViscosity = viscosity.si();
    const double neighbourViscosity = viscosity.si();
    return DynamicViscosity::fromSi(neighbourViscosity +
                                    w * (ownerViscosity - neighbourViscosity));
}

std::optional<CfdFaceDecomposition> cfdFaceDecomposition(const CfdGeometry& geometry,
                                                         FaceIndex face) noexcept {
    // NO TOPOLOGY IS NEEDED TO KNOW WHETHER THE FACE IS INTERNAL.
    // `ownerToNeighbour` is empty exactly for a boundary face and for a face
    // this geometry does not hold (CfdGeometry.hpp), and the orthogonal
    // distance separates those two: it is strictly positive for a held face and
    // zero otherwise. So this is the ONE definition of `d` -- owner-to-neighbour
    // on an internal face, cell-to-face on a boundary one -- and the assembly
    // reads it from here rather than keeping a second copy, which is what an
    // earlier draft did.
    const std::optional<Translation3D> internalSpan = geometry.ownerToNeighbour(face);
    const double orthogonal = geometry.orthogonalDistance(face).si();
    if (!(orthogonal > 0.0)) {
        return std::nullopt;
    }
    const Vec d = internalSpan.has_value() ? siOf(*internalSpan) : siOf(geometry.cellToFace(face));
    const Vec s = siOf(geometry.faceAreaVector(face));
    const double area = normOf(s);
    const double scale = area / orthogonal;
    const Vec implicitArm{scale * d.x, scale * d.y, scale * d.z};
    const Vec deferredArm{s.x - implicitArm.x, s.y - implicitArm.y, s.z - implicitArm.z};
    const double implicitNorm = normOf(implicitArm);
    if (!(implicitNorm > 0.0)) {
        return std::nullopt;
    }
    return CfdFaceDecomposition{areaVectorOf(implicitArm), areaVectorOf(deferredArm),
                                normOf(deferredArm) / implicitNorm};
}

Result<CfdDiffusionSystem> assembleCfdDiffusion(const CfdTopology& topology, const CfdGeometry& geometry,
                                                const PreparedCfdBoundary& boundary,
                                                const CfdDiffusionInput& input) {
    std::string message;
    if (const std::optional<CfdDiffusionProblem> problem =
            problemOf(topology, geometry, boundary, input, &message);
        problem.has_value()) {
        return makeError(ErrorCode::FailedPrecondition, std::move(message));
    }

    const std::size_t cells = topology.cells();
    const auto componentIndex = static_cast<std::size_t>(input.component);

    // ---- SYMBOLIC ---------------------------------------------------------
    std::vector<std::vector<CfdSparseIndex>> pattern(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        // Every row holds its own diagonal, whether or not a face reaches it: a
        // cell with no face at all would otherwise have an empty row, and a
        // zero diagonal is a different statement from an absent one.
        pattern[c].push_back(static_cast<CfdSparseIndex>(c));
    }
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        if (!topology.isInternal(face)) {
            continue;
        }
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        if (!neighbour.has_value()) {
            continue;
        }
        const std::size_t owner = topology.owner(face).value();
        const std::size_t other = neighbour->value();
        pattern[owner].push_back(static_cast<CfdSparseIndex>(other));
        pattern[other].push_back(static_cast<CfdSparseIndex>(owner));
    }
    const Layout layout = layoutOf(pattern);

    // ---- NUMERIC ----------------------------------------------------------
    std::vector<double> values(layout.inner.size(), 0.0);
    std::vector<double> rhs(cells, 0.0);
    CfdDiffusionSummary summary;
    summary.cells = cells;
    summary.correctionApplied = !input.faceGradient.empty();

    // THE TOTAL A BOUNDARY FACE ADDS TO ANY DIAGONAL, accumulated rather than
    // inferred from the face counts. A first draft set
    // `constantModeIsInTheNullSpace` from `fixedValueFaces == 0 &&
    // symmetryFaces == 0`, which is WRONG for a symmetry face whose normal is
    // perpendicular to this component's axis: it contributes `Gamma n_i^2` with
    // `n_i == 0`, so it touches no diagonal and the constant mode survives,
    // while the count-based flag claimed it had been lifted. Measuring what was
    // actually added cannot disagree with the matrix.
    double boundaryDiagonal = 0.0;

    const auto inconsistent = [](std::size_t row, std::size_t column) {
        return makeError(ErrorCode::Internal,
                         std::format("the symbolic pass did not reserve ({}, {})", row, column));
    };

    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::optional<Arms> arms = armsOf(geometry, face);
        if (!arms.has_value()) {
            return makeError(ErrorCode::Internal,
                             std::format("face {} has no usable orthogonal distance", f));
        }
        // NOT NOTED HERE. A ZeroGradient face defers nothing, so raising the
        // reported fraction for it would overstate what was omitted -- a review
        // found the earlier placement did exactly that. `noteDeferred` is called
        // from the three paths that contribute a coefficient.
        const auto noteDeferred = [&] {
            summary.deferredFraction = std::max(summary.deferredFraction, arms->deferredFraction);
        };

        const double area = normOf(siOf(geometry.faceAreaVector(face)));
        // THE SINGLE CALL SITE ADR-069 Decision 7 names. For the constant
        // viscosity P19 v1 is limited to this is `mu`; a variable one changes
        // `cfdFaceViscosity` and nothing here.
        const double muFace = cfdFaceViscosity(geometry, face, input.viscosity).si();
        const double gamma = muFace * area / geometry.orthogonalDistance(face).si();
        const std::size_t owner = topology.owner(face).value();

        // The deferred correction, when a gradient was supplied: the flux the
        // implicit arm does not carry is `mu (grad U)_f . T_f`, and it moves to
        // the right-hand side of `A u = b` with a plus sign.
        const auto correctionOf = [&](std::size_t faceSlot) {
            if (input.faceGradient.empty()) {
                return 0.0;
            }
            const VelocityGradient3& g = input.faceGradient[faceSlot];
            const Vec gradient{g.x.si(), g.y.si(), g.z.si()};
            return muFace * dotOf(gradient, arms->deferredArm);
        };

        if (topology.isInternal(face)) {
            const std::optional<CellIndex> neighbour = topology.neighbour(face);
            if (!neighbour.has_value()) {
                return makeError(ErrorCode::Internal,
                                 std::format("face {} is internal and has no neighbour", f));
            }
            const std::size_t other = neighbour->value();
            // ONE VISIT, FOUR ENTRIES. Equal and opposite by construction:
            // there is no second visit that could disagree.
            const std::optional<std::size_t> ownerDiagonal = slotOf(layout, owner, owner);
            const std::optional<std::size_t> ownerOff = slotOf(layout, owner, other);
            const std::optional<std::size_t> otherDiagonal = slotOf(layout, other, other);
            const std::optional<std::size_t> otherOff = slotOf(layout, other, owner);
            if (!ownerDiagonal.has_value() || !ownerOff.has_value() || !otherDiagonal.has_value() ||
                !otherOff.has_value()) {
                return inconsistent(owner, other);
            }
            noteDeferred();
            values[*ownerDiagonal] += gamma;
            values[*ownerOff] -= gamma;
            values[*otherDiagonal] += gamma;
            values[*otherOff] -= gamma;

            // The correction is signed by the face's winding: `T_f` is wound out
            // of the OWNER, so the neighbour sees its negation.
            const double correction = correctionOf(f);
            rhs[owner] += correction;
            rhs[other] -= correction;
            ++summary.internalFaces;
            continue;
        }

        // ---- a boundary face ----------------------------------------------
        const std::optional<BoundaryFaceIndex> boundarySlot = topology.boundaryFaceOf(face);
        if (!boundarySlot.has_value()) {
            return makeError(ErrorCode::Internal,
                             std::format("face {} is a boundary face with no boundary index", f));
        }
        const std::optional<CfdBoundaryTreatment> treatment = boundary.treatment(*boundarySlot);
        const std::optional<CfdBoundaryFace> record = boundary.face(*boundarySlot);
        if (!treatment.has_value() || !record.has_value()) {
            return makeError(ErrorCode::Internal,
                             std::format("boundary face {} has no prepared condition",
                                         boundarySlot->value()));
        }
        const std::optional<std::size_t> diagonal = slotOf(layout, owner, owner);
        if (!diagonal.has_value()) {
            return inconsistent(owner, owner);
        }

        switch (treatment->velocity) {
        case CfdBoundaryPolicy::FixedValue: {
            // Dirichlet. A boundary face is NOT a degree of freedom in a
            // cell-centred method, so there is no row to eliminate later and
            // the value has to enter here -- which is where this departs from
            // thermal, whose boundary nodes are unknowns (ADR-069 Decision 6).
            // `PreparedCfdBoundary` holds a velocity exactly when the policy
            // is FixedValue, so an absent one here is an inconsistency in that
            // value and not a zero wall velocity. Defaulting to zero would
            // assemble a no-slip wall where the intent is unknown, which is the
            // silent wrong answer this project's rules exist to prevent.
            if (!record->velocity.has_value()) {
                return makeError(ErrorCode::Internal,
                                 std::format("boundary face {} prescribes the velocity and carries none",
                                             boundarySlot->value()));
            }
            noteDeferred();
            const double prescribed = record->velocity->component(input.component).si();
            values[*diagonal] += gamma;
            boundaryDiagonal += gamma;
            rhs[owner] += gamma * prescribed + correctionOf(f);
            ++summary.fixedValueFaces;
            break;
        }
        case CfdBoundaryPolicy::ZeroGradient:
            // The normal derivative is zero, so the diffusive flux is zero and
            // NOTHING is touched -- not a zero written into a coefficient.
            ++summary.zeroGradientFaces;
            break;
        case CfdBoundaryPolicy::Symmetry: {
            // `U . n = 0` at the face and the tangential part has zero normal
            // gradient, so the flux of component i is
            // `-Gamma n_i (U_P . n)`. The `n_i^2` part is implicit; the cross
            // terms are a known number from the current field.
            const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
            if (!normal.has_value()) {
                return makeError(ErrorCode::Internal,
                                 std::format("boundary face {} has no unit normal", f));
            }
            const std::array<double, 3> n{normal->x(), normal->y(), normal->z()};
            noteDeferred();
            const double implicitSymmetry = gamma * n[componentIndex] * n[componentIndex];
            values[*diagonal] += implicitSymmetry;
            boundaryDiagonal += implicitSymmetry;
            double cross = 0.0;
            for (std::size_t j = 0; j < kVectorComponents.size(); ++j) {
                if (j == componentIndex) {
                    continue;
                }
                cross += n[j] * input.currentVelocity[j * cells + owner];
            }
            rhs[owner] -= gamma * n[componentIndex] * cross;
            rhs[owner] += correctionOf(f);
            ++summary.symmetryFaces;
            break;
        }
        case CfdBoundaryPolicy::Computed:
            // Refused in phase 7, so this is unreachable. Named rather than
            // `default:`-ed so a fifth policy is a -Wswitch error.
            return makeError(ErrorCode::Internal,
                             std::format("boundary face {} reached the assembly with an outcome policy",
                                         boundarySlot->value()));
        }
    }

    // Exactly zero, not a tolerance: every contribution above is a sum of
    // strictly positive terms, so a zero total means none was added at all.
    summary.constantModeIsInTheNullSpace = boundaryDiagonal == 0.0;

    std::expected<CfdSparseMatrix, CfdSparseFailure> matrix =
        CfdSparseMatrix::fromCsr(cells, cells, layout.rowStart, layout.inner, std::move(values),
                                 kCfdDiffusionSymmetryTolerance);
    if (!matrix.has_value()) {
        return makeError(ErrorCode::Internal,
                         std::format("the assembled layout was refused as {}: {}",
                                     toString(matrix.error().problem), matrix.error().error.message));
    }
    return CfdDiffusionSystem{std::move(*matrix), std::move(rhs), summary};
}

std::optional<CfdDiffusionFailure> cfdDiffusionFailure(const CfdTopology& topology,
                                                       const CfdGeometry& geometry,
                                                       const PreparedCfdBoundary& boundary,
                                                       const CfdDiffusionInput& input) {
    std::string message;
    const std::optional<CfdDiffusionProblem> problem =
        problemOf(topology, geometry, boundary, input, &message);
    if (!problem.has_value()) {
        return std::nullopt;
    }
    return CfdDiffusionFailure{*problem, Error{ErrorCode::FailedPrecondition, std::move(message)}};
}

std::optional<CfdDiffusionProblem> cfdDiffusionProblem(const CfdTopology& topology,
                                                       const CfdGeometry& geometry,
                                                       const PreparedCfdBoundary& boundary,
                                                       const CfdDiffusionInput& input) noexcept {
    // `nullptr` for the message, so this path allocates nothing.
    return problemOf(topology, geometry, boundary, input, nullptr);
}

} // namespace bettercad::cfd
