#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>

#include <optional>
#include <span>
#include <string>

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

} // namespace bettercad::geometry
