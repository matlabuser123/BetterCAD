#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Shell.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// Inputs of a shell feature (P12-FEAT-003), e.g.
/// `ShellDefinition{.target = block, .openFaces = {{blockId, {.role = FaceRole::EndCap}}}, .thickness = 2_mm}`.
struct ShellDefinition {
    /// The feature whose body is hollowed. The shell consumes it.
    FeatureId target{};
    /// The faces of the target's body to remove, by name (FaceName). Each
    /// must be carried by a face of the target's body when the shell
    /// regenerates; a name several faces carry removes all of them.
    std::vector<FaceName> openFaces{};
    /// Used when no parameter drives the thickness.
    Length thickness{};
    /// Document parameter (a length) that drives the thickness, if any.
    std::optional<ParameterId> thicknessParameter{};
    /// Whether the walls lie inside the body's faces or outside them.
    geometry::ShellSide side = geometry::ShellSide::Inward;

    friend bool operator==(const ShellDefinition&, const ShellDefinition&) = default;
};

/// Checks that the definition is self-consistent (the target and its faces
/// are resolved only at regeneration): a valid target, at least one valid
/// open face without repeats, a positive, finite thickness unless a
/// parameter drives it, and a known side.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const ShellDefinition& definition);

/// Hollows another feature's body into walls, opened where named faces are
/// removed (type name "shell"). Stores its inputs only; its body is computed
/// by regeneration from the target's body (geometry::shellBody()).
class BETTERCAD_FEATURES_EXPORT ShellFeature final : public SolidFeature {
public:
    using Definition = ShellDefinition;
    static constexpr std::string_view kTypeName = "shell";

    [[nodiscard]] static Result<std::unique_ptr<ShellFeature>> create(std::string name,
                                                                       const ShellDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target feature, the thickness parameter, and the features the
    /// open faces name (their generators and copying features), each once.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const ShellDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const ShellDefinition& definition);

private:
    ShellFeature(std::string name, const ShellDefinition& definition);

    ShellDefinition definition_;
};

} // namespace bettercad::features
