#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// Inputs of a chamfer feature, e.g.
/// `ChamferDefinition{.target = padId, .edges = {edge}, .distance = 5_mm}`.
struct ChamferDefinition {
    /// The feature whose body is chamfered. The chamfer consumes it: the
    /// chamfered body is the model's result in its place.
    FeatureId target{};
    /// The edges of the target's body to chamfer, referred to by their
    /// supporting curves. See geometry::EdgeSignature for what such
    /// references survive; they are not persistent topological names.
    std::vector<geometry::EdgeSignature> edges{};
    geometry::ChamferMode mode = geometry::ChamferMode::EqualDistance;
    /// EqualDistance: on both faces. Otherwise: on the reference face. Used
    /// when no parameter drives it.
    Length distance{};
    /// Document parameter (a length) that drives `distance`, if any.
    std::optional<ParameterId> distanceParameter{};
    /// TwoDistance only.
    Length distance2{};
    /// DistanceAngle only, in (0, 90°).
    Angle angle{};
    /// TwoDistance and DistanceAngle: selects the reference face at each edge
    /// (see geometry::ChamferRequest).
    std::optional<Direction3D> referenceSide{};

    friend bool operator==(const ChamferDefinition&, const ChamferDefinition&) = default;
};

/// Checks that the definition is self-consistent (the target and its edges
/// are resolved only at regeneration): a valid target, at least one valid
/// edge without duplicates, and positive distances and angle for the mode.
/// Fields the mode does not use must be left at zero, so no input is
/// silently ignored.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const ChamferDefinition& definition);

/// Chamfers edges of another feature's body (type name "chamfer"). Stores its
/// inputs only; its body is computed by regeneration from the target's body.
class BETTERCAD_FEATURES_EXPORT ChamferFeature final : public SolidFeature {
public:
    using Definition = ChamferDefinition;
    static constexpr std::string_view kTypeName = "chamfer";

    [[nodiscard]] static Result<std::unique_ptr<ChamferFeature>> create(std::string name,
                                                                         const ChamferDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target feature and the distance parameter.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const ChamferDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const ChamferDefinition& definition);

private:
    ChamferFeature(std::string name, const ChamferDefinition& definition);

    ChamferDefinition definition_;
};

} // namespace bettercad::features
