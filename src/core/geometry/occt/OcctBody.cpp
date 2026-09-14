#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>

namespace bettercad::geometry {

namespace {

// Target relative error of the adaptive volume/area integration.
constexpr double kPropertyRelativeTolerance = 1e-10;

std::size_t countSubShapes(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
    occt::ShapeMap map;
    TopExp::MapShapes(shape, type, map);
    return static_cast<std::size_t>(map.Extent());
}

std::unexpected<Error> emptyBodyError(std::string_view what) {
    return makeError(ErrorCode::FailedPrecondition, std::format("{} of an empty body is undefined", what));
}

} // namespace

Body::Body() noexcept = default;

Body::Body(std::shared_ptr<const occt::BodyData> data) noexcept : data_(std::move(data)) {}

bool Body::isEmpty() const noexcept {
    return data_ == nullptr || data_->shape.IsNull();
}

bool Body::isValid() const {
    if (isEmpty()) {
        return false;
    }
    try {
        const BRepCheck_Analyzer analyzer(data_->shape);
        return analyzer.IsValid();
    } catch (const Standard_Failure&) {
        return false;
    }
}

TopologySummary Body::topology() const {
    TopologySummary summary;
    if (isEmpty()) {
        return summary;
    }
    const TopoDS_Shape& shape = data_->shape;
    summary.solids = countSubShapes(shape, TopAbs_SOLID);
    summary.shells = countSubShapes(shape, TopAbs_SHELL);
    summary.faces = countSubShapes(shape, TopAbs_FACE);
    summary.edges = countSubShapes(shape, TopAbs_EDGE);
    summary.vertices = countSubShapes(shape, TopAbs_VERTEX);
    return summary;
}

Result<MassProperties> Body::massProperties() const {
    if (isEmpty()) {
        return emptyBodyError("mass properties");
    }
    return occt::guardKernelCall("mass properties", [&]() -> Result<MassProperties> {
        GProp_GProps volumeProps;
        const double volumeError = BRepGProp::VolumeProperties(
            data_->shape, volumeProps, kPropertyRelativeTolerance, /*OnlyClosed=*/true);
        GProp_GProps surfaceProps;
        const double areaError =
            BRepGProp::SurfaceProperties(data_->shape, surfaceProps, kPropertyRelativeTolerance);

        MassProperties properties;
        properties.volume = occt::volumeFromModel(volumeProps.Mass());
        properties.surfaceArea = occt::areaFromModel(surfaceProps.Mass());
        properties.centerOfMass = occt::pointFromModel(volumeProps.CentreOfMass());
        properties.volumeRelativeError = volumeError;
        properties.areaRelativeError = areaError;
        return properties;
    });
}

Result<BoundingBox3D> Body::boundingBox() const {
    if (isEmpty()) {
        return emptyBodyError("the bounding box");
    }
    return occt::guardKernelCall("bounding box", [&]() -> Result<BoundingBox3D> {
        Bnd_Box box;
        // Exact geometric bounds: no triangulation, no tolerance padding.
        BRepBndLib::AddOptimal(data_->shape, box, /*useTriangulation=*/false,
                               /*useShapeTolerance=*/false);
        if (box.IsVoid()) {
            return makeError(ErrorCode::Internal, "bounding box: the kernel returned a void box");
        }
        box.SetGap(0.0);
        double xmin = 0.0;
        double ymin = 0.0;
        double zmin = 0.0;
        double xmax = 0.0;
        double ymax = 0.0;
        double zmax = 0.0;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        return BoundingBox3D{occt::pointFromModel(gp_Pnt(xmin, ymin, zmin)),
                           occt::pointFromModel(gp_Pnt(xmax, ymax, zmax))};
    });
}

} // namespace bettercad::geometry
