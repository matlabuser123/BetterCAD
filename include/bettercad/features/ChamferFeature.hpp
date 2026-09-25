#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/geometry/Chamfer.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/features/Export.hpp>
#include <bettercad/features/Feature.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::features {

/// One edge selection of a chamfer: the curve it selects, and the selection's
/// own persistent identity.
///
/// THE ID IS WHY THIS STRUCT EXISTS. A chamfer face is referenced by a
/// drawing dimension or annotation as {chamfer feature, this id}, and the id
/// travels with the selection: reordering `ChamferDefinition::edges` moves the
/// pairs, so every stored reference still names the material it named before.
/// Naming the face by the selection's POSITION instead -- which is what this
/// replaced -- left a reference resolving to a different face after an edit
/// that did not change the solid at all (ADR-024).
///
/// `curve` is an EdgeSignature and is not a persistent name: it says which
/// edge of the target body to chamfer, and it can stop matching when the
/// target changes shape. `id` is the persistent part.
struct ChamferEdge {
    /// Left unset when building a definition: `ChamferFeature` allocates one.
    /// An id already set is preserved, which is how a reorder keeps its
    /// references and how a loaded file keeps the ids it was saved with.
    ChamferEdgeId id{};
    geometry::EdgeSignature curve{};

    ChamferEdge() = default;
    /// A NEW selection of @p selected, whose identity `ChamferFeature` will
    /// allocate.
    ///
    /// EXPLICIT ON PURPOSE, and it is the difference between re-pointing a
    /// selection and replacing it. Given `edges[1] = someCurve`, an implicit
    /// conversion would quietly discard the identity `edges[1]` already had and
    /// mint a new one -- so a user repairing a chamfer whose edge moved would
    /// lose every drawing reference to that face, silently. Spelling it out
    /// forces the choice to be visible:
    ///
    ///     edges[1].curve = someCurve;          // re-select: same selection,
    ///                                          // references follow
    ///     edges[1] = ChamferEdge{someCurve};   // replace: a new selection,
    ///                                          // old references unresolve
    explicit ChamferEdge(geometry::EdgeSignature selected) : curve(selected) {}
    ChamferEdge(ChamferEdgeId identity, geometry::EdgeSignature selected)
        : id(identity), curve(selected) {}

    friend bool operator==(const ChamferEdge&, const ChamferEdge&) = default;
};

/// Inputs of a chamfer feature, e.g.
/// `ChamferDefinition{.target = padId, .edges = {edge}, .distance = 5_mm}`.
struct ChamferDefinition {
    /// The feature whose body is chamfered. The chamfer consumes it: the
    /// chamfered body is the model's result in its place.
    FeatureId target{};
    /// The edges of the target's body to chamfer, each with its own stable
    /// identity. Order is presentation only: it does not name anything, and
    /// changing it changes no reference and no geometry.
    std::vector<ChamferEdge> edges{};
    geometry::ChamferMode mode = geometry::ChamferMode::EqualDistance;
    /// EqualDistance: on both faces. Otherwise: on the reference face. Used
    /// when no parameter drives it.
    Length distance{};
    /// Document parameter (a length) that drives `distance`, if any.
    std::optional<ParameterId> distanceParameter{};
    /// TwoDistance only.
    Length distance2{};
    /// DistanceAngle only, in (0, 90°).
    Angle angle{};
    /// TwoDistance and DistanceAngle: selects the reference face at each edge
    /// (see geometry::ChamferRequest).
    std::optional<Direction3D> referenceSide{};

    friend bool operator==(const ChamferDefinition&, const ChamferDefinition&) = default;
};

/// Checks that the definition is self-consistent (the target and its edges
/// are resolved only at regeneration): a valid target, at least one valid
/// edge without duplicates, and positive distances and angle for the mode.
/// Fields the mode does not use must be left at zero, so no input is
/// silently ignored.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT Result<void> validate(const ChamferDefinition& definition);

/// The selected curves in list order, for the geometry request. The kernel
/// chamfers curves and knows nothing about identity; the mapping from a
/// request's edge index back to the selection's id is the namer's job.
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::vector<geometry::EdgeSignature> chamferCurves(
    const ChamferDefinition& definition);

/// The selections' ids in list order, parallel to chamferCurves().
[[nodiscard]] BETTERCAD_FEATURES_EXPORT std::vector<ChamferEdgeId> chamferEdgeIds(
    const ChamferDefinition& definition);

/// Chamfers edges of another feature's body (type name "chamfer"). Stores its
/// inputs only; its body is computed by regeneration from the target's body.
class BETTERCAD_FEATURES_EXPORT ChamferFeature final : public SolidFeature {
public:
    using Definition = ChamferDefinition;
    static constexpr std::string_view kTypeName = "chamfer";

    [[nodiscard]] static Result<std::unique_ptr<ChamferFeature>> create(std::string name,
                                                                         const ChamferDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The target feature and the distance parameter.
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;
    [[nodiscard]] std::optional<FeatureId> target() const noexcept override { return definition_.target; }

    [[nodiscard]] const ChamferDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition after validating it, giving every edge
    /// selection a stable identity.
    ///
    /// An edge whose `id` is unset gets a FRESH one, never a retired one. An
    /// edge whose `id` is already set keeps it, and must be one THIS chamfer
    /// currently has: that is what lets a caller reorder, insert into or
    /// shorten the list without disturbing a stored reference, while making it
    /// impossible to revive a deleted selection's id and capture the
    /// references that still name it. InvalidArgument for an id this chamfer
    /// does not have, or for a duplicate.
    Result<bool> setDefinition(const ChamferDefinition& definition);

    /// Highest edge ID allocated or reserved, so a reload can continue the
    /// sequence rather than restart it and hand out a retired id.
    [[nodiscard]] std::uint64_t lastEdgeId() const noexcept { return edgeIds_.lastValue(); }

    /// Rebuilds a chamfer as it was saved: the definition verbatim, including
    /// every edge id, and the allocator continued through @p lastEdgeId. For
    /// deserialization only -- ordinary code uses create() and setDefinition(),
    /// which allocate.
    [[nodiscard]] static Result<std::unique_ptr<ChamferFeature>> restore(
        std::string name, const ChamferDefinition& definition, std::uint64_t lastEdgeId);

private:
    ChamferFeature(std::string name, const ChamferDefinition& definition);

    /// Fills in missing edge ids and checks the ones that are set.
    /// @p existing is the definition the ids must come from, or nullptr when
    /// there is none yet (create and restore).
    Result<ChamferDefinition> identify(const ChamferDefinition& definition,
                                       const ChamferDefinition* existing);

    ChamferDefinition definition_;
    IdAllocator edgeIds_;
};

} // namespace bettercad::features
