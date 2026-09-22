#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/features/Export.hpp>

#include <functional>
#include <string>
#include <string_view>

// Faces of features as references (P12-STREF-001, P12-SKETCH-003).
//
// A face is named by the feature that generates it and its role there
// (FaceName): extrudes, revolves and sweeps name their start and end caps and
// the side each profile entity sweeps (a sweep's also by its path edge),
// lofts their caps, holes their bottom and counterbore floor, chamfers the
// face each edge reference cuts, and ribs their two walls (as caps) and the
// side each profile edge makes (P12-FEAT-005). The feature puts the names on
// the faces of its body as it builds it, and its own booleans carry them
// through the kernel's history. A pattern or mirror copies faces: each copy's name is the
// original's with the copy step appended (FaceCopy), and it is found in the
// body of the last feature that copied it. A reference is resolved in that
// body, by name only: when no face carries the name, the reference fails,
// and no other face is taken because it lies where the named face used to
// be.
namespace bettercad {
class Document;
}

namespace bettercad::features {

/// The body @p object produced in the current regeneration, or nullptr.
using BodyLookup = std::function<const geometry::Body*(ObjectId object)>;

/// Whether features of the type @p typeName name the faces they generate
/// (extrudes, revolves, sweeps, lofts, holes, chamfers and ribs).
[[nodiscard]] BETTERCAD_FEATURES_EXPORT bool namesFaces(std::string_view typeName) noexcept;

/// "the end cap of Base (object:6)", "the side from entity:4 of Base (object:6)",
/// "the bottom of Bore (object:9), copy 2 of Row (object:10)".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string describe(const Document& document, const FaceName& name);

/// The feature in whose body the named face is found: the last copy's
/// pattern or mirror, or the generating feature.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT ObjectId holderOf(const FaceName& name) noexcept;

/// Checks a face name against the document alone: the feature exists
/// (NotFound), is a feature whose kind names its faces, generates the role
/// (a sweep's side names its path edge, a hole bottom needs a blind hole, a
/// counterbore floor a counterbored one) and the selector is valid
/// (InvalidArgument); a side's entity is a profile curve of the feature's
/// sketch (NotFound if the sketch has no such entity, InvalidArgument for a
/// point or construction geometry), a sweep's path edge is an edge of its
/// path, a chamfer's edge reference one of its references and a rib's side
/// entity one of its profile edges (NotFound); and
/// every copy names an existing (NotFound) pattern or mirror, a mirror only
/// with instance 1 (InvalidArgument). A missing profile sketch is left to the
/// feature's own regeneration.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> checkFaceName(const Document& document, const FaceName& name);

/// The plane of the named face as a sketch frame (geometry::faceFrame()),
/// facing out of the material, from the faces that carry the name in
/// @p bodies(holderOf(name)), after checkFaceName(). Fails with
/// FailedPrecondition without @p bodies or when the feature has no body,
/// NotFound when no face carries the name (the feature's own operation
/// removed the face), InvalidArgument when a named face is not planar, and
/// FailedPrecondition when the named faces do not lie on one plane facing one
/// way.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Frame3D> resolveFacePlane(const Document& document, const FaceName& name,
                                                                        const BodyLookup& bodies);

/// The axis and radius of the named CYLINDRICAL face, exactly as the kernel
/// holds them, after checkFaceName(). The same failures as resolveFacePlane,
/// with InvalidArgument when the named face is not a cylinder and
/// FailedPrecondition when the named faces are parts of different cylinders
/// -- a radius is one number, and two would have to be chosen between.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<geometry::CylindricalFace> resolveFaceCylinder(
    const Document& document, const FaceName& name, const BodyLookup& bodies);

} // namespace bettercad::features
