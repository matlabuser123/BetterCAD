#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

// Test-only verification tooling: reads a STEP file back with the geometry
// kernel so export tests can compare what was written with what was
// intended. This is not a product feature (STEP import is a later
// milestone) and deliberately returns plain numbers, not BetterCAD types.
namespace bettercad::test {

struct StepContents {
    std::size_t roots = 0;  ///< Top-level shapes transferred.
    std::size_t solids = 0;
    double volumeMm3 = 0.0; ///< In the reader's unit, millimetres.
    double areaMm2 = 0.0;
    bool valid = false;     ///< Kernel validity check of the whole result.
};

/// std::nullopt if the file cannot be read or transferred.
[[nodiscard]] std::optional<StepContents> readStepFile(const std::filesystem::path& path);

} // namespace bettercad::test
