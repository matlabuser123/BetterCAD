#include <bettercad/core/geometry/Booleans.hpp>

#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctFaceNames.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRep_Builder.hxx>
#include <TopExp.hxx>
#include <TopoDS_Compound.hxx>

#include <format>
#include <memory>
#include <sstream>
#include <string>

namespace bettercad::geometry {

namespace {

std::unique_ptr<BRepAlgoAPI_BooleanOperation> makeAlgorithm(BooleanOperation op) {
    switch (op) {
    case BooleanOperation::Union:
        return std::make_unique<BRepAlgoAPI_Fuse>();
    case BooleanOperation::Difference:
        return std::make_unique<BRepAlgoAPI_Cut>();
    case BooleanOperation::Intersection:
        return std::make_unique<BRepAlgoAPI_Common>();
    }
    return nullptr;
}

// Keeps only the solids of a boolean result: none gives an empty shape, one
// is returned directly, several are grouped in a compound.
TopoDS_Shape collectSolids(const TopoDS_Shape& shape) {
    occt::ShapeMap solids;
    TopExp::MapShapes(shape, TopAbs_SOLID, solids);
    if (solids.Extent() == 0) {
        return {};
    }
    if (solids.Extent() == 1) {
        return solids(1);
    }
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (int i = 1; i <= solids.Extent(); ++i) {
        builder.Add(compound, solids(i));
    }
    return compound;
}

} // namespace

std::string_view toString(BooleanOperation op) noexcept {
    switch (op) {
    case BooleanOperation::Union:
        return "union";
    case BooleanOperation::Difference:
        return "difference";
    case BooleanOperation::Intersection:
        return "intersection";
    }
    return "unknown";
}

Result<Body> booleanOperation(BooleanOperation op, const Body& a, const Body& b) {
    const TopoDS_Shape* shapeA = occt::BodyAccess::shape(a);
    const TopoDS_Shape* shapeB = occt::BodyAccess::shape(b);
    const std::string name = std::format("boolean {}", toString(op));
    if (shapeA == nullptr || shapeB == nullptr) {
        return makeError(ErrorCode::InvalidArgument, std::format("{}: operands must not be empty", name));
    }

    return occt::guardKernelCall(name, [&]() -> Result<Body> {
        auto algorithm = makeAlgorithm(op);
        occt::ShapeList arguments;
        arguments.Append(*shapeA);
        occt::ShapeList tools;
        tools.Append(*shapeB);
        algorithm->SetArguments(arguments);
        algorithm->SetTools(tools);
        // Single-threaded: identical inputs always take the same code path.
        algorithm->SetRunParallel(false);
        algorithm->Build();
        if (algorithm->HasErrors() || !algorithm->IsDone()) {
            std::ostringstream details;
            algorithm->DumpErrors(details);
            return makeError(ErrorCode::Internal,
                             std::format("{} failed: {}", name, details.str()));
        }
        // Merge coplanar faces and collinear edges split by the operation.
        // The kernel's history includes the merging.
        algorithm->SimplifyResult();

        TopoDS_Shape solids = collectSolids(algorithm->Shape());
        std::vector<occt::NamedFace> names = occt::carriedNames(*algorithm, solids, {&a, &b});
        Body result = occt::BodyAccess::makeBody(std::move(solids), std::move(names));
        if (!result.isEmpty() && !result.isValid()) {
            return makeError(ErrorCode::Internal, std::format("{} produced an invalid shape", name));
        }
        return result;
    });
}

} // namespace bettercad::geometry
