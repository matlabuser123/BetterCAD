#pragma once

#include "MeshReferenceModels.hpp"

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/units/Units.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

// BetterCAD's thermal reference models (P18-REFMOD-001): a committed qualification suite for the whole P18
// stack, built only through the public document, parameter, sketch, feature, material, meshing and thermal APIs.
//
// A seventh suite beside the parts (P11/P12), the assemblies (P13), the drawings (P14), the engineering data
// (P15), the meshes (P16) and the structural models (P17), in the same shape: a struct of IDs per model, a
// builder per model, one catalog, and one loop in the runner.
//
// THESE ARE NOT DEMO MODELS, AND THEY ARE NOT THEIR OWN ORACLE. Each one exists to make a specific defect
// impossible to hide, and each is judged against a closed form that `tests/reference/ThermalAnalytic.hpp` computes from
// the model's own declared numbers. That file includes no BetterCAD header, so it CANNOT call a production
// formula; a reference model whose expected value came from the code under test would only compare the code
// with itself.
//
//   RM-THERM-01  a block whose mesh has an INTERIOR node, on which an affine temperature imposed on every
//                boundary node is a real patch test: constant gradient, constant flux, and an interior node
//                that the solve must put exactly on the plane
//   RM-THERM-02  a slab held at two temperatures: T(z) linear, q = k dT/L, Qdot = k A dT / L, reactions
//                +Qdot and -Qdot, at three mesh levels (the slab builder takes the face size)
//   RM-THERM-03  NO DOCUMENT. A composite wall needs two materials in one solid, and P18 v1 has neither a
//                per-body material (the assignment is the Document's) nor a multi-solid mesh (P16 refuses it).
//                See `kCompositeWallLimitation`. A model that averaged the conductivities would be a different
//                problem under the same name, so none is built.
//   RM-THERM-04  a box with uniform generation, both caps held and the sides insulated (an exact 1D parabola, which the 9-node mesh cannot\n//                represent, so the box is judged on its RESULTANT q'''V and its conservation), and a thick tube with radial\n//                generation: a smooth non-affine field on real P16 meshes, judged against an independent FE solve
//   RM-THERM-05  a slab held hot on one cap and convecting on the other: a resistance network whose heat rate,
//                surface temperature and balance are closed forms
//   RM-THERM-06  a block and the same block rigidly transformed: scalars invariant, vectors rotated by R
//   RM-THERM-07  a baseline, a target-refined and a mirror-refined beam under generation, so that "the target
//                band is finer" and "the rest was left alone" are both measured in the VOLUME
//   RM-THERM-08  a lumped body that satisfies Bi << 0.1 on purpose, cooling by convection: T(t) is an
//                exponential; and an insulated body with generation: T(t) is a line
//   RM-THERM-09  a steady conduction problem with NO temperature datum, which must be REFUSED, in two forms
//                (net heat in; and net heat zero, which is compatible and still undetermined)
//   RM-THERM-10  a stale mesh, and a target that resolves to nothing, three ways (a load, a held temperature, a
//                convection)
//
// EVERY MODEL CARRIES ITS WHOLE INTENT: a material assigned at document level (as P15 assigns one), a
// `MeshControl` named "Mesh" and ONE `ThermalAnalysis` named "Study". The refusals included -- their committed
// state IS the refusal, and a model without an analysis would be refused for the wrong reason.
//
// SI THROUGHOUT. The CAD boundary is millimetres (the sketch and the extrusion depth); every analytical
// quantity below is in metres, kelvin, watts and seconds, and the one conversion is made once, in the test
// support, and is itself pinned (a 1000x error in length is the commonest way to make a smooth wrong field).
namespace bettercad::reference {

/// The one analysis every thermal model holds, by name.
inline constexpr std::string_view kThermalStudyName = "Study";

/// The material every thermal model is made of is SYNTHETIC. The numbers are round and chosen so that the
/// closed forms are exact arithmetic, and each model declares its own.
struct ThermalMaterialSpec {
    double conductivity;     ///< W/(m K)
    double density;          ///< kg/m^3
    double specificHeat;     ///< J/(kg K)
};

// --- RM-THERM-01 -----------------------------------------------------------

/// The conductivity of the patch block, W/(m K). Distinct from every other, so a conductivity borrowed from a
/// neighbouring model cannot pass.
inline constexpr ThermalMaterialSpec kPatchMaterial{5.0, 7800.0, 500.0};

/// The entity RM-THERM-10's unresolvable targets name: well formed, and swept by nothing. 9991 is the value
/// P17-CLI-001's and P18-CLI-001's fixtures use for the same case.
inline constexpr EntityId kThermalUnresolvableEntity = EntityId::fromValue(9991U);

/// The model every document below is a variant of: a rectangular block of `a` x `b` mm extruded `depth` mm along
/// +Z, with its two caps and four sides named by the entity that swept them.
///
/// ONE STRUCT FOR ALL OF THEM, because what differs between the models is the intent (sizes, material, loads,
/// conditions, sizing) and not the shape of what has to be remembered about them.
struct ThermalBlockModel {
    Document document;
    ParameterId a{}, b{}, depth{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    /// The profile's four lines, in order: y = 0, x = a, y = b, x = 0.
    std::array<EntityId, 4> lines{};
    /// Whether this document is the rigidly transformed one.
    bool placed = false;

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    /// The cap at z = 0 (the sketch plane).
    [[nodiscard]] FaceName start() const { return startCapFace(solid); }
    /// The cap at z = depth.
    [[nodiscard]] FaceName end() const { return endCapFace(solid); }
    /// A face no entity sweeps: well formed, and resolving to nothing.
    [[nodiscard]] FaceName missing() const { return sweptFace(solid, kThermalUnresolvableEntity); }
};

/// RM-THERM-01. A 40 x 30 x 20 mm block at a 20 mm global target: measured 9 nodes and 12 tetrahedra, the 8
/// corners and ONE INTERIOR node. The interior node is what makes the patch test real: with none, every node is
/// a boundary node and the check that an interior node lands on the plane would be vacuous.
///
/// The committed analysis (400 K on the start cap, 300 K on the end cap) is there so the document solves through
/// the runner and the CLI; the affine patch itself is imposed in process, on the nodes, which no canonical
/// boundary condition can express.
[[nodiscard]] Result<ThermalBlockModel> buildThermalPatchReferenceModel();

// --- RM-THERM-02 -----------------------------------------------------------

/// RM-THERM-02's physics, SI. A slab 1 m thick with a 0.5 m x 0.5 m section: A = 0.25 m^2, L = 1 m, so
///
///     Qdot = k A (Th - Tc) / L = 8 x 0.25 x 100 / 1 = 200 W        q = 800 W/m^2        dT/dz = -100 K/m
///
/// Three different numbers where a symmetric choice would hide an exchanged pair: k = 8, A = 0.25, L = 1.
inline constexpr ThermalMaterialSpec kSlabMaterial{8.0, 7800.0, 500.0};
inline constexpr double kSlabHotKelvin = 400.0;
inline constexpr double kSlabColdKelvin = 300.0;

/// RM-THERM-02. The slab above, 500 x 500 mm section, 1000 mm along Z; the start cap held at 400 K and the end
/// cap at 300 K, the four sides insulated (natural). @p sideTarget, when given, puts a local size on each of
/// the four SIDE faces: the three mesh levels of the suite are this model at no size, and at two sizes.
[[nodiscard]] Result<ThermalBlockModel> buildThermalSlabReferenceModel(std::optional<Length> sideTarget = std::nullopt);

// --- RM-THERM-03 -----------------------------------------------------------

/// WHY RM-THERM-03 HAS NO DOCUMENT. P18 v1 cannot represent a layered wall, for two independent reasons, both
/// of which the suite REFUSES rather than works around:
///
///   * the material is the DOCUMENT's (`features::assignMaterial` takes no body, `ThermalMaterial.hpp` says "no
///     per-body assignment"), so one solid has one conductivity;
///   * P16 meshes one solid and refuses a body of more than one (ADR-049: "a composite wall of two materials is
///     therefore NOT a two-body model"), so two bodies cannot stand in for two layers.
///
/// A single material with a conductivity averaged over the layers would give the right HEAT RATE for a series
/// wall and the wrong everything else (no interface, no piecewise gradient, no flux jump in the field), and it
/// would be called a composite wall by anyone reading the name. The suite therefore carries the closed form
/// (ThermalAnalytic.hpp, tested against hand arithmetic), tests that both refusals hold, and records the limitation as
/// a capability the architecture does not have.
inline constexpr std::string_view kCompositeWallLimitation =
    "P18 v1 has one material per document and meshes one solid per body (ADR-049): a layered wall is not representable";

// --- RM-THERM-04 -----------------------------------------------------------

/// RM-THERM-04's physics, SI: a slab of thickness L = 2a = 0.16 m (a = 0.08 m), both faces held at Ts = 300 K,
/// uniform generation q''' = 2e5 W/m^3, k = 10 W/(m K). With x measured from the MIDPLANE,
///
///     T(x) = Ts + q'''/(2k) (a^2 - x^2)        Tmax = Ts + q''' a^2 / 2k = 300 + 64 = 364 K
///
/// and the generated heat Qgen = q''' V = 2e5 x 1.024e-3 = 204.8 W leaves through the two held faces.
inline constexpr ThermalMaterialSpec kGenerationMaterial{10.0, 7800.0, 500.0};
inline constexpr double kGenerationWattsPerM3 = 2.0e5;
inline constexpr double kGenerationFaceKelvin = 300.0;

/// RM-THERM-04. 80 x 80 mm section, 160 mm along Z, both caps held at 300 K, generation q''' on the body.
///
/// A SLAB, KEPT FOR WHAT IT CAN SHOW EXACTLY AND NOT FOR CONVERGENCE. Its mesh is the coarse one P16 gives a box
/// (measured: 9 nodes, 12 tetrahedra, one interior node), because a planar face has two triangles whatever the
/// target and a local size refines only a skin one target deep. So the generated heat q'''V and the heat removed
/// by the held caps are exact on it (conservation does not need a resolved field), and the FIELD is not -- which
/// is a measurement of P16, recorded in AUDIT.md, and the reason the convergent non-affine field is the tube.
/// @p faceTarget, when given, puts a local size on every face (the measurement that showed it).
[[nodiscard]] Result<ThermalBlockModel> buildThermalGenerationReferenceModel(std::optional<Length> faceTarget = std::nullopt);

/// RM-THERM-04's second field: a thick tube, inner radius 20 mm, outer 40 mm, 6 mm long, the OUTER wall held at
/// 300 K, the inner wall and the two annular caps insulated, uniform generation q''' = 3e6 W/m^3, k = 10 W/(m K).
/// The flow is radial, so
///
///     T(r) = Ts + q'''/(4k) (ro^2 - r^2) - q''' ri^2/(2k) ln(ro/r)       dT/dr = q'''/(2k r) (ri^2 - r^2)
///     Qgen = q''' pi (ro^2 - ri^2) L        all of it through the outer wall; the inner wall is hottest (348.41 K)
///
/// A SMOOTH, NON-AFFINE FIELD ON A REAL P16 MESH -- AND WHAT P16 CAN AND CANNOT DO WITH IT, MEASURED. The suite
/// tried to make this field a convergence study and could not, on this body or any other P16 can mesh:
///
///   * a planar face has two triangles whatever the target, and a local size refines a skin ONE target deep, so a
///     box with sizes on all six faces has no node on its axis (peak 301 K against an exact 306.4 K);
///   * a long rod's wall triangles span its length, and its RMS error (10 to 13 K) does not fall with the mesh;
///   * this tube, over deflections from 0.5 to 0.02 mm and wall sizes from 8 to 2 mm (160 to 4200 nodes), has its
///     hottest node between 351.6 and 358.2 K and an RMS error of 1.9 to 3.9 K, neither moving with the mesh: its
///     interior nodes sit 4 to 5 mm from each wall whatever the control, so the first element beside each wall is
///     a sliver across the steepest part of the field.
///
/// The solution on every one of those meshes equals an INDEPENDENT dense finite-element solve of the same mesh to
/// 1e-11 K, so the production chain is right and the bias is the mesh's. The suite therefore (a) holds production to
/// the independent solve on the real P16 meshes, (b) bounds the error against the closed form and records it, and
/// (c) demonstrates convergence of the P18 stack on structured tetrahedral meshes whose refinement the suite
/// controls (a cube with generation and all faces held, against its Fourier series). See AUDIT.md.
inline constexpr ThermalMaterialSpec kTubeMaterial{10.0, 7800.0, 500.0};
inline constexpr double kTubeInnerRadiusMm = 20.0;
inline constexpr double kTubeOuterRadiusMm = 40.0;
inline constexpr double kTubeLengthMm = 6.0;
inline constexpr double kTubeGenerationWattsPerM3 = 3.0e6;
inline constexpr double kTubeOuterKelvin = 300.0;

struct ThermalTubeModel {
    Document document;
    ParameterId outerRadius{}, innerRadius{}, length{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    EntityId outerRim{}, innerRim{};

    [[nodiscard]] FaceName outerWall() const { return sweptFace(solid, outerRim); }
    [[nodiscard]] FaceName innerWall() const { return sweptFace(solid, innerRim); }
    [[nodiscard]] FaceName start() const { return startCapFace(solid); }
    [[nodiscard]] FaceName end() const { return endCapFace(solid); }
};

/// RM-THERM-04. The tube above. The mesh level is the surface deflection and the global target; the committed
/// intent is the COARSEST level the suite uses, and the suite refines through the document's own `MeshControl`.
[[nodiscard]] Result<ThermalTubeModel> buildThermalTubeReferenceModel(Length linearDeflection = Length::fromSi(0.2e-3),
                                                                       Length globalTarget = Length::fromSi(10.0e-3),
                                                                       std::optional<Length> outerWallTarget = std::nullopt);

// --- RM-THERM-05 -----------------------------------------------------------

/// RM-THERM-05's physics, SI. A slab of L = 0.5 m and A = 0.2 m^2, k = 10, held at 400 K on its start cap and
/// convecting (h = 20 W/(m^2 K), Tinf = 300 K) on its end cap:
///
///     Rcond = L/(kA) = 0.25 K/W      Rconv = 1/(hA) = 0.25 K/W      Qdot = 100/0.5 = 200 W
///     Ts = Tinf + Qdot Rconv = 350 K = 400 - Qdot Rcond
inline constexpr ThermalMaterialSpec kConvectionMaterial{10.0, 7800.0, 500.0};
inline constexpr double kConvectionHotKelvin = 400.0;
inline constexpr double kConvectionAmbientKelvin = 300.0;
inline constexpr double kConvectionCoefficient = 20.0;

/// RM-THERM-05. 400 x 500 mm section (A = 0.2 m^2), 500 mm along Z.
[[nodiscard]] Result<ThermalBlockModel> buildThermalConvectionReferenceModel();

// --- RM-THERM-06 -----------------------------------------------------------

/// RM-THERM-06's transform is RM-MESH-06's exactly (the thirds triad and its origin), for the reason RM-FEA-06
/// recorded: an exact invariance claim needs the two meshes to be rigid images of each other INCLUDING
/// CONNECTIVITY, and whether OCCT picks the same diagonals in a rotated frame is a property of the block and the
/// frame. This block on this frame was measured to give the same tetrahedralisation, and the suite asserts it.
[[nodiscard]] constexpr RigidPlacement thermalTransformedPlacement() noexcept { return meshTransformedPlacement(); }

/// The sketch plane RM-THERM-06's placed model is built on.
[[nodiscard]] Result<Frame3D> thermalTransformedFrame();

inline constexpr ThermalMaterialSpec kTransformedMaterial{15.0, 7800.0, 500.0};

/// RM-THERM-06. A 90 x 55 x 24 mm block, 400 K on the datum cap and 300 K on the opposite cap, at a 16 mm target,
/// built at the origin (`placed == false`) or on `thermalTransformedFrame()` (`placed == true`).
[[nodiscard]] Result<ThermalBlockModel> buildThermalTransformedBaseReferenceModel();
[[nodiscard]] Result<ThermalBlockModel> buildThermalTransformedPlacedReferenceModel();

// --- RM-THERM-07 -----------------------------------------------------------

/// RM-THERM-07's physics, SI: a 100 mm beam with both caps held at 300 K under uniform generation
/// q''' = 1e5 W/m^3, k = 2 W/(m K). The same parabola as RM-THERM-04 along Z (L = 0.1 m):
///
///     T(z) = Ts + q'''/(2k) z (L - z)      dT/dz = q'''/(2k) (L - 2z)      Tmax = Ts + q''' L^2/8k = 362.5 K
///
/// whose gradient is LARGEST at the two held caps (2500 K/m), where a local refinement is meant to help.
inline constexpr ThermalMaterialSpec kLocalMaterial{2.0, 7800.0, 500.0};
inline constexpr double kLocalGenerationWattsPerM3 = 1.0e5;
inline constexpr double kLocalCapKelvin = 300.0;
/// The size the local control asks for, and the depth of the volume band the suite measures.
inline constexpr double kLocalSizeMm = 5.0;

/// Which cap carries RM-THERM-07's local control, if any.
enum class ThermalLocalRefinement { None, Start, End };

/// RM-THERM-07. 40 x 60 x 100 mm (the section RM-FEA-07 measured as the smallest in which a 5 mm control takes
/// effect), both caps held, generation on the body, a 5 mm local control on the cap @p refined names.
[[nodiscard]] Result<ThermalBlockModel> buildThermalLocalRefinementReferenceModel(ThermalLocalRefinement refined);

// --- RM-THERM-08 -----------------------------------------------------------

/// RM-THERM-08's physics, SI. A 40 mm cube of a synthetic copper-like solid, convecting from ALL SIX faces:
///
///     V = 6.4e-5 m^3   A = 9.6e-3 m^2   Lc = V/A = 6.667 mm   k = 400   h = 25   Bi = h Lc/k = 4.2e-4
///     rho cp V = 8960 x 385 x 6.4e-5 = 220.7744 J/K          tau = rho cp V/(hA) = 919.893 s
///
/// Bi is more than two orders below the 0.1 that licenses the lumped model, and the suite asserts it from the
/// DECLARED numbers rather than assuming it.
inline constexpr ThermalMaterialSpec kLumpedMaterial{400.0, 8960.0, 385.0};
inline constexpr double kLumpedCoefficient = 25.0;
inline constexpr double kLumpedInitialKelvin = 400.0;
inline constexpr double kLumpedAmbientKelvin = 300.0;
inline constexpr double kLumpedSideMm = 40.0;
inline constexpr double kLumpedEndSeconds = 4600.0;

/// RM-THERM-08. The cube above, a transient analysis from 400 K in a 300 K ambient, 100 s steps to 4600 s. The
/// time step is an argument so the refinement study can halve it; the committed value is 100 s.
[[nodiscard]] Result<ThermalBlockModel> buildThermalLumpedReferenceModel(double timeStepSeconds = 100.0,
                                                                          double endSeconds = kLumpedEndSeconds);

/// The insulated variant: the same cube, no boundary condition at all, uniform generation 1e6 W/m^3 from 300 K.
/// Backward Euler with a constant source and a uniform field is EXACT: T(t) = T0 + q''' t / (rho cp).
inline constexpr double kInsulatedGenerationWattsPerM3 = 1.0e6;
inline constexpr double kInsulatedInitialKelvin = 300.0;
[[nodiscard]] Result<ThermalBlockModel> buildThermalInsulatedGenerationReferenceModel(double timeStepSeconds = 0.3,
                                                                                       double endSeconds = 1.0);

// --- RM-THERM-02 (surface heat flux) ------------------------------------------

/// A steady 1D slab driven by a SURFACE HEAT FLUX: 50 x 50 x 100 mm, k = 5 W/(m K), the start cap held at 300 K and
/// +500 W/m^2 INTO the body through the end cap. The heat leaves through the held cap: Qdot = q'' A = 500 x 0.0025 =
/// 1.25 W, dT/dz = +q''/k = +100 K/m, T(z) = 300 + 100 z, so the end cap sits at 310 K. Without this document no
/// accepted model exercised a surface load at all.
inline constexpr double kSurfaceFluxWattsPerM2 = 500.0;
inline constexpr double kSurfaceFluxHeldKelvin = 300.0;
[[nodiscard]] Result<ThermalBlockModel> buildThermalSurfaceFluxReferenceModel();

// --- RM-THERM-08 (held cap, two convections, non-uniform field) ----------------

/// A transient slab whose solution is NOT uniform and whose boundary conditions differ: 40 x 40 x 100 mm, k = 50,
/// rho = 7800, cp = 500; the start cap HELD at 400 K, the end cap convecting (h 100, ambient 300 K), ONE side face
/// convecting with a DIFFERENT h and ambient (h 10, 350 K); initial 300 K; backward Euler, 50 s steps to 500 s. It
/// exercises the consistent capacity matrix on a field that varies, a prescribed temperature in a transient run, and
/// two convection conditions that must not be confused. The oracle is an independent dense backward-Euler integrator.
inline constexpr ThermalMaterialSpec kTransientSlabMaterial{50.0, 7800.0, 500.0};
inline constexpr double kTransientSlabHeldKelvin = 400.0;
inline constexpr double kTransientSlabEndCoefficient = 100.0;
inline constexpr double kTransientSlabEndAmbientKelvin = 300.0;
inline constexpr double kTransientSlabSideCoefficient = 10.0;
inline constexpr double kTransientSlabSideAmbientKelvin = 350.0;
inline constexpr double kTransientSlabInitialKelvin = 300.0;
inline constexpr double kTransientSlabStepSeconds = 50.0;
inline constexpr double kTransientSlabEndSeconds = 500.0;
[[nodiscard]] Result<ThermalBlockModel> buildThermalTransientSlabReferenceModel();
// --- RM-THERM-09 -----------------------------------------------------------

/// RM-THERM-09. A steady conduction problem on a 60 x 40 x 25 mm block with NOTHING that fixes the temperature:
/// uniform generation (heat in, nowhere for it to go) and insulated everywhere.
[[nodiscard]] Result<ThermalBlockModel> buildThermalSingularReferenceModel();

/// RM-THERM-09's compatible variant: +1000 W/m^2 in through the start cap and -1000 W/m^2 out through the end
/// cap, so the net heat is exactly zero and the equations are CONSISTENT -- and the temperature is still
/// undetermined up to a constant. A solver that returns one member of that family (a pseudo-inverse, a pinned
/// node, a tiny diagonal) is wrong in a way no residual shows.
[[nodiscard]] Result<ThermalBlockModel> buildThermalSingularBalancedReferenceModel();

// --- RM-THERM-10 -----------------------------------------------------------

/// RM-THERM-10. A 50 x 40 x 30 mm block, valid and solvable as committed, whose depth parameter `stale_l` the
/// suite edits AFTER meshing.
[[nodiscard]] Result<ThermalBlockModel> buildThermalStaleMeshReferenceModel();

/// RM-THERM-10. The same block with a VALID held temperature and a VALID convection, plus ONE more condition on
/// a face that does not exist -- a load, a held temperature, or a convection. Three documents, because the
/// three go through different preparation stages and each must refuse on its own.
enum class ThermalUnresolvedTarget { Load, Temperature, Convection };
[[nodiscard]] Result<ThermalBlockModel> buildThermalUnresolvedReferenceModel(ThermalUnresolvedTarget target);

/// The models of the thermal suite: TWENTY DOCUMENTS FOR NINE EXECUTABLE MODEL IDS (RM-THERM-03 has none by
/// design). RM-THERM-06 is a pair, RM-THERM-07 a triple, RM-THERM-08 and RM-THERM-09 are pairs, RM-THERM-10 holds
/// four.
enum class ThermalReferenceModelKind {
    Patch,                  ///< RM-THERM-01
    Slab,                   ///< RM-THERM-02
    Generation,             ///< RM-THERM-04
    Tube,                   ///< RM-THERM-04
    Convection,             ///< RM-THERM-05
    TransformedBase,        ///< RM-THERM-06
    TransformedPlaced,      ///< RM-THERM-06
    LocalBaseline,          ///< RM-THERM-07
    LocalTarget,            ///< RM-THERM-07
    LocalMirror,            ///< RM-THERM-07
    Lumped,                 ///< RM-THERM-08
    InsulatedGeneration,    ///< RM-THERM-08
    SurfaceFlux,            ///< RM-THERM-02
    TransientSlab,          ///< RM-THERM-08
    Singular,               ///< RM-THERM-09
    SingularBalanced,       ///< RM-THERM-09
    StaleMesh,              ///< RM-THERM-10
    UnresolvedLoad,         ///< RM-THERM-10
    UnresolvedTemperature,  ///< RM-THERM-10
    UnresolvedConvection,   ///< RM-THERM-10
};

struct ThermalReferenceModelInfo {
    ThermalReferenceModelKind kind;
    /// The reference ID, e.g. "RM-THERM-01".
    std::string_view id;
    /// Document name, e.g. "ThermalPatch".
    std::string_view name;
    /// File name stem, e.g. "thermal_patch" for thermal_patch.bcad.
    std::string_view fileStem;
    /// What this model exists to prove, one line, for the runner's output.
    std::string_view purpose;
    /// Whether the committed analysis is expected to be ACCEPTED by P18-VALID. False for the refusals, whose
    /// expected outcome is an explicit refusal with nothing published.
    bool expectAccepted;
    /// A main dimension, and a value to try it at (mm): what the model-change gate changes to make the held mesh
    /// stale and the model regenerate, remesh and re-solve.
    std::string_view mainParameter;
    double mainParameterMm;
};

inline constexpr std::array kThermalReferenceModels{
    ThermalReferenceModelInfo{ThermalReferenceModelKind::Patch, "RM-THERM-01", "ThermalPatch", "thermal_patch",
                              "affine patch test on a mesh with an interior node", true, "patch_a", 48.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::Slab, "RM-THERM-02", "ThermalSlab", "thermal_slab",
                              "1D slab conduction: T linear, q = k dT/L, Qdot = k A dT/L", true, "slab_l", 1200.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::Generation, "RM-THERM-04", "ThermalGeneration",
                              "thermal_generation", "uniform generation between two held faces: a parabola", true,
                              "gen_l", 200.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::Tube, "RM-THERM-04", "ThermalTube", "thermal_tube",
                              "a thick tube with radial generation and a held outer wall: a smooth non-affine field on a real mesh", true,
                              "tube_l", 24.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::Convection, "RM-THERM-05", "ThermalConvection",
                              "thermal_convection", "conduction in series with convection: a resistance network",
                              true, "conv_l", 600.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::TransformedBase, "RM-THERM-06", "ThermalTransformedBase",
                              "thermal_transformed_base", "the base block, at the origin on the XY plane", true,
                              "tbase_a", 110.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::TransformedPlaced, "RM-THERM-06",
                              "ThermalTransformedPlaced", "thermal_transformed_placed",
                              "the same block under a compound rotation and a translation", true, "tplaced_a",
                              110.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::LocalBaseline, "RM-THERM-07", "ThermalLocalBaseline",
                              "thermal_local_baseline", "the beam with no local control", true, "locb_l", 120.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::LocalTarget, "RM-THERM-07", "ThermalLocalTarget",
                              "thermal_local_target", "the beam with a 5 mm local control on the start cap", true,
                              "loct_l", 120.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::LocalMirror, "RM-THERM-07", "ThermalLocalMirror",
                              "thermal_local_mirror", "the mirror: the same control on the end cap instead", true,
                              "locm_l", 120.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::Lumped, "RM-THERM-08", "ThermalLumped", "thermal_lumped",
                              "a lumped body cooling by convection, Bi << 0.1: T(t) is an exponential", true,
                              "lump_a", 44.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::InsulatedGeneration, "RM-THERM-08",
                              "ThermalInsulatedGeneration", "thermal_insulated_generation",
                              "an insulated body with generation: T(t) is a line, exactly", true, "ins_a", 44.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::SurfaceFlux, "RM-THERM-02", "ThermalSurfaceFlux",
                              "thermal_surface_flux", "a surface heat flux into a slab with one held cap: T(z) = 300 + 100 z", true,
                              "sf_l", 120.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::TransientSlab, "RM-THERM-08", "ThermalTransientSlab",
                              "thermal_transient_slab",
                              "a held cap, two different convections and a non-uniform field: an independent backward-Euler integration",
                              true, "ts_l", 120.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::Singular, "RM-THERM-09", "ThermalSingular",
                              "thermal_singular", "steady conduction with no temperature datum: refused", false,
                              "sing_a", 72.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::SingularBalanced, "RM-THERM-09",
                              "ThermalSingularBalanced", "thermal_singular_balanced",
                              "net heat zero, equations consistent, temperature still undetermined: refused",
                              false, "singb_a", 72.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::StaleMesh, "RM-THERM-10", "ThermalStaleMesh",
                              "thermal_stale_mesh", "a geometry edit after meshing: the held mesh is stale", true,
                              "stale_l", 36.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::UnresolvedLoad, "RM-THERM-10", "ThermalUnresolvedLoad",
                              "thermal_unresolved_load", "a heat flux on a face that does not exist: refused",
                              false, "unl_a", 60.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::UnresolvedTemperature, "RM-THERM-10",
                              "ThermalUnresolvedTemperature", "thermal_unresolved_temperature",
                              "a held temperature on a face that does not exist: refused", false, "unt_a", 60.0},
    ThermalReferenceModelInfo{ThermalReferenceModelKind::UnresolvedConvection, "RM-THERM-10",
                              "ThermalUnresolvedConvection", "thermal_unresolved_convection",
                              "a convection on a face that does not exist: refused", false, "unc_a", 60.0},
};

/// Builds the document of @p kind.
[[nodiscard]] Result<Document> buildThermalReferenceModel(ThermalReferenceModelKind kind);

} // namespace bettercad::reference
