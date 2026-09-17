#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Split.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// Inputs of a split feature (P12-FEAT-002), e.g.
/// `SplitDefinition{.target = block, .plane = {.object = middlePlane}, .keep = SplitKeep::Front}`.
struct SplitDefinition {
    /// The feature whose body is split. The split consumes it.
    FeatureId target{};
    /// The splitting plane: any plane reference (a principal plane, a datum
    /// plane, a coordinate system's plane or a named face).
    PlaneReference plane{};
    /// What is kept: the part in front of the plane (the side its normal
    /// points to), behind it, or both, as separate solids of one body.
    geometry::SplitKeep keep = geometry::SplitKeep::Both;

    friend bool operator==(const SplitDefinition&, const SplitDefinition&) = default;
};

/// Checks that the definition is self-consistent (references are resolved
/// only at regeneration): a valid target, a valid plane reference, a known
/// side to keep.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const SplitDefinition& definition);

/// Cuts another feature's body with a plane (type name "split"). Stores its
/// inputs only; its body is computed by regeneration from the target's body.
class BETTERCAD_FEATURES_EXPORT SplitFeature final : public SolidFeature {
public:
    using Definition = SplitDefinition;
    static constexpr std::string_view kTypeName = "split";

    [[nodiscard]] static Result<std::unique_ptr<SplitFeature>> create(std::string name,
                                                                       const SplitDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target feature and the objects the plane refers to.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const SplitDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const SplitDefinition& definition);

private:
    SplitFeature(std::string name, const SplitDefinition& definition);

    SplitDefinition definition_;
};

} // namespace bettercad::features
