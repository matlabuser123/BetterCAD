#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// BetterCAD's mechanical reference models (P11-REF-001): realistic parts
// built only through the public document, parameter, sketch and feature APIs,
// the way a user or a script builds them. Nothing here creates geometry
// directly: every body comes from regenerating the document.
//
// Each builder returns a new document that has not been regenerated yet,
// with the IDs of its parameters and objects. The documents are
// deterministic: the same builder always gives the same items with the same
// IDs and the same values, and a fixed document ID, so a saved model is
// reproducible byte for byte.
//
// Parameter expressions are stored but not evaluated yet (P1-003), so every
// dimension a feature follows is one parameter, used directly. Where a model
// needs a derived dimension (the shaft's mirror plane at half its length), a
// sketch builds the relation geometrically from one parameter.
namespace bettercad::reference {

/// A stepped shaft turned about the Z axis, from z = 0 to 2 × half_length:
/// Ø2·r1 × l1, then Ø2·r2 × l2, then Ø2·r3 for the rest (Ø30 × 40, Ø40 × 40,
/// Ø25 × 40 mm), with a countersunk centre hole in each end, the two
/// shoulder corners rounded and both ends chamfered.
///
/// Parameters → Profile (XZ sketch) → Turn (revolve about Z) → CentreDrill
/// (hole in the z = 0 end) → TailCentreDrill (mirror of CentreDrill across
/// z = half_length) → ShoulderFillets → EndChamfers.
struct ShaftModel {
    Document document;
    ParameterId r1{}, l1{}, r2{}, l2{}, r3{}, halfLength{}, drillDiameter{}, drillDepth{}, filletRadius{},
        chamferSize{};
    ObjectId profile{}, turn{}, centreDrill{}, tailCentreDrill{}, shoulderFillets{}, endChamfers{};
};

/// Builds the shaft. Fails only if the model definition itself is broken.
[[nodiscard]] Result<ShaftModel> buildShaftReferenceModel();

/// A circular flange: a disc of radius outer_r and the given thickness
/// (Ø100 × 12 mm) with a central bore and bolt_count bolt holes on a circle
/// of radius bolt_circle_r (Ø30 bore, 6 × Ø8 on Ø70); the top edges of the
/// rim and the bore chamfered, the bottom rim rounded.
///
/// Parameters → DiscSketch → Disc (extrude up from the mating face z = 0) →
/// Bore (hole) → BoltHole (hole) → BoltCircle (circular pattern of
/// BoltHole) → EdgeChamfers → RimFillet.
struct FlangeModel {
    Document document;
    ParameterId outerRadius{}, thickness{}, boreDiameter{}, boltCircleRadius{}, boltDiameter{}, boltCount{},
        chamferSize{}, filletRadius{};
    ObjectId discSketch{}, disc{}, bore{}, boltHole{}, boltCircle{}, edgeChamfers{}, rimFillet{};
};

[[nodiscard]] Result<FlangeModel> buildFlangeReferenceModel();

/// A V-belt pulley turned about the Z axis: a rim of radius outer_r and
/// rim_width wide carried by a web web_thickness thick on a hub of radius
/// hub_r that projects past the rim (Ø120 × 30, Ø50 hub, 40 mm long), with
/// a Ø20 bore and a V-groove groove_depth deep around the rim.
///
/// Parameters → Section (XZ sketch) → Blank (revolve about Z) →
/// GrooveSection (XZ sketch measured from the rim) → Groove (revolved cut) →
/// Bore (hole) → WebFillets → HubChamfers.
struct PulleyModel {
    Document document;
    ParameterId outerRadius{}, rimInnerRadius{}, rimWidth{}, hubRadius{}, hubLength{}, webStart{}, webThickness{},
        boreDiameter{}, grooveCentre{}, grooveDepth{}, grooveWidth{}, grooveBottomWidth{}, filletRadius{},
        chamferSize{};
    ObjectId section{}, blank{}, grooveSection{}, groove{}, bore{}, webFillets{}, hubChamfers{};
};

[[nodiscard]] Result<PulleyModel> buildPulleyReferenceModel();

/// A pillow-block bearing housing: a base 2·base_half_length long,
/// base_width wide and base_t thick (120 × 60 × 12 mm) carrying an arched
/// boss of radius boss_r whose bore of radius bore_r sits axis_height above
/// the base (Ø70 boss, Ø40 bore, 45 mm up), with four Ø10 mounting holes.
/// Half of the section is drawn and mirrored as a body, so the halves cannot
/// drift apart; both extrudes are symmetric about y = 0, so the part stays
/// symmetric whatever its width.
///
/// Parameters → BaseSection, BossSection (XZ sketches) → Base, Boss (joined
/// symmetric extrudes) → Housing (body mirror) → Bore (extruded cut through
/// the joined housing) → MountHole → MountHoles (2 × 2 linear pattern) →
/// BossFillets → BaseChamfers.
struct BearingHousingModel {
    Document document;
    ParameterId baseHalfLength{}, baseWidth{}, baseThickness{}, bossRadius{}, axisHeight{}, boreRadius{},
        mountDiameter{}, mountX{}, mountY{}, mountPitchX{}, mountPitchY{}, filletRadius{}, chamferSize{};
    ObjectId baseSection{}, base{}, bossSection{}, boss{}, housing{}, boreSection{}, bore{}, mountHole{},
        mountHoles{}, bossFillets{}, baseChamfers{};
};

[[nodiscard]] Result<BearingHousingModel> buildBearingHousingReferenceModel();

/// An L-shaped mounting bracket: a base width long, base_width deep and
/// base_t thick (100 × 60 × 10 mm) with a plate plate_t thick standing
/// plate_height up its back edge (100 × 10 × 70 mm), four Ø8 holes in the
/// base and two in the plate, the inside corner rounded, the outside edge
/// broken, and a tapered gusset lofted between the plates.
///
/// Parameters → BaseSection, PlateSection → BasePlate, BackPlate (joined
/// extrudes) → BaseHole → BaseHoles (2 × 2 linear pattern) → PlateHole →
/// PlateHoles (mirror across the middle) → InnerFillet → OuterChamfer →
/// GussetFoot, GussetTip (XY sketches) → Gusset (loft, joined).
struct MountingBracketModel {
    Document document;
    ParameterId width{}, baseWidth{}, baseThickness{}, plateHeight{}, plateThickness{}, holeDiameter{},
        holeInset{}, holeRow{}, holePitchX{}, holePitchY{}, plateHoleX{}, plateHoleZ{}, centreX{}, gussetBack{},
        gussetEmbed{}, gussetTop{}, gussetFootThickness{}, gussetFootDepth{}, gussetTipThickness{},
        gussetTipDepth{}, filletRadius{}, chamferSize{};
    ObjectId baseSection{}, basePlate{}, plateSection{}, backPlate{}, baseHole{}, baseHoles{}, plateHole{},
        plateHoles{}, innerFillet{}, outerChamfer{}, gussetFoot{}, gussetTip{}, gusset{};
};

[[nodiscard]] Result<MountingBracketModel> buildMountingBracketReferenceModel();

/// A U-bolt: a rod of radius rod_r swept along two legs leg long joined by a
/// half turn of radius bend_r (Ø10 rod, 60 mm legs, R25 bend), both ends
/// chamfered. It puts the sweep feature to a natural use, as the five parts
/// above have none: their grooves and ribs are turned, extruded or lofted.
///
/// Parameters → RodSection (the section, on the plane where the path
/// starts), Route (line, arc, line in the XZ plane) → Rod (sweep) →
/// EndChamfers.
struct UBoltModel {
    Document document;
    ParameterId rodRadius{}, legLength{}, bendRadius{}, chamferSize{};
    ObjectId rodSection{}, route{}, rod{}, endChamfers{};
};

[[nodiscard]] Result<UBoltModel> buildUBoltReferenceModel();

/// The six models: the five P11 reference parts and the swept U-bolt that
/// puts the sweep feature to a natural use.
enum class ReferenceModelKind {
    Shaft,
    Flange,
    Pulley,
    BearingHousing,
    MountingBracket,
    UBolt,
};

struct ReferenceModelInfo {
    ReferenceModelKind kind;
    /// Document name, e.g. "Shaft".
    std::string_view name;
    /// File name stem, e.g. "shaft" for shaft.bcad, shaft.step, shaft.stl.
    std::string_view fileStem;
    /// A main dimension of the model, and a value to try it at (mm): what a
    /// timing run or a smoke test changes to make the model regenerate.
    std::string_view mainParameter;
    double mainParameterMm;
};

inline constexpr std::array kReferenceModels{
    ReferenceModelInfo{ReferenceModelKind::Shaft, "Shaft", "shaft", "r2", 25.0},
    ReferenceModelInfo{ReferenceModelKind::Flange, "Flange", "flange", "bolt_circle_r", 40.0},
    ReferenceModelInfo{ReferenceModelKind::Pulley, "Pulley", "pulley", "outer_r", 70.0},
    ReferenceModelInfo{ReferenceModelKind::BearingHousing, "BearingHousing", "bearing_housing", "base_width", 70.0},
    ReferenceModelInfo{ReferenceModelKind::MountingBracket, "MountingBracket", "mounting_bracket", "plate_height",
                       90.0},
    ReferenceModelInfo{ReferenceModelKind::UBolt, "UBolt", "u_bolt", "leg", 80.0},
};

/// The model's document, from its builder.
[[nodiscard]] Result<Document> buildReferenceModel(ReferenceModelKind kind);

// --- Fingerprints -----------------------------------------------------------

/// One parameter or object of a document: what a fingerprint keeps of it.
struct ItemFingerprint {
    ObjectId id{};
    /// "parameter", or the object's type name ("sketch", "revolve", ...).
    std::string kind{};
    std::string name{};

    friend bool operator==(const ItemFingerprint&, const ItemFingerprint&) = default;
};

/// One result body.
struct BodyFingerprint {
    ObjectId feature{};
    std::string name{};
    bool valid = false;
    geometry::TopologySummary topology{};
    double volumeMm3 = 0.0;
    double areaMm2 = 0.0;
    std::array<double, 3> centroidMm{};
    std::array<double, 3> minMm{};
    std::array<double, 3> maxMm{};

    friend bool operator==(const BodyFingerprint&, const BodyFingerprint&) = default;
};

/// What identifies a regenerated model: its items (stable IDs, kinds and
/// names, in ID order), how many features it has, and its result bodies with
/// their geometric properties. Two regenerations of the same model must give
/// equal fingerprints; operator== compares every value exactly.
struct ModelFingerprint {
    std::vector<ItemFingerprint> items{};
    std::size_t featureCount = 0;
    std::vector<BodyFingerprint> bodies{};

    friend bool operator==(const ModelFingerprint&, const ModelFingerprint&) = default;
};

/// The properties of one body, e.g. a feature's intermediate body. Fails
/// with FailedPrecondition for an empty body.
[[nodiscard]] Result<BodyFingerprint> bodyFingerprint(const geometry::Body& body, ObjectId feature,
                                                      std::string name);

/// The fingerprint of @p document as @p regenerator last built it. Fails
/// with FailedPrecondition if a result feature has no body.
[[nodiscard]] Result<ModelFingerprint> fingerprint(const Document& document,
                                                   const features::Regenerator& regenerator);

/// How far two fingerprints are apart.
struct FingerprintDifference {
    /// Items, feature count, result features, names, validity and topology
    /// counts all equal.
    bool sameStructure = false;
    /// Largest relative difference of a volume or area, and largest absolute
    /// difference of a centroid or bound coordinate (mm).
    double volumeAreaRelative = 0.0;
    double positionMm = 0.0;
};

[[nodiscard]] FingerprintDifference compare(const ModelFingerprint& a, const ModelFingerprint& b);

/// A text rendering with every value printed to 17 significant digits, one
/// line per item and per body, for evidence files and comparisons across
/// builds.
[[nodiscard]] std::string toText(const ModelFingerprint& fingerprint);

} // namespace bettercad::reference
