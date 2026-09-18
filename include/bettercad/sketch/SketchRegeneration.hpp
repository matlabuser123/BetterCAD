#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/Configurations.hpp>
#include <bettercad/core/parameters/ParameterTable.hpp>
#include <bettercad/sketch/Export.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/sketch/Solver.hpp>

namespace bettercad::sketch {

/// Copies the values of driving parameters into the constraints they drive:
/// angles into Angle constraints, lengths into the others. Fails if a
/// driving parameter is missing (NotFound), has the wrong dimension (an
/// angle for an Angle constraint, a length otherwise; DimensionMismatch) or
/// gives an invalid value (InvalidArgument).
///
/// @p overrides are the values in force under a document's active
/// configuration (P12-PARAM-002); a parameter it names takes that value
/// instead of its own. Empty -- the default -- is the base configuration,
/// which is what every caller did before configurations existed.
BETTERCAD_SKETCH_EXPORT Result<bool> applyDrivingParameters(Sketch& sketch,
                                                            const ParameterTable& parameters,
                                                            const ParameterOverrides& overrides = {});

/// Regenerates a sketch: updates driven constraint values from @p parameters
/// and solves. The work happens on a copy; @p sketch changes only if the
/// solve succeeds (SolveResult::solved()). Parameter problems are errors;
/// solver outcomes, including failures, are returned as the SolveResult.
BETTERCAD_SKETCH_EXPORT Result<SolveResult> regenerateSketch(Sketch& sketch,
                                                             const ParameterTable& parameters,
                                                             const SolverOptions& options = {},
                                                             const ParameterOverrides& overrides = {});

} // namespace bettercad::sketch
