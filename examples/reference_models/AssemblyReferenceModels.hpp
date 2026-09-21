#pragma once

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/document/Document.hpp>

#include <array>
#include <string_view>

// BetterCAD's production assembly reference models (P13-REFMOD-001).
//
// The part models in ReferenceModels.hpp prove that a part can be modelled.
// These prove that an ASSEMBLY of parts works as a whole: components,
// placements, mates, the solver, configurations, suppression, stable
// references, regeneration, persistence, the CLI and STEP export, together,
// through nothing but the public API.
//
// Each model builds its own parts. P13 assemblies live inside one document
// and cross-document references are not implemented, so a component's part
// is always an object of the same document -- see "Accepted P13 Constraints"
// in TODO.md.
//
// WHAT IS AND IS NOT HERE. Every model states the degrees of freedom it is
// meant to have, and every one of those was derived by hand from the mate
// equation counts that P13-SOLVE-001 and P13-MATE-002 qualified, BEFORE any
// solver output was read. Those derivations live in the tests and the
// evidence, never here: a builder that carried its own expected answer would
// be marking its own homework.
//
// Like the part models, these are deterministic. Each has a fixed document
// ID, so saving a freshly built model reproduces its committed .bcad file
// byte for byte.
namespace bettercad::reference {

/// RM-A. Two plates, both held where they are put: the smallest assembly
/// that still exercises the whole pipeline. Base 80 x 60 x 10 at the origin,
/// Cover 80 x 60 x 6 forty millimetres above it, each grounded by a Fixed
/// mate.
///
/// Nothing is free, so the solved transforms are the placements themselves
/// and every expected value is read straight off the model. That is the
/// point: if this one is wrong, nothing further is worth measuring.
struct GroundedPairModel {
    Document document;
    ObjectId basePart{}, coverPart{};
    ComponentId base{}, cover{};
    MateId groundBase{}, groundCover{};

    static constexpr double kCoverHeightMm = 40.0;
};

[[nodiscard]] Result<GroundedPairModel> buildGroundedPairReferenceModel();

/// RM-B. A base carrying two arms of ONE part, each fully located by five
/// mates: parallel, three distances and a perpendicular. Nothing is left
/// free, and the two arms are instances -- the same part in two places,
/// which is what a STEP product structure has to show.
///
/// Base 100 x 80 x 12 grounded; Arm 40 x 30 x 10 placed twice on top of it.
struct ConstrainedStackModel {
    Document document;
    ObjectId basePart{}, armPart{};
    ComponentId base{}, armLeft{}, armRight{};
    MateId groundBase{};

    /// Where the mates put the two arms (mm), which is what the test derives
    /// independently and then checks.
    static constexpr double kDeckMm = 12.0;
    static constexpr double kLeftXMm = 20.0;
    static constexpr double kRightXMm = 55.0;
    static constexpr double kArmYMm = 25.0;
};

[[nodiscard]] Result<ConstrainedStackModel> buildConstrainedStackReferenceModel();

/// RM-C. A shaft in a bore: one concentric mate, and the two freedoms it
/// deliberately leaves -- slide along the axis and spin about it.
///
/// The shaft is placed off the axis, along it, and spun, so the solve has
/// something to remove and something to keep. Which is which is the whole
/// assertion: the off-axis offset must go, the other two must survive
/// untouched.
struct ShaftInBoreModel {
    Document document;
    /// Drives the shaft's position ALONG the bore. The concentric mate
    /// leaves that free, so the placement keeps it -- which makes this the
    /// one place in the suite where a parameter-driven placement is visible
    /// in the solved answer rather than overwritten by a mate.
    ParameterId shaftAlongBore{};
    ObjectId housingPart{}, shaftPart{};
    ComponentId housing{}, shaft{};
    MateId groundHousing{}, bore{};

    /// How the shaft starts: off the axis, along it, and spun about it.
    static constexpr double kOffsetXMm = 18.0;
    static constexpr double kOffsetYMm = -7.0;
    static constexpr double kAlongZMm = 25.0;
    static constexpr double kSpinDeg = 40.0;
};

[[nodiscard]] Result<ShaftInBoreModel> buildShaftInBoreReferenceModel();

/// RM-D. One frame and the four mechanical joints, each on its own
/// component: a hinge, a slide, a sleeve and a face.
///
/// Every joint is placed displaced in the freedoms it keeps AND in the ones
/// it removes, so a joint that silently behaved like a different joint --
/// a slider that kept its turn, a revolute that kept its slide -- would be
/// caught by what survived rather than by a count.
struct JointSetModel {
    Document document;
    ObjectId framePart{}, linkPart{}, shoePart{}, sleevePart{}, padPart{};
    ComponentId frame{}, hinge{}, shoe{}, sleeve{}, pad{};
    MateId groundFrame{}, revolute{}, slider{}, cylindrical{}, planar{};

    /// How each joint's component starts, in the freedoms that must survive.
    static constexpr double kHingeSpinDeg = 30.0;
    static constexpr double kShoeAlongMm = 30.0;
    static constexpr double kSleeveAlongMm = 40.0;
    static constexpr double kSleeveSpinDeg = 35.0;
    static constexpr double kPadAcrossXMm = 12.0;
    static constexpr double kPadAcrossYMm = -8.0;
    static constexpr double kPadSpinDeg = 28.0;
    /// The off-joint displacement every one of them must have removed.
    static constexpr double kStrayMm = 15.0;
};

[[nodiscard]] Result<JointSetModel> buildJointSetReferenceModel();

/// RM-E. One frame in three configurations: everything, a component taken
/// out, and a mate released.
///
///   Full     base, two posts and a brace, all mated      -- fully constrained
///   NoBrace  the brace suppressed, and with it its mates -- the posts stay put
///   Loose    the brace's locating mate suppressed        -- the brace is freed
///
/// The third is the one worth having: suppressing a MATE rather than a
/// component changes the answer without changing what is in the assembly,
/// which is the distinction a configuration has to keep.
struct ConfiguredFrameModel {
    Document document;
    ObjectId basePart{}, postPart{}, bracePart{};
    ComponentId base{}, postLeft{}, postRight{}, brace{};
    MateId groundBase{}, braceSeat{};
    ConfigurationId full{}, noBrace{}, loose{};

    static constexpr double kDeckMm = 14.0;
    static constexpr double kPostLeftXMm = 15.0;
    static constexpr double kPostRightXMm = 85.0;
    static constexpr double kPostYMm = 20.0;
};

[[nodiscard]] Result<ConfiguredFrameModel> buildConfiguredFrameReferenceModel();

/// RM-F. A lid seated on a body's top FACE, by name -- so when the body
/// grows, the lid rises with it.
///
/// The mate names the body's end cap through a FaceName, which ADR-004
/// requires to be semantic: it follows the feature that generates it rather
/// than matching a plane that happens to sit at that height. Changing
/// `body_h` is therefore the test -- the lid must move to the new height,
/// and must come back when the parameter does.
struct DrivenCoverModel {
    Document document;
    ParameterId bodyHeight{};
    ObjectId bodyPart{}, lidPart{};
    ComponentId body{}, lid{};
    MateId groundBody{}, seat{};

    /// The height the model is committed at, and the one the test drives it
    /// to and back from.
    static constexpr double kBodyHeightMm = 30.0;
    static constexpr double kRaisedHeightMm = 45.0;
};

[[nodiscard]] Result<DrivenCoverModel> buildDrivenCoverReferenceModel();

/// RM-G. The production-scale one: a gearbox that uses most of P13 at once.
///
/// A housing carrying two shafts through cylindrical joints, a cover seated
/// on the housing's top face by name, and four feet that are four instances
/// of one part. Two configurations: fully built, and a bare housing for
/// machining.
///
/// Eight components, five parts, both kinds of mate, a stable reference,
/// repeated instances, configurations and suppression -- small enough for
/// CI, big enough that the pieces have to agree with each other.
struct MachineModel {
    Document document;
    ParameterId housingHeight{};
    ObjectId housingPart{}, coverPart{}, shaftPart{}, footPart{};
    ComponentId housing{}, cover{}, shaftFront{}, shaftRear{};
    std::array<ComponentId, 4> feet{};
    MateId groundHousing{}, coverSeat{};
    ConfigurationId assembled{}, bare{};

    static constexpr double kHousingHeightMm = 40.0;
    static constexpr double kShaftFrontXMm = 45.0;
    static constexpr double kShaftRearXMm = 115.0;
    static constexpr double kShaftYMm = 60.0;
    static constexpr std::size_t kFootCount = 4;
};

[[nodiscard]] Result<MachineModel> buildMachineReferenceModel();

/// RM-H. An assembly that cannot be solved, and the configuration that
/// rescues it.
///
/// Two distance mates ask for ten millimetres and ninety at once. The base
/// state is the broken one on purpose: a committed model that fails is worth
/// more than a description of failure, and the CLI and the export have to be
/// provably unable to pretend otherwise. Suppressing the second mate in
/// `Healthy` makes the same assembly solve.
struct FaultCasesModel {
    Document document;
    ObjectId blockPart{};
    ComponentId anchor{}, floater{};
    MateId groundAnchor{}, near{}, far{};
    ConfigurationId healthy{};

    static constexpr double kNearMm = 10.0;
    static constexpr double kFarMm = 90.0;
};

[[nodiscard]] Result<FaultCasesModel> buildFaultCasesReferenceModel();

// --- The catalogue -----------------------------------------------------------

enum class AssemblyReferenceModelKind {
    GroundedPair,
    ConstrainedStack,
    ShaftInBore,
    JointSet,
    ConfiguredFrame,
    DrivenCover,
    Machine,
    FaultCases,
};

struct AssemblyReferenceModelInfo {
    AssemblyReferenceModelKind kind;
    /// Document name, e.g. "GroundedPair".
    std::string_view name;
    /// File stem: assembly_grounded_pair.bcad and so on.
    std::string_view fileStem;
    /// RM-A .. RM-H, the name the evidence uses.
    std::string_view label;
    /// Whether the model is meant to solve in the state it is committed in.
    /// False only for RM-H, whose committed state is the broken one.
    bool solves;
};

inline constexpr std::array kAssemblyReferenceModels{
    AssemblyReferenceModelInfo{AssemblyReferenceModelKind::GroundedPair, "GroundedPair",
                               "assembly_grounded_pair", "RM-A", true},
    AssemblyReferenceModelInfo{AssemblyReferenceModelKind::ConstrainedStack, "ConstrainedStack",
                               "assembly_constrained_stack", "RM-B", true},
    AssemblyReferenceModelInfo{AssemblyReferenceModelKind::ShaftInBore, "ShaftInBore", "assembly_shaft_in_bore",
                               "RM-C", true},
    AssemblyReferenceModelInfo{AssemblyReferenceModelKind::JointSet, "JointSet", "assembly_joint_set", "RM-D",
                               true},
    AssemblyReferenceModelInfo{AssemblyReferenceModelKind::ConfiguredFrame, "ConfiguredFrame",
                               "assembly_configured_frame", "RM-E", true},
    AssemblyReferenceModelInfo{AssemblyReferenceModelKind::DrivenCover, "DrivenCover", "assembly_driven_cover",
                               "RM-F", true},
    AssemblyReferenceModelInfo{AssemblyReferenceModelKind::Machine, "Machine", "assembly_machine", "RM-G", true},
    AssemblyReferenceModelInfo{AssemblyReferenceModelKind::FaultCases, "FaultCases", "assembly_fault_cases",
                               "RM-H", false},
};

/// The model's document, from its builder.
[[nodiscard]] Result<Document> buildAssemblyReferenceModel(AssemblyReferenceModelKind kind);

} // namespace bettercad::reference
