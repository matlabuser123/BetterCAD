#include "AssemblyParts.hpp"
#include "BuildSupport.hpp"
#include "DrawingReferenceModels.hpp"
#include "DrawingSupport.hpp"

#include <bettercad/core/document/MateReference.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <utility>

namespace bettercad::reference {

using namespace bettercad::literals;
using detail::at;
using detail::blockPart;
using detail::discPart;
using detail::ground;
using detail::place;

// RM-DWG-07 -- a bill of materials, and the balloons that cite it.
//
//   Bracket  90 x 60 x 12   x1
//   Bolt     Ø8 x 24        x4   ONE part definition, four occurrences
//   Spacer   Ø18 x 10       x2   ONE part definition, two occurrences
//
// Seven occurrences of three part definitions, so the BOM has three rows with
// quantities 1, 4 and 2, and `totalOccurrences()` is 7. Item numbers run over
// ascending part ObjectId, and the parts are created bracket, bolt, spacer --
// so the bracket is item 1, the bolt item 2 and the spacer item 3. That is
// derivable from the builder and is derived again, independently, in the
// tests.
//
// THE FOUR BOLTS ARE THE POINT. They are identical instances of one part, in
// four places, and a balloon has to name the OCCURRENCE rather than the part:
// two of them carry balloons, and the two balloons must show the same item
// number while landing on different components. A balloon that had collapsed
// onto the part would show the same number in the same place twice and look
// entirely correct.
//
// Every component is grounded, so the solved transforms are the placements
// and a balloon's position is arithmetic rather than a solver output.
[[nodiscard]] Result<DrawnBoltedStackModel> buildDrawnBoltedStackReferenceModel() {
    DrawnBoltedStackModel m{.document = Document(
                                detail::fixedDocumentId("d4a70000-0000-4000-8000-000000000007"),
                                "DrawnBoltedStack")};
    detail::ModelBuilder b(m.document);

    // --- the parts ------------------------------------------------------------
    //
    // Created in this order, and the order is load-bearing: item numbers run
    // over ascending part ObjectId, so bracket, bolt, spacer become items 1,
    // 2 and 3. Nothing stores those numbers (ADR-022) -- they are computed
    // from the assembly every time it is asked.
    m.bracketPart = blockPart(b, "Bracket", "bracket", 90.0, 60.0, 12.0).solid;
    m.boltPart = discPart(b, "Bolt", "bolt", 4.0, 24.0).solid;
    m.spacerPart = discPart(b, "Spacer", "spacer", 9.0, 10.0).solid;

    // --- the assembly ---------------------------------------------------------
    m.bracket = place(b, "BracketPlate", m.bracketPart);
    m.groundBracket = ground(b, "HoldBracket", m.bracket);

    // Four bolts at the corners of the bolt circle, two spacers between them.
    constexpr std::array<std::pair<double, double>, DrawnBoltedStackModel::kBoltCount> kBoltAt{
        {{15.0, 15.0}, {75.0, 15.0}, {15.0, 45.0}, {75.0, 45.0}}};
    for (std::size_t i = 0; i < DrawnBoltedStackModel::kBoltCount; ++i) {
        m.bolts[i] = place(b, std::format("Bolt{}", i + 1), m.boltPart,
                           at(kBoltAt[i].first, kBoltAt[i].second, DrawnBoltedStackModel::kDeckMm));
        (void)ground(b, std::format("HoldBolt{}", i + 1), m.bolts[i]);
    }
    constexpr std::array<std::pair<double, double>, DrawnBoltedStackModel::kSpacerCount> kSpacerAt{
        {{45.0, 15.0}, {45.0, 45.0}}};
    for (std::size_t i = 0; i < DrawnBoltedStackModel::kSpacerCount; ++i) {
        m.spacers[i] =
            place(b, std::format("Spacer{}", i + 1), m.spacerPart,
                  at(kSpacerAt[i].first, kSpacerAt[i].second, DrawnBoltedStackModel::kDeckMm));
        (void)ground(b, std::format("HoldSpacer{}", i + 1), m.spacers[i]);
    }

    // --- the drawing ------------------------------------------------------------
    detail::DrawingBuilder d(b);

    m.sheet = d.sheet("Sheet1", drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                                         .scale = {1, 1}});

    // The stack reaches x [0, 90], y [0, 60], z [0, 36], so the front view is
    // 90 wide and 36 tall about its centre.
    m.front = d.view("Front", drawing::ViewDefinition{.sheet = m.sheet,
                                                      .subject = drawing::ViewSubject::Assembly,
                                                      .orientation = drawing::StandardView::Front,
                                                      .placement = Point2D{120_mm, 150_mm}});

    // The table says what the assembly contains. It stores its LAYOUT and
    // nothing else: the rows, the quantities and the item numbers are
    // computed from the assembly every time it is drawn.
    m.table = d.annotation("BomTable",
                           drawing::AnnotationDefinition{.view = m.front,
                                                          .type = drawing::AnnotationType::BomTable,
                                                          .placement = Point2D{280_mm, 240_mm}});

    const auto balloon = [&](const std::string& name, ComponentId occurrence, double x, double y) {
        return d.annotation(
            name, drawing::AnnotationDefinition{
                      .view = m.front,
                      .type = drawing::AnnotationType::Balloon,
                      .target = drawing::AnnotationTarget{.object = ObjectId::fromValue(occurrence.value())},
                      .placement = Point2D{x * units::mm, y * units::mm}});
    };
    m.bracketBalloon = balloon("BracketBalloon", m.bracket, 60.0, 110.0);
    // Two balloons on two DIFFERENT occurrences of the SAME part. They must
    // show one item number and land in two places.
    m.boltBalloonA = balloon("BoltBalloonA", m.bolts[0], 60.0, 200.0);
    m.boltBalloonB = balloon("BoltBalloonB", m.bolts[3], 190.0, 200.0);
    m.spacerBalloon = balloon("SpacerBalloon", m.spacers[0], 190.0, 110.0);

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
