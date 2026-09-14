#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <bettercad/core/geometry/Transform.hpp>

#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace bettercad::geometry {

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
        // A copy, so the result shares no geometry (or cached meshes) with the input.
        BRepBuilderAPI_Transform transform(*shape, transformation, /*copy=*/true);
        if (!transform.IsDone()) {
            return makeError(ErrorCode::Internal, "translate: the kernel cannot move the body");
        }
        Body result = occt::BodyAccess::makeBody(transform.Shape());
        if (result.isEmpty() || !result.isValid()) {
            return makeError(ErrorCode::Internal, "translate: the kernel produced an invalid shape");
        }
        return result;
    });
}

} // namespace bettercad::geometry
