#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/core/units/Format.hpp>

#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <Precision.hxx>
#include <gp_Ax2.hxx>

#include <format>
#include <initializer_list>

namespace bettercad::geometry {

namespace {

// Sizes must be representable by the kernel: finite and larger than its
// confusion tolerance (in model units).
Result<void> requireSize(std::string_view what, const Length& value) {
    if (!isFinite(value) || occt::toModel(value) <= Precision::Confusion()) {
        return makeError(ErrorCode::InvalidArgument,
                         std::format("{} must be finite and larger than {} mm, got {}", what,
                                     Precision::Confusion(), toString(value, units::mm)));
    }
    return {};
}

Result<void> requireFinite(std::string_view what, const Point3D& point) {
    if (!isFinite(point.x) || !isFinite(point.y) || !isFinite(point.z)) {
        return makeError(ErrorCode::InvalidArgument, std::format("{} must be finite", what));
    }
    return {};
}

// Returns the first failure among the given checks, or success.
Result<void> firstError(std::initializer_list<Result<void>> checks) {
    for (const Result<void>& check : checks) {
        if (!check) {
            return check;
        }
    }
    return {};
}

} // namespace

Result<Body> makeBox(Length dx, Length dy, Length dz) {
    return makeBox(Point3D{}, dx, dy, dz);
}

Result<Body> makeBox(const Point3D& corner, Length dx, Length dy, Length dz) {
    if (auto valid = firstError({requireFinite("box corner", corner), requireSize("box dx", dx),
                                 requireSize("box dy", dy), requireSize("box dz", dz)});
        !valid) {
        return std::unexpected(valid.error());
    }
    return occt::guardKernelCall("makeBox", [&]() -> Result<Body> {
        BRepPrimAPI_MakeBox maker(occt::toModel(corner), occt::toModel(dx), occt::toModel(dy),
                                  occt::toModel(dz));
        return occt::BodyAccess::makeBody(maker.Solid());
    });
}

Result<Body> makeCylinder(Length radius, Length height) {
    return makeCylinder(Axis3D{}, radius, height);
}

Result<Body> makeCylinder(const Axis3D& axis, Length radius, Length height) {
    if (auto valid = firstError({requireFinite("cylinder origin", axis.origin),
                                 requireSize("cylinder radius", radius),
                                 requireSize("cylinder height", height)});
        !valid) {
        return std::unexpected(valid.error());
    }
    return occt::guardKernelCall("makeCylinder", [&]() -> Result<Body> {
        const gp_Ax2 placement(occt::toModel(axis.origin), occt::toModel(axis.direction));
        BRepPrimAPI_MakeCylinder maker(placement, occt::toModel(radius), occt::toModel(height));
        return occt::BodyAccess::makeBody(maker.Solid());
    });
}

Result<Body> makeSphere(Length radius) {
    return makeSphere(Point3D{}, radius);
}

Result<Body> makeSphere(const Point3D& center, Length radius) {
    if (auto valid =
            firstError({requireFinite("sphere centre", center), requireSize("sphere radius", radius)});
        !valid) {
        return std::unexpected(valid.error());
    }
    return occt::guardKernelCall("makeSphere", [&]() -> Result<Body> {
        BRepPrimAPI_MakeSphere maker(occt::toModel(center), occt::toModel(radius));
        return occt::BodyAccess::makeBody(maker.Solid());
    });
}

} // namespace bettercad::geometry
