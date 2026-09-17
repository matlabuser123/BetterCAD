#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>
#include <bettercad/features/Pattern.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// How the instances of a circular pattern are spread around the axis.
enum class CircularSpacing {
    /// Around the whole circle: 360°/count apart. There is never an instance
    /// at 360°, which would be the source again.
    FullCircle,
    /// From the source to the last instance `angle` in all, so the first and
    /// last are `angle` apart and neighbours angle/(count - 1).
    IncludedAngle,
    /// `angle` from one instance to the next.
    AngleStep,
};

/// "full circle", "included angle" or "angle step".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(CircularSpacing spacing) noexcept;

/// Which way the instances turn about the axis.
enum class RotationDirection {
    Positive, ///< counter-clockwise looking against the axis' direction (right-hand rule)
    Negative, ///< the other way
};

/// "positive" or "negative".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(RotationDirection direction) noexcept;

/// The axis of a circular pattern: the line through `origin` along
/// `direction`. The same content as an Axis3D, with the direction kept as
/// given (any finite, non-zero vector) and normalized when used, as for a
/// linear pattern's direction.
///
/// With a `reference` (P12-DATUM-001), the axis is the referenced axis (a
/// datum axis, a coordinate system's or the model's principal axis); origin
/// and direction then keep their defaults.
struct PatternAxis {
    Point3D origin{};
    Vector3D direction{0.0, 0.0, 1.0};
    std::optional<AxisReference> reference{};

    friend bool operator==(const PatternAxis&, const PatternAxis&) = default;
};

/// Inputs of a circular pattern: the source feature's operation repeated
/// around an axis. E.g. six holes around the Z axis:
/// `{.source = boltId, .axis = {.origin = {}, .direction = {0, 0, 1}},
/// .count = 6}` (a full circle).
///
/// Instance i is the source turned by i × step about the axis, where the
/// step is 360°/count (full circle), angle/(count - 1) (included angle) or
/// angle (angle step), negated for RotationDirection::Negative. Instance 0
/// is the source itself. The orbit radius is the source's distance from the
/// axis; the pattern has no radius of its own.
struct CircularPatternDefinition {
    /// The feature whose operation is repeated, as for a linear pattern: an
    /// extrude or revolve (new body, join or cut), a hole, a chamfer or a
    /// fillet. The pattern consumes it.
    FeatureId source{};
    PatternAxis axis{};
    /// The number of instances, the source included: 1 is the source alone.
    /// Used when no parameter drives it.
    std::uint32_t count = 1;
    /// A dimensionless parameter, which must hold a whole number.
    std::optional<ParameterId> countParameter{};
    CircularSpacing spacing = CircularSpacing::FullCircle;
    /// IncludedAngle: the span from the source to the last instance, in
    /// (0, 360°). AngleStep: from one instance to the next, in (0, 360°),
    /// and (count - 1) steps must stay below 360°. FullCircle: zero, unused.
    /// Used when no parameter drives it.
    Angle angle{};
    std::optional<ParameterId> angleParameter{};
    RotationDirection direction = RotationDirection::Positive;
    /// Whether the instances sit on both sides of the source, which is then
    /// the middle one (P12-PATTERN-001): the span is centred on the source
    /// and the count must be odd, as for a linear pattern. A full circle
    /// takes no symmetry: its instances already go all the way round.
    bool symmetric = false;
    /// The instances that make no geometry (P12-PATTERN-001): indices from
    /// 1 (0 is the source), each below the count, listed once. Suppressing
    /// an instance never renumbers another.
    std::vector<std::uint32_t> suppressed{};

    friend bool operator==(const CircularPatternDefinition&, const CircularPatternDefinition&) = default;
};

/// Checks that the definition is self-consistent (the source and driven
/// values are resolved at regeneration): a valid source and parameter IDs, a
/// finite axis origin and a finite, non-zero axis direction, no angle for a
/// full circle, and for literal values a count from 1 to
/// kMaxPatternInstances and an angle that never reaches 360°.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const CircularPatternDefinition& definition);

/// One instance of a circular pattern.
struct CircularPatternInstance {
    /// Whether the definition suppresses this instance, so that it makes no
    /// geometry. Its index stays its own (P12-PATTERN-001).
    bool suppressed = false;
    /// Position in the pattern, deterministic: 0 is the source, then one
    /// step further each, in the pattern's direction.
    std::size_t index = 0;
    /// How far the source turns: index × step, computed for each instance
    /// from the source, never by adding to the previous one. Negative for
    /// RotationDirection::Negative.
    Angle angle{};
    /// The rotation by `angle` about the axis.
    RigidTransform3D motion{};

    friend bool operator==(const CircularPatternInstance&, const CircularPatternInstance&) = default;
};

/// The instances of a pattern of @p count instances, @p step apart (signed)
/// about @p axis, in order. A symmetric pattern turns its copies both ways
/// (patternStepMultiple()); instances whose index is in @p suppressed are
/// marked, not left out.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::vector<CircularPatternInstance>
circularPatternInstances(const Axis3D& axis, std::size_t count, Angle step, bool symmetric = false,
                         const std::vector<std::uint32_t>& suppressed = {});

/// The signed angle between neighbouring instances: 360°/count, angle/(count
/// - 1) or angle for the three spacings (0 for a single instance), negated
/// for RotationDirection::Negative. The values are not checked here.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Angle circularPatternStep(CircularSpacing spacing, std::size_t count,
                                                                  Angle angle, RotationDirection direction);

/// A circular pattern (type name "circular_pattern"). Stores its inputs
/// only; its body, and the instances in it, are computed by regeneration.
/// As for linear patterns, the instances are not document objects: they are
/// identified by the pattern and their index.
class BETTERCAD_FEATURES_EXPORT CircularPatternFeature final : public SolidFeature {
public:
    using Definition = CircularPatternDefinition;
    static constexpr std::string_view kTypeName = "circular_pattern";

    [[nodiscard]] static Result<std::unique_ptr<CircularPatternFeature>> create(
        std::string name, const CircularPatternDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The source feature, then the count and angle parameters.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    /// The source, whose body the pattern consumes.
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.source; }

    [[nodiscard]] const CircularPatternDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const CircularPatternDefinition& definition);

private:
    CircularPatternFeature(std::string name, const CircularPatternDefinition& definition);

    CircularPatternDefinition definition_;
};

} // namespace bettercad::features
