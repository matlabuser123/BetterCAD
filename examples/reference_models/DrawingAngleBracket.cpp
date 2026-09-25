#include "BuildSupport.hpp"
#include "DrawingReferenceModels.hpp"
#include "DrawingSupport.hpp"

#include <bettercad/features/ExtrudeFeature.hpp>

#include <numbers>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::sketchId;

// RM-DWG-02 -- views that disagree, an angle, and a view that is NOT 1:1.
//
// A pentagonal prism: 120 long, 60 tall, 50 wide, with one corner cut back 40
// by 40 -- so the slant is exactly 45 degrees, and no two views show the same
// thing.
//
//   profile   120 x 60 rectangle less a 40 x 40 corner triangle
//             A = 7200 - 800 = 6400 mm^2
//   width     50
//                                       V = 320000 mm^3 exactly
//
// The 45 degrees is the point of the shape. An angular dimension reads
// 180 - angle(outward normals), so two parallel faces read 0 and this one
// must read 45.000 -- a number that is wrong the moment the convention is.
//
// The front view is drawn 1:2 while the sheet is 1:1, so scale is exercised
// in the one place it can hide: the view's own override. Its geometry halves;
// its lettering and its line weights do not.
[[nodiscard]] Result<DrawnAngleBracketModel> buildDrawnAngleBracketReferenceModel() {
    DrawnAngleBracketModel m{.document = Document(
                                 detail::fixedDocumentId("d4a70000-0000-4000-8000-000000000002"),
                                 "DrawnAngleBracket")};
    detail::ModelBuilder b(m.document);

    m.length = b.length("body_length", 120.0);
    m.height = b.length("body_height", 60.0);
    m.width = b.length("body_width", 50.0);
    m.shoulder = b.length("shoulder_height", 20.0);
    m.cutback = b.length("corner_cutback", 40.0);

    {
        detail::SketchBuilder s(b, "BodyProfile", Frame3D::xy());
        const EntityId p0 = s.point(0.0, 0.0);    // bottom left
        const EntityId p1 = s.point(120.0, 0.0);  // bottom right
        const EntityId p2 = s.point(120.0, 20.0); // up the right side
        const EntityId p3 = s.point(80.0, 60.0);  // the 45 degree slant
        const EntityId p4 = s.point(0.0, 60.0);   // top left
        m.profileLines = {s.line(p0, p1), s.line(p1, p2), s.line(p2, p3), s.line(p3, p4),
                          s.line(p4, p0)};
        s.fixed(p0);
        s.horizontal(m.profileLines[0]); // bottom
        s.vertical(m.profileLines[1]);   // right shoulder
        s.horizontal(m.profileLines[3]); // top
        s.vertical(m.profileLines[4]);   // left
        s.distance(p0, p1, m.length);
        s.distance(p0, p4, m.height);
        s.distance(p1, p2, m.shoulder);
        m.profileSketch = s.finish();
    }
    m.body = b.feature<features::ExtrudeFeature>(
        "Body", {.profile = sketchId(m.profileSketch), .depth = 50_mm, .depthParameter = m.width});

    // --- the drawing ---------------------------------------------------------
    detail::DrawingBuilder d(b);

    m.sheet = d.sheet("Sheet1", drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                                         .scale = {1, 1}});

    // 1:2, overriding the sheet. At half size the front view draws 60 x 25 on
    // paper rather than 120 x 50.
    m.front = d.view("Front", drawing::ViewDefinition{.sheet = m.sheet,
                                                      .source = ObjectReference{m.body},
                                                      .orientation = drawing::StandardView::Front,
                                                      .scale = drawing::DrawingScale{1, 2},
                                                      .placement = Point2D{260_mm, 210_mm}});
    m.top = d.view("Top", drawing::ViewDefinition{.kind = drawing::ViewKind::Projected,
                                                  .sheet = m.sheet,
                                                  .parent = m.front,
                                                  .direction = drawing::ProjectedDirection::Top,
                                                  .spacing = 70_mm});
    m.side = d.view("Side", drawing::ViewDefinition{.kind = drawing::ViewKind::Projected,
                                                    .sheet = m.sheet,
                                                    .parent = m.front,
                                                    .direction = drawing::ProjectedDirection::Right,
                                                    .spacing = 80_mm});

    const auto side = [&](std::size_t line) {
        return drawing::DimensionTarget{
            .plane = PlaneReference{
                .object = m.body,
                .face = FaceSelector{.role = FaceRole::Side, .entity = m.profileLines[line]}}};
    };

    // 120.000 mm, between the left face and the right shoulder -- both
    // normal to X, so the linear family accepts them.
    m.overall = d.dimension("Overall",
                            drawing::DimensionDefinition{.view = m.top,
                                                         .from = side(4),
                                                         .to = side(1),
                                                         .placement = Point2D{260_mm, 100_mm}});
    // 60.000 mm, between the bottom and the top.
    m.rise = d.dimension("Rise", drawing::DimensionDefinition{.view = m.top,
                                                              .from = side(0),
                                                              .to = side(3),
                                                              .placement = Point2D{180_mm, 140_mm}});
    // 45.000 degrees: the bottom face against the slant. Two parallel faces
    // would read 0, so this number IS the convention.
    m.corner = d.dimension("Corner",
                           drawing::DimensionDefinition{.view = m.top,
                                                        .type = drawing::DimensionType::Angular,
                                                        .from = side(0),
                                                        .to = side(2),
                                                        // An angular dimension is written in
                                                        // degrees; millimetres are refused
                                                        // rather than reinterpreted.
                                                        .format = drawing::DimensionFormat{.unit = "deg",
                                                                                           .showUnit = true},
                                                        .placement = Point2D{330_mm, 140_mm}});

    m.note = d.annotation("ScaleNote",
                          drawing::AnnotationDefinition{.view = m.front,
                                                        .type = drawing::AnnotationType::Note,
                                                        .text = "FRONT VIEW SCALE 1:2",
                                                        .placement = Point2D{120_mm, 250_mm}});

    // --- sheet 2: the slant, seen square ---------------------------------------
    //
    // A SECOND SHEET, in a second format and the other orientation, carrying
    // the one view none of the six standard directions can give: the 45
    // degree face is foreshortened in every orthographic view of this part,
    // so its true size is only visible looking along its own normal.
    //
    // The slant runs from (120, 20) to (80, 60) in the sketch plane, so its
    // direction is (-1, 1, 0)/sqrt(2) and its outward normal is
    // (1, 1, 0)/sqrt(2). The auxiliary view looks from there, with the
    // extrude's axis upright.
    m.secondSheet = d.sheet("Sheet2",
                            drawing::SheetDefinition{
                                .format = drawing::SheetFormat::A4,
                                .orientation = drawing::SheetOrientation::Portrait,
                                // A4 portrait is 210 x 297, and the bracket
                                // is 120 long: at 1:1 the front view alone
                                // would be more than half the page wide and
                                // the auxiliary beside it would run off. The
                                // whole sheet is 1:2, which is also what a
                                // second sheet of a detail drawing usually is.
                                .scale = {1, 2}});
    m.secondFront = d.view("Sheet2Front",
                           drawing::ViewDefinition{.sheet = m.secondSheet,
                                                   .source = ObjectReference{m.body},
                                                   .orientation = drawing::StandardView::Front,
                                                   .placement = Point2D{140_mm, 240_mm}});
    const double root2 = std::numbers::sqrt2;
    m.auxiliary = d.view(
        "SlantAuxiliary",
        drawing::ViewDefinition{
            .kind = drawing::ViewKind::Auxiliary,
            .sheet = m.secondSheet,
            .parent = m.secondFront,
            .auxiliary = drawing::ViewDirection{
                .normal = Direction3D::fromComponents(1.0 / root2, 1.0 / root2, 0.0)
                              .value_or(Direction3D::unitY()),
                .reference = Direction3D::unitZ()},
            .spacing = 80_mm});

    // 50.000 mm: the bracket's width, between the extrude's own two caps.
    //
    // ALIGNED, which measures the separation AS DRAWN -- projected into the
    // view plane. In this view the caps are separated along the view's own up
    // axis, so it reads the true 50 and equals what a linear dimension would
    // say. That agreement is the point: aligned and linear differ only where
    // the separation leaves the view plane, and a drawing that showed them
    // differing here would be drawing the width foreshortened.
    m.acrossSlant = d.dimension(
        "AcrossSlant",
        drawing::DimensionDefinition{
            .view = m.auxiliary,
            .type = drawing::DimensionType::Aligned,
            .from = {.plane = PlaneReference{.object = m.body,
                                              .face = FaceSelector{.role = FaceRole::StartCap}}},
            .to = {.plane = PlaneReference{.object = m.body,
                                            .face = FaceSelector{.role = FaceRole::EndCap}}},
            .placement = Point2D{60_mm, 195_mm}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
