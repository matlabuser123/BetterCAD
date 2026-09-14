#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// Which side of the sketch plane the extrusion goes to.
enum class ExtrudeDirection {
    Normal,    ///< from the plane along its normal
    Reversed,  ///< from the plane against its normal
    Symmetric, ///< half the depth on each side
};

/// How the feature's solid combines with an existing body.
enum class FeatureOperation {
    NewBody,   ///< the extruded solid on its own
    Join,      ///< union with the target body
    Cut,       ///< target body minus the extruded solid
    Intersect, ///< intersection with the target body
};

[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(ExtrudeDirection direction) noexcept;
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(FeatureOperation operation) noexcept;

/// Inputs of an extrude feature, e.g.
/// `ExtrudeDefinition{.profile = sketchId, .depth = 20_mm}`.
struct ExtrudeDefinition {
    /// Sketch whose closed profiles are extruded.
    SketchId profile{};
    /// Depth used when no parameter drives it.
    Length depth{};
    /// Document parameter (a length) that drives the depth, if any.
    std::optional<ParameterId> depthParameter{};
    ExtrudeDirection direction = ExtrudeDirection::Normal;
    FeatureOperation operation = FeatureOperation::NewBody;
    /// Feature whose body Join/Cut/Intersect combine with; empty for NewBody.
    std::optional<FeatureId> target{};

    friend bool operator==(const ExtrudeDefinition&, const ExtrudeDefinition&) = default;
};

/// Checks that the definition is self-consistent (references are resolved
/// only at regeneration).
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const ExtrudeDefinition& definition);

/// A linear extrusion of a sketch's closed profiles (type name "extrude").
/// The feature stores its inputs only; its body is computed by regeneration.
class BETTERCAD_FEATURES_EXPORT ExtrudeFeature final : public DocumentObject {
public:
    [[nodiscard]] static Result<std::unique_ptr<ExtrudeFeature>> create(std::string name,
                                                                         const ExtrudeDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return "extrude"; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The profile sketch, the depth parameter and the target feature.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    /// Typed view of id(); invalid until the feature is in a document.
    [[nodiscard]] FeatureId featureId() const noexcept { return FeatureId::fromValue(id().value()); }
    [[nodiscard]] const ExtrudeDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const ExtrudeDefinition& definition);

private:
    ExtrudeFeature(std::string name, const ExtrudeDefinition& definition);

    ExtrudeDefinition definition_;
};

} // namespace bettercad::features
