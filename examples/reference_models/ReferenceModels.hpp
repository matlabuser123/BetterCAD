#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/features/Regenerator.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
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
// These models were built before parameter expressions were evaluated
// (P12-PARAM-001), so every dimension a feature follows is one parameter,
// used directly. Where a model needs a derived dimension (the shaft's mirror
// plane at half its length), a sketch builds the relation geometrically from
// one parameter.
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

// --- P12-REF-001 -----------------------------------------------------------
//
// Production parts that exercise the P12 capabilities together. Unlike the
// P11 models above, these use parameter expressions for every derived
// dimension, so each model has a small number of free parameters and the
// rest follow.

/// A configuration-driven motor mounting bracket: a plate width x width/2 x
/// width/15 with two bolt holes whose size and spacing are equations, and a
/// motor pilot boss on a sketch attached to the plate's end cap by name.
///
/// One free parameter, `width`; the configurations Small, Medium and Large
/// set it to 90, 120 and 180 mm and nothing else. The pilot boss does not
/// scale: it mates to a motor.
///
///   width -> height = width/2, thickness = width/15, bolt_d = thickness,
///            edge = 2*thickness, bolt_span = width - 2*edge
///
/// PlateSketch -> Plate (extrude) -> BoltHole -> BoltHoles (linear pattern)
/// -> PilotSketch (on Plate's end cap) -> Pilot (joined extrude).
struct MotorMountModel {
    Document document;
    ParameterId width{}, height{}, thickness{}, boltDiameter{}, edge{}, boltSpan{}, halfWidth{}, halfHeight{};
    ObjectId plateSketch{}, plate{}, boltHole{}, boltHoles{}, pilotSketch{}, pilot{};
    ConfigurationId small{}, medium{}, large{};

    /// The pilot boss, which is the same in every configuration.
    static constexpr double kPilotRadiusMm = 16.0;
    static constexpr double kPilotHeightMm = 10.0;
};

[[nodiscard]] Result<MotorMountModel> buildMotorMountReferenceModel();

/// A cast gearbox cover: a drafted box hollowed to a wall, with a spotfaced
/// inspection port and a row of tapped fixing holes.
///
///   width  -> wall = width/20, port_d = width*0.3
///   length -> fixing_pitch = length/3
///   height -> parting_z = -height
///
/// The cover hangs BELOW its outside face: z = 0 is the outside, z =
/// -height the open rim. That way the plane the two holes are placed on
/// never moves, and the faces that do move are reached by a driven datum
/// and by named faces. See the comment in GearboxCover.cpp.
///
/// BoxSketch -> Block (extrude, reversed) -> PartingPlane (datum, driven) ->
/// SideDraft (the four named sides, about the parting plane) -> Hollow
/// (shell, opened at the named far cap) -> InspectionPort (spotface) ->
/// FixingHole (M6 tapped, through) -> FixingHoles (linear pattern).
struct GearboxCoverModel {
    Document document;
    ParameterId length{}, width{}, height{}, wall{}, draftAngle{}, portDiameter{}, fixingPitch{},
        partingOffset{};
    ObjectId boxSketch{}, block{}, partingPlane{}, draft{}, shell{}, port{}, fixingHole{}, fixingHoles{};
};

[[nodiscard]] Result<GearboxCoverModel> buildGearboxCoverReferenceModel();

/// A square-section manifold leg swept along a path that leaves any one
/// plane: straight down, a quarter turn into +X, then a quarter turn into
/// +Y on another plane, with the section turning by `twist` along the way
/// (P12-SWEEP-001).
///
///   bend1_r -> bend2_r = bend1_r * 5 / 6
///
/// BoreSection (the square, centred so its centroid rides the path),
/// DropRun (XZ: line + arc), ElbowPlane (a datum at -(drop + bend1_r), so
/// the second run follows the first), SweepRun (on it: arc) -> Tube (sweep).
struct ManifoldTubeModel {
    Document document;
    ParameterId side{}, drop{}, firstBend{}, secondBend{}, twist{}, elbowOffset{}, flangeSide{},
        flangeThickness{}, halfSide{}, halfFlange{};
    ObjectId boreSection{}, dropRun{}, elbowPlane{}, sweepRun{}, tube{}, flangeSketch{}, flange{};
};

[[nodiscard]] Result<ManifoldTubeModel> buildManifoldTubeReferenceModel();

/// A square-to-round transition duct carrying a smooth nozzle: both halves
/// of P12-LOFT-001 in one part. The duct is RULED between sections of
/// different shapes, so its volume is the prismatoid of the closed-form
/// mixed area; the nozzle is SMOOTH through three equally spaced circles,
/// the one smooth case with a closed form (the quadratic through them).
///
///   throat_r -> mid_r = throat_r*2/3, outlet_r = throat_r*5/6
///   duct_h   -> nozzle_h = duct_h, mid_offset = duct_h + nozzle_h/2,
///               outlet_offset = duct_h + nozzle_h
///
/// InletSketch (a centred square), ThroatSketch, MidSketch, OutletSketch
/// (circles) -> Duct (ruled loft) -> Nozzle (smooth loft, joined).
struct TransitionDuctModel {
    Document document;
    ParameterId inletSide{}, throatRadius{}, midRadius{}, outletRadius{}, ductHeight{}, nozzleHeight{},
        midOffset{}, outletOffset{}, flangeSide{}, flangeThickness{}, halfInlet{}, halfFlange{};
    ObjectId inletSketch{}, throatSketch{}, midSketch{}, outletSketch{}, duct{}, nozzle{}, flangeSketch{},
        flange{};
};

[[nodiscard]] Result<TransitionDuctModel> buildTransitionDuctReferenceModel();

/// An index plate lightened by a ring of elliptical pockets, one of them
/// suppressed so the plate keeps solid metal for a keyway, carrying a hub
/// joined to it as a separate body.
///
///   pocket_a -> pocket_b = pocket_a*7/13
///   plate_r  -> pocket_circle_r = plate_r - pocket_a - 9 mm,
///               hub_r = plate_r*13/45
///   thickness-> hub_h = thickness*2
///
/// PlateSketch -> Plate (extrude) -> PlateAxis (datum axis) -> PocketSketch
/// (an ellipse) -> Pocket (through-all cut) -> Pockets (circular pattern of
/// 6 about the axis, instance 3 suppressed) -> HubSketch -> Hub (its own
/// body) -> Assembly (combine, join).
struct IndexPlateModel {
    Document document;
    ParameterId plateRadius{}, thickness{}, pocketSemiMajor{}, pocketSemiMinor{}, pocketCircle{}, hubRadius{},
        hubHeight{}, pocketCount{};
    ObjectId plateSketch{}, plate{}, axis{}, pocketSketch{}, pocket{}, pockets{}, hubSketch{}, hub{},
        assembly{};

    /// The instance the pattern leaves out, and how many it makes.
    static constexpr std::uint32_t kSuppressedInstance = 3;
    static constexpr std::uint32_t kPocketCount = 6;
};

[[nodiscard]] Result<IndexPlateModel> buildIndexPlateReferenceModel();

/// An angle bracket stiffened by a rib: a base and an upright joined, a
/// triangular gusset between them, a standard clearance hole, and one
/// vertical corner rounded by a fillet whose radius grows up the edge.
///
///   thickness -> rib_t = thickness*3/5, corner_top_r = thickness*2/5
///   wall_h    -> rib_reach = wall_h - 25 mm, and the fillet edge is
///                wall_h - thickness long (the base fills the corner below)
///
/// MountFrame (a coordinate system) -> BaseSketch -> Base (extrude) ->
/// WallSketch -> Wall (joined extrude) -> GussetSketch -> Rib (a straight
/// profile, so the stiffener is a triangular prism) -> BoltHole (an ISO 273
/// clearance hole for M8, medium series) -> CornerRound (variable-radius
/// fillet, 2 mm at the bottom to `corner_top_r` at the top).

/// MountFrame is the model's own coordinate system: the two sketches and the
/// rib's datum are attached to its principal planes, so the solid geometry
/// follows it. The bolt hole and the corner fillet are placed by geometric
/// signatures, which name planes and edges in MODEL space, so they do not
/// follow a moved frame -- they fail atomically and say so. Relocating a
/// whole part by datum needs semantic face naming for holes and fillets,
/// which P12 does not have.
struct RibbedBracketModel {
    Document document;
    ParameterId width{}, baseDepth{}, thickness{}, wallHeight{}, ribThickness{}, ribReach{}, ribToe{},
        cornerBottom{}, cornerTop{}, halfWidth{};
    ObjectId frame{}, baseSketch{}, base{}, wallSketch{}, wall{}, ribPlane{}, gussetSketch{}, rib{},
        boltHole{}, cornerRound{};
};

[[nodiscard]] Result<RibbedBracketModel> buildRibbedBracketReferenceModel();

/// The models: the five P11 reference parts, the swept U-bolt, and the
/// P12 production parts.
enum class ReferenceModelKind {
    Shaft,
    Flange,
    Pulley,
    BearingHousing,
    MountingBracket,
    UBolt,
    // P12-REF-001.
    MotorMount,
    GearboxCover,
    ManifoldTube,
    TransitionDuct,
    IndexPlate,
    RibbedBracket,
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
    ReferenceModelInfo{ReferenceModelKind::MotorMount, "MotorMount", "motor_mount", "width", 150.0},
    ReferenceModelInfo{ReferenceModelKind::GearboxCover, "GearboxCover", "gearbox_cover", "width", 96.0},
    ReferenceModelInfo{ReferenceModelKind::ManifoldTube, "ManifoldTube", "manifold_tube", "drop", 65.0},
    ReferenceModelInfo{ReferenceModelKind::TransitionDuct, "TransitionDuct", "transition_duct", "throat_r", 33.0},
    ReferenceModelInfo{ReferenceModelKind::IndexPlate, "IndexPlate", "index_plate", "plate_r", 99.0},
    ReferenceModelInfo{ReferenceModelKind::RibbedBracket, "RibbedBracket", "ribbed_bracket", "wall_h", 75.0},
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
