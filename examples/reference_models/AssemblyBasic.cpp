#include "AssemblyParts.hpp"
#include "AssemblyReferenceModels.hpp"

#include <bettercad/core/document/MateReference.hpp>

namespace bettercad::reference {

using namespace bettercad::literals;
using assembly::MateType;
using detail::at;
using detail::blockPart;
using detail::discPart;
using detail::ground;
using detail::mate;
using detail::place;

// --- RM-A: GroundedPair ---------------------------------------------------------

Result<GroundedPairModel> buildGroundedPairReferenceModel() {
    GroundedPairModel m{
        .document = Document(detail::fixedDocumentId("a55e0000-0000-4000-8000-000000000001"), "GroundedPair")};
    detail::ModelBuilder b(m.document);

    // Two plates. Nothing here is clever on purpose: this is the model that
    // says the pipeline works at all, so every number in it is one a reader
    // can check in their head.
    m.basePart = blockPart(b, "Base", "base", 80.0, 60.0, 10.0).solid;
    m.coverPart = blockPart(b, "Cover", "cover", 80.0, 60.0, 6.0).solid;

    m.base = place(b, "BasePlate", m.basePart);
    m.cover = place(b, "CoverPlate", m.coverPart, at(0.0, 0.0, GroundedPairModel::kCoverHeightMm));

    // Both held where they are put, so the solve has no freedom to use and
    // the derived transforms are the placements themselves.
    m.groundBase = ground(b, "HoldBase", m.base);
    m.groundCover = ground(b, "HoldCover", m.cover);

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-B: ConstrainedStack -----------------------------------------------------

Result<ConstrainedStackModel> buildConstrainedStackReferenceModel() {
    ConstrainedStackModel m{.document = Document(detail::fixedDocumentId("a55e0000-0000-4000-8000-000000000002"),
                                                 "ConstrainedStack")};
    detail::ModelBuilder b(m.document);

    m.basePart = blockPart(b, "Deck", "deck", 100.0, 80.0, ConstrainedStackModel::kDeckMm).solid;
    m.armPart = blockPart(b, "Arm", "arm", 40.0, 30.0, 10.0).solid;

    m.base = place(b, "Deck1", m.basePart);
    // Both arms are instances of ONE part, and neither placement is where
    // its mates will put it: the solve has to do the work, and the file has
    // to show one product in two places.
    m.armLeft = place(b, "ArmLeft", m.armPart, at(5.0, 5.0, 60.0, 12.0));
    m.armRight = place(b, "ArmRight", m.armPart, at(90.0, 70.0, 5.0, -20.0));

    m.groundBase = ground(b, "HoldDeck", m.base);
    detail::locate(b, "Left", m.base, m.armLeft, ConstrainedStackModel::kLeftXMm, ConstrainedStackModel::kArmYMm,
                   ConstrainedStackModel::kDeckMm);
    detail::locate(b, "Right", m.base, m.armRight, ConstrainedStackModel::kRightXMm,
                   ConstrainedStackModel::kArmYMm, ConstrainedStackModel::kDeckMm);

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-C: ShaftInBore ----------------------------------------------------------

Result<ShaftInBoreModel> buildShaftInBoreReferenceModel() {
    ShaftInBoreModel m{
        .document = Document(detail::fixedDocumentId("a55e0000-0000-4000-8000-000000000003"), "ShaftInBore")};
    detail::ModelBuilder b(m.document);

    m.housingPart = blockPart(b, "Housing", "housing", 60.0, 60.0, 40.0).solid;
    m.shaftPart = discPart(b, "Shaft", "shaft", 10.0, 80.0).solid;

    m.housing = place(b, "HousingBody", m.housingPart);
    // Displaced in all three: across the axis, along it, and about it. The
    // concentric mate must remove the first and keep the other two, and a
    // model that started on the axis could not tell those apart.
    // The distance along the bore is a PARAMETER, not a literal: the mate
    // leaves that freedom, so the placement keeps it and driving the
    // parameter drives the assembly (P13-XFORM-001).
    m.shaftAlongBore = b.length("shaft_along_bore", ShaftInBoreModel::kAlongZMm);
    ComponentPlacement placement =
        at(ShaftInBoreModel::kOffsetXMm, ShaftInBoreModel::kOffsetYMm, ShaftInBoreModel::kAlongZMm,
           ShaftInBoreModel::kSpinDeg);
    placement.translationParameters[2] = m.shaftAlongBore;
    m.shaft = place(b, "ShaftPin", m.shaftPart, placement);

    m.groundHousing = ground(b, "HoldHousing", m.housing);
    m.bore = mate(b, "Bore",
                  {.type = MateType::Concentric,
                   .a = axisTarget(m.housing, AxisReference{}),
                   .b = axisTarget(m.shaft, AxisReference{})});

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
