#pragma once

#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/math/BoundingBox.hpp>
#include <bettercad/features/Export.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// The checks of validateDocument(), in the order they run.
enum class ValidationCheck {
    /// References point at items of the right kind: profiles are sketches,
    /// targets are features with bodies, revolve axes are lines, and driving
    /// parameters have the right dimension (lengths for dimensions and
    /// depths, angles for revolve angles).
    DocumentConsistency,
    /// Every referenced item exists, including revolve axis lines in their
    /// profile sketches.
    MissingReferences,
    /// No item depends on itself, directly or indirectly.
    DependencyCycles,
    /// Every sketch solves with its driving parameters applied. Sketches that
    /// are not fully constrained get a warning.
    SketchConstraints,
    /// Every object regenerates.
    FeatureRegeneration,
    /// Every body is a valid solid with positive volume.
    Geometry,
};

inline constexpr std::array kValidationChecks{
    ValidationCheck::DocumentConsistency, ValidationCheck::MissingReferences,
    ValidationCheck::DependencyCycles,    ValidationCheck::SketchConstraints,
    ValidationCheck::FeatureRegeneration, ValidationCheck::Geometry,
};

/// "document consistency", "missing references", "dependency cycles",
/// "sketch constraints", "feature regeneration" or "geometry".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(ValidationCheck check) noexcept;

enum class Severity {
    Warning, ///< Worth knowing; the document is still valid.
    Error,   ///< The document is invalid.
};

/// "warning" or "error".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(Severity severity) noexcept;

struct ValidationIssue {
    ValidationCheck check = ValidationCheck::DocumentConsistency;
    Severity severity = Severity::Error;
    /// The parameter or object the issue is about, if any.
    std::optional<ObjectId> item{};
    std::string message{};
};

/// A result body of the model (see resultFeatures()).
struct BodySummary {
    ObjectId feature{};
    std::string name{};
    bool valid = false;
    geometry::TopologySummary topology{};
    std::optional<geometry::MassProperties> properties{};
    std::optional<BoundingBox3D> boundingBox{};
};

struct ValidationReport {
    /// Grouped by check, in check order; within a check, by item.
    std::vector<ValidationIssue> issues{};
    /// Result bodies, when regeneration produced them.
    std::vector<BodySummary> bodies{};
    /// Number of objects rebuilt during the regeneration check.
    std::size_t regenerated = 0;

    [[nodiscard]] BETTERCAD_FEATURES_EXPORT std::size_t count(Severity severity) const noexcept;
    [[nodiscard]] BETTERCAD_FEATURES_EXPORT std::size_t count(ValidationCheck check,
                                                              Severity severity) const noexcept;
    [[nodiscard]] bool valid() const noexcept { return count(Severity::Error) == 0; }
};

/// Runs every check on a copy of @p document; the document is not modified.
/// Each problem is reported once, under the first check that finds it: a
/// sketch that does not solve is a sketch-constraint error, not also a
/// regeneration error. Items that cannot be rebuilt because something they
/// depend on failed are reported as regeneration errors.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT ValidationReport validateDocument(const Document& document);

} // namespace bettercad::features
