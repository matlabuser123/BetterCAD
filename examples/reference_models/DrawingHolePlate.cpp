#include "BuildSupport.hpp"
#include "DrawingReferenceModels.hpp"
#include "DrawingSupport.hpp"

#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>

#include <cstdint>
#include <format>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

// RM-DWG-04 -- a hole PATTERN, and four identical holes a drawing must be
// able to tell apart.
//
//   V = 120*80*10 - 4 * pi*5^2*10 - pi*8^2*10
//     = 96000 - 1000 pi - 640 pi
//     = 96000 - 1640 pi
//     = 90847.788048112736 mm^3
//
// The four patterned bores sit at (20,15), (100,15), (20,55) and (100,55) --
// 80 apart along X and 40 apart along Y. Every one of those numbers is a
// PARAMETER, so driving `pitch_x` moves two of the four and leaves the other
// two where they are, which is what makes the regeneration check worth
// running.
//
// NOTHING HERE IS SYMMETRIC, and that is a correction the adversarial review
// forced. The pattern began at (20,20) on a 120 x 80 plate with the clearance
// hole at its centre -- which is symmetric about BOTH axes, so a mirrored or
// reflected top view would have drawn every circle exactly where the test
// expected to find one. The rows are now at y = 15 and 55 about a centre of
// 40, and the clearance hole is off both axes, so a reflection in either
// direction changes the picture.
//
// WHY AN EXTRUDE CUT AND NOT A HOLE FEATURE. A cut's side face is named by
// the sketch entity that swept it, and a pattern's copy of that face is named
// by the same entity plus the copy it belongs to -- so each of the four bores
// has a SEMANTIC NAME of its own, and a dimension or a centre mark can name
// exactly one of four identical circles. A HoleFeature's bore has no name at
// all (P12 names a hole's bottom and floors, never its wall). The clearance
// hole IS a HoleFeature, because a hole callout reads a hole, and because
// four identical bores are more interesting when there is a fifth that is
// not one of them.
[[nodiscard]] Result<DrawnHolePlateModel> buildDrawnHolePlateReferenceModel() {
    DrawnHolePlateModel m{.document = Document(
                              detail::fixedDocumentId("d4a70000-0000-4000-8000-000000000004"),
                              "DrawnHolePlate")};
    detail::ModelBuilder b(m.document);

    // --- the part -----------------------------------------------------------
    m.plateLength = b.length("plate_length", 120.0);
    m.plateWidth = b.length("plate_width", 80.0);
    m.plateThickness = b.length("plate_thickness", 10.0);
    m.boreRadius = b.length("bore_r", 5.0);
    m.pitchX = b.length("pitch_x", 80.0);
    m.pitchY = b.length("pitch_y", 40.0);
    m.clearanceDiameter = b.length("clearance_d", 16.0);

    {
        detail::SketchBuilder s(b, "PlateProfile", Frame3D::xy());
        const EntityId a = s.point(0.0, 0.0);
        const EntityId c = s.point(120.0, 0.0);
        const EntityId d = s.point(120.0, 80.0);
        const EntityId e = s.point(0.0, 80.0);
        m.plateLines = {s.line(a, c), s.line(c, d), s.line(d, e), s.line(e, a)};
        s.fixed(a);
        s.horizontal(m.plateLines[0]);
        s.vertical(m.plateLines[1]);
        s.horizontal(m.plateLines[2]);
        s.vertical(m.plateLines[3]);
        s.distance(a, c, m.plateLength);
        s.distance(a, e, m.plateWidth);
        m.plateSketch = s.finish();
    }
    m.plate = b.feature<features::ExtrudeFeature>(
        "Plate", {.profile = sketchId(m.plateSketch),
                  .depth = 10_mm,
                  .depthParameter = m.plateThickness});

    // The bore, sketched on the plate's top face so it follows the plate's
    // thickness, and cut THROUGH ALL rather than to a depth: a through hole
    // that happens to equal the thickness today would stop being one the
    // moment the plate got thicker.
    {
        detail::SketchBuilder s(b, "BoreProfile", detail::levelPlane(10.0));
        const EntityId centre = s.point(20.0, 15.0);
        m.boreCircle = s.circle(centre, 5.0);
        s.fixed(centre);
        s.radius(m.boreCircle, m.boreRadius);
        s.attach(PlaneReference{.object = m.plate,
                                .face = FaceSelector{.role = FaceRole::EndCap}});
        m.boreSketch = s.finish();
    }
    m.bore = b.feature<features::ExtrudeFeature>(
        "Bore", {.profile = sketchId(m.boreSketch),
                 .direction = features::ExtrudeDirection::Reversed,
                 .operation = features::FeatureOperation::Cut,
                 .target = featureId(m.plate),
                 .termination = features::ExtrudeTermination::ThroughAll});

    // A 2 x 2 grid. Instance index is first + second * 2, so 0 is the source
    // at (20,20), 1 is (100,20), 2 is (20,60) and 3 is (100,60) -- and a face
    // of copy N is named by the source's entity plus {Bores, N}.
    m.bores = b.feature<features::LinearPatternFeature>(
        "Bores", {.source = featureId(m.bore),
                  .first = {.direction = {1.0, 0.0, 0.0},
                            .count = 2,
                            .spacing = 80_mm,
                            .spacingParameter = m.pitchX},
                  .second = features::PatternDirection{.direction = {0.0, 1.0, 0.0},
                                                        .count = 2,
                                                        .spacing = 40_mm,
                                                        .spacingParameter = m.pitchY}});

    // The one hole that is not one of the four: a Ø16 clearance hole at the
    // centre of the plate, which carries the drawing's hole callout.
    m.clearance = b.feature<features::HoleFeature>(
        "Clearance", {.target = featureId(m.bores),
                      .face = geometry::planeSignature(pointMm(0.0, 0.0, 10.0),
                                                       Direction3D::unitZ()),
                      .center = Point2D{75_mm, 62_mm},
                      .extent = geometry::HoleExtent::Through,
                      .diameter = 16_mm,
                      .diameterParameter = m.clearanceDiameter});

    // --- the drawing ---------------------------------------------------------
    detail::DrawingBuilder d(b);

    m.sheet = d.sheet("Sheet1", drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                                         .scale = {1, 1}});

    // A3 landscape is 420 x 297. The plate view is 120 x 80 about its
    // bounding-box centre, so a placement of (150, 100) puts it in
    // x [90, 210], y [60, 140].
    m.top = d.view("Top", drawing::ViewDefinition{.sheet = m.sheet,
                                                  .source = ObjectReference{m.clearance},
                                                  .orientation = drawing::StandardView::Top,
                                                  .placement = Point2D{150_mm, 100_mm}});
    // Projecting BOTTOM from a Top parent turns the frame about the parent's
    // own horizontal axis, giving right = +X, up = +Z, normal = -Y: the front
    // view. First angle puts it on the far side, so it lands above the parent.
    m.front = d.view("Front",
                     drawing::ViewDefinition{.kind = drawing::ViewKind::Projected,
                                             .sheet = m.sheet,
                                             .parent = m.top,
                                             .direction = drawing::ProjectedDirection::Bottom,
                                             .spacing = 90_mm});

    const auto side = [&](std::size_t line) {
        return drawing::DimensionTarget{
            .plane = PlaneReference{
                .object = m.plate,
                .face = FaceSelector{.role = FaceRole::Side, .entity = m.plateLines[line]}}};
    };
    /// The cylindrical face of one bore: the source when @p copy is 0, and
    /// the pattern's copy of it otherwise.
    const auto boreFace = [&](std::uint32_t copy) {
        FaceSelector face{.role = FaceRole::Side, .entity = m.boreCircle};
        if (copy != 0) {
            face.copies.push_back(FaceCopy{.feature = m.bores, .instance = copy});
        }
        return FaceName{.feature = m.bore, .face = face};
    };

    // 120.000 mm along the plate, and 80.000 mm across it.
    //
    // HORIZONTAL and VERTICAL rather than LINEAR, and the difference is the
    // VIEW. A linear dimension measures the true distance in the model; these
    // measure the part of it that runs along the view's own axes, which in
    // the top view are +X and +Y. On this drawing the three agree, because
    // both separations lie in the view plane -- and that agreement is worth
    // asserting, because it is what says the view's axes are the ones the
    // sheet is drawn on.
    m.length = d.dimension("Length",
                           drawing::DimensionDefinition{.view = m.top,
                                                        .type = drawing::DimensionType::Horizontal,
                                                        .from = side(3),
                                                        .to = side(1),
                                                        .placement = Point2D{150_mm, 45_mm}});
    m.width = d.dimension("Width",
                          drawing::DimensionDefinition{.view = m.top,
                                                       .type = drawing::DimensionType::Vertical,
                                                       .from = side(0),
                                                       .to = side(2),
                                                       .placement = Point2D{60_mm, 100_mm}});

    // ORDINATES, from two datum edges. An ordinate is a SIGNED coordinate
    // along one of the view's own axes, which is what makes it a different
    // thing from a horizontal or a vertical dimension rather than a second
    // spelling of one: those two report a magnitude, and an ordinate says
    // which side of its datum the feature is on.
    //
    // Across from the left-hand end, and down from the top edge -- so one of
    // them is negative, and that is information rather than a fault.
    //
    // THESE DO NOT DIMENSION THE HOLES, and not for want of trying. A bore's
    // position is the single most common dimension on a machining drawing,
    // and it cannot be spelled here: a cylindrical face may be the target of
    // a radius or a diameter and of nothing else (P14-DIM-001), and a hole's
    // axis has no name of its own under ADR-012. The pattern's geometry is
    // therefore validated from the drawn sheet instead, against positions
    // computed from the pitch, and the gap is recorded with the milestone.
    m.ordinateAcross = d.dimension(
        "AcrossFromLeft",
        drawing::DimensionDefinition{.view = m.top,
                                     .type = drawing::DimensionType::Ordinate,
                                     .from = side(3),
                                     .to = side(1),
                                     .placement = Point2D{110_mm, 35_mm}});
    m.ordinateDown = d.dimension(
        "DownFromTop",
        drawing::DimensionDefinition{.view = m.top,
                                     .type = drawing::DimensionType::Ordinate,
                                     .from = side(2),
                                     .to = side(0),
                                     .ordinate = drawing::OrdinateAxis::Y,
                                     .placement = Point2D{75_mm, 145_mm}});

    // Ø10.000, twice: the source bore, and the copy diagonally opposite it.
    // Two dimensions that must read the same while naming different material
    // -- which a reference that had collapsed onto one face would also do, so
    // the test checks WHERE each one is drawn as well as what it says.
    m.boreDiameter = d.dimension(
        "BoreDiameter", drawing::DimensionDefinition{.view = m.top,
                                                     .type = drawing::DimensionType::Diameter,
                                                     .from = {.cylinder = boreFace(0)},
                                                     .placement = Point2D{265_mm, 70_mm}});
    m.farBoreDiameter = d.dimension(
        "FarBoreDiameter", drawing::DimensionDefinition{.view = m.top,
                                                        .type = drawing::DimensionType::Diameter,
                                                        .from = {.cylinder = boreFace(3)},
                                                        .placement = Point2D{265_mm, 130_mm}});
    // The same face as a RADIUS, which must be exactly half the diameter --
    // by the one path, so the two cannot come to disagree about one cylinder.
    m.boreRadiusDimension = d.dimension(
        "BoreRadius", drawing::DimensionDefinition{.view = m.top,
                                                   .type = drawing::DimensionType::Radius,
                                                   .from = {.cylinder = boreFace(0)},
                                                   .placement = Point2D{265_mm, 50_mm}});

    m.callout = d.annotation("ClearanceCallout",
                             drawing::AnnotationDefinition{
                                 .view = m.top,
                                 .type = drawing::AnnotationType::HoleCallout,
                                 .target = drawing::AnnotationTarget{.object = m.clearance},
                                 .placement = Point2D{250_mm, 100_mm}});

    // One centre mark per bore, in pattern-instance order. Four identical
    // circles; four different names; four marks that must land on four
    // different places.
    for (std::uint32_t copy = 0; copy < 4; ++copy) {
        m.centremarks[copy] = d.annotation(
            std::format("BoreMark{}", copy),
            drawing::AnnotationDefinition{
                .view = m.top,
                .type = drawing::AnnotationType::Centremark,
                .target = drawing::AnnotationTarget{.cylinder = boreFace(copy)},
                .placement = Point2D{150_mm, 100_mm}});
    }

    // A centreline must run along the axis ON THE SHEET, so it belongs in the
    // front view: in the top view the bore's axis points at the viewer and
    // would draw as a point, which that view is refused for.
    m.centreline = d.annotation(
        "BoreCentreline",
        drawing::AnnotationDefinition{.view = m.front,
                                      .type = drawing::AnnotationType::Centreline,
                                      .target = drawing::AnnotationTarget{.cylinder = boreFace(0)},
                                      .placement = Point2D{150_mm, 190_mm}});

    m.note = d.annotation("PatternNote",
                          drawing::AnnotationDefinition{
                              .view = m.top,
                              .type = drawing::AnnotationType::Note,
                              .text = "4 HOLES EQUALLY SPACED ON AN 80 x 40 PITCH",
                              .placement = Point2D{40_mm, 25_mm}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
