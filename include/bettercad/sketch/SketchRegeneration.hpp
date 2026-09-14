#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/parameters/ParameterTable.hpp>
#include <bettercad/sketch/Export.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/Solver.hpp>

namespace bettercad::sketch {

/// Copies the values of driving parameters into the constraints they drive.
/// Fails if a driving parameter is missing (NotFound), is not a length
/// (DimensionMismatch) or gives an invalid value (InvalidArgument).
BETTERCAD_SKETCH_EXPORT Result<bool> applyDrivingParameters(Sketch& sketch,
                                                            const ParameterTable& parameters);

/// Regenerates a sketch: updates driven constraint values from @p parameters
/// and solves. The work happens on a copy; @p sketch changes only if the
/// solve succeeds (SolveResult::solved()). Parameter problems are errors;
/// solver outcomes, including failures, are returned as the SolveResult.
BETTERCAD_SKETCH_EXPORT Result<SolveResult> regenerateSketch(Sketch& sketch,
                                                             const ParameterTable& parameters,
                                                             const SolverOptions& options = {});

} // namespace bettercad::sketch
