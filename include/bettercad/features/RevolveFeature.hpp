#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

enum class RevolveAxisKind {
    SketchX, ///< the profile sketch's local X axis, through its origin
    SketchY, ///< the profile sketch's local Y axis, through its origin
    Line,    ///< a line entity of the profile sketch (construction or not)
};

/// The axis of a revolve, always in the profile sketch's plane. Referring to
/// sketch geometry keeps the axis parametric: it follows the sketch.
struct RevolveAxis {
    RevolveAxisKind kind = RevolveAxisKind::SketchY;
    /// The line entity, for RevolveAxisKind::Line; its direction runs from
    /// the line's start to its end.
    EntityId line{};

    [[nodiscard]] static constexpr RevolveAxis sketchX() noexcept { return {RevolveAxisKind::SketchX, {}}; }
    [[nodiscard]] static constexpr RevolveAxis sketchY() noexcept { return {RevolveAxisKind::SketchY, {}}; }
    [[nodiscard]] static constexpr RevolveAxis alongLine(EntityId line) noexcept {
        return {RevolveAxisKind::Line, line};
    }

    friend constexpr bool operator==(const RevolveAxis&, const RevolveAxis&) = default;
};

/// Which way the profile turns about the axis (right-hand rule about the
/// axis direction).
enum class RevolveDirection {
    Positive,  ///< from the profile through +angle
    Negative,  ///< from the profile through -angle
    Symmetric, ///< half the angle each way
};

/// "sketch X axis", "sketch Y axis" or "line".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(RevolveAxisKind kind) noexcept;
/// "positive", "negative" or "symmetric".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(RevolveDirection direction) noexcept;

/// Inputs of a revolve feature, e.g.
/// `RevolveDefinition{.profile = sketchId, .axis = RevolveAxis::sketchY(), .angle = 90_deg}`.
struct RevolveDefinition {
    /// Sketch whose closed profiles are revolved; they must lie on one side
    /// of the axis.
    SketchId profile{};
    RevolveAxis axis{};
    /// Sweep angle in (0, 360°] used when no parameter drives it.
    Angle angle = Angle::fromSi(2.0 * std::numbers::pi);
    /// Document parameter (an angle) that drives the sweep angle, if any.
    std::optional<ParameterId> angleParameter{};
    RevolveDirection direction = RevolveDirection::Positive;
    FeatureOperation operation = FeatureOperation::NewBody;
    /// Feature whose body Join/Cut/Intersect combine with; empty for NewBody.
    std::optional<FeatureId> target{};

    friend bool operator==(const RevolveDefinition&, const RevolveDefinition&) = default;
};

/// Fails with InvalidArgument unless @p angle is a sweep angle in (0, 360°].
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validateRevolveAngle(Angle angle);

/// Checks that the definition is self-consistent (references are resolved
/// only at regeneration): a valid profile, a valid axis line for
/// RevolveAxisKind::Line, an angle in (0, 360°] unless a parameter drives it,
/// and the operation/target pairing.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const RevolveDefinition& definition);

/// Rotation of a sketch's closed profiles about an axis in the sketch
/// plane (type name "revolve"). Stores its inputs only; its body is computed
/// by regeneration.
class BETTERCAD_FEATURES_EXPORT RevolveFeature final : public SolidFeature {
public:
    using Definition = RevolveDefinition;
    static constexpr std::string_view kTypeName = "revolve";

    [[nodiscard]] static Result<std::unique_ptr<RevolveFeature>> create(std::string name,
                                                                         const RevolveDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The profile sketch, the angle parameter and the target feature.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] FeatureOperation operation() const noexcept override { return definition_.operation; }
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const RevolveDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const RevolveDefinition& definition);

private:
    RevolveFeature(std::string name, const RevolveDefinition& definition);

    RevolveDefinition definition_;
};

} // namespace bettercad::features
