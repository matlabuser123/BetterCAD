#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Export.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/units/Units.hpp>

#include <vector>

// Draft: tapering faces for moulding (P12-FEAT-004).
namespace bettercad::geometry {

/// A draft: the faces named `faces` turned by `angle` about their lines on
/// `neutralPlane` (its origin and normal). The plane's normal is the pull
/// direction: going along it, a positive angle takes material away (the
/// faces lean into the body) and a negative angle adds it. A face keeps its
/// line on the neutral plane, wherever that line lies.
struct DraftRequest {
    /// The faces to turn, by name (see findNamedFaces()). A name several
    /// faces carry turns all of them.
    std::vector<FaceName> faces{};
    Frame3D neutralPlane = Frame3D::xy();
    /// In (-90, 90) deg.
    Angle angle{};

    friend bool operator==(const DraftRequest&, const DraftRequest&) = default;
};

/// Checks the parts of a request that do not depend on a body. Fails with
/// InvalidArgument for no faces, a face with an invalid feature or selector,
/// a repeated face, or an angle that is not finite or not in (-90, 90) deg.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<void> validate(const DraftRequest& request);

/// @p body with the named faces drafted; @p body is not modified. Only
/// planes, cylinders (which become cones) and cones can be drafted. The
/// kernel drafts every face joined smoothly (tangentially) to a named face
/// with it, so a chain of sides and rounds turns as a whole; the chain must
/// not reach a face the draft cannot turn (e.g. one parallel to the neutral
/// plane). A zero angle leaves the faces where they are.
///
/// The kernel's result is checked, not trusted: OCCT 8.0.1 reports success
/// for a draft that shrinks a face to nothing, returning an invalid,
/// self-intersecting solid (docs/verification/P12-FEAT-004). A draft is
/// accepted only as one valid solid that does not intersect itself, has the
/// same numbers of faces, edges and vertices as @p body (a draft changes
/// no topology), keeps an image of every face, and has a finite positive
/// volume.
///
/// Errors:
/// - InvalidArgument: see validate().
/// - FailedPrecondition: the body is empty or not one solid; a named face is
///   not a plane, cylinder or cone; the kernel cannot turn a face (or a face
///   tangent to it) about the plane, or cannot build the draft, or its
///   result fails the checks above. Angles that make faces vanish end here.
/// - NotFound: a face's name is carried by no face of the body.
/// - Internal: the kernel failed unexpectedly.
///
/// Messages name faces by their position in the request, from 1.
///
/// The result carries the names of @p body's faces: each face's names go to
/// the face the kernel made of it, turned or not.
[[nodiscard]] BETTERCAD_GEOMETRY_EXPORT Result<Body> draftFaces(const Body& body, const DraftRequest& request);

} // namespace bettercad::geometry
