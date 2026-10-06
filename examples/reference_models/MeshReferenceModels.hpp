#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/meshing/MeshControl.hpp>

#include <array>
#include <string_view>

// BetterCAD's meshing reference models (P16-REFMOD-001): a committed
// qualification suite for the whole P16 stack, built only through the public
// document, parameter, sketch, feature and meshing APIs.
//
// A fifth suite beside the parts (P11/P12), the assemblies (P13), the drawings
// (P14) and the engineering data (P15), following the same shape as those four:
// a struct of IDs per model, a builder per model, one catalog, and one loop in
// the runner.
//
// THESE ARE NOT DEMO MODELS. Each one exists to make a specific defect
// impossible to hide, and each carries its own CLOSED-FORM volume so that the
// expectation never comes from the code under test:
//
//   RM-MESH-01  a rectangular block with three DIFFERENT edge lengths, so an
//               axis swap or a permuted index cannot pass. V = abc exactly,
//               and a planar body's facets tile it exactly, so this is the one
//               model where mesh volume and CAD volume must agree to
//               accumulation rather than to an approximation bound
//   RM-MESH-02  a cylinder with h != 2r, so a transposed radius and height
//               cannot pass. Curved, so the tetrahedral volume approaches the
//               CAD volume FROM BELOW and the tolerance is a derived bound
//   RM-MESH-03  a plate with a through-hole: the hole must stay EMPTY, and
//               because an inscribed polygon removes LESS than the true
//               circle, the mesh volume must be slightly LARGER than the
//               analytic one -- the opposite direction from RM-MESH-02, which
//               is a check a single-sided tolerance could not make
//   RM-MESH-04  a hollow tube: the void must survive, and the inner and outer
//               walls must map to DIFFERENT CAD faces
//   RM-MESH-05  a thin plate, 80:1: deliberately hard. Either a structurally
//               valid mesh whose shape metrics are measurably worse than
//               RM-MESH-01's, or an explicit refusal. Never a quiet success
//   RM-MESH-06  an asymmetric block and the SAME block rigidly transformed by
//               a compound rotation that mixes all three axes. Volume and the
//               dimensionless quality metrics must be invariant, and the mesh
//               must actually have moved
//   RM-MESH-07  local refinement on one face, with a second face proving the
//               refinement did not reach the whole body
//   RM-MESH-08  an extrude of an UNCLOSED profile: the body fails to build, so
//               meshing must be refused explicitly and NOTHING may be
//               published
//
// WHY RM-MESH-03 AND RM-MESH-04 ARE TWO-LOOP EXTRUSIONS RATHER THAN DRILLED
// HOLES. A boundary region can only be the target of a forward query if its
// face carries a `FaceName`, and `cutHole` names a hole's flat faces and NOT
// its cylindrical wall (P16-MAP-001's known limitation; `MappedFace::names`
// "MAY BE EMPTY, and that is a real state"). Both models must map their hole
// wall, so both put the inner loop in the profile sketch, where the wall
// becomes `FaceRole::Side` of the circle that swept it. That is a construction
// decision forced by the naming contract, not a preference.
//
// EVERY MODEL CARRIES ITS MESHING INTENT as a `MeshControl` document object,
// including RM-MESH-08. Without one, the headless path would fail with
// `no_mesh_control` -- true, and not the reason -- which is exactly the defect
// P16-CLI-001's mutation M6 found. A reference model that reproduced it would
// be asserting the wrong failure.
namespace bettercad::reference {

/// A side face of an extrusion: the face swept by one entity of its profile.
///
/// Spelled once, here, because the suite and its builders must agree on what
/// "the hole wall" is. A side face is named by the sketch ENTITY that swept it
/// (P12-STREF-001), never by a face index.
[[nodiscard]] inline FaceName sweptFace(ObjectId solid, EntityId entity) {
    return FaceName{solid, FaceSelector{.role = FaceRole::Side, .entity = entity}};
}

/// The face an extrusion starts from: its sketch plane.
[[nodiscard]] inline FaceName startCapFace(ObjectId solid) {
    return FaceName{solid, FaceSelector{.role = FaceRole::StartCap}};
}

/// The face an extrusion ends at: its depth.
[[nodiscard]] inline FaceName endCapFace(ObjectId solid) {
    return FaceName{solid, FaceSelector{.role = FaceRole::EndCap}};
}

/// RM-MESH-01. A 120 x 70 x 35 mm block with one corner at the origin,
/// meshed with a 20 mm global target and no local control.
///
/// Three different edge lengths deliberately: a cube would let an axis
/// permutation, a transposed bounding box or a swapped sketch axis pass every
/// check in the suite.
///
/// Its six faces are all nameable, which makes it the model that proves a
/// COMPLETE boundary partition: four side faces (one per profile line), a
/// start cap at z = 0 and an end cap at z = 35.
struct MeshBlockModel {
    Document document;
    ParameterId a{}, b{}, c{};
    ObjectId sketch{}, solid{}, control{};
    /// The profile's four lines, in order: y = 0, x = a, y = b, x = 0.
    std::array<EntityId, 4> lines{};

    /// The side face swept by line @p which of the profile.
    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName bottom() const { return startCapFace(solid); }
    [[nodiscard]] FaceName top() const { return endCapFace(solid); }
};

[[nodiscard]] Result<MeshBlockModel> buildMeshBlockReferenceModel();

/// RM-MESH-02. A cylinder of radius 25 mm and height 60 mm, axis along Z from
/// z = 0, meshed with a 12 mm global target and a 0.25 mm surface deflection.
///
/// h != 2r on purpose: a 50 mm height would make the body as tall as it is
/// wide, and a product that transposed the radius and the height would give
/// the same volume.
struct MeshCylinderModel {
    Document document;
    ParameterId radius{}, height{};
    ObjectId sketch{}, solid{}, control{};
    /// The profile circle, which sweeps the lateral wall.
    EntityId rim{};

    [[nodiscard]] FaceName wall() const { return sweptFace(solid, rim); }
    [[nodiscard]] FaceName bottomCap() const { return startCapFace(solid); }
    [[nodiscard]] FaceName topCap() const { return endCapFace(solid); }
};

[[nodiscard]] Result<MeshCylinderModel> buildMeshCylinderReferenceModel();

/// RM-MESH-03. A 100 x 60 x 12 mm plate with a 15 mm radius hole through its
/// thickness, centred at (50, 30) -- the middle of the plate, so the hole is
/// fully internal with 35 mm of material beside it along X and 15 mm along Y.
///
/// V = LWt - pi r^2 t, and the hole is 13.4% of that: a mesher that filled it
/// would miss by thirteen percent, which no tolerance in this suite could
/// absorb.
struct MeshPlateWithHoleModel {
    Document document;
    ParameterId length{}, width{}, thickness{}, holeRadius{};
    ObjectId sketch{}, solid{}, control{};
    std::array<EntityId, 4> lines{};
    /// The inner loop: the circle that sweeps the hole's wall.
    EntityId hole{};

    [[nodiscard]] FaceName holeWall() const { return sweptFace(solid, hole); }
    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName bottom() const { return startCapFace(solid); }
    [[nodiscard]] FaceName top() const { return endCapFace(solid); }
};

[[nodiscard]] Result<MeshPlateWithHoleModel> buildMeshPlateWithHoleReferenceModel();

/// RM-MESH-04. A tube, outer radius 30 mm, inner radius 18 mm, height 45 mm.
///
/// V = pi(Ro^2 - Ri^2) h = 81430.08 mm^3, where the bounding cylinder would be
/// 127235 mm^3: filling the void would be a 56% error. The inner and outer
/// walls are swept by DIFFERENT circles, so they carry different names and a
/// mapping that confused them would be visible.
struct MeshTubeModel {
    Document document;
    ParameterId outerRadius{}, innerRadius{}, height{};
    ObjectId sketch{}, solid{}, control{};
    EntityId outerRim{}, innerRim{};

    [[nodiscard]] FaceName outerWall() const { return sweptFace(solid, outerRim); }
    [[nodiscard]] FaceName innerWall() const { return sweptFace(solid, innerRim); }
    [[nodiscard]] FaceName bottomAnnulus() const { return startCapFace(solid); }
    [[nodiscard]] FaceName topAnnulus() const { return endCapFace(solid); }
};

[[nodiscard]] Result<MeshTubeModel> buildMeshTubeReferenceModel();

/// RM-MESH-05. A 120 x 80 x 1.5 mm plate: 80 times longer than it is thick.
///
/// Deliberately challenging, and deliberately NOT pathological -- 1.5 mm is
/// seven orders of magnitude above the kernel's confusion tolerance of 1e-7 mm,
/// geometry is sound and only the DISCRETISATION is hard. Planar, so its
/// analytic volume is exact and a volume failure could not be excused by
/// curvature.
struct MeshThinPlateModel {
    Document document;
    ParameterId length{}, width{}, thickness{};
    ObjectId sketch{}, solid{}, control{};
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName bottom() const { return startCapFace(solid); }
    [[nodiscard]] FaceName top() const { return endCapFace(solid); }
};

[[nodiscard]] Result<MeshThinPlateModel> buildMeshThinPlateReferenceModel();

/// The rigid transform RM-MESH-06 applies: the orthonormal right-handed triad
/// its placed model's sketch plane is built from, and the offset of its origin.
///
/// WRITTEN OUT RATHER THAN COMPUTED FROM AN ANGLE. The entries are thirds, so
/// they are the same doubles on every platform and in every configuration, and
/// orthonormality can be checked by hand:
///
///   |X| = |Y| = |N| = 1,  X.Y = X.N = Y.N = 0,  X x Y = N
///
/// It mixes all three axes, so it is not a rotation about a coordinate axis and
/// an axis-permutation defect cannot survive it. There is no transform FEATURE
/// in BetterCAD -- P13-XFORM-001 is about component placements and says "no
/// transformed bodies" -- so the transform is applied by building the model's
/// profile on this frame, where local (u, v, w) maps to origin + uX + vY + wN.
struct RigidPlacement {
    std::array<double, 3> xAxis{2.0 / 3.0, 2.0 / 3.0, -1.0 / 3.0};
    std::array<double, 3> yAxis{-1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0};
    std::array<double, 3> normal{2.0 / 3.0, -1.0 / 3.0, 2.0 / 3.0};
    /// Millimetres.
    std::array<double, 3> origin{37.0, -19.0, 23.0};
};

/// RM-MESH-06's transform, as one value both the builder and the suite read.
[[nodiscard]] constexpr RigidPlacement meshTransformedPlacement() noexcept { return {}; }

/// The sketch plane RM-MESH-06's placed model is built on.
[[nodiscard]] Result<Frame3D> meshTransformedFrame();

/// RM-MESH-06. A 90 x 55 x 24 mm asymmetric block, built either at the origin
/// on the XY plane (`placed == false`) or on the rotated and translated frame
/// `meshTransformedFrame()` (`placed == true`).
///
/// ITS OWN BASE BLOCK, not RM-MESH-01's, so that neither model depends on the
/// other having been built -- which the brief requires of every case and which
/// also keeps the two suites' dimensions from drifting into each other.
struct MeshTransformedBlockModel {
    Document document;
    ParameterId a{}, b{}, c{};
    ObjectId sketch{}, solid{}, control{};
    std::array<EntityId, 4> lines{};
    /// Whether this document is the transformed one.
    bool placed = false;

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName datumFace() const { return startCapFace(solid); }
    [[nodiscard]] FaceName oppositeFace() const { return endCapFace(solid); }
};

[[nodiscard]] Result<MeshTransformedBlockModel> buildMeshTransformedBaseReferenceModel();
[[nodiscard]] Result<MeshTransformedBlockModel> buildMeshTransformedPlacedReferenceModel();

/// RM-MESH-07. A 100 x 60 x 40 mm block with a 20 mm global target and a 6 mm
/// local target on ONE side face.
///
/// `refined` is the face the control names; `coarse` is the face OPPOSITE it,
/// and it carries no control. Two faces rather than one because "the target
/// region is finer" is only half the claim: the other half is that everything
/// else was left alone, and a control that refined the whole body would satisfy
/// the first half perfectly.
/// The sizes are NOT document parameters: `MeshSizingControls::globalTargetSize`
/// is a `Length`, and there is no parameter-driven mesh size in P16. A reader
/// that wants them takes them from the control's own definition, which is where
/// the canonical intent lives.
struct MeshLocalRefinementModel {
    Document document;
    ParameterId a{}, b{}, c{};
    ObjectId sketch{}, solid{}, control{};
    std::array<EntityId, 4> lines{};

    /// The face the local control names: the profile line at y = 0.
    [[nodiscard]] FaceName refined() const { return sweptFace(solid, lines.at(0)); }
    /// The face opposite it, at y = b, with no control of its own.
    [[nodiscard]] FaceName coarse() const { return sweptFace(solid, lines.at(2)); }
    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
    [[nodiscard]] FaceName bottom() const { return startCapFace(solid); }
    [[nodiscard]] FaceName top() const { return endCapFace(solid); }
};

[[nodiscard]] Result<MeshLocalRefinementModel> buildMeshLocalRefinementReferenceModel();

/// RM-MESH-08. An extrude of an OPEN profile: three sides of an 80 x 50 mm
/// rectangle, with the fourth deliberately absent.
///
/// The sketch solves; the extrude fails, with the feature layer's own words --
/// "the sketch has no closed profile". So the document loads, regenerates
/// deterministically, and reports a failed body every time, in every
/// configuration and after every save and load.
///
/// WHY NOT AN OPEN SHELL, which is the other input the brief offers.
/// `GeometryIneligibility::NotASolid` is where an open shell lands, "and lands
/// here on TOPOLOGY" -- but no feature in this repository produces a non-solid
/// body, so that state is not reachable from a committed model. A failed body
/// is the brief's own alternative, and it is reachable, deterministic and
/// honestly diagnosed.
///
/// IT CARRIES A MeshControl. See this header's opening note: without one the
/// refusal would name the missing control rather than the broken body.
struct MeshOpenProfileModel {
    Document document;
    ParameterId length{}, width{}, depth{};
    ObjectId sketch{}, solid{}, control{};
    /// The three lines that are present. The fourth side is absent.
    std::array<EntityId, 3> lines{};

    [[nodiscard]] FaceName side(std::size_t which) const { return sweptFace(solid, lines.at(which)); }
};

[[nodiscard]] Result<MeshOpenProfileModel> buildMeshOpenProfileReferenceModel();

/// The models of the meshing suite.
///
/// NINE ENTRIES FOR EIGHT MODEL IDS: RM-MESH-06 is a PAIR -- a body and the
/// same body rigidly transformed -- and comparing them is the whole point of
/// the case. RM-MAT-04 does the same with three entries for one ID.
enum class MeshReferenceModelKind {
    Block,            ///< RM-MESH-01
    Cylinder,         ///< RM-MESH-02
    PlateWithHole,    ///< RM-MESH-03
    Tube,             ///< RM-MESH-04
    ThinPlate,        ///< RM-MESH-05
    TransformedBase,  ///< RM-MESH-06
    TransformedPlaced,///< RM-MESH-06
    LocalRefinement,  ///< RM-MESH-07
    OpenProfile,      ///< RM-MESH-08
};

struct MeshReferenceModelInfo {
    MeshReferenceModelKind kind;
    /// The reference ID, e.g. "RM-MESH-01".
    std::string_view id;
    /// Document name, e.g. "MeshBlock".
    std::string_view name;
    /// File name stem, e.g. "mesh_block" for mesh_block.bcad.
    std::string_view fileStem;
    /// What this model exists to prove, one line, for the runner's output.
    std::string_view purpose;
    /// Whether a volume mesh is expected to be produced at all. False for
    /// RM-MESH-08, whose expected outcome is an explicit refusal.
    bool expectMesh;
    /// The CAD volume this model's dimensions imply, in mm^3, from closed form.
    ///
    /// A DECLARATION OF WHAT THE MODEL IS, and deliberately not the suite's
    /// oracle. The tests read the model's DIMENSIONS from its own parameters,
    /// put them through the independent closed forms in
    /// `tests/reference/Analytic.hpp`, and require the result to match this
    /// number -- so a typo here, or a builder that does not build what this
    /// says, is a failure rather than a silently agreed mistake. Zero for
    /// RM-MESH-08, which has no body.
    double analyticVolumeMm3;
    /// A main dimension, and a value to try it at (mm): what the model-change
    /// gate changes to make the model regenerate and remesh.
    std::string_view mainParameter;
    double mainParameterMm;
};

/// `pi` to the precision of the suite's own constant, so the declared volumes
/// below are the same number the tests derive. Not 3.14159.
inline constexpr double kPi = 3.14159265358979323846;

inline constexpr std::array kMeshReferenceModels{
    MeshReferenceModelInfo{MeshReferenceModelKind::Block, "RM-MESH-01", "MeshBlock", "mesh_block",
                           "asymmetric block: exact analytic volume, six nameable faces", true,
                           120.0 * 70.0 * 35.0, "block_a", 150.0},
    MeshReferenceModelInfo{MeshReferenceModelKind::Cylinder, "RM-MESH-02", "MeshCylinder", "mesh_cylinder",
                           "cylinder, h != 2r: curved conformity and volume convergence", true,
                           kPi * 25.0 * 25.0 * 60.0, "cyl_h", 75.0},
    MeshReferenceModelInfo{MeshReferenceModelKind::PlateWithHole, "RM-MESH-03", "MeshPlateWithHole",
                           "mesh_plate_with_hole",
                           "plate with a through-hole: the hole must stay empty", true,
                           100.0 * 60.0 * 12.0 - kPi * 15.0 * 15.0 * 12.0, "plate_t", 16.0},
    MeshReferenceModelInfo{MeshReferenceModelKind::Tube, "RM-MESH-04", "MeshTube", "mesh_tube",
                           "hollow tube: the void survives and the two walls map apart", true,
                           kPi * (30.0 * 30.0 - 18.0 * 18.0) * 45.0, "tube_h", 60.0},
    MeshReferenceModelInfo{MeshReferenceModelKind::ThinPlate, "RM-MESH-05", "MeshThinPlate",
                           "mesh_thin_plate",
                           "thin plate, 80:1: a valid mesh with worse shape metrics, or a refusal",
                           true, 120.0 * 80.0 * 1.5, "thin_t", 3.0},
    MeshReferenceModelInfo{MeshReferenceModelKind::TransformedBase, "RM-MESH-06", "MeshTransformedBase",
                           "mesh_transformed_base", "the base block, at the origin on the XY plane", true,
                           90.0 * 55.0 * 24.0, "base_a", 110.0},
    MeshReferenceModelInfo{MeshReferenceModelKind::TransformedPlaced, "RM-MESH-06", "MeshTransformedPlaced",
                           "mesh_transformed_placed",
                           "the same block under a compound rotation and a translation", true,
                           90.0 * 55.0 * 24.0, "placed_a", 110.0},
    MeshReferenceModelInfo{MeshReferenceModelKind::LocalRefinement, "RM-MESH-07", "MeshLocalRefinement",
                           "mesh_local_refinement",
                           "local refinement on one face, with the opposite face left coarse", true,
                           100.0 * 60.0 * 40.0, "local_a", 120.0},
    MeshReferenceModelInfo{MeshReferenceModelKind::OpenProfile, "RM-MESH-08", "MeshOpenProfile",
                           "mesh_open_profile", "an open profile: the body fails and nothing is published",
                           false, 0.0, "open_l", 90.0},
};

/// How many distinct RM-MESH IDs the suite covers. Eight, mandated; the
/// catalog has nine entries because RM-MESH-06 is a pair.
inline constexpr std::size_t kMeshReferenceModelIdCount = 8;

/// The model's document, from its builder.
[[nodiscard]] Result<Document> buildMeshReferenceModel(MeshReferenceModelKind kind);

} // namespace bettercad::reference
