#include <bettercad/drawing/Dimensions.hpp>

#include <bettercad/assembly/Component.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/FaceReferences.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <format>
#include <utility>

namespace bettercad::drawing {
namespace {

/// How near two directions must be to count as parallel.
///
/// 1e-9 on the cosine, which is the figure the geometry module uses for two
/// directions agreeing. A measurement between planes that are only NEARLY
/// parallel has no single answer, and a tolerance loose enough to accept one
/// would be a tolerance loose enough to give the wrong one.
constexpr double kParallelSi = 1e-9;

[[nodiscard]] std::unexpected<Error> notFound(DimensionId id) {
    return makeError(ErrorCode::NotFound, std::format("{} is not a dimension of this document", id));
}

/// What a target resolves to in model space.
struct Anchor {
    enum class Kind : std::uint8_t { Plane, Axis, Cylinder };
    Kind kind = Kind::Plane;
    Frame3D plane = Frame3D::xy();
    Axis3D axis{};
    Length radius{};

    /// The direction the anchor is "along": a plane's normal, or an axis's
    /// own direction.
    [[nodiscard]] Direction3D direction() const noexcept {
        return kind == Kind::Plane ? plane.normal() : axis.direction;
    }
    [[nodiscard]] Point3D origin() const noexcept {
        return kind == Kind::Plane ? plane.origin() : axis.origin;
    }
};

[[nodiscard]] double dot(const Direction3D& a, const Direction3D& b) noexcept {
    return a.x() * b.x() + a.y() * b.y() + a.z() * b.z();
}

/// Vector3D carries plain SI metres, so everything below works in SI and
/// converts once, at the end, when the answer becomes a Length.
[[nodiscard]] Vector3D between(const Point3D& from, const Point3D& to) noexcept {
    return Vector3D{(to.x - from.x).si(), (to.y - from.y).si(), (to.z - from.z).si()};
}

[[nodiscard]] double along(const Vector3D& v, const Direction3D& d) noexcept {
    return v.x * d.x() + v.y * d.y() + v.z * d.z();
}

/// @p v with everything along @p d taken out of it.
[[nodiscard]] Vector3D perpendicularTo(const Vector3D& v, const Direction3D& d) noexcept {
    const double amount = along(v, d);
    return Vector3D{v.x - amount * d.x(), v.y - amount * d.y(), v.z - amount * d.z()};
}

[[nodiscard]] Result<Anchor> resolveTarget(const Document& document, const DimensionTarget& target,
                                           const features::BodyLookup& bodies) {
    if (target.plane) {
        auto plane = features::resolvePlane(document, *target.plane, bodies);
        if (!plane) {
            return std::unexpected(plane.error());
        }
        return Anchor{.kind = Anchor::Kind::Plane, .plane = *plane};
    }
    if (target.axis) {
        auto axis = features::resolveAxis(document, *target.axis, bodies);
        if (!axis) {
            return std::unexpected(axis.error());
        }
        return Anchor{.kind = Anchor::Kind::Axis, .axis = *axis};
    }
    auto cylinder = features::resolveFaceCylinder(document, *target.cylinder, bodies);
    if (!cylinder) {
        return std::unexpected(cylinder.error());
    }
    return Anchor{.kind = Anchor::Kind::Cylinder, .axis = cylinder->axis, .radius = cylinder->radius};
}

/// The anchor moved by @p motion.
[[nodiscard]] Result<Anchor> moved(const Anchor& anchor, const RigidTransform3D& motion) {
    Anchor result = anchor;
    if (anchor.kind == Anchor::Kind::Plane) {
        auto frame = Frame3D::fromAxes(motion.apply(anchor.plane.origin()),
                                       motion.apply(anchor.plane.xAxis()),
                                       motion.apply(anchor.plane.yAxis()),
                                       motion.apply(anchor.plane.normal()));
        if (!frame) {
            return std::unexpected(frame.error());
        }
        result.plane = *frame;
        return result;
    }
    result.axis = Axis3D{motion.apply(anchor.axis.origin), motion.apply(anchor.axis.direction)};
    return result;
}

/// The vector from @p from to @p to, perpendicular to both.
///
/// This is the whole of what a linear-family dimension measures. Every case
/// needs the two anchors to be parallel, because two things that are not
/// parallel have no ONE distance between them -- the answer would depend on
/// where along them it was taken, and picking a place is the silent wrong
/// answer this milestone exists to avoid.
[[nodiscard]] Result<Vector3D> separation(const Anchor& from, const Anchor& to) {
    const Direction3D a = from.direction();
    const Direction3D b = to.direction();

    if (from.kind == Anchor::Kind::Plane && to.kind == Anchor::Kind::Plane) {
        if (std::abs(std::abs(dot(a, b)) - 1.0) > kParallelSi) {
            return makeError(ErrorCode::FailedPrecondition,
                             "the two planes are not parallel, so there is no one distance "
                             "between them; an angular dimension measures what they do have");
        }
        const double distance = from.plane.signedDistance(to.plane.origin()).si();
        return Vector3D{distance * a.x(), distance * a.y(), distance * a.z()};
    }

    if (from.kind != Anchor::Kind::Plane && to.kind != Anchor::Kind::Plane) {
        if (std::abs(std::abs(dot(a, b)) - 1.0) > kParallelSi) {
            return makeError(ErrorCode::FailedPrecondition,
                             "the two axes are not parallel, so there is no one distance between "
                             "them");
        }
        return perpendicularTo(between(from.origin(), to.origin()), a);
    }

    // One plane and one axis. The axis has to lie parallel to the plane, or
    // it crosses it and the distance is zero somewhere and not zero
    // elsewhere.
    const Anchor& plane = from.kind == Anchor::Kind::Plane ? from : to;
    const Anchor& axis = from.kind == Anchor::Kind::Plane ? to : from;
    const Direction3D& normal = plane.plane.normal();
    if (std::abs(dot(normal, axis.axis.direction)) > kParallelSi) {
        return makeError(ErrorCode::FailedPrecondition,
                         "the axis is not parallel to the plane, so it crosses it and there is no "
                         "one distance between them");
    }
    const double distance = plane.plane.signedDistance(axis.axis.origin).si();
    // The vector runs FROM the first target TO the second, so it turns round
    // when the axis was named first. An ordinate reads its sign, and a
    // dimension that reported the same coordinate on both sides of its datum
    // would say nothing.
    const double sign = &plane == &from ? 1.0 : -1.0;
    return Vector3D{sign * distance * normal.x(), sign * distance * normal.y(),
                    sign * distance * normal.z()};
}

/// The angle a protractor would read between two targets.
///
/// TWO PLANES: the angle between the PLANES, not between their normals --
/// 180 degrees minus the angle the normals make. `resolveFacePlane` faces a
/// face's normal out of the material, so this is the angle measured through
/// the material, which is what an angle marked on a drawing means. It comes
/// out right in every case: two faces of a slab (normals opposed) read 0 and
/// are parallel; two coplanar faces (normals together) read 180 and are flat;
/// a wedge whose faces meet at 53.13 reads 53.13.
///
/// TWO AXES: the angle between the two LINES, folded into [0, 90]. An axis's
/// direction may be stored either way round, and an angle that flipped to its
/// supplement because a datum was defined in the other direction would be a
/// number that depended on how the model was typed rather than on its shape.
///
/// atan2 of the cross against the dot, never acos of the dot: acos loses its
/// precision exactly where two faces are nearly parallel or nearly opposed --
/// which is where an angular dimension is most often placed -- and walks out
/// of its domain into NaN as soon as rounding pushes the dot past 1.
[[nodiscard]] Result<Angle> angleBetween(const Anchor& from, const Anchor& to) {
    if (from.kind == Anchor::Kind::Cylinder || to.kind == Anchor::Kind::Cylinder) {
        return makeError(ErrorCode::InvalidArgument,
                         "an angular dimension measures between planes or between axes");
    }
    const bool planes = from.kind == Anchor::Kind::Plane;
    if (planes != (to.kind == Anchor::Kind::Plane)) {
        return makeError(ErrorCode::InvalidArgument,
                         "an angular dimension measures between two planes or between two axes, "
                         "not between a plane and an axis");
    }
    const Direction3D a = from.direction();
    const Direction3D b = to.direction();
    const double cx = a.y() * b.z() - a.z() * b.y();
    const double cy = a.z() * b.x() - a.x() * b.z();
    const double cz = a.x() * b.y() - a.y() * b.x();
    const double betweenDirections = std::atan2(std::hypot(cx, cy, cz), dot(a, b));
    if (planes) {
        return Angle::fromSi(std::numbers::pi - betweenDirections);
    }
    return Angle::fromSi(betweenDirections > 0.5 * std::numbers::pi
                             ? std::numbers::pi - betweenDirections
                             : betweenDirections);
}

} // namespace

Result<void> checkDimension(const Document& document, const DimensionDefinition& definition) {
    if (auto valid = validate(definition); !valid) {
        return valid;
    }
    if (findView(document, definition.view) == nullptr) {
        return makeError(ErrorCode::NotFound,
                         std::format("a dimension cannot be measured in {}, which is not a view of "
                                     "this document",
                                     definition.view));
    }
    for (const DimensionTarget* target : {&definition.from, &definition.to}) {
        if (isEmpty(*target)) {
            continue;
        }
        if (target->cylinder) {
            if (auto valid = features::checkFaceName(document, *target->cylinder); !valid) {
                return valid;
            }
            continue;
        }
        for (const ObjectId object : referencedObjects(*target)) {
            if (document.findObject(object) == nullptr) {
                return makeError(ErrorCode::NotFound,
                                 std::format("a dimension cannot measure to {}, which is not an "
                                             "object of this document",
                                             object));
            }
        }
    }
    return {};
}

Result<DimensionId> createDimension(Document& document, std::string name,
                                    const DimensionDefinition& definition) {
    if (auto valid = checkDimension(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    auto dimension = Dimension::create(std::move(name), definition);
    if (!dimension) {
        return std::unexpected(dimension.error());
    }
    auto id = document.addObject(std::move(*dimension));
    if (!id) {
        return std::unexpected(id.error());
    }
    return DimensionId::fromValue(id->value());
}

Result<bool> setDimensionDefinition(Document& document, DimensionId id,
                                    const DimensionDefinition& definition) {
    if (findDimension(document, id) == nullptr) {
        return notFound(id);
    }
    if (auto valid = checkDimension(document, definition); !valid) {
        return std::unexpected(valid.error());
    }
    return document.modifyObject<Dimension>(
        ObjectId{id}, [&](Dimension& d) { return d.setDefinition(definition); });
}

const Dimension* findDimension(const Document& document, DimensionId id) noexcept {
    return document.findObjectAs<Dimension>(id);
}

std::vector<DimensionId> dimensions(const Document& document) {
    std::vector<DimensionId> found;
    for (const DocumentObject& object : document.objects()) {
        if (dynamic_cast<const Dimension*>(&object) != nullptr) {
            found.push_back(DimensionId::fromValue(object.id().value()));
        }
    }
    return found;
}

std::vector<DimensionId> dimensionsOn(const Document& document, ViewId view) {
    std::vector<DimensionId> found;
    for (const DimensionId id : dimensions(document)) {
        if (findDimension(document, id)->definition().view == view) {
            found.push_back(id);
        }
    }
    return found;
}

Result<void> removeDimension(Document& document, DimensionId id) {
    if (findDimension(document, id) == nullptr) {
        return notFound(id);
    }
    auto removed = document.removeObject(ObjectId{id});
    if (!removed) {
        return std::unexpected(removed.error());
    }
    return {};
}

Result<MeasuredDimension> measure(const Document& document, DimensionId id,
                                  const BodyLookup& bodies, const TransformLookup& transforms) {
    const Dimension* dimension = findDimension(document, id);
    if (dimension == nullptr) {
        return notFound(id);
    }
    const DimensionDefinition& d = dimension->definition();

    auto from = resolveTarget(document, d.from, bodies);
    if (!from) {
        return makeError(from.error().code,
                         std::format("{} ({}) cannot be measured: {}", dimension->name(), id,
                                     from.error().message));
    }
    std::optional<Anchor> to;
    if (!isSingleTarget(d.type)) {
        auto resolved = resolveTarget(document, d.to, bodies);
        if (!resolved) {
            return makeError(resolved.error().code,
                             std::format("{} ({}) cannot be measured: {}", dimension->name(), id,
                                         resolved.error().message));
        }
        to = *resolved;
    }

    // A view of a component measures the part where the SOLVER put it. That
    // changes nothing about a length or an angle, which a rigid motion leaves
    // alone, and everything about which view axis a distance runs along.
    auto source = effectiveSource(document, d.view);
    if (!source) {
        return std::unexpected(source.error());
    }
    if (isInternal(*source)) {
        const auto* component = document.findObjectAs<assembly::Component>(
            ComponentId::fromValue(source->object.value()));
        if (component != nullptr) {
            const RigidTransform3D* solved =
                transforms ? transforms(component->componentId()) : nullptr;
            if (solved == nullptr) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("{} measures {}, which has no solved transform; the "
                                             "assembly did not solve",
                                             id, source->object));
            }
            auto movedFrom = moved(*from, *solved);
            if (!movedFrom) {
                return std::unexpected(movedFrom.error());
            }
            from = *movedFrom;
            if (to) {
                auto movedTo = moved(*to, *solved);
                if (!movedTo) {
                    return std::unexpected(movedTo.error());
                }
                to = *movedTo;
            }
        }
    }

    MeasuredDimension measured;

    if (d.type == DimensionType::Radius || d.type == DimensionType::Diameter) {
        // Diameter is twice the radius by the ONE path, so the two can never
        // disagree about the same face.
        const double factor = d.type == DimensionType::Diameter ? 2.0 : 1.0;
        measured.length = Length::fromSi(from->radius.si() * factor);
    } else if (d.type == DimensionType::Angular) {
        auto angle = angleBetween(*from, *to);
        if (!angle) {
            return makeError(angle.error().code,
                             std::format("{} ({}) cannot be measured: {}", dimension->name(), id,
                                         angle.error().message));
        }
        measured.angle = *angle;
    } else {
        auto vector = separation(*from, *to);
        if (!vector) {
            return makeError(vector.error().code,
                             std::format("{} ({}) cannot be measured: {}", dimension->name(), id,
                                         vector.error().message));
        }
        if (d.type == DimensionType::Linear) {
            measured.length = Length::fromSi(std::hypot(vector->x, vector->y, vector->z));
        } else {
            // Everything else is read off the VIEW's own axes, so the view
            // has to say what they are. Its scale plays no part: a scale
            // changes how big the drawing is, never how big the part is.
            auto basis = effectiveBasis(document, d.view);
            if (!basis) {
                return std::unexpected(basis.error());
            }
            const double x = along(*vector, basis->xAxis());
            const double y = along(*vector, basis->yAxis());
            switch (d.type) {
            case DimensionType::Horizontal:
                measured.length = Length::fromSi(std::abs(x));
                break;
            case DimensionType::Vertical:
                measured.length = Length::fromSi(std::abs(y));
                break;
            case DimensionType::Aligned:
                measured.length = Length::fromSi(std::hypot(x, y));
                break;
            case DimensionType::Ordinate:
                // Signed: an ordinate says which side of the datum it is on,
                // and a negative one is information rather than an error.
                measured.length = Length::fromSi(d.ordinate == OrdinateAxis::X ? x : y);
                break;
            default:
                return makeError(ErrorCode::Internal,
                                 std::format("{} has a type this build does not measure", id));
            }
        }
    }

    auto text = measured.angle ? formatAngle(*measured.angle, d.format)
                               : formatLength(*measured.length, d.format);
    if (!text) {
        return makeError(text.error().code,
                         std::format("{} ({}) cannot be written: {}", dimension->name(), id,
                                     text.error().message));
    }
    measured.text = std::move(*text);
    return measured;
}

} // namespace bettercad::drawing
