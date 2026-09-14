#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// Inputs of a hole feature, e.g. a 10 mm through hole in the top face of a
/// block: `HoleDefinition{.target = padId, .face = geometry::planeSignature(
/// Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ()), .center = {50_mm, 25_mm},
/// .diameter = 10_mm}`.
struct HoleDefinition {
    /// The feature whose body is drilled. The hole consumes it: the drilled
    /// body is the model's result in its place.
    FeatureId target{};
    /// The planar face the hole starts on, by its plane and outward side (not
    /// a persistent topological name; see geometry::FaceSignature). The hole
    /// goes into the material, perpendicular to the face.
    geometry::FaceSignature face{};
    /// The centre in the face's local coordinates (see geometry::facePoint()),
    /// each used when no parameter drives it.
    Point2D center{};
    std::optional<ParameterId> centerUParameter{};
    std::optional<ParameterId> centerVParameter{};
    geometry::HoleType type = geometry::HoleType::Simple;
    geometry::HoleExtent extent = geometry::HoleExtent::Through;
    Length diameter{};
    std::optional<ParameterId> diameterParameter{};
    /// Blind only. A through hole has no depth: it goes through all material,
    /// however thick the target becomes.
    Length depth{};
    std::optional<ParameterId> depthParameter{};
    /// Counterbore only.
    Length counterboreDiameter{};
    Length counterboreDepth{};
    /// Countersink only: the cone's diameter at the face and included angle.
    Length countersinkDiameter{};
    Angle countersinkAngle{};

    friend bool operator==(const HoleDefinition&, const HoleDefinition&) = default;
};

/// Checks that the definition is self-consistent (the target, face and room
/// are checked at regeneration): a valid target and face, valid parameter
/// IDs, fields the type and extent do not use left at zero (and no depth
/// parameter for a through hole), each literal value in range, and the
/// relations between literal values (a head wider than the hole, shallower
/// than a blind hole). Relations that involve a driven value are checked at
/// regeneration.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const HoleDefinition& definition);

/// A hole drilled into a planar face of another feature's body (type name
/// "hole"). Stores its inputs only; its body is computed by regeneration.
class BETTERCAD_FEATURES_EXPORT HoleFeature final : public SolidFeature {
public:
    using Definition = HoleDefinition;
    static constexpr std::string_view kTypeName = "hole";

    [[nodiscard]] static Result<std::unique_ptr<HoleFeature>> create(std::string name,
                                                                      const HoleDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target feature, then the diameter, depth and centre parameters.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const HoleDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const HoleDefinition& definition);

private:
    HoleFeature(std::string name, const HoleDefinition& definition);

    HoleDefinition definition_;
};

} // namespace bettercad::features
