/// \file
/// P19-PVCOUPLE-001 — the SIMPLE pressure–velocity coupling. ADR-075.
///
/// THE SHAPE OF THIS FILE. A gate that is total over its inputs and reports the
/// earliest phase that fails; a pressure-correction assembly whose loop visits
/// each face exactly once; the two corrections, each one face loop; a predictor
/// that relaxes and hands three systems to a SUPPLIED solve; and one outer
/// iteration that calls qualified operators for everything already qualified.
///
/// WHAT IS CALLED RATHER THAN RE-DERIVED. `cfdMassFlux` makes `phi`
/// (`ADR-072`), `evaluateCfdContinuity` measures its closure (`ADR-074`),
/// `cfdFaceInterpolationWeight` gives the face weight (`ADR-069` Decision 7),
/// and `assembleCfdMomentum` built the three systems before this file was
/// entered. Each had three private copies in this module at some point, which
/// is why calling them is a rule and not a preference.
///
/// WHAT THIS FILE WRITES A THIRD TIME, AND WHY. The compressed-sparse-row
/// symbolic pass. `CfdDiffusion.cpp` and `CfdConvection.cpp` each hold a
/// private `Layout`, and extracting a shared one would mean editing two
/// QUALIFIED operators to serve this milestone — the shape of extraction
/// `ADR-063` Decision 7 refused and `ADR-074` Decision 1 refused again. It is
/// recorded as a limitation rather than defended as a design.

#include <bettercad/cfd/CfdCoupling.hpp>

#include <bettercad/cfd/CfdContinuity.hpp>
#include <bettercad/cfd/CfdConvection.hpp>
#include <bettercad/cfd/CfdDiffusion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace bettercad::cfd {

namespace {

using Vec = std::array<double, 3>;

[[nodiscard]] Vec siOf(const AreaVector3D& v) noexcept {
    return {v.x.si(), v.y.si(), v.z.si()};
}

/// An input the caller got wrong.
[[nodiscard]] CfdCouplingFailure failureOf(CfdCouplingProblem problem, std::string message) {
    return CfdCouplingFailure{problem, Error{ErrorCode::InvalidArgument, std::move(message)}};
}

/// A fault in THIS MODULE rather than in the caller's input.
///
/// A SEPARATE CODE, as `assembleCfdDiffusion`, `assembleCfdMomentum` and
/// `evaluateCfdContinuity` all have: `InvalidArgument` for an input and
/// `Internal` for an internal inconsistency. `ResultIsNotFinite` and
/// `EvaluationIsInconsistent` judge this file's own arrays, so blaming the
/// caller for one would be the category error `P19-MOM-001`'s review found in
/// its own first draft.
[[nodiscard]] CfdCouplingFailure internalFailure(CfdCouplingProblem problem, std::string message) {
    return CfdCouplingFailure{problem, Error{ErrorCode::Internal, std::move(message)}};
}

/// A refusal that builds its message only when a caller wants one, which is
/// what makes the `noexcept` predicate a fact rather than a hope.
template <typename Describe>
[[nodiscard]] std::optional<CfdCouplingProblem> refuse(CfdCouplingProblem problem, std::string* message,
                                                       Describe describe) {
    if (message != nullptr) {
        *message = describe();
    }
    return problem;
}

/// `|a - b| / scale`, and exactly `0.0` when there is no scale to divide by.
///
/// NO FLOOR, AND ITS ABSENCE IS DELIBERATE, as `linalg::ResidualNorms` argues
/// for the same shape: a vanishing scale means both quantities vanished, and
/// reporting `0 / floor` would flatter a measurement rather than make one.
[[nodiscard]] double relativeDeparture(double a, double b, double scale) noexcept {
    if (!(scale > 0.0)) {
        return a == b ? 0.0 : std::abs(a - b);
    }
    return std::abs(a - b) / scale;
}

/// `||A||_inf`, the largest absolute row sum.
///
/// NOT `rowSumSi`, WHICH IS SIGNED. A signed row sum of a conservative operator
/// is near zero by construction, so using it as a matrix norm would divide a
/// residual by almost nothing and report every solve as catastrophic.
[[nodiscard]] double infinityNorm(const CfdSparseMatrix& matrix) noexcept {
    const std::span<const CfdSparseIndex> rowStart = matrix.rowStart();
    const std::span<const double> values = matrix.values();
    double worst = 0.0;
    for (std::size_t row = 0; row + 1 < rowStart.size(); ++row) {
        double sum = 0.0;
        for (CfdSparseIndex slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            sum += std::abs(values[static_cast<std::size_t>(slot)]);
        }
        worst = std::max(worst, sum);
    }
    return worst;
}

[[nodiscard]] double euclidean(std::span<const double> v) noexcept {
    double sum = 0.0;
    for (const double x : v) {
        sum += x * x;
    }
    return std::sqrt(sum);
}

/// The normalised residual of `A x = b`, in `ADR-052`'s form:
///
/// ```text
///     ||A x - b||_2 / (||A||_inf ||x||_2 + ||b||_2)
/// ```
///
/// THE SAME DEFINITION THE PROJECT ALREADY USES, recomputed here because `cfd`
/// does not link `linalg` (ADR-075 Decision 1). Choosing a DIFFERENT
/// normalisation would be worse than recomputing this one: `CfdResiduals` is
/// documented as normalised and dimensionless, and two normalisations in one
/// project is the defect `ADR-069` Decision 7 was written about.
///
/// Returns a non-finite value when the arithmetic produces one, so a caller can
/// refuse it; it never silently reports zero.
[[nodiscard]] double normalisedResidual(const CfdSparseMatrix& matrix, std::span<const double> rhs,
                                        std::span<const double> x) {
    const Result<std::vector<double>> product = matrix.multiply(x);
    if (!product.has_value()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::vector<double> residual = *product;
    for (std::size_t i = 0; i < residual.size(); ++i) {
        residual[i] -= rhs[i];
    }
    const double denominator = infinityNorm(matrix) * euclidean(x) + euclidean(rhs);
    const double numerator = euclidean(residual);
    if (!(denominator > 0.0)) {
        return numerator == 0.0 ? 0.0 : std::numeric_limits<double>::infinity();
    }
    return numerator / denominator;
}

[[nodiscard]] bool allFinite(std::span<const double> v) noexcept {
    return std::ranges::all_of(v, [](double x) { return std::isfinite(x); });
}

[[nodiscard]] std::optional<std::size_t> firstNonFinite(std::span<const double> v) noexcept {
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!std::isfinite(v[i])) {
            return i;
        }
    }
    return std::nullopt;
}

/// The compressed-sparse-row pattern, and a slot lookup within it.
///
/// A THIRD COPY. See this file's header comment: the two earlier ones belong to
/// qualified operators.
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

/// The project's interpolation form, `N + w (P - N)`, which is `ADR-069`
/// Decision 7's and `CfdConvection.cpp`'s.
///
/// IT NEVER COMPUTES `1 - w`, which is the whole point: `ADR-075` Q1 measured
/// `1 - w` as round-tripping exactly on only 68.2728% of real weights.
[[nodiscard]] double interpolate(double owner, double neighbour, double weight) noexcept {
    return neighbour + weight * (owner - neighbour);
}

/// Whether `ADR-067`'s table fixes the PRESSURE at @p treatment.
///
/// The pressure-correction boundary condition, and nothing else, is read from
/// this. `FixedValue` means `p' = 0` at the facet; `ZeroGradient` means the face
/// contributes nothing at all, because the flux there is prescribed and cannot
/// be corrected. `Symmetry` never appears in the pressure column — the table
/// gives it only to a slip wall's velocity — and is treated as zero-gradient,
/// which is what a wall is.
[[nodiscard]] bool fixesPressureLevel(const CfdBoundaryTreatment& treatment) noexcept {
    return treatment.pressure == CfdBoundaryPolicy::FixedValue;
}

/// The three component diagonals at @p cell, from a component-blocked array.
[[nodiscard]] Vec diagonalAt(std::span<const double> blocked, std::size_t cells,
                             std::size_t cell) noexcept {
    return {blocked[cell], blocked[cells + cell], blocked[2 * cells + cell]};
}

/// `d^i = V / a^i`, the three SIMPLE coefficients at one cell.
[[nodiscard]] Vec coefficientsAt(const CfdGeometry& geometry, std::span<const double> diagonal,
                                 std::size_t cells, std::size_t cell) noexcept {
    const double volume = geometry.cellVolume(CellIndex::fromValue(cell)).si();
    const Vec a = diagonalAt(diagonal, cells, cell);
    return {volume / a[0], volume / a[1], volume / a[2]};
}

/// The connected regions of a mesh, by internal-face adjacency, with what each
/// region's boundary prescribes.
///
/// ONE PIN FIXES ONE REGION. A mesh with two disconnected fluid regions needs
/// each region's pressure level fixed separately, and the global flags an
/// earlier draft used could not express that.
struct Regions {
    /// The representative cell of each cell's region, one per cell.
    std::vector<std::size_t> root{};
    /// Whether that region has a boundary face that FIXES the pressure, indexed
    /// by root.
    std::vector<bool> fixesPressure{};
    /// The region's net boundary flux and the sum of its magnitudes, in kg/s,
    /// indexed by root.
    std::vector<double> net{};
    std::vector<double> magnitude{};
};

/// MUST RUN AFTER THE RANGE GUARD: it indexes by `owner` and `neighbour`.
[[nodiscard]] Regions regionsOf(const CfdTopology& topology, const PreparedCfdBoundary& boundary,
                                std::span<const double> faceFlux) {
    const std::size_t cells = topology.cells();
    Regions out;
    out.root.resize(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        out.root[c] = c;
    }
    const auto find = [&out](std::size_t x) {
        while (out.root[x] != x) {
            out.root[x] = out.root[out.root[x]];
            x = out.root[x];
        }
        return x;
    };
    for (std::size_t f = 0; f < topology.internalFaces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        if (!neighbour.has_value()) {
            continue;
        }
        const std::size_t a = find(topology.owner(face).value());
        const std::size_t b = find(neighbour->value());
        if (a != b) {
            out.root[a] = b;
        }
    }
    // Flatten, so `root[c]` is the representative and not a link.
    for (std::size_t c = 0; c < cells; ++c) {
        out.root[c] = find(c);
    }

    out.fixesPressure.assign(cells, false);
    out.net.assign(cells, 0.0);
    out.magnitude.assign(cells, 0.0);
    for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const FaceIndex face = topology.faceOf(boundaryFace);
        const std::size_t root = out.root[topology.owner(face).value()];
        const double phi = faceFlux[face.value()];
        out.net[root] += phi;
        out.magnitude[root] += std::abs(phi);
        const std::optional<CfdBoundaryTreatment> treatment = boundary.treatment(boundaryFace);
        if (treatment.has_value() && fixesPressureLevel(*treatment)) {
            out.fixesPressure[root] = true;
        }
    }
    return out;
}

/// THE decision path, shared by the assembly and its predictor. The message is
/// built only when a caller passes somewhere to put it.
///
/// `PressureMatrixNotSymmetric`, `ResultIsNotFinite` and
/// `EvaluationIsInconsistent` are NOT decided here: they judge arrays this
/// function never builds, which is the limitation every cfd operator since
/// `P19-DIFF-001` has recorded.
[[nodiscard]] std::optional<CfdCouplingProblem>
pressureCorrectionProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                          const PreparedCfdBoundary& boundary, const CfdPressureCorrectionInput& input,
                          std::string* message) noexcept {
    // PHASE 1 -- the three descriptions must be of one mesh.
    if (!geometry.describes(topology)) {
        return refuse(CfdCouplingProblem::GeometryDescribesAnotherTopology, message, [&] {
            return std::string{"the finite-volume metrics were built for a different mesh or "
                               "geometry revision than the topology"};
        });
    }
    if (boundary.boundaryFaces() != topology.boundaryFaces()) {
        return refuse(CfdCouplingProblem::BoundaryDescribesAnotherTopology, message, [&] {
            return std::format("the prepared boundary covers {} faces and the topology has {}",
                               boundary.boundaryFaces(), topology.boundaryFaces());
        });
    }

    const std::size_t cells = topology.cells();
    const std::size_t faces = topology.faces();

    // PHASE 2 -- EVERY FACE MUST NAME CELLS THIS TOPOLOGY HOLDS.
    //
    // THIS GUARDS WRITES, AND ITS ABSENCE WAS THE SHARPEST FINDING OF THIS
    // MILESTONE'S OWN CORRECTNESS REVIEW. `CfdTopology::owner` is invalid for a
    // face the topology does not hold and `CellIndex::kInvalid` is `SIZE_MAX`;
    // the symbolic pattern, the right-hand side and the per-cell face sums are
    // all written THROUGH that index. `geometry.describes` compares the mesh
    // stamp and the counts and never the owner and neighbour ranges, so nothing
    // upstream establishes it.
    //
    // `P19-DIV-001`'s review and then `P19-CONV-001`'s review each found an
    // unguarded `SIZE_MAX` reaching writes, and `CfdConvection.hpp` records "It
    // is not regressing a third time." It regressed a third time, here, and is
    // fixed before the gate.
    for (std::size_t f = 0; f < faces; ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const CellIndex owner = topology.owner(face);
        if (!owner.isValid() || owner.value() >= cells) {
            return refuse(CfdCouplingProblem::TopologyNamesAnUnknownCell, message, [&] {
                return std::format("face {} names owner cell {} and the topology holds {} cells", f,
                                   owner.value(), cells);
            });
        }
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        if (neighbour.has_value() && (!neighbour->isValid() || neighbour->value() >= cells)) {
            return refuse(CfdCouplingProblem::TopologyNamesAnUnknownCell, message, [&] {
                return std::format("face {} names neighbour cell {} and the topology holds {} cells",
                                   f, neighbour->value(), cells);
            });
        }
    }

    // PHASE 3 -- lengths.
    if (input.predictedFaceFlux.size() != faces) {
        return refuse(CfdCouplingProblem::ValueLengthWrong, message, [&] {
            return std::format("the predicted face flux has {} values and the topology has {} faces",
                               input.predictedFaceFlux.size(), faces);
        });
    }
    if (input.momentumDiagonal.size() != 3 * cells) {
        return refuse(CfdCouplingProblem::ValueLengthWrong, message, [&] {
            return std::format("the momentum diagonal has {} values and {} cells need {} "
                               "component-blocked entries",
                               input.momentumDiagonal.size(), cells, 3 * cells);
        });
    }

    // PHASE 4 -- finiteness and usability of the inputs.
    if (const std::optional<std::size_t> bad = firstNonFinite(input.predictedFaceFlux)) {
        return refuse(CfdCouplingProblem::ValueNotFinite, message, [&] {
            return std::format("the predicted face flux at face {} is not finite", *bad);
        });
    }
    if (const std::optional<std::size_t> bad = firstNonFinite(input.momentumDiagonal)) {
        return refuse(CfdCouplingProblem::ValueNotFinite, message, [&] {
            return std::format("the momentum diagonal at component-blocked index {} is not finite",
                               *bad);
        });
    }
    if (!std::isfinite(input.density.si()) || !(input.density.si() > 0.0)) {
        return refuse(CfdCouplingProblem::ValueNotFinite, message, [&] {
            return std::format("the density is {} kg/m^3 and must be finite and positive",
                               input.density.si());
        });
    }

    // PHASE 5 -- a diagonal `d_P = V_P / a_P` can divide by.
    //
    // REACHABLE FROM A CORRECT ASSEMBLY, which is why the message says what it
    // says. Upwind convection contributes a negative `phi` to the diagonal at a
    // zero-gradient-velocity boundary and diffusion contributes nothing there,
    // so a convection-dominated cell beside a pressure boundary can assemble a
    // non-positive diagonal with nothing wrong anywhere. SIMPLE then has no
    // coefficient; it is a hard problem and not the caller's mistake.
    for (std::size_t i = 0; i < input.momentumDiagonal.size(); ++i) {
        if (!(input.momentumDiagonal[i] > 0.0)) {
            return refuse(CfdCouplingProblem::MomentumDiagonalNotPositive, message, [&] {
                return std::format("the momentum diagonal at component-blocked index {} is {} kg/s "
                                   "and d_P = V_P / a_P needs it strictly positive",
                                   i, input.momentumDiagonal[i]);
            });
        }
    }

    // PHASE 6 -- every internal face that is interpolated must have a weight.
    //
    // REFUSED, NOT DEFAULTED. P19-DIV-001's review found a transcription that
    // returned 0.5 on a degenerate face.
    for (std::size_t f = 0; f < topology.internalFaces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        if (!cfdFaceInterpolationWeight(geometry, face).has_value()) {
            return refuse(CfdCouplingProblem::FaceWeightUnavailable, message, [&] {
                return std::format("internal face {} has no interpolation weight: its orthogonal "
                                   "distance is not positive, or it has no unit normal",
                                   f);
            });
        }
        if (!topology.neighbour(face).has_value()) {
            return refuse(CfdCouplingProblem::BoundaryDescribesAnotherTopology, message, [&] {
                return std::format("face {} is numbered internal and has no neighbour", f);
            });
        }
    }

    // PHASE 7 -- EVERY CONNECTED REGION'S PRESSURE LEVEL MUST BE FIXED, not
    // merely the mesh's.
    //
    // THE FLAGS USED TO BE GLOBAL AND THE PIN FIXES ONE CELL, which its own
    // adversarial review showed is not enough: on a mesh with two disconnected
    // fluid regions, a pressure boundary on region A leaves region B's constant
    // mode free, and `A` is then singular whatever the summary says. The
    // precondition `linalg::solveSymmetricSparse` needs is positive
    // definiteness, so a free mode is a refusal and not a diagnostic.
    const Regions regions = regionsOf(topology, boundary, input.predictedFaceFlux);
    const std::optional<CellIndex> reference = boundary.referenceCell();
    const std::optional<std::size_t> referenceRegion =
        reference.has_value() ? std::optional<std::size_t>{regions.root[reference->value()]}
                              : std::nullopt;
    for (std::size_t c = 0; c < cells; ++c) {
        const std::size_t root = regions.root[c];
        if (regions.fixesPressure[root]) {
            continue;
        }
        if (referenceRegion.has_value() && *referenceRegion == root) {
            continue;
        }
        return refuse(CfdCouplingProblem::PressureLevelNotFixed, message, [&] {
            return std::format(
                "the connected region containing cell {} has no boundary that fixes the pressure "
                "level, and the prepared boundary's reference cell {} it to pin: the "
                "pressure-correction system is singular in that region's constant mode",
                c,
                reference.has_value()
                    ? std::format("({}) lies in another region, so there is nothing in", reference->value())
                    : std::string{"is absent, so there is nothing in"});
        });
    }

    // PHASE 8 -- a region whose pressure level is PINNED rather than fixed by a
    // boundary is pure Neumann, and its prescribed flux must balance or
    // `A p' = b` has no solution in that region at all.
    //
    // PER REGION, AND PER REGION IS WHAT MAKES `relativeDeparture`'S OWN
    // JUSTIFICATION TRUE HERE. The global form divided one region's net by
    // another region's throughput, and compared a sum over ALL cells with a
    // scale from the boundary alone -- so the two did not vanish together and
    // the "relative" residual silently became an absolute kg/s number. A
    // region's net and its own throughput do vanish together: both are sums
    // over the same faces.
    for (std::size_t c = 0; c < cells; ++c) {
        const std::size_t root = regions.root[c];
        if (root != c || regions.fixesPressure[root]) {
            continue;
        }
        const double net = regions.net[root];
        const double throughput = 0.5 * regions.magnitude[root];
        const double residual = relativeDeparture(net, 0.0, throughput);
        if (residual > kCfdCouplingCompatibilityTolerance) {
            return refuse(CfdCouplingProblem::PrescribedFluxUnbalanced, message, [&] {
                return std::format("every boundary of the region containing cell {} prescribes the "
                                   "flux, so that region is pure Neumann and its right-hand side "
                                   "must sum to zero; its net boundary flux is {} kg/s against a "
                                   "throughput of {} kg/s, a relative {} above the bound {}",
                                   root, net, throughput, residual,
                                   kCfdCouplingCompatibilityTolerance);
            });
        }
    }

    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

std::string_view toString(CfdCouplingProblem problem) noexcept {
    switch (problem) {
    case CfdCouplingProblem::GeometryDescribesAnotherTopology:
        return "GeometryDescribesAnotherTopology";
    case CfdCouplingProblem::BoundaryDescribesAnotherTopology:
        return "BoundaryDescribesAnotherTopology";
    case CfdCouplingProblem::TopologyNamesAnUnknownCell:
        return "TopologyNamesAnUnknownCell";
    case CfdCouplingProblem::ValueLengthWrong:
        return "ValueLengthWrong";
    case CfdCouplingProblem::ValueNotFinite:
        return "ValueNotFinite";
    case CfdCouplingProblem::SettingsInvalid:
        return "SettingsInvalid";
    case CfdCouplingProblem::MomentumSystemsWrong:
        return "MomentumSystemsWrong";
    case CfdCouplingProblem::MomentumDiagonalNotPositive:
        return "MomentumDiagonalNotPositive";
    case CfdCouplingProblem::FaceWeightUnavailable:
        return "FaceWeightUnavailable";
    case CfdCouplingProblem::PressureLevelNotFixed:
        return "PressureLevelNotFixed";
    case CfdCouplingProblem::PrescribedFluxUnbalanced:
        return "PrescribedFluxUnbalanced";
    case CfdCouplingProblem::PressureMatrixNotSymmetric:
        return "PressureMatrixNotSymmetric";
    case CfdCouplingProblem::LinearSolveRefused:
        return "LinearSolveRefused";
    case CfdCouplingProblem::LinearSolutionUnusable:
        return "LinearSolutionUnusable";
    case CfdCouplingProblem::ResultIsNotFinite:
        return "ResultIsNotFinite";
    case CfdCouplingProblem::EvaluationIsInconsistent:
        return "EvaluationIsInconsistent";
    }
    // An enumerator this build does not define. `EvaluationIsInconsistent` is
    // the honest fallback: an unknown problem IS an inconsistency here.
    return "EvaluationIsInconsistent";
}

std::string_view toString(CfdCouplingVerdict verdict) noexcept {
    switch (verdict) {
    case CfdCouplingVerdict::Converged:
        return "Converged";
    case CfdCouplingVerdict::Diverged:
        return "Diverged";
    case CfdCouplingVerdict::Continue:
        return "Continue";
    }
    return "Continue";
}

// ---------------------------------------------------------------------------
// The pressure-correction system
// ---------------------------------------------------------------------------

std::expected<CfdPressureCorrectionSystem, CfdCouplingFailure>
assembleCfdPressureCorrection(const CfdTopology& topology, const CfdGeometry& geometry,
                              const PreparedCfdBoundary& boundary,
                              const CfdPressureCorrectionInput& input) {
    std::string message;
    if (const std::optional<CfdCouplingProblem> problem =
            pressureCorrectionProblem(topology, geometry, boundary, input, &message)) {
        return std::unexpected(failureOf(*problem, std::move(message)));
    }

    const std::size_t cells = topology.cells();
    const std::size_t internal = topology.internalFaces();
    const double rho = input.density.si();

    // The symbolic pass: a diagonal for every cell plus both off-diagonals of
    // every internal face.
    std::vector<std::vector<CfdSparseIndex>> pattern(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        pattern[c].push_back(static_cast<CfdSparseIndex>(c));
    }
    for (std::size_t f = 0; f < internal; ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::size_t owner = topology.owner(face).value();
        const std::size_t neighbour = topology.neighbour(face)->value();
        pattern[owner].push_back(static_cast<CfdSparseIndex>(neighbour));
        pattern[neighbour].push_back(static_cast<CfdSparseIndex>(owner));
    }
    const Layout layout = layoutOf(pattern);
    std::vector<double> values(layout.inner.size(), 0.0);
    std::vector<double> rhs(cells, 0.0);

    CfdPressureCorrectionSummary summary;
    summary.cells = cells;

    // The worst spread between the three component diagonals, reported so a
    // caller can see when the normal projection's isotropic reading is strained.
    for (std::size_t c = 0; c < cells; ++c) {
        const Vec a = diagonalAt(input.momentumDiagonal, cells, c);
        const double hi = std::max({a[0], a[1], a[2]});
        const double lo = std::min({a[0], a[1], a[2]});
        if (hi > 0.0) {
            summary.largestDiagonalSpread = std::max(summary.largestDiagonalSpread, (hi - lo) / hi);
        }
    }

    // ---- THE INTERNAL FACE LOOP, ONCE EACH ------------------------------
    //
    // One `a_f` is computed and written to BOTH rows in this single visit, so
    // the matrix is symmetric whatever the interpolation does. ADR-075 Q1
    // measured a per-cell alternative at 57.3703% bit-identical, worst relative
    // 1.241051e-14, and `linalg::solveSymmetricSparse` requires symmetry.
    for (std::size_t f = 0; f < internal; ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::size_t owner = topology.owner(face).value();
        const std::size_t neighbour = topology.neighbour(face)->value();

        // The gate proved the weight and the neighbour are both there.
        const double weight = *cfdFaceInterpolationWeight(geometry, face);
        const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
        if (!normal.has_value()) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::format("internal face {} has an interpolation weight and no unit normal", f)));
        }
        const Vec n{normal->x(), normal->y(), normal->z()};

        const Vec dOwner = coefficientsAt(geometry, input.momentumDiagonal, cells, owner);
        const Vec dNeighbour = coefficientsAt(geometry, input.momentumDiagonal, cells, neighbour);

        // THE NORMAL-PROJECTED DIAGONAL (ADR-075 Decision 11). `sum_i n_i^2`
        // is 1 for a unit normal, so this reduces exactly to the isotropic
        // case when the three component coefficients agree.
        double effective = 0.0;
        for (std::size_t i = 0; i < 3; ++i) {
            effective += n[i] * n[i] * interpolate(dOwner[i], dNeighbour[i], weight);
        }

        const double area = geometry.faceArea(face).si();
        const double distance = geometry.orthogonalDistance(face).si();
        const double coefficient = rho * effective * area / distance;

        const std::optional<std::size_t> ownerDiagonal = slotOf(layout, owner, owner);
        const std::optional<std::size_t> neighbourDiagonal = slotOf(layout, neighbour, neighbour);
        const std::optional<std::size_t> ownerOff = slotOf(layout, owner, neighbour);
        const std::optional<std::size_t> neighbourOff = slotOf(layout, neighbour, owner);
        if (!ownerDiagonal || !neighbourDiagonal || !ownerOff || !neighbourOff) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::format("the symbolic pass did not reserve the four slots of internal face {}",
                            f)));
        }
        values[*ownerDiagonal] += coefficient;
        values[*neighbourDiagonal] += coefficient;
        values[*ownerOff] -= coefficient;
        values[*neighbourOff] -= coefficient;
        ++summary.internalFacesCoupled;
    }

    // ---- THE BOUNDARY FACE LOOP ----------------------------------------
    //
    // `pressure == FixedValue` gives `p' = 0` at the facet, so the face adds to
    // the owner's diagonal and contributes nothing to the right-hand side.
    // `ZeroGradient` contributes NOTHING AT ALL: the flux is prescribed there
    // and cannot be corrected.
    double boundaryDiagonal = 0.0;
    double boundaryMagnitude = 0.0;
    for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const FaceIndex face = topology.faceOf(boundaryFace);
        const std::size_t owner = topology.owner(face).value();
        boundaryMagnitude += std::abs(input.predictedFaceFlux[face.value()]);

        const std::optional<CfdBoundaryTreatment> treatment = boundary.treatment(boundaryFace);
        if (!treatment.has_value()) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::format("boundary face {} has no treatment although the counts agree", b)));
        }
        if (!fixesPressureLevel(*treatment)) {
            ++summary.boundaryFacesZeroGradient;
            continue;
        }

        const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
        const double distance = geometry.orthogonalDistance(face).si();
        if (!normal.has_value() || !(distance > 0.0)) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::format("boundary face {} fixes the pressure and has no usable normal or "
                            "orthogonal distance",
                            b)));
        }
        const Vec n{normal->x(), normal->y(), normal->z()};
        const Vec d = coefficientsAt(geometry, input.momentumDiagonal, cells, owner);
        double effective = 0.0;
        for (std::size_t i = 0; i < 3; ++i) {
            effective += n[i] * n[i] * d[i];
        }
        const double area = geometry.faceArea(face).si();
        const double coefficient = rho * effective * area / distance;

        const std::optional<std::size_t> diagonal = slotOf(layout, owner, owner);
        if (!diagonal.has_value()) {
            return std::unexpected(
                internalFailure(CfdCouplingProblem::EvaluationIsInconsistent,
                                std::format("the symbolic pass did not reserve cell {}'s diagonal",
                                            owner)));
        }
        values[*diagonal] += coefficient;
        boundaryDiagonal += coefficient;
        ++summary.boundaryFacesFixedPressure;
    }
    summary.boundaryThroughput = 0.5 * boundaryMagnitude;

    // PER REGION, NOT GLOBALLY. A pressure boundary on one disconnected region
    // lifts only THAT region's constant mode, so a global `boundaryDiagonal ==
    // 0.0` would conclude "nothing to pin" while another region's mode was
    // still free. The gate has already refused any region that neither fixes
    // its pressure nor contains the reference cell, so the only region that can
    // still need a pin is the reference cell's own.
    //
    // (void) on the global total: it is kept as a cross-check below rather than
    // as the decision.
    const Regions regions = regionsOf(topology, boundary, input.predictedFaceFlux);
    const std::optional<CellIndex> datum = boundary.referenceCell();
    summary.constantModeWasInTheNullSpace =
        datum.has_value() && !regions.fixesPressure[regions.root[datum->value()]];

    // The global total must agree with the per-region reading on a CONNECTED
    // mesh, which is every mesh `buildCfdTopology` produces from one body. A
    // disagreement means the mesh is disconnected and a boundary fixes the
    // pressure of a region the reference cell is not in -- which the gate
    // admits, and which the per-region reading handles and the global one does
    // not.
    if (summary.constantModeWasInTheNullSpace && boundaryDiagonal != 0.0) {
        summary.pressureFixedInAnotherRegion = true;
    }

    // ---- THE RIGHT-HAND SIDE: `b = -div(phi*)` -------------------------
    //
    // IN FACE ORDER, because ADR-075 Q5 measured a shuffled face order as
    // bit-identical in 0 of 200 trials.
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const double phi = input.predictedFaceFlux[f];
        rhs[topology.owner(face).value()] -= phi;
        if (const std::optional<CellIndex> neighbour = topology.neighbour(face)) {
            rhs[neighbour->value()] += phi;
        }
    }

    // THE COMPATIBILITY RESIDUAL IS PER REGION, AND THE WORST ONE IS REPORTED.
    //
    // The earlier form summed the right-hand side over ALL cells and divided by
    // the BOUNDARY throughput. Those two do not vanish together -- a closed
    // domain with interior flow has a bit-zero throughput and a non-zero
    // accumulation residual -- so `relativeDeparture` fell through to its
    // no-scale branch and put an ABSOLUTE kg/s number into a field the header
    // documents as relative. A region's net and its own throughput are sums
    // over the same faces, so they do vanish together and the measure is
    // relative as documented.
    summary.compatibilityResidual = 0.0;
    for (std::size_t c = 0; c < cells; ++c) {
        if (regions.root[c] != c || regions.fixesPressure[c]) {
            continue;
        }
        summary.compatibilityResidual =
            std::max(summary.compatibilityResidual,
                     relativeDeparture(regions.net[c], 0.0, 0.5 * regions.magnitude[c]));
    }

    // ---- PIN THE PRESSURE LEVEL ----------------------------------------
    //
    // CLEARING THE ROW AND THE COLUMN IS ELIMINATION, NOT REGULARISATION, and
    // it is exact precisely because the pinned value is ZERO: the cleared
    // column would have contributed `A[P][ref] * p'_ref` to every other row,
    // and `p'_ref` is exactly 0, so each remaining row still states
    // `(A_orig p')_P = b_P` unchanged. That is what keeps ADR-075 Decision 5's
    // identity true away from the pinned cell.
    //
    // THE DIAGONAL KEEPS ITS OWN VALUE WHERE IT HAS ONE, rather than becoming
    // 1.0. Either gives `p'_ref = 0 / a = 0` exactly, but `1.0` is a
    // dimensionless number in a matrix whose entries are metre-seconds, and it
    // would wreck the pivot ratio a symmetric kernel measures its conditioning
    // by.
    //
    // IT IS NOT UNCONDITIONAL, AND AN EARLIER COMMENT SAID IT WAS. When the
    // reference cell has neither an internal face nor a fixed-pressure face its
    // diagonal is exactly zero -- a one-cell mesh, or an isolated cell -- and
    // then 1.0 is used, because a zero diagonal is worse than a
    // scale-inconsistent one. `summary.pinnedDiagonalIsUnitValued` reports
    // which happened rather than leaving a reader to infer it.
    if (summary.constantModeWasInTheNullSpace) {
        const std::optional<CellIndex> reference = boundary.referenceCell();
        if (!reference.has_value()) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::string{"the constant mode is in the null space and the gate admitted an "
                            "input with no reference cell"}));
        }
        const std::size_t ref = reference->value();
        if (ref >= cells) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::format("the prepared boundary's reference cell {} is not one of {} cells", ref,
                            cells)));
        }
        const std::optional<std::size_t> refDiagonal = slotOf(layout, ref, ref);
        if (!refDiagonal.has_value()) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::format("the symbolic pass did not reserve the reference cell {}'s diagonal",
                            ref)));
        }
        const double kept = values[*refDiagonal];

        // The row.
        for (CfdSparseIndex slot = layout.rowStart[ref]; slot < layout.rowStart[ref + 1]; ++slot) {
            values[static_cast<std::size_t>(slot)] = 0.0;
        }
        // The column: only the reference cell's own neighbours hold an entry in
        // it, and they are exactly the cells reached by its internal faces.
        for (std::size_t c = 0; c < cells; ++c) {
            if (c == ref) {
                continue;
            }
            if (const std::optional<std::size_t> slot = slotOf(layout, c, ref)) {
                values[*slot] = 0.0;
            }
        }
        summary.pinnedDiagonalIsUnitValued = !(kept > 0.0);
        values[*refDiagonal] = kept > 0.0 ? kept : 1.0;
        rhs[ref] = 0.0;
        summary.pinnedCell = *reference;
    }

    std::expected<CfdSparseMatrix, CfdSparseFailure> matrix =
        CfdSparseMatrix::fromCsr(cells, cells, layout.rowStart, layout.inner, std::move(values),
                                 kCfdCouplingSymmetryTolerance);
    if (!matrix.has_value()) {
        const CfdSparseProblem problem = matrix.error().problem;
        if (problem == CfdSparseProblem::NotSymmetric) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::PressureMatrixNotSymmetric,
                std::format("the assembled pressure-correction matrix is not symmetric: {}",
                            matrix.error().error.message)));
        }
        return std::unexpected(internalFailure(
            CfdCouplingProblem::EvaluationIsInconsistent,
            std::format("the assembled layout was refused as {}: {}", toString(problem),
                        matrix.error().error.message)));
    }
    summary.largestSymmetryError = matrix->largestSymmetryError();

    if (!allFinite(rhs)) {
        return std::unexpected(internalFailure(
            CfdCouplingProblem::ResultIsNotFinite,
            std::string{"the assembled right-hand side holds a non-finite value although every "
                        "input was finite"}));
    }

    return CfdPressureCorrectionSystem{std::move(*matrix), std::move(rhs), summary};
}

std::optional<CfdCouplingFailure>
cfdPressureCorrectionFailure(const CfdTopology& topology, const CfdGeometry& geometry,
                             const PreparedCfdBoundary& boundary,
                             const CfdPressureCorrectionInput& input) {
    std::string message;
    if (const std::optional<CfdCouplingProblem> problem =
            pressureCorrectionProblem(topology, geometry, boundary, input, &message)) {
        return failureOf(*problem, std::move(message));
    }
    return std::nullopt;
}

std::optional<CfdCouplingProblem>
cfdPressureCorrectionProblem(const CfdTopology& topology, const CfdGeometry& geometry,
                             const PreparedCfdBoundary& boundary,
                             const CfdPressureCorrectionInput& input) noexcept {
    // THE ONE CALLER THAT MAKES THE SHARED GATE'S `noexcept` MEAN SOMETHING.
    // Passing nullptr builds no message, so no refusal path allocates.
    return pressureCorrectionProblem(topology, geometry, boundary, input, nullptr);
}

// ---------------------------------------------------------------------------
// The predictor
// ---------------------------------------------------------------------------

std::expected<CfdCouplingPredictor, CfdCouplingFailure>
cfdCouplingPredictor(const CfdTopology& topology, const CfdGeometry& geometry,
                     const PreparedCfdBoundary& boundary, const CfdCouplingPredictorInput& input,
                     const CfdLinearSolve& solve) {
    (void)geometry;
    (void)boundary;

    if (const Result<void> ok = validate(input.iteration); !ok.has_value()) {
        return std::unexpected(failureOf(CfdCouplingProblem::SettingsInvalid, ok.error().message));
    }
    if (const Result<void> ok = validate(input.linearSolver); !ok.has_value()) {
        return std::unexpected(failureOf(CfdCouplingProblem::SettingsInvalid, ok.error().message));
    }
    if (!solve) {
        return std::unexpected(failureOf(CfdCouplingProblem::LinearSolveRefused,
                                         "no linear solve was supplied, and this operator contains "
                                         "none: the solve is P19-LINSOLVE-001's"));
    }

    const std::size_t cells = topology.cells();
    if (input.momentum.size() != kVectorComponents.size()) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::MomentumSystemsWrong,
            std::format("{} momentum systems were supplied and the three components need {}",
                        input.momentum.size(), kVectorComponents.size())));
    }
    for (std::size_t c = 0; c < input.momentum.size(); ++c) {
        const CfdMomentumSystem& system = input.momentum[c];
        if (system.summary.component != kVectorComponents[c]) {
            return std::unexpected(failureOf(
                CfdCouplingProblem::MomentumSystemsWrong,
                std::format("the momentum system at position {} assembles {} and VectorComponent "
                            "order needs {}",
                            c, toString(system.summary.component), toString(kVectorComponents[c]))));
        }
        if (system.matrix.rows() != cells || system.matrix.columns() != cells ||
            system.source.size() != cells) {
            return std::unexpected(failureOf(
                CfdCouplingProblem::MomentumSystemsWrong,
                std::format("the {} momentum system is {}x{} with {} sources and {} cells need a "
                            "square system of that size",
                            toString(kVectorComponents[c]), system.matrix.rows(),
                            system.matrix.columns(), system.source.size(), cells)));
        }
    }
    if (input.velocity.size() != 3 * cells) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueLengthWrong,
            std::format("the velocity has {} values and {} cells need {} component-blocked entries",
                        input.velocity.size(), cells, 3 * cells)));
    }
    if (const std::optional<std::size_t> bad = firstNonFinite(input.velocity)) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueNotFinite,
            std::format("the velocity at component-blocked index {} is not finite", *bad)));
    }

    const double alpha = input.iteration.velocityRelaxation;
    CfdCouplingPredictor out;
    out.velocity.assign(3 * cells, 0.0);
    out.diagonal.assign(3 * cells, 0.0);

    for (std::size_t c = 0; c < kVectorComponents.size(); ++c) {
        const CfdMomentumSystem& system = input.momentum[c];

        // THE IMPLICIT RELAXATION. The diagonal is divided by alpha and the part
        // taken out is returned to the source against the OLD field:
        //
        //   (a_P / alpha) u* + sum_N a_N u*_N = b + ((1 - alpha) / alpha) a_P u_old
        //
        // At alpha = 1 the diagonal is unchanged (a_P / 1.0 == a_P) and the
        // added term is exactly zero.
        std::vector<double> values(system.matrix.values().begin(), system.matrix.values().end());
        std::vector<double> source = system.source;
        const std::span<const CfdSparseIndex> rowStart = system.matrix.rowStart();
        const std::span<const CfdSparseIndex> inner = system.matrix.inner();

        for (std::size_t row = 0; row < cells; ++row) {
            std::optional<std::size_t> diagonalSlot;
            for (CfdSparseIndex slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
                if (inner[static_cast<std::size_t>(slot)] == static_cast<CfdSparseIndex>(row)) {
                    diagonalSlot = static_cast<std::size_t>(slot);
                    break;
                }
            }
            if (!diagonalSlot.has_value()) {
                return std::unexpected(internalFailure(
                    CfdCouplingProblem::MomentumSystemsWrong,
                    std::format("the {} momentum system's row {} holds no diagonal",
                                toString(kVectorComponents[c]), row)));
            }
            const double a = values[*diagonalSlot];
            if (!(a > 0.0)) {
                return std::unexpected(failureOf(
                    CfdCouplingProblem::MomentumDiagonalNotPositive,
                    std::format("the {} momentum diagonal at cell {} is {} kg/s and the relaxation "
                                "and d_P = V_P / a_P both need it strictly positive",
                                toString(kVectorComponents[c]), row, a)));
            }
            values[*diagonalSlot] = a / alpha;
            source[row] += ((1.0 - alpha) / alpha) * a * input.velocity[c * cells + row];
            out.diagonal[c * cells + row] = a / alpha;
        }

        // NO SYMMETRY TOLERANCE. A momentum matrix with convection in it is
        // asymmetric by construction (ADR-072 Decision 3), and demanding
        // symmetry of it would be wrong rather than merely slack.
        std::expected<CfdSparseMatrix, CfdSparseFailure> relaxed = CfdSparseMatrix::fromCsr(
            cells, cells, std::vector<CfdSparseIndex>(rowStart.begin(), rowStart.end()),
            std::vector<CfdSparseIndex>(inner.begin(), inner.end()), std::move(values));
        if (!relaxed.has_value()) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::format("the relaxed {} momentum system was refused as {}: {}",
                            toString(kVectorComponents[c]), toString(relaxed.error().problem),
                            relaxed.error().error.message)));
        }

        std::expected<CfdLinearSolveResult, Error> solved =
            solve(*relaxed, source, input.linearSolver);
        if (!solved.has_value()) {
            return std::unexpected(CfdCouplingFailure{
                CfdCouplingProblem::LinearSolveRefused,
                Error{solved.error().code,
                      std::format("the supplied solve refused the {} momentum system: {}",
                                  toString(kVectorComponents[c]), solved.error().message)}});
        }
        if (solved->solution.size() != cells) {
            return std::unexpected(failureOf(
                CfdCouplingProblem::LinearSolutionUnusable,
                std::format("the supplied solve returned {} values for the {} momentum system and "
                            "{} cells were asked for",
                            solved->solution.size(), toString(kVectorComponents[c]), cells)));
        }
        if (const std::optional<std::size_t> bad = firstNonFinite(solved->solution)) {
            return std::unexpected(failureOf(
                CfdCouplingProblem::LinearSolutionUnusable,
                std::format("the supplied solve returned a non-finite value at cell {} of the {} "
                            "momentum system",
                            *bad, toString(kVectorComponents[c]))));
        }
        if (!std::isfinite(solved->normalizedResidual) || solved->normalizedResidual < 0.0 ||
            solved->normalizedResidual > input.linearSolver.relativeResidualTolerance) {
            return std::unexpected(failureOf(
                CfdCouplingProblem::LinearSolveRefused,
                std::format("the supplied solve reported a normalised residual of {} for the {} "
                            "momentum system against a tolerance of {}",
                            solved->normalizedResidual, toString(kVectorComponents[c]),
                            input.linearSolver.relativeResidualTolerance)));
        }

        for (std::size_t cell = 0; cell < cells; ++cell) {
            out.velocity[c * cells + cell] = solved->solution[cell];
        }
        out.solveResidual[c] = solved->normalizedResidual;
    }

    return out;
}

// ---------------------------------------------------------------------------
// The corrections
// ---------------------------------------------------------------------------

std::expected<CfdCouplingCorrection, CfdCouplingFailure>
applyCfdCouplingCorrection(const CfdTopology& topology, const CfdGeometry& geometry,
                           const PreparedCfdBoundary& boundary,
                           const CfdCouplingCorrectionInput& input) {
    // THE GATE FIRST, THEN THE SETTINGS. `SettingsInvalid` is phase 6 and the
    // shared gate holds phases 1 to 5, so validating the settings before it
    // would report a LATER phase for an input with two faults -- the opposite
    // of what the header promises. The first draft had it the other way round.
    const CfdPressureCorrectionInput gateInput{.predictedFaceFlux = input.predictedFaceFlux,
                                               .momentumDiagonal = input.momentumDiagonal,
                                               .density = input.density};
    std::string message;
    if (const std::optional<CfdCouplingProblem> problem =
            pressureCorrectionProblem(topology, geometry, boundary, gateInput, &message)) {
        return std::unexpected(failureOf(*problem, std::move(message)));
    }

    if (const Result<void> ok = validate(input.iteration); !ok.has_value()) {
        return std::unexpected(failureOf(CfdCouplingProblem::SettingsInvalid, ok.error().message));
    }

    const std::size_t cells = topology.cells();
    const std::size_t faces = topology.faces();
    const std::size_t internal = topology.internalFaces();
    const double rho = input.density.si();

    if (input.pressureCorrection.size() != cells || input.pressure.size() != cells) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueLengthWrong,
            std::format("the pressure correction has {} values and the pressure {}, and {} cells "
                        "need one each",
                        input.pressureCorrection.size(), input.pressure.size(), cells)));
    }
    if (input.predictedVelocity.size() != 3 * cells) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueLengthWrong,
            std::format("the predicted velocity has {} values and {} cells need {}",
                        input.predictedVelocity.size(), cells, 3 * cells)));
    }
    if (const std::optional<std::size_t> bad = firstNonFinite(input.pressureCorrection)) {
        return std::unexpected(
            failureOf(CfdCouplingProblem::ValueNotFinite,
                      std::format("the pressure correction at cell {} is not finite", *bad)));
    }
    if (const std::optional<std::size_t> bad = firstNonFinite(input.pressure)) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueNotFinite,
            std::format("the pressure at cell {} is not finite", *bad)));
    }
    if (const std::optional<std::size_t> bad = firstNonFinite(input.predictedVelocity)) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueNotFinite,
            std::format("the predicted velocity at component-blocked index {} is not finite", *bad)));
    }

    CfdCouplingCorrection out;
    out.faceFlux.assign(input.predictedFaceFlux.begin(), input.predictedFaceFlux.end());
    out.velocity.assign(input.predictedVelocity.begin(), input.predictedVelocity.end());
    out.pressure.assign(cells, 0.0);

    // `sum_f p'_f S_f`, accumulated per cell and oriented OUT of that cell.
    std::vector<Vec> pressureFaceSum(cells, Vec{0.0, 0.0, 0.0});

    // ---- THE INTERNAL FACE LOOP, ONCE EACH -----------------------------
    for (std::size_t f = 0; f < internal; ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const std::size_t owner = topology.owner(face).value();
        const std::size_t neighbour = topology.neighbour(face)->value();
        const double weight = *cfdFaceInterpolationWeight(geometry, face);
        const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
        if (!normal.has_value()) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::format("internal face {} has an interpolation weight and no unit normal", f)));
        }
        const Vec n{normal->x(), normal->y(), normal->z()};
        const Vec dOwner = coefficientsAt(geometry, input.momentumDiagonal, cells, owner);
        const Vec dNeighbour = coefficientsAt(geometry, input.momentumDiagonal, cells, neighbour);

        double effective = 0.0;
        for (std::size_t i = 0; i < 3; ++i) {
            effective += n[i] * n[i] * interpolate(dOwner[i], dNeighbour[i], weight);
        }
        const double area = geometry.faceArea(face).si();
        const double distance = geometry.orthogonalDistance(face).si();
        const double coefficient = rho * effective * area / distance;

        // THE FLUX CORRECTION. `phi'_f = -a_f (p'_N - p'_P)`, which is exactly
        // what makes ADR-075 Decision 5's identity hold: summed out of a cell
        // this is `(A p')_P`, and `A` carries the same `a_f`.
        const double pOwner = input.pressureCorrection[owner];
        const double pNeighbour = input.pressureCorrection[neighbour];
        out.faceFlux[f] = input.predictedFaceFlux[f] - coefficient * (pNeighbour - pOwner);

        // The face value of `p'`, for the velocity correction's face sum.
        const double faceValue = interpolate(pOwner, pNeighbour, weight);
        const Vec areaVector = siOf(geometry.faceAreaVector(face));
        for (std::size_t i = 0; i < 3; ++i) {
            pressureFaceSum[owner][i] += faceValue * areaVector[i];
            pressureFaceSum[neighbour][i] -= faceValue * areaVector[i];
        }
    }

    // ---- THE BOUNDARY FACE LOOP ----------------------------------------
    for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const FaceIndex face = topology.faceOf(boundaryFace);
        const std::size_t owner = topology.owner(face).value();
        const std::optional<CfdBoundaryTreatment> treatment = boundary.treatment(boundaryFace);
        if (!treatment.has_value()) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::format("boundary face {} has no treatment although the counts agree", b)));
        }

        const Vec areaVector = siOf(geometry.faceAreaVector(face));
        if (!fixesPressureLevel(*treatment)) {
            // Zero gradient: `p'_b = p'_P`, and the flux is NOT corrected,
            // because it is prescribed.
            const double faceValue = input.pressureCorrection[owner];
            for (std::size_t i = 0; i < 3; ++i) {
                pressureFaceSum[owner][i] += faceValue * areaVector[i];
            }
            continue;
        }

        // Fixed pressure: `p'_b = 0`, so the face value contributes nothing to
        // the velocity face sum, and the flux correction is `+a_b p'_P`.
        const std::optional<Direction3D> normal = geometry.faceUnitNormal(face);
        const double distance = geometry.orthogonalDistance(face).si();
        if (!normal.has_value() || !(distance > 0.0)) {
            return std::unexpected(internalFailure(
                CfdCouplingProblem::EvaluationIsInconsistent,
                std::format("boundary face {} fixes the pressure and has no usable normal or "
                            "orthogonal distance",
                            b)));
        }
        const Vec n{normal->x(), normal->y(), normal->z()};
        const Vec d = coefficientsAt(geometry, input.momentumDiagonal, cells, owner);
        double effective = 0.0;
        for (std::size_t i = 0; i < 3; ++i) {
            effective += n[i] * n[i] * d[i];
        }
        const double area = geometry.faceArea(face).si();
        const double coefficient = rho * effective * area / distance;
        out.faceFlux[face.value()] =
            input.predictedFaceFlux[face.value()] + coefficient * input.pressureCorrection[owner];
    }

    // ---- THE VELOCITY CORRECTION ---------------------------------------
    //
    // `u'^i_P = -(1 / a_P^i) sum_f p'_f S_f,i`. THE CELL VOLUME CANCELLED
    // ANALYTICALLY against `d_P = V_P / a_P` (ADR-075 Decision 12), so it does
    // not appear here and cannot contribute a rounding of its own.
    for (std::size_t cell = 0; cell < cells; ++cell) {
        const Vec a = diagonalAt(input.momentumDiagonal, cells, cell);
        for (std::size_t i = 0; i < 3; ++i) {
            out.velocity[i * cells + cell] =
                input.predictedVelocity[i * cells + cell] - pressureFaceSum[cell][i] / a[i];
        }
    }

    // ---- THE PRESSURE UPDATE -------------------------------------------
    for (std::size_t cell = 0; cell < cells; ++cell) {
        out.pressure[cell] = input.pressure[cell] +
                             input.iteration.pressureRelaxation * input.pressureCorrection[cell];
    }

    if (!allFinite(out.faceFlux) || !allFinite(out.velocity) || !allFinite(out.pressure)) {
        return std::unexpected(internalFailure(
            CfdCouplingProblem::ResultIsNotFinite,
            std::string{"a corrected field holds a non-finite value although every input was "
                        "finite"}));
    }
    if (out.faceFlux.size() != faces) {
        return std::unexpected(internalFailure(
            CfdCouplingProblem::EvaluationIsInconsistent,
            std::format("the corrected flux has {} values and the topology has {} faces",
                        out.faceFlux.size(), faces)));
    }

    return out;
}

// ---------------------------------------------------------------------------
// One outer iteration
// ---------------------------------------------------------------------------

std::expected<CfdCouplingIteration, CfdCouplingFailure>
advanceCfdCoupling(const CfdTopology& topology, const CfdGeometry& geometry,
                   const PreparedCfdBoundary& boundary, const CfdCouplingIterationInput& input,
                   const CfdLinearSolve& solve) {
    // ---- 0  THIS OPERATOR'S OWN GATE, BEFORE ANY QUALIFIED OPERATOR RUNS
    //
    // THE ORDER AND THE BLAME WERE BOTH WRONG IN THE FIRST DRAFT, and its own
    // correctness review found both. The draft validated the settings first and
    // then went straight to the predictor and to `cfdMassFlux`, so:
    //
    //   * a geometry built for another mesh was reported as `SettingsInvalid`
    //     or `LinearSolveRefused`, not as `GeometryDescribesAnotherTopology` --
    //     the header promises the EARLIER phase, and #1 was not even reachable
    //     until after three linear solves had run;
    //   * a caller's bad DENSITY reached `cfdMassFlux`, whose refusal was
    //     wrapped as `EvaluationIsInconsistent` with `ErrorCode::Internal` --
    //     this module blaming itself for the caller's input, which is the exact
    //     category error `P19-MOM-001`'s review found in its own first draft.
    //
    // So the phases that do not need `phi*` or the diagonal run here, in the
    // order `kCfdCouplingProblems` declares, before anything else is touched.
    const std::size_t cells = topology.cells();
    if (!geometry.describes(topology)) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::GeometryDescribesAnotherTopology,
            std::string{"the finite-volume metrics were built for a different mesh or geometry "
                        "revision than the topology"}));
    }
    if (boundary.boundaryFaces() != topology.boundaryFaces()) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::BoundaryDescribesAnotherTopology,
            std::format("the prepared boundary covers {} faces and the topology has {}",
                        boundary.boundaryFaces(), topology.boundaryFaces())));
    }
    for (std::size_t f = 0; f < topology.faces(); ++f) {
        const FaceIndex face = FaceIndex::fromValue(f);
        const CellIndex owner = topology.owner(face);
        if (!owner.isValid() || owner.value() >= cells) {
            return std::unexpected(failureOf(
                CfdCouplingProblem::TopologyNamesAnUnknownCell,
                std::format("face {} names owner cell {} and the topology holds {} cells", f,
                            owner.value(), cells)));
        }
        const std::optional<CellIndex> neighbour = topology.neighbour(face);
        if (neighbour.has_value() && (!neighbour->isValid() || neighbour->value() >= cells)) {
            return std::unexpected(failureOf(
                CfdCouplingProblem::TopologyNamesAnUnknownCell,
                std::format("face {} names neighbour cell {} and the topology holds {} cells", f,
                            neighbour->value(), cells)));
        }
    }
    if (input.pressure.size() != cells) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueLengthWrong,
            std::format("the pressure has {} values and the topology has {} cells",
                        input.pressure.size(), cells)));
    }
    if (input.velocity.size() != 3 * cells) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueLengthWrong,
            std::format("the velocity has {} values and {} cells need {} component-blocked entries",
                        input.velocity.size(), cells, 3 * cells)));
    }
    if (const std::optional<std::size_t> bad = firstNonFinite(input.pressure)) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueNotFinite,
            std::format("the pressure at cell {} is not finite", *bad)));
    }
    if (const std::optional<std::size_t> bad = firstNonFinite(input.velocity)) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueNotFinite,
            std::format("the velocity at component-blocked index {} is not finite", *bad)));
    }
    if (!std::isfinite(input.density.si()) || !(input.density.si() > 0.0)) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::ValueNotFinite,
            std::format("the density is {} kg/m^3 and must be finite and positive",
                        input.density.si())));
    }
    if (const Result<void> ok = validate(input.iteration); !ok.has_value()) {
        return std::unexpected(failureOf(CfdCouplingProblem::SettingsInvalid, ok.error().message));
    }
    if (const Result<void> ok = validate(input.linearSolver); !ok.has_value()) {
        return std::unexpected(failureOf(CfdCouplingProblem::SettingsInvalid, ok.error().message));
    }

    // ---- 1  the predictor ----------------------------------------------
    const CfdCouplingPredictorInput predictorInput{.momentum = input.momentum,
                                                   .velocity = input.velocity,
                                                   .iteration = input.iteration,
                                                   .linearSolver = input.linearSolver};
    std::expected<CfdCouplingPredictor, CfdCouplingFailure> predicted =
        cfdCouplingPredictor(topology, geometry, boundary, predictorInput, solve);
    if (!predicted.has_value()) {
        return std::unexpected(predicted.error());
    }

    // ---- 2  the predicted flux, from the QUALIFIED producer -------------
    const CfdMassFluxInput fluxInput{.currentVelocity = predicted->velocity,
                                     .density = input.density};
    std::expected<std::vector<double>, CfdConvectionFailure> predictedFlux =
        cfdMassFlux(topology, geometry, boundary, fluxInput);
    if (!predictedFlux.has_value()) {
        return std::unexpected(CfdCouplingFailure{
            CfdCouplingProblem::EvaluationIsInconsistent,
            Error{ErrorCode::Internal,
                  std::format("the qualified mass-flux producer refused the predicted velocity "
                              "as {}: {}",
                              toString(predictedFlux.error().problem),
                              predictedFlux.error().error.message)}});
    }

    // ---- 3  the correction system ---------------------------------------
    const CfdPressureCorrectionInput systemInput{.predictedFaceFlux = *predictedFlux,
                                                 .momentumDiagonal = predicted->diagonal,
                                                 .density = input.density};
    std::expected<CfdPressureCorrectionSystem, CfdCouplingFailure> system =
        assembleCfdPressureCorrection(topology, geometry, boundary, systemInput);
    if (!system.has_value()) {
        return std::unexpected(system.error());
    }

    // ---- 4  the supplied solve ------------------------------------------
    if (!solve) {
        return std::unexpected(failureOf(CfdCouplingProblem::LinearSolveRefused,
                                         "no linear solve was supplied"));
    }
    std::expected<CfdLinearSolveResult, Error> solved =
        solve(system->matrix, system->source, input.linearSolver);
    if (!solved.has_value()) {
        return std::unexpected(CfdCouplingFailure{
            CfdCouplingProblem::LinearSolveRefused,
            Error{solved.error().code,
                  std::format("the supplied solve refused the pressure-correction system: {}",
                              solved.error().message)}});
    }
    if (solved->solution.size() != cells) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::LinearSolutionUnusable,
            std::format("the supplied solve returned {} values for the pressure-correction system "
                        "and {} cells were asked for",
                        solved->solution.size(), cells)));
    }
    if (const std::optional<std::size_t> bad = firstNonFinite(solved->solution)) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::LinearSolutionUnusable,
            std::format("the supplied solve returned a non-finite pressure correction at cell {}",
                        *bad)));
    }
    if (!std::isfinite(solved->normalizedResidual) || solved->normalizedResidual < 0.0 ||
        solved->normalizedResidual > input.linearSolver.relativeResidualTolerance) {
        return std::unexpected(failureOf(
            CfdCouplingProblem::LinearSolveRefused,
            std::format("the supplied solve reported a normalised residual of {} for the "
                        "pressure-correction system against a tolerance of {}",
                        solved->normalizedResidual, input.linearSolver.relativeResidualTolerance)));
    }

    // ---- 5  the corrections ---------------------------------------------
    const CfdCouplingCorrectionInput correctionInput{
        .pressureCorrection = solved->solution,
        .pressure = input.pressure,
        .predictedVelocity = predicted->velocity,
        .predictedFaceFlux = *predictedFlux,
        .momentumDiagonal = predicted->diagonal,
        .density = input.density,
        .iteration = input.iteration};
    std::expected<CfdCouplingCorrection, CfdCouplingFailure> corrected =
        applyCfdCouplingCorrection(topology, geometry, boundary, correctionInput);
    if (!corrected.has_value()) {
        return std::unexpected(corrected.error());
    }

    // ---- 6  continuity, from the QUALIFIED evaluator ---------------------
    const CfdContinuityInput continuityInput{.faceFlux = corrected->faceFlux};
    std::expected<CfdContinuityReport, CfdContinuityFailure> continuity =
        evaluateCfdContinuity(topology, geometry, boundary, continuityInput);
    if (!continuity.has_value()) {
        return std::unexpected(CfdCouplingFailure{
            CfdCouplingProblem::EvaluationIsInconsistent,
            Error{ErrorCode::Internal,
                  std::format("the qualified continuity evaluator refused the CORRECTED flux as "
                              "{}: {}",
                              toString(continuity.error().problem),
                              continuity.error().error.message)}});
    }

    CfdCouplingIterationSummary summary;
    summary.cells = cells;
    summary.faces = topology.faces();
    summary.pressureSolveResidual = solved->normalizedResidual;
    summary.momentumSolveResidual = predicted->solveResidual;
    summary.pressure = system->summary;

    double largest = 0.0;
    for (const double p : solved->solution) {
        largest = std::max(largest, std::abs(p));
    }
    summary.largestPressureCorrection = largest;

    // ---- 7  the residuals ------------------------------------------------
    //
    // THE MOMENTUM RESIDUAL IS MEASURED AGAINST THE FIELD THE ITERATION STARTED
    // FROM, with the system as supplied and NOT as relaxed: it answers "how far
    // was the incoming field from satisfying momentum", which is what an outer
    // iteration's convergence is about. Relaxation is a path to the answer, not
    // part of the equation being measured.
    std::array<double, 3> momentumResidual{};
    for (std::size_t c = 0; c < kVectorComponents.size(); ++c) {
        const CfdMomentumSystem& system2 = input.momentum[c];
        momentumResidual[c] = normalisedResidual(
            system2.matrix, system2.source, input.velocity.subspan(c * cells, cells));
    }
    CfdResiduals residuals;
    residuals.momentumX = momentumResidual[0];
    residuals.momentumY = momentumResidual[1];
    residuals.momentumZ = momentumResidual[2];

    // A CLOSED DOMAIN HAS NO RELATIVE MEASURES, AND THAT IS NOT AN ERROR.
    // `CfdContinuity.hpp` says so in as many words: a closed domain's boundary
    // throughput is zero, so `ADR-058`'s two RELATIVE measures are UNDEFINED
    // rather than small. The honest number there is the ABSOLUTE largest cell
    // residual, which is exactly zero for a conservative closed field; claiming
    // a relative zero would be a measurement nobody made.
    //
    // THIS WAS A DEFECT AND THE CLOSED-DOMAIN BENCHMARK FOUND IT. The first
    // draft treated an absent balance as an internal inconsistency, so the one
    // case with an exact analytic answer -- a closed domain at rest -- was the
    // one case that could not run.
    // AND AN ABSOLUTE kg/s NUMBER MUST NOT GO INTO A DIMENSIONLESS FIELD, which
    // is what the SECOND draft did. `CfdResiduals` is documented as normalised
    // and dimensionless, and `continuity` fed straight into
    // `CfdConvergenceSettings::continuityResidualTolerance = 1e-6`: at
    // millimetre scale with water, a wholly non-conservative field reports
    // ~1e-9 kg/s and would be called CONVERGED. Its own correctness review
    // found that, and noted that every reachable pure-Neumann run lands in this
    // branch. So a scale is needed, and the interior flux provides one.
    //
    // ALSO: AN ABSENT BALANCE IS NOT THE SAME AS A CLOSED DOMAIN.
    // `CfdContinuity.hpp` is explicit that `massBalance()` is absent "for a
    // closed domain AND for any mesh whose boundary `phi` is all zero" -- an
    // open duct started from rest is the second case, and the second draft
    // refused it as an internal inconsistency. The SCALE decides, not the
    // boundary kind.
    const std::optional<CfdMassBalance> balance = continuity->massBalance();
    summary.closedDomain = continuity->summary().closedDomain;
    summary.massBalanceDefined = balance.has_value();

    // FROM THE PREDICTED FLUX, NOT THE CORRECTED ONE, and the first form of
    // this got it wrong in a way a test caught at once. The correction's whole
    // purpose is to drive the interior flux toward conservation, so in a closed
    // domain the CORRECTED interior flux collapses toward zero -- scaling by it
    // divides a noise-floor residual by a noise-floor scale and reports a
    // meaningless ratio (0.375 on the first run, which is two values at the
    // noise floor and nothing else). The magnitude actually being balanced is
    // the PREDICTED flux, which is also what the boundary throughput is
    // computed from, so the two scales are consistent.
    double interiorMagnitude = 0.0;
    for (std::size_t f = 0; f < topology.internalFaces(); ++f) {
        interiorMagnitude += std::abs((*predictedFlux)[f]);
    }
    const double boundaryThroughput = system->summary.boundaryThroughput;
    summary.continuityScale =
        boundaryThroughput > 0.0 ? boundaryThroughput : 0.5 * interiorMagnitude;

    CfdMassBalance reported{};
    if (balance.has_value()) {
        // ADR-058's measure, relative to the boundary throughput.
        reported = *balance;
        residuals.continuity = balance->maxCellContinuity;
    } else {
        // No boundary throughput to divide by, so this milestone's own scale.
        // `CfdMassBalance` is NOT filled from it: ADR-058 fixed those two
        // measures against the boundary throughput and they stay undefined.
        residuals.continuity = relativeDeparture(
            std::abs(continuity->largestCellResidual().si()), 0.0, summary.continuityScale);
    }

    if (!isWellFormed(residuals)) {
        return std::unexpected(internalFailure(
            CfdCouplingProblem::ResultIsNotFinite,
            std::format("a residual is not finite or is negative: momentum ({}, {}, {}) and "
                        "continuity {}",
                        residuals.momentumX, residuals.momentumY, residuals.momentumZ,
                        residuals.continuity)));
    }

    // THE CLOSURE BOUND IS DERIVED LIKE FOR LIKE (ADR-075 Decision 5).
    //
    // The identity says the corrected flux's per-cell continuity IS the
    // pressure system's residual, so the bound is THAT RESIDUAL IN THE SAME
    // MEASURE -- an infinity norm over the same continuity scale -- plus an
    // allowance for the identity's own arithmetic.
    //
    // THE FIRST FORM MULTIPLIED THE SOLVER'S RESIDUAL BY A SLACK, AND ITS OWN
    // REVIEW SHOWED THE DERIVATION DID NOT TRANSFER: the solver reports
    // ||r||_2 / (||A||_inf ||x||_2 + ||b||_2) (ADR-052) while
    // `maxCellContinuity` is ||r||_inf over the throughput, and the ratio
    // between those drifts with mesh size -- ||A||_inf ||p_prime||_2 grows like
    // sqrt(N) while the boundary throughput does not. Comparing a quantity with
    // itself in its own norm has no such drift, and needs no slack factor.
    //
    // THE PINNED ROW IS EXCLUDED, because A's row and b were cleared there: the
    // identity is silent at that cell by construction (ADR-075 Decision 5), and
    // including it would compare a real flux sum against a structural zero.
    const Result<std::vector<double>> product = system->matrix.multiply(solved->solution);
    if (!product.has_value()) {
        return std::unexpected(internalFailure(
            CfdCouplingProblem::EvaluationIsInconsistent,
            std::format("the assembled pressure matrix refused its own solution: {}",
                        product.error().message)));
    }
    const std::optional<CellIndex> pinned = system->summary.pinnedCell;
    double systemResidualInf = 0.0;
    for (std::size_t c = 0; c < cells; ++c) {
        if (pinned.has_value() && pinned->value() == c) {
            continue;
        }
        systemResidualInf =
            std::max(systemResidualInf, std::abs((*product)[c] - system->source[c]));
    }
    summary.systemResidual = relativeDeparture(systemResidualInf, 0.0, summary.continuityScale);

    // AND NO PASS/FAIL FLAG IS DERIVED FROM IT. An earlier version compared
    // `residuals.continuity` against `systemResidual + floor` and reported the
    // verdict as `continuityClosed`; a mutation probe showed `= true` survived
    // the entire suite, because the identity makes the comparison hold for
    // every input. The measured number is reported and the identity is asserted
    // by tests; a flag that cannot be false is not a check.
    //
    // `kCfdCouplingClosureFloor` is still the allowance the IDENTITY's
    // arithmetic needs, and the tests use it as such.

    return CfdCouplingIteration{.corrected = std::move(*corrected),
                                .pressureCorrection = std::move(solved->solution),
                                .system = std::move(*system),
                                .residuals = residuals,
                                .balance = reported,
                                .summary = summary};
}

// ---------------------------------------------------------------------------
// The criteria
// ---------------------------------------------------------------------------

CfdCouplingVerdict cfdCouplingVerdict(const CfdResiduals& current, const CfdResiduals& first,
                                      const CfdConvergenceSettings& settings) noexcept {
    const std::array<double, 4> now{current.momentumX, current.momentumY, current.momentumZ,
                                    current.continuity};
    const std::array<double, 4> start{first.momentumX, first.momentumY, first.momentumZ,
                                      first.continuity};

    // AN UNUSABLE CRITERION DECIDES NOTHING.
    //
    // `CfdConvergenceSettings` reaches this operator only here, and this
    // function is `noexcept` and returns a verdict: it has no channel to refuse
    // through, so it states its precondition and reports `Continue` when the
    // precondition is not met. The caller validates; `L7` records what happens
    // when it does not. Without this, a NaN tolerance makes every comparison
    // false and the verdict is `Continue` forever by accident rather than by
    // decision -- and a `divergenceFactor` of 0.5, which `validate` rejects,
    // makes the FIRST iteration diverge.
    if (!std::isfinite(settings.divergenceFactor) || !(settings.divergenceFactor > 1.0) ||
        !std::isfinite(settings.momentumResidualTolerance) ||
        !std::isfinite(settings.continuityResidualTolerance)) {
        return CfdCouplingVerdict::Continue;
    }

    // DIVERGENCE IS CHECKED FIRST. A run whose residual exploded and then
    // happened to land under a tolerance has still diverged, and reporting it
    // as converged would publish a field nobody should trust.
    //
    // A ZERO FIRST RESIDUAL IS NOT A BASELINE TO GROW FROM, and the first draft
    // treated it as one: `now > factor * 0` is true for ANY positive residual,
    // so a run that started exactly satisfied -- which is precisely the
    // closed-domain-at-rest benchmark on its first iteration -- was reported as
    // DIVERGED on its second. Its own correctness review found that. The ratio
    // test needs a positive baseline; with none, there is nothing to compare.
    for (std::size_t i = 0; i < now.size(); ++i) {
        if (!std::isfinite(now[i])) {
            return CfdCouplingVerdict::Diverged;
        }
        if (std::isfinite(start[i]) && start[i] > 0.0 &&
            now[i] > settings.divergenceFactor * start[i]) {
            return CfdCouplingVerdict::Diverged;
        }
    }

    const bool momentum = current.momentumX <= settings.momentumResidualTolerance &&
                          current.momentumY <= settings.momentumResidualTolerance &&
                          current.momentumZ <= settings.momentumResidualTolerance;
    const bool continuity = current.continuity <= settings.continuityResidualTolerance;
    if (momentum && continuity) {
        return CfdCouplingVerdict::Converged;
    }
    return CfdCouplingVerdict::Continue;
}

bool cfdCouplingBudgetExhausted(std::uint32_t completed,
                                const CfdSteadySettings& settings) noexcept {
    return completed >= settings.maxIterations;
}

} // namespace bettercad::cfd
