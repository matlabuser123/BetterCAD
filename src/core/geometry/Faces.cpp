#include "core/geometry/FaceMatching.hpp"

#include <cmath>
#include <format>

namespace bettercad::geometry {

namespace {

struct Vec {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec vec(const Point3D& p) {
    return {p.x.si(), p.y.si(), p.z.si()};
}

Vec vec(const Direction3D& d) {
    return {d.x(), d.y(), d.z()};
}

double dot(const Vec& a, const Vec& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec scaled(const Vec& a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}

Vec minus(const Vec& a, const Vec& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec normalized(const Vec& a) {
    return scaled(a, 1.0 / std::sqrt(dot(a, a)));
}

Point3D point(const Vec& v) {
    return {Length::fromSi(v.x), Length::fromSi(v.y), Length::fromSi(v.z)};
}

/// The face-local frame: origin and axes (see facePoint()).
struct Axes {
    Vec origin;
    Vec u;
    Vec v;
};

Axes axesOf(const FaceSignature& signature) {
    const Vec n = vec(signature.normal);
    const Vec p = vec(signature.point);
    const double ax = std::abs(n.x);
    const double ay = std::abs(n.y);
    const double az = std::abs(n.z);
    const Vec X{1, 0, 0};
    const Vec Y{0, 1, 0};
    const Vec Z{0, 0, 1};
    const Vec& a = az >= ax && az >= ay ? X : ay >= ax ? X : Y;
    const Vec& b = az >= ax && az >= ay ? Y : Z;
    // Gram-Schmidt: both model axes projected into the plane, v made
    // orthogonal to u. The chosen axes are never close to the normal.
    const Vec u = normalized(minus(a, scaled(n, dot(a, n))));
    const Vec bInPlane = minus(b, scaled(n, dot(b, n)));
    const Vec v = normalized(minus(bInPlane, scaled(u, dot(bInPlane, u))));
    return {scaled(n, dot(p, n)), u, v};
}

/// Negative zero (as in a reversed axis) as 0.
double tidy(double value) {
    return value == 0.0 ? 0.0 : value;
}

std::string formatMm(const Point3D& p) {
    return std::format("({:.6g}, {:.6g}, {:.6g}) mm", tidy(p.x.in(units::mm)), tidy(p.y.in(units::mm)),
                       tidy(p.z.in(units::mm)));
}

} // namespace

namespace detail {

bool samePlane(const FaceSignature& a, const FaceSignature& b) noexcept {
    if (a.surface != b.surface) {
        return false;
    }
    const Vec na = vec(a.normal);
    const Vec nb = vec(b.normal);
    const Vec cross{na.y * nb.z - na.z * nb.y, na.z * nb.x - na.x * nb.z, na.x * nb.y - na.y * nb.x};
    if (dot(na, nb) <= 0.0 || std::sqrt(dot(cross, cross)) > kEdgeAngularTolerance) {
        return false;
    }
    return std::abs(dot(minus(vec(a.point), vec(b.point)), nb)) <= kEdgeLengthTolerance;
}

} // namespace detail

std::string_view toString(FaceSurface surface) noexcept {
    switch (surface) {
    case FaceSurface::Plane:
        return "plane";
    case FaceSurface::Cylinder:
        return "cylinder";
    case FaceSurface::Cone:
        return "cone";
    case FaceSurface::Sphere:
        return "sphere";
    case FaceSurface::Torus:
        return "torus";
    case FaceSurface::Other:
        return "other";
    }
    return "unknown";
}

FaceSignature planeSignature(const Point3D& p, const Direction3D& outwardNormal) {
    const Vec n = vec(outwardNormal);
    const Vec nearest = scaled(n, dot(vec(p), n));
    // Without negative zeros (from a reversed axis), so equal references are
    // also written the same way.
    const Direction3D normal =
        Direction3D::fromUnitComponents(tidy(n.x), tidy(n.y), tidy(n.z)).value_or(outwardNormal);
    return {.surface = FaceSurface::Plane,
            .point = point(Vec{tidy(nearest.x), tidy(nearest.y), tidy(nearest.z)}),
            .normal = normal};
}

FaceSignature translated(const FaceSignature& signature, const Translation3D& translation) {
    FaceSignature moved = planeSignature(signature.point + translation, signature.normal);
    moved.surface = signature.surface;
    return moved;
}

FaceSignature transformed(const FaceSignature& signature, const RigidTransform3D& motion) {
    if (motion.isTranslation()) {
        return translated(signature, motion.translationPart());
    }
    FaceSignature moved = planeSignature(motion.apply(signature.point), motion.apply(signature.normal));
    moved.surface = signature.surface;
    return moved;
}

Result<void> validate(const FaceSignature& signature) {
    if (signature.surface != FaceSurface::Plane) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("only planar faces can be referenced, not a {}", toString(signature.surface)));
    }
    if (!isFinite(signature.point.x) || !isFinite(signature.point.y) || !isFinite(signature.point.z)) {
        return makeError(ErrorCode::InvalidArgument, "a face reference needs a finite point");
    }
    return {};
}

std::string describe(const FaceSignature& signature) {
    const Direction3D& n = signature.normal;
    return std::format("{} through {} facing ({:.6g}, {:.6g}, {:.6g})", toString(signature.surface),
                       formatMm(signature.point), tidy(n.x()), tidy(n.y()), tidy(n.z()));
}

Point3D facePoint(const FaceSignature& signature, const Point2D& local) {
    const Axes axes = axesOf(signature);
    const Vec along = Vec{axes.u.x * local.x.si() + axes.v.x * local.y.si(),
                          axes.u.y * local.x.si() + axes.v.y * local.y.si(),
                          axes.u.z * local.x.si() + axes.v.z * local.y.si()};
    return point(Vec{axes.origin.x + along.x, axes.origin.y + along.y, axes.origin.z + along.z});
}

Result<Frame3D> faceFrame(const FaceSignature& signature) {
    if (auto valid = validate(signature); !valid) {
        return std::unexpected(valid.error());
    }
    const Axes axes = axesOf(signature);
    const auto xAxis = Direction3D::fromComponents(axes.u.x, axes.u.y, axes.u.z);
    if (!xAxis) {
        return makeError(ErrorCode::InvalidArgument, "the face's plane has no direction in it");
    }
    return Frame3D::create(point(axes.origin), signature.normal, *xAxis);
}

Point2D faceCoordinates(const FaceSignature& signature, const Point3D& p) {
    const Axes axes = axesOf(signature);
    const Vec offset = minus(vec(p), axes.origin);
    return {Length::fromSi(dot(offset, axes.u)), Length::fromSi(dot(offset, axes.v))};
}

} // namespace bettercad::geometry
