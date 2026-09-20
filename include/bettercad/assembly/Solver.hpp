#pragma once

#include <bettercad/assembly/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/features/FaceReferences.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// The assembly constraint solver (P13-SOLVE-001, implementing ADR-005).
//
//     P13-MATE-001   stores assembly intent
//     P13-SOLVE-001  solves that intent into derived component transforms
//
// What the solver produces is DERIVED STATE. It is returned, not written into
// the document: a component's placement intent is canonical and is never
// overwritten because a solution exists, and no transform reaches the .bcad
// file. ADR-005 settled that, and rejected persisting a solved transform even
// as a solver seed -- so this solver converges from placement intent alone,
// with no warm start. A seeded solve would depend on save history, and
// P12-PARAM-002 measured that path-dependence at 1.3e-15 per configuration
// cycle in the sketch solver.
//
// The shape deliberately mirrors sketch::Solver: the same five states, the
// same meaning for degrees of freedom, and the same discipline that a failed
// solve changes nothing. An assembly is a different equation system, not a
// different philosophy.
namespace bettercad {
class Document;
}

namespace bettercad::assembly {

/// How a solve ended. The same five the sketch solver distinguishes, because
/// CLAUDE.md requires a solve to tell them apart and never collapse to a
/// boolean: a solve that failed and a solve that succeeded into an
/// under-constrained assembly are different answers.
enum class SolveStatus {
    UnderConstrained, ///< Solved; components can still move (degreesOfFreedom > 0).
    FullyConstrained, ///< Solved; no freedom remains.
    OverConstrained,  ///< Satisfiable, but some mates are redundant.
    Inconsistent,     ///< The mates cannot all be satisfied together.
    SolverFailure,    ///< No solution was reached and none was proven impossible.
};

/// "UNDER_CONSTRAINED", "FULLY_CONSTRAINED", "OVER_CONSTRAINED",
/// "INCONSISTENT" or "SOLVER_FAILURE".
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::string_view toString(SolveStatus status) noexcept;

struct SolverOptions {
    /// A mate is satisfied when all of its residuals are within this.
    ///
    /// Every residual is a length, including the angular ones, which are
    /// scaled by the assembly's characteristic size (see the evidence). So
    /// one tolerance covers both, and the Jacobian has no mixed units.
    Length tolerance = Length::fromSi(1e-9);
    int maxIterations = 100;
};

/// What a solve produced.
struct AssemblySolveResult {
    SolveStatus status = SolveStatus::SolverFailure;
    /// 6 per component that is free to move; a grounded one has none.
    std::size_t unknowns = 0;
    std::size_t equations = 0;
    /// unknowns - rank of the constraint Jacobian at the solution. The rigid
    /// motion of an assembly with nothing grounded shows up here as 6.
    std::size_t degreesOfFreedom = 0;
    int iterations = 0;
    /// Largest residual of any equation after solving.
    Length maxResidual{};
    /// The derived transform of every component, grounded ones included.
    /// Empty unless the solve succeeded: a failed solve produces no partial
    /// answer.
    std::map<ComponentId, RigidTransform3D> transforms{};
    /// Inconsistent: the mates not satisfied at the best compromise.
    std::vector<MateId> conflicting{};
    /// OverConstrained: mates implied by mates with lower IDs.
    std::vector<MateId> redundant{};
    std::string message{};

    [[nodiscard]] bool solved() const noexcept {
        return status == SolveStatus::UnderConstrained || status == SolveStatus::FullyConstrained;
    }
};

/// Bodies of the document's features, for mate targets that name a face.
/// Empty is fine when no mate names one.
///
/// The same type face resolution already takes, rather than a second one that
/// would have to be converted at every call.
using BodyLookup = features::BodyLookup;

/// Solves @p document's enabled mates.
///
/// Fails, rather than returning a status, when the problem cannot be built at
/// all: a mate naming a component that is gone, a reference that does not
/// resolve, or a face target with no body to resolve it against. Those are
/// structured errors because they are not solver outcomes -- ADR-004 requires
/// "this reference does not resolve" to stay distinct from every other
/// failure, and a solve that never started did not converge, diverge or find
/// the system inconsistent.
///
/// The document is not modified. Component placement intent is canonical and
/// stays exactly as it was, whatever the solve returns.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<AssemblySolveResult>
solve(const Document& document, const SolverOptions& options = {}, const BodyLookup& bodies = {});

} // namespace bettercad::assembly
