#pragma once

#include <bettercad/assembly/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/core/document/MateReference.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Assembly constraints (P13-MATE-001, implementing ADR-004).
//
// A mate is engineering INTENT: "these two faces are coincident", "this
// component is fixed". It is not a solution, and this milestone moves
// nothing. Solving intent into transforms is P13-SOLVE-001, and the split is
// the same one ADR-005 drew for placement:
//
//     canonical, persisted     derived, computed
//     --------------------     -----------------
//     MateDefinition      ->   component transforms
//
// So a mate holds no residual, no Jacobian, no iteration state and no solved
// transform, and nothing here is a solver-facing cache. What the solver will
// consume is the definition itself.
namespace bettercad::assembly {

/// The basic constraints. Seven kinds, and no more: a kind that cannot be
/// stated exactly is not added until it can be.
enum class MateType {
    /// Holds one component where it is. This is the datum the rest solve
    /// against, and is why a component needs no separate "grounded" flag.
    Fixed,
    /// Two planes in the same plane, or two axes on the same line.
    Coincident,
    /// Two axes on the same line. Distinct from Coincident because it says
    /// *why*: a shaft in a bore, not two faces that happen to meet.
    Concentric,
    /// Two directions parallel, in either sense.
    Parallel,
    /// Two directions at a right angle.
    Perpendicular,
    /// Two planes, or two axes, a given distance apart.
    Distance,
    /// Two directions at a given angle.
    Angle,

    // --- Mechanical mates (P13-MATE-002) -------------------------------
    //
    // Joints, named for the freedom they leave rather than the constraint
    // they add. Each is built from the same equation forms the basic
    // constraints use; what makes them worth their own kinds is that the
    // engineer said "hinge", not "two axes that happen to be collinear".
    //
    //     Cylindrical  = axes collinear                  2 DOF
    //     Revolute     = Cylindrical + axial position    1 rotational DOF
    //     Slider       = Cylindrical + roll              1 translational DOF
    //     Planar       = planes coincident               3 DOF

    /// A hinge: two axes collinear and held against sliding, free to turn
    /// about the axis. 1 rotational DOF.
    Revolute,
    /// A slide: two axes collinear and held against turning, free to move
    /// along the axis. 1 translational DOF. The only kind that needs a
    /// second pair of targets -- see `a2`/`b2`.
    Slider,
    /// A shaft that both turns and slides in its bore: two axes collinear.
    /// 1 translational + 1 rotational DOF.
    Cylindrical,
    /// Two faces that stay in one plane and may slide and spin in it.
    /// 2 in-plane translations + 1 rotation about the normal.
    Planar,
};

/// "fixed", "coincident", "concentric", "parallel", "perpendicular",
/// "distance", "angle".
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT std::string_view toString(MateType type) noexcept;

/// What a mate is made of.
///
/// The shape is deliberately not uniform, because the constraints are not:
/// `Fixed` holds one *component*, and the other six relate two pieces of
/// *geometry*. Pretending otherwise -- by giving `Fixed` a geometry target
/// it ignores -- would make a field that means nothing, which is exactly
/// what validation is meant to prevent. `CoordinateSystemDefinition` already
/// carries the same kind of by-kind asymmetry.
///
/// Conventions, stated here because a convention left implicit is a defect
/// waiting for the solver:
///
/// * A target's **direction** is a plane's or a face's normal, or an axis'
///   direction. `Parallel`, `Perpendicular` and `Angle` are about those
///   directions.
/// * `Angle` is the **unsigned** angle between the two directions, in
///   `[0, 180]` degrees. A value outside that is refused, never normalised
///   into range: silently wrapping 190 degrees to 170 would be the model
///   deciding what the engineer meant.
/// * `Distance` between two planes is **signed**, measured along the first
///   target's normal, so a plane on the other side is a negative distance.
///   Between two axes it is the perpendicular separation, which has no side
///   and so must not be negative.
struct MateDefinition {
    MateType type = MateType::Coincident;
    /// `Fixed` only: the component held in place.
    ComponentId component{};
    /// Every type but `Fixed`: the geometry related, in order. `a` is the
    /// first target, and it is `a`'s normal that gives `Distance` its sign.
    std::optional<MateTarget> a{};
    std::optional<MateTarget> b{};
    /// `Slider` only: the roll reference, one direction on each side, which
    /// is what stops the slide turning about its own axis.
    ///
    /// It is a second *pair* rather than an inferred direction because there
    /// is nothing to infer from. A target carries one direction, and a
    /// condition written over a single direction pair fixes at most two of a
    /// rotation's three degrees of freedom -- the turn about that direction
    /// is exactly what such a condition cannot see. Every other mechanical
    /// mate leaves that turn free and so needs no roll reference; a slide is
    /// the one that must remove it.
    ///
    /// `a2` names geometry on `a`'s component and `b2` on `b`'s, and neither
    /// may be parallel to the slide axis -- a roll reference along the axis
    /// says nothing about the roll. Refused on every other kind.
    std::optional<MateTarget> a2{};
    std::optional<MateTarget> b2{};
    /// `Distance` only.
    std::optional<Length> distance{};
    /// `Angle` only.
    std::optional<Angle> angle{};
    /// A suppressed mate keeps its identity and its targets; suppression is
    /// engineering intent ("not in this configuration"), not deletion.
    bool suppressed = false;

    friend bool operator==(const MateDefinition&, const MateDefinition&) = default;
};

/// Checks a definition on its own: the right fields for the kind, targets
/// that are self-consistent and of kinds the constraint can relate, a value
/// of the right dimension and in range, and two different targets on two
/// different components.
///
/// InvalidArgument otherwise, with a message naming what was wrong. Whether
/// the components exist and whether the geometry resolves is checked against
/// the document by checkMate(), because a definition alone cannot know.
[[nodiscard]] BETTERCAD_ASSEMBLY_EXPORT Result<void> validate(const MateDefinition& definition);

/// One assembly constraint (type name "mate").
class BETTERCAD_ASSEMBLY_EXPORT Mate final : public DocumentObject {
public:
    using Definition = MateDefinition;
    static constexpr std::string_view kTypeName = "mate";

    [[nodiscard]] static Result<std::unique_ptr<Mate>> create(std::string name, const MateDefinition& definition);

    [[nodiscard]] std::string_view typeName() const noexcept override { return kTypeName; }
    [[nodiscard]] std::unique_ptr<DocumentObject> clone() const override;
    [[nodiscard]] bool contentEquals(const DocumentObject& other) const override;
    /// The components the mate relates and every object its targets' geometry
    /// names, so a mate rebuilds when a datum it uses moves (ADR-004).
    [[nodiscard]] std::vector<ObjectId> dependencies() const override;

    /// This mate's ID, narrowed. Valid once the document owns it.
    [[nodiscard]] MateId mateId() const noexcept { return MateId::fromValue(id().value()); }

    [[nodiscard]] const MateDefinition& definition() const noexcept { return definition_; }
    /// Replaces the definition. Returns whether anything changed.
    Result<bool> setDefinition(const MateDefinition& definition);

private:
    Mate(std::string name, const MateDefinition& definition);

    MateDefinition definition_;
};

} // namespace bettercad::assembly
