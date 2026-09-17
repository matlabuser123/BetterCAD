#include "core/geometry/occt/OcctFaceNames.hpp"

#include <TopExp.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <utility>

namespace bettercad::geometry::occt {

std::vector<NamedFace> canonicalNames(const TopoDS_Shape& shape, std::vector<NamedFace> names) {
    ShapeMap faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    std::vector<std::pair<int, FaceName>> indexed;
    indexed.reserve(names.size());
    for (const NamedFace& named : names) {
        const int index = faces.FindIndex(named.face);
        if (index > 0) {
            indexed.emplace_back(index, named.name);
        }
    }
    std::ranges::sort(indexed);
    const auto [first, last] = std::ranges::unique(indexed);
    indexed.erase(first, last);
    std::vector<NamedFace> result;
    result.reserve(indexed.size());
    for (const auto& [index, name] : indexed) {
        result.push_back({TopoDS::Face(faces(index)), name});
    }
    return result;
}

std::vector<NamedFace> carriedNames(BRepBuilderAPI_MakeShape& operation, const TopoDS_Shape& result,
                                    std::initializer_list<const Body*> inputs) {
    ShapeMap resultFaces;
    TopExp::MapShapes(result, TopAbs_FACE, resultFaces);
    std::vector<NamedFace> carried;
    for (const Body* input : inputs) {
        if (input == nullptr) {
            continue;
        }
        for (const NamedFace& named : BodyAccess::names(*input)) {
            if (operation.IsDeleted(named.face)) {
                continue;
            }
            const ShapeList& images = operation.Modified(named.face);
            if (images.IsEmpty()) {
                if (resultFaces.Contains(named.face)) {
                    carried.push_back(named);
                }
                continue;
            }
            for (const TopoDS_Shape& image : images) {
                if (image.ShapeType() == TopAbs_FACE && resultFaces.Contains(image)) {
                    carried.push_back({TopoDS::Face(image), named.name});
                }
            }
        }
    }
    return canonicalNames(result, std::move(carried));
}

} // namespace bettercad::geometry::occt
