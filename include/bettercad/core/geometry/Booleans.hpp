#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>

#include <string_view>

namespace bettercad::geometry {

enum class BooleanOperation {
    Union,        ///< Material in either body.
    Difference,   ///< Material in the first body but not the second.
    Intersection, ///< Material in both bodies.
};

[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT std::string_view toString(BooleanOperation op) noexcept;

/// Boolean combination of two non-empty bodies.
///
/// The result contains only solids; coplanar faces and collinear edges created
/// by the operation are merged. The result may be empty (for example the
/// intersection of disjoint bodies). Fails with InvalidArgument for empty
/// operands and with Internal if the kernel fails or produces an invalid shape.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> booleanOperation(BooleanOperation op,
                                                                     const Body& a, const Body& b);

[[nodiscard]] inline Result<Body> booleanUnion(const Body& a, const Body& b) {
    return booleanOperation(BooleanOperation::Union, a, b);
}
/// @p a minus @p b.
[[nodiscard]] inline Result<Body> booleanDifference(const Body& a, const Body& b) {
    return booleanOperation(BooleanOperation::Difference, a, b);
}
[[nodiscard]] inline Result<Body> booleanIntersection(const Body& a, const Body& b) {
    return booleanOperation(BooleanOperation::Intersection, a, b);
}

} // namespace bettercad::geometry
