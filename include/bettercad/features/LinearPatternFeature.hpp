#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/math/Direction.hpp>
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

/// One direction of a linear pattern: `count` instances in a row, the
/// source included, `spacing` apart along `direction`.
struct PatternDirection {
    /// Any finite, non-zero vector; only its direction is used (it is
    /// normalized), e.g. {1, 1, 0} for the diagonal of the XY plane. To
    /// pattern the other way, reverse the vector.
    Vector3D direction{1.0, 0.0, 0.0};
    /// The number of instances along this direction, the source included:
    /// 1 is the source alone, 2 the source and one copy. Used when no
    /// parameter drives it.
    std::uint32_t count = 1;
    /// A dimensionless parameter, which must hold a whole number.
    std::optional<ParameterId> countParameter{};
    /// From one instance to the next, measured along the direction. Used
    /// when no parameter drives it.
    Length spacing{};
    std::optional<ParameterId> spacingParameter{};

    friend bool operator==(const PatternDirection&, const PatternDirection&) = default;
};

/// Inputs of a linear pattern: the source feature's operation repeated
/// along one direction, or two for a grid. E.g. five holes 20 mm apart
/// along X: `{.source = drillId, .first = {.direction = {1, 0, 0},
/// .count = 5, .spacing = 20_mm}}`.
///
/// Instance (i, j), i steps along the first direction and j along the
/// second, is the source moved by i s1 d1 + j s2 d2, where d1 and d2 are the
/// normalized directions. Instance (0, 0) is the source itself.
struct LinearPatternDefinition {
    /// The feature whose operation is repeated: an extrude or revolve (new
    /// body, join or cut), a hole, a chamfer or a fillet. The pattern
    /// consumes it: the patterned body is the model's result in its place.
    FeatureId source{};
    PatternDirection first{};
    /// For a grid. Its direction must not be parallel to the first.
    std::optional<PatternDirection> second{};

    friend bool operator==(const LinearPatternDefinition&, const LinearPatternDefinition&) = default;
};

/// Checks that the definition is self-consistent (the source and driven
/// values are resolved at regeneration): a valid source and parameter IDs,
/// finite non-zero directions that are not parallel, and for literal values
/// a count of at least 1, a positive finite spacing and at most
/// kMaxPatternInstances instances.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const LinearPatternDefinition& definition);

/// A pattern direction with its driven values resolved.
struct PatternStep {
    Direction3D direction = Direction3D::unitX();
    std::size_t count = 1;
    Length spacing{};
};

/// One instance of a linear pattern.
struct PatternInstance {
    /// Position in the pattern, deterministic: 0 is the source, then along
    /// the first direction and row by row, index = first + second × count1.
    std::size_t index = 0;
    /// Steps along the first and second directions.
    std::size_t first = 0;
    std::size_t second = 0;
    /// How far the source moves: first s1 d1 + second s2 d2, computed from
    /// the source for each instance, never by adding to the previous one.
    Translation3D offset{};

    friend bool operator==(const PatternInstance&, const PatternInstance&) = default;
};

/// The instances of a pattern with these steps, in order.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::vector<PatternInstance>
patternInstances(const PatternStep& first, const std::optional<PatternStep>& second = std::nullopt);

/// A linear pattern (type name "linear_pattern"). Stores its inputs only;
/// its body, and the instances in it, are computed by regeneration. The
/// instances are not document objects: they are identified by the pattern
/// and their index.
class BETTERCAD_FEATURES_EXPORT LinearPatternFeature final : public SolidFeature {
public:
    using Definition = LinearPatternDefinition;
    static constexpr std::string_view kTypeName = "linear_pattern";

    [[nodiscard]] static Result<std::unique_ptr<LinearPatternFeature>> create(
        std::string name, const LinearPatternDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The source feature, then the count and spacing parameters of each
    /// direction.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    /// The source, whose body the pattern consumes.
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.source; }

    [[nodiscard]] const LinearPatternDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const LinearPatternDefinition& definition);

private:
    LinearPatternFeature(std::string name, const LinearPatternDefinition& definition);

    LinearPatternDefinition definition_;
};

} // namespace bettercad::features
