#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/standards/ClearanceHoles.hpp>
#include <bettercad/core/standards/HoleTolerances.hpp>
#include <bettercad/core/standards/MetricThreads.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad {
class Document;
} // namespace bettercad

namespace bettercad::features {

/// The thread of a threaded hole (P12-HOLE-001): an ISO metric size and an
/// internal thread tolerance class, e.g. M8-6H. The hole is cut at the
/// thread's basic minor diameter; the thread itself is described, not
/// modelled (geometry::CosmeticThread).
struct HoleThread {
    standards::MetricThread size;
    /// One BetterCAD knows the limits of for the size (see
    /// standards::internalThreadLimits()).
    standards::ThreadToleranceClass tolerance{};
    /// From the face, used when no parameter drives it; zero for the whole
    /// hole.
    Length length{};
    std::optional<ParameterId> lengthParameter{};

    friend bool operator==(const HoleThread&, const HoleThread&) = default;
};

/// A clearance hole of a standard size (P12-HOLE-001): the diameter ISO 273
/// gives a bolt or screw of a metric size in a series.
struct HoleClearance {
    standards::MetricThread bolt;
    standards::ClearanceSeries series = standards::ClearanceSeries::Medium;

    friend bool operator==(const HoleClearance&, const HoleClearance&) = default;
};

/// Inputs of a hole feature, e.g. a 10 mm through hole in the top face of a
/// block: `HoleDefinition{.target = padId, .face = geometry::planeSignature(
/// Point3D{0_mm, 0_mm, 20_mm}, Direction3D::unitZ()), .center = {50_mm, 25_mm},
/// .diameter = 10_mm}`.
struct HoleDefinition {
    /// The feature whose body is drilled. The hole consumes it: the drilled
    /// body is the model's result in its place.
    FeatureId target{};
    /// The planar face the hole starts on, by its plane and outward side (not
    /// a persistent topological name; see geometry::FaceSignature). The hole
    /// goes into the material, perpendicular to the face.
    geometry::FaceSignature face{};
    /// The centre in the face's local coordinates (see geometry::facePoint()),
    /// each used when no parameter drives it.
    Point2D center{};
    std::optional<ParameterId> centerUParameter{};
    std::optional<ParameterId> centerVParameter{};
    geometry::HoleType type = geometry::HoleType::Simple;
    geometry::HoleExtent extent = geometry::HoleExtent::Through;
    Length diameter{};
    std::optional<ParameterId> diameterParameter{};
    /// Blind only. A through hole has no depth: it goes through all material,
    /// however thick the target becomes.
    Length depth{};
    std::optional<ParameterId> depthParameter{};
    /// Counterbore only.
    Length counterboreDiameter{};
    Length counterboreDepth{};
    /// Countersink only: the cone's diameter at the face and included angle.
    Length countersinkDiameter{};
    Angle countersinkAngle{};
    /// Spotface only (P12-HOLE-001).
    Length spotfaceDiameter{};
    Length spotfaceDepth{};
    /// A threaded hole: its diameter is the thread's basic minor diameter,
    /// so `diameter` stays zero, with no parameter.
    std::optional<HoleThread> thread{};
    /// A clearance hole of a standard size: its diameter comes from ISO 273,
    /// so `diameter` stays zero, with no parameter. Not with a thread.
    std::optional<HoleClearance> clearance{};
    /// The tolerance class of the diameter (ISO 286). Not for a threaded
    /// hole, whose thread has its own class.
    std::optional<standards::HoleToleranceClass> tolerance{};

    friend bool operator==(const HoleDefinition&, const HoleDefinition&) = default;
};

/// Checks that the definition is self-consistent (the target, face and room
/// are checked at regeneration): a valid target and face, valid parameter
/// IDs, fields the type and extent do not use left at zero (and no depth
/// parameter for a through hole), each literal value in range, and the
/// relations between literal values (a head wider than the hole, shallower
/// than a blind hole). Relations that involve a driven value are checked at
/// regeneration.
///
/// A thread or a clearance size (not both) gives the diameter, which then
/// has no literal value or parameter; a thread's class must be one whose
/// limits BetterCAD knows, and its heads wider than its major diameter. A
/// tolerance class must be known and defined for a literal or standard
/// diameter (ISO 286: up to 500 mm), and a threaded hole takes none.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const HoleDefinition& definition);

/// A hole drilled into a planar face of another feature's body (type name
/// "hole"). Stores its inputs only; its body is computed by regeneration.
class BETTERCAD_FEATURES_EXPORT HoleFeature final : public SolidFeature {
public:
    using Definition = HoleDefinition;
    static constexpr std::string_view kTypeName = "hole";

    [[nodiscard]] static Result<std::unique_ptr<HoleFeature>> create(std::string name,
                                                                      const HoleDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target feature, then the diameter, depth, centre and thread length
    /// parameters.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const HoleDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it.
    Result<bool> setDefinition(const HoleDefinition& definition);

private:
    HoleFeature(std::string name, const HoleDefinition& definition);

    HoleDefinition definition_;
};

/// A thread as a hole's callout gives it.
struct HoleThreadCallout {
    /// "M8-6H".
    std::string designation;
    standards::ThreadDiameters basic{};
    standards::InternalThreadLimits limits{};
    /// From the face; none for a thread through the whole of a through hole.
    std::optional<Length> length{};

    friend bool operator==(const HoleThreadCallout&, const HoleThreadCallout&) = default;
};

/// What a hole is in manufacturing terms (P12-HOLE-001): the diameter it is
/// cut at and, where its definition gives them, the tolerance class of that
/// diameter with its limit deviations, and its thread.
struct HoleCallout {
    Length diameter{};
    std::optional<standards::HoleToleranceClass> tolerance{};
    std::optional<standards::LimitDeviations> deviations{};
    std::optional<HoleThreadCallout> thread{};

    friend bool operator==(const HoleCallout&, const HoleCallout&) = default;
};

/// The callout of @p definition, with its driven values taken from
/// @p document (see resolveHoleRequest()). Fails as the resolution does.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<HoleCallout> holeCallout(const HoleDefinition& definition,
                                                                        const Document& document);

} // namespace bettercad::features
