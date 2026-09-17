#include <bettercad/core/geometry/Split.hpp>

#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctFaceNames.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <BRep_Builder.hxx>
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

} // namespace bettercad::geometry
