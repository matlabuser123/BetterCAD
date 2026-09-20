#pragma once

#include <bettercad/assembly/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/core/document/ObjectReference.hpp>
#include <bettercad/core/document/ReferenceResolver.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/features/Datums.hpp>

#include <optional>
#include <string_view>
#include <vector>

// Resolving a component's part, and reporting the ones that do not resolve
// (P13-REF-001).
//
// core resolves a reference to an object; this resolves it to a PART, which
// means also deciding whether the object it found is a kind that can be
// placed. That check needs features::SolidFeature, so it lives here rather
// than in core -- the split ADR-006 draws between the reference vocabulary
// and the resolution that knows what the kinds mean.
namespace bettercad {
class Document;
}

namespace bettercad::features {
class Regenerator;
class SolidFeature;
} // namespace bettercad::features

namespace bettercad::assembly {

/// The part @p reference names, resolved through @p resolver and checked for
/// a kind that produces a body.
///
/// Fails with NotFound when the reference does not resolve -- the message
/// names which of the four states it ended in -- and with InvalidArgument
/// when it resolves to something that is not a part. An internal reference
/// never consults the resolver.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<const features::SolidFeature*>
resolvePart(const Document& document, const ObjectReference& reference, const ReferenceResolver* resolver = nullptr);

/// The part the component with @p id places. NotFound if there is no such
/// component.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<const features::SolidFeature*>
resolveComponentPart(const Document& document, ComponentId id, const ReferenceResolver* resolver = nullptr);

/// A component whose part does not resolve, and why.
struct UnresolvedComponent {
    ComponentId component{};
    ReferenceState state = ReferenceState::DocumentUnavailable;

    friend bool operator==(const UnresolvedComponent&, const UnresolvedComponent&) = default;
};

/// Every component of @p document whose part does not resolve, in ascending
/// ID order.
///
/// This is how a document reports its unresolved references rather than
/// refusing to open: load it, ask, and show the answer. A component with an
/// internal part appears here only if that object is missing.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::vector<UnresolvedComponent>
unresolvedComponents(const Document& document, const ReferenceResolver* resolver = nullptr);

// --- Mate targets (P13-STREF-001) --------------------------------------------------------------

/// The model-space geometry a mate target names, in its own part's space.
///
/// A plane and a face both give a plane -- a face through the plane it lies
/// in -- and an axis gives a line. `planar` says which, because the
/// constraints mean different things by the two.
struct MateTargetGeometry {
    bool planar = false;
    Point3D origin{};
    Direction3D direction = Direction3D::unitZ();

    friend bool operator==(const MateTargetGeometry&, const MateTargetGeometry&) = default;
};

/// The geometry @p target names.
///
/// NotFound when the target's component is not in this document, and
/// whatever resolvePlane()/resolveAxis() report when the geometry itself
/// does not resolve -- a missing object, an object of the wrong kind, or a
/// face whose role its feature no longer produces.
///
/// **This is the one place a mate target is resolved.** The solver calls it
/// to build its problem and unresolvedMateTargets() calls it to report, so
/// the question "does this target resolve" is answered by the same code that
/// answers "to what" -- there is no second path that could disagree with it.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<MateTargetGeometry>
resolveMateTarget(const Document& document, const MateTarget& target,
                  const features::BodyLookup& bodies = {});

/// Which of a mate's targets this is. A slider carries a roll reference as
/// well as its axis (P13-MATE-002), so a mate has up to four.
enum class MateTargetSide {
    A,
    B,
    RollA,
    RollB,
};

/// "a", "b", "a2", "b2".
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::string_view toString(MateTargetSide side) noexcept;

/// A mate target that does not resolve, and why.
struct UnresolvedMateTarget {
    MateId mate{};
    MateTargetSide side = MateTargetSide::A;
    ErrorCode reason = ErrorCode::NotFound;

    friend bool operator==(const UnresolvedMateTarget&, const UnresolvedMateTarget&) = default;
};

/// Every mate target of @p document that does not resolve, in ascending mate
/// ID order and then in target order.
///
/// The mate counterpart of unresolvedComponents(), and it exists for the same
/// reason: a document reports what is broken rather than refusing to open, so
/// a caller can load it, ask, and show the answer. Before this, the only way
/// to discover an unresolved mate target was to attempt a solve and have the
/// whole thing fail.
///
/// **Suppressed and inactive mates are skipped**, because a mate that is not
/// in this build has nothing to say about whether the model is broken. That
/// distinction is P13-CONF-001's: a target on a suppressed component is
/// inactive, not unresolved.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::vector<UnresolvedMateTarget>
unresolvedMateTargets(const Document& document, const features::BodyLookup& bodies = {});

// --- Regeneration (P13-REGEN-001) --------------------------------------------------------------

/// Why an assembly re-solved, or did not, in a regeneration pass.
///
/// The trigger is derived from what the solve actually reads rather than from
/// a revision proxy, which is what makes "it did not re-solve for an
/// unrelated change" provable instead of hoped for (ADR-008).
enum class SolveTrigger {
    /// Nothing the solve consumes changed, so the previous transforms stand.
    NotNeeded,
    /// No solve has run for this document yet.
    First,
    /// A component or mate was rebuilt, failed or blocked in this pass.
    ObjectChanged,
    /// The set of components in force changed.
    ComponentsInForceChanged,
    /// The set of mates in force changed.
    MatesInForceChanged,
    /// A different configuration is active.
    ConfigurationChanged,
    /// A component's resolved placement moved -- which is how an override of
    /// a free parameter is caught, since that changes no object's revision.
    PlacementChanged,
    /// Something the solve needs is broken, so no transforms were published.
    Broken,
};

/// "not needed", "first", "object changed", ...
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::string_view toString(SolveTrigger trigger) noexcept;

/// What the last regeneration pass did about the assembly.
struct AssemblyRegeneration {
    SolveTrigger trigger = SolveTrigger::NotNeeded;
    /// Set when the solve ran; absent when it did not.
    std::optional<SolveStatus> status{};
    std::size_t degreesOfFreedom = 0;
    /// Components whose transforms the pass published.
    std::size_t transforms = 0;
};

/// Registers the assembly regeneration handlers on @p regenerator, capturing
/// @p resolver for external references.
///
/// `features` does not know assemblies exist; assembly registers its own
/// handlers through the public interface, which is what makes its layer
/// workable (ADR-006).
///
/// The component handler exists to stop an unresolvable part being silent.
/// An external part is not a dependency edge, so the graph reports nothing
/// missing and a handler-less component would regenerate as if all were
/// well. With the handler, a component whose part does not resolve **fails**,
/// and its state says which of the four reasons it was.
/// @p report, when given, is written on every pass with what the final pass
/// did -- whether it re-solved and why. The caller owns it, so that
/// observing the trigger needs no global state; it is the same shape as the
/// resolver, which is also captured rather than looked up.
BETTERCAD_ASSEMBLY_EXPORT void registerHandlers(features::Regenerator& regenerator,
                                                const ReferenceResolver* resolver = nullptr,
                                                AssemblyRegeneration* report = nullptr);

} // namespace bettercad::assembly
