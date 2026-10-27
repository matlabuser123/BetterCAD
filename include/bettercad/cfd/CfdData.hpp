#pragma once

// P19-DATA-001 -- the CFD analysis data model: canonical intent on one side,
// derived value types on the other, and the line between them.
//
// THE LINE, WHICH IS THE WHOLE MILESTONE (ADR-058, ADR-061):
//
// ```text
// CANONICAL   the analysis definition: which mesh control, which mode, the
//             boundary conditions, the initial condition, the pressure datum,
//             gravity, the iteration, convergence and linear-solver settings,
//             the steady or transient settings and the output settings.
//             Document state. Undone, redone, revisioned, and (P19-PERSIST-001)
//             persisted. Names CAD geometry by FaceName and nothing mesh-local.
// DERIVED     everything a solve produces or needs on the way: cells, faces,
//             centroids, normals, every CellIndex and FaceIndex, matrices,
//             factorisations, p, U, phi, gradients, residual vectors, forces,
//             pressure drop, flow split, render buffers. Reproducible from the
//             canonical intent plus the current mesh and fluid. Never persisted
//             as authority, never undo payload, never a member of anything
//             canonical.
// ```
//
// The value types the derived side needs that are VALUES WITH UNITS rather
// than results -- a velocity triple, the resolved fluid properties, a set of
// residuals, the two mass-balance measures -- live here too. The result
// CONTAINER, which binds them to a mesh stamp and a provenance record, is
// `CfdResult.hpp`, and it is deliberately not reachable from the analysis
// object's header: a definition cannot hold a result because the result type is
// not visible where the definition is.
//
// EVERY QUANTITY IS SI THROUGH THE UNITS FRAMEWORK: velocity m/s, pressure Pa
// (gauge, ADR-058), acceleration m/s^2, length m, time s, density kg/m^3,
// dynamic viscosity Pa s. Kinematic viscosity is DERIVED (mu / rho) and never
// stored. The only bare doubles are ratios: relaxation factors, tolerances and
// normalised residuals.
//
// WHAT IS NOT HERE. No cell, face, owner/neighbour pair, field array, CellIndex,
// matrix, boundary-face resolution, cardinality check, operator or solve. Those
// are P19-TOPO-001 onwards. This file defines what they will read and what they
// will report.

#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

namespace bettercad::cfd {

// ---------------------------------------------------------------------------
// Mode
// ---------------------------------------------------------------------------

/// What kind of flow problem an analysis poses (ADR-058).
///
/// AN ENUMERATION, NOT A BOOL, for the reason `thermal::ThermalAnalysisMode`
/// is one: the two modes have different REQUIRED inputs -- a steady analysis
/// needs an iteration budget and must not carry a time step, a transient one
/// needs a time step, an end time and a per-step budget -- and
/// `validate(definition)` enforces that per mode.
enum class CfdAnalysisMode : std::uint8_t {
    /// The velocity and pressure the boundary conditions settle to, reached by
    /// a SIMPLE-family outer iteration. No time in it.
    Steady,
    /// Advanced by backward Euler at a fixed step from the initial condition
    /// to an end time, with an outer iteration inside every step. The method is
    /// fixed and not a field: there is one, so a selector would be a false
    /// capability.
    Transient,
};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdAnalysisMode mode) noexcept;

// ---------------------------------------------------------------------------
// Vectors with units
// ---------------------------------------------------------------------------

/// The three Cartesian components, in the order every vector below stores
/// them.
///
/// FROZEN HERE, and deliberately a copy of `thermal::VectorComponent` rather
/// than a use of it: cfd (layer 42) cannot include thermal (layer 45), and the
/// order -- x, y, z -- is the one every module of the tree already uses.
enum class VectorComponent : std::uint8_t { X = 0, Y = 1, Z = 2 };

/// The three, in index order.
inline constexpr std::array<VectorComponent, 3> kVectorComponents{VectorComponent::X, VectorComponent::Y,
                                                                  VectorComponent::Z};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(VectorComponent component) noexcept;

/// A Cartesian vector whose components are one quantity.
///
/// ONE TEMPLATE, DISTINCT TYPES. `Velocity3` and `Acceleration3` are both
/// instances, and because the quantity is the template argument they are
/// different types: a gravity vector cannot be passed where a wall velocity is
/// wanted. The field names ARE the components, so `x` cannot be read as a
/// position. The magnitude is derived on every call and never stored.
template <typename Q>
struct CartesianVector {
    Q x{};
    Q y{};
    Q z{};

    [[nodiscard]] constexpr Q component(VectorComponent which) const noexcept {
        switch (which) {
        case VectorComponent::X:
            return x;
        case VectorComponent::Y:
            return y;
        case VectorComponent::Z:
            return z;
        }
        return {};
    }

    /// `sqrt(x^2 + y^2 + z^2)`, in the components' unit.
    [[nodiscard]] Q magnitude() const noexcept { return Q::fromSi(std::hypot(x.si(), y.si(), z.si())); }

    friend constexpr bool operator==(const CartesianVector&, const CartesianVector&) = default;
};

/// A velocity, in m/s.
using Velocity3 = CartesianVector<Velocity>;
/// An acceleration, in m/s^2. Gravity is one.
using Acceleration3 = CartesianVector<Acceleration>;

/// Every component finite.
template <typename Q>
[[nodiscard]] bool isFinite(const CartesianVector<Q>& vector) noexcept {
    return bettercad::isFinite(vector.x) && bettercad::isFinite(vector.y) && bettercad::isFinite(vector.z);
}

/// Every coordinate finite.
[[nodiscard]] inline bool isFinite(const Point3D& point) noexcept {
    return bettercad::isFinite(point.x) && bettercad::isFinite(point.y) && bettercad::isFinite(point.z);
}

// ---------------------------------------------------------------------------
// Canonical boundary conditions
// ---------------------------------------------------------------------------
//
// SIX KINDS, EXACTLY ADR-058's. Each names a CAD face by `FaceName` and holds
// typed physical values and nothing else: no facet handle, no FaceIndex, no
// patch number, no snapshot of a resolved set. Which boundary faces a condition
// covers is derived afresh for whatever mesh is current, and a face that does
// not resolve is a named refusal, never a rebind (ADR-032). Whether every
// boundary facet carries exactly one condition, whether a velocity points into
// the domain on every facet, and whether a moving wall's velocity is tangential
// are questions about a mesh and belong to P19-BC-001.

/// Fluid enters through a CAD face at a uniform speed NORMAL to it, in m/s.
///
/// POSITIVE IS INTO THE DOMAIN, and the speed must be finite and positive. A
/// zero inflow is a no-slip wall wearing an inlet record -- one condition with
/// two spellings, which would let the same intent have two fingerprints -- and
/// a negative one is an outlet that prescribes its outflow, which v1 does not
/// offer. Inflow at an angle is not in v1 (ADR-061).
struct VelocityInlet {
    FaceName face{};
    Velocity normalSpeed{};

    friend bool operator==(const VelocityInlet&, const VelocityInlet&) = default;
};

/// Fluid enters through a CAD face held at a static gauge pressure, in Pa.
///
/// The flow rate is an OUTCOME of the solve, not an input; the inflow direction
/// is normal to the face. Any finite value: gauge pressure is relative to the
/// datum, so a negative one is ordinary.
struct PressureInlet {
    FaceName face{};
    Pressure staticPressure{};

    friend bool operator==(const PressureInlet&, const PressureInlet&) = default;
};

/// Fluid leaves through a CAD face held at a static gauge pressure, in Pa.
/// Any finite value. What happens if fluid re-enters through it (backflow) is
/// P19-BC-001's policy to state, not a default.
struct PressureOutlet {
    FaceName face{};
    Pressure staticPressure{};

    friend bool operator==(const PressureOutlet&, const PressureOutlet&) = default;
};

/// The fluid at a CAD face is at rest: zero velocity, no mass flux.
struct NoSlipWall {
    FaceName face{};

    friend bool operator==(const NoSlipWall&, const NoSlipWall&) = default;
};

/// The fluid at a CAD face moves with the wall at a prescribed velocity, in
/// m/s: the lid of a driven cavity, the moving plate of a Couette flow.
///
/// Finite and NOT ZERO: a wall that does not move is a `NoSlipWall`, and one
/// intent has one spelling. The wall itself does not move -- there is no moving
/// mesh (ADR-058) -- so the velocity must lie in the face; whether it does is a
/// question about the geometry, answered by P19-BC-001.
struct MovingWall {
    FaceName face{};
    Velocity3 velocity{};

    friend bool operator==(const MovingWall&, const MovingWall&) = default;
};

/// Slip or symmetry at a CAD face: no flow through it, no shear along it. For
/// an incompressible flow the two are the same condition, so they are one kind.
struct SlipWall {
    FaceName face{};

    friend bool operator==(const SlipWall&, const SlipWall&) = default;
};

/// Which kind of condition a `CfdBoundaryCondition` carries.
enum class CfdBoundaryConditionKind : std::uint8_t {
    VelocityInlet,
    PressureInlet,
    PressureOutlet,
    NoSlipWall,
    MovingWall,
    Slip,
};

/// The six, in declaration order.
inline constexpr std::array<CfdBoundaryConditionKind, 6> kCfdBoundaryConditionKinds{
    CfdBoundaryConditionKind::VelocityInlet, CfdBoundaryConditionKind::PressureInlet,
    CfdBoundaryConditionKind::PressureOutlet, CfdBoundaryConditionKind::NoSlipWall,
    CfdBoundaryConditionKind::MovingWall, CfdBoundaryConditionKind::Slip};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdBoundaryConditionKind kind) noexcept;

/// One canonical boundary condition: an identity, a CAD face and exactly one
/// strongly-typed payload.
///
/// A VARIANT, for the reason the thermal and structural conditions are: an
/// inflow speed, a pressure and a wall velocity have different units, and a
/// shared `double value` would let one be read as another. Every kind targets
/// a face, so `face()` always answers.
class BETTERCAD_CFD_EXPORT CfdBoundaryCondition {
public:
    CfdBoundaryCondition(CfdBoundaryConditionId id, VelocityInlet condition)
        : id_(id), payload_(std::move(condition)) {}
    CfdBoundaryCondition(CfdBoundaryConditionId id, PressureInlet condition)
        : id_(id), payload_(std::move(condition)) {}
    CfdBoundaryCondition(CfdBoundaryConditionId id, PressureOutlet condition)
        : id_(id), payload_(std::move(condition)) {}
    CfdBoundaryCondition(CfdBoundaryConditionId id, NoSlipWall condition)
        : id_(id), payload_(std::move(condition)) {}
    CfdBoundaryCondition(CfdBoundaryConditionId id, MovingWall condition)
        : id_(id), payload_(std::move(condition)) {}
    CfdBoundaryCondition(CfdBoundaryConditionId id, SlipWall condition)
        : id_(id), payload_(std::move(condition)) {}

    /// The persisted identity. NOT a thermal boundary condition's, NOT a
    /// restraint's.
    [[nodiscard]] CfdBoundaryConditionId id() const noexcept { return id_; }
    [[nodiscard]] CfdBoundaryConditionKind kind() const noexcept;

    /// The payload, or nullptr if this condition is of another kind.
    [[nodiscard]] const VelocityInlet* velocityInlet() const noexcept { return std::get_if<VelocityInlet>(&payload_); }
    [[nodiscard]] const PressureInlet* pressureInlet() const noexcept { return std::get_if<PressureInlet>(&payload_); }
    [[nodiscard]] const PressureOutlet* pressureOutlet() const noexcept {
        return std::get_if<PressureOutlet>(&payload_);
    }
    [[nodiscard]] const NoSlipWall* noSlipWall() const noexcept { return std::get_if<NoSlipWall>(&payload_); }
    [[nodiscard]] const MovingWall* movingWall() const noexcept { return std::get_if<MovingWall>(&payload_); }
    [[nodiscard]] const SlipWall* slipWall() const noexcept { return std::get_if<SlipWall>(&payload_); }

    /// The CAD face this condition applies to. Every kind has one.
    [[nodiscard]] const FaceName& face() const noexcept;

    /// Whether this condition FIXES THE PRESSURE LEVEL: a static-pressure inlet
    /// or outlet. The pressure datum is decided from this and nothing else
    /// (ADR-058); see `pressureDatumProblem`.
    [[nodiscard]] bool fixesPressure() const noexcept;

    friend bool operator==(const CfdBoundaryCondition&, const CfdBoundaryCondition&) = default;

private:
    CfdBoundaryConditionId id_{};
    std::variant<VelocityInlet, PressureInlet, PressureOutlet, NoSlipWall, MovingWall, SlipWall> payload_;
};

/// Checks @p condition on its own terms: a valid identity, a valid face
/// selector, finite values, a positive inflow speed and a non-zero wall
/// velocity. DOES NOT CONSULT A MESH OR THE GEOMETRY, for the reason
/// `thermal::validate(ThermalBoundaryCondition)` gives: whether the face
/// resolves is a question about a particular body and belongs to P19-BC-001.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdBoundaryCondition& condition);

// ---------------------------------------------------------------------------
// Canonical initial condition
// ---------------------------------------------------------------------------

/// The velocity and gauge pressure every cell starts at.
///
/// UNIFORM, AND THAT IS THE AUTHORIZED SHAPE (ADR-058). A per-cell field would
/// be mesh-local state inside canonical intent and a field from an earlier
/// result would be derived state promoted to authority; one value per
/// quantity is neither.
///
/// PRESENT IN BOTH MODES, UNLIKE THERMAL'S (ADR-061). A steady conduction solve
/// is linear and has no starting point, so P18 refuses an initial temperature
/// on a steady analysis as inert. A steady flow solve is a nonlinear iteration:
/// where it starts decides the path it takes and the bits it lands on, so the
/// starting point is intent the user owns rather than a hidden default. In a
/// transient analysis it is the physical state at t = 0.
struct CfdInitialCondition {
    Velocity3 velocity{};
    Pressure pressure{};

    friend bool operator==(const CfdInitialCondition&, const CfdInitialCondition&) = default;
};

/// Every value finite.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdInitialCondition& initial);

// ---------------------------------------------------------------------------
// Canonical pressure datum and gravity
// ---------------------------------------------------------------------------

/// The pressure level of an analysis no boundary condition fixes: the gauge
/// pressure is `value` at `location`.
///
/// A POINT IN MODEL SPACE, IN METRES, NOT A CELL (ADR-061). A reference cell is
/// mesh-local and would be orphaned by a remesh; a point is CAD-space intent
/// that every mesh of the body can resolve. `value` is the full static gauge
/// pressure there, hydrostatic part included (ADR-058), so the point alone fixes
/// the level with or without gravity; `CfdGravity::referenceHeight` is about
/// prescribed BOUNDARY pressures and does not enter here. Whether the point lies
/// in the fluid is a question about the geometry, answered where the datum is
/// resolved (P19-BC-001).
struct CfdReferencePressure {
    Pressure value{};
    Point3D location{};

    friend bool operator==(const CfdReferencePressure&, const CfdReferencePressure&) = default;
};

/// Value and location finite.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdReferencePressure& reference);

/// The body force per unit mass, `g`, applied as `rho g`, and the height the
/// hydrostatic part of a prescribed pressure is taken from (ADR-058, ADR-061).
///
/// A VECTOR, NOT A MAGNITUDE AND AN AXIS: "down" is whatever the model says it
/// is. NO GRAVITY IS THE ABSENCE OF THIS RECORD, not a zero vector: a zero
/// vector would be a second spelling of "none".
///
/// THE REFERENCE HEIGHT IS PART OF GRAVITY, because it exists exactly when
/// gravity does. The hydrostatic part is inside `p` (ADR-058), so a uniform
/// prescribed pressure over a face that is not level would contradict
/// hydrostatics. A static-pressure inlet or outlet therefore prescribes the
/// static gauge pressure AT THIS HEIGHT, and along its face the pressure varies
/// hydrostatically, `p_b - rho |g| (h(x) - referenceHeight)`. Heights are measured
/// along `-g` (up) from the model origin, in metres. Without gravity there is no
/// height and a boundary pressure is uniform. P19-BC-001 applies it.
///
/// IN A DOMAIN NO BOUNDARY PRESSURE FIXES (a closed cavity under gravity) the
/// height has no effect: the reference pressure's point fixes the level. It is
/// carried rather than made conditional, because making the shape of gravity
/// depend on the boundary set would put a second datum rule in a second place.
/// ADR-061 records it as the one value made inert by the BOUNDARY SET (others,
/// like the residual history of a one-iteration budget, are inert only at
/// particular settings).
///
/// ALWAYS CARRIED, DEFAULTING TO 0 m (the model origin's height): a gravity
/// record built from the vector alone takes that datum. Requiring the user to
/// state it is the command layer's job (P19-CMD-001), not a missing value here.
struct CfdGravity {
    Acceleration3 acceleration{};
    Length referenceHeight{};

    friend bool operator==(const CfdGravity&, const CfdGravity&) = default;
};

/// The vector finite and not zero; the reference height finite.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdGravity& gravity);

// ---------------------------------------------------------------------------
// Canonical solver settings
// ---------------------------------------------------------------------------
//
// CANONICAL INTENT, for the reason ADR-047 made structural and P18 made thermal
// solver settings canonical: a value a user chooses must be undoable, redoable
// and persistable, and settings that live only as an argument to the solve are
// none of those. There is NO ALGORITHM SELECTOR anywhere below. The coupling is
// SIMPLE-family, the time integration backward Euler and the convection scheme
// first-order upwind (ADR-058); until a second one is qualified a selector
// would be a field with one legal value.

/// The nonlinear outer iteration: how much of each correction is kept.
///
/// Two under-relaxation factors in (0, 1]. 1 keeps the whole correction; the
/// defaults are the classic SIMPLE pair. Used in both modes: by the steady
/// iteration and by the iteration inside every transient step.
struct CfdIterationSettings {
    double velocityRelaxation = 0.7;
    double pressureRelaxation = 0.3;

    friend bool operator==(const CfdIterationSettings&, const CfdIterationSettings&) = default;
};

/// Both factors finite and in (0, 1].
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdIterationSettings& settings);

/// When the outer iteration is converged, and when it has diverged.
///
/// ```text
/// momentumResidualTolerance     the normalised momentum residual, every
///                               component, below which momentum is converged
/// continuityResidualTolerance   the normalised continuity residual below
///                               which continuity is converged
/// divergenceFactor              the loop has DIVERGED when a normalised
///                               residual exceeds this multiple of its value
///                               at the first iteration
/// ```
///
/// CONVERGENCE IS NOT ACCEPTANCE. These decide when the loop stops; whether the
/// result it stopped at is accepted -- the mass balance, the finiteness, the
/// laminar bound -- is decided in one place by validation (P19-VALID-001), with
/// thresholds that are policy and not user settings (ADR-053's rule).
struct CfdConvergenceSettings {
    double momentumResidualTolerance = 1.0e-6;
    double continuityResidualTolerance = 1.0e-6;
    double divergenceFactor = 1.0e3;

    friend bool operator==(const CfdConvergenceSettings&, const CfdConvergenceSettings&) = default;
};

/// Both tolerances finite and positive; the divergence factor finite and
/// greater than 1 (at 1 or below the first iteration would already have
/// "diverged").
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdConvergenceSettings& settings);

/// How every linear solve inside the outer iteration is judged.
///
/// ONE RATIO: the normalised residual of the solved system, computed
/// independently of the solver, above which a linear solve is refused. Which
/// solver is used is P19-LINSOLVE-001's decision and not a field.
struct CfdLinearSolverSettings {
    double relativeResidualTolerance = 1.0e-9;

    friend bool operator==(const CfdLinearSolverSettings&, const CfdLinearSolverSettings&) = default;
};

/// Finite and positive.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdLinearSolverSettings& settings);

/// The budget of a steady analysis: at most this many outer iterations. Reaching
/// it without converging is a FAILURE ("iteration limit"), never a result.
struct CfdSteadySettings {
    std::uint32_t maxIterations = 1000;

    friend bool operator==(const CfdSteadySettings&, const CfdSteadySettings&) = default;
};

/// At least one iteration.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdSteadySettings& settings);

/// Which time levels a transient analysis keeps.
enum class CfdTimeSampling : std::uint8_t {
    /// Only the field at the end time.
    FinalOnly,
    /// The field at every step, including the end time.
    EveryStep,
};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdTimeSampling sampling) noexcept;

/// The time discretisation of a transient analysis.
///
/// A FIXED STEP from t = 0 to `endTime`, the last step shortened so the run
/// lands on `endTime` exactly (ADR-058, as P18's `TimeGrid`). Inside every step
/// at most `maxIterationsPerStep` outer iterations; a step that does not
/// converge within them fails the analysis.
struct CfdTransientSettings {
    Time timeStep{};
    Time endTime{};
    std::uint32_t maxIterationsPerStep = 50;
    CfdTimeSampling sampling = CfdTimeSampling::FinalOnly;

    friend bool operator==(const CfdTransientSettings&, const CfdTransientSettings&) = default;
};

/// `0 < timeStep <= endTime`, both finite; at least one iteration per step; a
/// sampling policy this build recognises.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdTransientSettings& settings);

/// How much of the iteration's history a result keeps.
enum class CfdResidualHistory : std::uint8_t {
    /// The final residuals only.
    FinalOnly,
    /// The residuals after every outer iteration.
    EveryIteration,
};

[[nodiscard]] BETTERCAD_CFD_EXPORT std::string_view toString(CfdResidualHistory history) noexcept;

/// What a solve reports beyond the fields themselves, in either mode.
///
/// THE FIELDS ARE NOT SELECTABLE. A result always carries p, U and phi: a
/// result without the face flux could not have its mass balance checked, so an
/// option to omit it would be an option to publish something unverifiable. The
/// engineering integrals (a force on a face, the flow through a face) are
/// requests that name CAD faces; they are P19-FORCES-001's and land here when
/// it defines them.
struct CfdOutputSettings {
    CfdResidualHistory residualHistory = CfdResidualHistory::FinalOnly;

    friend bool operator==(const CfdOutputSettings&, const CfdOutputSettings&) = default;
};

/// A history policy this build recognises.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdOutputSettings& settings);

// ---------------------------------------------------------------------------
// Derived value types
// ---------------------------------------------------------------------------

/// The fluid model of P19 v1, as the solver consumes it: an incompressible
/// Newtonian fluid with a constant density and a constant dynamic viscosity.
///
/// DERIVED, NEVER CANONICAL. Both values are resolved from the document's P15
/// material (ADR-060) every time they are needed -- the density today through
/// `requireDensity`, the viscosity once P19-FLUID-001 gives P15 one -- and are
/// never stored in a definition, a file or a command. The kinematic viscosity
/// is computed, never held, so it cannot disagree with the two it comes from.
struct CfdFluidProperties {
    Density density{};
    DynamicViscosity viscosity{};

    /// `nu = mu / rho`, in m^2/s.
    [[nodiscard]] KinematicViscosity kinematicViscosity() const noexcept { return viscosity / density; }

    friend bool operator==(const CfdFluidProperties&, const CfdFluidProperties&) = default;
};

/// Both finite and positive. A zero viscosity is an inviscid fluid, which v1
/// does not solve; a zero density divides.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<void> validate(const CfdFluidProperties& fluid);

/// The normalised residuals of one outer iteration: one per momentum component,
/// in `VectorComponent` order, and continuity. Dimensionless.
struct CfdResiduals {
    double momentumX = 0.0;
    double momentumY = 0.0;
    double momentumZ = 0.0;
    double continuity = 0.0;

    friend bool operator==(const CfdResiduals&, const CfdResiduals&) = default;
};

/// Every residual finite and not negative (a norm cannot be).
[[nodiscard]] BETTERCAD_CFD_EXPORT bool isWellFormed(const CfdResiduals& residuals) noexcept;

/// The two mass-balance measures ADR-058 fixed, each relative to the boundary
/// throughput (`0.5 * sum |phi|` over the boundary faces, with a floor). The
/// floor and the acceptance thresholds are P19-MASSBAL-001's; the measures are
/// fixed here so that no later milestone can quietly report a different one.
///
/// ```text
/// maxCellContinuity   max over cells of |sum_f phi_f|, relative
/// globalImbalance     |sum over boundary faces of phi|, relative
/// ```
struct CfdMassBalance {
    double maxCellContinuity = 0.0;
    double globalImbalance = 0.0;

    friend bool operator==(const CfdMassBalance&, const CfdMassBalance&) = default;
};

/// Both measures finite and not negative.
[[nodiscard]] BETTERCAD_CFD_EXPORT bool isWellFormed(const CfdMassBalance& balance) noexcept;

} // namespace bettercad::cfd
