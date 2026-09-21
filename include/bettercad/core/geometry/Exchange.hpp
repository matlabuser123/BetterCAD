#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/RigidTransform.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Neutral-format output of solid geometry. The functions return the file
// contents; writing files is the caller's job, so that file handling
// (Unicode paths, atomic replacement) lives in one place (bettercad_io).
namespace bettercad::geometry {

struct NamedBody {
    std::string name{};
    Body body{};
};

struct StepOptions {
    /// FILE_NAME header fields.
    std::string name{};
    std::string author{};
    std::string organization{};
    /// Defaults to "BetterCAD <version>".
    std::string originatingSystem{};
    /// ISO 8601 time stamp for the header; the current time if not set. A
    /// fixed value makes the output reproducible.
    std::optional<std::string> timeStamp{};
};

/// STEP (ISO 10303-21, AP214) text for @p bodies: one product per body,
/// named after it, with lengths in millimetres. Names must be UTF-8.
/// Fails with InvalidArgument if there are no bodies or a body is empty, and
/// with Internal if the kernel's translator fails.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<std::string> writeStep(std::span<const NamedBody> bodies,
                                                                      const StepOptions& options = {});

// --- Assemblies (P13-STEP-001) ------------------------------------------------------------------

/// One placed instance of a part.
///
/// `part` indexes StepAssembly::parts. Several instances may name the same
/// part, and that is the point: an assembly with three of one bracket writes
/// the bracket's geometry ONCE and places it three times, which is what makes
/// the file an assembly rather than three unrelated solids.
struct StepInstance {
    std::string name{};
    std::size_t part = 0;
    /// Where the instance sits, in the assembly's own coordinates.
    RigidTransform3D placement{};
};

/// A shape tree to write: distinct parts, and the instances that place them.
struct StepAssembly {
    /// Name of the assembly product itself.
    std::string name{};
    /// Each written once, as its own product. A part with no instance is
    /// refused rather than written unreferenced.
    std::vector<NamedBody> parts{};
    std::vector<StepInstance> instances{};
};

/// STEP (AP214, millimetres) for @p assembly, as a product structure: one
/// product definition per part, one component instance per placement, under
/// one assembly product.
///
/// This is deliberately NOT writeStep() with the bodies pre-transformed.
/// Pre-transforming loses the thing a downstream system wants most -- that
/// these two solids are the same part in two places -- and duplicates the
/// geometry of every repeated part in the file.
///
/// Fails with InvalidArgument if there are no instances, if a part is empty,
/// if an instance names a part that does not exist, if a part has no
/// instance, or if a placement is not finite; and with Internal if the
/// kernel's translator fails.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<std::string> writeStepAssembly(const StepAssembly& assembly,
                                                                              const StepOptions& options = {});

} // namespace bettercad::geometry
