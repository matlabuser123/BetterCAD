// Build-failure tests for support reactions (see CMakeLists.txt in this
// directory). Built once without any BETTERCAD_CF_* macro as a control, which
// must compile -- so every failure below is attributable to its own line.
//
// Four claims of P17-REACTION-001 are enforced here rather than reviewed:
//
//   1. REACTIONS CANNOT BE CONJURED. `SupportReactions` has a private
//      constructor and exactly one friend, so possessing one is the evidence
//      that every source agreed, that every constrained degree of freedom was
//      read once, and that BOTH equilibrium gates passed (ADR-036, ADR-041).
//      A hand-built one would be a set of reactions that nobody balanced.
//
//   2. THERE IS NO NODAL ROTATIONAL REACTION. A Tet4 node has `Ux, Uy, Uz`,
//      so a `SupportReaction` carries a `Force3D` and no moment at all. A
//      support region's moment is DERIVED from the force distribution and is
//      only ever obtained with an explicit origin. Asking a nodal reaction for
//      a moment must not compile.
//
//   3. A MOMENT NEEDS ITS ORIGIN. `momentAbout` takes a mesh and a point;
//      there is no nullary overload, because a moment about an unstated point
//      is not a quantity.
//
//   4. A FORCE IS NOT A MOMENT AND NEITHER IS A NUMBER. The equilibrium
//      thresholds are dimensionless ratios and the floors are quantities, and
//      the compiler keeps the three apart -- which is what stops a newton
//      floor being used as a newton-metre one.
#include <bettercad/core/Units.hpp>
#include <bettercad/structural/StructuralReaction.hpp>

using namespace bettercad;
using namespace bettercad::structural;

namespace {

void takesForce(Force /*unused*/) {}
void takesTorque(Torque /*unused*/) {}
void takesDouble(double /*unused*/) {}
void takesForce3(const Force3D& /*unused*/) {}

} // namespace

int main() {
    // The legitimate spellings, so the control compiles.
    EquilibriumTolerance tolerance;
    tolerance.force = 1.0e-12;
    tolerance.moment = 1.0e-12;
    tolerance.forceFloor = Force::fromSi(1.0e-12);
    tolerance.momentFloor = Torque::fromSi(1.0e-12);
    (void)validate(tolerance);

    SupportReaction reaction;
    reaction.node = meshing::NodeId::fromValue(1);
    reaction.force = Force3D{Force::fromSi(1.0), Force::fromSi(2.0), Force::fromSi(3.0)};
    reaction.constrained = RestraintComponents::fixed();
    takesForce3(reaction.force);
    takesForce(reaction.force.x);

    ForceBalance forceBalance;
    takesForce(forceBalance.euclideanNorm);
    takesForce(forceBalance.scale);
    takesDouble(forceBalance.normalized);

    MomentBalance momentBalance;
    takesTorque(momentBalance.euclideanNorm);
    takesTorque(momentBalance.scale);
    takesDouble(momentBalance.normalized);

    RestraintReaction summary;
    summary.restraint = RestraintId::fromValue(1);
    takesForce3(summary.ownedForce);
    takesForce3(summary.sharedForce);

#if defined(BETTERCAD_CF_SUPPORT_REACTIONS_CONSTRUCTED_DIRECTLY)
    // Only recoverSupportReactions may build one, because only it has checked
    // every source and applied both equilibrium gates. There is no default
    // constructor at all.
    const SupportReactions reactions{};
    (void)reactions;
#endif

#if defined(BETTERCAD_CF_NODAL_REACTION_HAS_A_MOMENT)
    // A Tet4 node has no rotational degree of freedom, so a nodal reaction
    // carries no moment. A support region's moment is derived from the
    // distribution and needs an origin.
    takesTorque(reaction.moment.x);
#endif

#if defined(BETTERCAD_CF_REACTION_FORCE_AS_TORQUE)
    // A force resultant is not a moment. Conflating them is the unit error
    // that would let a newton be reported as a newton-metre.
    takesTorque(summary.ownedForce.x);
#endif

#if defined(BETTERCAD_CF_FORCE_NORM_AS_DOUBLE)
    // ||e_F|| is a Force in newtons; it does not decay to a number, so it
    // cannot be compared against the dimensionless normalized ratio by
    // accident.
    takesDouble(forceBalance.euclideanNorm);
#endif

#if defined(BETTERCAD_CF_MOMENT_FLOOR_AS_FORCE)
    // The floors carry their own dimension. One dimensionless number for both
    // would be a unit error the compiler could not see, so there are two
    // typed fields and they do not interchange.
    tolerance.momentFloor = Force::fromSi(1.0e-12);
#endif

#if defined(BETTERCAD_CF_TOLERANCE_HAS_A_SOLVER_RESIDUAL)
    // A solver residual tolerance is NOT an equilibrium tolerance: the
    // solver's gate is on the free equations and this one is on the whole
    // body. The field is absent rather than present and ignored.
    tolerance.relativeResidualTolerance = 1.0e-9;
#endif

    return 0;
}
