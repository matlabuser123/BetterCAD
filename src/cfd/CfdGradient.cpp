#include <bettercad/cfd/CfdGradient.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

namespace bettercad::cfd {

// Type aliases have NO LINKAGE, so these may sit at namespace scope in a
// translation unit without exporting anything. The TU-local *types* that the
// builder's signatures mention are nested inside the builder instead, so that
// no externally-linked member function names an internal-linkage type.
using Vec = std::array<double, 3>;
/// A symmetric 3x3 by its six distinct entries: xx, xy, xz, yy, yz, zz.
using Sym = std::array<double, 6>;

namespace {

[[nodiscard]] Vec siOf(const AreaVector3D& v) noexcept {
    return {v.x.si(), v.y.si(), v.z.si()};
}

[[nodiscard]] Vec siOf(const Translation3D& v) noexcept {
    return {v.x.si(), v.y.si(), v.z.si()};
}

[[nodiscard]] double dotOf(const Vec& a, const Vec& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

[[nodiscard]] Vec scaledBy(const Vec& a, double s) noexcept {
    return {a[0] * s, a[1] * s, a[2] * s};
}

/// `acc += s * v`, in one place so the accumulation is written once.
void addScaled(Vec& acc, const Vec& v, double s) noexcept {
    acc[0] += s * v[0];
    acc[1] += s * v[1];
    acc[2] += s * v[2];
}

/// ONE least-squares row: `m += w (d (x) d)` and `rhs += w d value`.
///
/// Both halves are written here together, because a row whose matrix
/// contribution and right-hand side disagreed about `d` or `w` would still
/// produce a plausible-looking gradient.
void addRow(Sym& m, Vec& rhs, const Vec& d, double value, double weight) noexcept {
    m[0] += weight * d[0] * d[0];
    m[1] += weight * d[0] * d[1];
    m[2] += weight * d[0] * d[2];
    m[3] += weight * d[1] * d[1];
    m[4] += weight * d[1] * d[2];
    m[5] += weight * d[2] * d[2];
    addScaled(rhs, d, weight * value);
}

/// The extreme eigenvalue ratio of a symmetric 3x3, by the closed-form
/// trigonometric solution of its characteristic cubic.
///
/// NO ITERATION AND NO LIBRARY. ADR-070 Decision 8: `core/math` has no matrix
/// type and no solve -- its only 3x3 is `RigidTransform3D`'s private rotation --
/// and `BetterCAD::linalg`'s one entry point is a SPARSE symmetric solver with
/// no eigenvalue routine at all. So there is nothing in a lower layer to reuse
/// and nothing being duplicated.
///
/// Infinite when the smallest eigenvalue is not positive, which is the
/// numerically rank-deficient case, and 1 for a multiple of the identity.
///
/// THE LARGEST EIGENVALUE COMES FROM THE TRIGONOMETRIC FORM AND THE SMALLEST
/// FROM THE INVARIANTS, and the second half is a correction an independent
/// oracle forced. Taking the smallest as `mean + 2p cos(phi + 2pi/3)` is the
/// textbook form and it is ACCURATE ONLY WHERE THE SPECTRUM IS WELL SPREAD:
/// `phi` comes through `acos`, which is ill-conditioned at its endpoints, and
/// the endpoints are exactly the repeated-eigenvalue case that an
/// ill-conditioned normal matrix approaches. Measured against a cyclic Jacobi
/// diagonalisation over constructed spectra:
///
/// ```text
/// eigenvalues        the textbook form     the truth      error
/// (1, 1, 2)          2.000000008           2              4.1e-09
/// (1, 2, 3)          3                     3              1.5e-16
/// (1, 1, 1e12)       9.9988e11             1e12           1.2e-04
/// (1, 1e6, 1e12)     3.1283e11             1e12           A FACTOR OF 3.2
/// (1, 1.5, 1e12)     inf                   1e12           A FALSE REFUSAL
/// ```
///
/// The last row is why this is a correction and not a refinement: a positive
/// definite matrix was reported as having a non-positive eigenvalue, so a cell
/// that could be solved was refused because the formula broke rather than
/// because its ratio exceeded the limit. And the factor of 3.2 matters on its
/// own terms, because ADR-070 Decision 7 hands this number to P19-VALID-001 to
/// judge a mesh by.
///
/// So: `lambda_max` by the trigonometric form, where `cos(phi)` is the
/// well-conditioned end; then `lambda_mid + lambda_min = trace - lambda_max` and
/// `lambda_mid * lambda_min = det / lambda_max`, solved by the quadratic in its
/// cancellation-free form. STILL CLOSED-FORM ARITHMETIC -- no iteration, no
/// convergence criterion -- so ADR-070 Decision 8 is unchanged. The new form
/// agrees with Jacobi to every printed digit in all nine measured cases.
[[nodiscard]] double eigenvalueRatio(const Sym& m) noexcept {
    const double a = m[0];
    const double b = m[1];
    const double c = m[2];
    const double d = m[3];
    const double e = m[4];
    const double f = m[5];
    const double infinite = std::numeric_limits<double>::infinity();

    const double trace = a + d + f;
    const double determinant = a * (d * f - e * e) - b * (b * f - e * c) + c * (b * e - d * c);
    const double mean = trace / 3.0;
    const double offDiagonal = b * b + c * c + e * e;
    const double spread =
        (a - mean) * (a - mean) + (d - mean) * (d - mean) + (f - mean) * (f - mean) + 2.0 * offDiagonal;
    const double p = std::sqrt(std::max(spread / 6.0, 0.0));
    if (!(p > 0.0)) {
        // Every eigenvalue equal: perfectly conditioned if positive at all.
        return mean > 0.0 ? 1.0 : infinite;
    }

    // `B = (M - mean I) / p` has `det B = 2 cos(3 phi)` and eigenvalues in
    // [-2, 2]. The clamp is for rounding, not for a case that can lie outside.
    const double ba = (a - mean) / p;
    const double bb = b / p;
    const double bc = c / p;
    const double bd = (d - mean) / p;
    const double be = e / p;
    const double bf = (f - mean) / p;
    const double detB = ba * (bd * bf - be * be) - bb * (bb * bf - be * bc) + bc * (bb * be - bd * bc);
    const double phi = std::acos(std::clamp(detB / 2.0, -1.0, 1.0)) / 3.0;

    const double largest = mean + 2.0 * p * std::cos(phi);
    if (!(largest > 0.0) || !std::isfinite(largest) || !(determinant > 0.0)) {
        return infinite;
    }

    // The other two, from the invariants. `sum` and `product` are the remaining
    // elementary symmetric functions once the largest is divided out.
    const double sum = trace - largest;
    const double product = determinant / largest;
    const double discriminant = std::max(sum * sum - 4.0 * product, 0.0);
    const double root = std::sqrt(discriminant);
    if (!(sum + root > 0.0)) {
        return infinite;
    }
    // `2 product / (sum + root)`, not `(sum - root) / 2`: the second subtracts
    // two nearly equal numbers exactly where the smallest eigenvalue is small,
    // which is the case this function exists to measure.
    const double smallest = 2.0 * product / (sum + root);
    if (!(smallest > 0.0)) {
        return infinite;
    }
    return largest / smallest;
}

/// Solves `M g = rhs` for a symmetric positive-definite `M`, by cofactors.
///
/// Empty when the determinant is not strictly positive, or when the solution is
/// not finite. The CONDITIONING limit is the caller's, because the ratio is
/// reported whether or not a solve is refused.
[[nodiscard]] std::optional<Vec> solveSymmetric(const Sym& m, const Vec& rhs) noexcept {
    const double a = m[0];
    const double b = m[1];
    const double c = m[2];
    const double d = m[3];
    const double e = m[4];
    const double f = m[5];

    const double c00 = d * f - e * e;
    const double c01 = c * e - b * f;
    const double c02 = b * e - c * d;
    const double determinant = a * c00 + b * c01 + c * c02;
    if (!(determinant > 0.0)) {
        return std::nullopt;
    }
    const double c11 = a * f - c * c;
    const double c12 = b * c - a * e;
    const double c22 = a * d - b * b;

    const Vec solution{(c00 * rhs[0] + c01 * rhs[1] + c02 * rhs[2]) / determinant,
                       (c01 * rhs[0] + c11 * rhs[1] + c12 * rhs[2]) / determinant,
                       (c02 * rhs[0] + c12 * rhs[1] + c22 * rhs[2]) / determinant};
    if (!std::isfinite(solution[0]) || !std::isfinite(solution[1]) || !std::isfinite(solution[2])) {
        return std::nullopt;
    }
    return solution;
}

template <typename Describe>
[[nodiscard]] std::optional<CfdGradientProblem> refuse(CfdGradientProblem problem, std::string* message,
                                                       Describe describe) {
    if (message != nullptr) {
        *message = describe();
    }
    return problem;
}

} // namespace

// ---------------------------------------------------------------------------
// The builder: the only thing that may construct a field, and the one place the
// arithmetic lives.
// ---------------------------------------------------------------------------

struct CfdGradientBuilder {
    /// Which field is being differentiated. Needed because ADR-067's table has a
    /// different column for each, and only the velocity column holds `Symmetry`.
    enum class Field : std::uint8_t { Pressure, Velocity };

    /// One read-only view of the caller's field: the cell values, the policy
    /// column and the prescribed boundary value. NESTED, so the two entry points
    /// share one body instead of two that must agree.
    struct View {
        Field field = Field::Pressure;
        VectorComponent component = VectorComponent::X;
        std::span<const double> values{};
        std::size_t cells = 0;

        [[nodiscard]] std::size_t expectedLength() const noexcept {
            return field == Field::Pressure ? cells : kVectorComponents.size() * cells;
        }

        /// The value of the field being differentiated, at @p cell.
        [[nodiscard]] double at(std::size_t cell) const noexcept {
            if (field == Field::Pressure) {
                return values[cell];
            }
            return values[static_cast<std::size_t>(component) * cells + cell];
        }

        /// One named velocity component at @p cell. A slip wall needs all three.
        [[nodiscard]] double velocityAt(std::size_t cell, VectorComponent which) const noexcept {
            return values[static_cast<std::size_t>(which) * cells + cell];
        }

        [[nodiscard]] CfdBoundaryPolicy policyOf(const CfdBoundaryTreatment& treatment) const noexcept {
            return field == Field::Pressure ? treatment.pressure : treatment.velocity;
        }

        /// The prescribed value at a `FixedValue` facet, or none if the prepared
        /// boundary holds none -- an inconsistency in that value, not a zero.
        [[nodiscard]] std::optional<double> prescribedAt(const CfdBoundaryFace& facet) const noexcept {
            if (field == Field::Pressure) {
                return facet.pressure.has_value() ? std::optional<double>{facet.pressure->si()}
                                                  : std::nullopt;
            }
            return facet.velocity.has_value()
                       ? std::optional<double>{facet.velocity->component(component).si()}
                       : std::nullopt;
        }

        /// The face value a slip wall gives this component. `U_b = U_P - (U_P .
        /// n) n`, so `U_b,i = U_P,i - n_i (U_P . n)` -- which reads ALL THREE
        /// components of the owner's velocity (ADR-070 Decision 4).
        [[nodiscard]] double symmetryValueAt(std::size_t cell, const Vec& unitNormal) const noexcept {
            const Vec velocity{velocityAt(cell, VectorComponent::X), velocityAt(cell, VectorComponent::Y),
                               velocityAt(cell, VectorComponent::Z)};
            const double along = dotOf(velocity, unitNormal);
            return at(cell) - unitNormal[static_cast<std::size_t>(component)] * along;
        }

        [[nodiscard]] std::string_view name() const noexcept {
            return field == Field::Pressure ? "pressure" : "velocity";
        }
    };

    // -----------------------------------------------------------------------
    // Geometry reads
    // -----------------------------------------------------------------------

    /// The weight on the OWNER's value at an internal face, from the geometry
    /// alone.
    ///
    /// `(d . n - (C_f - C_P) . n) / (d . n)`: a NORMAL PROJECTION, and not
    /// `|C_N - C_f| / |C_N - C_P|`, which is not a convex weight on a skewed
    /// mesh -- P19-DIFF-001's own test measured 3.6095 for that form on the
    /// Cylinder, and replacing it is why this form is here.
    [[nodiscard]] static double interpolationWeight(const CfdGeometry& geometry, FaceIndex face) noexcept {
        const double total = geometry.orthogonalDistance(face).si();
        const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
        if (!(total > 0.0) || !normal.has_value()) {
            return 0.5;
        }
        const Vec unitNormal{normal->x(), normal->y(), normal->z()};
        const double owner = dotOf(siOf(geometry.cellToFace(face)), unitNormal);
        return (total - owner) / total;
    }

    /// The face value a boundary policy gives. `ZeroGradient` is the owner's own
    /// value, which is what a zero normal derivative means for a FACE VALUE.
    [[nodiscard]] static std::optional<double> boundaryValue(const PreparedCfdBoundary& boundary,
                                                             const View& view,
                                                             BoundaryFaceIndex boundaryFace,
                                                             CfdBoundaryPolicy policy, std::size_t owner,
                                                             const Vec& unitNormal) {
        switch (policy) {
        case CfdBoundaryPolicy::FixedValue: {
            const std::optional<CfdBoundaryFace> facet = boundary.face(boundaryFace);
            return facet.has_value() ? view.prescribedAt(*facet) : std::nullopt;
        }
        case CfdBoundaryPolicy::ZeroGradient:
            return view.at(owner);
        case CfdBoundaryPolicy::Symmetry:
            // THE FIELD IS CONSULTED, NOT ONLY THE POLICY. A review found that
            // this switch dispatched on the policy alone, so a PRESSURE face
            // with this policy would call `symmetryValueAt`, which reads
            // `values[1 * cells + cell]` and `values[2 * cells + cell]` -- past
            // the end of a span that holds one value per cell and does no
            // bounds checking. The gate refuses that input by name now, so this
            // arm is unreachable for a scalar field; the check stays because a
            // silent out-of-bounds read is not a thing to leave one table
            // change away.
            if (view.field != Field::Velocity) {
                return std::nullopt;
            }
            return view.symmetryValueAt(owner, unitNormal);
        case CfdBoundaryPolicy::Computed:
            return std::nullopt; // refused by the gate; unreachable here
        }
        return std::nullopt;
    }

    /// One cell's least-squares normal system.
    struct System {
        Sym m{};
        Vec rhs{};
    };

    /// The least-squares normal system for one cell.
    ///
    /// THE ONLY COPY OF THIS ARITHMETIC. The gate calls it to judge
    /// conditioning and the assembly calls it to solve, so there is no second
    /// version that could drift -- which is the defect a review found in
    /// P19-DIFF-001, where the surface-vector split was written out twice in a
    /// file whose own header forbade exactly that.
    ///
    /// IT RETURNS THE SYSTEM RATHER THAN FILLING TWO OUT-PARAMETERS, and that
    /// is a change the mutation sweep argued for. The earlier signature took
    /// `Sym&` and `Vec&` and reset them on entry -- which was DEAD CODE, because
    /// both call sites declared fresh zero-initialised locals inside their own
    /// per-cell loop, so no test could distinguish the reset being there from it
    /// being absent. A probe that tried to make it matter had to mutate the
    /// function AND both call sites together, which is a test for a code shape
    /// that does not exist. Returning the value deletes the dead reset and makes
    /// a system that accumulates across cells unrepresentable instead of merely
    /// untested.
    [[nodiscard]] static System normalSystem(const CfdTopology& topology, const CfdGeometry& geometry,
                                             const PreparedCfdBoundary& boundary, const View& view,
                                             std::size_t cell) {
        System system;
        Sym& m = system.m;
        Vec& rhs = system.rhs;
        const double here = view.at(cell);
        // THE ORDER IS cellFaces', ascending FaceIndex, stored rather than
        // rebuilt, so the accumulation order is fixed once (ADR-070 Decision 9).
        for (const FaceIndex face : topology.cellFaces(CellIndex::fromValue(cell))) {
            const CellIndex owner = topology.owner(face);
            const bool isOwner = owner.value() == cell;

            if (topology.isInternal(face)) {
                const std::optional<CellIndex> neighbour = topology.neighbour(face);
                const std::optional<Translation3D> span = geometry.ownerToNeighbour(face);
                if (!neighbour.has_value() || !span.has_value()) {
                    continue;
                }
                const std::size_t other = isOwner ? neighbour->value() : owner.value();
                // `d` must point away from THIS cell, so the stored
                // owner-to-neighbour vector is negated when this cell is the
                // neighbour.
                const Vec d = isOwner ? siOf(*span) : scaledBy(siOf(*span), -1.0);
                const double squared = dotOf(d, d);
                if (squared > 0.0) {
                    addRow(m, rhs, d, view.at(other) - here, 1.0 / squared);
                }
                continue;
            }

            const std::optional<BoundaryFaceIndex> boundaryFace = topology.boundaryFaceOf(face);
            const std::optional<CfdBoundaryTreatment> treatment =
                boundaryFace.has_value() ? boundary.treatment(*boundaryFace) : std::nullopt;
            const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
            if (!treatment.has_value() || !normal.has_value()) {
                continue;
            }
            const Vec unitNormal{normal->x(), normal->y(), normal->z()};
            const CfdBoundaryPolicy policy = view.policyOf(*treatment);

            if (policy == CfdBoundaryPolicy::ZeroGradient) {
                // `g . n = 0`, scaled by the orthogonal distance so this row
                // weights consistently with a value row. Writing it as
                // `(C_f - C_P, 0)` would instead say `g . (C_f - C_P) = 0`,
                // which is `g . n = 0` only on an ORTHOGONAL mesh -- and the two
                // forms agree exactly on an orthogonal fixture, so only a
                // non-orthogonal one can tell them apart (ADR-070 Decision 5).
                const Vec d = scaledBy(unitNormal, geometry.orthogonalDistance(face).si());
                const double squared = dotOf(d, d);
                if (squared > 0.0) {
                    addRow(m, rhs, d, 0.0, 1.0 / squared);
                }
                continue;
            }

            const std::optional<double> value =
                boundaryValue(boundary, view, *boundaryFace, policy, owner.value(), unitNormal);
            if (!value.has_value()) {
                continue;
            }
            const Vec d = siOf(geometry.cellToFace(face));
            const double squared = dotOf(d, d);
            if (squared > 0.0) {
                addRow(m, rhs, d, *value - here, 1.0 / squared);
            }
        }
        return system;
    }

    // -----------------------------------------------------------------------
    // The gate
    // -----------------------------------------------------------------------

    /// THE ONE DECISION PATH. Both entry points and both predictors reach the
    /// gate through here, so there is no second copy that could drift. The
    /// message is built only when a caller passes somewhere to put it, which is
    /// what makes the predictors' `noexcept` honest: they allocate nothing.
    [[nodiscard]] static std::optional<CfdGradientProblem>
    problemOf(const CfdTopology& topology, const CfdGeometry& geometry, const PreparedCfdBoundary& boundary,
              const View& view, CfdGradientMethod method, std::string* message) {
        // PHASE 1 -- the metrics must describe this topology. First, because
        // every later phase reads both.
        if (!geometry.describes(topology)) {
            return refuse(CfdGradientProblem::GeometryDescribesAnotherTopology, message, [&] {
                return std::format("the geometric metrics were built for a different mesh generation than "
                                   "the topology, so its {} cells and {} faces do not mean the same thing",
                                   topology.cells(), topology.faces());
            });
        }

        // PHASE 2 -- and so must the prepared boundary. STRUCTURAL ONLY, by face
        // count: `bindCfdBoundary` deliberately returns a default-constructed
        // source, so comparing a stamp unconditionally would refuse every
        // seam-built boundary -- the defect P19-DIFF-001 found in its own gate
        // before any test ran, which would have made both of its analytic
        // fixtures unreachable.
        if (boundary.boundaryFaces() != topology.boundaryFaces()) {
            return refuse(CfdGradientProblem::BoundaryDescribesAnotherTopology, message, [&] {
                return std::format("the prepared boundary covers {} boundary faces and the topology has {}",
                                   boundary.boundaryFaces(), topology.boundaryFaces());
            });
        }

        // PHASE 3 -- the cell values, at the length this field needs.
        if (view.values.size() != view.expectedLength()) {
            return refuse(CfdGradientProblem::CellValueLengthWrong, message, [&] {
                if (view.field == Field::Pressure) {
                    return std::format("a cell-centred pressure over {} cells needs {} values, not {}",
                                       view.cells, view.expectedLength(), view.values.size());
                }
                return std::format("a component-blocked velocity over {} cells needs {} values, not {}",
                                   view.cells, view.expectedLength(), view.values.size());
            });
        }

        // PHASE 4 -- and finite. AFTER the length, so a wrong length is reported
        // as a length and not as whatever happens to sit in it.
        for (std::size_t i = 0; i < view.values.size(); ++i) {
            if (!std::isfinite(view.values[i])) {
                return refuse(CfdGradientProblem::CellValueNotFinite, message, [&] {
                    return std::format("the cell-centred {} holds {} at index {}, which is not finite",
                                       view.name(), view.values[i], i);
                });
            }
        }

        // PHASE 5 -- a prescribed value must be there when the policy says it is.
        for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
            const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
            const std::optional<CfdBoundaryTreatment> treatment = boundary.treatment(boundaryFace);
            if (!treatment.has_value()) {
                return refuse(CfdGradientProblem::BoundaryDescribesAnotherTopology, message, [&] {
                    return std::format("the prepared boundary holds no treatment at boundary face {}", b);
                });
            }
            if (view.policyOf(*treatment) != CfdBoundaryPolicy::FixedValue) {
                continue;
            }
            const std::optional<CfdBoundaryFace> facet = boundary.face(boundaryFace);
            if (!facet.has_value() || !view.prescribedAt(*facet).has_value()) {
                return refuse(CfdGradientProblem::PrescribedValueMissing, message, [&] {
                    return std::format("boundary face {} gives the {} the FixedValue policy and the "
                                       "prepared boundary holds no value for it",
                                       b, view.name());
                });
            }
        }

        // PHASE 6 -- a Symmetry policy on a SCALAR field. `Symmetry` decomposes a
        // vector, and the scalar entry point holds one value per cell, so the
        // projection has nothing to read. Checked before the outcome policy
        // because it is the one that would otherwise read out of bounds.
        if (view.field != Field::Velocity) {
            for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
                const std::optional<CfdBoundaryTreatment> treatment =
                    boundary.treatment(BoundaryFaceIndex::fromValue(b));
                if (treatment.has_value() &&
                    view.policyOf(*treatment) == CfdBoundaryPolicy::Symmetry) {
                    return refuse(CfdGradientProblem::SymmetryNeedsAVectorField, message, [&] {
                        return std::format("boundary face {} gives the {} the Symmetry policy, which "
                                           "decomposes a vector and has no meaning for a scalar field",
                                           b, view.name());
                    });
                }
            }
        }

        // PHASE 7 -- an outcome policy. Reported AFTER a missing value, so a
        // boundary with both faults reports the earlier phase.
        for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
            const std::optional<CfdBoundaryTreatment> treatment =
                boundary.treatment(BoundaryFaceIndex::fromValue(b));
            if (treatment.has_value() && view.policyOf(*treatment) == CfdBoundaryPolicy::Computed) {
                return refuse(CfdGradientProblem::VariablePolicyIsAnOutcome, message, [&] {
                    return std::format("boundary face {} gives the {} the Computed policy, which is an "
                                       "outcome of a solve and not a gradient boundary condition",
                                       b, view.name());
                });
            }
        }

        // PHASE 8 -- least squares only: every neighbourhood must determine a
        // gradient. LAST OF THE INPUT PHASES, because it is the only one that
        // costs a pass over the geometry, and it calls the SAME normalSystem the
        // assembly will. The ninth phase judges the OUTPUT and so lives in the
        // assembly.
        if (method != CfdGradientMethod::LeastSquares) {
            return std::nullopt;
        }
        for (std::size_t c = 0; c < topology.cells(); ++c) {
            const System system = normalSystem(topology, geometry, boundary, view, c);
            const double ratio = eigenvalueRatio(system.m);
            if (!(ratio <= kCfdGradientConditioningLimit)) {
                return refuse(CfdGradientProblem::NeighbourhoodIsDegenerate, message, [&] {
                    return std::format("cell {}'s neighbourhood does not determine a gradient: its normal "
                                       "matrix has an extreme eigenvalue ratio of {}, against the {} a "
                                       "solve can carry",
                                       c, ratio, kCfdGradientConditioningLimit);
                });
            }
            // THE TWO CAUSES ARE REPORTED SEPARATELY NOW. `solveSymmetric`
            // returns empty for a non-positive determinant OR a non-finite
            // solution, and this collapsed both into the degenerate tag -- so an
            // input with two cell pressures near 1.8e308, whose normal matrix is
            // perfectly healthy, was refused with a message reading "an extreme
            // eigenvalue ratio of 3, against the 1e+12 a solve can carry". A
            // diagnostic that refutes itself, and that named the mesh when the
            // fault was the field. A review found it.
            if (!solveSymmetric(system.m, system.rhs).has_value()) {
                return refuse(CfdGradientProblem::GradientIsNotFinite, message, [&] {
                    return std::format("cell {}'s normal system has a usable eigenvalue ratio of {} but "
                                       "no finite solution, so the {} values it was given do not "
                                       "determine a finite gradient",
                                       c, ratio, view.name());
                });
            }
        }
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // The assembly
    // -----------------------------------------------------------------------

    template <typename Q>
    [[nodiscard]] static Result<CfdGradientField<Q>> build(const CfdTopology& topology,
                                                           const CfdGeometry& geometry,
                                                           const PreparedCfdBoundary& boundary,
                                                           const View& view, CfdGradientMethod method) {
        std::string message;
        if (problemOf(topology, geometry, boundary, view, method, &message).has_value()) {
            return makeError(ErrorCode::FailedPrecondition, std::move(message));
        }

        const std::size_t cells = topology.cells();
        CfdGradientField<Q> field;
        field.si_.assign(3 * cells, 0.0);
        field.summary_.method = method;
        field.summary_.cells = cells;

        // The policy census, counted off the treatments this field actually
        // reads rather than predicted from the condition kinds.
        for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
            const std::optional<CfdBoundaryTreatment> treatment =
                boundary.treatment(BoundaryFaceIndex::fromValue(b));
            if (!treatment.has_value()) {
                continue;
            }
            switch (view.policyOf(*treatment)) {
            case CfdBoundaryPolicy::FixedValue:
                ++field.summary_.fixedValueFaces;
                break;
            case CfdBoundaryPolicy::ZeroGradient:
                ++field.summary_.zeroGradientFaces;
                break;
            case CfdBoundaryPolicy::Symmetry:
                ++field.summary_.symmetryFaces;
                break;
            case CfdBoundaryPolicy::Computed:
                break; // refused in phase 6; unreachable here
            }
        }

        // The constant-mode statement, MEASURED and not assumed from the method
        // alone. Least squares returns exactly zero whatever the mesh, because
        // every right-hand side is a difference of cell values; Green-Gauss does
        // so only where the outward area vectors cancel BIT-exactly, which
        // ADR-065 Decision 2 proves algebraically and its own evidence records
        // as bit-exact on only some inputs.
        // A SYMMETRY FACE BREAKS IT FOR EITHER METHOD, and that clause was
        // missing until a review supplied it. A slip wall's row value is
        // `U_b,i - U_P,i = -n_i (U_P . n)`, which is not zero for a uniform
        // velocity that is not tangential to the wall -- so the flag read `true`
        // on a mesh where a uniform field reconstructs to something non-zero,
        // and the reason given for it was false rather than merely incomplete.
        const bool noSymmetry = field.summary_.symmetryFaces == 0;
        if (method == CfdGradientMethod::LeastSquares) {
            field.summary_.constantModeIsExact = noSymmetry;
        } else {
            bool bitZero = noSymmetry;
            for (std::size_t c = 0; c < cells && bitZero; ++c) {
                const std::optional<AreaVector3D> sum =
                    geometry.cellSurfaceVectorSum(CellIndex::fromValue(c));
                bitZero = sum.has_value() && sum->x.si() == 0.0 && sum->y.si() == 0.0 && sum->z.si() == 0.0;
            }
            field.summary_.constantModeIsExact = bitZero;
        }

        double worstSkewness = 0.0;
        double worstConditioning = 0.0;

        for (std::size_t c = 0; c < cells; ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            Vec gradient{};

            if (method == CfdGradientMethod::GreenGauss) {
                Vec accumulated{};
                for (const FaceIndex face : topology.cellFaces(cell)) {
                    const CellIndex owner = topology.owner(face);
                    const Vec stored = siOf(geometry.faceAreaVector(face));
                    // The stored vector is wound OUT OF THE OWNER, so a cell that
                    // merely neighbours the face must negate it. CfdGeometry's own
                    // header warns that summing a cell's four stored vectors
                    // WITHOUT consulting `owner` leaves a residual of order one,
                    // not of order eps -- so the sign is read, never assumed.
                    const Vec outward = owner.value() == c ? stored : scaledBy(stored, -1.0);
                    const std::optional<double> value =
                        faceValue(topology, geometry, boundary, view, face, owner, worstSkewness);
                    if (!value.has_value()) {
                        // REFUSED, NOT SKIPPED. This used to `continue`, which
                        // left a face out of `sum_f phi_f S_f` while still
                        // dividing by the WHOLE volume -- a wrong gradient
                        // returned as a success, two lines above a zero-volume
                        // case that is refused by name. A review pointed out the
                        // inconsistency inside one function. Unreachable while
                        // the gate holds, and recorded as such.
                        return makeError(
                            ErrorCode::FailedPrecondition,
                            std::format("cell {}'s face {} has no usable value, so the surface sum "
                                        "would be taken over fewer faces than the volume it divides",
                                        c, face.value()));
                    }
                    addScaled(accumulated, outward, *value);
                }
                const double volume = geometry.cellVolume(cell).si();
                if (!(volume > 0.0)) {
                    // A zero volume means this geometry does not hold the cell,
                    // by CfdGeometry's own documented contract -- so this is the
                    // currency failure phase 1 checks, surfacing late. Refused
                    // rather than left as a silent zero gradient. Unreachable
                    // while `describes` holds, and recorded as such.
                    return makeError(ErrorCode::FailedPrecondition,
                                     std::format("cell {} has no volume in these metrics, so they do not "
                                                 "describe this topology after all",
                                                 c));
                }
                gradient = scaledBy(accumulated, 1.0 / volume);
            } else {
                const System system = normalSystem(topology, geometry, boundary, view, c);
                worstConditioning = std::max(worstConditioning, eigenvalueRatio(system.m));
                const std::optional<Vec> solved = solveSymmetric(system.m, system.rhs);
                if (!solved.has_value()) {
                    // The gate ran the SAME normalSystem and the same solve, so
                    // reaching here means the two disagreed. Refused rather than
                    // left as a silent zero: a zero gradient is a legitimate
                    // answer elsewhere, so it must never double as a failure.
                    return makeError(ErrorCode::FailedPrecondition,
                                     std::format("cell {}'s normal system passed the gate and then failed "
                                                 "to solve, which is an internal inconsistency",
                                                 c));
                }
                gradient = *solved;
            }

            // PHASE 9, AND THE ONLY ONE THAT JUDGES THE ANSWER. A review
            // found that nothing between the accumulation and this store
            // checked the result: phase 4 requires every cell VALUE to be
            // finite, which admits 1.8e308, so a surface sum can overflow to
            // infinities of each sign, cancel to a NaN, pass `volume > 0`, and
            // reach a caller through `at()` with no error at all. A tiny volume
            // reaches the same place by division. `thermal` and `structural`
            // both refuse a non-finite gradient rather than propagate it.
            if (!std::isfinite(gradient[0]) || !std::isfinite(gradient[1]) ||
                !std::isfinite(gradient[2])) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("the reconstructed {} gradient at cell {} is ({}, {}, {}), "
                                             "which is not finite",
                                             view.name(), c, gradient[0], gradient[1], gradient[2]));
            }
            field.si_[3 * c + 0] = gradient[0];
            field.si_[3 * c + 1] = gradient[1];
            field.si_[3 * c + 2] = gradient[2];
        }

        field.summary_.worstSkewness = worstSkewness;
        field.summary_.worstConditioning = worstConditioning;
        return field;
    }

    /// The value Green-Gauss uses at @p face, raising @p worstSkewness for what
    /// it paid.
    ///
    /// THE SKEWNESS IS RAISED ONLY ON AN INTERNAL FACE, because that is the only
    /// place a value is interpolated: a `FixedValue` facet gives the exact value
    /// AT the face centroid, so Green-Gauss is exact there. That is why a
    /// Dirichlet-dominated mesh flatters it, and why the skew comparison has to
    /// run on a mesh whose internal faces carry the error (ADR-070 Decision 4).
    [[nodiscard]] static std::optional<double> faceValue(const CfdTopology& topology,
                                                         const CfdGeometry& geometry,
                                                         const PreparedCfdBoundary& boundary,
                                                         const View& view, FaceIndex face, CellIndex owner,
                                                         double& worstSkewness) {
        if (topology.isInternal(face)) {
            const std::optional<CellIndex> neighbour = topology.neighbour(face);
            if (!neighbour.has_value()) {
                return std::nullopt;
            }
            if (const std::optional<double> skew = geometry.skewness(face); skew.has_value()) {
                worstSkewness = std::max(worstSkewness, std::abs(*skew));
            }
            // THE WEIGHT IS THE OWNER'S, so the face has ONE value whichever of
            // its two cells is asking -- which is what makes a face's
            // contribution equal and opposite across it without a second visit
            // to compare against.
            const double weight = interpolationWeight(geometry, face);
            const double ownerValue = view.at(owner.value());
            const double neighbourValue = view.at(neighbour->value());
            // `phi_N + w (phi_P - phi_N)`, not `w phi_P + (1 - w) phi_N`: the
            // first is EXACTLY phi when the two are equal and the second is not,
            // which is the defect P19-DIFF-001's own test caught in its face
            // viscosity, where `w mu + (1 - w) mu` did not reproduce mu.
            return neighbourValue + weight * (ownerValue - neighbourValue);
        }
        const std::optional<BoundaryFaceIndex> boundaryFace = topology.boundaryFaceOf(face);
        const std::optional<CfdBoundaryTreatment> treatment =
            boundaryFace.has_value() ? boundary.treatment(*boundaryFace) : std::nullopt;
        const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
        if (!treatment.has_value() || !normal.has_value()) {
            return std::nullopt;
        }
        const Vec unitNormal{normal->x(), normal->y(), normal->z()};
        return boundaryValue(boundary, view, *boundaryFace, view.policyOf(*treatment), owner.value(),
                             unitNormal);
    }

    [[nodiscard]] static View pressureView(const CfdTopology& topology,
                                           const CfdGradientInput& input) noexcept {
        return View{Field::Pressure, VectorComponent::X, input.cellValue, topology.cells()};
    }

    [[nodiscard]] static View velocityView(const CfdTopology& topology,
                                           const CfdVelocityGradientInput& input) noexcept {
        return View{Field::Velocity, input.component, input.currentVelocity, topology.cells()};
    }
};

// ---------------------------------------------------------------------------
// CfdGradientField
// ---------------------------------------------------------------------------

template <typename Q>
std::optional<typename CfdGradientField<Q>::Vector> CfdGradientField<Q>::at(CellIndex cell) const noexcept {
    if (!cell.isValid() || cell.value() >= summary_.cells) {
        return std::nullopt;
    }
    const std::size_t base = 3 * cell.value();
    return Vector{Q::fromSi(si_[base + 0]), Q::fromSi(si_[base + 1]), Q::fromSi(si_[base + 2])};
}

template <typename Q>
Q CfdGradientField<Q>::largestMagnitude() const noexcept {
    double worst = 0.0;
    for (std::size_t c = 0; c < summary_.cells; ++c) {
        worst = std::max(worst, std::hypot(si_[3 * c + 0], si_[3 * c + 1], si_[3 * c + 2]));
    }
    return Q::fromSi(worst);
}

template class BETTERCAD_CFD_EXPORT CfdGradientField<PressureGradient>;
template class BETTERCAD_CFD_EXPORT CfdGradientField<VelocityGradient>;

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

std::string_view toString(CfdGradientMethod method) noexcept {
    switch (method) {
    case CfdGradientMethod::GreenGauss:
        return "green_gauss";
    case CfdGradientMethod::LeastSquares:
        return "least_squares";
    }
    return "unknown";
}

std::string_view toString(CfdGradientProblem problem) noexcept {
    switch (problem) {
    case CfdGradientProblem::GeometryDescribesAnotherTopology:
        return "geometry_describes_another_topology";
    case CfdGradientProblem::BoundaryDescribesAnotherTopology:
        return "boundary_describes_another_topology";
    case CfdGradientProblem::CellValueLengthWrong:
        return "cell_value_length_wrong";
    case CfdGradientProblem::CellValueNotFinite:
        return "cell_value_not_finite";
    case CfdGradientProblem::PrescribedValueMissing:
        return "prescribed_value_missing";
    case CfdGradientProblem::SymmetryNeedsAVectorField:
        return "symmetry_needs_a_vector_field";
    case CfdGradientProblem::VariablePolicyIsAnOutcome:
        return "variable_policy_is_an_outcome";
    case CfdGradientProblem::NeighbourhoodIsDegenerate:
        return "neighbourhood_is_degenerate";
    case CfdGradientProblem::GradientIsNotFinite:
        return "gradient_is_not_finite";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// The four public entry points
// ---------------------------------------------------------------------------

Result<CfdPressureGradientField> reconstructCfdPressureGradient(const CfdTopology& topology,
                                                                const CfdGeometry& geometry,
                                                                const PreparedCfdBoundary& boundary,
                                                                const CfdGradientInput& input) {
    return CfdGradientBuilder::build<PressureGradient>(
        topology, geometry, boundary, CfdGradientBuilder::pressureView(topology, input), input.method);
}

Result<CfdVelocityGradientField> reconstructCfdVelocityGradient(const CfdTopology& topology,
                                                                const CfdGeometry& geometry,
                                                                const PreparedCfdBoundary& boundary,
                                                                const CfdVelocityGradientInput& input) {
    return CfdGradientBuilder::build<VelocityGradient>(
        topology, geometry, boundary, CfdGradientBuilder::velocityView(topology, input), input.method);
}

std::optional<CfdGradientProblem> cfdPressureGradientProblem(const CfdTopology& topology,
                                                             const CfdGeometry& geometry,
                                                             const PreparedCfdBoundary& boundary,
                                                             const CfdGradientInput& input) noexcept {
    // `nullptr` for the message, so this path allocates nothing.
    return CfdGradientBuilder::problemOf(topology, geometry, boundary,
                                         CfdGradientBuilder::pressureView(topology, input), input.method,
                                         nullptr);
}

std::optional<CfdGradientProblem> cfdVelocityGradientProblem(const CfdTopology& topology,
                                                             const CfdGeometry& geometry,
                                                             const PreparedCfdBoundary& boundary,
                                                             const CfdVelocityGradientInput& input) noexcept {
    return CfdGradientBuilder::problemOf(topology, geometry, boundary,
                                         CfdGradientBuilder::velocityView(topology, input), input.method,
                                         nullptr);
}

} // namespace bettercad::cfd
