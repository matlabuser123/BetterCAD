#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>

#include <array>
#include <string_view>

// BetterCAD's production DRAWING reference models (P14-REFMOD-001).
//
// The part and assembly suites prove that a model is built correctly. These
// prove that a DRAWING of one is: real sheets, real views, real dimensions
// against named faces, and annotations that mean something -- then
// regeneration, persistence, the CLI and all three export formats over the
// same models.
//
// The rules are the ones the other two suites already keep:
//
//   * built ONLY through the public document, sketch, feature, assembly and
//     drawing APIs. No back door, and nothing a client could not do.
//   * the builder returns a document that has NOT been regenerated.
//   * deterministic: same items, same IDs, same values, and a FIXED document
//     ID, so a saved model is reproducible byte for byte.
//   * a builder carries no expected answer. What each model should measure is
//     derived independently in the tests and in the evidence -- a builder that
//     stated its own answer would be marking its own homework.
//
// Drawing models take document IDs of the form
// `d4a70000-0000-4000-8000-0000000000NN`, beside the parts' `5eed0000-...`
// and the assemblies' `a55e0000-...`.
//
// THE SUITE, AND WHY EACH ONE IS HERE. Eight models, each owning a capability
// no other one owns, rather than eight variations on a plate:
//
//   RM-DWG-01  the whole pipeline, with every number obvious
//   RM-DWG-02  views that disagree, an angle, and a view that is not 1:1
//   RM-DWG-03  a section and a detail of internal geometry
//   RM-DWG-04  a hole pattern: four identical holes, each named separately
//   RM-DWG-05  tolerances, fits, datums and a feature-control frame
//   RM-DWG-06  an assembly: repeated parts, a rotated part, and occlusion
//   RM-DWG-07  a bill of materials and the balloons that cite it
//   RM-DWG-08  two configurations, and what changes between them
namespace bettercad::reference {

// --- RM-DWG-01 ---------------------------------------------------------------
//
// DrawnStepPlate: the suite's smoke test, and the simplest complete drawing.
//
// A 100 x 60 x 20 plate, a 40 x 20 x 10 step on one corner, and a Ø12 hole
// through the plate beside it -- asymmetric in both directions, so a mirrored
// or transposed projection cannot look correct.
//
//   PlateProfile -> Plate -> StepProfile -> Step -> Bore
//   Sheet1 (A3, 1:1) -> Front -> {Top, Right}
//                    -> Length, Thickness, BoreCallout, GeneralNote
struct DrawnStepPlateModel {
    Document document;
    ParameterId plateLength{};
    ParameterId plateWidth{};
    ParameterId plateThickness{};
    ParameterId stepLength{};
    ParameterId stepWidth{};
    ParameterId stepHeight{};
    ParameterId holeDiameter{};
    ObjectId plateSketch{};
    ObjectId plate{};
    ObjectId stepSketch{};
    ObjectId step{};
    ObjectId hole{};
    /// The plate profile's four lines, in order: bottom, right, top, left.
    /// A SIDE face is named by the entity that swept it.
    std::array<EntityId, 4> plateLines{};
    SheetId sheet{};
    ViewId front{};
    ViewId top{};
    ViewId right{};
    DimensionId length{};
    DimensionId thickness{};
    AnnotationId callout{};
    AnnotationId note{};
};

[[nodiscard]] Result<DrawnStepPlateModel> buildDrawnStepPlateReferenceModel();

// --- RM-DWG-02 ---------------------------------------------------------------
//
// DrawnAngleBracket: views that disagree, a 45 degree corner, and a view drawn
// at 1:2 while its sheet is 1:1.
//
//   BodyProfile -> Body                              V = 320000 mm^3
//   Sheet1 (A3, 1:1)      -> Front (1:2) -> {Top, Side}
//                         -> Overall, Rise, Corner, ScaleNote
//   Sheet2 (A4 portrait)  -> Sheet2Front -> SlantAuxiliary
//                         -> AcrossSlant
struct DrawnAngleBracketModel {
    Document document;
    ParameterId length{};
    ParameterId height{};
    ParameterId width{};
    ParameterId shoulder{};
    ParameterId cutback{};
    ObjectId profileSketch{};
    ObjectId body{};
    /// The profile's five lines: bottom, right shoulder, slant, top, left.
    std::array<EntityId, 5> profileLines{};
    SheetId sheet{};
    ViewId front{};
    ViewId top{};
    ViewId side{};
    DimensionId overall{};
    DimensionId rise{};
    DimensionId corner{};
    AnnotationId note{};
    /// Sheet 2: A4 PORTRAIT, so the suite has a second format, the other
    /// orientation and a drawing of more than one sheet.
    SheetId secondSheet{};
    ViewId secondFront{};
    /// The 45 degree face seen square, which no orthographic view shows.
    ViewId auxiliary{};
    DimensionId acrossSlant{};
};

[[nodiscard]] Result<DrawnAngleBracketModel> buildDrawnAngleBracketReferenceModel();

// --- RM-DWG-03 ---------------------------------------------------------------
//
// DrawnPocketBlock: a section and a detail of internal geometry that no
// outside view can show. The pocket is PRISMATIC because a section through a
// curved face is refused (Section.cpp), and off centre in both directions so a
// mirrored section would not look right.
//
//   BlockProfile -> Block -> PocketProfile -> Pocket -> StepProfile -> Step
//                                                    V = 127200 mm^3
//   Sheet1 (A3, 1:1) -> Front -> {SectionAA, DetailB (2:1)}
//                    -> Across, Tall, SectionNote
struct DrawnPocketBlockModel {
    Document document;
    ParameterId blockLength{};
    ParameterId blockWidth{};
    ParameterId blockHeight{};
    ParameterId pocketDepth{};
    ParameterId stepDepth{};
    ObjectId blockSketch{};
    ObjectId block{};
    ObjectId pocketSketch{};
    ObjectId pocket{};
    ObjectId stepSketch{};
    ObjectId step{};
    std::array<EntityId, 4> blockLines{};
    SheetId sheet{};
    ViewId front{};
    ViewId section{};
    ViewId detail{};
    DimensionId across{};
    DimensionId tall{};
    AnnotationId note{};
};

[[nodiscard]] Result<DrawnPocketBlockModel> buildDrawnPocketBlockReferenceModel();

// --- RM-DWG-04 ---------------------------------------------------------------
//
// DrawnHolePlate: a hole PATTERN, and four identical holes that a drawing has
// to be able to tell apart.
//
// A 120 x 80 x 10 plate, a Ø10 bore at (20, 20), and a 2 x 2 rectangular
// pattern of it at 80 x 40 pitch -- so bores at (20,20), (100,20), (20,60)
// and (100,60). A Ø16 clearance hole sits at the centre, (60, 40), and is NOT
// patterned: it gives the drawing a hole callout, and it gives the four
// patterned bores something they are distinguishable from.
//
// THE BORE IS AN EXTRUDE CUT OF A CIRCLE, not a HoleFeature, and that is the
// point of the model. A cut's side face is named by the sketch entity that
// swept it, and a pattern's copy of that face is named by the same entity
// plus the copy it belongs to -- so each of the four bores has a semantic
// name of its own. A HoleFeature's bore has no name at all (P12 names a
// hole's bottom and floors, never its wall), which is why the clearance hole
// carries the callout and the patterned bores carry the dimensions.
//
//   PlateProfile -> Plate -> BoreProfile -> Bore -> Bores (2x2) -> Clearance
//   Sheet1 (A3, 1:1) -> Top -> Front (projected)
//                    -> Length, Width, BoreDiameter, FarBoreDiameter
//                    -> ClearanceCallout, four Centremarks, a Centreline
struct DrawnHolePlateModel {
    Document document;
    ParameterId plateLength{};
    ParameterId plateWidth{};
    ParameterId plateThickness{};
    ParameterId boreRadius{};
    ParameterId pitchX{};
    ParameterId pitchY{};
    ParameterId clearanceDiameter{};
    ObjectId plateSketch{};
    ObjectId plate{};
    ObjectId boreSketch{};
    ObjectId bore{};
    ObjectId bores{};
    ObjectId clearance{};
    std::array<EntityId, 4> plateLines{};
    /// The circle the bore's cylindrical face is named by.
    EntityId boreCircle{};
    SheetId sheet{};
    ViewId top{};
    ViewId front{};
    /// HORIZONTAL and VERTICAL, not linear: read along the top view's own
    /// axes, which is what an overall dimension on a drawing means.
    DimensionId length{};
    DimensionId width{};
    /// The source bore's diameter, and the diameter of the copy diagonally
    /// opposite it -- instance 3 of the grid.
    DimensionId boreDiameter{};
    DimensionId farBoreDiameter{};
    /// The same cylinder as a radius, which must read exactly half.
    DimensionId boreRadiusDimension{};
    /// Signed coordinates from two datum edges: across from the left-hand
    /// end (positive) and down from the top edge (negative). The sign is
    /// what an ordinate has and a horizontal or vertical dimension does not.
    DimensionId ordinateAcross{};
    DimensionId ordinateDown{};
    AnnotationId callout{};
    /// One centre mark per bore, in pattern-instance order 0, 1, 2, 3. Each
    /// names ONE bore; four identical circles, four different names.
    std::array<AnnotationId, 4> centremarks{};
    AnnotationId centreline{};
    AnnotationId note{};
};

[[nodiscard]] Result<DrawnHolePlateModel> buildDrawnHolePlateReferenceModel();

// --- RM-DWG-05 ---------------------------------------------------------------
//
// DrawnToleranceBlock: what a part is MADE TO, rather than what it measures.
//
// An 80 x 50 x 25 block with a Ø20 bore through it. The bottom face is datum
// A, the left-hand end face is datum B, and the bore's position is held to
// Ø0.05 against A then B -- the datum ORDER being the whole of what makes
// A|B different from B|A.
//
// Three kinds of tolerance, because they are written differently and resolve
// differently:
//
//   Length      80 +/-0.10          a symmetric deviation pair
//   Width       50 +0.15 -0.05      written as LIMITS
//   BoreSize    Ø20 H7              resolved from ISO 286, never stored
//
//   BlockProfile -> Block -> BoreProfile -> Bore       V = 100000 - 2500 pi
//   Sheet1 (A3, 1:1) -> Front -> Top (projected)
//                    -> Length, Width, BoreSize
//                    -> DatumA, DatumB, BorePosition, TopFinish
struct DrawnToleranceBlockModel {
    Document document;
    ParameterId blockLength{};
    ParameterId blockWidth{};
    ParameterId blockHeight{};
    ParameterId boreRadius{};
    ObjectId blockSketch{};
    ObjectId block{};
    ObjectId boreSketch{};
    ObjectId bore{};
    std::array<EntityId, 4> blockLines{};
    EntityId boreCircle{};
    SheetId sheet{};
    ViewId front{};
    ViewId top{};
    DimensionId length{};
    DimensionId width{};
    DimensionId boreSize{};
    AnnotationId datumA{};
    AnnotationId datumB{};
    AnnotationId borePosition{};
    AnnotationId topFinish{};
};

[[nodiscard]] Result<DrawnToleranceBlockModel> buildDrawnToleranceBlockReferenceModel();

// --- RM-DWG-06 ---------------------------------------------------------------
//
// DrawnClampSet: an ASSEMBLY drawing, and the three things one has that a
// part drawing does not.
//
//   repeated parts      two jaws, ONE part definition, two occurrences
//   a rotated part      the pin is turned 35 degrees about Z by its placement
//   occlusion           the jaws stand in front of the body, so edges the
//                       body has are hidden BY ANOTHER COMPONENT
//
// Body 90 x 50 x 15 grounded at the origin; Jaw 20 x 50 x 35 placed twice on
// the body's deck at x = 12 and x = 58; Pin, a Ø14 x 70 rod, laid across them.
// Four occurrences, three part definitions.
//
//   Sheet1 (A3, 1:1) -> Front (assembly) -> Top (projected, assembly)
//                    -> BodyLength
//                    -> AssemblyNote, PinNote (a leader onto the pin)
struct DrawnClampSetModel {
    Document document;
    ParameterId bodyLength{};
    ObjectId bodyPart{};
    ObjectId jawPart{};
    ObjectId pinPart{};
    /// The body profile's four lines, so a dimension can name its ends.
    std::array<EntityId, 4> bodyLines{};
    ComponentId body{};
    ComponentId jawLeft{};
    ComponentId jawRight{};
    ComponentId pin{};
    MateId groundBody{};
    SheetId sheet{};
    ViewId front{};
    ViewId top{};
    DimensionId bodyLengthDimension{};
    AnnotationId note{};
    /// A leader onto the pin OCCURRENCE: words that point at something.
    AnnotationId pinNote{};

    /// Where the mates put the two jaws, and how the pin is placed. Derived
    /// by hand from the mate equations, and asserted by the tests rather than
    /// read back from the solver.
    static constexpr double kDeckMm = 15.0;
    static constexpr double kJawLeftXMm = 12.0;
    static constexpr double kJawRightXMm = 58.0;
    /// The jaw is 30 deep and the body 50, so an origin at y = 10 seats it
    /// wholly on the body: y in [10, 40]. Nothing overhangs, which is both
    /// what a clamp looks like and what makes the top view exactly the body's
    /// footprint.
    static constexpr double kJawYMm = 10.0;
    static constexpr double kPinSpinDeg = 35.0;
};

[[nodiscard]] Result<DrawnClampSetModel> buildDrawnClampSetReferenceModel();

// --- RM-DWG-07 ---------------------------------------------------------------
//
// DrawnBoltedStack: a bill of materials, and balloons that cite it.
//
// Bracket x1, Bolt x4, Spacer x2 -- seven occurrences of three part
// definitions, so the BOM has three rows with quantities 1, 4 and 2, and
// `totalOccurrences` is 7. The four bolts are identical instances of one
// part, which is exactly the case a balloon must not get wrong: TWO of them
// carry balloons, and the two balloons must name different occurrences while
// showing the same item number.
//
//   Sheet1 (A3, 1:1) -> Front (assembly)
//                    -> BomTable
//                    -> BracketBalloon, BoltBalloonA, BoltBalloonB,
//                       SpacerBalloon
struct DrawnBoltedStackModel {
    Document document;
    ObjectId bracketPart{};
    ObjectId boltPart{};
    ObjectId spacerPart{};
    ComponentId bracket{};
    /// Four instances of ONE part. Balloons cite [0] and [3].
    std::array<ComponentId, 4> bolts{};
    std::array<ComponentId, 2> spacers{};
    MateId groundBracket{};
    SheetId sheet{};
    ViewId front{};
    AnnotationId table{};
    AnnotationId bracketBalloon{};
    AnnotationId boltBalloonA{};
    AnnotationId boltBalloonB{};
    AnnotationId spacerBalloon{};

    static constexpr std::size_t kBoltCount = 4;
    static constexpr std::size_t kSpacerCount = 2;
    /// Every active occurrence: 1 bracket + 4 bolts + 2 spacers.
    static constexpr std::size_t kOccurrences = 7;
    static constexpr double kDeckMm = 12.0;
};

[[nodiscard]] Result<DrawnBoltedStackModel> buildDrawnBoltedStackReferenceModel();

// --- RM-DWG-08 ---------------------------------------------------------------
//
// DrawnGuardedFrame: one drawing, two configurations, and everything that
// has to change between them.
//
//   Guarded   frame, guard and four bolts        3 BOM rows, quantities 1,1,4
//   Open      the guard and two bolts suppressed 2 BOM rows, quantities 1,2
//
// The guard's balloon is the one that matters: in `Open` the occurrence it
// names is not drawn, so the balloon must become UNRESOLVED rather than move
// to another component. Switching back to `Guarded` must restore it exactly
// -- A -> B -> A, with no history-dependent drift.
//
//   Sheet1 (A3, 1:1) -> Front (assembly)
//                    -> BomTable, GuardBalloon, FrameBalloon
struct DrawnGuardedFrameModel {
    Document document;
    ObjectId framePart{};
    ObjectId guardPart{};
    ObjectId boltPart{};
    ComponentId frame{};
    ComponentId guard{};
    std::array<ComponentId, 4> bolts{};
    MateId groundFrame{};
    ConfigurationId guarded{};
    ConfigurationId open{};
    SheetId sheet{};
    ViewId front{};
    AnnotationId table{};
    AnnotationId guardBalloon{};
    AnnotationId frameBalloon{};

    static constexpr std::size_t kBoltCount = 4;
    /// How many bolts `Open` leaves active, and therefore its BOM quantity.
    static constexpr std::size_t kOpenBoltCount = 2;
    /// Active occurrences in each configuration.
    static constexpr std::size_t kGuardedOccurrences = 6; // frame + guard + 4 bolts
    static constexpr std::size_t kOpenOccurrences = 3;    // frame + 2 bolts
    static constexpr double kDeckMm = 12.0;
};

[[nodiscard]] Result<DrawnGuardedFrameModel> buildDrawnGuardedFrameReferenceModel();

// --- the catalogue -----------------------------------------------------------

enum class DrawingReferenceModelKind {
    StepPlate,
    AngleBracket,
    PocketBlock,
    HolePlate,
    ToleranceBlock,
    ClampSet,
    BoltedStack,
    GuardedFrame,
};

/// One drawing reference model, as the tests, the CLI cases and the evidence
/// all name it.
struct DrawingReferenceModelInfo {
    DrawingReferenceModelKind kind;
    /// The document's name, e.g. "DrawnStepPlate".
    std::string_view name;
    /// The committed file's stem: `drawing_step_plate.bcad`.
    std::string_view fileStem;
    /// The label the evidence uses: "RM-DWG-01".
    std::string_view label;
    /// What the model is for, in one line.
    std::string_view purpose;
    /// A main dimension to drive, and a value to drive it to, for the
    /// model-change regeneration checks.
    std::string_view mainParameter;
    double mainParameterMm;
    /// Whether the model places component occurrences, so that drawing it
    /// costs an assembly solve and its views may be of the assembly.
    bool assembly;
};

inline constexpr std::array kDrawingReferenceModels{
    DrawingReferenceModelInfo{DrawingReferenceModelKind::StepPlate, "DrawnStepPlate",
                              "drawing_step_plate", "RM-DWG-01",
                              "the whole pipeline, with every number obvious", "plate_length",
                              120.0, false},
    DrawingReferenceModelInfo{DrawingReferenceModelKind::AngleBracket, "DrawnAngleBracket",
                              "drawing_angle_bracket", "RM-DWG-02",
                              "views that disagree, an angle, and a view that is not 1:1",
                              "body_length", 140.0, false},
    DrawingReferenceModelInfo{DrawingReferenceModelKind::PocketBlock, "DrawnPocketBlock",
                              "drawing_pocket_block", "RM-DWG-03",
                              "a section and a detail of internal geometry", "pocket_depth",
                              16.0, false},
    DrawingReferenceModelInfo{DrawingReferenceModelKind::HolePlate, "DrawnHolePlate",
                              "drawing_hole_plate", "RM-DWG-04",
                              "a hole pattern, and four identical holes named apart", "pitch_x",
                              70.0, false},
    DrawingReferenceModelInfo{DrawingReferenceModelKind::ToleranceBlock, "DrawnToleranceBlock",
                              "drawing_tolerance_block", "RM-DWG-05",
                              "tolerances, fits, datums and a feature-control frame",
                              "block_length", 90.0, false},
    DrawingReferenceModelInfo{DrawingReferenceModelKind::ClampSet, "DrawnClampSet",
                              "drawing_clamp_set", "RM-DWG-06",
                              "an assembly: repeated parts, a rotated part, and occlusion",
                              "body_length", 100.0, true},
    DrawingReferenceModelInfo{DrawingReferenceModelKind::BoltedStack, "DrawnBoltedStack",
                              "drawing_bolted_stack", "RM-DWG-07",
                              "a bill of materials and the balloons that cite it",
                              "bracket_w", 100.0, true},
    DrawingReferenceModelInfo{DrawingReferenceModelKind::GuardedFrame, "DrawnGuardedFrame",
                              "drawing_guarded_frame", "RM-DWG-08",
                              "two configurations, and what changes between them", "frame_w",
                              110.0, true},
};

[[nodiscard]] Result<Document> buildDrawingReferenceModel(DrawingReferenceModelKind kind);

} // namespace bettercad::reference
