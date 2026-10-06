#include "BuildSupport.hpp"
#include "MeshReferenceModels.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <string>
#include <utility>
#include <vector>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::sketchId;
using meshing::LocalMeshSizing;
using meshing::MeshControl;
using meshing::MeshControlDefinition;
using meshing::NamedBoundarySet;
using meshing::VolumeMeshControls;

namespace {

/// The surface deflection every curved model in the suite declares.
///
/// 0.25 mm, and the figure is load-bearing rather than a round number: it is
/// what the analytic tolerance is DERIVED from. A chord of a circle of radius r
/// whose deepest deviation from the arc is d has its closest approach to the
/// centre at r - d, so the inscribed polygon contains the disc of radius r - d
/// and is contained in the disc of radius r. That gives the suite a two-sided,
/// closed-form bound on every curved model's volume -- see
/// `tests/reference/Analytic.hpp` -- instead of a percentage chosen because it
/// passed.
///
/// A tighter deflection would tighten the bound and cost boundary triangles in
/// every preset of every regression run. 0.25 mm on the smallest radius in the
/// suite (15 mm, RM-MESH-03's hole) bounds the volume to 3.3% OF THE HOLE,
/// which is 0.44% of that model's body -- while a mesher that FILLED the hole
/// would miss by 13.4%. The bound is thirty times smaller than the defect it
/// exists to catch, which is the only test a tolerance has to pass.
constexpr Length kCurvedDeflection = Length::fromSi(0.25e-3);

/// The angular limit, left at the surface layer's own default (20 degrees) for
/// every model: this suite has no reason to state a different one, and a
/// reference model that quietly changed a default would make the committed
/// intent disagree with the product's.
constexpr Angle kAngularDeflection = Angle::fromSi(0.3490658503988659);

/// A rectangle (0,0)..(w,d) on @p plane, fully constrained, returning its four
/// lines in order: y = 0, x = w, y = d, x = 0.
///
/// Its own helper rather than `AssemblyParts`' `blockPart`, because this suite
/// needs the LINE ENTITIES: a side face is named by the entity that swept it,
/// so a model that cannot say which line is which cannot name its own faces.
struct Rectangle {
    std::array<EntityId, 4> lines{};
    EntityId origin{};
};

Rectangle rectangleProfile(detail::SketchBuilder& profile, double wMm, double dMm, ParameterId width,
                           ParameterId depth) {
    Rectangle rectangle;
    const EntityId origin = profile.point(0.0, 0.0);
    const EntityId alongX = profile.point(wMm, 0.0);
    const EntityId corner = profile.point(wMm, dMm);
    const EntityId alongY = profile.point(0.0, dMm);
    rectangle.origin = origin;
    rectangle.lines[0] = profile.line(origin, alongX);
    rectangle.lines[1] = profile.line(alongX, corner);
    rectangle.lines[2] = profile.line(corner, alongY);
    rectangle.lines[3] = profile.line(alongY, origin);
    profile.fixed(origin);
    profile.horizontal(rectangle.lines[0]);
    profile.horizontal(rectangle.lines[2]);
    profile.vertical(rectangle.lines[1]);
    profile.vertical(rectangle.lines[3]);
    profile.length(rectangle.lines[0], width);
    profile.length(rectangle.lines[1], depth);
    return rectangle;
}

/// Adds the model's canonical meshing intent.
///
/// EVERY MODEL GETS ONE, RM-MESH-08 INCLUDED. The quality policy is
/// `reportOnlyThresholds()` -- P16-QUALITY-001's own default, which carries no
/// thresholds at all because "quality thresholds are solver requirements" and
/// P17 owns them. A reference model that invented a threshold would be grading
/// every mesh in the suite against a standard nobody set.
ObjectId meshIntent(detail::ModelBuilder& b, ObjectId body, const VolumeMeshControls& mesh,
                    std::vector<NamedBoundarySet> sets) {
    MeshControlDefinition definition;
    definition.body = body;
    definition.mesh = mesh;
    definition.quality = meshing::reportOnlyThresholds();
    definition.boundarySets = std::move(sets);
    return b.feature<MeshControl>("Mesh", definition);
}

/// Planar sizing: the surface layer's defaults, with one global target.
VolumeMeshControls planarIntent(Length globalTarget) {
    VolumeMeshControls controls;
    controls.sizing.globalTargetSize = globalTarget;
    return controls;
}

/// Curved sizing: the suite's declared deflection, with one global target.
VolumeMeshControls curvedIntent(Length globalTarget) {
    VolumeMeshControls controls;
    controls.surface.linearDeflection = kCurvedDeflection;
    controls.surface.angularDeflection = kAngularDeflection;
    controls.sizing.globalTargetSize = globalTarget;
    return controls;
}

} // namespace

Result<MeshBlockModel> buildMeshBlockReferenceModel() {
    MeshBlockModel m{.document = Document(detail::fixedDocumentId("16e50001-0000-4000-8000-000000000001"),
                                          "MeshBlock")};
    detail::ModelBuilder b(m.document);

    // 120 x 70 x 35 mm, one corner at the origin. THREE DIFFERENT EDGE
    // LENGTHS: a cube would let an axis permutation, a transposed bounding box
    // or a swapped sketch axis pass every check in the suite.
    m.a = b.length("block_a", 120.0);
    m.b = b.length("block_b", 70.0);
    m.c = b.length("block_c", 35.0);

    detail::SketchBuilder profile(b, "BlockSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 120.0, 70.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "Block", {.profile = sketchId(m.sketch), .depth = 35_mm, .depthParameter = m.c});

    // Two boundary sets at opposite ends, which is what a restraint and a load
    // would be. Named for the physics rather than for the geometry, because
    // that is what a solver's input will say.
    m.control = meshIntent(b, m.solid, planarIntent(20_mm),
                           {NamedBoundarySet{.id = BoundarySetId::fromValue(1U),
                                             .name = "fixed_end",
                                             .faces = {m.bottom()}},
                            NamedBoundarySet{.id = BoundarySetId::fromValue(2U),
                                             .name = "loaded_end",
                                             .faces = {m.top()}}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

Result<MeshCylinderModel> buildMeshCylinderReferenceModel() {
    MeshCylinderModel m{.document = Document(detail::fixedDocumentId("16e50002-0000-4000-8000-000000000002"),
                                             "MeshCylinder")};
    detail::ModelBuilder b(m.document);

    // r = 25, h = 60. h != 2r DELIBERATELY: at h = 50 the body would be as
    // tall as it is wide, and a product that transposed the radius and the
    // height would give the same volume and the same bounding box.
    m.radius = b.length("cyl_r", 25.0);
    m.height = b.length("cyl_h", 60.0);

    detail::SketchBuilder profile(b, "CylinderSketch", Frame3D::xy());
    const EntityId centre = profile.point(0.0, 0.0);
    m.rim = profile.circle(centre, 25.0);
    profile.fixed(centre);
    profile.radius(m.rim, m.radius);
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "Cylinder", {.profile = sketchId(m.sketch), .depth = 60_mm, .depthParameter = m.height});

    // Three sets, one per CAD face, so that the mapping matrix can be checked
    // region by region: a lateral set holding a cap facet, or a cap set
    // holding a lateral one, is the leak this separation exists to find.
    m.control = meshIntent(b, m.solid, curvedIntent(12_mm),
                           {NamedBoundarySet{.id = BoundarySetId::fromValue(1U),
                                             .name = "bottom_cap",
                                             .faces = {m.bottomCap()}},
                            NamedBoundarySet{.id = BoundarySetId::fromValue(2U),
                                             .name = "top_cap",
                                             .faces = {m.topCap()}},
                            NamedBoundarySet{.id = BoundarySetId::fromValue(3U),
                                             .name = "lateral_wall",
                                             .faces = {m.wall()}}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

Result<MeshPlateWithHoleModel> buildMeshPlateWithHoleReferenceModel() {
    MeshPlateWithHoleModel m{
        .document = Document(detail::fixedDocumentId("16e50003-0000-4000-8000-000000000003"),
                             "MeshPlateWithHole")};
    detail::ModelBuilder b(m.document);

    // 100 x 60 x 12 mm with a 15 mm radius hole at the centre of the plate.
    // Fully internal: 35 mm of material beside the hole along X and 15 mm
    // along Y, both far above anything the kernel could confuse.
    m.length = b.length("plate_l", 100.0);
    m.width = b.length("plate_w", 60.0);
    m.thickness = b.length("plate_t", 12.0);
    m.holeRadius = b.length("plate_hole_r", 15.0);

    detail::SketchBuilder profile(b, "PlateSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 100.0, 60.0, m.length, m.width);
    m.lines = rectangle.lines;

    // THE HOLE IS AN INNER LOOP OF THE PROFILE, not a drilled feature, and
    // that is forced rather than chosen: `cutHole` does not name a hole's
    // cylindrical wall, so a drilled hole's wall has no reference to map. Swept
    // by a profile circle, it is `FaceRole::Side` of that circle.
    const EntityId holeCentre = profile.point(50.0, 30.0);
    m.hole = profile.circle(holeCentre, 15.0);
    profile.fixed(holeCentre);
    profile.radius(m.hole, m.holeRadius);
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "Plate", {.profile = sketchId(m.sketch), .depth = 12_mm, .depthParameter = m.thickness});

    // The hole wall is the set that matters, and it is the one the persistence
    // gate re-resolves after a fresh load.
    m.control = meshIntent(b, m.solid, curvedIntent(12_mm),
                           {NamedBoundarySet{.id = BoundarySetId::fromValue(1U),
                                             .name = "hole_wall",
                                             .faces = {m.holeWall()}},
                            NamedBoundarySet{.id = BoundarySetId::fromValue(2U),
                                             .name = "clamped_faces",
                                             .faces = {m.side(0), m.side(2)}}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

Result<MeshTubeModel> buildMeshTubeReferenceModel() {
    MeshTubeModel m{.document = Document(detail::fixedDocumentId("16e50004-0000-4000-8000-000000000004"),
                                         "MeshTube")};
    detail::ModelBuilder b(m.document);

    // Ro = 30, Ri = 18, h = 45. The bore is 36% of the outer disc's area, so
    // filling the void would be a 56% volume error.
    m.outerRadius = b.length("tube_ro", 30.0);
    m.innerRadius = b.length("tube_ri", 18.0);
    m.height = b.length("tube_h", 45.0);

    detail::SketchBuilder profile(b, "TubeSketch", Frame3D::xy());
    const EntityId centre = profile.point(0.0, 0.0);
    m.outerRim = profile.circle(centre, 30.0);
    m.innerRim = profile.circle(centre, 18.0);
    profile.fixed(centre);
    profile.radius(m.outerRim, m.outerRadius);
    profile.radius(m.innerRim, m.innerRadius);
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "Tube", {.profile = sketchId(m.sketch), .depth = 45_mm, .depthParameter = m.height});

    // FOUR SETS, one per CAD face. The inner and outer walls are swept by
    // DIFFERENT circles, so they carry different names -- and a mapping that
    // confused them would put a pressure load on the wrong surface.
    m.control = meshIntent(b, m.solid, curvedIntent(10_mm),
                           {NamedBoundarySet{.id = BoundarySetId::fromValue(1U),
                                             .name = "outer_wall",
                                             .faces = {m.outerWall()}},
                            NamedBoundarySet{.id = BoundarySetId::fromValue(2U),
                                             .name = "inner_wall",
                                             .faces = {m.innerWall()}},
                            NamedBoundarySet{.id = BoundarySetId::fromValue(3U),
                                             .name = "bottom_annulus",
                                             .faces = {m.bottomAnnulus()}},
                            NamedBoundarySet{.id = BoundarySetId::fromValue(4U),
                                             .name = "top_annulus",
                                             .faces = {m.topAnnulus()}}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

Result<MeshThinPlateModel> buildMeshThinPlateReferenceModel() {
    MeshThinPlateModel m{.document = Document(detail::fixedDocumentId("16e50005-0000-4000-8000-000000000005"),
                                              "MeshThinPlate")};
    detail::ModelBuilder b(m.document);

    // 120 x 80 x 1.5 mm: eighty times longer than it is thick. CHALLENGING AND
    // NOT PATHOLOGICAL -- 1.5 mm is seven orders of magnitude above the kernel's
    // confusion tolerance, so the geometry is sound and only the
    // discretisation is hard. The brief forbids geometry below model
    // tolerance "merely to force failure", and this is not that.
    m.length = b.length("thin_l", 120.0);
    m.width = b.length("thin_w", 80.0);
    m.thickness = b.length("thin_t", 1.5);

    detail::SketchBuilder profile(b, "ThinPlateSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 120.0, 80.0, m.length, m.width);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "ThinPlate", {.profile = sketchId(m.sketch), .depth = 1.5_mm, .depthParameter = m.thickness});

    // A 20 mm global target on a body 1.5 mm thick: the global bound imposes
    // nothing through the thickness, so the BOUNDARY governs, which is the
    // condition that makes this model hard. Stating a target finer than the
    // thickness would have been quietly fixing the model's own difficulty.
    m.control = meshIntent(b, m.solid, planarIntent(20_mm),
                           {NamedBoundarySet{.id = BoundarySetId::fromValue(1U),
                                             .name = "broad_face",
                                             .faces = {m.top()}}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

Result<Frame3D> meshTransformedFrame() {
    const RigidPlacement placement = meshTransformedPlacement();
    // fromUnitComponents, NOT fromComponents: it stores the components
    // unchanged after checking they are unit within 1e-12, so the frame holds
    // exactly the thirds written above and the suite computing R x + t for
    // itself uses the same doubles. Normalising would store something a
    // rounding apart from what the test believes the transform to be.
    auto direction = [](const std::array<double, 3>& v) {
        return Direction3D::fromUnitComponents(v[0], v[1], v[2]);
    };
    const auto x = direction(placement.xAxis);
    const auto y = direction(placement.yAxis);
    const auto normal = direction(placement.normal);
    if (!x || !y || !normal) {
        return makeError(ErrorCode::InvalidArgument,
                         "RM-MESH-06's triad is not unit-length to within 1e-12");
    }
    // fromAxes STORES THE AXES UNCHANGED after checking orthonormality and
    // handedness within 1e-12, which is why the triad is written out in thirds
    // rather than derived from an angle: the frame the model is built on is the
    // frame the suite's own transform uses, bit for bit.
    return Frame3D::fromAxes(detail::pointMm(placement.origin[0], placement.origin[1], placement.origin[2]),
                             *x, *y, *normal);
}

namespace {

/// RM-MESH-06's two documents differ only in the plane their profile is drawn
/// on, which is the whole point: one function, one set of dimensions, and no
/// way for the pair to drift apart.
Result<MeshTransformedBlockModel> buildTransformedBlock(bool placed) {
    MeshTransformedBlockModel m{
        .document = Document(detail::fixedDocumentId(placed ? "16e50006-0000-4000-8000-000000000007"
                                                            : "16e50006-0000-4000-8000-000000000006"),
                             placed ? "MeshTransformedPlaced" : "MeshTransformedBase"),
        .placed = placed};
    detail::ModelBuilder b(m.document);

    const std::string prefix = placed ? "placed" : "base";
    m.a = b.length(prefix + "_a", 90.0);
    m.b = b.length(prefix + "_b", 55.0);
    m.c = b.length(prefix + "_c", 24.0);

    Frame3D plane = Frame3D::xy();
    if (placed) {
        auto frame = meshTransformedFrame();
        if (!frame) {
            return std::unexpected(frame.error());
        }
        plane = *frame;
    }

    detail::SketchBuilder profile(b, prefix == "placed" ? "PlacedSketch" : "BaseSketch", plane);
    const Rectangle rectangle = rectangleProfile(profile, 90.0, 55.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        placed ? "PlacedBlock" : "BaseBlock",
        {.profile = sketchId(m.sketch), .depth = 24_mm, .depthParameter = m.c});

    // THE SAME intent on both, so that any difference between the two meshes
    // is the placement and nothing else.
    m.control = meshIntent(b, m.solid, planarIntent(16_mm),
                           {NamedBoundarySet{.id = BoundarySetId::fromValue(1U),
                                             .name = "datum_face",
                                             .faces = {m.datumFace()}},
                            NamedBoundarySet{.id = BoundarySetId::fromValue(2U),
                                             .name = "first_side",
                                             .faces = {m.side(0)}}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace

Result<MeshTransformedBlockModel> buildMeshTransformedBaseReferenceModel() {
    return buildTransformedBlock(false);
}

Result<MeshTransformedBlockModel> buildMeshTransformedPlacedReferenceModel() {
    return buildTransformedBlock(true);
}

Result<MeshLocalRefinementModel> buildMeshLocalRefinementReferenceModel() {
    MeshLocalRefinementModel m{
        .document = Document(detail::fixedDocumentId("16e50007-0000-4000-8000-000000000008"),
                             "MeshLocalRefinement")};
    detail::ModelBuilder b(m.document);

    m.a = b.length("local_a", 100.0);
    m.b = b.length("local_b", 60.0);
    m.c = b.length("local_c", 40.0);

    detail::SketchBuilder profile(b, "LocalSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 100.0, 60.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "LocalBlock", {.profile = sketchId(m.sketch), .depth = 40_mm, .depthParameter = m.c});

    // 20 mm globally, 6 mm on ONE side face. The ratio matters: too close and
    // the refinement is lost in the grading Netgen inserts between regions,
    // too far and the mesher has to subdivide the whole body to get from one
    // size to the other. 3.3x is measurable at the face and still local.
    VolumeMeshControls controls = planarIntent(20_mm);
    controls.sizing.local.push_back(LocalMeshSizing{.face = m.refined(), .targetSize = 6_mm});

    // BOTH faces are named sets, including the one with NO control. "The
    // target is finer" is only half the claim; the other half is that the rest
    // of the body was left alone, and a set on the opposite face is what lets
    // the suite measure it through the same production API.
    m.control = meshIntent(b, m.solid, controls,
                           {NamedBoundarySet{.id = BoundarySetId::fromValue(1U),
                                             .name = "refined_face",
                                             .faces = {m.refined()}},
                            NamedBoundarySet{.id = BoundarySetId::fromValue(2U),
                                             .name = "coarse_face",
                                             .faces = {m.coarse()}}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

Result<MeshOpenProfileModel> buildMeshOpenProfileReferenceModel() {
    MeshOpenProfileModel m{
        .document = Document(detail::fixedDocumentId("16e50008-0000-4000-8000-000000000009"),
                             "MeshOpenProfile")};
    detail::ModelBuilder b(m.document);

    m.length = b.length("open_l", 80.0);
    m.width = b.length("open_w", 50.0);
    m.depth = b.length("open_d", 20.0);

    // THREE SIDES OF A RECTANGLE. The fourth, from (0, w) back to the origin,
    // is deliberately absent, so the profile does not close and the extrude
    // has nothing to sweep.
    detail::SketchBuilder profile(b, "OpenSketch", Frame3D::xy());
    const EntityId origin = profile.point(0.0, 0.0);
    const EntityId alongX = profile.point(80.0, 0.0);
    const EntityId corner = profile.point(80.0, 50.0);
    const EntityId alongY = profile.point(0.0, 50.0);
    m.lines[0] = profile.line(origin, alongX);
    m.lines[1] = profile.line(alongX, corner);
    m.lines[2] = profile.line(corner, alongY);
    profile.fixed(origin);
    profile.horizontal(m.lines[0]);
    profile.horizontal(m.lines[2]);
    profile.vertical(m.lines[1]);
    profile.length(m.lines[0], m.length);
    profile.length(m.lines[1], m.width);
    m.sketch = profile.finish();

    // The sketch SOLVES. The extrude is the thing that fails, at regeneration,
    // with the feature layer's own words -- so the diagnostic names the real
    // cause instead of a downstream symptom.
    m.solid = b.feature<features::ExtrudeFeature>(
        "OpenSolid", {.profile = sketchId(m.sketch), .depth = 20_mm, .depthParameter = m.depth});

    // AND IT CARRIES A CONTROL. Without one the headless refusal would be
    // `no_mesh_control` -- true, and not the reason, which is the defect
    // P16-CLI-001's mutation M6 found. The model exists to prove that a FAILED
    // BODY is refused, so the control has to be there for the refusal to be
    // about the body.
    m.control = meshIntent(b, m.solid, planarIntent(20_mm),
                           {NamedBoundarySet{.id = BoundarySetId::fromValue(1U),
                                             .name = "intended_wall",
                                             .faces = {m.side(0)}}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
