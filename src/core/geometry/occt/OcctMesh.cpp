#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <bettercad/core/geometry/Mesh.hpp>

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <IMeshData_Status.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Poly_Triangulation.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include <cmath>
#include <format>
#include <limits>

namespace bettercad::geometry {

Result<Mesh> triangulate(const Body& body, const MeshOptions& options) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "cannot triangulate an empty body");
    }
    const double deflection = occt::toModel(options.linearDeflection);
    const double angle = options.angularDeflection.si();
    if (!std::isfinite(deflection) || deflection <= 0.0) {
        return makeError(ErrorCode::InvalidArgument, "the linear deflection must be positive");
    }
    if (!std::isfinite(angle) || angle <= 0.0) {
        return makeError(ErrorCode::InvalidArgument, "the angular deflection must be positive");
    }

    return occt::guardKernelCall("triangulation", [&]() -> Result<Mesh> {
        // Mesh a copy: the kernel stores triangulations on the faces, and the
        // body's faces are shared with every copy of the body. Meshing them in
        // place would make results depend on earlier meshing requests.
        BRepBuilderAPI_Copy copier(*shape, /*copyGeom=*/false, /*copyMesh=*/false);
        const TopoDS_Shape copy = copier.Shape();

        IMeshTools_Parameters parameters;
        parameters.Deflection = deflection;
        parameters.Angle = angle;
        parameters.Relative = false;
        parameters.InParallel = false; // deterministic
        const BRepMesh_IncrementalMesh mesher(copy, parameters);
        const int status = mesher.GetStatusFlags();
        if (!mesher.IsDone() || (status & (IMeshData_Failure | IMeshData_UserBreak)) != 0) {
            return makeError(ErrorCode::Internal,
                             std::format("triangulation: the mesher failed (status flags {:#x})", status));
        }

        Mesh mesh;
        for (TopExp_Explorer faces(copy, TopAbs_FACE); faces.More(); faces.Next()) {
            const TopoDS_Face& face = TopoDS::Face(faces.Current());
            TopLoc_Location location;
            const auto& triangulation = BRep_Tool::Triangulation(face, location);
            if (triangulation.IsNull()) {
                return makeError(ErrorCode::Internal, "triangulation: a face was not meshed");
            }
            if (mesh.vertices.size() + static_cast<std::size_t>(triangulation->NbNodes()) >
                std::numeric_limits<std::uint32_t>::max()) {
                return makeError(ErrorCode::Internal, "triangulation: too many vertices");
            }
            const auto offset = static_cast<std::uint32_t>(mesh.vertices.size());
            const gp_Trsf transform = location.Transformation();
            for (int i = 1; i <= triangulation->NbNodes(); ++i) {
                mesh.vertices.push_back(occt::pointFromModel(triangulation->Node(i).Transformed(transform)));
            }
            // Node order follows the surface parametrization; a reversed face
            // points the other way.
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            for (int i = 1; i <= triangulation->NbTriangles(); ++i) {
                int a = 0;
                int b = 0;
                int c = 0;
                triangulation->Triangle(i).Get(a, b, c);
                if (reversed) {
                    std::swap(b, c);
                }
                mesh.triangles.push_back({offset + static_cast<std::uint32_t>(a - 1),
                                          offset + static_cast<std::uint32_t>(b - 1),
                                          offset + static_cast<std::uint32_t>(c - 1)});
            }
        }
        return mesh;
    });
}

} // namespace bettercad::geometry
