#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Reference geometry (P12-DATUM-001): datum planes, datum axes and
// coordinate systems. They are document objects that store how they are
// placed, never where they ended up: resolvePlane(), resolveAxis() and
// resolveCoordinateSystem() compute their model-space geometry from their
// definitions, the objects they are placed from and the parameters that
// drive them. Sketches are attached to them, and mirrors and circular
// patterns refer to them.
namespace bettercad {
class Document;
}

namespace bettercad::features {

// --- Datum planes ------------------------------------------------------------------------------------

enum class DatumPlaneKind {
    /// The plane `frame`.
    Fixed,
    /// The base plane moved `offset` along its normal (the frame's axes are
    /// kept).
    Offset,
    /// The base plane turned by `angle` about `axis`, right-handed about the
    /// axis' direction. The axis must lie in the base plane.
    Angled,
};

/// "fixed", "offset" or "angled".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(DatumPlaneKind kind) noexcept;

/// How a datum plane is placed. Fields another kind uses keep their defaults.
struct DatumPlaneDefinition {
    DatumPlaneKind kind = DatumPlaneKind::Offset;
    /// Fixed.
    Frame3D frame = Frame3D::xy();
    /// Offset and Angled: the plane it is placed from.
    PlaneReference base{};
    /// Offset: literal, or a length parameter.
    Length offset{};
    std::optional<ParameterId> offsetParameter{};
    /// Angled: the axis and the angle (literal, or an angle parameter).
    AxisReference axis{};
    Angle angle{};
    std::optional<ParameterId> angleParameter{};

    friend bool operator==(const DatumPlaneDefinition&, const DatumPlaneDefinition&) = default;
};

/// Self-consistency (references are resolved at regeneration): finite
/// literals, valid IDs, and defaults in the fields the kind does not use.
/// InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const DatumPlaneDefinition& definition);

/// A datum plane (type name "datum_plane").
class BETTERCAD_FEATURES_EXPORT DatumPlane final : public DocumentObject {
public:
    using Definition = DatumPlaneDefinition;
    static constexpr std::string_view kTypeName = "datum_plane";

    [[nodiscard]] static Result<std::unique_ptr<DatumPlane>> create(std::string name, const DatumPlaneDefinition& d);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The objects and parameters the plane is placed from, in field order.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    [[nodiscard]] const DatumPlaneDefinition& definition() const noexcept { return definition_; }
    Result<bool> setDefinition(const DatumPlaneDefinition& definition);

private:
    DatumPlane(std::string name, const DatumPlaneDefinition& definition);

    DatumPlaneDefinition definition_;
};

// --- Datum axes -----------------------------------------------------------------------------------------

enum class DatumAxisKind {
    /// The line `axis`.
    Fixed,
    /// The line where `first` and `second` meet: along first normal x second
    /// normal, through the point of that line nearest the model's origin.
    Intersection,
};

/// "fixed" or "intersection".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(DatumAxisKind kind) noexcept;

struct DatumAxisDefinition {
    DatumAxisKind kind = DatumAxisKind::Fixed;
    /// Fixed.
    Axis3D axis{};
    /// Intersection.
    PlaneReference first{};
    PlaneReference second{};

    friend bool operator==(const DatumAxisDefinition&, const DatumAxisDefinition&) = default;
};

/// Self-consistency, as for datum planes. InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const DatumAxisDefinition& definition);

/// A datum axis (type name "datum_axis").
class BETTERCAD_FEATURES_EXPORT DatumAxis final : public DocumentObject {
public:
    using Definition = DatumAxisDefinition;
    static constexpr std::string_view kTypeName = "datum_axis";

    [[nodiscard]] static Result<std::unique_ptr<DatumAxis>> create(std::string name, const DatumAxisDefinition& d);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    [[nodiscard]] const DatumAxisDefinition& definition() const noexcept { return definition_; }
    Result<bool> setDefinition(const DatumAxisDefinition& definition);

private:
    DatumAxis(std::string name, const DatumAxisDefinition& definition);

    DatumAxisDefinition definition_;
};

// --- Coordinate systems -------------------------------------------------------------------------------------

enum class CoordinateSystemKind {
    /// The frame `frame` (its normal is the Z axis).
    Fixed,
    /// Placed relative to `base` (another coordinate system, or the model's):
    /// turned about the base's X, then Y, then Z axis by `rotation` (each
    /// right-handed, about axes through the base's origin), then moved by
    /// `translation` along the base's axes.
    Offset,
};

/// "fixed" or "offset".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string_view toString(CoordinateSystemKind kind) noexcept;

struct CoordinateSystemDefinition {
    CoordinateSystemKind kind = CoordinateSystemKind::Offset;
    /// Fixed.
    Frame3D frame = Frame3D::xy();
    /// Offset: a coordinate system object, or empty for the model's.
    std::optional<ObjectId> base{};
    /// Offset: along the base's X, Y and Z; each literal or a length parameter.
    std::array<Length, 3> translation{};
    std::array<std::optional<ParameterId>, 3> translationParameters{};
    /// Offset: about the base's X, Y and Z; each literal or an angle parameter.
    std::array<Angle, 3> rotation{};
    std::array<std::optional<ParameterId>, 3> rotationParameters{};

    friend bool operator==(const CoordinateSystemDefinition&, const CoordinateSystemDefinition&) = default;
};

/// Self-consistency, as for datum planes. InvalidArgument otherwise.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const CoordinateSystemDefinition& definition);

/// A coordinate system (type name "coordinate_system").
class BETTERCAD_FEATURES_EXPORT CoordinateSystem final : public DocumentObject {
public:
    using Definition = CoordinateSystemDefinition;
    static constexpr std::string_view kTypeName = "coordinate_system";

    [[nodiscard]] static Result<std::unique_ptr<CoordinateSystem>> create(std::string name,
                                                                          const CoordinateSystemDefinition& d);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    [[nodiscard]] const CoordinateSystemDefinition& definition() const noexcept { return definition_; }
    Result<bool> setDefinition(const CoordinateSystemDefinition& definition);

private:
    CoordinateSystem(std::string name, const CoordinateSystemDefinition& definition);

    CoordinateSystemDefinition definition_;
};

// --- Resolution ---------------------------------------------------------------------------------------

/// Model-space geometry of references, computed from the definitions of
/// the objects they name (and of the objects those are placed from) and the
/// current parameter values. Fails with NotFound for a missing object or
/// parameter, InvalidArgument for an object of the wrong kind or a
/// reference naming the wrong principal element, DimensionMismatch for a
/// parameter of the wrong dimension, FailedPrecondition for geometry that
/// does not exist (parallel planes do not meet; an axis off its base plane)
/// and for references nested more than 64 deep (a cycle). Messages name the
/// objects involved.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Frame3D> resolvePlane(const Document& document,
                                                                     const PlaneReference& reference);
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Axis3D> resolveAxis(const Document& document,
                                                                   const AxisReference& reference);
/// The model's coordinate system (Frame3D::xy()) for an empty @p object.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Frame3D> resolveCoordinateSystem(const Document& document,
                                                                                std::optional<ObjectId> object);

} // namespace bettercad::features
