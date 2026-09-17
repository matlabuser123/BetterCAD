#include "core/geometry/FaceMatching.hpp"
#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"
#include "core/geometry/occt/OcctTopology.hpp"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <algorithm>

namespace bettercad::geometry {

namespace occt {

namespace {

FaceSurface surfaceOf(GeomAbs_SurfaceType type) {
    switch (type) {
    case GeomAbs_Plane:
        return FaceSurface::Plane;
    case GeomAbs_Cylinder:
        return FaceSurface::Cylinder;
    case GeomAbs_Cone:
        return FaceSurface::Cone;
    case GeomAbs_Sphere:
        return FaceSurface::Sphere;
    case GeomAbs_Torus:
        return FaceSurface::Torus;
    default:
        return FaceSurface::Other;
    }
}

FaceInfo describeFace(const TopoDS_Face& face) {
    FaceInfo info;
    const BRepAdaptor_Surface surface(face);
    info.surface = surfaceOf(surface.GetType());
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(face, properties);
    info.area = areaFromModel(properties.Mass());
    info.centroid = pointFromModel(properties.CentreOfMass());
    if (info.surface == FaceSurface::Plane) {
        const gp_Pln plane = surface.Plane();
        // The surface's own normal is XDirection x YDirection, which is the
        // frame's main direction only for a right-handed frame: a mirrored
        // plane has a left-handed one, whose normal is the opposite. A
        // reversed face then points the other way.
        gp_Dir normal = plane.Axis().Direction();
        if (!plane.Position().Direct()) {
            normal.Reverse();
        }
        if (face.Orientation() == TopAbs_REVERSED) {
            normal.Reverse();
        }
        // gp_Dir is unit length by construction.
        info.signature = planeSignature(pointFromModel(plane.Location()),
                                        *Direction3D::fromComponents(normal.X(), normal.Y(), normal.Z()));
    }
    return info;
}

} // namespace

std::optional<double> distance(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    BRepExtrema_DistShapeShape extrema(a, b);
    if (!extrema.IsDone() || extrema.NbSolution() == 0) {
        return std::nullopt;
    }
    return extrema.Value();
}

std::optional<double> distance(const gp_Pnt& point, const TopoDS_Shape& shape) {
    return distance(BRepBuilderAPI_MakeVertex(point).Vertex(), shape);
}

std::vector<KernelFace> kernelFaces(const TopoDS_Shape& shape) {
    std::vector<KernelFace> faces;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face& face = TopoDS::Face(it.Current());
        faces.push_back({face, describeFace(face)});
    }
    return faces;
}

std::vector<KernelFace> kernelFaces(const Body& body) {
    std::vector<KernelFace> faces = kernelFaces(*BodyAccess::shape(body));
    const std::vector<NamedFace>& names = BodyAccess::names(body);
    for (KernelFace& face : faces) {
        for (const NamedFace& named : names) {
            if (named.face.IsSame(face.face)) {
                face.info.names.push_back(named.name);
            }
        }
        std::ranges::sort(face.info.names);
    }
    return faces;
}

std::vector<const KernelFace*> matchingFaces(const std::vector<KernelFace>& faces, const FaceSignature& signature) {
    std::vector<const KernelFace*> matches;
    for (const KernelFace& face : faces) {
        if (face.info.signature && detail::samePlane(*face.info.signature, signature)) {
            matches.push_back(&face);
        }
    }
    return matches;
}

} // namespace occt

Result<std::vector<FaceInfo>> listFaces(const Body& body) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "an empty body has no faces");
    }
    return occt::guardKernelCall("listFaces", [&]() -> Result<std::vector<FaceInfo>> {
        std::vector<FaceInfo> result;
        for (occt::KernelFace& face : occt::kernelFaces(body)) {
            result.push_back(std::move(face.info));
        }
        return result;
    });
}

Result<std::vector<FaceInfo>> findNamedFaces(const Body& body, const FaceName& name) {
    if (occt::BodyAccess::shape(body) == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "an empty body has no faces");
    }
    return occt::guardKernelCall("findNamedFaces", [&]() -> Result<std::vector<FaceInfo>> {
        std::vector<FaceInfo> result;
        for (occt::KernelFace& face : occt::kernelFaces(body)) {
            if (std::ranges::binary_search(face.info.names, name)) {
                result.push_back(std::move(face.info));
            }
        }
        return result;
    });
}

Result<std::vector<FaceInfo>> findFaces(const Body& body, const FaceSignature& signature) {
    if (auto valid = validate(signature); !valid) {
        return std::unexpected(valid.error());
    }
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "an empty body has no faces");
    }
    return occt::guardKernelCall("findFaces", [&]() -> Result<std::vector<FaceInfo>> {
        const auto faces = occt::kernelFaces(body);
        std::vector<FaceInfo> result;
        for (const occt::KernelFace* face : occt::matchingFaces(faces, signature)) {
            result.push_back(face->info);
        }
        return result;
    });
}

} // namespace bettercad::geometry
