#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <bettercad/core/geometry/Transform.hpp>

#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <format>
#include <string_view>

namespace bettercad::geometry {

namespace {

/// Moves a copy of @p shape (so the result shares no geometry or cached
/// meshes with the input) and checks the result. @p operation names it in
/// messages.
Result<Body> applyToCopy(const TopoDS_Shape& shape, const gp_Trsf& transformation, std::string_view operation) {
    BRepBuilderAPI_Transform transform(shape, transformation, /*copy=*/true);
    if (!transform.IsDone()) {
        return makeError(ErrorCode::Internal, std::format("{}: the kernel cannot move the body", operation));
    }
    Body result = occt::BodyAccess::makeBody(transform.Shape());
    if (result.isEmpty() || !result.isValid()) {
        return makeError(ErrorCode::Internal, std::format("{}: the kernel produced an invalid shape", operation));
    }
    return result;
}

} // namespace

Result<Body> translated(const Body& body, const Translation3D& translation) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "translate: the body is empty");
    }
    if (!isFinite(translation)) {
        return makeError(ErrorCode::InvalidArgument, "translate: the translation must be finite");
    }
    return occt::guardKernelCall("translate", [&]() -> Result<Body> {
        gp_Trsf transformation;
        transformation.SetTranslation(
            gp_Vec(occt::toModel(translation.x), occt::toModel(translation.y), occt::toModel(translation.z)));
        return applyToCopy(*shape, transformation, "translate");
    });
}

Result<Body> transformed(const Body& body, const RigidTransform3D& motion) {
    if (motion.isTranslation()) {
        return translated(body, motion.translationPart());
    }
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "transform: the body is empty");
    }
    const auto& r = motion.rotationMatrix();
    const Translation3D& t = motion.translationPart();
    for (const double value : r) {
        if (!std::isfinite(value)) {
            return makeError(ErrorCode::InvalidArgument, "transform: the motion must be finite");
        }
    }
    if (!isFinite(t)) {
        return makeError(ErrorCode::InvalidArgument, "transform: the motion must be finite");
    }
    return occt::guardKernelCall("transform", [&]() -> Result<Body> {
        // BetterCAD's matrix, not one the kernel recomputes from an axis and
        // angle: references moved with the same motion then match exactly.
        gp_Trsf transformation;
        transformation.SetValues(r[0], r[1], r[2], occt::toModel(t.x), r[3], r[4], r[5], occt::toModel(t.y), r[6],
                                 r[7], r[8], occt::toModel(t.z));
        return applyToCopy(*shape, transformation, "transform");
    });
}

} // namespace bettercad::geometry
