#include "BuildSupport.hpp"
#include "DrawingReferenceModels.hpp"
#include "DrawingSupport.hpp"

#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::featureId;
using detail::pointMm;
using detail::sketchId;

// RM-DWG-01 -- the simplest complete drawing, and the suite's smoke test.
//
// A 100 x 60 x 20 plate with a 40 x 20 x 10 step on one corner and a Ø12 hole
// through the plate beside it. Every number is chosen so the answer is
// arithmetic:
//
//   V = 100*60*20 + 40*20*10 - pi*6^2*20
//     = 120000 + 8000 - 720 pi
//     = 125738.053289415349 mm^3
//
// ASYMMETRIC ON PURPOSE. The step sits on one corner and the hole on the
// other side of the plate, so no view of it is symmetric about either axis. A
// mirrored projection, a forgotten flip or a transposed pair of axes all
// change the picture; on a symmetric part every one of them looks correct.
//
// Three views, because the point of the model is that the whole pipeline runs:
// a base view, one projected below it and one beside it, two dimensions
// between NAMED faces, a hole callout and a note.
[[nodiscard]] Result<DrawnStepPlateModel> buildDrawnStepPlateReferenceModel() {
    DrawnStepPlateModel m{.document = Document(
                              detail::fixedDocumentId("d4a70000-0000-4000-8000-000000000001"),
                              "DrawnStepPlate")};
    detail::ModelBuilder b(m.document);

    // --- the part -----------------------------------------------------------
    m.plateLength = b.length("plate_length", 100.0);
    m.plateWidth = b.length("plate_width", 60.0);
    m.plateThickness = b.length("plate_thickness", 20.0);
    m.stepLength = b.length("step_length", 40.0);
    m.stepWidth = b.length("step_width", 20.0);
    m.stepHeight = b.length("step_height", 10.0);
    m.holeDiameter = b.length("hole_d", 12.0);

    {
        detail::SketchBuilder s(b, "PlateProfile", Frame3D::xy());
        const EntityId a = s.point(0.0, 0.0);
        const EntityId c = s.point(100.0, 0.0);
        const EntityId d = s.point(100.0, 60.0);
        const EntityId e = s.point(0.0, 60.0);
        // The four profile lines are kept because a SIDE face is named by the
        // sketch entity that swept it -- that is how a dimension reaches the
        // ends of the plate (ADR-012).
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
                  .depth = 20_mm,
                  .depthParameter = m.plateThickness});

    // The step sits on the plate's top face, in one corner.
    {
        detail::SketchBuilder s(b, "StepProfile", detail::levelPlane(20.0));
        const EntityId a = s.point(60.0, 40.0);
        const EntityId c = s.point(100.0, 40.0);
        const EntityId d = s.point(100.0, 60.0);
        const EntityId e = s.point(60.0, 60.0);
        const EntityId bottom = s.line(a, c);
        const EntityId right = s.line(c, d);
        const EntityId top = s.line(d, e);
        const EntityId left = s.line(e, a);
        s.fixed(a);
        s.horizontal(bottom);
        s.vertical(right);
        s.horizontal(top);
        s.vertical(left);
        s.distance(a, c, m.stepLength);
        s.distance(a, e, m.stepWidth);
        s.attach(PlaneReference{.object = m.plate,
                                .face = FaceSelector{.role = FaceRole::EndCap}});
        m.stepSketch = s.finish();
    }
    m.step = b.feature<features::ExtrudeFeature>(
        "Step", {.profile = sketchId(m.stepSketch),
                 .depth = 10_mm,
                 .depthParameter = m.stepHeight,
                 .operation = features::FeatureOperation::Join,
                 .target = featureId(m.plate)});

    // Through the plate, well clear of the step, so its depth is the plate's
    // thickness and the volume above is exact.
    m.hole = b.feature<features::HoleFeature>(
        "Bore", {.target = featureId(m.step),
                 .face = geometry::planeSignature(pointMm(0.0, 0.0, 20.0), Direction3D::unitZ()),
                 .center = Point2D{25_mm, 25_mm},
                 .extent = geometry::HoleExtent::Through,
                 .diameter = 12_mm,
                 .diameterParameter = m.holeDiameter});

    // --- the drawing ---------------------------------------------------------
    detail::DrawingBuilder d(b);

    m.sheet = d.sheet("Sheet1", drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                                         .scale = {1, 1}});

    // A3 landscape is 420 x 297. The front view draws 100 wide (X) by 30 tall
    // (Z, plate plus step), so it is placed right of centre to leave room for
    // the side view, which first-angle projection puts on the LEFT.
    m.front = d.view("Front", drawing::ViewDefinition{.sheet = m.sheet,
                                                      .source = ObjectReference{m.hole},
                                                      .orientation = drawing::StandardView::Front,
                                                      .placement = Point2D{240_mm, 200_mm}});
    m.top = d.view("Top", drawing::ViewDefinition{.kind = drawing::ViewKind::Projected,
                                                  .sheet = m.sheet,
                                                  .parent = m.front,
                                                  .direction = drawing::ProjectedDirection::Top,
                                                  .spacing = 80_mm});
    m.right = d.view("Right", drawing::ViewDefinition{.kind = drawing::ViewKind::Projected,
                                                      .sheet = m.sheet,
                                                      .parent = m.front,
                                                      .direction = drawing::ProjectedDirection::Right,
                                                      .spacing = 90_mm});

    // Two dimensions, each between a pair of PARALLEL named faces -- the only
    // thing the linear family accepts.
    const auto side = [&](std::size_t line) {
        return drawing::DimensionTarget{
            .plane = PlaneReference{
                .object = m.plate,
                .face = FaceSelector{.role = FaceRole::Side, .entity = m.plateLines[line]}}};
    };
    const auto cap = [&](FaceRole role) {
        return drawing::DimensionTarget{
            .plane = PlaneReference{.object = m.plate, .face = FaceSelector{.role = role}}};
    };

    // The plate's length: between the two SIDE faces the short sketch lines
    // swept, which are the ends of the plate. 100.000 mm.
    m.length = d.dimension("Length", drawing::DimensionDefinition{.view = m.front,
                                                                  .from = side(3),
                                                                  .to = side(1),
                                                                  .placement = Point2D{240_mm, 170_mm}});
    // Its thickness: the extrude's own two caps. 20.000 mm.
    m.thickness = d.dimension("Thickness",
                              drawing::DimensionDefinition{.view = m.right,
                                                           .from = cap(FaceRole::StartCap),
                                                           .to = cap(FaceRole::EndCap),
                                                           .placement = Point2D{120_mm, 230_mm}});

    // The hole's diameter reaches the drawing as a CALLOUT against the hole
    // feature, because a bore is not a named face: P12 names a hole's bottom
    // and floors, never its wall (ADR-012).
    m.callout = d.annotation("BoreCallout",
                             drawing::AnnotationDefinition{.view = m.top,
                                                           .type = drawing::AnnotationType::HoleCallout,
                                                           .target = drawing::AnnotationTarget{.object = m.hole},
                                                           .placement = Point2D{180_mm, 90_mm}});
    m.note = d.annotation("GeneralNote",
                          drawing::AnnotationDefinition{.view = m.front,
                                                        .type = drawing::AnnotationType::Note,
                                                        .text = "BREAK SHARP EDGES 0.3 MAX",
                                                        .placement = Point2D{60_mm, 40_mm}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
