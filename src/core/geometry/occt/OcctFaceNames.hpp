#pragma once

// Face names through kernel operations (P12-STREF-001). Operations that
// generate faces name them (see makePrism()); operations with a kernel
// history carry the names of their inputs' faces to the faces those became.

#include "core/geometry/occt/OcctBody.hpp"

#include <BRepBuilderAPI_MakeShape.hxx>

#include <initializer_list>
#include <vector>

namespace bettercad::geometry::occt {

/// @p names restricted to faces of @p shape, each (face, name) once, ordered
/// by the face's position in the shape's face map (TopExp::MapShapes), then
/// by name. The order depends only on the shape and the names.
[[nodiscard]] std::vector<NamedFace> canonicalNames(const TopoDS_Shape& shape, std::vector<NamedFace> names);

/// The names of @p inputs' faces carried to @p result by @p operation's
/// history: a face the operation deleted loses its names; a modified face
/// passes them to every face of the result it became (a split face to each
/// part, merged faces all to the merged face); a face the operation kept
/// keeps them. Faces the history does not account for lose their names:
/// nothing is carried by geometric similarity.
[[nodiscard]] std::vector<NamedFace> carriedNames(BRepBuilderAPI_MakeShape& operation, const TopoDS_Shape& result,
                                                  std::initializer_list<const Body*> inputs);

} // namespace bettercad::geometry::occt
