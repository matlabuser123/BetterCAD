#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// What a mirror mirrors.
enum class MirrorScope {
    /// The source feature's own operation, mirrored, applied to the body the
    /// source made: one more instance, as in a pattern. A boss gets a
    /// mirrored boss, a hole a mirrored hole, a chamfer or fillet the mirrored
    /// edge; a new-body extrude or revolve a mirrored copy. The original
    /// always stays.
    Feature,
    /// The source feature's whole body (with everything that built it),
    /// mirrored: united with the original, or on its own.
    Body,
};

/// "feature" or "body".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(MirrorScope scope) noexcept;

/// The mirror plane in model space: the plane through origin + offset n̂
/// with unit normal n̂ = normal / |normal|. The normal is kept as given (any
/// finite, non-zero vector, like a pattern's direction), so a file holds
/// exactly what was entered; which way it points does not matter. The offset
/// moves the plane along its normal, so a parameter can drive the plane's
/// position, e.g. the plane x = half_length through the origin along X.
struct MirrorPlane {
    Point3D origin{};
    Vector3D normal{1.0, 0.0, 0.0};
    /// Used when no parameter drives it.
    Length offset{};
    /// A length parameter.
    std::optional<ParameterId> offsetParameter{};

    friend bool operator==(const MirrorPlane&, const MirrorPlane&) = default;
};

/// Inputs of a mirror: the source feature reflected across a plane. E.g.
/// the hole `drillId` mirrored across x = 50 mm:
/// `{.source = drillId, .plane = {.origin = {50_mm, 0_mm, 0_mm}, .normal = {1, 0, 0}}}`.
///
/// Every point p goes to p' = p - 2 ((p - p0) . n̂) n̂, where p0 is the
/// plane's point. The result is instance 0 (the source, when kept) and
/// instance 1 (its mirror image), in that order.
struct MirrorDefinition {
    /// The feature to mirror; the mirror consumes its body. For the feature
    /// scope: an extrude or revolve (new body, join or cut), a hole, a
    /// chamfer or a fillet. For the body scope: any feature with a body.
    FeatureId source{};
    MirrorPlane plane{};
    MirrorScope scope = MirrorScope::Feature;
    /// Whether the result keeps the original next to its mirror image. A
    /// feature mirror always does (the mirrored operation is added to the
    /// body that has the original's); only a body mirror can leave it out.
    bool keepOriginal = true;

    friend bool operator==(const MirrorDefinition&, const MirrorDefinition&) = default;
};

/// Checks that the definition is self-consistent (the source and a driven
/// offset are resolved at regeneration): a valid source and parameter ID, a
/// finite plane origin and offset, a finite, non-zero normal, and the
/// original kept unless the scope is the body. InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const MirrorDefinition& definition);

/// A mirror plane resolved in model space.
struct MirrorReflection {
    /// The plane's point, origin + offset n̂.
    Point3D point{};
    /// Its unit normal.
    Direction3D normal = Direction3D::unitX();
    /// The reflection across the plane.
    RigidTransform3D motion{};

    friend bool operator==(const MirrorReflection&, const MirrorReflection&) = default;
};

/// The reflection across the plane through @p point with normal @p normal.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT MirrorReflection mirrorReflection(const Point3D& point,
                                                                          const Direction3D& normal);

/// A mirror (type name "mirror"). Stores its inputs only; its body is
/// computed by regeneration. The mirror image is not a document object: it
/// is identified by the mirror (instance 1).
class BETTERCAD_FEATURES_EXPORT MirrorFeature final : public SolidFeature {
public:
    using Definition = MirrorDefinition;
    static constexpr std::string_view kTypeName = "mirror";

    [[nodiscard]] static Result<std::unique_ptr<MirrorFeature>> create(std::string name,
                                                                       const MirrorDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The source feature, then the offset parameter.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    /// The source, whose body the mirror consumes.
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.source; }

    [[nodiscard]] const MirrorDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const MirrorDefinition& definition);

private:
    MirrorFeature(std::string name, const MirrorDefinition& definition);

    MirrorDefinition definition_;
};

} // namespace bettercad::features
