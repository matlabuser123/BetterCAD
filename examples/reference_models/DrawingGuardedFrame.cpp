#include "AssemblyParts.hpp"
#include "BuildSupport.hpp"
#include "DrawingReferenceModels.hpp"
#include "DrawingSupport.hpp"

#include <bettercad/assembly/Configurations.hpp>
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

// RM-DWG-08 -- one drawing, two configurations, and everything that has to
// change between them.
//
//   Guarded   frame, guard and four bolts         3 rows, quantities 1, 1, 4
//   Open      the guard and two bolts suppressed   2 rows, quantities 1, 2
//
// THE ITEM NUMBER OF THE BOLT CHANGES, and that is correct rather than a
// defect. Item numbers are computed over the parts that are actually there,
// contiguously (ADR-022): in `Guarded` the bolt is item 3 behind the frame
// and the guard, and in `Open` -- where no guard is drawn -- it is item 2.
// A stored number could not do that, and a drawing carrying one would show
// a gap where item 2 used to be.
//
// THE GUARD'S BALLOON IS THE OTHER HALF. In `Open` the occurrence it names is
// not drawn, so the balloon must become UNRESOLVED. It must not move to the
// frame, and it must not quietly label one of the bolts, and it must come
// back exactly as it was when the configuration returns: A -> B -> A, with no
// drift.
[[nodiscard]] Result<DrawnGuardedFrameModel> buildDrawnGuardedFrameReferenceModel() {
    DrawnGuardedFrameModel m{.document = Document(
                                 detail::fixedDocumentId("d4a70000-0000-4000-8000-000000000008"),
                                 "DrawnGuardedFrame")};
    detail::ModelBuilder b(m.document);

    // --- the parts ------------------------------------------------------------
    //
    // Frame, then guard, then bolt: ascending part ObjectId is item order, so
    // that is 1, 2, 3 while all three are drawn.
    m.framePart = blockPart(b, "Frame", "frame", 100.0, 60.0, 12.0).solid;
    m.guardPart = blockPart(b, "Guard", "guard", 90.0, 50.0, 6.0).solid;
    m.boltPart = discPart(b, "Bolt", "bolt", 3.0, 20.0).solid;

    // --- the assembly ---------------------------------------------------------
    m.frame = place(b, "FrameBase", m.framePart);
    m.groundFrame = ground(b, "HoldFrame", m.frame);

    m.guard = place(b, "GuardPlate", m.guardPart, at(5.0, 5.0, DrawnGuardedFrameModel::kDeckMm));
    (void)ground(b, "HoldGuard", m.guard);

    constexpr std::array<std::pair<double, double>, DrawnGuardedFrameModel::kBoltCount> kBoltAt{
        {{12.0, 12.0}, {88.0, 12.0}, {12.0, 48.0}, {88.0, 48.0}}};
    for (std::size_t i = 0; i < DrawnGuardedFrameModel::kBoltCount; ++i) {
        m.bolts[i] = place(b, std::format("Bolt{}", i + 1), m.boltPart,
                           at(kBoltAt[i].first, kBoltAt[i].second, DrawnGuardedFrameModel::kDeckMm));
        (void)ground(b, std::format("HoldBolt{}", i + 1), m.bolts[i]);
    }

    // --- the configurations -----------------------------------------------------
    //
    // `Guarded` overrides nothing, so it is the base state under a name;
    // `Open` suppresses the guard and the two bolts behind it. Suppressing a
    // COMPONENT takes its mates with it (P13), so neither configuration is
    // over-constrained.
    m.guarded = b.need(m.document.createConfiguration("Guarded"), "Guarded");
    m.open = b.need(m.document.createConfiguration("Open"), "Open");
    b.need(assembly::suppressComponent(m.document, m.open, m.guard, true), "suppress guard");
    for (std::size_t i = DrawnGuardedFrameModel::kOpenBoltCount;
         i < DrawnGuardedFrameModel::kBoltCount; ++i) {
        b.need(assembly::suppressComponent(m.document, m.open, m.bolts[i], true),
               std::format("suppress bolt {}", i + 1));
    }

    // --- the drawing ------------------------------------------------------------
    detail::DrawingBuilder d(b);

    m.sheet = d.sheet("Sheet1", drawing::SheetDefinition{.format = drawing::SheetFormat::A3,
                                                         .scale = {1, 1}});

    // Guarded reaches x [0, 100], y [0, 60], z [0, 32]; Open reaches the same
    // in x and y and the same in z, because the bolts that stay are the tall
    // things. The view is placed for the larger of the two.
    m.front = d.view("Front", drawing::ViewDefinition{.sheet = m.sheet,
                                                      .subject = drawing::ViewSubject::Assembly,
                                                      .orientation = drawing::StandardView::Front,
                                                      .placement = Point2D{120_mm, 150_mm}});

    m.table = d.annotation("BomTable",
                           drawing::AnnotationDefinition{.view = m.front,
                                                          .type = drawing::AnnotationType::BomTable,
                                                          .placement = Point2D{280_mm, 240_mm}});

    const auto balloon = [&](const std::string& name, ComponentId occurrence, double x, double y) {
        return d.annotation(
            name, drawing::AnnotationDefinition{
                      .view = m.front,
                      .type = drawing::AnnotationType::Balloon,
                      .target = drawing::AnnotationTarget{
                          .object = ObjectId::fromValue(occurrence.value())},
                      .placement = Point2D{x * units::mm, y * units::mm}});
    };
    m.frameBalloon = balloon("FrameBalloon", m.frame, 60.0, 110.0);
    // The one that has to become unresolved in `Open`, and come back.
    m.guardBalloon = balloon("GuardBalloon", m.guard, 60.0, 200.0);

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
