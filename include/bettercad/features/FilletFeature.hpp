#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// Inputs of a constant-radius fillet feature, e.g.
/// `FilletDefinition{.target = padId, .edges = {edge}, .radius = 5_mm}`.
struct FilletDefinition {
    /// The feature whose body is rounded. The fillet consumes it: the rounded
    /// body is the model's result in its place.
    FeatureId target{};
    /// The edges of the target's body to round, referred to by their
    /// supporting curves (see geometry::EdgeSignature; not persistent
    /// topological names). Edges that continue a selected edge smoothly are
    /// rounded with it (see geometry::filletEdges()).
    std::vector<geometry::EdgeSignature> edges{};
    /// Used when no parameter drives the radius.
    Length radius{};
    /// Document parameter (a length) that drives the radius, if any.
    std::optional<ParameterId> radiusParameter{};

    friend bool operator==(const FilletDefinition&, const FilletDefinition&) = default;
};

/// Checks that the definition is self-consistent (the target and its edges
/// are resolved only at regeneration): a valid target, at least one valid
/// edge without duplicates, and a positive, finite radius unless a parameter
/// drives it.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const FilletDefinition& definition);

/// Rounds edges of another feature's body with a constant radius (type name
/// "fillet"). Stores its inputs only; its body is computed by regeneration
/// from the target's body.
class BETTERCAD_FEATURES_EXPORT FilletFeature final : public SolidFeature {
public:
    using Definition = FilletDefinition;
    static constexpr std::string_view kTypeName = "fillet";

    [[nodiscard]] static Result<std::unique_ptr<FilletFeature>> create(std::string name,
                                                                        const FilletDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target feature and the radius parameter.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const FilletDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const FilletDefinition& definition);

private:
    FilletFeature(std::string name, const FilletDefinition& definition);

    FilletDefinition definition_;
};

} // namespace bettercad::features
