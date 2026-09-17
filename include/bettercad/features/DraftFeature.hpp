#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// Inputs of a draft feature (P12-FEAT-004), e.g.
/// `DraftDefinition{.target = block, .faces = {side}, .neutralPlane = {}, .angle = 3_deg}`.
struct DraftDefinition {
    /// The feature whose body is drafted. The draft consumes it.
    FeatureId target{};
    /// The faces of the target's body to turn, by name (FaceName). Each must
    /// be carried by a face of the target's body when the draft regenerates;
    /// a name several faces carry turns all of them, and faces tangent to a
    /// turned face turn with it (see geometry::draftFaces()).
    std::vector<FaceName> faces{};
    /// The plane the faces turn about: any plane reference (a principal
    /// plane, a datum plane, a coordinate system's plane or a named face,
    /// whose normal points out of the material). Its normal is the pull
    /// direction.
    PlaneReference neutralPlane{};
    /// Used when no parameter drives the angle; in (-90, 90) deg. Going along
    /// the pull direction, a positive angle takes material away.
    Angle angle{};
    /// Document parameter (an angle) that drives the angle, if any.
    std::optional<ParameterId> angleParameter{};

    friend bool operator==(const DraftDefinition&, const DraftDefinition&) = default;
};

/// Checks that the definition is self-consistent (the target, its faces and
/// the plane are resolved only at regeneration): a valid target, at least
/// one valid face without repeats, a valid plane reference, and an angle in
/// (-90, 90) deg unless a parameter drives it.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const DraftDefinition& definition);

/// Tapers named faces of another feature's body about a neutral plane (type
/// name "draft"). Stores its inputs only; its body is computed by
/// regeneration from the target's body (geometry::draftFaces()).
class BETTERCAD_FEATURES_EXPORT DraftFeature final : public SolidFeature {
public:
    using Definition = DraftDefinition;
    static constexpr std::string_view kTypeName = "draft";

    [[nodiscard]] static Result<std::unique_ptr<DraftFeature>> create(std::string name,
                                                                       const DraftDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target feature, the angle parameter, the neutral plane's objects,
    /// and the features the faces name (their generators and copying
    /// features), each once.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const DraftDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const DraftDefinition& definition);

private:
    DraftFeature(std::string name, const DraftDefinition& definition);

    DraftDefinition definition_;
};

} // namespace bettercad::features
