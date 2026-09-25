#include "AssemblyParts.hpp"
#include "BuildSupport.hpp"
#include "DrawingReferenceModels.hpp"
#include "DrawingSupport.hpp"

#include <bettercad/core/document/MateReference.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::at;
using detail::blockPart;
using detail::discPart;
using detail::ground;
using detail::place;
using detail::sketchId;

// RM-DWG-06 -- an ASSEMBLY drawing, and the three things one has that a part
// drawing does not.
//
//   repeated parts   two jaws, ONE part definition, two occurrences
//   a rotated part   the key is turned 35 degrees about Z by its placement
//   occlusion        the jaws and the pin stand behind the body's front face,
//                    so edges they have are hidden BY ANOTHER COMPONENT
//
// Five occurrences of four part definitions:
//
//   Body  90 x 50 x 15   grounded at the origin
//   Jaw   20 x 30 x 35   placed twice, at x = 12 and x = 58, on the deck
//   Pin   Ø14 x 40       standing between the jaws, on the deck
//   Key   60 x 10 x 8    across the top, turned 35 degrees
//
// EVERY COMPONENT IS FULLY CONSTRAINED, deliberately. An under-constrained
// assembly still solves and still draws, but its transforms would depend on
// where the solver happened to start, and a reference model that could move
// between runs is not a reference. The jaws are located by five mates each;
// the other three are held where they are put.
//
// THE KEY IS THE ONE THAT CATCHES A MIRROR. A 35 degree rotation is visible
// from either side but reads differently, so a transposed or reflected top
// view cannot look correct -- which a cylinder spun about its own axis
// could not tell anybody, being unchanged by it.
[[nodiscard]] Result<DrawnClampSetModel> buildDrawnClampSetReferenceModel() {
    DrawnClampSetModel m{.document = Document(
                             detail::fixedDocumentId("d4a70000-0000-4000-8000-000000000006"),
                             "DrawnClampSet")};
    detail::ModelBuilder b(m.document);

    // --- the parts ------------------------------------------------------------
    //
    // The body is built here rather than by blockPart() for one reason: the
    // drawing dimensions its ends, and a SIDE face is named by the sketch
    // entity that swept it, so the builder has to keep those entity IDs.
    m.bodyLength = b.length("body_length", 90.0);
    const ParameterId bodyWidth = b.length("body_width", 50.0);
    const ParameterId bodyHeight = b.length("body_height", DrawnClampSetModel::kDeckMm);
    ObjectId bodySketch{};
    {
        detail::SketchBuilder s(b, "BodyProfile", Frame3D::xy());
        const EntityId a = s.point(0.0, 0.0);
        const EntityId c = s.point(90.0, 0.0);
        const EntityId d = s.point(90.0, 50.0);
        const EntityId e = s.point(0.0, 50.0);
        m.bodyLines = {s.line(a, c), s.line(c, d), s.line(d, e), s.line(e, a)};
        s.fixed(a);
        s.horizontal(m.bodyLines[0]);
        s.vertical(m.bodyLines[1]);
        s.horizontal(m.bodyLines[2]);
        s.vertical(m.bodyLines[3]);
        s.distance(a, c, m.bodyLength);
        s.distance(a, e, bodyWidth);
        bodySketch = s.finish();
    }
    m.bodyPart = b.feature<features::ExtrudeFeature>(
        "Body", {.profile = sketchId(bodySketch),
                 .depth = 15_mm,
                 .depthParameter = bodyHeight});

    m.jawPart = blockPart(b, "Jaw", "jaw", 20.0, 30.0, 35.0).solid;
    m.pinPart = discPart(b, "Pin", "pin", 7.0, 40.0).solid;
    const ObjectId keyPart = blockPart(b, "Key", "key", 60.0, 10.0, 8.0).solid;

    // --- the assembly ---------------------------------------------------------
    m.body = place(b, "BodyBlock", m.bodyPart);
    // Neither jaw starts where its mates will put it, so the solve has to do
    // the work rather than agreeing with a placement that was already right.
    m.jawLeft = place(b, "JawLeft", m.jawPart, at(4.0, 3.0, 60.0, 11.0));
    m.jawRight = place(b, "JawRight", m.jawPart, at(80.0, 44.0, 5.0, -17.0));
    m.pin = place(b, "PinRod", m.pinPart, at(45.0, 25.0, DrawnClampSetModel::kDeckMm));
    const ComponentId key =
        place(b, "KeyBar", keyPart, at(15.0, 4.0, 55.0, DrawnClampSetModel::kPinSpinDeg));

    m.groundBody = ground(b, "HoldBody", m.body);
    detail::locate(b, "JawL", m.body, m.jawLeft, DrawnClampSetModel::kJawLeftXMm,
                   DrawnClampSetModel::kJawYMm, DrawnClampSetModel::kDeckMm);
    detail::locate(b, "JawR", m.body, m.jawRight, DrawnClampSetModel::kJawRightXMm,
                   DrawnClampSetModel::kJawYMm, DrawnClampSetModel::kDeckMm);
    (void)ground(b, "HoldPin", m.pin);
    (void)ground(b, "HoldKey", key);

    // --- the drawing ------------------------------------------------------------
    detail::DrawingBuilder d(b);

    m.sheet = d.sheet("Sheet1", drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                                         .scale = {1, 1}});

    // The assembly reaches x [0, 90], y [0, 50], z [0, 63], so the front view
    // is 90 x 63 and the top view below it is 90 x 50.
    m.front = d.view("Front", drawing::ViewDefinition{.sheet = m.sheet,
                                                      .subject = drawing::ViewSubject::Assembly,
                                                      .orientation = drawing::StandardView::Front,
                                                      .placement = Point2D{130_mm, 200_mm}});
    m.top = d.view("Top", drawing::ViewDefinition{.kind = drawing::ViewKind::Projected,
                                                  .sheet = m.sheet,
                                                  .parent = m.front,
                                                  .direction = drawing::ProjectedDirection::Top,
                                                  .spacing = 100_mm});

    // 90.000 mm along the body, between the two SIDE faces its short sketch
    // lines swept. The body is grounded at the origin, so its part space and
    // the assembly's are the same and the measurement is unambiguous.
    const auto side = [&](std::size_t line) {
        return drawing::DimensionTarget{
            .plane = PlaneReference{
                .object = m.bodyPart,
                .face = FaceSelector{.role = FaceRole::Side, .entity = m.bodyLines[line]}}};
    };
    m.bodyLengthDimension =
        d.dimension("BodyLength", drawing::DimensionDefinition{.view = m.front,
                                                               .from = side(3),
                                                               .to = side(1),
                                                               .placement = Point2D{130_mm, 155_mm}});

    m.note = d.annotation("AssemblyNote",
                          drawing::AnnotationDefinition{
                              .view = m.front,
                              .type = drawing::AnnotationType::Note,
                              .text = "SHOWN IN THE ASSEMBLED CONDITION",
                              .placement = Point2D{40_mm, 265_mm}});

    // A LEADER, which is the other half of "words on a drawing": a note
    // points at nothing and a leader points at something. This one names the
    // pin OCCURRENCE, so its arrow lands where the solver put that component
    // and moves when the assembly does.
    m.pinNote = d.annotation(
        "PinNote",
        drawing::AnnotationDefinition{
            .view = m.front,
            .type = drawing::AnnotationType::Leader,
            .target = drawing::AnnotationTarget{.object = ObjectId::fromValue(m.pin.value())},
            .text = "PRESS FIT",
            .placement = Point2D{300_mm, 245_mm}});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
