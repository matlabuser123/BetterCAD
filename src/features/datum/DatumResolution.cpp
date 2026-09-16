#include "features/SolidSupport.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/features/Datums.hpp>

#include <cmath>
#include <format>
#include <numbers>
#include <string>

namespace bettercad::features {

namespace {

// References deeper than this are a cycle (the dependency graph reports
// cycles; resolution may still be asked, e.g. by validation).
constexpr int kMaxDepth = 64;
// An axis lies in a plane when its direction is within this of the plane
// (the sine of the angle) and its point within kLengthTolerance of it: the
// tolerances of face matching and revolution axes.
constexpr double kAngularTolerance = 1e-9;
constexpr double kLengthTolerance = 1e-10; // metres

std::string label(const Document& document, ObjectId id) {
    const auto name = document.nameOf(id);
    return name ? std::format("{} ({})", *name, id) : std::format("{}", id);
}

std::string article(std::string_view word) {
    return std::format("{} {}", std::string_view{"aeiou"}.find(word.front()) == std::string_view::npos ? "a" : "an",
                       word);
}

std::string kindName(std::string_view typeName) {
    std::string text{typeName};
    std::ranges::replace(text, '_', ' ');
    return text;
}

Result<const DocumentObject*> requireObject(const Document& document, ObjectId id) {
    const DocumentObject* object = document.findObject(id);
    if (object == nullptr) {
        return makeError(ErrorCode::NotFound, std::format("{} does not exist", id));
    }
    return object;
}

std::unexpected<Error> wrongKind(const Document& document, const DocumentObject& object, std::string_view expected) {
    return makeError(ErrorCode::InvalidArgument, std::format("{} is {}, not {}", label(document, object.id()),
                                                             article(kindName(object.typeName())), expected));
}

std::unexpected<Error> tooDeep(const Document& document, ObjectId id) {
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("{}: its references nest more than {} deep (a dependency cycle?)",
                                 label(document, id), kMaxDepth));
}

/// Prefixes an error with the object it came from.
std::unexpected<Error> within(const Document& document, ObjectId id, const Error& error) {
    return makeError(error.code, std::format("{}: {}", label(document, id), error.message));
}

template <QuantityType Q>
Result<Q> valueOf(const Document& document, Q literal, const std::optional<ParameterId>& parameter,
                  std::string_view role) {
    if (!parameter) {
        return literal;
    }
    return detail::drivingValue<Q>(document, *parameter, role);
}

Result<Frame3D> frameOf(const Point3D& origin, const Direction3D& normal, const Direction3D& xAxis) {
    return Frame3D::create(origin, normal, xAxis);
}

/// The principal plane @p plane of the coordinate system @p f.
Result<Frame3D> principalPlane(const Frame3D& f, PrincipalPlane plane) {
    switch (plane) {
    case PrincipalPlane::XY:
        return f;
    case PrincipalPlane::YZ:
        return frameOf(f.origin(), f.xAxis(), f.yAxis());
    case PrincipalPlane::XZ:
        return frameOf(f.origin(), f.yAxis().reversed(), f.xAxis());
    }
    return makeError(ErrorCode::Internal, "unknown principal plane");
}

Axis3D principalAxis(const Frame3D& f, PrincipalAxis axis) {
    switch (axis) {
    case PrincipalAxis::X:
        return {f.origin(), f.xAxis()};
    case PrincipalAxis::Y:
        return {f.origin(), f.yAxis()};
    case PrincipalAxis::Z:
        break;
    }
    return {f.origin(), f.normal()};
}

Result<Frame3D> coordinateSystem(const Document& document, std::optional<ObjectId> object, int depth);
Result<Frame3D> plane(const Document& document, const PlaneReference& reference, int depth);
Result<Axis3D> axis(const Document& document, const AxisReference& reference, int depth);

Result<Frame3D> coordinateSystem(const Document& document, std::optional<ObjectId> object, int depth) {
    if (!object) {
        return Frame3D::xy();
    }
    if (depth > kMaxDepth) {
        return tooDeep(document, *object);
    }
    auto found = requireObject(document, *object);
    if (!found) {
        return std::unexpected(found.error());
    }
    const auto* system = dynamic_cast<const CoordinateSystem*>(*found);
    if (system == nullptr) {
        return wrongKind(document, **found, "a coordinate system");
    }
    const CoordinateSystemDefinition& d = system->definition();
    if (d.kind == CoordinateSystemKind::Fixed) {
        return d.frame;
    }
    auto base = coordinateSystem(document, d.base, depth + 1);
    if (!base) {
        return std::unexpected(base.error());
    }
    static constexpr std::array<std::string_view, 3> kAxes{"X", "Y", "Z"};
    std::array<Length, 3> translation{};
    std::array<Angle, 3> rotation{};
    for (std::size_t i = 0; i < 3; ++i) {
        auto t = valueOf(document, d.translation[i], d.translationParameters[i],
                         std::format("{} translation parameter", kAxes[i]));
        auto r = valueOf(document, d.rotation[i], d.rotationParameters[i],
                         std::format("{} rotation parameter", kAxes[i]));
        if (!t || !r) {
            return within(document, *object, !t ? t.error() : r.error());
        }
        if (!isFinite(*t) || !isFinite(*r)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{}: its translation and rotation must be finite", label(document, *object)));
        }
        translation[i] = *t;
        rotation[i] = *r;
    }
    // About the base's fixed X, then Y, then Z axis, all through its origin.
    const Point3D& o = base->origin();
    Direction3D x = base->xAxis();
    Direction3D z = base->normal();
    const std::array<Direction3D, 3> baseAxes{base->xAxis(), base->yAxis(), base->normal()};
    for (std::size_t i = 0; i < 3; ++i) {
        if (rotation[i].si() == 0.0) {
            continue;
        }
        const RigidTransform3D turn = RigidTransform3D::rotation(Axis3D{o, baseAxes[i]}, rotation[i]);
        x = turn.apply(x);
        z = turn.apply(z);
    }
    const Point3D origin = o + Translation3D::along(baseAxes[0], translation[0]) +
                           Translation3D::along(baseAxes[1], translation[1]) +
                           Translation3D::along(baseAxes[2], translation[2]);
    auto frame = frameOf(origin, z, x);
    if (!frame) {
        return within(document, *object, frame.error());
    }
    return frame;
}

Result<Frame3D> plane(const Document& document, const PlaneReference& reference, int depth) {
    if (!reference.object) {
        switch (reference.plane) {
        case PrincipalPlane::XY:
            return Frame3D::xy();
        case PrincipalPlane::YZ:
            return Frame3D::yz();
        case PrincipalPlane::XZ:
            return Frame3D::xz();
        }
        return makeError(ErrorCode::Internal, "unknown principal plane");
    }
    const ObjectId id = *reference.object;
    if (depth > kMaxDepth) {
        return tooDeep(document, id);
    }
    auto found = requireObject(document, id);
    if (!found) {
        return std::unexpected(found.error());
    }
    if (dynamic_cast<const CoordinateSystem*>(*found) != nullptr) {
        auto frame = coordinateSystem(document, id, depth);
        if (!frame) {
            return std::unexpected(frame.error());
        }
        return principalPlane(*frame, reference.plane);
    }
    const auto* datum = dynamic_cast<const DatumPlane*>(*found);
    if (datum == nullptr) {
        return wrongKind(document, **found, "a datum plane or a coordinate system");
    }
    if (reference.plane != PrincipalPlane::XY) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is a datum plane, which has only one plane: refer to it as xy, not {}",
                                     label(document, id), toString(reference.plane)));
    }
    const DatumPlaneDefinition& d = datum->definition();
    if (d.kind == DatumPlaneKind::Fixed) {
        return d.frame;
    }
    auto base = plane(document, d.base, depth + 1);
    if (!base) {
        return std::unexpected(base.error());
    }
    if (d.kind == DatumPlaneKind::Offset) {
        auto offset = valueOf(document, d.offset, d.offsetParameter, "offset parameter");
        if (!offset) {
            return within(document, id, offset.error());
        }
        if (!isFinite(*offset)) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("{}: its offset must be finite", label(document, id)));
        }
        const Point3D origin = base->origin() + Translation3D::along(base->normal(), *offset);
        auto frame = Frame3D::fromAxes(origin, base->xAxis(), base->yAxis(), base->normal());
        if (!frame) {
            return within(document, id, frame.error());
        }
        return frame;
    }
    auto hinge = axis(document, d.axis, depth + 1);
    if (!hinge) {
        return std::unexpected(hinge.error());
    }
    auto angle = valueOf(document, d.angle, d.angleParameter, "angle parameter");
    if (!angle) {
        return within(document, id, angle.error());
    }
    if (!isFinite(*angle)) {
        return makeError(ErrorCode::InvalidArgument, std::format("{}: its angle must be finite", label(document, id)));
    }
    const double tilt = std::abs(hinge->direction.dot(base->normal()));
    const Length away = abs(base->signedDistance(hinge->origin));
    if (tilt > kAngularTolerance || away.si() > kLengthTolerance) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: its axis does not lie in its base plane (it is {:.6g} deg and {:.6g} mm "
                                     "off it)",
                                     label(document, id), std::asin(std::min(1.0, tilt)) * 180.0 / std::numbers::pi,
                                     away.in(units::mm)));
    }
    const RigidTransform3D turn = RigidTransform3D::rotation(*hinge, *angle);
    auto frame = frameOf(turn.apply(base->origin()), turn.apply(base->normal()), turn.apply(base->xAxis()));
    if (!frame) {
        return within(document, id, frame.error());
    }
    return frame;
}

Result<Axis3D> axis(const Document& document, const AxisReference& reference, int depth) {
    if (!reference.object) {
        return principalAxis(Frame3D::xy(), reference.axis);
    }
    const ObjectId id = *reference.object;
    if (depth > kMaxDepth) {
        return tooDeep(document, id);
    }
    auto found = requireObject(document, id);
    if (!found) {
        return std::unexpected(found.error());
    }
    if (dynamic_cast<const CoordinateSystem*>(*found) != nullptr) {
        auto frame = coordinateSystem(document, id, depth);
        if (!frame) {
            return std::unexpected(frame.error());
        }
        return principalAxis(*frame, reference.axis);
    }
    const auto* datum = dynamic_cast<const DatumAxis*>(*found);
    if (datum == nullptr) {
        return wrongKind(document, **found, "a datum axis or a coordinate system");
    }
    if (reference.axis != PrincipalAxis::Z) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} is a datum axis, which is one axis: refer to it as z, not {}",
                                     label(document, id), toString(reference.axis)));
    }
    const DatumAxisDefinition& d = datum->definition();
    if (d.kind == DatumAxisKind::Fixed) {
        return d.axis;
    }
    auto first = plane(document, d.first, depth + 1);
    if (!first) {
        return std::unexpected(first.error());
    }
    auto second = plane(document, d.second, depth + 1);
    if (!second) {
        return std::unexpected(second.error());
    }
    const Direction3D& n1 = first->normal();
    const Direction3D& n2 = second->normal();
    const auto direction = n1.cross(n2);
    const double c = n1.dot(n2);
    if (!direction || 1.0 - c * c <= kAngularTolerance * kAngularTolerance) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("{}: its planes are parallel and do not meet", label(document, id)));
    }
    // The point p = a n1 + b n2 on both planes (n1.p = h1, n2.p = h2): the
    // point of the line nearest the model's origin.
    const auto height = [](const Frame3D& f) {
        const Point3D& o = f.origin();
        const Direction3D& n = f.normal();
        return n.x() * o.x.si() + n.y() * o.y.si() + n.z() * o.z.si();
    };
    const double h1 = height(*first);
    const double h2 = height(*second);
    const double det = 1.0 - c * c;
    const double a = (h1 - h2 * c) / det;
    const double b = (h2 - h1 * c) / det;
    const Point3D point{Length::fromSi(a * n1.x() + b * n2.x()), Length::fromSi(a * n1.y() + b * n2.y()),
                        Length::fromSi(a * n1.z() + b * n2.z())};
    return Axis3D{point, *direction};
}

} // namespace

Result<Frame3D> resolvePlane(const Document& document, const PlaneReference& reference) {
    return plane(document, reference, 0);
}

Result<Axis3D> resolveAxis(const Document& document, const AxisReference& reference) {
    return axis(document, reference, 0);
}

Result<Frame3D> resolveCoordinateSystem(const Document& document, std::optional<ObjectId> object) {
    return coordinateSystem(document, object, 0);
}

} // namespace bettercad::features
