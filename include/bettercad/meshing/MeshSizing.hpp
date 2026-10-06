#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Export.hpp>
#include <bettercad/meshing/Mesh.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Canonical mesh sizing intent (P16-SIZE-001).
//
// THE DEPENDENCY DIRECTION THIS FILE EXISTS TO ENFORCE:
//
//     BetterCAD sizing intent        <- canonical, unit-safe, persistable
//              |
//     backend-neutral request        <- Point3D + Length, no backend types
//              |
//     Netgen adapter translation     <- maxh / Ng_RestrictMeshSizePoint
//              |
//     generated engineering mesh     <- derived
//
// and NOT the reverse: no Netgen parameter name, default or range appears in
// any type here. `maxh` is a double of unstated unit in a third-party header;
// `Length` is a dimensioned quantity. They are not the same kind of thing, and
// the point of this layer is that upgrading Netgen cannot change what a
// BetterCAD document means.
//
// WHAT IS CANONICAL HERE, AND WHAT IS DELIBERATELY NOT. Only two controls
// exist: a global target size and a set of face-local targets. Netgen offers
// more knobs (minh, grading, elementspercurve, ...) and they are NOT exposed,
// each for a stated reason -- see docs/verification/P16-SIZE-001/AUDIT.md.
// A knob is exposed when BetterCAD can state its engineering meaning, not when
// the backend happens to have one.
namespace bettercad::meshing {

/// A face-local sizing request: this face, no coarser than this.
///
/// THE REFERENCE IS A `FaceName`, which is BetterCAD's persistent face
/// identity (P12-STREF-001, ADR-024): the feature that generates the face and
/// the face's role there, with the copies made of it. It is NOT:
///
///   NodeId / ElementId      mesh-local, destroyed by every remesh (ADR-031)
///   a viewer triangle index derived display state
///   a Netgen entity tag     transient adapter data, and the OCC front end
///                           that would produce one is switched off (ADR-033)
///   an object NAME          a string a user can change, and resolving by it
///                           is the rebinding defect P15 forbids for materials
///
/// So a control survives a regeneration exactly when its face's NAME still
/// resolves, and becomes explicitly unresolved when it does not.
struct LocalMeshSizing {
    /// The face to refine.
    FaceName face{};
    /// Maximum element size in that face's region. Positive and finite.
    Length targetSize{};

    friend bool operator==(const LocalMeshSizing&, const LocalMeshSizing&) = default;
};

/// What a mesh is asked to be sized like.
///
/// VALUE SEMANTICS throughout: copyable, comparable, and holding no pointer,
/// no backend handle and no mesh-local id. That is what lets `P16-CMD-001`
/// implement undo/redo over it and `P16-PERSIST-001` serialise it without
/// either milestone having to change this type.
///
/// Equality is equality of INTENT, and because `Length` is dimensioned and SI
/// internally, 10 mm and 0.01 m are the same intent and compare equal. There
/// is no display-unit field here: how a number is shown is UI state and must
/// never change engineering semantics.
struct MeshSizingControls {
    /// Upper bound on element size across the whole body, or nullopt for
    /// BetterCAD's default (see `defaultGlobalTargetSize`).
    ///
    /// AN UPPER BOUND, NOT A PROMISE. It does not guarantee every tetrahedron
    /// edge equals it: the mesher also respects the boundary discretisation,
    /// which can make elements near a finely triangulated surface smaller than
    /// asked. Calling it a "target" rather than a "size" is deliberate.
    std::optional<Length> globalTargetSize{};
    /// Face-local refinements. Order carries no meaning; see
    /// `resolveSizing` for why the result cannot depend on it.
    std::vector<LocalMeshSizing> local{};

    friend bool operator==(const MeshSizingControls&, const MeshSizingControls&) = default;
};

/// What is wrong with a sizing request.
enum class SizingIssueKind : std::uint8_t {
    /// A target size is zero or negative.
    NonPositiveSize,
    /// A target size is NaN or infinite.
    NonFiniteSize,
    /// A local control's face selector is malformed on its own terms.
    InvalidFaceSelector,
    /// Two local controls name the same face.
    ///
    /// REFUSED rather than merged, because two sizes for one face is a
    /// modelling mistake the author should see. Overlap between DIFFERENT
    /// faces is a different question and is resolved by minimum; see
    /// `resolveSizing`.
    DuplicateFaceControl,
    /// A local control's face does not resolve against the current geometry:
    /// the feature is gone, no longer names faces, or no face carries the
    /// name any more.
    UnresolvedFace,
    /// The named face resolves but is a kind this milestone cannot turn into
    /// a region: only planar and cylindrical faces can be.
    UnsupportedFaceGeometry,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(SizingIssueKind kind) noexcept;

/// One structured complaint about a sizing request.
struct SizingIssue {
    SizingIssueKind kind = SizingIssueKind::NonPositiveSize;
    /// Which local control, by its index in `MeshSizingControls::local`, when
    /// the issue is about one. Nullopt for the global target.
    ///
    /// An index into the request the CALLER passed, which is a position in an
    /// argument rather than an identity: it is a way of pointing at the
    /// offending entry in a diagnostic, never a persistent handle.
    std::optional<std::size_t> control{};
    /// The face the issue is about, when there is one.
    std::optional<FaceName> face{};
    std::string message;

    friend bool operator==(const SizingIssue&, const SizingIssue&) = default;
};

/// What is wrong with a sizing request, in a defined order.
///
/// ORDERING IS DETERMINISTIC: issues appear in `SizingIssueKind` enumeration
/// order, and within a kind by control index ascending. Nothing is built from
/// an unordered container.
struct SizingValidationReport {
    std::vector<SizingIssue> issues;

    [[nodiscard]] bool valid() const noexcept { return issues.empty(); }

    friend bool operator==(const SizingValidationReport&, const SizingValidationReport&) = default;
};

/// Checks @p controls on their own terms, without geometry.
///
/// Catches every defect that does not need a body: non-positive and
/// non-finite sizes, malformed face selectors, and two controls naming one
/// face. Run BEFORE any geometry work and long before the backend, so an
/// invalid request fails fast and identically every time.
[[nodiscard]] BETTERCAD_MESHING_EXPORT SizingValidationReport validate(const MeshSizingControls& controls);

/// BetterCAD's global target when a document asks for none.
///
/// The bounding-box diagonal of @p bounds.
///
/// WHY A FORMULA AND NOT A CONSTANT. A constant in metres cannot serve a 1 mm
/// body and a 1 m body: one value is finer than the geometry and the other
/// coarser than it. The diagonal is scale-relative, so a model and a 10x copy
/// of it get proportional defaults, and it imposes no practical restriction --
/// an element cannot exceed the body anyway -- which makes the default
/// "the boundary discretisation governs" while still being a NUMBER BETTERCAD
/// CHOSE.
///
/// That last part is the point. Netgen's own default is `maxh = 1000`, a bare
/// double that happens to mean 1000 metres here and could change in any
/// release. Leaving it would make a BetterCAD document's meaning depend on a
/// third-party default, which the milestone gate forbids.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Length defaultGlobalTargetSize(const MeshBounds& bounds);

/// Where a local control ended up.
enum class SizingSelectionState : std::uint8_t {
    /// The face resolved and a region was produced.
    Resolved,
    /// The face no longer resolves. The control is kept and reported, never
    /// dropped and never moved to another face.
    Unresolved,
    /// The face resolved but cannot be turned into a region (see
    /// `UnsupportedFaceGeometry`).
    Unsupported,
};

[[nodiscard]] BETTERCAD_MESHING_EXPORT std::string_view toString(SizingSelectionState state) noexcept;

/// What became of one local control.
struct LocalSizingResolution {
    FaceName face{};
    Length targetSize{};
    SizingSelectionState state = SizingSelectionState::Unresolved;
    /// How many surface nodes the control claimed. Zero when not resolved,
    /// and zero is also possible for a face whose nodes none of the surface
    /// carries -- which is reported rather than treated as success.
    std::size_t nodeCount = 0;

    friend bool operator==(const LocalSizingResolution&, const LocalSizingResolution&) = default;
};

/// A single backend-neutral size restriction at a point.
///
/// Point3D and Length only: no backend type, by ADR-033. The adapter turns
/// each of these into one `Ng_RestrictMeshSizePoint` call, and a backend that
/// expressed local sizing differently would translate the same list
/// differently without anything above this line changing.
struct SizeRestriction {
    Point3D at{};
    Length maxSize{};

    friend bool operator==(const SizeRestriction&, const SizeRestriction&) = default;
};

/// A size restriction over an axis-aligned region.
///
/// A POINT RESTRICTION IS NOT ENOUGH, and this is the central empirical
/// finding of the milestone. A face's nodes lie on a surface, so restricting
/// at each of them constrains an infinitesimally thin SHEET; measured, that
/// changed the mesh without concentrating refinement at the face, because the
/// mesher samples the size function at candidate points and almost none of
/// them fall exactly on the sheet. A region has to have volume.
///
/// So a face becomes a SLAB: the bounding box of its nodes, extended into the
/// material by one target size. That is the smallest region whose refinement
/// is what "refine at this face" means, and it maps onto nglib's own
/// `Ng_RestrictMeshSizeBox`.
struct BoxSizeRestriction {
    Point3D min{};
    Point3D max{};
    Length maxSize{};

    friend bool operator==(const BoxSizeRestriction&, const BoxSizeRestriction&) = default;
};

/// Sizing intent resolved against one body and its surface.
struct ResolvedSizing {
    /// The global upper bound actually used, explicit even when the request
    /// gave none.
    Length globalTargetSize{};
    /// True when the global bound came from `defaultGlobalTargetSize` rather
    /// than from the request.
    bool globalIsDefault = false;
    /// Per-point restrictions, ascending by position then by size, so the
    /// list is identical however the controls were ordered.
    std::vector<SizeRestriction> restrictions{};
    /// Per-region restrictions, one slab per resolved face, ascending by
    /// corner then size. See `BoxSizeRestriction` for why these exist.
    std::vector<BoxSizeRestriction> regions{};
    /// What became of each local control, in the order they were given.
    std::vector<LocalSizingResolution> local{};

    /// Local controls that did not resolve.
    ///
    /// DEFINED HERE, AND IT HAS TO BE. `ResolvedSizing` is a plain data struct
    /// with no export macro -- as every report struct in this module is -- so an
    /// out-of-line definition is not exported, and a caller in another DLL gets
    /// "undefined reference to ResolvedSizing::unresolvedCount() const" at link
    /// time. It did until P16-REFMOD-001 became the first caller outside
    /// `bettercad_meshing` and the `debug-shared-ext` build refused it. Every
    /// other predicate on a report struct in this module -- `conforms()`,
    /// `dataValid()`, `complete()`, `fullyResolved()` -- is defined inline for
    /// the same reason.
    [[nodiscard]] std::size_t unresolvedCount() const noexcept {
        std::size_t count = 0;
        for (const LocalSizingResolution& entry : local) {
            if (entry.state != SizingSelectionState::Resolved) {
                ++count;
            }
        }
        return count;
    }

    friend bool operator==(const ResolvedSizing&, const ResolvedSizing&) = default;
};

/// Resolves @p controls against @p body and the nodes of @p surface.
///
/// HOW A FACE BECOMES A REGION, given what the approved pipeline allows.
/// nglib takes a surface mesh and fills it; it has no notion of a CAD face,
/// and the Netgen OCC front end that would is switched off and stays off
/// (ADR-033, and switching it on to get face-local sizing would bypass the
/// validated P16-SURF boundary). What nglib does offer is
/// `Ng_RestrictMeshSizePoint`. So a face's region is expressed as the surface
/// NODES lying on that face:
///
///   planar face       nodes on the face's plane, facing its way
///   cylindrical face  nodes at the face's radius about its axis
///
/// Both tests are exact up to floating point -- a planar face's triangulation
/// nodes lie ON its plane, they are not near it -- so the tolerance here is a
/// comparison epsilon scaled by the body size, never a modelling choice.
///
/// CONSEQUENCE, STATED PLAINLY: this refines the VOLUME near the face, not the
/// boundary triangles. The boundary is P16-SURF-001's, fixed before nglib sees
/// it, and governed by `SurfaceMeshControls`. A caller wanting a finer surface
/// asks the surface for one.
///
/// OVERLAP RESOLVES BY MINIMUM, and order-independently BY CONSTRUCTION rather
/// than by a rule someone has to honour: every backend restriction is a
/// MAXIMUM, so a node claimed by two faces takes the smaller of the two, and
/// the smaller of two numbers does not depend on which arrived first. Two
/// controls on the SAME face are refused by `validate` before this runs.
///
/// Fails with InvalidArgument when `validate` would, so a caller that skips
/// validation still cannot reach the backend with an invalid request. An
/// unresolved FACE is not a failure of this function: it is reported in
/// `local`, and whether to proceed is the caller's decision.
[[nodiscard]] BETTERCAD_MESHING_EXPORT Result<ResolvedSizing>
resolveSizing(const geometry::Body& body, const Mesh& surface, const MeshSizingControls& controls);

} // namespace bettercad::meshing
