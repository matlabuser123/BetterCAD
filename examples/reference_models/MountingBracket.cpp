#include "BuildSupport.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

namespace {

/// One of the gusset's two sections: a rectangle gusset_back in from the
/// plate, @p thickness wide about the bracket's middle and @p depth deep,
/// on the XY plane. The loft lifts it by the section's offset.
ObjectId gussetSection(detail::ModelBuilder& b, const std::string& name, const MountingBracketModel& m,
                       ParameterId thickness, ParameterId depth, double thicknessMm, double depthMm) {
    detail::SketchBuilder s(b, name, Frame3D::xy());
    const EntityId origin = s.point(0.0, 0.0);
    const EntityId along = s.point(10.0, 0.0);
    const EntityId datum = s.line(origin, along);
    const EntityId centre = s.point(50.0, 5.0);
    s.construction(datum);
    s.fixed(origin);
    s.fixed(along);
    s.horizontal(origin, centre);
    s.distance(origin, centre, m.centreX);

    const double half = thicknessMm / 2.0;
    const EntityId a = s.point(50.0 - half, 5.0);
    const EntityId c = s.point(50.0 + half, 5.0);
    const EntityId d = s.point(50.0 + half, 5.0 + depthMm);
    const EntityId e = s.point(50.0 - half, 5.0 + depthMm);
    const EntityId back = s.line(a, c);
    const EntityId right = s.line(c, d);
    const EntityId front = s.line(d, e);
    const EntityId left = s.line(e, a);
    const EntityId leftHalf = s.line(a, centre);
    const EntityId rightHalf = s.line(centre, c);
    s.construction(leftHalf);
    s.construction(rightHalf);
    s.horizontal(back);
    s.vertical(right);
    s.horizontal(front);
    s.vertical(left);
    s.length(back, thickness);
    s.length(right, depth);
    s.equal(leftHalf, rightHalf);
    s.distance(a, datum, m.gussetBack);
    return s.finish();
}

} // namespace

Result<MountingBracketModel> buildMountingBracketReferenceModel() {
    MountingBracketModel m{
        .document = Document(detail::fixedDocumentId("5eed0000-0000-4000-8000-000000000005"), "MountingBracket")};
    detail::ModelBuilder b(m.document);

    // An L bracket: a 100 × 60 × 10 mm base with a 100 × 10 × 70 mm plate up
    // its back edge, four Ø8 holes in the base and two in the plate, the
    // inside corner rounded, the outside broken, and a tapered gusset lofted
    // between them.
    m.width = b.length("width", 100.0);
    m.baseWidth = b.length("base_width", 60.0);
    m.baseThickness = b.length("base_t", 10.0);
    m.plateHeight = b.length("plate_height", 70.0);
    m.plateThickness = b.length("plate_t", 10.0);
    m.holeDiameter = b.length("hole_d", 8.0);
    m.holeInset = b.length("hole_inset", 15.0);
    m.holeRow = b.length("hole_row", 25.0);
    m.holePitchX = b.length("hole_pitch_x", 70.0);
    m.holePitchY = b.length("hole_pitch_y", 20.0);
    m.plateHoleX = b.length("plate_hole_x", 20.0);
    m.plateHoleZ = b.length("plate_hole_z", 45.0);
    m.centreX = b.length("centre_x", 50.0);
    m.gussetBack = b.length("gusset_back", 5.0);
    m.gussetEmbed = b.length("gusset_embed", 5.0);
    m.gussetTop = b.length("gusset_top", 50.0);
    m.gussetFootThickness = b.length("gusset_foot_t", 10.0);
    m.gussetFootDepth = b.length("gusset_foot_depth", 45.0);
    m.gussetTipThickness = b.length("gusset_tip_t", 6.0);
    m.gussetTipDepth = b.length("gusset_tip_depth", 7.0);
    m.filletRadius = b.length("inner_fillet_r", 5.0);
    m.chamferSize = b.length("outer_chamfer", 2.0);

    // The base plate, lying on the XY plane, and the back plate standing on
    // its back edge.
    detail::SketchBuilder base(b, "BaseSection", Frame3D::xy());
    const EntityId p0 = base.point(0.0, 0.0);
    const EntityId p1 = base.point(100.0, 0.0);
    const EntityId p2 = base.point(100.0, 60.0);
    const EntityId p3 = base.point(0.0, 60.0);
    const EntityId baseBack = base.line(p0, p1);
    const EntityId baseRight = base.line(p1, p2);
    const EntityId baseFront = base.line(p2, p3);
    const EntityId baseLeft = base.line(p3, p0);
    base.fixed(p0);
    base.horizontal(baseBack);
    base.vertical(baseRight);
    base.horizontal(baseFront);
    base.vertical(baseLeft);
    base.length(baseBack, m.width);
    base.length(baseRight, m.baseWidth);
    m.baseSection = base.finish();
    m.basePlate = b.feature<features::ExtrudeFeature>(
        "BasePlate", {.profile = sketchId(m.baseSection), .depth = 10_mm, .depthParameter = m.baseThickness});

    detail::SketchBuilder plate(b, "PlateSection", Frame3D::xz());
    const EntityId r0 = plate.point(0.0, 0.0);
    const EntityId r1 = plate.point(100.0, 0.0);
    const EntityId r2 = plate.point(100.0, 70.0);
    const EntityId r3 = plate.point(0.0, 70.0);
    const EntityId plateBottom = plate.line(r0, r1);
    const EntityId plateRight = plate.line(r1, r2);
    const EntityId plateTop = plate.line(r2, r3);
    const EntityId plateLeft = plate.line(r3, r0);
    plate.fixed(r0);
    plate.horizontal(plateBottom);
    plate.vertical(plateRight);
    plate.horizontal(plateTop);
    plate.vertical(plateLeft);
    plate.length(plateBottom, m.width);
    plate.length(plateRight, m.plateHeight);
    m.plateSection = plate.finish();
    m.backPlate = b.feature<features::ExtrudeFeature>("BackPlate",
                                                      {.profile = sketchId(m.plateSection),
                                                       .depth = 10_mm,
                                                       .depthParameter = m.plateThickness,
                                                       .direction = features::ExtrudeDirection::Reversed,
                                                       .operation = features::FeatureOperation::Join,
                                                       .target = featureId(m.basePlate)});

    // Four mounting holes in the base, as a 2 × 2 grid, and two in the
    // plate, one drilled and one mirrored across the bracket's middle.
    m.baseHole = b.feature<features::HoleFeature>(
        "BaseHole", {.target = featureId(m.backPlate),
                     .face = geometry::planeSignature(pointMm(0.0, 0.0, 10.0), Direction3D::unitZ()),
                     .center = Point2D{15_mm, 25_mm},
                     .centerUParameter = m.holeInset,
                     .centerVParameter = m.holeRow,
                     .diameter = 8_mm,
                     .diameterParameter = m.holeDiameter});
    m.baseHoles = b.feature<features::LinearPatternFeature>(
        "BaseHoles", {.source = featureId(m.baseHole),
                      .first = {.direction = {1.0, 0.0, 0.0},
                                .count = 2,
                                .spacing = 70_mm,
                                .spacingParameter = m.holePitchX},
                      .second = features::PatternDirection{.direction = {0.0, 1.0, 0.0},
                                                           .count = 2,
                                                           .spacing = 20_mm,
                                                           .spacingParameter = m.holePitchY}});
    m.plateHole = b.feature<features::HoleFeature>(
        "PlateHole", {.target = featureId(m.baseHoles),
                      .face = geometry::planeSignature(pointMm(0.0, 10.0, 0.0), Direction3D::unitY()),
                      .center = Point2D{20_mm, 45_mm},
                      .centerUParameter = m.plateHoleX,
                      .centerVParameter = m.plateHoleZ,
                      .diameter = 8_mm,
                      .diameterParameter = m.holeDiameter});
    m.plateHoles = b.feature<features::MirrorFeature>("PlateHoles",
                                                      {.source = featureId(m.plateHole),
                                                       .plane = {.origin = pointMm(0.0, 0.0, 0.0),
                                                                 .normal = {1.0, 0.0, 0.0},
                                                                 .offset = 50_mm,
                                                                 .offsetParameter = m.centreX}});

    // Round the inside corner and break the outside one, both the full width
    // of the bracket.
    m.innerFillet = b.feature<features::FilletFeature>(
        "InnerFillet", {.target = featureId(m.plateHoles),
                        .edges = {geometry::lineSignature(pointMm(0.0, 10.0, 10.0), Direction3D::unitX())},
                        .radius = 5_mm,
                        .radiusParameter = m.filletRadius});
    m.outerChamfer = b.feature<features::ChamferFeature>(
        "OuterChamfer", {.target = featureId(m.innerFillet),
                         .edges = { features::ChamferEdge{geometry::lineSignature(pointMm(0.0, 0.0, 0.0), Direction3D::unitX())}},
                         .distance = 2_mm,
                         .distanceParameter = m.chamferSize});

    // The gusset: a tapered rib lofted from a wide section inside the base to
    // a narrow one part way up the plate. Both sections reach into the
    // plates, so the join has no faces that only touch.
    m.gussetFoot = gussetSection(b, "GussetFoot", m, m.gussetFootThickness, m.gussetFootDepth, 10.0, 45.0);
    m.gussetTip = gussetSection(b, "GussetTip", m, m.gussetTipThickness, m.gussetTipDepth, 6.0, 7.0);
    m.gusset = b.feature<features::LoftFeature>(
        "Gusset", {.sections = {{.sketch = sketchId(m.gussetFoot), .offset = 5_mm, .offsetParameter = m.gussetEmbed},
                                {.sketch = sketchId(m.gussetTip), .offset = 50_mm, .offsetParameter = m.gussetTop}},
                   .operation = features::FeatureOperation::Join,
                   .target = featureId(m.outerChamfer)});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
