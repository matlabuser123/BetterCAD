#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

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

[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(ExtrudeDirection direction) noexcept;

/// Where the extrusion ends (P12-FEAT-001).
enum class ExtrudeTermination {
    Blind,      ///< at its depth
    ThroughAll, ///< through all the material of its target, however thick
};

/// "blind", "through all".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(ExtrudeTermination termination) noexcept;

/// Inputs of an extrude feature, e.g.
/// `ExtrudeDefinition{.profile = sketchId, .depth = 20_mm}`, or a cut through
/// all of its target:
/// `ExtrudeDefinition{.profile = sketchId, .direction = ExtrudeDirection::Reversed,
/// .operation = FeatureOperation::Cut, .target = block,
/// .termination = ExtrudeTermination::ThroughAll}`.
struct ExtrudeDefinition {
    /// Sketch whose closed profiles are extruded.
    SketchId profile{};
    /// Depth used when no parameter drives it. A through-all extrude has
    /// none (zero, and no parameter).
    Length depth{};
    /// Document parameter (a length) that drives the depth, if any.
    std::optional<ParameterId> depthParameter{};
    ExtrudeDirection direction = ExtrudeDirection::Normal;
    FeatureOperation operation = FeatureOperation::NewBody;
    /// Feature whose body Join/Cut/Intersect combine with; empty for NewBody.
    std::optional<FeatureId> target{};
    /// ThroughAll: a cut (the only operation it takes) whose tool reaches
    /// through the target's whole body in `direction` (both ways when
    /// symmetric). The tool's length is not stored: regeneration takes it
    /// from the target body's bounds.
    ExtrudeTermination termination = ExtrudeTermination::Blind;

    friend bool operator==(const ExtrudeDefinition&, const ExtrudeDefinition&) = default;
};

/// Checks that the definition is self-consistent (references are resolved
/// only at regeneration): a blind extrude has a positive depth or a depth
/// parameter; a through-all extrude has neither and is a cut.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const ExtrudeDefinition& definition);

/// A linear extrusion of a sketch's closed profiles (type name "extrude").
/// The feature stores its inputs only; its body is computed by regeneration.
class BETTERCAD_FEATURES_EXPORT ExtrudeFeature final : public SolidFeature {
public:
    using Definition = ExtrudeDefinition;
    static constexpr std::string_view kTypeName = "extrude";

    [[nodiscard]] static Result<std::unique_ptr<ExtrudeFeature>> create(std::string name,
                                                                         const ExtrudeDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The profile sketch, the depth parameter and the target feature.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] FeatureOperation operation() const noexcept { return definition_.operation; }
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const ExtrudeDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const ExtrudeDefinition& definition);

private:
    ExtrudeFeature(std::string name, const ExtrudeDefinition& definition);

    ExtrudeDefinition definition_;
};

} // namespace bettercad::features
