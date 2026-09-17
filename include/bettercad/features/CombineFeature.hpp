#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// Inputs of a combine feature (P12-FEAT-002), e.g.
/// `CombineDefinition{.target = block, .tools = {boss}, .operation = FeatureOperation::Join}`.
struct CombineDefinition {
    /// The feature whose body the tools are combined with.
    FeatureId target{};
    /// The features whose bodies are combined with it, in order. The combine
    /// consumes the target and every tool.
    std::vector<FeatureId> tools{};
    /// Join (union), Cut (the target less every tool) or Intersect (what
    /// the target and every tool have in common).
    FeatureOperation operation = FeatureOperation::Join;

    friend bool operator==(const CombineDefinition&, const CombineDefinition&) = default;
};

/// Checks that the definition is self-consistent (references are resolved
/// only at regeneration): a valid target, one or more valid tools, none
/// repeated and none the target, and an operation other than NewBody.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const CombineDefinition& definition);

/// Combines another feature's body with the bodies of further features
/// (type name "combine"). Stores its inputs only; its body is computed by
/// regeneration from theirs.
class BETTERCAD_FEATURES_EXPORT CombineFeature final : public SolidFeature {
public:
    using Definition = CombineDefinition;
    static constexpr std::string_view kTypeName = "combine";

    [[nodiscard]] static Result<std::unique_ptr<CombineFeature>> create(std::string name,
                                                                         const CombineDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target and the tools, in order.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }
    /// The target and every tool.
    [[nodiscard]] std::vector<FeatureId> consumedFeatures() const override;

    [[nodiscard]] const CombineDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const CombineDefinition& definition);

private:
    CombineFeature(std::string name, const CombineDefinition& definition);

    CombineDefinition definition_;
};

} // namespace bettercad::features
