#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Export.hpp>

#include <cstdint>
#include <string_view>

// The one geometry boundary every meshing milestone passes through
// (P16-GEOM-001).
//
// THE RULE THIS FILE EXISTS FOR:
//
//     stale geometry  !=  meshable geometry
//
// P16's invariant is that "a stale or failed model must never produce a
// nominally valid current mesh". That cannot be enforced by each backend
// checking for itself, because each would check something slightly different and
// the weakest would win. So P16-SURF-001 and P16-VOL-001 obtain geometry HERE and
// nowhere else, and neither re-implements eligibility.
//
// WHAT THIS IS NOT. It does not mesh, it does not regenerate, it does not heal,
// and it does not touch the document: every argument is const, and a caller that
// wants current geometry regenerates first. A read-only query that silently
// rebuilt the model would be a surprising mutation in exactly the place a later
// asynchronous mesher would call it from.
namespace bettercad::meshing {

/// Why geometry is not eligible for meshing.
///
/// Ordered by the sequence in which they are checked, which is also their
/// precedence: an earlier cause is reported even when a later one is also true.
/// That ordering is deliberate and is the subject of
/// `GeometryIneligibility` precedence below.
enum class GeometryIneligibility : std::uint8_t {
    /// A configuration with parameter overrides is active. The carried
    /// regeneration defect means the bodies in hand are the base
    /// configuration's, so nothing here can be trusted to describe the active
    /// one.
    ConfigurationOverrideActive,
    /// The document has no object with that id.
    ObjectNotFound,
    /// The object has never been regenerated, so there is no result at all.
    NeverRegenerated,
    /// The last pass failed to build this object.
    RegenerationFailed,
    /// The last pass did not build this object because something upstream was
    /// broken or missing.
    RegenerationBlocked,
    /// A result exists but no longer follows from the document: this object or
    /// something it is built from has changed since the result was built.
    GeometryStale,
    /// The object is up to date and produces no body -- a sketch, a parameter, a
    /// datum. Not a failure of the model, and distinguished from one.
    NoBody,
    /// The body exists and encloses nothing.
    EmptyBody,
    /// The kernel's own analyzer judges the shape invalid.
    InvalidBRep,
    /// The body contains no solid, so there is no closed volume to fill. An open
    /// shell lands here, and lands here on TOPOLOGY, before any volume is
    /// computed.
    NotASolid,
    /// The body is a closed solid that encloses no usable volume.
    ZeroVolume,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(GeometryIneligibility reason) noexcept;

/// A deterministic stamp over the geometry a body was built from
/// (P16-ARCH-001's "geometry revision foundation for mesh invalidation").
///
/// WHAT IT IS: a mix of the source object's own revision and the revisions of
/// every object it transitively depends on, plus the active configuration's
/// identity. Logical dependency revisions, which is what ADR-030's revision
/// machinery already tracks -- never a hash of BRep bytes, which would be a
/// fingerprint of the kernel's internal representation rather than of the model.
///
/// WHY NOT THE OBJECT'S OWN REVISION ALONE: editing a sketch does not change the
/// revision of the extrude that consumes it. A stamp built from the feature's own
/// revision would be IDENTICAL before and after an upstream edit that changed the
/// geometry completely, which is the one thing a geometry revision must never do.
///
/// WHY NOT Document::revision(): it moves for every change to anything,
/// including a material density or a rename. ADR-030's mesh build stamp uses it
/// as a deliberately conservative outer guard and says that "P16-GEOM-001 may
/// narrow it with measurement. It may never widen it." This is that narrowing: a
/// material edit does not change this stamp, and there is a test that says so.
///
/// The mix is computed here rather than with std::hash, because std::hash is not
/// required to agree between builds and this value is compared across presets.
struct GeometryRevision {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept { return value != 0; }

    friend constexpr bool operator==(const GeometryRevision&, const GeometryRevision&) = default;
};

/// Authoritative geometry, cleared for meshing.
///
/// The body is the regenerator's own, copied by value -- and a `Body` copy
/// "shares the underlying kernel shape", so this is a handle and not a rebuild.
/// Nothing here reconstructs, sews, heals or re-tessellates the shape, which
/// matters for two reasons beyond honesty: a reconstruction would strip the
/// kernel's locations, and it would break the subshape correspondence that
/// P16-MAP-001 needs to attribute facets to CAD faces.
///
/// It holds no meshing settings and no generated nodes or elements. Geometry
/// preparation and meshing are separate steps, and this is the output of the
/// first.
struct MeshableGeometry {
    /// The feature whose body this is.
    ObjectId source{};
    /// The authoritative body, in the document's model frame (ADR-032: a mesh is
    /// in its body's coordinate frame and carries no transform).
    geometry::Body body{};
    /// How many solids the body holds. ADR-032 gives a mesh "one region per
    /// solid", so more than one is supported and is not an error; the count is
    /// reported so that P16-VOL-001 can make the regions.
    std::size_t solidCount = 0;
    /// The volume the kernel integrates, via the same path P15-MASS-001 uses.
    /// Positive and finite, or this object would not exist.
    Volume volume{};
    /// What this geometry was built from. A later mesh records it, and a mesh
    /// whose recorded revision differs from the current one is stale.
    GeometryRevision revision{};
};

/// The geometry of @p feature, if and only if it may be meshed.
///
/// CHECK ORDER, which is also diagnostic precedence, and every step of it is
/// deliberate:
///
///   1. active configuration overrides   -- refuse before looking at anything
///   2. the object exists
///   3. regeneration state: never built, failed, blocked
///   4. currentness: is the stored result still implied by the document?
///   5. a body exists, and is not empty
///   6. the kernel's analyzer accepts it
///   7. it contains a solid                 TOPOLOGY, before volume
///   8. it encloses positive finite volume
///
/// STEPS 3 AND 4 COME BEFORE 5 TO 8 ON PURPOSE. A stale body is not to be
/// inspected: asking whether it is closed, or what it weighs, invites reporting
/// `ZeroVolume` for a body whose real problem is that it describes a model the
/// user has already changed. The honest diagnostic is the one about currency, and
/// it is only honest if nothing downstream of it has run yet.
///
/// Read-only in every argument. Nothing is regenerated, healed or activated.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<MeshableGeometry>
requireMeshableGeometry(const Document& document, const features::Regenerator& regenerator, ObjectId feature);

/// The reason @p document's @p feature is ineligible, or none if it is eligible.
///
/// The same checks in the same order as requireMeshableGeometry, for a caller
/// that wants to present or count the reason rather than a message. Provided so
/// that a UI or a report does not have to parse a diagnostic string; the message
/// stays the fallback for logs.
[[nodiscard]] BETTERCAD_MESHING_EXPORT std::optional<GeometryIneligibility>
geometryIneligibility(const Document& document, const features::Regenerator& regenerator, ObjectId feature);

/// The stamp @p feature's geometry would carry, whether or not it is eligible.
///
/// Exposed because mesh invalidation needs to ask "has the geometry moved?"
/// without first asking "may I mesh it?" -- a mesh of a body that has since
/// become ineligible is still stale, and saying so should not require the body to
/// be meshable again.
[[nodiscard]] BETTERCAD_MESHING_EXPORT GeometryRevision geometryRevision(const Document& document,
                                                                        ObjectId feature);

} // namespace bettercad::meshing
