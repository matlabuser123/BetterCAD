#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/features/Export.hpp>

#include <functional>
#include <string>
#include <string_view>

// Faces of features as references (P12-STREF-001).
//
// A face is named by the feature that generates it and its role there
// (FaceName): an extrude names its start and end caps and the side each
// profile entity sweeps. The feature puts the names on the faces of its body
// as it builds it, and its own booleans carry them through the kernel's
// history. A reference is resolved in that feature's body, by name only:
// when no face carries the name, the reference fails, and no other face is
// taken because it lies where the named face used to be.
namespace bettercad {
class Document;
}

namespace bettercad::features {

/// The body @p object produced in the current regeneration, or nullptr.
using BodyLookup = std::function<const geometry::Body*(ObjectId object)>;

/// Whether features of the type @p typeName name their faces (extrudes).
[[nodiscard]] BETTERCAD_FEATURES_EXPORT bool namesFaces(std::string_view typeName) noexcept;

/// "the end cap of Base (object:6)", "the side from entity:4 of Base (object:6)".
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::string describe(const Document& document, const FaceName& name);

/// Checks a face name against the document alone: the feature exists
/// (NotFound), is a feature whose kind names its faces and the selector is
/// valid (InvalidArgument), and a side's entity is a profile curve of the
/// feature's sketch (NotFound if the sketch has no such entity,
/// InvalidArgument for a point or construction geometry). A missing profile
/// sketch is left to the feature's own regeneration.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> checkFaceName(const Document& document, const FaceName& name);

/// The plane of the named face as a sketch frame (geometry::faceFrame()),
/// facing out of the material, from the faces that carry the name in
/// @p bodies(name.feature), after checkFaceName(). Fails with
/// FailedPrecondition without @p bodies or when the feature has no body,
/// NotFound when no face carries the name (the feature's own operation
/// removed the face), InvalidArgument when a named face is not planar, and
/// FailedPrecondition when the named faces do not lie on one plane facing one
/// way.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<Frame3D> resolveFacePlane(const Document& document, const FaceName& name,
                                                                        const BodyLookup& bodies);

} // namespace bettercad::features
