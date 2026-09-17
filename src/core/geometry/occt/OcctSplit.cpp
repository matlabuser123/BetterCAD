#include <bettercad/core/geometry/Split.hpp>

#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctFaceNames.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <BRep_Builder.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>

#include <vector>

namespace bettercad::geometry {

Result<Body> gatherSolids(std::span<const Body> parts) {
    if (parts.empty()) {
        return makeError(ErrorCode::InvalidArgument, "gather solids: there are no parts");
    }
    for (const Body& part : parts) {
        if (part.isEmpty()) {
            return makeError(ErrorCode::InvalidArgument, "gather solids: a part is empty");
        }
    }
    return occt::guardKernelCall("gather solids", [&]() -> Result<Body> {
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        std::vector<occt::NamedFace> names;
        for (const Body& part : parts) {
            for (TopExp_Explorer solid(*occt::BodyAccess::shape(part), TopAbs_SOLID); solid.More(); solid.Next()) {
                builder.Add(compound, solid.Current());
            }
            const auto& partNames = occt::BodyAccess::names(part);
            names.insert(names.end(), partNames.begin(), partNames.end());
        }
        // The solids keep their faces, so the parts' names apply as they are.
        Body result = occt::BodyAccess::makeBody(compound, occt::canonicalNames(compound, std::move(names)));
        if (!result.isValid()) {
            return makeError(ErrorCode::Internal, "gather solids: the kernel rejects the result");
        }
        return result;
    });
}

Result<std::vector<Body>> solidsOf(const Body& body) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return std::vector<Body>{};
    }
    return occt::guardKernelCall("solids of a body", [&]() -> Result<std::vector<Body>> {
        std::vector<Body> solids;
        occt::ShapeMap map;
        TopExp::MapShapes(*shape, TopAbs_SOLID, map);
        for (int i = 1; i <= map.Extent(); ++i) {
            // The solid keeps its faces, so the body's names apply to it.
            const TopoDS_Shape& solid = map(i);
            solids.push_back(occt::BodyAccess::makeBody(
                solid, occt::canonicalNames(solid, occt::BodyAccess::names(body))));
        }
        return solids;
    });
}

} // namespace bettercad::geometry
