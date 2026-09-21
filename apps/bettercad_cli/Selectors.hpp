#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/MateReference.hpp>

#include <string>
#include <string_view>

// Naming things from a command line (P13-CLI-001, implementing ADR-009).
//
// A script has to say which component it means, and the obvious answer --
// by name -- is the one this project rejected in ADR-003: a locator is never
// identity, and P13-REF-001 has a qualified test to that effect.
//
// The document settles it more strongly than "names are not identity". A name
// is an identifier, [A-Za-z_][A-Za-z0-9_]*, unique across objects AND
// parameters, re-checked on every rename. So:
//
//     7      is all digits, and NO name can be         -> an ID
//     Base   starts with a letter, and no ID can       -> a name
//
// The two sets cannot overlap, and that is a consequence of
// validateIdentifier() rather than a convention this parser invents. A name
// is resolved to its ID at the moment of use and never stored, so identity
// stays with the ID exactly as ADR-003 requires.
namespace bettercad {
class Document;
}

namespace bettercad::cli {

/// The object @p selector names: an object ID if it is all digits, otherwise
/// the object of that name.
///
/// NotFound if nothing of that ID or name is in @p document, and
/// InvalidArgument if the selector is neither form (which is the only way to
/// get here with something that could have been either).
[[nodiscard]] Result<ObjectId> resolveObject(const Document& document, std::string_view selector);

/// The same, narrowed to a component. FailedPrecondition, naming what the
/// object actually is, when the selector resolves to something else -- so
/// "that is a mate" is never reported as "no such component".
[[nodiscard]] Result<ComponentId> resolveComponent(const Document& document, std::string_view selector);

/// The same, narrowed to a mate.
[[nodiscard]] Result<MateId> resolveMate(const Document& document, std::string_view selector);

/// The configuration @p name names. Configuration names are not object names
/// and share none of their space, so this is a name lookup and nothing else.
[[nodiscard]] Result<ConfigurationId> resolveConfiguration(const Document& document, std::string_view name);

/// One side of a mate, written as component and geometry:
///
/// ```text
/// <component>:origin:<xy|yz|xz|x|y|z>   a principal plane or axis of the part
/// <component>:datum:<selector>          a datum plane or datum axis object
/// <component>:csys:<selector>:<xy|..|z> a plane or axis of a coordinate system
/// <component>:face:<selector>:<role>[:<entity>]
/// ```
///
/// `role` is spelled as toString(FaceRole) spells it: `start_cap`, `end_cap`,
/// `side`, `hole_bottom`, `counterbore_floor`, `chamfer`, `spotface_floor`.
/// `entity` is the profile entity of a side face, as an entity ID.
///
/// Copied faces (FaceSelector::copies) cannot be named this way; that is a
/// recorded limitation of the interface, not of the model.
///
/// The geometry is NOT resolved here -- whether it exists and belongs to the
/// component's part is checked by assembly::checkMate(), which is the one
/// place that knows how. This builds the target and validates its shape.
[[nodiscard]] Result<MateTarget> parseMateTarget(const Document& document, std::string_view text);

/// @p target written in the grammar parseMateTarget() accepts, so that what
/// a report prints can be pasted back into a command unchanged.
///
/// Components and referenced objects are named where they have names, and by
/// ID where they do not, which keeps the text usable as a selector either
/// way. A target this interface cannot express -- a copied face -- is
/// rendered with a trailing `:...` marker rather than as something that would
/// parse back to a different target.
[[nodiscard]] std::string formatMateTarget(const Document& document, const MateTarget& target);

/// "Base (object:7)", for diagnostics that should name a thing the way the
/// engineer wrote it and the way the document knows it.
[[nodiscard]] std::string label(const Document& document, ObjectId id);

} // namespace bettercad::cli
