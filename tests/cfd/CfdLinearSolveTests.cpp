// P19-LINSOLVE-001 — the CFD linear solve. ADR-076.
//
// These cases test CFD's half and only CFD's half: the routing, the symmetry
// precondition in CFD's own words, the translation of a structured refusal, and
// that the residual CFD reports is one a reader can recompute. The
// factorisations and their gates belong to `bettercad::linalg` and are tested
// in `tests/linalg/`.
//
// NO MESH IS BUILT HERE, and that is deliberate rather than lazy. This layer
// takes a `CfdSparseMatrix` and a right-hand side; a mesh would add a hundred
// lines of fixture and test the assembly, which `P19-PVCOUPLE-001` already
// qualifies. The matrices below are written out so the expected answer can be
// derived by hand or constructed before the solve.

#include <bettercad/cfd/CfdLinearSolve.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

using namespace bettercad;
using cfd::CfdLinearSolveProblem;
using cfd::CfdLinearSolution;
using cfd::CfdLinearSolverSettings;
using cfd::CfdSparseIndex;
using cfd::CfdSparseMatrix;

namespace {

/// A matrix from a dense picture. `fromCsr` validates, so a fixture that is not
/// a valid CSR fails here rather than inside a solve.
[[nodiscard]] CfdSparseMatrix matrixOf(const std::vector<std::vector<double>>& dense) {
    std::vector<CfdSparseIndex> rowStart{0};
    std::vector<CfdSparseIndex> inner;
    std::vector<double> values;
    for (const std::vector<double>& row : dense) {
        for (std::size_t j = 0; j < row.size(); ++j) {
            if (row[j] != 0.0) {
                inner.push_back(static_cast<CfdSparseIndex>(j));
                values.push_back(row[j]);
            }
        }
        rowStart.push_back(static_cast<CfdSparseIndex>(values.size()));
    }
    const std::size_t columns = dense.empty() ? 0 : dense.front().size();
    auto built = CfdSparseMatrix::fromCsr(dense.size(), columns, std::move(rowStart), std::move(inner),
                                          std::move(values));
    REQUIRE(built.has_value());
    return std::move(*built);
}

/// The pressure-correction shape: diagonal `+sum a_f`, off-diagonal `-a_f`,
/// symmetric to the last bit, with cell 0 pinned KEEPING ITS OWN DIAGONAL --
/// which is the rule the qualified assembly follows, and the reason the LDLT's
/// pivot ratio for this shape is scale-free (`ADR-076` Q5').
/// `unitPin` reproduces the ONE case the qualified coupling reports through
/// `pinnedDiagonalIsUnitValued`: a reference cell with no internal face and no
/// fixed-pressure face, whose diagonal falls back to a dimensionless 1.0
/// instead of keeping its own value. `ADR-076` `L1` is about this path.
[[nodiscard]] std::vector<std::vector<double>> pressureShape(std::size_t cells, double faceCoefficient,
                                                             bool pinned = true, bool unitPin = false) {
    std::vector<std::vector<double>> dense(cells, std::vector<double>(cells, 0.0));
    for (std::size_t i = 0; i + 1 < cells; ++i) {
        dense[i][i] += faceCoefficient;
        dense[i + 1][i + 1] += faceCoefficient;
        dense[i][i + 1] -= faceCoefficient;
        dense[i + 1][i] -= faceCoefficient;
    }
    if (pinned) {
        const double kept = dense[0][0];
        for (std::size_t j = 0; j < cells; ++j) {
            dense[0][j] = 0.0;
            dense[j][0] = 0.0;
        }
        dense[0][0] = unitPin ? 1.0 : kept;
    }
    return dense;
}

/// The momentum shape: diffusion plus UPWIND convection, which never pairs into
/// a symmetric entry unless the flux is zero (`ADR-072` Decision 3).
[[nodiscard]] std::vector<std::vector<double>> momentumShape(std::size_t cells, double diffusion, double flux) {
    std::vector<std::vector<double>> dense(cells, std::vector<double>(cells, 0.0));
    for (std::size_t i = 0; i + 1 < cells; ++i) {
        dense[i][i] += diffusion;
        dense[i + 1][i + 1] += diffusion;
        dense[i][i + 1] -= diffusion;
        dense[i + 1][i] -= diffusion;
        dense[i][i] += flux;
        dense[i + 1][i] -= flux;
    }
    dense[0][0] += diffusion + flux;
    dense[cells - 1][cells - 1] += diffusion + flux;
    return dense;
}

/// `||A x - b||_2 / (||A||_inf ||x||_2 + ||b||_2)`, recomputed here so the
/// number this layer passes on can be checked against one this file derived.
[[nodiscard]] double independentNormalizedResidual(const CfdSparseMatrix& matrix,
                                                   std::span<const double> rightHandSide,
                                                   std::span<const double> x) {
    double squared = 0.0;
    double matrixInfinity = 0.0;
    double solutionSquared = 0.0;
    double rhsSquared = 0.0;
    const std::span<const CfdSparseIndex> rowStart = matrix.rowStart();
    const std::span<const CfdSparseIndex> inner = matrix.inner();
    const std::span<const double> values = matrix.values();
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
        double product = 0.0;
        double rowSum = 0.0;
        for (CfdSparseIndex slot = rowStart[row]; slot < rowStart[row + 1]; ++slot) {
            product += values[slot] * x[static_cast<std::size_t>(inner[slot])];
            rowSum += std::abs(values[slot]);
        }
        const double entry = product - rightHandSide[row];
        squared += entry * entry;
        matrixInfinity = std::max(matrixInfinity, rowSum);
        solutionSquared += x[row] * x[row];
        rhsSquared += rightHandSide[row] * rightHandSide[row];
    }
    const double denominator = matrixInfinity * std::sqrt(solutionSquared) + std::sqrt(rhsSquared);
    return denominator > 0.0 ? std::sqrt(squared) / denominator : std::sqrt(squared);
}

[[nodiscard]] std::uint64_t fingerprint(const CfdLinearSolution& solution) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto feed = [&hash](double value) {
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (bits >> (byte * 8)) & 0xffULL;
            hash *= 1099511628211ULL;
        }
    };
    for (const double value : solution.values) {
        feed(value);
    }
    feed(solution.normalizedResidual);
    feed(solution.conditioningRatio);
    return hash;
}

} // namespace

TEST_CASE("CfdLinearSolve_SolvesTheSymmetricPressureSystem", "[cfd][linsolve]") {
    const CfdSparseMatrix matrix = matrixOf(pressureShape(16, 2.5e-7));
    std::vector<double> rightHandSide(16, 1.0e-10);
    rightHandSide[0] = 0.0;

    const auto solved = cfd::solveCfdPressureSystem(matrix, rightHandSide, CfdLinearSolverSettings{});
    REQUIRE(solved.has_value());
    CHECK(solved->values.size() == 16);
    CHECK(solved->normalizedResidual <= 1.0e-9);
    CHECK(solved->symmetricKernel);
    // The LDLT has no notion of an independent-column count, so the field is
    // not borrowed to mean something else on this path.
    CHECK(solved->reportedRank == 0);

    SECTION("the fixture is exactly symmetric, so the path is the one under test") {
        CHECK(matrix.largestSymmetryError() == 0.0);
    }

    SECTION("the pivot ratio is scale-free, which is what makes the default floor safe") {
        // ADR-076 Q5': the pin keeps its own diagonal, so the ratio does not
        // move with the face coefficient. Measured across six decades here, and
        // this is the claim that lets CFD pass ONE ratio across the boundary
        // and leave the conditioning floor to the kernel's default.
        double reference = 0.0;
        for (const double coefficient : {1.0e-3, 1.0e-6, 1.0e-9, 1.0e-12, 1.0e-15, 1.0e-18}) {
            const CfdSparseMatrix scaled = matrixOf(pressureShape(16, coefficient));
            std::vector<double> rhs(16, coefficient * 1.0e-3);
            rhs[0] = 0.0;
            const auto result = cfd::solveCfdPressureSystem(scaled, rhs, CfdLinearSolverSettings{});
            REQUIRE(result.has_value());
            if (reference == 0.0) {
                reference = result->conditioningRatio;
                // PINNED TO THE MEASURED VALUE. `ADR-076` Q5' measured 0.25 for
                // this shape, and a one-sided `> 1e-3` admitted a hard-coded
                // constant -- which is also perfectly scale-free, so the sweep
                // could not tell the difference.
                CHECK(std::abs(reference - 0.25) <= 1.0e-12);
            } else {
                CHECK(std::abs(result->conditioningRatio - reference) <= 1.0e-15);
            }
        }
    }

    SECTION("and the residual it reports is one this file can recompute") {
        const double independent = independentNormalizedResidual(matrix, rightHandSide, solved->values);
        // EXACT EQUALITY. The first form allowed an ABSOLUTE `1e-18` against a
        // residual of magnitude `4e-17` -- a 2.4% relative tolerance, which a
        // defect scaling the reported residual by 1.001 passes. The two loops
        // are the same arithmetic over the same arrays in the same order.
        REQUIRE(independent > 0.0);
        CHECK(solved->normalizedResidual == independent);
    }
}

TEST_CASE("CfdLinearSolve_RefusesAnAsymmetricPressureSystemInCfdsOwnWords", "[cfd][linsolve]") {
    // Handing a momentum system to the pressure entry point is a structural
    // mistake, and the kernel WOULD catch it -- the LDLT reads one triangle and
    // the independent residual refuses the answer. But it would say
    // "residual_too_large", which describes a numerical difficulty rather than
    // the mistake that was made. So CFD refuses it first, by name.
    const CfdSparseMatrix matrix = matrixOf(momentumShape(16, 1.0e-6, 1.0e-6));
    std::vector<double> rightHandSide(16, 0.0);
    rightHandSide[0] = 1.0e-9;

    const auto refused = cfd::solveCfdPressureSystem(matrix, rightHandSide, CfdLinearSolverSettings{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == CfdLinearSolveProblem::MatrixIsNotSymmetric);
    CHECK(refused.error().largestSymmetryError > 0.0);
    CHECK(refused.error().rows == 16);
    CHECK_THAT(refused.error().error.message, Catch::Matchers::ContainsSubstring("symmetric"));

    SECTION("and it is refused BEFORE any factorisation is attempted") {
        // No kernel ran, so neither a conditioning measure nor a residual may
        // be reported: a number here would be a number from nowhere.
        CHECK(refused.error().conditioningRatio == 0.0);
        CHECK(refused.error().normalizedResidual == 0.0);
        CHECK_FALSE(refused.error().symmetricKernel);
    }

    SECTION("the same matrix is solved by the momentum entry point") {
        const auto solved = cfd::solveCfdMomentumSystem(matrix, rightHandSide, CfdLinearSolverSettings{});
        REQUIRE(solved.has_value());
        CHECK(solved->normalizedResidual <= 1.0e-9);
        CHECK_FALSE(solved->symmetricKernel);
    }

    SECTION("an asymmetry of one bit is still refused, because the test is exact") {
        std::vector<std::vector<double>> dense = pressureShape(8, 2.5e-7);
        dense[1][2] = std::nextafter(dense[1][2], 1.0);
        const CfdSparseMatrix nudged = matrixOf(dense);
        REQUIRE(nudged.largestSymmetryError() > 0.0);
        std::vector<double> rhs(8, 1.0e-10);
        rhs[0] = 0.0;
        const auto result = cfd::solveCfdPressureSystem(nudged, rhs, CfdLinearSolverSettings{});
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().problem == CfdLinearSolveProblem::MatrixIsNotSymmetric);
    }
}

TEST_CASE("CfdLinearSolve_SolvesTheAsymmetricMomentumSystem", "[cfd][linsolve]") {
    for (const double flux : {0.0, 1.0e-9, 1.0e-6, 1.0e-3}) {
        const CfdSparseMatrix matrix = matrixOf(momentumShape(24, 1.0e-6, flux));
        std::vector<double> rightHandSide(24, 0.0);
        rightHandSide[0] = (1.0e-6 + flux) * 1.0e-3;
        const auto solved = cfd::solveCfdMomentumSystem(matrix, rightHandSide, CfdLinearSolverSettings{});
        REQUIRE(solved.has_value());
        CHECK(solved->normalizedResidual <= 1.0e-9);
        CHECK_FALSE(solved->symmetricKernel);
        CHECK(solved->reportedRank == 24);
        CHECK(solved->conditioningRatio > 1.0e-12);
        for (const double value : solved->values) {
            CHECK(std::isfinite(value));
        }
    }
}

TEST_CASE("CfdLinearSolve_ThePortRoutesOnExactSymmetry", "[cfd][linsolve]") {
    const cfd::CfdLinearSolve port = cfd::cfdLinearSolve();
    const CfdLinearSolverSettings settings{};

    SECTION("a symmetric matrix takes the pressure path, bit for bit") {
        const CfdSparseMatrix matrix = matrixOf(pressureShape(16, 2.5e-7));
        std::vector<double> rightHandSide(16, 1.0e-10);
        rightHandSide[0] = 0.0;
        const auto throughPort = port(matrix, rightHandSide, settings);
        const auto direct = cfd::solveCfdPressureSystem(matrix, rightHandSide, settings);
        REQUIRE(throughPort.has_value());
        REQUIRE(direct.has_value());
        // BITWISE, because it is the SAME kernel on the same data and anything
        // else would mean the port had done something of its own.
        CHECK(throughPort->solution == direct->values);
        CHECK(throughPort->normalizedResidual == direct->normalizedResidual);
    }

    SECTION("an asymmetric matrix takes the momentum path, bit for bit") {
        const CfdSparseMatrix matrix = matrixOf(momentumShape(16, 1.0e-6, 1.0e-6));
        std::vector<double> rightHandSide(16, 0.0);
        rightHandSide[0] = 1.0e-9;
        const auto throughPort = port(matrix, rightHandSide, settings);
        const auto direct = cfd::solveCfdMomentumSystem(matrix, rightHandSide, settings);
        REQUIRE(throughPort.has_value());
        REQUIRE(direct.has_value());
        CHECK(throughPort->solution == direct->values);
        CHECK(throughPort->normalizedResidual == direct->normalizedResidual);
    }

    SECTION("and the routing is OBSERVABLE: the two kernels do not agree bit for bit") {
        // If the two paths gave identical answers the routing would be
        // untestable and this whole case vacuous. They agree to their accuracy
        // and differ in the last bits, which is also why `ADR-076` forbids a
        // bitwise claim across the kernels anywhere.
        const CfdSparseMatrix matrix = matrixOf(pressureShape(16, 2.5e-7));
        std::vector<double> rightHandSide(16, 1.0e-10);
        rightHandSide[0] = 0.0;
        const auto bySymmetric = cfd::solveCfdPressureSystem(matrix, rightHandSide, settings);
        const auto byGeneral = cfd::solveCfdMomentumSystem(matrix, rightHandSide, settings);
        REQUIRE(bySymmetric.has_value());
        REQUIRE(byGeneral.has_value());
        CHECK(bySymmetric->symmetricKernel);
        CHECK_FALSE(byGeneral->symmetricKernel);
        bool identical = true;
        double worst = 0.0;
        for (std::size_t i = 0; i < bySymmetric->values.size(); ++i) {
            worst = std::max(worst, std::abs(bySymmetric->values[i] - byGeneral->values[i]));
            if (bySymmetric->values[i] != byGeneral->values[i]) {
                identical = false;
            }
        }
        CHECK_FALSE(identical);
        // They do agree numerically, relative to the field's own magnitude.
        double scale = 0.0;
        for (const double value : bySymmetric->values) {
            scale = std::max(scale, std::abs(value));
        }
        REQUIRE(scale > 0.0);
        CHECK(worst / scale <= 1.0e-9);
    }

    SECTION("a refusal reaches the port as an Error, since that is the port's shape") {
        const CfdSparseMatrix singular = matrixOf(pressureShape(8, 2.5e-7, false));
        std::vector<double> rightHandSide(8, 1.0e-10);
        const auto throughPort = port(singular, rightHandSide, settings);
        REQUIRE_FALSE(throughPort.has_value());
        CHECK_FALSE(throughPort.error().message.empty());
    }
}

TEST_CASE("CfdLinearSolve_RefusesAnUnpinnedPressureSystemAsSingular", "[cfd][linsolve][singular]") {
    // A pressure-correction matrix with no pin and no fixed-pressure boundary
    // has the constant mode in its null space -- the physically meaningful
    // singular case for this operator, and the one `P19-PVCOUPLE-001` pins a
    // reference cell to remove. Nothing is regularised to hide it.
    const CfdSparseMatrix matrix = matrixOf(pressureShape(16, 2.5e-7, false));
    std::vector<double> rightHandSide(16, 1.0e-10);

    const auto refused = cfd::solveCfdPressureSystem(matrix, rightHandSide, CfdLinearSolverSettings{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == CfdLinearSolveProblem::SystemIsSingular);
    CHECK(refused.error().symmetricKernel);
    CHECK_THAT(refused.error().error.message, Catch::Matchers::ContainsSubstring("singular"));

    SECTION("the matrix really is symmetric, so this is singularity and not asymmetry") {
        CHECK(matrix.largestSymmetryError() == 0.0);
    }

    SECTION("and pinning one cell makes the same system solvable") {
        const CfdSparseMatrix pinned = matrixOf(pressureShape(16, 2.5e-7, true));
        std::vector<double> rhs(16, 1.0e-10);
        rhs[0] = 0.0;
        const auto solved = cfd::solveCfdPressureSystem(pinned, rhs, CfdLinearSolverSettings{});
        CHECK(solved.has_value());
    }
}

TEST_CASE("CfdLinearSolve_RefusesAMalformedSystem", "[cfd][linsolve][invalid]") {
    const CfdSparseMatrix square = matrixOf(pressureShape(4, 2.5e-7));

    SECTION("a matrix that is not square") {
        std::vector<CfdSparseIndex> rowStart{0, 1, 2};
        std::vector<CfdSparseIndex> inner{0, 1};
        std::vector<double> values{1.0, 1.0};
        auto built = CfdSparseMatrix::fromCsr(2, 3, std::move(rowStart), std::move(inner), std::move(values));
        REQUIRE(built.has_value());
        const std::vector<double> rightHandSide(2, 1.0);
        const auto refused = cfd::solveCfdPressureSystem(*built, rightHandSide, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == CfdLinearSolveProblem::MatrixIsNotSquare);
    }

    SECTION("a right-hand side of the wrong length, with no implicit resizing") {
        const std::vector<double> tooShort(3, 1.0e-10);
        const auto refused = cfd::solveCfdPressureSystem(square, tooShort, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == CfdLinearSolveProblem::RightHandSideLengthWrong);
    }

    SECTION("a non-finite right-hand side entry names its index") {
        std::vector<double> poisoned(4, 1.0e-10);
        poisoned[2] = std::numeric_limits<double>::quiet_NaN();
        const auto refused = cfd::solveCfdPressureSystem(square, poisoned, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == CfdLinearSolveProblem::ValueNotFinite);
        CHECK(refused.error().index == 2);
    }

    SECTION("unusable settings") {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const std::vector<double> rightHandSide(4, 1.0e-10);
        for (const double tolerance : {0.0, -1.0e-9, nan}) {
            const auto refused = cfd::solveCfdPressureSystem(
                square, rightHandSide, CfdLinearSolverSettings{.relativeResidualTolerance = tolerance});
            REQUIRE_FALSE(refused.has_value());
            CHECK(refused.error().problem == CfdLinearSolveProblem::SettingsInvalid);
        }
    }

    SECTION("every one of those is refused by the momentum entry point too") {
        const std::vector<double> tooShort(3, 1.0e-10);
        const auto refused = cfd::solveCfdMomentumSystem(square, tooShort, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == CfdLinearSolveProblem::RightHandSideLengthWrong);
    }
}

TEST_CASE("CfdLinearSolve_TheMatrixCannotCarryANonFiniteValue", "[cfd][linsolve][invalid]") {
    // THE SOLVE DOES NOT LOOP OVER THE MATRIX LOOKING FOR A NaN, and this case
    // is why that is not a missing guard. `fromCsr` is the only producer of the
    // type and it refuses a non-finite entry, so a guard in the solve could not
    // fire for any input a caller can construct. `CfdBoundary.hpp`: "An
    // enumerator no input can produce is worse than no enumerator" -- and a
    // guard no input can fire is that rule's other face. Asserted here so the
    // claim is falsifiable rather than a comment.
    std::vector<CfdSparseIndex> rowStart{0, 1, 2};
    std::vector<CfdSparseIndex> inner{0, 1};
    std::vector<double> values{1.0, std::numeric_limits<double>::infinity()};
    const auto built = CfdSparseMatrix::fromCsr(2, 2, std::move(rowStart), std::move(inner), std::move(values));
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error().problem == cfd::CfdSparseProblem::NonFiniteEntry);
}

TEST_CASE("CfdLinearSolve_ProblemMatchesTheFailure", "[cfd][linsolve]") {
    const CfdSparseMatrix asymmetric = matrixOf(momentumShape(8, 1.0e-6, 1.0e-6));
    std::vector<double> rightHandSide(8, 0.0);
    rightHandSide[0] = 1.0e-9;

    CHECK(cfd::cfdLinearSolveProblem(asymmetric, rightHandSide, CfdLinearSolverSettings{}, true) ==
          std::optional{CfdLinearSolveProblem::MatrixIsNotSymmetric});
    CHECK_FALSE(cfd::cfdLinearSolveProblem(asymmetric, rightHandSide, CfdLinearSolverSettings{}, false).has_value());

    SECTION("every enumerator has a name, and the table holds each exactly once") {
        std::set<std::string_view> names;
        for (const CfdLinearSolveProblem problem : cfd::kCfdLinearSolveProblems) {
            const std::string_view name = cfd::toString(problem);
            CHECK(name != "unknown");
            names.insert(name);
        }
        CHECK(names.size() == cfd::kCfdLinearSolveProblems.size());
    }

    SECTION("and they are declared in the order the phases check them") {
        // The same claim `CfdCouplingProblem` makes and asserts: an enumerator
        // that moved would mean a phase had moved.
        const auto index = [](CfdLinearSolveProblem problem) {
            return static_cast<std::size_t>(std::ranges::find(cfd::kCfdLinearSolveProblems, problem) -
                                            cfd::kCfdLinearSolveProblems.begin());
        };
        CHECK(index(CfdLinearSolveProblem::SettingsInvalid) < index(CfdLinearSolveProblem::MatrixIsNotSquare));
        CHECK(index(CfdLinearSolveProblem::MatrixIsNotSquare) <
              index(CfdLinearSolveProblem::RightHandSideLengthWrong));
        CHECK(index(CfdLinearSolveProblem::RightHandSideLengthWrong) <
              index(CfdLinearSolveProblem::ValueNotFinite));
        CHECK(index(CfdLinearSolveProblem::ValueNotFinite) < index(CfdLinearSolveProblem::MatrixIsNotSymmetric));
        CHECK(index(CfdLinearSolveProblem::MatrixIsNotSymmetric) <
              index(CfdLinearSolveProblem::FactorizationFailed));
        CHECK(index(CfdLinearSolveProblem::SystemIsSingular) < index(CfdLinearSolveProblem::SolutionIsNotFinite));
        CHECK(index(CfdLinearSolveProblem::SolutionIsNotFinite) <
              index(CfdLinearSolveProblem::ResidualIsNotFinite));
        CHECK(index(CfdLinearSolveProblem::ResidualIsNotFinite) < index(CfdLinearSolveProblem::ResidualTooLarge));
    }
}

TEST_CASE("CfdLinearSolve_RefusesAnAnswerItCannotJustify", "[cfd][linsolve][residual]") {
    // ONE RATIO CROSSES THE BOUNDARY (ADR-058), so this is the only solver knob
    // CFD owns -- and it must actually bite. A tolerance below what double
    // precision can deliver for this system refuses the kernel's own best
    // answer, on both paths.
    const CfdSparseMatrix pressure = matrixOf(pressureShape(32, 2.5e-7));
    std::vector<double> pressureRhs(32, 1.0e-10);
    pressureRhs[0] = 0.0;
    const auto achievable = cfd::solveCfdPressureSystem(pressure, pressureRhs, CfdLinearSolverSettings{});
    REQUIRE(achievable.has_value());
    REQUIRE(achievable->normalizedResidual > 0.0);
    const auto refused = cfd::solveCfdPressureSystem(
        pressure, pressureRhs,
        CfdLinearSolverSettings{.relativeResidualTolerance = achievable->normalizedResidual / 1.0e3});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == CfdLinearSolveProblem::ResidualTooLarge);
    CHECK(refused.error().normalizedResidual > 0.0);

    SECTION("and on the momentum path") {
        const CfdSparseMatrix momentum = matrixOf(momentumShape(32, 1.0e-6, 1.0e-6));
        std::vector<double> rhs(32, 0.0);
        rhs[0] = 1.0e-9;
        const auto best = cfd::solveCfdMomentumSystem(momentum, rhs, CfdLinearSolverSettings{});
        REQUIRE(best.has_value());
        REQUIRE(best->normalizedResidual > 0.0);
        const auto tooTight = cfd::solveCfdMomentumSystem(
            momentum, rhs,
            CfdLinearSolverSettings{.relativeResidualTolerance = best->normalizedResidual / 1.0e3});
        REQUIRE_FALSE(tooTight.has_value());
        CHECK(tooTight.error().problem == CfdLinearSolveProblem::ResidualTooLarge);
        // THE SAME SYSTEM AND THE SAME SOLVE, so every measurement the refusal
        // carries must equal the one the successful call reported. Asserting
        // only `problem` let each of these be replaced by a constant.
        CHECK(tooTight.error().normalizedResidual == best->normalizedResidual);
        CHECK(tooTight.error().conditioningRatio == best->conditioningRatio);
        CHECK(tooTight.error().conditioningRatio > 1.0e-12);
        CHECK_FALSE(tooTight.error().symmetricKernel);
    }
}

TEST_CASE("CfdLinearSolve_DeterministicRepeat", "[cfd][linsolve][determinism]") {
    // No tolerance: the same operations in the same order on the same data.
    const CfdSparseMatrix pressure = matrixOf(pressureShape(24, 2.5e-7));
    std::vector<double> pressureRhs(24, 1.0e-10);
    pressureRhs[0] = 0.0;
    const CfdSparseMatrix momentum = matrixOf(momentumShape(24, 1.0e-6, 1.0e-6));
    std::vector<double> momentumRhs(24, 0.0);
    momentumRhs[0] = 1.0e-9;

    const auto firstPressure = cfd::solveCfdPressureSystem(pressure, pressureRhs, CfdLinearSolverSettings{});
    const auto firstMomentum = cfd::solveCfdMomentumSystem(momentum, momentumRhs, CfdLinearSolverSettings{});
    REQUIRE(firstPressure.has_value());
    REQUIRE(firstMomentum.has_value());

    std::size_t differences = 0;
    for (int repeat = 0; repeat < 40; ++repeat) {
        const auto again = cfd::solveCfdPressureSystem(pressure, pressureRhs, CfdLinearSolverSettings{});
        const auto againMomentum = cfd::solveCfdMomentumSystem(momentum, momentumRhs, CfdLinearSolverSettings{});
        REQUIRE(again.has_value());
        REQUIRE(againMomentum.has_value());
        if (!(*again == *firstPressure) || !(*againMomentum == *firstMomentum)) {
            ++differences;
        }
    }
    CHECK(differences == 0);

    SECTION("the port is deterministic too, and routes the same way every time") {
        const cfd::CfdLinearSolve port = cfd::cfdLinearSolve();
        const auto first = port(momentum, momentumRhs, CfdLinearSolverSettings{});
        REQUIRE(first.has_value());
        std::size_t portDifferences = 0;
        for (int repeat = 0; repeat < 20; ++repeat) {
            const auto again = port(momentum, momentumRhs, CfdLinearSolverSettings{});
            REQUIRE(again.has_value());
            if (!(*again == *first)) {
                ++portDifferences;
            }
        }
        CHECK(portDifferences == 0);
    }

    SECTION("bit fingerprints, for comparing presets outside the test") {
        WARN(std::format("bit fingerprint, 24-cell pressure system: {:016x}", fingerprint(*firstPressure)));
        WARN(std::format("bit fingerprint, 24-cell momentum system: {:016x}", fingerprint(*firstMomentum)));
    }
}

TEST_CASE("CfdLinearSolve_LargeSystemSmoke", "[cfd][linsolve][smoke]") {
    // NOT A PERFORMANCE TEST. The seconds are printed, never asserted.
    // The reference is exact by construction: a field is chosen, the right-hand
    // side is formed from it, and the solve must return it.
    constexpr std::size_t kCells = 1500;
    const CfdSparseMatrix matrix = matrixOf(momentumShape(kCells, 1.0e-6, 1.0e-6));

    std::vector<double> expected(kCells, 0.0);
    for (std::size_t i = 0; i < kCells; ++i) {
        expected[i] = 1.0e-3 * (1.0 + static_cast<double>(i) / static_cast<double>(kCells));
    }
    const auto product = matrix.multiply(expected);
    REQUIRE(product.has_value());

    const auto solved = cfd::solveCfdMomentumSystem(matrix, *product, CfdLinearSolverSettings{});
    REQUIRE(solved.has_value());

    SECTION("the fixture is large enough for the claim to mean anything") {
        CHECK(matrix.rows() > 1000);
    }

    SECTION("it returns the constructed field") {
        double worst = 0.0;
        for (std::size_t i = 0; i < kCells; ++i) {
            REQUIRE(std::isfinite(solved->values[i]));
            worst = std::max(worst, std::abs(solved->values[i] - expected[i]));
        }
        CHECK(worst <= 1.0e-3 * 1.0e-6);
        WARN(std::format("cfd large sparse smoke: {} unknowns, {} stored entries, worst departure {:.6e}, "
                         "normalized residual {:.6e}, conditioning {:.6e}",
                         kCells, matrix.nonZeros(), worst, solved->normalizedResidual, solved->conditioningRatio));
    }

    SECTION("the gates hold at size, and nothing densified") {
        CHECK(solved->normalizedResidual <= 1.0e-9);
        CHECK(solved->conditioningRatio > 1.0e-12);
        CHECK(matrix.nonZeros() < kCells * kCells / 10);
    }
}

TEST_CASE("CfdLinearSolve_RefusesANonSquareMatrixOnEveryPath", "[cfd][linsolve][invalid]") {
    // THE NON-SQUARE GUARD WAS EXERCISED ONLY THROUGH THE PRESSURE PATH, where
    // it is also caught by accident -- `largestSymmetryError()` is infinity for
    // a non-square matrix, so even without the guard the symmetry check would
    // refuse it. On the MOMENTUM path nothing caught it, and `linalg`'s own
    // check cannot: for a 2x3 matrix read as a 2x2 system every clause it tests
    // is satisfied, so the leading square block would be solved and the third
    // column discarded in silence.
    std::vector<cfd::CfdSparseIndex> rowStart{0, 1, 2};
    std::vector<cfd::CfdSparseIndex> inner{0, 1};
    std::vector<double> values{1.0, 1.0};
    auto built = CfdSparseMatrix::fromCsr(2, 3, std::move(rowStart), std::move(inner), std::move(values));
    REQUIRE(built.has_value());
    const std::vector<double> rightHandSide(2, 1.0);

    const auto byMomentum = cfd::solveCfdMomentumSystem(*built, rightHandSide, CfdLinearSolverSettings{});
    REQUIRE_FALSE(byMomentum.has_value());
    CHECK(byMomentum.error().problem == CfdLinearSolveProblem::MatrixIsNotSquare);

    const auto byPressure = cfd::solveCfdPressureSystem(*built, rightHandSide, CfdLinearSolverSettings{});
    REQUIRE_FALSE(byPressure.has_value());
    CHECK(byPressure.error().problem == CfdLinearSolveProblem::MatrixIsNotSquare);

    SECTION("and through the port, which is where a caller actually reaches it") {
        const cfd::CfdLinearSolve port = cfd::cfdLinearSolve();
        const auto throughPort = port(*built, rightHandSide, CfdLinearSolverSettings{});
        REQUIRE_FALSE(throughPort.has_value());
        CHECK_FALSE(throughPort.error().message.empty());
    }

    SECTION("a non-square matrix with a wrong-length right-hand side reports the SQUARENESS first") {
        const std::vector<double> alsoWrong(5, 1.0);
        const auto refused = cfd::solveCfdMomentumSystem(*built, alsoWrong, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == CfdLinearSolveProblem::MatrixIsNotSquare);
    }
}

TEST_CASE("CfdLinearSolve_SolvesTheSmallestSystems", "[cfd][linsolve]") {
    SECTION("a one-cell system, on both paths") {
        const CfdSparseMatrix one = matrixOf({{4.0}});
        const std::vector<double> rhs = {2.0};
        const auto pressure = cfd::solveCfdPressureSystem(one, rhs, CfdLinearSolverSettings{});
        const auto momentum = cfd::solveCfdMomentumSystem(one, rhs, CfdLinearSolverSettings{});
        REQUIRE(pressure.has_value());
        REQUIRE(momentum.has_value());
        REQUIRE(pressure->values.size() == 1);
        REQUIRE(momentum->values.size() == 1);
        CHECK(pressure->values[0] == 0.5);
        CHECK(momentum->values[0] == 0.5);
        CHECK(pressure->symmetricKernel);
        CHECK_FALSE(momentum->symmetricKernel);
    }

    SECTION("a zero-cell system is solved and not refused") {
        std::vector<cfd::CfdSparseIndex> rowStart{0};
        auto empty = CfdSparseMatrix::fromCsr(0, 0, std::move(rowStart), {}, {});
        REQUIRE(empty.has_value());
        const auto pressure = cfd::solveCfdPressureSystem(*empty, {}, CfdLinearSolverSettings{});
        const auto momentum = cfd::solveCfdMomentumSystem(*empty, {}, CfdLinearSolverSettings{});
        REQUIRE(pressure.has_value());
        REQUIRE(momentum.has_value());
        CHECK(pressure->values.empty());
        CHECK(momentum->values.empty());
        CHECK(pressure->conditioningRatio == 1.0);
        CHECK(momentum->conditioningRatio == 1.0);
        CHECK(momentum->reportedRank == 0);
    }
}

TEST_CASE("CfdLinearSolve_RefusesASingularMomentumSystem", "[cfd][linsolve][singular]") {
    // THE GENERAL PATH'S FAILURE PAYLOAD WAS ENTIRELY UNVERIFIED: the only
    // refusal any test extracted from it was `ResidualTooLarge`, and that test
    // asserted only `problem`. So `translate`'s `SingularSystem` arm was
    // unreached -- the single commonest real failure of a momentum solve could
    // have been reported as a library fault -- and `conditioningRatio`,
    // `normalizedResidual`, `index` and `symmetricKernel` could each have been
    // replaced by a constant.
    const CfdSparseMatrix matrix = matrixOf({{1.0, 2.0, 0.0}, {1.0, 2.0, 0.0}, {0.0, 1.0, 3.0}});
    REQUIRE(matrix.largestSymmetryError() > 0.0);
    const std::vector<double> rightHandSide = {1.0, 1.0, 1.0};

    const auto refused = cfd::solveCfdMomentumSystem(matrix, rightHandSide, CfdLinearSolverSettings{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == CfdLinearSolveProblem::SystemIsSingular);
    CHECK_FALSE(refused.error().symmetricKernel);
    CHECK(refused.error().conditioningRatio == 0.0);
    CHECK(refused.error().rows == 3);
    CHECK_THAT(refused.error().error.message, Catch::Matchers::ContainsSubstring("singular"));

    SECTION("and the port refuses it too, since it routes asymmetric here") {
        const cfd::CfdLinearSolve port = cfd::cfdLinearSolve();
        const auto throughPort = port(matrix, rightHandSide, CfdLinearSolverSettings{});
        REQUIRE_FALSE(throughPort.has_value());
        CHECK_THAT(throughPort.error().message, Catch::Matchers::ContainsSubstring("singular"));
    }
}

TEST_CASE("CfdLinearSolve_RefusesASymmetricIndefiniteMatrixByItsRealName", "[cfd][linsolve]") {
    // AN ADVERSARIAL REVIEW FOUND THAT THE PORT ROUTES ON A CONDITION WEAKER
    // THAN THE KERNEL'S PRECONDITION. Exact symmetry is not
    // symmetric-positive-definite, and the LDLT requires the latter. Both
    // matrices below are exactly symmetric, perfectly solvable, and were
    // refused as `system_is_singular` with a message reporting a pivot ratio
    // "below the floor of 1e-12" when the ratio was 1 and 0.333 -- a false and
    // self-contradictory diagnostic.
    SECTION("a one-cell negative system") {
        const CfdSparseMatrix matrix = matrixOf({{-2.0}});
        REQUIRE(matrix.largestSymmetryError() == 0.0);
        const std::vector<double> rhs = {4.0};
        const auto refused = cfd::solveCfdPressureSystem(matrix, rhs, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == CfdLinearSolveProblem::MatrixIsNotPositiveDefinite);
        CHECK_THAT(refused.error().error.message,
                   Catch::Matchers::ContainsSubstring("not positive definite"));
        // AND IT IS NOT SINGULAR, which the general kernel proves by solving it.
        const auto solved = cfd::solveCfdMomentumSystem(matrix, rhs, CfdLinearSolverSettings{});
        REQUIRE(solved.has_value());
        CHECK(std::abs(solved->values[0] + 2.0) <= 1.0e-15);
    }

    SECTION("a symmetric indefinite two-cell system") {
        const CfdSparseMatrix matrix = matrixOf({{1.0, 2.0}, {2.0, 1.0}});
        REQUIRE(matrix.largestSymmetryError() == 0.0);
        const std::vector<double> rhs = {3.0, 3.0};
        const auto refused = cfd::solveCfdPressureSystem(matrix, rhs, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == CfdLinearSolveProblem::MatrixIsNotPositiveDefinite);
        const auto solved = cfd::solveCfdMomentumSystem(matrix, rhs, CfdLinearSolverSettings{});
        REQUIRE(solved.has_value());
        CHECK(std::abs(solved->values[0] - 1.0) <= 1.0e-14);
        CHECK(std::abs(solved->values[1] - 1.0) <= 1.0e-14);
    }

    SECTION("and a genuinely singular symmetric system is still SystemIsSingular") {
        // So the new enumerator has not swallowed the old one.
        const CfdSparseMatrix matrix = matrixOf(pressureShape(8, 2.5e-7, false));
        std::vector<double> rhs(8, 1.0e-10);
        const auto refused = cfd::solveCfdPressureSystem(matrix, rhs, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == CfdLinearSolveProblem::SystemIsSingular);
    }
}

TEST_CASE("CfdLinearSolve_TheLayerChecksItsOwnInputBeforeTheKernelDoes", "[cfd][linsolve][invalid]") {
    // CFD'S RIGHT-HAND-SIDE GUARD WAS UNOBSERVABLE: deleting it left every
    // assertion passing, because `linalg` reports the same problem with the
    // same index one layer down. What distinguishes them is the ORDER -- CFD
    // checks the right-hand side BEFORE symmetry, so an asymmetric matrix with
    // a non-finite right-hand side must report the value and not the asymmetry.
    const CfdSparseMatrix asymmetric = matrixOf(momentumShape(4, 1.0e-6, 1.0e-6));
    REQUIRE(asymmetric.largestSymmetryError() > 0.0);
    std::vector<double> poisoned(4, 1.0e-9);
    poisoned[2] = std::numeric_limits<double>::quiet_NaN();

    const auto refused = cfd::solveCfdPressureSystem(asymmetric, poisoned, CfdLinearSolverSettings{});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == CfdLinearSolveProblem::ValueNotFinite);
    CHECK(refused.error().index == 2);

    SECTION("an over-long right-hand side is a mismatch, not a truncation") {
        const CfdSparseMatrix square = matrixOf(pressureShape(4, 2.5e-7));
        const std::vector<double> tooLong(6, 1.0e-10);
        const auto byPressure = cfd::solveCfdPressureSystem(square, tooLong, CfdLinearSolverSettings{});
        const auto byMomentum = cfd::solveCfdMomentumSystem(square, tooLong, CfdLinearSolverSettings{});
        REQUIRE_FALSE(byPressure.has_value());
        REQUIRE_FALSE(byMomentum.has_value());
        CHECK(byPressure.error().problem == CfdLinearSolveProblem::RightHandSideLengthWrong);
        CHECK(byMomentum.error().problem == CfdLinearSolveProblem::RightHandSideLengthWrong);
    }

    SECTION("an INFINITE right-hand side entry, on an ASYMMETRIC matrix") {
        // THE INFINITY MUST RIDE ON AN ASYMMETRIC MATRIX OR THE CASE PROVES
        // NOTHING ABOUT THIS LAYER. A probe narrowing CFD's predicate from
        // `!isfinite` to `isnan` SURVIVED the first version of this section,
        // which used a symmetric matrix: the infinity simply passed CFD's guard
        // and `linalg` reported `NonFiniteSystem` with the SAME index, which
        // `translate` turns into the same `ValueNotFinite`. Identical problem,
        // identical index, nothing for an assertion to see.
        //
        // On an asymmetric matrix the two layers disagree about what is wrong,
        // and only CFD's ORDER gives the right answer: it checks the right-hand
        // side BEFORE symmetry, so a non-finite value wins. Without CFD's
        // guard the symmetry check fires first and reports
        // `MatrixIsNotSymmetric` -- true, but not the first fault.
        const CfdSparseMatrix asymmetricMatrix = matrixOf(momentumShape(4, 1.0e-6, 1.0e-6));
        REQUIRE(asymmetricMatrix.largestSymmetryError() > 0.0);
        std::vector<double> infinite(4, 1.0e-10);
        infinite[0] = -std::numeric_limits<double>::infinity();
        const auto refused2 =
            cfd::solveCfdPressureSystem(asymmetricMatrix, infinite, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused2.has_value());
        CHECK(refused2.error().problem == CfdLinearSolveProblem::ValueNotFinite);
        CHECK(refused2.error().index == 0);

        // And a negative infinity on a SYMMETRIC matrix is still refused, so
        // the predicate's breadth is covered as well as its position.
        const CfdSparseMatrix square = matrixOf(pressureShape(4, 2.5e-7));
        const auto alsoRefused = cfd::solveCfdPressureSystem(square, infinite, CfdLinearSolverSettings{});
        REQUIRE_FALSE(alsoRefused.has_value());
        CHECK(alsoRefused.error().problem == CfdLinearSolveProblem::ValueNotFinite);
    }

    SECTION("the asymmetry it reports on a refusal is the measured value") {
        // `momentumShape`'s asymmetry is exactly its flux, so the number is
        // known and a constant cannot pass for it.
        const CfdSparseMatrix matrix = matrixOf(momentumShape(16, 1.0e-6, 1.0e-6));
        std::vector<double> rhs(16, 0.0);
        rhs[0] = 1.0e-9;
        const auto refused3 = cfd::solveCfdPressureSystem(matrix, rhs, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused3.has_value());
        CHECK(refused3.error().problem == CfdLinearSolveProblem::MatrixIsNotSymmetric);
        CHECK(std::abs(refused3.error().largestSymmetryError - 1.0e-6) <= 1.0e-21);
    }
}

TEST_CASE("CfdLinearSolve_ThePortsRoutingIsExactAndObservableAtTheBoundary", "[cfd][linsolve]") {
    // THE HEADER SPENDS A PARAGRAPH ON "EXACT SYMMETRY, NOT A TOLERANCE" and
    // nothing tested near zero: the port saw only an exactly symmetric matrix
    // and one with an asymmetry of 1e-6, so `== 0.0` could become `< 1e-15`
    // undetectably. These two cases sit on either side of the exact boundary.
    const cfd::CfdLinearSolve port = cfd::cfdLinearSolve();
    const CfdLinearSolverSettings settings{};

    SECTION("an asymmetry of ONE BIT takes the momentum path") {
        std::vector<std::vector<double>> dense = pressureShape(8, 2.5e-7);
        dense[1][2] = std::nextafter(dense[1][2], 1.0);
        const CfdSparseMatrix nudged = matrixOf(dense);
        REQUIRE(nudged.largestSymmetryError() > 0.0);
        REQUIRE(nudged.largestSymmetryError() < 1.0e-15);
        std::vector<double> rhs(8, 1.0e-10);
        rhs[0] = 0.0;

        const auto throughPort = port(nudged, rhs, settings);
        const auto byMomentum = cfd::solveCfdMomentumSystem(nudged, rhs, settings);
        REQUIRE(throughPort.has_value());
        REQUIRE(byMomentum.has_value());
        // BITWISE, because only the exact test can route this to the QR: a
        // tolerance of 1e-15 would have sent it to the LDLT, which refuses it.
        CHECK(throughPort->solution == byMomentum->values);
        const auto byPressure = cfd::solveCfdPressureSystem(nudged, rhs, settings);
        CHECK_FALSE(byPressure.has_value());
    }

    SECTION("a momentum system at EXACTLY zero flux takes the pressure path") {
        // The header states this consequence; nothing tested it. At zero flux
        // the momentum operator IS symmetric positive definite, so the LDLT is
        // the right kernel for it.
        const CfdSparseMatrix zeroFlux = matrixOf(momentumShape(16, 1.0e-6, 0.0));
        REQUIRE(zeroFlux.largestSymmetryError() == 0.0);
        std::vector<double> rhs(16, 0.0);
        rhs[0] = 1.0e-9;
        const auto throughPort = port(zeroFlux, rhs, settings);
        const auto byPressure = cfd::solveCfdPressureSystem(zeroFlux, rhs, settings);
        REQUIRE(throughPort.has_value());
        REQUIRE(byPressure.has_value());
        CHECK(throughPort->solution == byPressure->values);
    }
}

TEST_CASE("CfdLinearSolve_TheProblemTableIsPinnedWholeAndThePredictorAgrees", "[cfd][linsolve]") {
    SECTION("the whole declared order, in one comparison") {
        // THE CHAIN OF `<` ASSERTIONS HAD A HOLE exactly where the two
        // hardest-to-distinguish enumerators meet: `FactorizationFailed` and
        // `SystemIsSingular` could be swapped and every link still held. The
        // table is now compared against a sequence written out here, so no link
        // can go missing again.
        const std::array<CfdLinearSolveProblem, 11> expected{
            CfdLinearSolveProblem::SettingsInvalid,
            CfdLinearSolveProblem::MatrixIsNotSquare,
            CfdLinearSolveProblem::RightHandSideLengthWrong,
            CfdLinearSolveProblem::ValueNotFinite,
            CfdLinearSolveProblem::MatrixIsNotSymmetric,
            CfdLinearSolveProblem::FactorizationFailed,
            CfdLinearSolveProblem::SystemIsSingular,
            CfdLinearSolveProblem::MatrixIsNotPositiveDefinite,
            CfdLinearSolveProblem::SolutionIsNotFinite,
            CfdLinearSolveProblem::ResidualIsNotFinite,
            CfdLinearSolveProblem::ResidualTooLarge,
        };
        REQUIRE(expected.size() == cfd::kCfdLinearSolveProblems.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            INFO("position " << i);
            CHECK(cfd::kCfdLinearSolveProblems[i] == expected[i]);
        }
    }

    SECTION("and the predictor agrees with the entry points on every shape") {
        // THE PREDICTOR WAS SHOWN ONE FAILING INPUT, so a hard-coded return
        // survived. Its promise is "the same checks in the same order".
        const CfdSparseMatrix good = matrixOf(pressureShape(8, 2.5e-7));
        const CfdSparseMatrix asymmetric = matrixOf(momentumShape(8, 1.0e-6, 1.0e-6));
        const CfdSparseMatrix unpinned = matrixOf(pressureShape(8, 2.5e-7, false));
        std::vector<double> rhs(8, 1.0e-10);
        rhs[0] = 0.0;
        std::vector<double> poisoned = rhs;
        poisoned[1] = std::numeric_limits<double>::quiet_NaN();
        const std::vector<double> tooShort(5, 1.0e-10);

        struct Shape {
            const char* what;
            const CfdSparseMatrix* matrix;
            std::vector<double> rhs;
            CfdLinearSolverSettings settings;
        };
        const std::vector<Shape> shapes = {
            {"well posed", &good, rhs, CfdLinearSolverSettings{}},
            {"asymmetric", &asymmetric, rhs, CfdLinearSolverSettings{}},
            {"unpinned and singular", &unpinned, rhs, CfdLinearSolverSettings{}},
            {"a non-finite right-hand side", &good, poisoned, CfdLinearSolverSettings{}},
            {"a short right-hand side", &good, tooShort, CfdLinearSolverSettings{}},
            {"unusable settings", &good, rhs,
             CfdLinearSolverSettings{.relativeResidualTolerance = 0.0}},
            {"a tolerance nothing can meet", &good, rhs,
             CfdLinearSolverSettings{.relativeResidualTolerance = 1.0e-300}},
        };

        for (const Shape& shape : shapes) {
            for (const bool requireSymmetry : {true, false}) {
                INFO(shape.what << ", requireSymmetry " << requireSymmetry);
                const auto solved = requireSymmetry
                                        ? cfd::solveCfdPressureSystem(*shape.matrix, shape.rhs, shape.settings)
                                        : cfd::solveCfdMomentumSystem(*shape.matrix, shape.rhs, shape.settings);
                const std::optional<CfdLinearSolveProblem> predicted =
                    cfd::cfdLinearSolveProblem(*shape.matrix, shape.rhs, shape.settings, requireSymmetry);
                CHECK(solved.has_value() == !predicted.has_value());
                if (!solved.has_value()) {
                    REQUIRE(predicted.has_value());
                    CHECK(*predicted == solved.error().problem);
                }
            }
        }
    }
}

TEST_CASE("CfdLinearSolve_RecoversAKnownPressureField", "[cfd][linsolve][smoke]") {
    // THE PRESSURE PATH HAD NO CONSTRUCT-AND-RECOVER TEST AT ALL: its whole
    // correctness evidence was `normalizedResidual <= 1e-9`, which is the gate
    // restating itself, plus the recomputed-residual check. For a pinned chain
    // the condition number grows like n^2, so a residual of 1e-9 admits a
    // relative error far larger than what is actually achieved.
    constexpr std::size_t kCells = 400;
    const CfdSparseMatrix matrix = matrixOf(pressureShape(kCells, 2.5e-7));
    std::vector<double> expected(kCells, 0.0);
    for (std::size_t i = 0; i < kCells; ++i) {
        expected[i] = 1.0e-3 * (1.0 + static_cast<double>(i) / static_cast<double>(kCells));
    }
    // Using the container's own product is not circular: a wrong `multiply`
    // yields a right-hand side the solve cannot reproduce `expected` from.
    const auto product = matrix.multiply(expected);
    REQUIRE(product.has_value());

    const auto solved = cfd::solveCfdPressureSystem(matrix, *product, CfdLinearSolverSettings{});
    REQUIRE(solved.has_value());
    CHECK(solved->symmetricKernel);

    double worst = 0.0;
    for (std::size_t i = 0; i < kCells; ++i) {
        REQUIRE(std::isfinite(solved->values[i]));
        worst = std::max(worst, std::abs(solved->values[i] - expected[i]));
    }
    // Pinned two-sidedly against the field's own magnitude rather than against
    // the gate's 1e-9.
    CHECK(worst <= 1.0e-3 * 1.0e-7);
    WARN(std::format("pressure recovery: {} cells, worst departure {:.6e}, residual {:.6e}, conditioning {:.6e}",
                     kCells, worst, solved->normalizedResidual, solved->conditioningRatio));
}

TEST_CASE("CfdLinearSolve_TheUnitPinnedPressureSystemHasAScaleDependentRatio", "[cfd][linsolve]") {
    // ADR-076 L1, PINNED AS A MEASUREMENT RATHER THAN LEFT AS PROSE. The rule
    // the qualified assembly follows is "the diagonal keeps its own value", and
    // on that path the pivot ratio is 0.25 at every scale. The ONE exception it
    // reports -- `pinnedDiagonalIsUnitValued`, a reference cell with no internal
    // face and no fixed-pressure face -- puts a dimensionless 1.0 into a matrix
    // whose other entries are the face coefficient, and the ratio then tracks
    // that coefficient linearly. Below about 2e-12 a CORRECT system is refused
    // as singular.
    //
    // Nothing in CFD can repair that: the assembly belongs to a qualified
    // milestone. What this case buys is that the boundary is a measured number
    // the suite will notice moving, and that the pressure floor's VALUE is
    // load-bearing -- raising it six decades, which no other test could see,
    // fails here.
    SECTION("the ratio is one half of the face coefficient, measured") {
        for (const double coefficient : {1.0e-3, 1.0e-6, 1.0e-9}) {
            const CfdSparseMatrix matrix = matrixOf(pressureShape(32, coefficient, true, true));
            std::vector<double> rhs(32, coefficient * 1.0e-3);
            rhs[0] = 0.0;
            const auto solved = cfd::solveCfdPressureSystem(matrix, rhs, CfdLinearSolverSettings{});
            REQUIRE(solved.has_value());
            INFO("coefficient " << coefficient << " ratio " << solved->conditioningRatio);
            CHECK(std::abs(solved->conditioningRatio - 0.5 * coefficient) <= 1.0e-14 * coefficient);
        }
    }

    SECTION("and below the floor a CORRECT system is refused, which is L1") {
        const CfdSparseMatrix matrix = matrixOf(pressureShape(32, 1.0e-13, true, true));
        std::vector<double> rhs(32, 1.0e-16);
        rhs[0] = 0.0;
        const auto refused = cfd::solveCfdPressureSystem(matrix, rhs, CfdLinearSolverSettings{});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == CfdLinearSolveProblem::SystemIsSingular);
        // The same system under the RULE's pin is solved, which is what makes
        // this a property of the pin and not of the system.
        const CfdSparseMatrix kept = matrixOf(pressureShape(32, 1.0e-13, true, false));
        const auto solved = cfd::solveCfdPressureSystem(kept, rhs, CfdLinearSolverSettings{});
        CHECK(solved.has_value());
    }
}
