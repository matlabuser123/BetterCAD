#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// One section of a loft: the closed profile of a sketch, moved along the
/// sketch plane's normal by an offset. The offset is literal, or driven by a
/// length parameter, so that the sections' spacing can be a parameter; it is
/// zero for a profile that stays on its sketch's plane.
struct LoftSection {
    SketchId sketch{};
    Length offset{};
    std::optional<ParameterId> offsetParameter{};

    friend bool operator==(const LoftSection&, const LoftSection&) = default;
};

/// How a loft passes from one section to the next.
enum class LoftInterpolation {
    /// Straight lines join matching points of consecutive sections (see
    /// geometry::makeLoft()).
    Ruled,
};

/// "ruled".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(LoftInterpolation interpolation) noexcept;

/// Inputs of a loft feature, e.g. the circle of sketch `bottom` lofted to
/// the circle of sketch `top` moved up by parameter `height`:
/// `LoftDefinition{.sections = {{.sketch = bottom}, {.sketch = top, .offsetParameter = height}}}`.
struct LoftDefinition {
    /// The sections in the order the loft passes through them. The order is
    /// the definition's: it is kept as given, never sorted.
    std::vector<LoftSection> sections{};
    LoftInterpolation interpolation = LoftInterpolation::Ruled;
    FeatureOperation operation = FeatureOperation::NewBody;
    /// Feature whose body Join/Cut/Intersect combine with; empty for NewBody.
    std::optional<FeatureId> target{};

    friend bool operator==(const LoftDefinition&, const LoftDefinition&) = default;
};

/// Checks that the definition is self-consistent (references are resolved
/// only at regeneration): at least two sections, each with a valid sketch
/// ID and a finite literal offset or a valid parameter ID; no section
/// repeated (the same sketch at the same offset); and the operation/target
/// pairing. InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const LoftDefinition& definition);

/// A solid through the closed profiles of two or more sketches, in order
/// (type name "loft"). Stores its inputs only; its body is computed by
/// regeneration.
class BETTERCAD_FEATURES_EXPORT LoftFeature final : public SolidFeature {
public:
    using Definition = LoftDefinition;
    static constexpr std::string_view kTypeName = "loft";

    [[nodiscard]] static Result<std::unique_ptr<LoftFeature>> create(std::string name,
                                                                      const LoftDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The sections' sketches (in order, each once), their offset
    /// parameters, and the target feature.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] FeatureOperation operation() const noexcept { return definition_.operation; }
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const LoftDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const LoftDefinition& definition);

private:
    LoftFeature(std::string name, const LoftDefinition& definition);

    LoftDefinition definition_;
};

} // namespace bettercad::features
