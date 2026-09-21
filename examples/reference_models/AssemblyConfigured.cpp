#include "AssemblyParts.hpp"
#include "AssemblyReferenceModels.hpp"

#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/core/document/MateReference.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using assembly::MateType;
using detail::at;
using detail::blockPart;
using detail::ground;
using detail::mate;
using detail::place;

// --- RM-E: ConfiguredFrame ------------------------------------------------------

Result<ConfiguredFrameModel> buildConfiguredFrameReferenceModel() {
    ConfiguredFrameModel m{.document = Document(detail::fixedDocumentId("a55e0000-0000-4000-8000-000000000005"),
                                                "ConfiguredFrame")};
    detail::ModelBuilder b(m.document);

    m.basePart = blockPart(b, "Bed", "bed", 100.0, 60.0, ConfiguredFrameModel::kDeckMm).solid;
    m.postPart = blockPart(b, "Post", "post", 15.0, 15.0, 50.0).solid;
    m.bracePart = blockPart(b, "Brace", "brace", 70.0, 10.0, 8.0).solid;

    m.base = place(b, "Bed1", m.basePart);
    // Two posts of one part, and a brace across them.
    m.postLeft = place(b, "PostLeft", m.postPart, at(2.0, 2.0, 40.0, 8.0));
    m.postRight = place(b, "PostRight", m.postPart, at(95.0, 50.0, 3.0, -14.0));
    m.brace = place(b, "Brace1", m.bracePart, at(10.0, 30.0, 70.0, 5.0));

    m.groundBase = ground(b, "HoldBed", m.base);
    detail::locate(b, "PostL", m.base, m.postLeft, ConfiguredFrameModel::kPostLeftXMm,
                   ConfiguredFrameModel::kPostYMm, ConfiguredFrameModel::kDeckMm);
    detail::locate(b, "PostR", m.base, m.postRight, ConfiguredFrameModel::kPostRightXMm,
                   ConfiguredFrameModel::kPostYMm, ConfiguredFrameModel::kDeckMm);

    // The brace is located by four mates and ONE more that is kept separate,
    // because `Loose` suppresses exactly that one. Splitting it out is what
    // makes the third configuration a statement about a mate rather than
    // about a component.
    (void)mate(b, "BraceFlat",
               {.type = MateType::Parallel,
                .a = planeTarget(m.base, PlaneReference{.plane = PrincipalPlane::XY}),
                .b = planeTarget(m.brace, PlaneReference{.plane = PrincipalPlane::XY})});
    (void)mate(b, "BraceLift",
               {.type = MateType::Distance,
                .a = planeTarget(m.base, PlaneReference{.plane = PrincipalPlane::XY}),
                .b = planeTarget(m.brace, PlaneReference{.plane = PrincipalPlane::XY}),
                .distance = 62.0_mm});
    // Negative because the XZ plane faces -Y (X x Z = -Y), so this puts the
    // brace at y = +25, across the bed rather than off it. detail::locate()
    // absorbs the same convention; these four are written out only because
    // `Loose` has to suppress one of them by name.
    (void)mate(b, "BraceAlongY",
               {.type = MateType::Distance,
                .a = planeTarget(m.base, PlaneReference{.plane = PrincipalPlane::XZ}),
                .b = planeTarget(m.brace, PlaneReference{.plane = PrincipalPlane::XZ}),
                .distance = -25.0_mm});
    (void)mate(b, "BraceSquare",
               {.type = MateType::Perpendicular,
                .a = planeTarget(m.base, PlaneReference{.plane = PrincipalPlane::YZ}),
                .b = planeTarget(m.brace, PlaneReference{.plane = PrincipalPlane::XZ})});
    // The one `Loose` releases: without it the brace may still slide along X.
    m.braceSeat = mate(b, "BraceAlongX",
                       {.type = MateType::Distance,
                        .a = planeTarget(m.base, PlaneReference{.plane = PrincipalPlane::YZ}),
                        .b = planeTarget(m.brace, PlaneReference{.plane = PrincipalPlane::YZ}),
                        .distance = 15.0_mm});

    // Three configurations. `Full` changes nothing, so it is the base state
    // named; the other two each suppress exactly one thing.
    m.full = b.need(m.document.createConfiguration("Full"), "Full");
    m.noBrace = b.need(m.document.createConfiguration("NoBrace"), "NoBrace");
    m.loose = b.need(m.document.createConfiguration("Loose"), "Loose");
    b.need(assembly::suppressComponent(m.document, m.noBrace, m.brace, true), "suppress brace");
    b.need(assembly::suppressMate(m.document, m.loose, m.braceSeat, true), "suppress brace seat");
    b.need(m.document.setActiveConfiguration(m.full), "activate Full");

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-H: FaultCases -----------------------------------------------------------

Result<FaultCasesModel> buildFaultCasesReferenceModel() {
    FaultCasesModel m{
        .document = Document(detail::fixedDocumentId("a55e0000-0000-4000-8000-000000000008"), "FaultCases")};
    detail::ModelBuilder b(m.document);

    m.blockPart = blockPart(b, "Block", "block", 40.0, 40.0, 10.0).solid;
    m.anchor = place(b, "Anchor", m.blockPart);
    m.floater = place(b, "Floater", m.blockPart, at(0.0, 0.0, 50.0));

    m.groundAnchor = ground(b, "HoldAnchor", m.anchor);
    // Ten millimetres apart and ninety apart, at once. Both cannot hold.
    m.near = mate(b, "Near",
                  {.type = MateType::Distance,
                   .a = planeTarget(m.anchor, PlaneReference{.plane = PrincipalPlane::XY}),
                   .b = planeTarget(m.floater, PlaneReference{.plane = PrincipalPlane::XY}),
                   .distance = FaultCasesModel::kNearMm * units::mm});
    m.far = mate(b, "Far",
                 {.type = MateType::Distance,
                  .a = planeTarget(m.anchor, PlaneReference{.plane = PrincipalPlane::XY}),
                  .b = planeTarget(m.floater, PlaneReference{.plane = PrincipalPlane::XY}),
                  .distance = FaultCasesModel::kFarMm * units::mm});

    // The rescue: the same assembly with `Far` released. The model is
    // committed in the BROKEN state -- a fault fixture that only fails when a
    // test reaches in and breaks it is testing the test.
    m.healthy = b.need(m.document.createConfiguration("Healthy"), "Healthy");
    b.need(assembly::suppressMate(m.document, m.healthy, m.far, true), "suppress Far");

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
