#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/geometry/Rib.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// Inputs of a rib feature (P12-FEAT-005), e.g.
/// `RibDefinition{.target = bracket, .profile = ribSketch, .edges = {line}, .thickness = 4_mm}`.
struct RibDefinition {
    /// The feature whose body the rib is joined to. The rib consumes it.
    FeatureId target{};
    /// The sketch whose plane the rib lies in and whose curves make its
    /// profile.
    SketchId profile{};
    /// The profile: lines, arcs and open splines of the sketch, head to tail,
    /// in the order of travel (the first edge's direction is the one that
    /// meets the second). The chain is open.
    std::vector<EntityId> edges{};
    /// Used when no parameter drives the thickness.
    Length thickness{};
    /// Document parameter (a length) that drives the thickness, if any.
    std::optional<ParameterId> thicknessParameter{};
    geometry::RibPlacement placement = geometry::RibPlacement::Symmetric;
    /// The rib fills the left of the profile's direction of travel (seen
    /// with the sketch's normal towards the viewer), or its right.
    bool flipped = false;

    friend bool operator==(const RibDefinition&, const RibDefinition&) = default;
};

/// Checks that the definition is self-consistent (the target, the sketch
/// and its curves are resolved only at regeneration): a valid target and
/// profile, one or more valid edges without repeats, a positive, finite
/// thickness unless a parameter drives it, and a known placement.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const RibDefinition& definition);

/// A wall between an open sketched profile and another feature's body
/// (type name "rib"). Stores its inputs only; its body is computed by
/// regeneration (geometry::addRib()). It names its faces: the side each
/// profile edge makes (FaceRole::Side with the edge) and its two walls
/// (FaceRole::StartCap on the lower side of the sketch plane, EndCap on the
/// upper).
class BETTERCAD_FEATURES_EXPORT RibFeature final : public SolidFeature {
public:
    using Definition = RibDefinition;
    static constexpr std::string_view kTypeName = "rib";

    [[nodiscard]] static Result<std::unique_ptr<RibFeature>> create(std::string name,
                                                                     const RibDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target feature, the profile sketch and the thickness parameter.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const RibDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const RibDefinition& definition);

private:
    RibFeature(std::string name, const RibDefinition& definition);

    RibDefinition definition_;
};

} // namespace bettercad::features
