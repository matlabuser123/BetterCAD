#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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
    /// Tight axis-aligned bounds (x, y, z) of the geometry, not enlarged by
    /// tolerances.
    std::array<double, 3> minMm{};
    std::array<double, 3> maxMm{};
};

/// std::nullopt if the file cannot be read or transferred.
[[nodiscard]] std::optional<StepContents> readStepFile(const std::filesystem::path& path);

// --- Assembly read-back (P13-STEP-001) ----------------------------------------------------------
//
// readStepFile() answers questions about the geometry as a whole, which is
// all a part export needs. An assembly needs to be asked WHERE each thing is
// and WHICH part it is an instance of, because the failure worth catching is
// a file in which every solid is present and valid and one of them is in the
// wrong place. A total bounding box cannot see that; per-instance bounds and
// centroids can.
//
// This reads through STEPCAFControl_Reader, which is a different reader from
// the one readStepFile() uses and a different one again from the writer, so
// the measurement does not share code with the thing it measures.

/// One placed solid, as the file describes it.
struct StepShape {
    /// The instance's name, as the file carries it. Empty if it has none.
    std::string name{};
    /// The product this instance refers to. Two instances of one part share
    /// it, which is how "two placements of one product" is distinguished
    /// from "two independent solids".
    std::string product{};
    double volumeMm3 = 0.0;
    std::array<double, 3> minMm{};
    std::array<double, 3> maxMm{};
    /// Centre of volume. Distinguishes a moved solid from an unmoved one far
    /// more sharply than a bounding box does for a symmetric part.
    std::array<double, 3> centroidMm{};
};

struct StepStructure {
    /// True if the file's root is an assembly rather than a loose shape.
    bool isAssembly = false;
    /// The assembly root's name, if it has one.
    std::string name{};
    /// Distinct part products in the file, by name, in the file's order.
    std::vector<std::string> products{};
    /// Every placed instance, in the file's order.
    std::vector<StepShape> instances{};
    bool valid = false;
};

/// Reads @p path as a product structure. std::nullopt if it cannot be read.
///
/// A file written as loose shapes (a part export) comes back with
/// `isAssembly` false and one entry per shape, so the same function can be
/// pointed at either kind.
[[nodiscard]] std::optional<StepStructure> readStepStructure(const std::filesystem::path& path);

} // namespace bettercad::test
