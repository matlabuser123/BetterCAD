#pragma once

#include "MeshReferenceModels.hpp"

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Frame.hpp>

#include <array>
#include <cstddef>
#include <string_view>

// BetterCAD's structural reference models (P17-REFMOD-001): a committed
// qualification suite for the whole P17 stack, built only through the public
// document, parameter, sketch, feature, material, meshing and structural APIs.
//
// A sixth suite beside the parts (P11/P12), the assemblies (P13), the drawings
// (P14), the engineering data (P15) and the meshes (P16), following the same
// shape as those five: a struct of IDs per model, a builder per model, one
// catalog, and one loop in the runner.
//
// THESE ARE NOT DEMO MODELS. Each one exists to make a specific defect
// impossible to hide, and each carries its own CLOSED-FORM expectation so that
// the answer never comes from the code under test:
//
//   RM-FEA-01  a block whose mesh has an INTERIOR node, so an affine field
//              imposed on every node is a real patch test: constant strain,
//              constant stress, and K u equal to the boundary tractions with
//              nothing left over at the interior node
//   RM-FEA-02  an axial bar under a uniform end traction with ROLLER supports
//              -- Uz on the base, Ux on one side, Uy on another -- so the
//              uniaxial state u = (-nu e x, -nu e y, e z) is AFFINE and a
//              linear Tet4 must reproduce delta = F L / (A E), sigma = F / A
//              and R = -F to roundoff, not to a tolerance
//   RM-FEA-03  a circular cantilever, refined three times, whose tip
//              deflection must approach beam theory and whose root reaction
//              must balance the tip load exactly at every level
//   RM-FEA-04  a block under a uniform pressure: F = p A through the nodal
//              load conversion, R = -F through the reactions, and a mean
//              axial stress of -p that discrete equilibrium gives EXACTLY
//   RM-FEA-05  the same under self-weight: W = rho V g, the moment of the
//              weight at the centre of volume, and a mean axial stress of
//              -rho g H / 2 that is likewise exact
//   RM-FEA-06  a block and the same block rigidly transformed, under a
//              PRESSURE -- which rotates with the face -- so the scalar
//              invariants must not move and the vectors must move by R
//   RM-FEA-07  a rectangular cantilever with a local control at the ROOT, and
//              the mirror model refining the TIP instead, because a refined
//              face's own facets cannot show refinement on a planar body
//   RM-FEA-08  eight constrained degrees of freedom that leave three rigid
//              modes: enough to pass the count, and the factorisation's pivot
//              gate must refuse it rather than publish a huge displacement
//   RM-FEA-09  a block whose depth the suite edits after meshing, so a held
//              mesh is stale and the solve must be refused BEFORE assembly
//   RM-FEA-10  a load naming a face that does not exist, which must be
//              refused and never rebound to the face next door
//
// EVERY MODEL CARRIES ITS WHOLE INTENT: a material (synthetic steel, assigned
// at document level as P15 assigns one), a `MeshControl`, and ONE
// `StructuralAnalysis` named "Study". RM-FEA-08 and RM-FEA-10 included -- their
// committed state IS the refusal, and a model without an analysis would be
// refused for the wrong reason.
namespace bettercad::reference {

/// The synthetic steel every structural model is made of.
///
/// TEST / SYNTHETIC ENGINEERING DATA, as the P15 suite's materials say of
/// themselves: round, physically plausible values chosen so that closed-form
/// arithmetic is exact. Declared here so the tests can compare the material
/// the document RESOLVES against what the model claims to be made of.
inline constexpr double kStructuralYoungsModulusPa = 210.0e9;
inline constexpr double kStructuralPoissonRatio = 0.3;
inline constexpr double kStructuralDensityKgM3 = 7850.0;

/// The one analysis every model holds, by name.
inline constexpr std::string_view kStructuralStudyName = "Study";

/// RM-FEA-01. A 40 x 30 x 20 mm block at a 20 mm global target.
///
/// THE SIZE IS CHOSEN FOR ITS MESH. Measured, this block meshes to 9 nodes and
/// 12 tetrahedra: the 8 corners and ONE INTERIOR node. A block with no
/// interior node would make the patch test's force check vacuous -- every node
/// would be a boundary node and K u would be boundary tractions everywhere by
/// construction. The suite asserts the interior node is there before it
/// claims anything.
///
/// The committed analysis -- fixed at z = 0, a uniform traction on the end
/// cap -- is there so the document solves through the runner and the CLI; the
/// patch test itself imposes its affine field in process.
struct StructuralPatchModel {
    Document document;
    ParameterId a{}, b{}, c{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName bottom() const { return startCapFace(solid); }
    [[nodiscard]] FaceName top() const { return endCapFace(solid); }
};

[[nodiscard]] Result<StructuralPatchModel> buildStructuralPatchReferenceModel();

/// The end traction RM-FEA-02 applies, in Pa along +Z: 50 MPa of tension.
inline constexpr double kAxialTractionPa = 50.0e6;

/// RM-FEA-02. A 24 x 18 mm bar, 90 mm long along Z, in uniform tension.
///
/// THE SUPPORTS ARE ROLLERS, NOT A FIXED END, AND THAT IS WHAT MAKES THE
/// ORACLE EXACT. A fully fixed base forbids the Poisson contraction, so the
/// stress near it is not uniaxial and no closed form describes the coarse
/// mesh. With Uz held on the base, Ux on the x = 0 face and Uy on the y = 0
/// face, the exact solution is
///
///     u = (-nu eps x,  -nu eps y,  eps z)      eps = sigma / E
///
/// which is AFFINE -- so the six rigid modes are removed, the problem is
/// well posed, and a linear tetrahedron reproduces it to roundoff. Every
/// closed form in TODO.md's RM-FEA-02 then holds to 1e-9 rather than to a
/// tuned band: delta = F L / (A E) at every tip node, sigma = F / A in every
/// element, and the base reaction -F.
struct StructuralAxialBarModel {
    Document document;
    ParameterId a{}, b{}, length{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    /// The profile's four lines, in order: y = 0, x = a, y = b, x = 0.
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName base() const { return startCapFace(solid); }
    [[nodiscard]] FaceName tip() const { return endCapFace(solid); }
    /// The face held in Uy: the profile line at y = 0.
    [[nodiscard]] FaceName planeY0() const { return sweptFace(solid, lines.at(0)); }
    /// The face held in Ux: the profile line at x = 0.
    [[nodiscard]] FaceName planeX0() const { return sweptFace(solid, lines.at(3)); }
    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
};

[[nodiscard]] Result<StructuralAxialBarModel> buildStructuralAxialBarReferenceModel();

/// The tip load RM-FEA-03 applies, in N along -Y, as a total over the cap.
inline constexpr double kCantileverTipForceN = 200.0;

/// RM-FEA-03. A circular cantilever, radius 10 mm and length 100 mm along Z,
/// fixed at z = 0 and loaded by a uniform transverse traction on the tip.
///
/// A CYLINDER AND NOT A BOX, for the reason P16-REFMOD-001 and
/// P17-ASSEMBLY-001 both recorded: a planar face has two boundary triangles
/// whatever the deflection, so a box cannot be refined through its own
/// intent and a convergence study on one measures nothing. A curved wall
/// responds to both the deflection and the global target, so three sizings
/// give three genuinely different meshes -- which the suite asserts before it
/// compares anything.
///
/// The committed intent is the COARSEST level; the suite refines through the
/// document's own `MeshControl`, exactly as a user would.
struct StructuralCantileverModel {
    Document document;
    ParameterId radius{}, length{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    /// The profile circle, which sweeps the lateral wall.
    EntityId rim{};

    [[nodiscard]] FaceName wall() const { return sweptFace(solid, rim); }
    [[nodiscard]] FaceName root() const { return startCapFace(solid); }
    [[nodiscard]] FaceName tip() const { return endCapFace(solid); }
};

[[nodiscard]] Result<StructuralCantileverModel> buildStructuralCantileverReferenceModel();

/// The pressure RM-FEA-04 applies, in Pa: 2 MPa inward on the top face.
inline constexpr double kBlockPressurePa = 2.0e6;

/// RM-FEA-04. A 60 x 45 x 30 mm block, fixed at z = 0, under a uniform
/// pressure on its top face.
///
/// Three different edge lengths, so a transposed area cannot pass. The
/// expectation is F = p A from the DECLARED dimensions, never from the facet
/// sum, and the reactions must supply exactly -F.
struct StructuralPressureBlockModel {
    Document document;
    ParameterId a{}, b{}, c{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName bottom() const { return startCapFace(solid); }
    [[nodiscard]] FaceName top() const { return endCapFace(solid); }
};

[[nodiscard]] Result<StructuralPressureBlockModel> buildStructuralPressureBlockReferenceModel();

/// RM-FEA-05. A 50 x 35 x 80 mm column, fixed at z = 0, under its own weight.
///
/// GRAVITY IS IN SCOPE -- P17-LOAD-001 integrates it and P17-REACTION-001
/// balanced it -- so this model is implemented rather than deferred. TALLER
/// THAN IT IS WIDE, so the weight's moment about the origin has two distinct
/// non-zero components and the centre-of-volume check is not degenerate.
///
/// The analysis mode is `LinearStaticWithGravity`, which is what makes the
/// material's density a requirement rather than an unused number.
struct StructuralGravityBlockModel {
    Document document;
    ParameterId a{}, b{}, height{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName bottom() const { return startCapFace(solid); }
    [[nodiscard]] FaceName top() const { return endCapFace(solid); }
};

[[nodiscard]] Result<StructuralGravityBlockModel> buildStructuralGravityBlockReferenceModel();

/// The pressure RM-FEA-06 applies to the face opposite its datum, in Pa.
inline constexpr double kTransformedPressurePa = 20.0e6;

/// RM-FEA-06's rigid transform: RM-MESH-06's exactly -- the orthonormal
/// thirds triad, written out so the doubles are exact on every platform, and
/// the same origin.
///
/// THE SAME PLACEMENT ON PURPOSE, AND THE REASON IS MEASURED RATHER THAN
/// STYLISTIC. An exact invariance claim needs the two meshes to be rigid
/// images of each other INCLUDING CONNECTIVITY, and whether OCCT picks the
/// same diagonal on every face in a rotated frame is a property of the block
/// and the frame, not a promise. An 80 x 50 x 30 block on a different origin
/// gave two DIFFERENT tetrahedralisations of the same eight nodes -- and a
/// 0.4% energy difference that no invariance tolerance could honestly
/// absorb. This block, on this frame, was measured by P17-POST-001 to give
/// bit-identical invariants; the suite asserts the correspondence rather than
/// assuming it.
[[nodiscard]] constexpr RigidPlacement structuralTransformedPlacement() noexcept {
    return meshTransformedPlacement();
}

/// The sketch plane RM-FEA-06's placed model is built on.
[[nodiscard]] Result<Frame3D> structuralTransformedFrame();

/// RM-FEA-06. A 90 x 55 x 24 mm block, fixed on its datum face and under a
/// uniform PRESSURE on the opposite face, built either at the origin on the
/// XY plane (`placed == false`) or on the rotated and translated frame
/// `structuralTransformedFrame()` (`placed == true`).
///
/// RM-MESH-06's dimensions and sizing, through this suite's own builder, for
/// the reason `structuralTransformedPlacement` gives: at a 16 mm target this
/// block meshes to 9 nodes and 12 tetrahedra in both placements, with ONE
/// interior node -- so the two node populations the brief asks to be judged
/// separately are both present.
///
/// A PRESSURE, NOT A TRACTION, AND THE CHOICE IS THE WHOLE POINT. A
/// `SurfaceTractionLoad` carries GLOBAL components, so rotating the body would
/// not rotate the load and the two documents would be two different problems.
/// A `PressureLoad` acts along the current surface normal and rotates with
/// the face, which makes "the same problem on a rotated frame" a true
/// statement that the suite can hold the solver to.
struct StructuralTransformedModel {
    Document document;
    ParameterId a{}, b{}, c{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    std::array<EntityId, 4> lines{};
    /// Whether this document is the transformed one.
    bool placed = false;

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName datumFace() const { return startCapFace(solid); }
    [[nodiscard]] FaceName oppositeFace() const { return endCapFace(solid); }
};

[[nodiscard]] Result<StructuralTransformedModel> buildStructuralTransformedBaseReferenceModel();
[[nodiscard]] Result<StructuralTransformedModel> buildStructuralTransformedPlacedReferenceModel();

/// The tip load RM-FEA-07 applies, in N along -Y, as a total over the cap.
inline constexpr double kLocalRefinementTipForceN = 300.0;

/// RM-FEA-07. A 40 x 60 mm rectangular cantilever, 100 mm long along Z, fixed
/// at z = 0 and loaded on its tip, with a 5 mm local control on ONE cap.
///
/// `root` refines the root, where the bending stress is largest; the MIRROR
/// refines the tip instead. Two documents rather than one because "the target
/// region is finer" is only half the claim -- the other half is that the rest
/// of the body was left alone -- and because a planar face's own facets cannot
/// show refinement at all: OCCT gives the face two triangles whatever the
/// control says, so the measurement is a band in the VOLUME, and the mirror
/// is what proves the band moved with the control.
///
/// THE SECTION IS 40 x 60 AND NOT SMALLER, because a local control on a cap
/// only takes effect when size-tree boxes of the requested size fit inside
/// the section: a 20 x 30 beam refined NOTHING at 4, 5 or 10 mm, and 30 x 40
/// barely. Measured, and recorded in the builder. A stubby cantilever, which
/// is fine: this model is about refinement, validity and mapping, and
/// RM-FEA-03 is the one about beam theory.
struct StructuralLocalRefinementModel {
    Document document;
    ParameterId a{}, b{}, length{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    std::array<EntityId, 4> lines{};
    /// Whether the local control is on the tip (the mirror) rather than the
    /// root.
    bool refinesTip = false;

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName root() const { return startCapFace(solid); }
    [[nodiscard]] FaceName tip() const { return endCapFace(solid); }
    /// The face the local control names.
    [[nodiscard]] FaceName refined() const { return refinesTip ? tip() : root(); }
    /// The face left to the global target.
    [[nodiscard]] FaceName coarse() const { return refinesTip ? root() : tip(); }
};

[[nodiscard]] Result<StructuralLocalRefinementModel>
buildStructuralLocalRefinementRootReferenceModel();
[[nodiscard]] Result<StructuralLocalRefinementModel>
buildStructuralLocalRefinementTipReferenceModel();

/// RM-FEA-08. A 60 x 40 x 25 mm block held in Uz ONLY, on both caps, and
/// pushed sideways by a traction on one side face.
///
/// EIGHT CONSTRAINED DEGREES OF FREEDOM, ALL IN Z. That is more than the six
/// P17-VALID-001's count requires, so validation accepts the model, and it is
/// exactly what the validation header warns cannot be sufficiency: "six
/// constrained degrees of freedom all in z remove z-translation and two
/// rotations and leave three rigid modes". The sideways load excites one of
/// them. The factorisation's pivot gate is the only thing standing between
/// this model and a huge finite displacement reported as a solution, and the
/// suite requires it to refuse.
struct StructuralUnderConstrainedModel {
    Document document;
    ParameterId a{}, b{}, c{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName bottom() const { return startCapFace(solid); }
    [[nodiscard]] FaceName top() const { return endCapFace(solid); }
    /// The face the sideways traction acts on: the profile line at x = 0.
    [[nodiscard]] FaceName pushedFace() const { return sweptFace(solid, lines.at(3)); }
};

[[nodiscard]] Result<StructuralUnderConstrainedModel>
buildStructuralUnderConstrainedReferenceModel();

/// RM-FEA-09. A 50 x 40 x 30 mm block, fixed at z = 0 under a pressure on its
/// top, whose depth parameter `stale_c` the suite edits AFTER meshing.
///
/// Its committed state is valid and solves. The model exists for what happens
/// next: a geometry edit leaves the held mesh describing a body that no longer
/// exists, and a settings edit leaves it describing an intent nobody holds.
/// Both must be refused at the INPUT stage, before any load is resolved or any
/// element assembled -- the gap P16 recorded ("nothing FORCES the holder of a
/// mesh to ask whether it is stale") and P17-VALID-001 closed.
struct StructuralStaleMeshModel {
    Document document;
    ParameterId a{}, b{}, c{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName bottom() const { return startCapFace(solid); }
    [[nodiscard]] FaceName top() const { return endCapFace(solid); }
};

[[nodiscard]] Result<StructuralStaleMeshModel> buildStructuralStaleMeshReferenceModel();

/// The entity RM-FEA-10's unresolvable load names: well formed, and swept by
/// nothing. 9991 is the value P17-CLI-001's fixture uses for the same case.
inline constexpr EntityId kUnresolvableEntity = EntityId::fromValue(9991U);

/// RM-FEA-10. A 50 x 40 x 30 mm block with a VALID fixed support and a valid
/// pressure, plus a second load on a face that does not exist.
///
/// The selector is well formed -- `FaceRole::Side` of an entity no profile
/// line has -- so the record validates on its own terms and the document
/// saves and loads. It is the MESH that cannot resolve it, and P16-MAP-001's
/// contract for that is "the reference is KEPT and reported; nothing is
/// rebound". The valid support must still resolve, so the refusal lands at
/// the LOAD stage with the restraint stage already passed, and the suite
/// checks that nothing substituted the face next door.
struct StructuralUnresolvedModel {
    Document document;
    ParameterId a{}, b{}, c{};
    ObjectId sketch{}, solid{}, control{}, analysis{};
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName bottom() const { return startCapFace(solid); }
    [[nodiscard]] FaceName top() const { return endCapFace(solid); }
    /// The face no entity sweeps.
    [[nodiscard]] FaceName missing() const { return sweptFace(solid, kUnresolvableEntity); }
};

[[nodiscard]] Result<StructuralUnresolvedModel> buildStructuralUnresolvedReferenceModel();

/// The models of the structural suite.
///
/// TWELVE ENTRIES FOR TEN MODEL IDS: RM-FEA-06 is a PAIR -- a body and the
/// same body rigidly transformed -- and RM-FEA-07 is a pair too -- a root
/// refinement and its mirror. Comparing the halves is the case in both.
enum class StructuralReferenceModelKind {
    Patch,               ///< RM-FEA-01
    AxialBar,            ///< RM-FEA-02
    Cantilever,          ///< RM-FEA-03
    PressureBlock,       ///< RM-FEA-04
    GravityBlock,        ///< RM-FEA-05
    TransformedBase,     ///< RM-FEA-06
    TransformedPlaced,   ///< RM-FEA-06
    LocalRefinementRoot, ///< RM-FEA-07
    LocalRefinementTip,  ///< RM-FEA-07
    UnderConstrained,    ///< RM-FEA-08
    StaleMesh,           ///< RM-FEA-09
    Unresolved,          ///< RM-FEA-10
};

struct StructuralReferenceModelInfo {
    StructuralReferenceModelKind kind;
    /// The reference ID, e.g. "RM-FEA-01".
    std::string_view id;
    /// Document name, e.g. "StructuralPatch".
    std::string_view name;
    /// File name stem, e.g. "structural_patch" for structural_patch.bcad.
    std::string_view fileStem;
    /// What this model exists to prove, one line, for the runner's output.
    std::string_view purpose;
    /// Whether the committed analysis is expected to SOLVE. False for
    /// RM-FEA-08 and RM-FEA-10, whose expected outcome is an explicit refusal
    /// with nothing published.
    bool expectSolve;
    /// A main dimension, and a value to try it at (mm): what the model-change
    /// gate changes to make the held mesh stale and the model regenerate,
    /// remesh and re-solve.
    std::string_view mainParameter;
    double mainParameterMm;
};

inline constexpr std::array kStructuralReferenceModels{
    StructuralReferenceModelInfo{StructuralReferenceModelKind::Patch, "RM-FEA-01", "StructuralPatch",
                                 "structural_patch",
                                 "constant-strain patch test on a mesh with an interior node", true,
                                 "patch_a", 48.0},
    StructuralReferenceModelInfo{StructuralReferenceModelKind::AxialBar, "RM-FEA-02",
                                 "StructuralAxialBar", "structural_axial_bar",
                                 "axial bar on rollers: delta = FL/AE, sigma = F/A, R = -F to roundoff",
                                 true, "bar_l", 120.0},
    // The cantilever's model-change gate SHORTENS it, 100 -> 80 mm. The gate
    // exists to prove the stale-mesh refusal and the re-solve, and a longer
    // cantilever would be a worse-conditioned bending solve whose forward
    // error reaches P17-REACTION-001's 1e-12 moment threshold -- measured at
    // 120 mm: eta_M 1.27e-12, refused. That is recorded as a finding of this
    // suite, not hidden behind a longer lever arm here.
    StructuralReferenceModelInfo{StructuralReferenceModelKind::Cantilever, "RM-FEA-03",
                                 "StructuralCantilever", "structural_cantilever",
                                 "circular cantilever: convergence toward beam theory over three meshes",
                                 true, "cant_l", 80.0},
    StructuralReferenceModelInfo{StructuralReferenceModelKind::PressureBlock, "RM-FEA-04",
                                 "StructuralPressureBlock", "structural_pressure_block",
                                 "uniform pressure: F = pA through the loads and -F through the reactions",
                                 true, "press_a", 72.0},
    StructuralReferenceModelInfo{StructuralReferenceModelKind::GravityBlock, "RM-FEA-05",
                                 "StructuralGravityBlock", "structural_gravity_block",
                                 "self-weight: W = rho V g at the centre of volume", true, "grav_h",
                                 100.0},
    StructuralReferenceModelInfo{StructuralReferenceModelKind::TransformedBase, "RM-FEA-06",
                                 "StructuralTransformedBase", "structural_transformed_base",
                                 "the base block, at the origin on the XY plane", true, "sbase_a",
                                 110.0},
    StructuralReferenceModelInfo{StructuralReferenceModelKind::TransformedPlaced, "RM-FEA-06",
                                 "StructuralTransformedPlaced", "structural_transformed_placed",
                                 "the same block under a compound rotation and a translation", true,
                                 "splaced_a", 110.0},
    StructuralReferenceModelInfo{StructuralReferenceModelKind::LocalRefinementRoot, "RM-FEA-07",
                                 "StructuralLocalRefinementRoot", "structural_local_refinement_root",
                                 "rectangular cantilever with a 5 mm local control at the root", true,
                                 "loc_l", 120.0},
    StructuralReferenceModelInfo{StructuralReferenceModelKind::LocalRefinementTip, "RM-FEA-07",
                                 "StructuralLocalRefinementTip", "structural_local_refinement_tip",
                                 "the mirror: the same cantilever refined at the tip instead", true,
                                 "loct_l", 120.0},
    StructuralReferenceModelInfo{StructuralReferenceModelKind::UnderConstrained, "RM-FEA-08",
                                 "StructuralUnderConstrained", "structural_under_constrained",
                                 "eight constrained DOFs all in z: the pivot gate must refuse", false,
                                 "uc_a", 72.0},
    StructuralReferenceModelInfo{StructuralReferenceModelKind::StaleMesh, "RM-FEA-09",
                                 "StructuralStaleMesh", "structural_stale_mesh",
                                 "a valid block whose mesh the suite makes stale after meshing", true,
                                 "stale_c", 36.0},
    StructuralReferenceModelInfo{StructuralReferenceModelKind::Unresolved, "RM-FEA-10",
                                 "StructuralUnresolved", "structural_unresolved",
                                 "a load on a face that does not exist: refused, never rebound", false,
                                 "unres_a", 60.0},
};

/// How many distinct RM-FEA IDs the suite covers. Ten, mandated; the catalog
/// has twelve entries because RM-FEA-06 and RM-FEA-07 are pairs.
inline constexpr std::size_t kStructuralReferenceModelIdCount = 10;

/// The model's document, from its builder.
[[nodiscard]] Result<Document> buildStructuralReferenceModel(StructuralReferenceModelKind kind);

} // namespace bettercad::reference
