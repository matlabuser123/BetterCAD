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

/// The path of a sweep: edges of a sketch other than the profile's, in the
/// order of travel. Referring to sketch entities keeps the path parametric:
/// it follows the sketch's constraints and parameters.
///
/// The edges are lines, arcs or one circle (a closed path on its own). Each
/// must share an end with the next. The direction of travel is fixed by the
/// edges themselves:
/// - a single line or arc runs from its start to its end (sketch arcs are
///   counter-clockwise);
/// - a circle starts on the sketch's X axis through its centre and runs
///   counter-clockwise;
/// - several edges run from the first edge's end that does not meet the
///   second edge (from its start if both do, as in a closed loop of two).
struct SweepPath {
    SketchId sketch{};
    std::vector<EntityId> edges{};

    friend bool operator==(const SweepPath&, const SweepPath&) = default;
};

/// How the profile is carried along the path.
enum class SweepOrientation {
    /// The profile keeps its angle to the path's tangent and to the path's
    /// plane: it turns with the path and never twists about it (see
    /// geometry::makeSweep()).
    FollowPath,
};

/// "follow path".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(SweepOrientation orientation) noexcept;

/// Inputs of a sweep feature, e.g. the closed profiles of sketch `ring`
/// swept along lines 4 and 7 of sketch `route`:
/// `SweepDefinition{.profile = ring, .path = {.sketch = route, .edges = {line4, line7}}}`.
struct SweepDefinition {
    /// Sketch whose closed profiles are swept. The path must start on its
    /// plane and leave it at right angles.
    SketchId profile{};
    SweepPath path{};
    SweepOrientation orientation = SweepOrientation::FollowPath;
    FeatureOperation operation = FeatureOperation::NewBody;
    /// Feature whose body Join/Cut/Intersect combine with; empty for NewBody.
    std::optional<FeatureId> target{};

    friend bool operator==(const SweepDefinition&, const SweepDefinition&) = default;
};

/// Checks that the definition is self-consistent (references are resolved
/// only at regeneration): valid profile and path sketches, and different
/// ones (the path leaves the profile's plane); at least one path edge, each
/// ID valid and listed once; and the operation/target pairing.
/// InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const SweepDefinition& definition);

/// A sketch's closed profiles swept along a path of another sketch's edges
/// (type name "sweep"). Stores its inputs only; its body is computed by
/// regeneration.
class BETTERCAD_FEATURES_EXPORT SweepFeature final : public SolidFeature {
public:
    using Definition = SweepDefinition;
    static constexpr std::string_view kTypeName = "sweep";

    [[nodiscard]] static Result<std::unique_ptr<SweepFeature>> create(std::string name,
                                                                       const SweepDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The profile sketch, the path sketch and the target feature.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] FeatureOperation operation() const noexcept { return definition_.operation; }
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const SweepDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const SweepDefinition& definition);

private:
    SweepFeature(std::string name, const SweepDefinition& definition);

    SweepDefinition definition_;
};

} // namespace bettercad::features
