#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Units.hpp>
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
/// One run of a path: the edges of one sketch, in the order of travel.
struct SweepPathRun {
    SketchId sketch{};
    std::vector<EntityId> edges{};

    friend bool operator==(const SweepPathRun&, const SweepPathRun&) = default;
};

struct SweepPath {
    SketchId sketch{};
    std::vector<EntityId> edges{};
    /// Further runs, each the edges of another sketch, continuing the path
    /// in model space (P12-SWEEP-001). Empty for a path in one sketch, which
    /// is planar; with them the path may leave any one plane. Each run must
    /// start where the one before ends, and meet it as two edges of one run
    /// must: tangentially, or as two straight segments at a mitred corner.
    std::vector<SweepPathRun> runs{};

    friend bool operator==(const SweepPath&, const SweepPath&) = default;
};

/// How the profile is carried along the path.
///
/// There is one convention, measured rather than assumed
/// (docs/verification/P12-SWEEP-001/kernel-probe): the rotation-minimizing
/// frame, which adds no turn about the tangent. On a planar path the path
/// plane's normal is that frame in closed form, and the kernel's fixed
/// binormal, Frenet and corrected Frenet modes give the same solid to the
/// last digit; on a spatial path the corrected Frenet frame carries it.
/// A twist or a guide turns the section about that frame.
enum class SweepOrientation {
    /// The profile keeps its angle to the path's tangent: it turns with the
    /// path and adds no twist of its own (see geometry::makeSweep()).
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
    /// How far the section turns about the path's tangent from the start to
    /// the end (P12-SWEEP-001): theta(u) = u * twist, u the normalized path
    /// coordinate. Measured from the profile sketch's own X axis, positive
    /// right-handed about the direction of travel. Zero for no twist, which
    /// builds exactly the sweep P11-FEAT-008 built.
    Angle twist{};
    /// An angle parameter driving the twist; the literal is used without one.
    std::optional<ParameterId> twistParameter{};
    /// A guide curve: a path of its own whose turning about the path carries
    /// the section (P12-SWEEP-001). A guide and a twist say the same thing
    /// two ways, so a definition holds one or the other, never both.
    std::optional<SweepPath> guide{};
    FeatureOperation operation = FeatureOperation::NewBody;
    /// Feature whose body Join/Cut/Intersect combine with; empty for NewBody.
    std::optional<FeatureId> target{};

    friend bool operator==(const SweepDefinition&, const SweepDefinition&) = default;
};

/// Checks that the definition is self-consistent (references are resolved
/// only at regeneration): valid profile and path sketches, and different
/// ones (the path leaves the profile's plane); at least one path edge, each
/// ID valid and listed once, in the first run and in every further one; a
/// finite twist, and not a twist and a guide together; a guide that is
/// itself a valid path; and the operation/target pairing. InvalidArgument
/// otherwise.
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
    /// The profile sketch, the path's sketches (the first run's, then each
    /// further run's), the guide's sketches, the twist parameter and the
    /// target feature, each once, in that order.
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
