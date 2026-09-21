#pragma once

// Private bridge between the BetterCAD geometry API and Open CASCADE. This is
// the only place where BetterCAD types and OCCT types meet.

#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/math/BoundingBox.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/RigidTransform.hpp>
#include <bettercad/core/units/Units.hpp>

#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <memory>
#include <utility>
#include <vector>

namespace bettercad::geometry::occt {

// OCCT 8 deprecates the TopTools_* container typedefs in favour of these.
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
using ShapeList = NCollection_List<TopoDS_Shape>;

/// A name on one face of a body (P12-STREF-001).
struct NamedFace {
    TopoDS_Face face;
    FaceName name;
};

struct BodyData {
    TopoDS_Shape shape;
    /// Each (face, name) once, in canonicalNames() order. Only operations
    /// that know where their faces come from fill it (see OcctFaceNames.hpp);
    /// every other operation gives a body without names.
    std::vector<NamedFace> names;
};

/// Access to Body internals for the adapter.
struct BodyAccess {
    /// Wraps a kernel shape; a null shape gives an empty body. @p names must
    /// be canonical (canonicalNames()).
    [[nodiscard]] static Body makeBody(TopoDS_Shape shape, std::vector<NamedFace> names = {}) {
        if (shape.IsNull()) {
            return Body{};
        }
        return Body{std::make_shared<const BodyData>(BodyData{std::move(shape), std::move(names)})};
    }

    /// The body's kernel shape, or nullptr for an empty body.
    [[nodiscard]] static const TopoDS_Shape* shape(const Body& body) noexcept {
        return body.isEmpty() ? nullptr : &body.data_->shape;
    }

    /// The body's face names (none for an empty body).
    [[nodiscard]] static const std::vector<NamedFace>& names(const Body& body) noexcept {
        static const std::vector<NamedFace> none;
        return body.isEmpty() ? none : body.data_->names;
    }
};

// OCCT model space uses millimetres: its default precision (1e-7) and
// tolerances are designed for millimetre-scale models, and it is the native
// unit of the exchange formats BetterCAD writes.
inline constexpr Unit<dimensions::length> kModelLength = units::mm;

[[nodiscard]] inline double toModel(const Length& length) noexcept {
    return length.in(kModelLength);
}
[[nodiscard]] inline Length lengthFromModel(double value) noexcept {
    return value * kModelLength;
}
[[nodiscard]] inline Area areaFromModel(double value) noexcept {
    return value * units::mm2;
}
[[nodiscard]] inline Volume volumeFromModel(double value) noexcept {
    return value * units::mm3;
}
[[nodiscard]] inline gp_Pnt toModel(const Point3D& point) {
    return gp_Pnt(toModel(point.x), toModel(point.y), toModel(point.z));
}
[[nodiscard]] inline Point3D pointFromModel(const gp_Pnt& point) noexcept {
    return {lengthFromModel(point.X()), lengthFromModel(point.Y()), lengthFromModel(point.Z())};
}
[[nodiscard]] inline gp_Dir toModel(const Direction3D& direction) {
    return gp_Dir(direction.x(), direction.y(), direction.z());
}

/// @p motion as a kernel transformation.
///
/// BetterCAD's own matrix is handed over element by element rather than being
/// described to the kernel as an axis and an angle for it to rebuild: a body
/// and the references moved with the same motion then agree exactly, and a
/// reflection (determinant -1) becomes a negative gp_Trsf, which the kernel
/// handles by turning faces inside out so they keep pointing out of the
/// material.
///
/// This is the ONLY conversion from a RigidTransform3D into kernel space.
/// A second one would be a second chance to get the row/column order or the
/// unit wrong, and nothing would notice until a component came out mirrored
/// or a metre from where it belonged (P13-STEP-001).
[[nodiscard]] inline gp_Trsf toModel(const RigidTransform3D& motion) {
    const std::array<double, 9>& r = motion.matrix();
    const Translation3D& t = motion.translationPart();
    gp_Trsf transformation;
    transformation.SetValues(r[0], r[1], r[2], toModel(t.x), r[3], r[4], r[5], toModel(t.y), r[6], r[7], r[8],
                             toModel(t.z));
    return transformation;
}

} // namespace bettercad::geometry::occt
