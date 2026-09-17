#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctFaceNames.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <bettercad/core/geometry/Transform.hpp>

#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <format>
#include <string_view>
#include <vector>

namespace bettercad::geometry {

namespace {

/// Moves a copy of @p body (so the result shares no geometry or cached
/// meshes with the input) and checks the result; the copy carries the
/// body's face names. @p operation names it in messages.
Result<Body> applyToCopy(const Body& body, const gp_Trsf& transformation, std::string_view operation) {
    BRepBuilderAPI_Transform transform(*occt::BodyAccess::shape(body), transformation, /*copy=*/true);
    if (!transform.IsDone()) {
        return makeError(ErrorCode::Internal, std::format("{}: the kernel cannot move the body", operation));
    }
    const TopoDS_Shape shape = transform.Shape();
    std::vector<occt::NamedFace> names = occt::carriedNames(transform, shape, {&body});
    Body result = occt::BodyAccess::makeBody(shape, std::move(names));
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
        return applyToCopy(body, transformation, "translate");
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
    const auto& r = motion.matrix();
    const Translation3D& t = motion.translationPart();
    for (const double value : r) {
        if (!std::isfinite(value)) {
            return makeError(ErrorCode::InvalidArgument, "transform: the motion must be finite");
        }
    }
    if (!isFinite(t)) {
        return makeError(ErrorCode::InvalidArgument, "transform: the motion must be finite");
    }
    auto moved = occt::guardKernelCall("transform", [&]() -> Result<Body> {
        // BetterCAD's matrix, not one the kernel recomputes from an axis and
        // angle: references moved with the same motion then match exactly. A
        // reflection (det -1) becomes a negative gp_Trsf, for which the copy
        // turns every face inside out, so faces keep pointing out of the
        // material.
        gp_Trsf transformation;
        transformation.SetValues(r[0], r[1], r[2], occt::toModel(t.x), r[3], r[4], r[5], occt::toModel(t.y), r[6],
                                 r[7], r[8], occt::toModel(t.z));
        return applyToCopy(body, transformation, "transform");
    });
    if (!moved || !motion.reversesOrientation()) {
        return moved;
    }
    // A mirror image must still enclose the same material: an inside-out
    // solid would have a negative volume. The kernel's volumes of a body and
    // its image agree to within 1e-15 relative (planes, cylinders, cones,
    // spheres, tori and blends; see docs/verification/P11-FEAT-007), so 1e-9
    // tells rounding from a wrong result.
    const auto before = body.massProperties();
    const auto after = moved->massProperties();
    if (!before || !after) {
        return makeError(ErrorCode::Internal, "transform: cannot measure the mirror image");
    }
    const double source = before->volume.in(units::mm3);
    const double image = after->volume.in(units::mm3);
    if (!(image > 0.0) || std::abs(image - source) > 1e-9 * std::abs(source)) {
        return makeError(ErrorCode::Internal,
                         std::format("transform: the mirror image encloses {:.12g} mm^3, not the body's {:.12g} mm^3",
                                     image, source));
    }
    return moved;
}

} // namespace bettercad::geometry
