#include "AssemblyParts.hpp"
#include "AssemblyReferenceModels.hpp"

#include <bettercad/assembly/Configurations.hpp>
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

namespace {

MateTarget planeOf(ComponentId component, PrincipalPlane which) {
    return planeTarget(component, PlaneReference{.plane = which});
}

/// The face a feature's extrude ends on, named by the feature that made it.
MateTarget endCapOf(ComponentId component, ObjectId feature) {
    return faceTarget(component, FaceName{.feature = feature, .face = {.role = FaceRole::EndCap}});
}

/// The face it starts from.
MateTarget startCapOf(ComponentId component, ObjectId feature) {
    return faceTarget(component, FaceName{.feature = feature, .face = {.role = FaceRole::StartCap}});
}

/// Seats @p moving's underside on @p held's top face BY NAME, and squares it
/// up. Four mates, six equations: the coincidence is three, and the two
/// distances and the square are one each.
///
/// The coincidence is the one that matters. It names a face through the
/// feature that generates it, so it follows that feature's geometry when a
/// parameter moves it -- which is what ADR-004 means by a semantic
/// reference, and what a face signature could not do.
/// Returns the coincidence, which is the mate a test names.
MateId seat(detail::ModelBuilder& b, const std::string& prefix, ComponentId held, ObjectId heldFeature,
            ComponentId moving, ObjectId movingFeature, double xMm, double yMm) {
    const MateId seated = mate(b, prefix + "Seat",
                               {.type = MateType::Coincident,
                                .a = endCapOf(held, heldFeature),
                                .b = startCapOf(moving, movingFeature)});
    (void)mate(b, prefix + "AlongX",
               {.type = MateType::Distance,
                .a = planeOf(held, PrincipalPlane::YZ),
                .b = planeOf(moving, PrincipalPlane::YZ),
                .distance = xMm * units::mm});
    (void)mate(b, prefix + "AlongY",
               {.type = MateType::Distance,
                .a = planeOf(held, PrincipalPlane::XZ),
                .b = planeOf(moving, PrincipalPlane::XZ),
                .distance = -yMm * units::mm});
    (void)mate(b, prefix + "Square",
               {.type = MateType::Perpendicular,
                .a = planeOf(held, PrincipalPlane::YZ),
                .b = planeOf(moving, PrincipalPlane::XZ)});
    return seated;
}

} // namespace

// --- RM-F: DrivenCover ----------------------------------------------------------

Result<DrivenCoverModel> buildDrivenCoverReferenceModel() {
    DrivenCoverModel m{
        .document = Document(detail::fixedDocumentId("a55e0000-0000-4000-8000-000000000006"), "DrivenCover")};
    detail::ModelBuilder b(m.document);

    const detail::BlockPart body = blockPart(b, "Body", "body", 80.0, 60.0, DrivenCoverModel::kBodyHeightMm);
    m.bodyPart = body.solid;
    m.bodyHeight = body.height;
    m.lidPart = blockPart(b, "Lid", "lid", 80.0, 60.0, 8.0).solid;

    m.body = place(b, "BodyBlock", m.bodyPart);
    // Deliberately nowhere near where it belongs, so the seat has to put it
    // there rather than the placement happening to be right.
    m.lid = place(b, "LidPlate", m.lidPart, at(17.0, -9.0, 95.0, 21.0));

    m.groundBody = ground(b, "HoldBody", m.body);
    m.seat = seat(b, "Lid", m.body, m.bodyPart, m.lid, m.lidPart, 0.0, 0.0);

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

// --- RM-G: Machine --------------------------------------------------------------

Result<MachineModel> buildMachineReferenceModel() {
    MachineModel m{
        .document = Document(detail::fixedDocumentId("a55e0000-0000-4000-8000-000000000007"), "Machine")};
    detail::ModelBuilder b(m.document);

    const detail::BlockPart housing =
        blockPart(b, "Housing", "housing", 160.0, 120.0, MachineModel::kHousingHeightMm);
    m.housingPart = housing.solid;
    m.housingHeight = housing.height;
    m.coverPart = blockPart(b, "Cover", "cover", 160.0, 120.0, 10.0).solid;
    m.shaftPart = discPart(b, "Shaft", "shaft", 12.0, 140.0).solid;
    m.footPart = blockPart(b, "Foot", "foot", 24.0, 24.0, 10.0).solid;

    m.housing = place(b, "HousingBody", m.housingPart);
    m.cover = place(b, "CoverPlate", m.coverPart, at(23.0, 14.0, 130.0, 9.0));
    // Two shafts of one part. The front one runs on the housing centreline
    // through a cylindrical joint, so it keeps the slide and the turn a
    // machine's output shaft has; the rear one is located outright.
    m.shaftFront = place(b, "ShaftFront", m.shaftPart, at(30.0, 18.0, 12.0, 17.0));
    m.shaftRear = place(b, "ShaftRear", m.shaftPart, at(5.0, 5.0, 5.0, -11.0));
    for (std::size_t i = 0; i < MachineModel::kFootCount; ++i) {
        m.feet[i] = place(b, std::format("Foot{}", i + 1), m.footPart, at(3.0 * static_cast<double>(i), 7.0, 60.0));
    }

    m.groundHousing = ground(b, "HoldHousing", m.housing);
    // The cover rides on the housing's top FACE, by name: raise the housing
    // and the cover rises with it.
    m.coverSeat = seat(b, "Cover", m.housing, m.housingPart, m.cover, m.coverPart, 0.0, 0.0);

    (void)mate(b, "OutputShaft",
               {.type = MateType::Cylindrical,
                .a = axisTarget(m.housing, AxisReference{}),
                .b = axisTarget(m.shaftFront, AxisReference{})});
    detail::locate(b, "Rear", m.housing, m.shaftRear, MachineModel::kShaftRearXMm, MachineModel::kShaftYMm, 0.0);

    // Four feet under the housing, each an instance of one part, each fully
    // located: the assembly stands on them, so none of them may wander.
    constexpr std::array<std::pair<double, double>, MachineModel::kFootCount> kFeet{
        {{12.0, 12.0}, {124.0, 12.0}, {12.0, 84.0}, {124.0, 84.0}}};
    for (std::size_t i = 0; i < MachineModel::kFootCount; ++i) {
        detail::locate(b, std::format("Foot{}", i + 1), m.housing, m.feet[i], kFeet[i].first, kFeet[i].second,
                       -10.0);
    }

    m.assembled = b.need(m.document.createConfiguration("Assembled"), "Assembled");
    m.bare = b.need(m.document.createConfiguration("Bare"), "Bare");
    // Bare: the housing and its shafts, for machining. The cover and the feet
    // come off, and their mates go with them.
    b.need(assembly::suppressComponent(m.document, m.bare, m.cover, true), "suppress cover");
    for (const ComponentId foot : m.feet) {
        b.need(assembly::suppressComponent(m.document, m.bare, foot, true), "suppress foot");
    }
    b.need(m.document.setActiveConfiguration(m.assembled), "activate Assembled");

    if (auto status = b.status(); !status) {
        return std::unexpected(status.error());
    }
    return m;
}

} // namespace bettercad::reference
