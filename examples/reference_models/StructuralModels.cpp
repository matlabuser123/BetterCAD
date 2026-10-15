#include "BuildSupport.hpp"
#include "StructuralReferenceModels.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralData.hpp>
#include <bettercad/structural/StructuralLoad.hpp>
#include <bettercad/structural/StructuralRestraint.hpp>

#include <string>
#include <utility>
#include <vector>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::sketchId;
using meshing::LocalMeshSizing;
using meshing::MeshControl;
using meshing::MeshControlDefinition;
using meshing::NamedBoundarySet;
using meshing::VolumeMeshControls;
using structural::DofComponent;
using structural::RestraintComponents;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralRestraint;

namespace {

/// The note every material in this suite carries, so that no reader can take a
/// number here for a sourced datasheet figure. The P15 suite's own wording.
constexpr std::string_view kSynthetic =
    "TEST / SYNTHETIC ENGINEERING DATA. Round, physically plausible values chosen so that "
    "closed-form validation is exact. Not sourced datasheet figures.";

/// The surface deflection RM-FEA-03's committed intent declares: P16's own
/// 0.25 mm, so the coarsest level of the convergence study is the deflection
/// the meshing suite qualified.
constexpr Length kCurvedDeflection = Length::fromSi(0.25e-3);

/// The angular limit, left at the surface layer's own default (20 degrees).
constexpr Angle kAngularDeflection = Angle::fromSi(0.3490658503988659);

/// A rectangle (0,0)..(w,d) on @p plane, fully constrained, returning its four
/// lines in order: y = 0, x = w, y = d, x = 0.
///
/// THE SAME HELPER THE MESHING SUITE HAS, restated here rather than shared
/// through a header, because its one job is to make the LINE ENTITIES
/// available: a side face is named by the entity that swept it, so a model
/// that cannot say which line is which cannot name its own faces -- and every
/// roller support below is a side face.
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

/// The suite's synthetic steel: E 210 GPa, nu 0.3, rho 7850 -- the three
/// constants the header declares, and nothing a linear-static solve does not
/// consume.
features::MaterialDefinition syntheticSteel() {
    using materials::MaterialProperty;
    features::MaterialDefinition d;
    d.designation = "Structural reference steel";
    d.family = "Carbon Steel";
    d.notes = std::string{kSynthetic};
    d.mechanical.density = MaterialProperty<Density>::known(Density::fromSi(kStructuralDensityKgM3));
    d.mechanical.youngsModulus =
        MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kStructuralYoungsModulusPa));
    d.mechanical.poissonRatio =
        MaterialProperty<PoissonRatio>::known(PoissonRatio::of(kStructuralPoissonRatio));
    return d;
}

/// Creates the steel and assigns it, the ordinary P15 way: one material per
/// document, assigned at document level.
void assignSteel(detail::ModelBuilder& b) {
    const MaterialId id = b.need(features::createMaterial(b.document(), "Steel", syntheticSteel()), "Steel");
    b.need(features::assignMaterial(b.document(), id), "material assignment");
}

/// Adds the model's canonical meshing intent, under P16-QUALITY-001's own
/// report-only default -- the structural policy is P17-VALID-001's and is
/// applied at validation, not stored in the control.
ObjectId meshIntent(detail::ModelBuilder& b, ObjectId body, const VolumeMeshControls& mesh,
                    std::vector<NamedBoundarySet> sets) {
    MeshControlDefinition definition;
    definition.body = body;
    definition.mesh = mesh;
    definition.quality = meshing::reportOnlyThresholds();
    definition.boundarySets = std::move(sets);
    return b.feature<MeshControl>("Mesh", definition);
}

VolumeMeshControls planarIntent(Length globalTarget) {
    VolumeMeshControls controls;
    controls.sizing.globalTargetSize = globalTarget;
    return controls;
}

VolumeMeshControls curvedIntent(Length globalTarget) {
    VolumeMeshControls controls;
    controls.surface.linearDeflection = kCurvedDeflection;
    controls.surface.angularDeflection = kAngularDeflection;
    controls.sizing.globalTargetSize = globalTarget;
    return controls;
}

/// Adds the model's one analysis, named "Study", from a definition that names
/// the control transitively.
ObjectId study(detail::ModelBuilder& b, ObjectId control, StructuralAnalysisMode mode,
               std::vector<StructuralLoad> loads, std::vector<StructuralRestraint> restraints) {
    StructuralAnalysisDefinition definition;
    definition.mesh = MeshControlId::fromValue(control.value());
    definition.mode = mode;
    definition.loads = std::move(loads);
    definition.restraints = std::move(restraints);
    auto created = StructuralAnalysis::create(std::string{kStructuralStudyName}, std::move(definition));
    if (!created) {
        // Recorded through the builder, like every other failure, so the first
        // error wins and the model still reports cleanly.
        (void)b.need(Result<ObjectId>{std::unexpected(created.error())}, kStructuralStudyName);
        return {};
    }
    return b.add(std::move(*created), kStructuralStudyName);
}

[[nodiscard]] Traction3D tractionPa(double x, double y, double z) {
    return Traction3D{Pressure::fromSi(x), Pressure::fromSi(y), Pressure::fromSi(z)};
}

[[nodiscard]] StructuralLoad pressureOn(std::uint64_t id, FaceName face, double pascals) {
    return StructuralLoad{LoadId::fromValue(id),
                          structural::PressureLoad{.face = std::move(face),
                                                   .magnitude = Pressure::fromSi(pascals)}};
}

[[nodiscard]] StructuralLoad tractionOn(std::uint64_t id, FaceName face, const Traction3D& traction) {
    return StructuralLoad{LoadId::fromValue(id),
                          structural::SurfaceTractionLoad{.face = std::move(face), .traction = traction}};
}

[[nodiscard]] StructuralRestraint fixedOn(std::uint64_t id, FaceName face) {
    return StructuralRestraint::fixedSupport(RestraintId::fromValue(id), std::move(face));
}

[[nodiscard]] StructuralRestraint heldAlong(std::uint64_t id, FaceName face, DofComponent component) {
    return StructuralRestraint{RestraintId::fromValue(id), std::move(face),
                               RestraintComponents::along(component)};
}

/// One boundary set per face a load or restraint names, so the meshing
/// intent records the same regions the analysis uses and `mesh-boundaries`
/// can show them.
[[nodiscard]] NamedBoundarySet region(std::uint64_t id, std::string name, FaceName face) {
    return NamedBoundarySet{.id = BoundarySetId::fromValue(id), .name = std::move(name),
                            .faces = {std::move(face)}};
}

} // namespace

// --- RM-FEA-01 ---------------------------------------------------------------

Result<StructuralPatchModel> buildStructuralPatchReferenceModel() {
    StructuralPatchModel m{.document = Document(detail::fixedDocumentId("17fea001-0000-4000-8000-000000000001"),
                                                "StructuralPatch")};
    detail::ModelBuilder b(m.document);

    // 40 x 30 x 20 mm at a 20 mm target: 9 nodes and 12 tetrahedra, with ONE
    // interior node -- the configuration the patch test needs.
    m.a = b.length("patch_a", 40.0);
    m.b = b.length("patch_b", 30.0);
    m.c = b.length("patch_c", 20.0);

    detail::SketchBuilder profile(b, "PatchSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 40.0, 30.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "PatchBlock", {.profile = sketchId(m.sketch), .depth = 20_mm, .depthParameter = m.c});
    assignSteel(b);

    m.control = meshIntent(b, m.solid, planarIntent(20_mm),
                           {region(1U, "fixed_end", m.bottom()), region(2U, "loaded_end", m.top())});
    m.analysis = study(b, m.control, StructuralAnalysisMode::LinearStatic,
                       {pressureOn(1U, m.top(), 2.5e6)}, {fixedOn(1U, m.bottom())});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-FEA-02 ---------------------------------------------------------------

Result<StructuralAxialBarModel> buildStructuralAxialBarReferenceModel() {
    StructuralAxialBarModel m{
        .document = Document(detail::fixedDocumentId("17fea002-0000-4000-8000-000000000002"),
                             "StructuralAxialBar")};
    detail::ModelBuilder b(m.document);

    // 24 x 18 mm, 90 mm long. Three different lengths, and a cross-section
    // that is not square, so a swapped area or a transposed axis cannot pass.
    m.a = b.length("bar_a", 24.0);
    m.b = b.length("bar_b", 18.0);
    m.length = b.length("bar_l", 90.0);

    detail::SketchBuilder profile(b, "BarSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 24.0, 18.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "Bar", {.profile = sketchId(m.sketch), .depth = 90_mm, .depthParameter = m.length});
    assignSteel(b);

    m.control = meshIntent(b, m.solid, planarIntent(20_mm),
                           {region(1U, "base", m.base()), region(2U, "tip", m.tip()),
                            region(3U, "plane_x0", m.planeX0()), region(4U, "plane_y0", m.planeY0())});

    // THE ROLLERS. Uz on the base removes z-translation and the two
    // rotations out of the base plane; Ux on x = 0 removes x-translation and
    // the rotations about y and z; Uy on y = 0 removes y-translation and the
    // rotations about x and z. Six rigid modes, all removed, and the Poisson
    // contraction left free -- so the exact solution is affine.
    m.analysis = study(b, m.control, StructuralAnalysisMode::LinearStatic,
                       {tractionOn(1U, m.tip(), tractionPa(0.0, 0.0, kAxialTractionPa))},
                       {heldAlong(1U, m.base(), DofComponent::Uz),
                        heldAlong(2U, m.planeX0(), DofComponent::Ux),
                        heldAlong(3U, m.planeY0(), DofComponent::Uy)});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-FEA-03 ---------------------------------------------------------------

Result<StructuralCantileverModel> buildStructuralCantileverReferenceModel() {
    StructuralCantileverModel m{
        .document = Document(detail::fixedDocumentId("17fea003-0000-4000-8000-000000000003"),
                             "StructuralCantilever")};
    detail::ModelBuilder b(m.document);

    // r = 10, L = 100: slenderness 10, where beam theory is a fair description
    // and the Timoshenko shear term is a 2% correction the oracle carries
    // rather than ignores.
    m.radius = b.length("cant_r", 10.0);
    m.length = b.length("cant_l", 100.0);

    detail::SketchBuilder profile(b, "CantileverSketch", Frame3D::xy());
    const EntityId centre = profile.point(0.0, 0.0);
    m.rim = profile.circle(centre, 10.0);
    profile.fixed(centre);
    profile.radius(m.rim, m.radius);
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "Cantilever", {.profile = sketchId(m.sketch), .depth = 100_mm, .depthParameter = m.length});
    assignSteel(b);

    // The COARSEST level of the convergence study: 10 mm globally with P16's
    // 0.25 mm deflection. The suite refines through this control.
    m.control = meshIntent(b, m.solid, curvedIntent(10_mm),
                           {region(1U, "root", m.root()), region(2U, "tip", m.tip()),
                            region(3U, "wall", m.wall())});

    // A GLOBAL traction along -Y, uniform over the tip, so the total is the
    // traction times the cap's area: t = -F / (pi r^2).
    const double radiusM = 0.010;
    const double traction = -kCantileverTipForceN / (kPi * radiusM * radiusM);
    m.analysis = study(b, m.control, StructuralAnalysisMode::LinearStatic,
                       {tractionOn(1U, m.tip(), tractionPa(0.0, traction, 0.0))},
                       {fixedOn(1U, m.root())});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-FEA-04 ---------------------------------------------------------------

Result<StructuralPressureBlockModel> buildStructuralPressureBlockReferenceModel() {
    StructuralPressureBlockModel m{
        .document = Document(detail::fixedDocumentId("17fea004-0000-4000-8000-000000000004"),
                             "StructuralPressureBlock")};
    detail::ModelBuilder b(m.document);

    m.a = b.length("press_a", 60.0);
    m.b = b.length("press_b", 45.0);
    m.c = b.length("press_c", 30.0);

    detail::SketchBuilder profile(b, "PressureSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 60.0, 45.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "PressureBlock", {.profile = sketchId(m.sketch), .depth = 30_mm, .depthParameter = m.c});
    assignSteel(b);

    m.control = meshIntent(b, m.solid, planarIntent(20_mm),
                           {region(1U, "fixed_end", m.bottom()), region(2U, "loaded_end", m.top())});
    m.analysis = study(b, m.control, StructuralAnalysisMode::LinearStatic,
                       {pressureOn(1U, m.top(), kBlockPressurePa)}, {fixedOn(1U, m.bottom())});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-FEA-05 ---------------------------------------------------------------

Result<StructuralGravityBlockModel> buildStructuralGravityBlockReferenceModel() {
    StructuralGravityBlockModel m{
        .document = Document(detail::fixedDocumentId("17fea005-0000-4000-8000-000000000005"),
                             "StructuralGravityBlock")};
    detail::ModelBuilder b(m.document);

    // 50 x 35 x 80: a column, so the weight's moment about the origin has two
    // distinct non-zero components and the mean axial stress -rho g H / 2 is
    // not a small number.
    m.a = b.length("grav_a", 50.0);
    m.b = b.length("grav_b", 35.0);
    m.height = b.length("grav_h", 80.0);

    detail::SketchBuilder profile(b, "GravitySketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 50.0, 35.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "Column", {.profile = sketchId(m.sketch), .depth = 80_mm, .depthParameter = m.height});
    assignSteel(b);

    m.control = meshIntent(b, m.solid, planarIntent(20_mm),
                           {region(1U, "fixed_end", m.bottom()), region(2U, "free_end", m.top())});

    // Earth gravity along -Z, stated rather than assumed: the load carries the
    // acceleration and the material carries the density.
    m.analysis = study(b, m.control, StructuralAnalysisMode::LinearStaticWithGravity,
                       {StructuralLoad{LoadId::fromValue(1U),
                                       structural::GravityLoad{.acceleration = Vector3D{
                                                                   0.0, 0.0, -structural::kStandardGravity}}}},
                       {fixedOn(1U, m.bottom())});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-FEA-06 ---------------------------------------------------------------

Result<Frame3D> structuralTransformedFrame() {
    const RigidPlacement placement = structuralTransformedPlacement();
    // fromUnitComponents and fromAxes STORE the components unchanged after
    // checking them within 1e-12, so the frame holds exactly the thirds the
    // suite's own transform uses -- the same reasoning as RM-MESH-06.
    auto direction = [](const std::array<double, 3>& v) {
        return Direction3D::fromUnitComponents(v[0], v[1], v[2]);
    };
    const auto x = direction(placement.xAxis);
    const auto y = direction(placement.yAxis);
    const auto normal = direction(placement.normal);
    if (!x || !y || !normal) {
        return makeError(ErrorCode::InvalidArgument,
                         "RM-FEA-06's triad is not unit-length to within 1e-12");
    }
    return Frame3D::fromAxes(detail::pointMm(placement.origin[0], placement.origin[1], placement.origin[2]),
                             *x, *y, *normal);
}

namespace {

/// RM-FEA-06's two documents differ only in the plane their profile is drawn
/// on: one function, one set of dimensions, one load, one support.
Result<StructuralTransformedModel> buildTransformed(bool placed) {
    StructuralTransformedModel m{
        .document = Document(detail::fixedDocumentId(placed ? "17fea006-0000-4000-8000-000000000007"
                                                            : "17fea006-0000-4000-8000-000000000006"),
                             placed ? "StructuralTransformedPlaced" : "StructuralTransformedBase"),
        .placed = placed};
    detail::ModelBuilder b(m.document);

    // RM-MESH-06's 90 x 55 x 24, for the reason the header gives: on this
    // frame the two placements mesh to the SAME tetrahedralisation, which an
    // exact invariance claim requires and which an 80 x 50 x 30 block did
    // not get.
    const std::string prefix = placed ? "splaced" : "sbase";
    m.a = b.length(prefix + "_a", 90.0);
    m.b = b.length(prefix + "_b", 55.0);
    m.c = b.length(prefix + "_c", 24.0);

    Frame3D plane = Frame3D::xy();
    if (placed) {
        auto frame = structuralTransformedFrame();
        if (!frame) {
            return std::unexpected(frame.error());
        }
        plane = *frame;
    }

    detail::SketchBuilder profile(b, placed ? "PlacedSketch" : "BaseSketch", plane);
    const Rectangle rectangle = rectangleProfile(profile, 90.0, 55.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        placed ? "PlacedBlock" : "BaseBlock",
        {.profile = sketchId(m.sketch), .depth = 24_mm, .depthParameter = m.c});
    assignSteel(b);

    m.control = meshIntent(b, m.solid, planarIntent(16_mm),
                           {region(1U, "datum_face", m.datumFace()),
                            region(2U, "opposite_face", m.oppositeFace())});

    // A PRESSURE, which rotates with the face. See the header.
    m.analysis = study(b, m.control, StructuralAnalysisMode::LinearStatic,
                       {pressureOn(1U, m.oppositeFace(), kTransformedPressurePa)},
                       {fixedOn(1U, m.datumFace())});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace

Result<StructuralTransformedModel> buildStructuralTransformedBaseReferenceModel() {
    return buildTransformed(false);
}

Result<StructuralTransformedModel> buildStructuralTransformedPlacedReferenceModel() {
    return buildTransformed(true);
}

// --- RM-FEA-07 ---------------------------------------------------------------

namespace {

/// RM-FEA-07's two documents differ only in which cap carries the local
/// control.
Result<StructuralLocalRefinementModel> buildLocalRefinement(bool refinesTip) {
    StructuralLocalRefinementModel m{
        .document = Document(detail::fixedDocumentId(refinesTip ? "17fea007-0000-4000-8000-000000000009"
                                                                : "17fea007-0000-4000-8000-000000000008"),
                             refinesTip ? "StructuralLocalRefinementTip"
                                        : "StructuralLocalRefinementRoot"),
        .refinesTip = refinesTip};
    detail::ModelBuilder b(m.document);

    const std::string prefix = refinesTip ? "loct" : "loc";
    m.a = b.length(prefix + "_a", 40.0);
    m.b = b.length(prefix + "_b", 60.0);
    m.length = b.length(prefix + "_l", 100.0);

    detail::SketchBuilder profile(b, "LocalSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 40.0, 60.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "LocalBeam", {.profile = sketchId(m.sketch), .depth = 100_mm, .depthParameter = m.length});
    assignSteel(b);

    // 20 mm globally, 5 mm on ONE cap -- and the CROSS-SECTION is what makes
    // the control take effect, which was measured rather than assumed. Netgen
    // inserts interior points at the centres of local-size tree boxes that
    // lie wholly inside the body, so a slab refines only where boxes of the
    // requested size FIT inside the section. Measured on this beam with a
    // 5 mm control on the root cap, nodes of the whole mesh: 20 x 30 gives
    // 8 (the unrefined mesh, and at 4 mm and 10 mm too), 30 x 40 gives 14,
    // 40 x 40 gives 43, 40 x 60 gives 78. The global 20 mm is immaterial on a
    // box, because OCCT gives every planar face two triangles whatever the
    // target, so the contrast the suite measures is the slab against elements
    // that span the body.
    VolumeMeshControls controls = planarIntent(20_mm);
    controls.sizing.local.push_back(LocalMeshSizing{.face = m.refined(), .targetSize = 5_mm});

    m.control = meshIntent(b, m.solid, controls,
                           {region(1U, "root", m.root()), region(2U, "tip", m.tip())});

    // The same bending problem on both: fixed at the root, a uniform
    // transverse traction on the tip, t = -F / (a b).
    const double traction = -kLocalRefinementTipForceN / (0.040 * 0.060);
    m.analysis = study(b, m.control, StructuralAnalysisMode::LinearStatic,
                       {tractionOn(1U, m.tip(), tractionPa(0.0, traction, 0.0))},
                       {fixedOn(1U, m.root())});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace

Result<StructuralLocalRefinementModel> buildStructuralLocalRefinementRootReferenceModel() {
    return buildLocalRefinement(false);
}

Result<StructuralLocalRefinementModel> buildStructuralLocalRefinementTipReferenceModel() {
    return buildLocalRefinement(true);
}

// --- RM-FEA-08 ---------------------------------------------------------------

Result<StructuralUnderConstrainedModel> buildStructuralUnderConstrainedReferenceModel() {
    StructuralUnderConstrainedModel m{
        .document = Document(detail::fixedDocumentId("17fea008-0000-4000-8000-000000000010"),
                             "StructuralUnderConstrained")};
    detail::ModelBuilder b(m.document);

    m.a = b.length("uc_a", 60.0);
    m.b = b.length("uc_b", 40.0);
    m.c = b.length("uc_c", 25.0);

    detail::SketchBuilder profile(b, "UnderConstrainedSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 60.0, 40.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "LooseBlock", {.profile = sketchId(m.sketch), .depth = 25_mm, .depthParameter = m.c});
    assignSteel(b);

    m.control = meshIntent(b, m.solid, planarIntent(20_mm),
                           {region(1U, "bottom", m.bottom()), region(2U, "top", m.top()),
                            region(3U, "pushed_face", m.pushedFace())});

    // Uz on BOTH caps: eight constrained degrees of freedom on a planar
    // block's eight corner nodes, every one of them in z. The count passes;
    // x-translation, y-translation and rotation about z remain, and the
    // sideways push along +X excites the first of them.
    m.analysis = study(b, m.control, StructuralAnalysisMode::LinearStatic,
                       {tractionOn(1U, m.pushedFace(), tractionPa(1.0e5, 0.0, 0.0))},
                       {heldAlong(1U, m.bottom(), DofComponent::Uz),
                        heldAlong(2U, m.top(), DofComponent::Uz)});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-FEA-09 ---------------------------------------------------------------

Result<StructuralStaleMeshModel> buildStructuralStaleMeshReferenceModel() {
    StructuralStaleMeshModel m{
        .document = Document(detail::fixedDocumentId("17fea009-0000-4000-8000-000000000011"),
                             "StructuralStaleMesh")};
    detail::ModelBuilder b(m.document);

    m.a = b.length("stale_a", 50.0);
    m.b = b.length("stale_b", 40.0);
    m.c = b.length("stale_c", 30.0);

    detail::SketchBuilder profile(b, "StaleSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 50.0, 40.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "StaleBlock", {.profile = sketchId(m.sketch), .depth = 30_mm, .depthParameter = m.c});
    assignSteel(b);

    m.control = meshIntent(b, m.solid, planarIntent(20_mm),
                           {region(1U, "fixed_end", m.bottom()), region(2U, "loaded_end", m.top())});
    m.analysis = study(b, m.control, StructuralAnalysisMode::LinearStatic,
                       {pressureOn(1U, m.top(), 1.5e6)}, {fixedOn(1U, m.bottom())});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-FEA-10 ---------------------------------------------------------------

Result<StructuralUnresolvedModel> buildStructuralUnresolvedReferenceModel() {
    StructuralUnresolvedModel m{
        .document = Document(detail::fixedDocumentId("17fea010-0000-4000-8000-000000000012"),
                             "StructuralUnresolved")};
    detail::ModelBuilder b(m.document);

    m.a = b.length("unres_a", 50.0);
    m.b = b.length("unres_b", 40.0);
    m.c = b.length("unres_c", 30.0);

    detail::SketchBuilder profile(b, "UnresolvedSketch", Frame3D::xy());
    const Rectangle rectangle = rectangleProfile(profile, 50.0, 40.0, m.a, m.b);
    m.lines = rectangle.lines;
    m.sketch = profile.finish();

    m.solid = b.feature<features::ExtrudeFeature>(
        "UnresolvedBlock", {.profile = sketchId(m.sketch), .depth = 30_mm, .depthParameter = m.c});
    assignSteel(b);

    m.control = meshIntent(b, m.solid, planarIntent(20_mm),
                           {region(1U, "fixed_end", m.bottom()), region(2U, "loaded_end", m.top())});

    // A valid support and a valid pressure, AND a traction on a face that
    // does not exist. The record validates on its own terms, so the document
    // builds, saves and loads; the mesh is what cannot resolve it.
    m.analysis = study(b, m.control, StructuralAnalysisMode::LinearStatic,
                       {pressureOn(1U, m.top(), 1.0e6),
                        tractionOn(2U, m.missing(), tractionPa(0.0, 0.0, -1.0e5))},
                       {fixedOn(1U, m.bottom())});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
