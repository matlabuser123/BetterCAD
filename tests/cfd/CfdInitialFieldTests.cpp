// P19-IC-001: the CFD initial field (ADR-068).
//
// ONE FILE AND NOT TWO, unlike P19-BC-001's. That milestone split its seam
// tests from its authority tests because its claims were about NAMING AND
// ATTRIBUTION -- that a `FaceName` resolves to the facets of the face it names
// and is never rebound to something nearby -- and no hand-built target can make
// such a claim. A uniform initial field names nothing and resolves nothing, so
// there is no attribution to prove and the split would be an empty gesture. The
// cases that need a real mesh use the reference models here, beside the ones
// that do not.
//
// THE ORACLES ARE INDEPENDENT WHERE THEY CAN BE:
//
// ```text
// the blocked subscript   comp * cells + cell is written out AGAIN below rather
//                         than read from CfdFieldMap, so the expansion is
//                         compared against arithmetic the production path did
//                         not produce. P17's interleaving is written out too,
//                         and asserted to DIFFER, so a mutation to it fails
// the expected values     computed from the canonical record directly --
//                         pressure.si(), velocity.x.si() -- never by asking the
//                         implementation what it meant to write
// the height premise      cell centroids come from CfdGeometry, which the
//                         initial field does not consult at all, so the premise
//                         of the gravity case is established by an instrument
//                         the case cannot influence
// ```
//
// EVERY EDIT GOES THROUGH THE DOCUMENT. `PreparedFluid::addAnalysis` puts the
// analysis in the document and the staleness cases edit it with
// `modifyObject`, because both of P19-BC-001's reviews found that the
// local-definition-copy spelling of these tests CANNOT FAIL: a copy edited in
// the test is not canonical state, so the production path never sees the edit.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>
#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdFields.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdInitialField.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;

using bettercad::cfd::CfdAnalysisDefinition;
using bettercad::cfd::CfdFieldMap;
using bettercad::cfd::CfdInitialCondition;
using bettercad::cfd::CfdInitialFieldProblem;
using bettercad::cfd::CfdInitialFieldSource;
using bettercad::cfd::CfdInitialFieldStaleReason;
using bettercad::cfd::CfdTopology;
using bettercad::cfd::CfdVariable;
using bettercad::cfd::Velocity3;
using bettercad::cfd::CellIndex;
using bettercad::cfd::PreparedCfdInitialField;
using bettercad::cfd::VectorComponent;
using bettercad::reference::MeshReferenceModelKind;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

[[nodiscard]] Velocity3 velocity(double x, double y, double z) {
    return Velocity3{Velocity::fromSi(x), Velocity::fromSi(y), Velocity::fromSi(z)};
}

/// The refusal a `Result<void>` carries, requiring that it is one.
[[nodiscard]] Error refusalOf(const Result<void>& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

/// Water, so the CFD input gate resolves a density and a viscosity. The values
/// are P19-FLUID-001's own; nothing here depends on them, which the
/// density-edit case asserts.
void assignWater(Document& document, MaterialId& id) {
    features::MaterialDefinition water;
    water.designation = "Water";
    water.mechanical.density = materials::MaterialProperty<Density>::known(998_kg_per_m3);
    water.thermal.dynamicViscosity =
        materials::MaterialProperty<DynamicViscosity>::known(1.002_mPa_s);
    const Result<MaterialId> created = features::createMaterial(document, "CfdWater", water);
    REQUIRE(created.has_value());
    REQUIRE(features::assignMaterial(document, *created).has_value());
    id = *created;
}

/// ADR-066's blocked numbering, `comp * cells + cell`, WRITTEN OUT HERE rather
/// than read from `CfdFieldMap`, so an expansion that agreed with a corrupted
/// layout would not agree with this.
[[nodiscard]] std::size_t blockedSlot(std::size_t component, std::size_t cell, std::size_t cells) {
    return component * cells + cell;
}

/// P17's INTERLEAVED numbering, `cell * 3 + comp`, which ADR-066 Decision 3
/// named as the hazard. Present so the blocked claim is a DISCRIMINATING one:
/// the two are asserted to differ wherever a mesh has more than one cell, so an
/// expansion that switched to interleaving fails rather than passing on an
/// arithmetic coincidence.
[[nodiscard]] std::size_t interleavedSlot(std::size_t component, std::size_t cell,
                                          std::size_t /*cells*/) {
    return cell * bettercad::cfd::kVectorComponents.size() + component;
}

// --- mirror member lists ---------------------------------------------------
//
// THE INSTRUMENT ADR-068 DECISIONS 2 AND 3 REST ON. A member added to either
// type changes its size and breaks the build here, which is what makes
// "the prepared field holds no array" enforceable rather than merely written
// down. It does NOT catch a member RESIZED -- swapping a per-patch vector for a
// per-cell one keeps `sizeof` identical -- and `CfdFieldMap.hpp` records the
// same limitation of the same device, so the claim is not overstated.

struct InitialFieldSourceMirror {
    ObjectId body;
    meshing::MeshStamp mesh;
    std::size_t cells;
    CfdInitialCondition initialCondition;
};
static_assert(sizeof(CfdInitialFieldSource) == sizeof(InitialFieldSourceMirror),
              "CfdInitialFieldSource grew or lost a member: ADR-068 Decision 7 fixes it at four, "
              "and each omission has a recorded reason");

struct PreparedInitialFieldMirror {
    CfdInitialFieldSource source;
    CfdFieldMap layout;
};
static_assert(sizeof(PreparedCfdInitialField) == sizeof(PreparedInitialFieldMirror),
              "PreparedCfdInitialField grew a member. If it is an array, ADR-068 Decision 2 and 3 "
              "forbid it: the expansion writes into the caller's memory and this type owns no "
              "field storage");

/// The canonical record is a pure value (ADR-068, Decision 9). Not a claim in a
/// comment: a pointer or a handle smuggled in here would change the size.
static_assert(sizeof(CfdInitialCondition) == sizeof(Velocity3) + sizeof(Pressure));
static_assert(std::is_trivially_copyable_v<CfdInitialCondition>);

/// The document, the mesh, the model, the topology and the layout, prepared
/// through the production path. The same harness `CfdFieldTests.cpp` uses, which
/// lives in that translation unit's anonymous namespace and so is written again
/// here rather than shared.
struct PreparedFluid {
    static Error notPrepared() { return Error{ErrorCode::FailedPrecondition, "not prepared yet"}; }

    meshref::MeshedReference model;
    MaterialId material{};
    Result<cfd::CfdModel> input{std::unexpected(notPrepared())};
    Result<cfd::CfdMeshContract> contract{std::unexpected(notPrepared())};
    Result<CfdTopology> topology{std::unexpected(notPrepared())};
    Result<CfdFieldMap> layout{std::unexpected(notPrepared())};

    explicit PreparedFluid(MeshReferenceModelKind kind) : model(document(kind)) {
        assignWater(model.document(), material);
        (void)model.regenerate();
        (void)model.require();
        prepare();
    }

    static Document document(MeshReferenceModelKind kind) {
        Result<Document> built = bettercad::reference::buildMeshReferenceModel(kind);
        REQUIRE(built.has_value());
        return std::move(*built);
    }

    void prepare() {
        input = cfd::requireCfdModel(model.document(), model.regenerator(), model.mesher(), model.control());
        INFO((input.has_value() ? std::string{} : input.error().message));
        REQUIRE(input.has_value());
        contract = cfd::requireCfdMesh(*input);
        INFO((contract.has_value() ? std::string{} : contract.error().message));
        REQUIRE(contract.has_value());
        topology = cfd::requireCfdTopology(*input, *contract);
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());
        layout = cfd::requireCfdFieldMap(*input, *topology);
        INFO((layout.has_value() ? std::string{} : layout.error().message));
        REQUIRE(layout.has_value());
    }

    [[nodiscard]] const meshing::Mesh& mesh() const { return input->mesh().mesh(); }

    void setDensity(Density density) {
        materials::MechanicalProperties mechanical =
            features::findMaterial(model.document(), material)->definition().mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(density);
        REQUIRE(features::setMaterialMechanical(model.document(), material, mechanical).has_value());
    }

    /// Puts a valid steady analysis in the document, carrying @p initial.
    CfdAnalysisId addAnalysis(CfdInitialCondition initial = {}) {
        CfdAnalysisDefinition definition;
        definition.mesh = model.control();
        definition.referencePressure = cfd::CfdReferencePressure{.value = 0_Pa, .location = Point3D{}};
        definition.initialCondition = initial;
        Result<std::unique_ptr<cfd::CfdAnalysis>> study =
            cfd::CfdAnalysis::create("Flow", std::move(definition));
        INFO((study.has_value() ? std::string{} : study.error().message));
        REQUIRE(study.has_value());
        const ObjectId added = require(model.document().addObject(std::move(*study)));
        (void)model.regenerate();
        return CfdAnalysisId::fromValue(added.value());
    }

    /// The definition the DOCUMENT holds, read afresh every time.
    [[nodiscard]] const CfdAnalysisDefinition& definitionOf(CfdAnalysisId analysis) {
        const cfd::CfdAnalysis* study = cfd::findCfdAnalysis(model.document(), analysis);
        REQUIRE(study != nullptr);
        return study->definition();
    }

    /// Edits the analysis IN THE DOCUMENT through the tracked path, and returns
    /// whether the edit was an effective change.
    template <typename Edit>
    [[nodiscard]] bool edit(CfdAnalysisId analysis, Edit apply) {
        const ObjectId object = ObjectId::fromValue(analysis.value());
        const Result<bool> changed = model.document().modifyObject<cfd::CfdAnalysis>(
            object, [&apply](cfd::CfdAnalysis& study) -> Result<bool> {
                CfdAnalysisDefinition definition = study.definition();
                apply(definition);
                return study.setDefinition(std::move(definition));
            });
        INFO((changed.has_value() ? std::string{} : changed.error().message));
        REQUIRE(changed.has_value());
        return *changed;
    }

    void remeshWithTargetSize(Length target) {
        const ObjectId control = model.controlObject();
        REQUIRE(model.document()
                    .modifyObject<meshing::MeshControl>(
                        control,
                        [target](meshing::MeshControl& object) -> Result<bool> {
                            meshing::MeshControlDefinition definition = object.definition();
                            definition.mesh.sizing.globalTargetSize = target;
                            return object.setDefinition(definition).has_value();
                        })
                    .has_value());
        (void)model.regenerate();
        (void)model.require();
        prepare();
    }
};

/// Prepares an initial field for the analysis the document holds.
[[nodiscard]] Result<PreparedCfdInitialField> fieldOf(PreparedFluid& fluid, CfdAnalysisId analysis) {
    return cfd::prepareCfdInitialField(*fluid.input, *fluid.topology, fluid.definitionOf(analysis));
}

/// The source the document's current state would produce.
[[nodiscard]] CfdInitialFieldSource sourceOf(PreparedFluid& fluid, CfdAnalysisId analysis) {
    return cfd::currentInitialFieldSource(*fluid.input, *fluid.topology, fluid.definitionOf(analysis));
}

}   // namespace

// ---------------------------------------------------------------------------
// The canonical record stays canonical intent
// ---------------------------------------------------------------------------

TEST_CASE("CfdInitialField_TheCanonicalRecordCarriesNothingMeshLocal", "[cfd][ic]") {
    // The static_asserts above carry the structural half. This carries the
    // observable half: the record is two typed quantities, and the only way to
    // change what a field holds is to change one of them.
    const CfdInitialCondition a{.velocity = velocity(0.5, -0.25, 2.0), .pressure = 300_Pa};
    CfdInitialCondition b = a;
    CHECK(a == b);
    b.velocity.y = Velocity::fromSi(-0.250000000000001);
    CHECK_FALSE(a == b);

    // A default record is a legal starting state: still fluid, zero velocity,
    // zero gauge pressure. Validated, not merely constructible.
    CHECK(cfd::validate(CfdInitialCondition{}).has_value());
}

TEST_CASE("CfdInitialField_EveryVelocityComponentAndThePressureRejectNaNAndBothInfinities",
          "[cfd][ic]") {
    // THE GAP THIS MILESTONE MEASURED AND CLOSED. P19-DATA-001's own test
    // exercises NaN in velocity.x and +Inf in the pressure, and nothing else:
    // it would pass unchanged if `isFinite(CartesianVector)` checked only x.
    // Twelve cases here -- four slots by three non-finite values -- so the
    // implementation's conjunction is actually measured.
    constexpr std::array<double, 3> kBad{kNaN, kInf, -kInf};

    for (const double bad : kBad) {
        INFO("bad value " << bad);

        CHECK_THAT(refusalOf(cfd::validate(CfdInitialCondition{.velocity = velocity(bad, 0.0, 0.0)}))
                       .message,
                   ContainsSubstring("initial velocity"));
        CHECK_THAT(refusalOf(cfd::validate(CfdInitialCondition{.velocity = velocity(0.0, bad, 0.0)}))
                       .message,
                   ContainsSubstring("initial velocity"));
        CHECK_THAT(refusalOf(cfd::validate(CfdInitialCondition{.velocity = velocity(0.0, 0.0, bad)}))
                       .message,
                   ContainsSubstring("initial velocity"));
        CHECK_THAT(
            refusalOf(cfd::validate(CfdInitialCondition{.pressure = Pressure::fromSi(bad)})).message,
            ContainsSubstring("initial pressure"));
    }
}

TEST_CASE("CfdInitialField_TheFingerprintChangesForEveryInitialConditionValue", "[cfd][ic]") {
    // ADR-068 Decision 9: the fingerprint is LOSSLESS for the record. Four
    // values, each perturbed by one ulp-ish amount on its own, and each must
    // change the fingerprint -- so a persistence milestone can compare
    // fingerprints and know the initial condition is included.
    CfdAnalysisDefinition base;
    base.mesh = MeshControlId::fromValue(1);
    base.referencePressure = cfd::CfdReferencePressure{.value = 0_Pa, .location = Point3D{}};
    base.initialCondition = CfdInitialCondition{.velocity = velocity(1.0, 2.0, 3.0),
                                                .pressure = 1000_Pa};

    const cfd::CfdDefinitionFingerprint reference = cfd::fingerprint(base);

    const std::array<CfdInitialCondition, 4> perturbed{
        CfdInitialCondition{.velocity = velocity(1.0000000000000002, 2.0, 3.0), .pressure = 1000_Pa},
        CfdInitialCondition{.velocity = velocity(1.0, 2.0000000000000004, 3.0), .pressure = 1000_Pa},
        CfdInitialCondition{.velocity = velocity(1.0, 2.0, 3.0000000000000004), .pressure = 1000_Pa},
        CfdInitialCondition{.velocity = velocity(1.0, 2.0, 3.0),
                            .pressure = Pressure::fromSi(1000.0000000000002)},
    };

    std::set<std::string> texts{cfd::canonicalText(base)};
    for (const CfdInitialCondition& initial : perturbed) {
        CfdAnalysisDefinition edited = base;
        edited.initialCondition = initial;
        INFO(cfd::canonicalText(edited));
        // The record really did change, so the case is not vacuous.
        REQUIRE_FALSE(edited.initialCondition == base.initialCondition);
        CHECK_FALSE(cfd::fingerprint(edited) == reference);
        texts.insert(cfd::canonicalText(edited));
    }
    // Five distinct canonical texts: the four perturbations are distinct from
    // each other too, not merely from the base.
    CHECK(texts.size() == 5);
}

TEST_CASE("CfdInitialField_TwoDefinitionsThatCompareEqualShareAFingerprint", "[cfd][ic]") {
    // THE OTHER HALF OF "EXACT", and the one a careless fingerprint breaks:
    // -0.0 == 0.0 is true in IEEE and so `operator==` calls two such records
    // equal. `number()` normalises -0.0 to 0.0 for exactly that reason, and this
    // asserts the two stay consistent. A fingerprint that distinguished them
    // would report a change no equality comparison can see.
    CfdAnalysisDefinition positive;
    positive.mesh = MeshControlId::fromValue(1);
    positive.referencePressure = cfd::CfdReferencePressure{.value = 0_Pa, .location = Point3D{}};
    positive.initialCondition = CfdInitialCondition{.velocity = velocity(0.0, 0.0, 0.0),
                                                    .pressure = Pressure::fromSi(0.0)};

    CfdAnalysisDefinition negative = positive;
    negative.initialCondition = CfdInitialCondition{.velocity = velocity(-0.0, -0.0, -0.0),
                                                    .pressure = Pressure::fromSi(-0.0)};

    // The premise: the two really are equal records with different bit
    // patterns, so the case is about normalisation and not about nothing.
    REQUIRE(positive.initialCondition == negative.initialCondition);
    REQUIRE(std::signbit(negative.initialCondition.pressure.si()));
    REQUIRE_FALSE(std::signbit(positive.initialCondition.pressure.si()));

    CHECK(cfd::canonicalText(positive) == cfd::canonicalText(negative));
    CHECK(cfd::fingerprint(positive) == cfd::fingerprint(negative));
}

TEST_CASE("CfdInitialField_SettingAnUnchangedInitialConditionIsNoEffectiveChange", "[cfd][ic]") {
    // ADR-068 Decision 9: there is nothing for a later undo to restore when
    // nothing moved. Through the DOCUMENT, so it is canonical state being set.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdInitialCondition initial{.velocity = velocity(0.1, 0.0, 0.0), .pressure = 50_Pa};
    const CfdAnalysisId analysis = fluid.addAnalysis(initial);

    CHECK_FALSE(fluid.edit(analysis, [initial](CfdAnalysisDefinition& d) { d.initialCondition = initial; }));
    CHECK(fluid.definitionOf(analysis).initialCondition == initial);

    CHECK(fluid.edit(analysis, [](CfdAnalysisDefinition& d) { d.initialCondition.pressure = 51_Pa; }));
    CHECK(fluid.definitionOf(analysis).initialCondition.pressure == 51_Pa);
}

TEST_CASE("CfdInitialField_ARefusedEditLeavesTheInitialConditionUnchanged", "[cfd][ic]") {
    // ATOMIC: a failed `setDefinition` commits nothing, so a non-finite initial
    // condition cannot be left half-applied in canonical state.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdInitialCondition good{.velocity = velocity(0.25, 0.0, 0.0), .pressure = 10_Pa};
    const CfdAnalysisId analysis = fluid.addAnalysis(good);

    const ObjectId object = ObjectId::fromValue(analysis.value());
    const Result<bool> refused = fluid.model.document().modifyObject<cfd::CfdAnalysis>(
        object, [](cfd::CfdAnalysis& study) -> Result<bool> {
            CfdAnalysisDefinition definition = study.definition();
            definition.initialCondition.pressure = Pressure::fromSi(kNaN);
            return study.setDefinition(std::move(definition));
        });
    CHECK_FALSE(refused.has_value());

    // The document still holds the good record, bit for bit.
    CHECK(fluid.definitionOf(analysis).initialCondition == good);
}

// ---------------------------------------------------------------------------
// Preparing, and the refusals
// ---------------------------------------------------------------------------

TEST_CASE("CfdInitialField_ARealAnalysisPreparesAndBindsToItsOwnMesh", "[cfd][ic]") {
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdInitialCondition initial{.velocity = velocity(1.5, -0.5, 0.25), .pressure = 2500_Pa};
    const CfdAnalysisId analysis = fluid.addAnalysis(initial);

    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());

    CHECK(field->initialCondition() == initial);
    CHECK(field->cells() == fluid.topology->cells());
    // The source's count corroborates the layout's rather than replacing it.
    CHECK(field->source().cells == field->layout().cells());
    CHECK(field->source().mesh == fluid.topology->mesh());
    CHECK(field->source().body == fluid.input->body());
    CHECK(cfd::isCurrent(*field, sourceOf(fluid, analysis)));

    // The lengths are the layout's, per variable, and MassFlux has none here.
    CHECK(field->size(CfdVariable::Pressure) == fluid.topology->cells());
    CHECK(field->size(CfdVariable::Velocity) == 3 * fluid.topology->cells());
    CHECK(field->size(CfdVariable::MassFlux) == 0);
}

TEST_CASE("CfdInitialField_ANonFiniteRecordIsRefusedBeforeTheLayoutIsConsulted", "[cfd][ic]") {
    // PHASE ORDER. A definition that is faulty in two ways at once -- a
    // non-finite record AND a topology with no cells -- reports the record,
    // because that is the first thing wrong and the one a user can act on.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis();

    CfdAnalysisDefinition broken = fluid.definitionOf(analysis);
    broken.initialCondition.velocity.z = Velocity::fromSi(kInf);

    const meshing::Mesh empty = meshing::MeshBuilder{}.build();
    const Result<CfdTopology> emptyTopology = cfd::buildCfdTopology(empty, {});
    REQUIRE(emptyTopology.has_value());
    REQUIRE(emptyTopology->cells() == 0);

    // Both faults present: the record wins.
    CHECK(cfd::cfdInitialFieldProblem(*fluid.input, *emptyTopology, broken) ==
          CfdInitialFieldProblem::InitialConditionNotFinite);
    // The record alone, against a good topology: the same answer.
    CHECK(cfd::cfdInitialFieldProblem(*fluid.input, *fluid.topology, broken) ==
          CfdInitialFieldProblem::InitialConditionNotFinite);
    // The topology alone: now the layout is named.
    CHECK(cfd::cfdInitialFieldProblem(*fluid.input, *emptyTopology, fluid.definitionOf(analysis)) ==
          CfdInitialFieldProblem::LayoutUnavailable);
}

TEST_CASE("CfdInitialField_ATopologyOfAnotherMeshIsRefusedByName", "[cfd][ic]") {
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis();
    const CfdTopology before = *fluid.topology;

    fluid.remeshWithTargetSize(3_mm);

    // The stale topology against the new model: the staged gate refuses it, so a
    // wrong array length is never discovered later as a length mismatch.
    CHECK(cfd::cfdInitialFieldProblem(*fluid.input, before, fluid.definitionOf(analysis)) ==
          CfdInitialFieldProblem::LayoutUnavailable);
    const Result<PreparedCfdInitialField> refused =
        cfd::prepareCfdInitialField(*fluid.input, before, fluid.definitionOf(analysis));
    REQUIRE_FALSE(refused.has_value());
    // THE TAG IS SHARED WITH "no cells", so the tag alone does not say which
    // fault it was -- a review pointed out that the name of this case claimed
    // more than it checked. The MESSAGE distinguishes them, and asserting it is
    // what makes the name true.
    CHECK_THAT(refused.error().message, ContainsSubstring("generation"));
    // The current one still prepares.
    CHECK(fieldOf(fluid, analysis).has_value());
}

TEST_CASE("CfdInitialField_ThePredictorAndThePreparedPathNeverDisagree", "[cfd][ic]") {
    // One function decides, two entry points report it. Asserted over every
    // case this file can present rather than claimed in a comment.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis();

    const meshing::Mesh empty = meshing::MeshBuilder{}.build();
    const Result<CfdTopology> emptyTopology = cfd::buildCfdTopology(empty, {});
    REQUIRE(emptyTopology.has_value());

    CfdAnalysisDefinition nonFinite = fluid.definitionOf(analysis);
    nonFinite.initialCondition.pressure = Pressure::fromSi(kNaN);

    const std::array<std::pair<const CfdTopology*, CfdAnalysisDefinition>, 4> cases{{
        {&*fluid.topology, fluid.definitionOf(analysis)},
        {&*fluid.topology, nonFinite},
        {&*emptyTopology, fluid.definitionOf(analysis)},
        {&*emptyTopology, nonFinite},
    }};

    for (const auto& [topology, definition] : cases) {
        const std::optional<CfdInitialFieldProblem> predicted =
            cfd::cfdInitialFieldProblem(*fluid.input, *topology, definition);
        const Result<PreparedCfdInitialField> prepared =
            cfd::prepareCfdInitialField(*fluid.input, *topology, definition);
        CHECK(predicted.has_value() == !prepared.has_value());

        const std::optional<cfd::CfdInitialFieldFailure> failure =
            cfd::cfdInitialFieldFailure(*fluid.input, *topology, definition);
        CHECK(failure.has_value() == predicted.has_value());
        if (failure.has_value() && predicted.has_value()) {
            CHECK(failure->problem == *predicted);
            // The tagged failure's message is the one the Result carries.
            REQUIRE_FALSE(prepared.has_value());
            CHECK(failure->error.message == prepared.error().message);
        }
    }
}

// ---------------------------------------------------------------------------
// Expanding into the caller's array
// ---------------------------------------------------------------------------

TEST_CASE("CfdInitialField_AUniformPressureFillsEveryCellSlot", "[cfd][ic]") {
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdInitialCondition initial{.velocity = {}, .pressure = -1234.5_Pa};
    const CfdAnalysisId analysis = fluid.addAnalysis(initial);
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    // The caller owns the array and sizes it from the field.
    std::vector<double> p(field->size(CfdVariable::Pressure), kNaN);
    REQUIRE(p.size() > 1);
    REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Pressure, p).has_value());

    // Against the canonical record directly, not against anything the
    // implementation reported.
    const double expected = initial.pressure.si();
    for (std::size_t cell = 0; cell < p.size(); ++cell) {
        INFO("cell " << cell);
        CHECK(p[cell] == expected);
    }
}

TEST_CASE("CfdInitialField_AUniformVelocityIsWrittenComponentBlocked", "[cfd][ic]") {
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdInitialCondition initial{.velocity = velocity(7.0, -11.0, 13.0), .pressure = 0_Pa};
    const CfdAnalysisId analysis = fluid.addAnalysis(initial);
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    const std::size_t cells = field->cells();
    REQUIRE(cells > 1);   // or blocked and interleaved would coincide

    std::vector<double> u(field->size(CfdVariable::Velocity), kNaN);
    REQUIRE(u.size() == 3 * cells);
    REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Velocity, u).has_value());

    const std::array<double, 3> expected{initial.velocity.x.si(), initial.velocity.y.si(),
                                         initial.velocity.z.si()};

    // THREE INSTRUMENTS, AND THEY MUST ALL AGREE.
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t k = 0; k < bettercad::cfd::kVectorComponents.size(); ++k) {
            const VectorComponent component = bettercad::cfd::kVectorComponents[k];
            INFO("cell " << c << " component " << k);

            // 1. the production layout
            const std::optional<cfd::CellVectorIndex> slot =
                field->layout().vectorIndex(CellIndex::fromValue(c), component);
            REQUIRE(slot.has_value());

            // 2. the independent oracle
            const std::size_t oracle = blockedSlot(k, c, cells);
            CHECK(slot->value() == oracle);

            // 3. the value that landed there
            CHECK(u[oracle] == expected[k]);
        }
    }

    // THE DISCRIMINATING PART. If the expansion used P17's interleaving, the
    // slot for a component would differ from the blocked one somewhere, and the
    // values would land in the wrong places. Assert the two numberings really do
    // differ on this mesh, so the agreement above is not a coincidence of a
    // one-cell mesh, and that the array does NOT match the interleaved reading
    // wherever they differ.
    // An earlier version of this block wrote
    //     if (misread != expected[k]) { CHECK(misread != expected[k]); }
    // which is a CHECK guarded by its own predicate and so cannot fail. A review
    // caught it, and also established where the real discrimination lives: the
    // main loop above, whose CHECK(u[oracle] == expected[k]) is what actually
    // fails under a mutation to interleaved indexing. What is asserted here is
    // the narrower thing this block can honestly carry -- that the two
    // numberings DO disagree on this mesh, so the agreement above is not a
    // coincidence of a mesh too small to tell them apart.
    std::size_t differing = 0;
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t k = 0; k < bettercad::cfd::kVectorComponents.size(); ++k) {
            if (blockedSlot(k, c, cells) != interleavedSlot(k, c, cells)) {
                ++differing;
            }
        }
    }
    CHECK(differing > 0);

    // AND THE DISCRIMINATION MADE DIRECT: reading the array by the interleaved
    // numbering must disagree with reading it by the blocked one somewhere,
    // whenever the velocity components are distinct. Unconditional, so it can
    // fail.
    REQUIRE(expected[0] != expected[1]);
    std::vector<double> blockedRead;
    std::vector<double> interleavedRead;
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t k = 0; k < bettercad::cfd::kVectorComponents.size(); ++k) {
            blockedRead.push_back(u[blockedSlot(k, c, cells)]);
            interleavedRead.push_back(u[interleavedSlot(k, c, cells)]);
        }
    }
    CHECK(blockedRead != interleavedRead);
}

TEST_CASE("CfdInitialField_MassFluxIsRefusedByName", "[cfd][ic]") {
    // ADR-068 Decision 5. `phi` is face-centred and ADR-067's table already
    // makes it Computed everywhere: an outcome, never a datum.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis();
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    // By TAG, not by message.
    CHECK(cfd::cfdInitialFieldExpansionProblem(*field, CfdVariable::MassFlux,
                                               fluid.topology->faces()) ==
          CfdInitialFieldProblem::VariableIsNotCellCentred);
    // And at the length `size()` would suggest, which is 0 -- so the refusal is
    // not an accident of a length mismatch.
    CHECK(cfd::cfdInitialFieldExpansionProblem(*field, CfdVariable::MassFlux, 0) ==
          CfdInitialFieldProblem::VariableIsNotCellCentred);

    std::vector<double> phi(fluid.topology->faces(), 0.0);
    const Result<void> refused = cfd::expandCfdInitialField(*field, CfdVariable::MassFlux, phi);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("not cell-centred"));
    CHECK_THAT(refused.error().message, ContainsSubstring("outcome"));
}

TEST_CASE("CfdInitialField_AnUnknownVariableIsRefusedBeforeTheLength", "[cfd][ic]") {
    // THE ORDER IS LOAD-BEARING, and this is the case that proves it. `size()`
    // answers 0 for an unrecognised variable, so an unknown variable offered a
    // ZERO-LENGTH array would pass a length-first check and succeed having
    // written nothing -- a silent no-op where a refusal belongs.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis();
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    const auto unknown = static_cast<CfdVariable>(bettercad::cfd::kCfdVariables.size());
    CHECK(field->size(unknown) == 0);

    CHECK(cfd::cfdInitialFieldExpansionProblem(*field, unknown, 0) ==
          CfdInitialFieldProblem::VariableIsUnknown);
    CHECK(cfd::cfdInitialFieldExpansionProblem(*field, unknown, 17) ==
          CfdInitialFieldProblem::VariableIsUnknown);

    std::vector<double> nothing;
    const Result<void> refused = cfd::expandCfdInitialField(*field, unknown, nothing);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("not one this build defines"));
}

TEST_CASE("CfdInitialField_AWronglySizedArrayIsRefusedAndLeftUntouched", "[cfd][ic]") {
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis =
        fluid.addAnalysis(CfdInitialCondition{.velocity = velocity(1.0, 1.0, 1.0),
                                              .pressure = 99_Pa});
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    const std::size_t right = field->size(CfdVariable::Pressure);
    REQUIRE(right > 1);

    // BOTH DIRECTIONS. A longer array is refused too, because the tail this
    // call would not write is a tail the caller believes is initialised.
    for (const std::size_t wrong : {right - 1, right + 1, std::size_t{0}}) {
        INFO("length " << wrong << " against " << right);
        CHECK(cfd::cfdInitialFieldExpansionProblem(*field, CfdVariable::Pressure, wrong) ==
              CfdInitialFieldProblem::ArrayLengthWrong);

        // PRE-POISONED, so a partial write is visible. The refusal must leave
        // every slot exactly as it was.
        const double poison = -12345.0;
        std::vector<double> values(wrong, poison);
        const Result<void> refused =
            cfd::expandCfdInitialField(*field, CfdVariable::Pressure, values);
        REQUIRE_FALSE(refused.has_value());
        CHECK_THAT(refused.error().message, ContainsSubstring("needs"));
        CHECK(std::ranges::all_of(values, [poison](double v) { return v == poison; }));
    }

    // A velocity array sized as though it were a pressure array is the likely
    // mistake, and it is refused.
    std::vector<double> asPressure(right, 0.0);
    CHECK(cfd::cfdInitialFieldExpansionProblem(*field, CfdVariable::Velocity, asPressure.size()) ==
          CfdInitialFieldProblem::ArrayLengthWrong);
}

TEST_CASE("CfdInitialField_TwoExpansionsOfOneFieldAreIdentical", "[cfd][ic]") {
    // DETERMINISM: no unordered iteration, no accumulation, no dependence on
    // call order.
    PreparedFluid fluid(MeshReferenceModelKind::Cylinder);
    const CfdAnalysisId analysis =
        fluid.addAnalysis(CfdInitialCondition{.velocity = velocity(0.3, -0.7, 1.1),
                                              .pressure = 777_Pa});
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    for (const CfdVariable variable : {CfdVariable::Pressure, CfdVariable::Velocity}) {
        std::vector<double> first(field->size(variable), kNaN);
        std::vector<double> second(field->size(variable), kInf);
        REQUIRE(cfd::expandCfdInitialField(*field, variable, first).has_value());
        REQUIRE(cfd::expandCfdInitialField(*field, variable, second).has_value());
        CHECK(first == second);
    }

    // And a field prepared again from the same state expands the same way.
    const Result<PreparedCfdInitialField> again = fieldOf(fluid, analysis);
    REQUIRE(again.has_value());
    CHECK(*again == *field);
}

// ---------------------------------------------------------------------------
// Gravity does not enter a uniform field
// ---------------------------------------------------------------------------

TEST_CASE("CfdInitialField_AUniformPressureIsEqualInEveryCellWithGravityPresent", "[cfd][ic]") {
    // ADR-068 DECISION 6, and the asymmetry with ADR-067 it records. A
    // prescribed BOUNDARY pressure is hydrostatic; an initial field is not.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdInitialCondition initial{.velocity = {}, .pressure = 101325_Pa};
    const CfdAnalysisId analysis = fluid.addAnalysis(initial);

    CHECK(fluid.edit(analysis, [](CfdAnalysisDefinition& d) {
        d.gravity = cfd::CfdGravity{.acceleration = {.x = 0_m_per_s2, .y = 0_m_per_s2,
                                                     .z = -9.81_m_per_s2},
                                    .referenceHeight = 0_m};
    }));
    // The premise: gravity really is in canonical state.
    REQUIRE(fluid.definitionOf(analysis).gravity.has_value());

    // AND THE OTHER PREMISE, measured with an instrument this field never
    // consults: the cells span a height range, so "equal everywhere" is a
    // statement about something. CfdGeometry is not a dependency of an initial
    // field, which is the point -- it cannot have influenced the answer.
    const Result<cfd::CfdGeometry> geometry = cfd::requireCfdGeometry(*fluid.input, *fluid.topology);
    REQUIRE(geometry.has_value());
    double lowest = std::numeric_limits<double>::infinity();
    double highest = -std::numeric_limits<double>::infinity();
    for (std::size_t c = 0; c < fluid.topology->cells(); ++c) {
        const std::optional<Point3D> centroid = geometry->cellCentroid(CellIndex::fromValue(c));
        REQUIRE(centroid.has_value());
        lowest = std::min(lowest, centroid->z.si());
        highest = std::max(highest, centroid->z.si());
    }
    REQUIRE(highest - lowest > 1e-6);

    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());
    std::vector<double> p(field->size(CfdVariable::Pressure), kNaN);
    REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Pressure, p).has_value());

    // EXACTLY equal, not approximately: no arithmetic was performed on it.
    CHECK(std::ranges::all_of(
        p, [expected = initial.pressure.si()](double v) { return v == expected; }));

    // A hydrostatic field over this height range would have spanned at least
    // this much, so the assertion above would have failed had gravity entered.
    const double wouldHaveSpanned = fluid.input->density().si() * 9.81 * (highest - lowest);
    CHECK(wouldHaveSpanned > 1e-3);
}

// ---------------------------------------------------------------------------
// Currency: what stales a field and what provably cannot
// ---------------------------------------------------------------------------

TEST_CASE("CfdInitialField_AnInitialConditionEditStalesTheFieldAndNamesIt", "[cfd][ic]") {
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis =
        fluid.addAnalysis(CfdInitialCondition{.velocity = velocity(1.0, 0.0, 0.0),
                                              .pressure = 10_Pa});
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());
    REQUIRE(cfd::isCurrent(*field, sourceOf(fluid, analysis)));

    CHECK(fluid.edit(analysis,
                     [](CfdAnalysisDefinition& d) { d.initialCondition.pressure = 11_Pa; }));

    const CfdInitialFieldSource now = sourceOf(fluid, analysis);
    CHECK_FALSE(cfd::isCurrent(*field, now));
    const std::vector<CfdInitialFieldStaleReason> reasons = cfd::staleReasons(*field, now);
    CHECK(reasons == std::vector{CfdInitialFieldStaleReason::InitialCondition});
}

TEST_CASE("CfdInitialField_ARemeshStalesTheFieldAndNamesTheMesh", "[cfd][ic]") {
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis();
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());
    const std::size_t before = field->cells();

    fluid.remeshWithTargetSize(3_mm);

    const CfdInitialFieldSource now = sourceOf(fluid, analysis);
    CHECK_FALSE(cfd::isCurrent(*field, now));
    CHECK(cfd::staleReasons(*field, now) == std::vector{CfdInitialFieldStaleReason::Mesh});

    // A NEW GENERATION STALES IT EVEN IF THE COUNTS MATCH, because the stamp is
    // the identity -- and this REQUIRE is what stops that claim resting on a
    // count comparison. An earlier version only INFO'd the two counts, which a
    // review pointed out is exactly what lets the claim rest silently: if a
    // meshing change ever made this remesh yield a different count, the stamp
    // comparison would become untested with nothing failing. The reviewer
    // MEASURED that the 3 mm remesh of the Block keeps the count, by deleting
    // the stamp comparison and watching this case fail, so the equality below
    // is a recorded fact and not a hope.
    const Result<PreparedCfdInitialField> after = fieldOf(fluid, analysis);
    REQUIRE(after.has_value());
    REQUIRE(after->cells() == before);
    CHECK(cfd::isCurrent(*after, now));
}

TEST_CASE("CfdInitialField_TheOmittedDependenciesLeaveTheFieldCurrent", "[cfd][ic]") {
    // ADR-068 DECISION 7, ASSERTED ONE OMISSION AT A TIME. Each of these edits
    // changes canonical state, and none of them can change what a uniform
    // cell-centred field holds -- so the field must stay CURRENT. A source that
    // carried any of them would stale here and fail this case, which is what
    // makes the omissions measured rather than asserted.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis =
        fluid.addAnalysis(CfdInitialCondition{.velocity = velocity(0.5, 0.0, 0.0),
                                              .pressure = 20_Pa});
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());
    REQUIRE(cfd::isCurrent(*field, sourceOf(fluid, analysis)));

    SECTION("a gravity edit") {
        CHECK(fluid.edit(analysis, [](CfdAnalysisDefinition& d) {
            d.gravity = cfd::CfdGravity{.acceleration = {.x = 0_m_per_s2, .y = 0_m_per_s2,
                                                         .z = -9.81_m_per_s2},
                                        .referenceHeight = 0_m};
        }));
        CHECK(cfd::isCurrent(*field, sourceOf(fluid, analysis)));
    }

    SECTION("a reference-pressure edit") {
        CHECK(fluid.edit(analysis, [](CfdAnalysisDefinition& d) {
            d.referencePressure =
                cfd::CfdReferencePressure{.value = 500_Pa, .location = Point3D{}};
        }));
        CHECK(cfd::isCurrent(*field, sourceOf(fluid, analysis)));
    }

    SECTION("a boundary-condition edit") {
        CHECK(fluid.edit(analysis, [this_body = fluid.model.body()](CfdAnalysisDefinition& d) {
            d.boundaryConditions.emplace_back(CfdBoundaryConditionId::fromValue(1),
                                              cfd::NoSlipWall{FaceName{this_body, FaceSelector{}}});
        }));
        CHECK(cfd::isCurrent(*field, sourceOf(fluid, analysis)));
    }

    SECTION("a fluid-density edit") {
        const meshing::MeshStamp stamp = fluid.mesh().stamp();
        fluid.setDensity(Density::fromSi(1200.0));
        // THE PREMISE, AND THE WHOLE POINT: a mesh is a function of geometry and
        // meshing intent, and a density is in neither, so no remesh is needed
        // and the generation does not move. An earlier spelling of this case
        // called regenerate() here, which issues a NEW generation and stales the
        // field -- it would have accused the source binding of carrying the
        // fluid when the test itself had remeshed.
        REQUIRE(fluid.model.currency() == meshing::MeshCurrency::Current);
        fluid.prepare();
        REQUIRE(fluid.mesh().stamp() == stamp);
        REQUIRE(fluid.input->density() == Density::fromSi(1200.0));

        CHECK(cfd::isCurrent(*field, sourceOf(fluid, analysis)));
        CHECK(cfd::staleReasons(*field, sourceOf(fluid, analysis)).empty());
    }

    SECTION("a solver-settings edit") {
        CHECK(fluid.edit(analysis,
                         [](CfdAnalysisDefinition& d) { d.iteration.velocityRelaxation = 0.5; }));
        CHECK(cfd::isCurrent(*field, sourceOf(fluid, analysis)));
    }
}

TEST_CASE("CfdInitialField_StaleReasonsComeOutInTheEnumsOrder", "[cfd][ic]") {
    // Two dependencies moved at once: the order is the enum's, so a diagnostic
    // reads the same way every time.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis();
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    CfdInitialFieldSource moved = sourceOf(fluid, analysis);
    moved.body = ObjectId::fromValue(moved.body.value() + 1);
    moved.cells = moved.cells + 1;
    moved.initialCondition.pressure = 1_Pa;

    const std::vector<CfdInitialFieldStaleReason> reasons = cfd::staleReasons(*field, moved);
    CHECK(reasons == std::vector{CfdInitialFieldStaleReason::Body, CfdInitialFieldStaleReason::Mesh,
                                 CfdInitialFieldStaleReason::InitialCondition});
    // And they are in the declared order of the enum, not merely in this order.
    CHECK(std::ranges::is_sorted(reasons));
}

// ---------------------------------------------------------------------------
// Every reference body
// ---------------------------------------------------------------------------

TEST_CASE("CfdInitialField_EveryReferenceBodyExpandsToItsOwnCellCount", "[cfd][ic][reference]") {
    const CfdInitialCondition initial{.velocity = velocity(2.0, 3.0, 5.0), .pressure = 1500_Pa};
    const std::array<double, 3> expected{initial.velocity.x.si(), initial.velocity.y.si(),
                                         initial.velocity.z.si()};

    std::size_t meshed = 0;
    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            // The one model with no body, and asserted to be that one: a filter
            // that quietly grew would make this sweep smaller than it claims.
            CHECK(info.kind == MeshReferenceModelKind::OpenProfile);
            continue;
        }
        INFO(info.id);
        PreparedFluid fluid(info.kind);
        ++meshed;
        const CfdAnalysisId analysis = fluid.addAnalysis(initial);
        const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
        INFO((field.has_value() ? std::string{} : field.error().message));
        REQUIRE(field.has_value());

        const std::size_t cells = field->cells();
        REQUIRE(cells > 0);
        CHECK(field->size(CfdVariable::Pressure) == cells);
        CHECK(field->size(CfdVariable::Velocity) == 3 * cells);

        std::vector<double> p(cells, kNaN);
        std::vector<double> u(3 * cells, kNaN);
        REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Pressure, p).has_value());
        REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Velocity, u).has_value());

        CHECK(std::ranges::all_of(
            p, [want = initial.pressure.si()](double v) { return v == want; }));
        for (std::size_t c = 0; c < cells; ++c) {
            for (std::size_t k = 0; k < bettercad::cfd::kVectorComponents.size(); ++k) {
                CHECK(u[blockedSlot(k, c, cells)] == expected[k]);
            }
        }
    }
    // Every reference body but the bodiless one.
    CHECK(meshed == bettercad::reference::kMeshReferenceModels.size() - 1);
    CHECK(meshed == 8);
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

TEST_CASE("CfdInitialField_EveryProblemAndStaleReasonHasADistinctName", "[cfd][ic]") {
    std::vector<std::string_view> problems;
    for (const CfdInitialFieldProblem problem : bettercad::cfd::kCfdInitialFieldProblems) {
        problems.emplace_back(cfd::toString(problem));
        CHECK_FALSE(problems.back().empty());
        CHECK(problems.back() != "unknown");
    }
    std::ranges::sort(problems);
    CHECK(std::ranges::adjacent_find(problems) == problems.end());
    CHECK(problems.size() == 6);
    CHECK(cfd::toString(static_cast<CfdInitialFieldProblem>(
              bettercad::cfd::kCfdInitialFieldProblems.size())) == "unknown");

    std::vector<std::string_view> reasons;
    for (const CfdInitialFieldStaleReason reason : bettercad::cfd::kCfdInitialFieldStaleReasons) {
        reasons.emplace_back(cfd::toString(reason));
        CHECK_FALSE(reasons.back().empty());
        CHECK(reasons.back() != "unknown");
    }
    std::ranges::sort(reasons);
    CHECK(std::ranges::adjacent_find(reasons) == reasons.end());
    CHECK(reasons.size() == 3);
    CHECK(cfd::toString(static_cast<CfdInitialFieldStaleReason>(
              bettercad::cfd::kCfdInitialFieldStaleReasons.size())) == "unknown");
}

// ---------------------------------------------------------------------------
// Added after two independent reviews
// ---------------------------------------------------------------------------

TEST_CASE("CfdInitialField_AnAnalysisNamingAnotherMeshControlIsRefusedFirst", "[cfd][ic]") {
    // THE DEFECT AN ADVERSARIAL REVIEW FOUND. `definition.mesh` is canonical
    // intent that provably changes the expanded array's LENGTH, so it is the one
    // definition member that could not be omitted from the source binding with a
    // reason -- and before the review there was no check at all: an analysis
    // re-pointed at another mesh control prepared happily against the old model
    // and reported itself CURRENT.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis();

    CfdAnalysisDefinition elsewhere = fluid.definitionOf(analysis);
    elsewhere.mesh = MeshControlId::fromValue(fluid.model.control().value() + 1);
    REQUIRE_FALSE(elsewhere.mesh == fluid.model.control());

    CHECK(cfd::cfdInitialFieldProblem(*fluid.input, *fluid.topology, elsewhere) ==
          CfdInitialFieldProblem::DefinitionNamesAnotherMesh);
    const Result<PreparedCfdInitialField> refused =
        cfd::prepareCfdInitialField(*fluid.input, *fluid.topology, elsewhere);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("another analysis"));

    // AND IT WINS OVER THE RECORD AND THE LAYOUT, because if the caller handed
    // over the wrong analysis entirely then complaining about its pressure
    // answers the wrong question.
    CfdAnalysisDefinition alsoBroken = elsewhere;
    alsoBroken.initialCondition.pressure = Pressure::fromSi(kNaN);
    CHECK(cfd::cfdInitialFieldProblem(*fluid.input, *fluid.topology, alsoBroken) ==
          CfdInitialFieldProblem::DefinitionNamesAnotherMesh);

    const meshing::Mesh empty = meshing::MeshBuilder{}.build();
    const Result<CfdTopology> emptyTopology = cfd::buildCfdTopology(empty, {});
    REQUIRE(emptyTopology.has_value());
    CHECK(cfd::cfdInitialFieldProblem(*fluid.input, *emptyTopology, elsewhere) ==
          CfdInitialFieldProblem::DefinitionNamesAnotherMesh);

    // The matching control still prepares, so the gate discriminates.
    CHECK(fieldOf(fluid, analysis).has_value());
}

TEST_CASE("CfdInitialField_EachStaleReasonIsReportedAloneForItsOwnDependency", "[cfd][ic]") {
    // EACH HALF OF EACH COMPARISON, ISOLATED. A review found that the stamp
    // operand of `staleReasons`' `||` was never separated from the cell-count
    // operand: one case moved the count with the stamp held equal, and the only
    // case that moved the stamp was a remesh. Three hand-built sources isolate
    // all three reasons.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis =
        fluid.addAnalysis(CfdInitialCondition{.velocity = velocity(1.0, 2.0, 3.0),
                                              .pressure = 400_Pa});
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());
    const CfdInitialFieldSource current = sourceOf(fluid, analysis);
    REQUIRE(cfd::isCurrent(*field, current));

    SECTION("the mesh stamp alone, with the cell count held equal") {
        CfdInitialFieldSource moved = current;
        moved.mesh = meshing::MeshStamp{};
        REQUIRE(moved.cells == current.cells);
        REQUIRE_FALSE(moved.mesh == current.mesh);
        CHECK(cfd::staleReasons(*field, moved) == std::vector{CfdInitialFieldStaleReason::Mesh});
    }

    SECTION("the cell count alone, with the stamp held equal") {
        CfdInitialFieldSource moved = current;
        moved.cells = current.cells + 1;
        REQUIRE(moved.mesh == current.mesh);
        CHECK(cfd::staleReasons(*field, moved) == std::vector{CfdInitialFieldStaleReason::Mesh});
    }

    SECTION("the body alone") {
        CfdInitialFieldSource moved = current;
        moved.body = ObjectId::fromValue(current.body.value() + 1);
        CHECK(cfd::staleReasons(*field, moved) == std::vector{CfdInitialFieldStaleReason::Body});
    }

    SECTION("the record alone") {
        CfdInitialFieldSource moved = current;
        moved.initialCondition.velocity.y = Velocity::fromSi(2.5);
        CHECK(cfd::staleReasons(*field, moved) ==
              std::vector{CfdInitialFieldStaleReason::InitialCondition});
    }
}

TEST_CASE("CfdInitialField_TheSourcesOwnEqualityAgreesWithIsCurrent", "[cfd][ic]") {
    // THE HEADER SAYS the source's equality is "field-complete by = default, so a
    // dependency added here without being compared is not possible". A review
    // found that guarantee unenforced: `staleReasons` compares the four members
    // INDIVIDUALLY and never calls `operator==`, which no production code and no
    // test called at all -- rewriting it as `return true` broke nothing. Tying
    // the two together is what makes the header's sentence true, because a fifth
    // member compared by one and not the other now fails here.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis =
        fluid.addAnalysis(CfdInitialCondition{.velocity = velocity(0.5, 0.0, -0.5),
                                              .pressure = 30_Pa});
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    const CfdInitialFieldSource current = sourceOf(fluid, analysis);
    std::vector<CfdInitialFieldSource> candidates{current};
    {
        CfdInitialFieldSource s = current;
        s.body = ObjectId::fromValue(current.body.value() + 1);
        candidates.push_back(s);
    }
    {
        CfdInitialFieldSource s = current;
        s.mesh = meshing::MeshStamp{};
        candidates.push_back(s);
    }
    {
        CfdInitialFieldSource s = current;
        s.cells = current.cells + 1;
        candidates.push_back(s);
    }
    {
        CfdInitialFieldSource s = current;
        s.initialCondition.pressure = 31_Pa;
        candidates.push_back(s);
    }

    for (const CfdInitialFieldSource& s : candidates) {
        // The live path and the declared equality must give the same verdict.
        CHECK(cfd::isCurrent(*field, s) == (field->source() == s));
        CHECK(cfd::staleReasons(*field, s).empty() == (field->source() == s));
    }
}

TEST_CASE("CfdInitialField_FieldEqualityDiscriminatesSoDeterminismMeansSomething", "[cfd][ic]") {
    // `TwoExpansionsOfOneFieldAreIdentical` ends in CHECK(*again == *field), and
    // a review showed that `operator== -> return true` survives it. Equality has
    // to be able to say NO for that line to mean anything.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis =
        fluid.addAnalysis(CfdInitialCondition{.velocity = velocity(1.0, 0.0, 0.0),
                                              .pressure = 10_Pa});
    const Result<PreparedCfdInitialField> first = fieldOf(fluid, analysis);
    REQUIRE(first.has_value());

    // One value apart.
    CHECK(fluid.edit(analysis,
                     [](CfdAnalysisDefinition& d) { d.initialCondition.pressure = 11_Pa; }));
    const Result<PreparedCfdInitialField> second = fieldOf(fluid, analysis);
    REQUIRE(second.has_value());
    // THE PREMISES FIRST, so a failure says WHICH step is wrong rather than
    // leaving "two fields compared equal" to be guessed at. These were added
    // while diagnosing a failure that turned out to be a stale binary, and they
    // are kept because a case whose premises are asserted cannot quietly become
    // vacuous.
    INFO("first p = " << first->initialCondition().pressure.si()
                      << " second p = " << second->initialCondition().pressure.si());
    CHECK(first->initialCondition().pressure.si() == 10.0);
    CHECK(second->initialCondition().pressure.si() == 11.0);
    CHECK_FALSE(first->source().initialCondition == second->source().initialCondition);
    CHECK_FALSE(first->source() == second->source());
    CHECK_FALSE(*first == *second);

    // And one mesh generation apart, with the record restored, so the layout is
    // what differs rather than the record.
    CHECK(fluid.edit(analysis,
                     [](CfdAnalysisDefinition& d) { d.initialCondition.pressure = 10_Pa; }));
    const Result<PreparedCfdInitialField> restored = fieldOf(fluid, analysis);
    REQUIRE(restored.has_value());
    CHECK(*restored == *first);

    fluid.remeshWithTargetSize(3_mm);
    const Result<PreparedCfdInitialField> remeshed = fieldOf(fluid, analysis);
    REQUIRE(remeshed.has_value());
    CHECK_FALSE(*remeshed == *first);
}

TEST_CASE("CfdInitialField_TheExpansionPredictorAndThePathNeverDisagree", "[cfd][ic]") {
    // THE EXPANSION HALF of the agreement `ThePredictorAndThePreparedPathNeverDisagree`
    // asserts for prepare. A review found `cfdInitialFieldExpansionFailure` was
    // called by no test at all, so its TAG was never read: swapping the
    // VariableIsNotCellCentred and ArrayLengthWrong tags on the returned struct
    // left every test green. That is the exact failure mode this milestone's own
    // header cites as the reason a tag exists.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis =
        fluid.addAnalysis(CfdInitialCondition{.velocity = velocity(2.0, 4.0, 8.0),
                                              .pressure = 60_Pa});
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    const auto unknown = static_cast<CfdVariable>(bettercad::cfd::kCfdVariables.size());
    const std::array<CfdVariable, 4> variables{CfdVariable::Pressure, CfdVariable::Velocity,
                                               CfdVariable::MassFlux, unknown};

    for (const CfdVariable variable : variables) {
        const std::size_t right = field->size(variable);
        for (const std::size_t length : {std::size_t{0}, right, right + 1, right + 7}) {
            INFO("variable " << static_cast<unsigned>(variable) << " length " << length);

            const std::optional<CfdInitialFieldProblem> tag =
                cfd::cfdInitialFieldExpansionProblem(*field, variable, length);
            const std::optional<cfd::CfdInitialFieldFailure> failure =
                cfd::cfdInitialFieldExpansionFailure(*field, variable, length);

            // The tag the predictor gives and the tag the failure carries.
            CHECK(failure.has_value() == tag.has_value());
            if (failure.has_value() && tag.has_value()) {
                CHECK(failure->problem == *tag);
                CHECK_FALSE(failure->error.message.empty());
            }

            // And the path itself agrees, with the same message.
            std::vector<double> values(length, kNaN);
            const Result<void> expanded =
                cfd::expandCfdInitialField(*field, variable, values);
            CHECK(expanded.has_value() == !tag.has_value());
            if (!expanded.has_value()) {
                REQUIRE(failure.has_value());
                CHECK(expanded.error().message == failure->error.message);
            }
        }
    }
}

TEST_CASE("CfdInitialField_AGeometryRevisionAndAPatchPartitionReachNeitherTheSourceNorTheField",
          "[cfd][ic]") {
    // THE TWO OMISSIONS ADR-068 DECISION 7 CLAIMED WERE TESTED AND WERE NOT.
    // Both reviews found the same hole independently: the existing sections edit
    // gravity, the datum, a boundary CONDITION, the fluid and solver settings --
    // and a boundary-condition edit changes neither the boundary-face count nor
    // the patch partition, so neither of those two omissions was exercised.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis =
        fluid.addAnalysis(CfdInitialCondition{.velocity = velocity(0.25, 0.5, 0.75),
                                              .pressure = 70_Pa});
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());
    const CfdInitialFieldSource staged = sourceOf(fluid, analysis);
    REQUIRE(cfd::isCurrent(*field, staged));

    const std::vector<meshing::MappedFace> faces(fluid.input->map().faces().begin(),
                                                 fluid.input->map().faces().end());
    REQUIRE(faces.size() > 1);

    SECTION("a layout of the same mesh carrying a different geometry revision") {
        // The PLAIN builder takes the topology's revision; the staged gate
        // substitutes the model's. So these two layouts of ONE mesh generation
        // can differ in the revision they carry -- and the source carries none,
        // which is the claim.
        const Result<CfdTopology> plain = cfd::buildCfdTopology(fluid.mesh(), faces);
        INFO((plain.has_value() ? std::string{} : plain.error().message));
        REQUIRE(plain.has_value());
        REQUIRE(plain->cells() == fluid.topology->cells());
        REQUIRE(plain->mesh() == fluid.topology->mesh());

        const CfdInitialFieldSource fromPlain =
            cfd::currentInitialFieldSource(*fluid.input, *plain, fluid.definitionOf(analysis));
        CHECK(fromPlain == staged);
        CHECK(cfd::isCurrent(*field, fromPlain));
    }

    SECTION("a topology of the same generation with the patches in another order") {
        // ADR-066 Decision 5: "Cell fields survive that". Two topologies of one
        // generation whose patch attribution differs agree on the stamp and on
        // every cell, so a cell-centred field must stay current.
        std::vector<meshing::MappedFace> reordered = faces;
        std::ranges::reverse(reordered);
        const Result<CfdTopology> other = cfd::buildCfdTopology(fluid.mesh(), reordered);
        INFO((other.has_value() ? std::string{} : other.error().message));
        REQUIRE(other.has_value());
        REQUIRE(other->cells() == fluid.topology->cells());
        REQUIRE(other->mesh() == fluid.topology->mesh());

        // THE PREMISE: the partition really did move. Without this the section
        // would be asserting that nothing changed nothing.
        const Result<CfdFieldMap> otherLayout = cfd::buildCfdFieldMap(*other);
        REQUIRE(otherLayout.has_value());
        const Result<CfdFieldMap> sameLayout = cfd::buildCfdFieldMap(*fluid.topology);
        REQUIRE(sameLayout.has_value());
        REQUIRE_FALSE(*otherLayout == *sameLayout);
        CHECK(otherLayout->currency(*fluid.topology) ==
              cfd::CfdFieldCurrency::BoundaryLayoutChanged);
        // And the cells are untouched, which is why the field survives.
        CHECK(otherLayout->describesCells(*fluid.topology));

        const CfdInitialFieldSource fromOther =
            cfd::currentInitialFieldSource(*fluid.input, *other, fluid.definitionOf(analysis));
        CHECK(fromOther == staged);
        CHECK(cfd::isCurrent(*field, fromOther));
    }
}

TEST_CASE("CfdInitialField_AFiniteExtremeIsAcceptedAndExpandedBitForBit", "[cfd][ic]") {
    // THE HOLE ON THE LINE THIS MILESTONE CLAIMS TO STRENGTHEN. A review
    // measured it: every one of the twelve NaN/Inf rejections still passes if
    // `isFinite` is weakened to a magnitude bound like |x| < 1e300, because
    // nothing anywhere asserts that a finite EXTREME is accepted.
    constexpr double kMax = std::numeric_limits<double>::max();
    constexpr double kLowest = std::numeric_limits<double>::lowest();
    constexpr double kTiny = std::numeric_limits<double>::min();
    constexpr double kSub = std::numeric_limits<double>::denorm_min();
    REQUIRE(kSub > 0.0);
    REQUIRE(kSub < kTiny);

    const CfdInitialCondition extreme{.velocity = velocity(kMax, kLowest, kSub),
                                      .pressure = Pressure::fromSi(kTiny)};
    CHECK(cfd::validate(extreme).has_value());

    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis(extreme);
    REQUIRE(fluid.definitionOf(analysis).initialCondition == extreme);

    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    // BIT FOR BIT, which is where a float intermediate or a fast-math build
    // would show and nowhere else in this suite.
    std::vector<double> p(field->size(CfdVariable::Pressure), 0.0);
    REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Pressure, p).has_value());
    CHECK(std::ranges::all_of(p, [](double v) { return v == kTiny; }));

    const std::size_t cells = field->cells();
    std::vector<double> u(field->size(CfdVariable::Velocity), 0.0);
    REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Velocity, u).has_value());
    const std::array<double, 3> want{kMax, kLowest, kSub};
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t k = 0; k < bettercad::cfd::kVectorComponents.size(); ++k) {
            CHECK(u[blockedSlot(k, c, cells)] == want[k]);
        }
    }
}

TEST_CASE("CfdInitialField_TwoAdjacentSubnormalsHaveDifferentCanonicalTexts", "[cfd][ic]") {
    // THE STRONGEST AVAILABLE TEST of "the shortest decimal that reads back as
    // the same double": two doubles one representable step apart, at the very
    // bottom of the range. The losslessness claim was otherwise measured only
    // near 1.0 to 1000.0.
    constexpr double kSub = std::numeric_limits<double>::denorm_min();
    const double twice = kSub * 2.0;
    REQUIRE(twice != kSub);

    CfdAnalysisDefinition a;
    a.mesh = MeshControlId::fromValue(1);
    a.referencePressure = cfd::CfdReferencePressure{.value = 0_Pa, .location = Point3D{}};
    a.initialCondition = CfdInitialCondition{.pressure = Pressure::fromSi(kSub)};

    CfdAnalysisDefinition b = a;
    b.initialCondition = CfdInitialCondition{.pressure = Pressure::fromSi(twice)};
    REQUIRE_FALSE(a.initialCondition == b.initialCondition);

    // THE TEXT, which is what is lossless. The fingerprint is a 64-bit FNV-1a
    // digest OVER that text, so it cannot be lossless and the strong claim does
    // not belong on it -- a review's correction, and the reason this case
    // asserts the texts and only then the digests.
    CHECK(cfd::canonicalText(a) != cfd::canonicalText(b));
    CHECK_FALSE(cfd::fingerprint(a) == cfd::fingerprint(b));

    // And the same at the top of the range.
    CfdAnalysisDefinition big = a;
    big.initialCondition =
        CfdInitialCondition{.pressure = Pressure::fromSi(std::numeric_limits<double>::max())};
    CfdAnalysisDefinition nextBig = a;
    nextBig.initialCondition = CfdInitialCondition{
        .pressure = Pressure::fromSi(std::nextafter(std::numeric_limits<double>::max(), 0.0))};
    REQUIRE_FALSE(big.initialCondition == nextBig.initialCondition);
    CHECK(cfd::canonicalText(big) != cfd::canonicalText(nextBig));
}

TEST_CASE("CfdInitialField_TheExpansionCarriesTheSignOfASignedZero", "[cfd][ic]") {
    // THE ONE INPUT FOR WHICH THE ARRAY'S BITS WERE UNPINNED, named by a review:
    // every value assertion in the suite is `==`, and -0.0 == 0.0, so an
    // expansion that normalised a signed zero -- or failed to -- was invisible.
    //
    // THE BEHAVIOUR IS DECIDED HERE AND IT IS ASYMMETRIC WITH THE FINGERPRINT.
    // The expansion COPIES, so it carries the caller's sign. `canonicalText`
    // NORMALISES -0.0 to 0.0, because `operator==` calls the two equal and a
    // fingerprint must not report a change no comparison can see. Both are
    // right for their own job, and the asymmetry is recorded rather than left to
    // be discovered.
    const CfdInitialCondition signed_{.velocity = velocity(-0.0, 0.0, -0.0),
                                      .pressure = Pressure::fromSi(-0.0)};
    REQUIRE(std::signbit(signed_.velocity.x.si()));
    REQUIRE_FALSE(std::signbit(signed_.velocity.y.si()));
    REQUIRE(std::signbit(signed_.velocity.z.si()));
    REQUIRE(std::signbit(signed_.pressure.si()));

    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis(signed_);
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    std::vector<double> p(field->size(CfdVariable::Pressure), 1.0);
    REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Pressure, p).has_value());
    CHECK(std::ranges::all_of(p, [](double v) { return std::signbit(v); }));

    const std::size_t cells = field->cells();
    std::vector<double> u(field->size(CfdVariable::Velocity), 1.0);
    REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Velocity, u).has_value());
    const std::array<bool, 3> wantSign{true, false, true};
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t k = 0; k < bettercad::cfd::kVectorComponents.size(); ++k) {
            INFO("cell " << c << " component " << k);
            CHECK(std::signbit(u[blockedSlot(k, c, cells)]) == wantSign[k]);
        }
    }
}

TEST_CASE("CfdInitialField_APreparedFieldIsASnapshotAndExpandsItsOwnValues", "[cfd][ic]") {
    // THE ENTIRE REASON the source carries the record BY VALUE rather than
    // referring to the definition, and a review found nothing testing it:
    // the staleness cases assert that `isCurrent` says stale, and none of them
    // asks what a stale field then expands.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdInitialCondition before{.velocity = velocity(1.0, 2.0, 3.0), .pressure = 100_Pa};
    const CfdAnalysisId analysis = fluid.addAnalysis(before);
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    CHECK(fluid.edit(analysis, [](CfdAnalysisDefinition& d) {
        d.initialCondition = CfdInitialCondition{.velocity = velocity(9.0, 9.0, 9.0),
                                                 .pressure = 900_Pa};
    }));
    // The document moved, so the field is stale...
    REQUIRE_FALSE(cfd::isCurrent(*field, sourceOf(fluid, analysis)));
    // ...and still holds, and still expands, ITS OWN values.
    CHECK(field->initialCondition() == before);

    std::vector<double> p(field->size(CfdVariable::Pressure), kNaN);
    REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Pressure, p).has_value());
    CHECK(std::ranges::all_of(p, [](double v) { return v == 100.0; }));

    const std::size_t cells = field->cells();
    std::vector<double> u(field->size(CfdVariable::Velocity), kNaN);
    REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Velocity, u).has_value());
    const std::array<double, 3> want{1.0, 2.0, 3.0};
    for (std::size_t c = 0; c < cells; ++c) {
        for (std::size_t k = 0; k < bettercad::cfd::kVectorComponents.size(); ++k) {
            CHECK(u[blockedSlot(k, c, cells)] == want[k]);
        }
    }

    // A field prepared afresh sees the new values, so the snapshot is a
    // snapshot and not a stale cache nobody can refresh.
    const Result<PreparedCfdInitialField> now = fieldOf(fluid, analysis);
    REQUIRE(now.has_value());
    CHECK(now->initialCondition().pressure == 900_Pa);
}

TEST_CASE("CfdInitialField_ACloneCarriesTheRecordAndNothingFieldShaped", "[cfd][ic]") {
    // ADR-068 Decision 9 claims the derived field is "NOT clonable into the
    // document". The compile-fail gates carry the definition half; this carries
    // the behavioural half, which a review found had nothing behind it.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdInitialCondition initial{.velocity = velocity(0.125, -0.25, 0.5),
                                      .pressure = 1234_Pa};
    const CfdAnalysisId analysis = fluid.addAnalysis(initial);

    const cfd::CfdAnalysis* study = cfd::findCfdAnalysis(fluid.model.document(), analysis);
    REQUIRE(study != nullptr);
    const std::unique_ptr<DocumentObject> copy = study->clone();
    REQUIRE(copy != nullptr);
    const auto* cloned = dynamic_cast<const cfd::CfdAnalysis*>(copy.get());
    REQUIRE(cloned != nullptr);

    // The record survives exactly...
    CHECK(cloned->definition().initialCondition == initial);
    CHECK(cloned->contentEquals(*study));
    // ...and the clone's definition is equal to the original's, which it cannot
    // be if either carried a per-mesh array that the other did not.
    CHECK(cloned->definition() == study->definition());
    CHECK(cfd::fingerprint(cloned->definition()) == cfd::fingerprint(study->definition()));
}

TEST_CASE("CfdInitialField_ANonSiLiteralLandsInTheArrayAsSi", "[cfd][ic]") {
    // THE "SI units" CHECKLIST ITEM, with a value that is not already SI. Every
    // other case in this file builds with `_Pa` and `Velocity::fromSi`, both of
    // which are SI, so the conversion at the expansion boundary was never
    // actually exercised -- a review's point.
    const CfdInitialCondition initial{.velocity = velocity(1.0, 0.0, 0.0), .pressure = 2_bar};
    REQUIRE_THAT(initial.pressure.si(), WithinRel(200000.0, 1e-12));

    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdAnalysisId analysis = fluid.addAnalysis(initial);
    const Result<PreparedCfdInitialField> field = fieldOf(fluid, analysis);
    REQUIRE(field.has_value());

    std::vector<double> p(field->size(CfdVariable::Pressure), kNaN);
    REQUIRE(cfd::expandCfdInitialField(*field, CfdVariable::Pressure, p).has_value());
    // Pascals in the array, not bar.
    CHECK(std::ranges::all_of(p, [](double v) { return v == 200000.0; }));
}

TEST_CASE("CfdInitialField_AVelocityOnlyEditIsAnEffectiveChange", "[cfd][ic]") {
    // "No effective change" was probed only by perturbing the pressure, so a
    // `setDefinition` that compared the pressure alone would have passed.
    PreparedFluid fluid(MeshReferenceModelKind::Block);
    const CfdInitialCondition initial{.velocity = velocity(1.0, 2.0, 3.0), .pressure = 50_Pa};
    const CfdAnalysisId analysis = fluid.addAnalysis(initial);

    for (const std::size_t k : {std::size_t{0}, std::size_t{1}, std::size_t{2}}) {
        INFO("component " << k);
        CHECK(fluid.edit(analysis, [k](CfdAnalysisDefinition& d) {
            const VectorComponent component = bettercad::cfd::kVectorComponents[k];
            const double now = d.initialCondition.velocity.component(component).si();
            switch (component) {
            case VectorComponent::X:
                d.initialCondition.velocity.x = Velocity::fromSi(now + 1.0);
                break;
            case VectorComponent::Y:
                d.initialCondition.velocity.y = Velocity::fromSi(now + 1.0);
                break;
            case VectorComponent::Z:
                d.initialCondition.velocity.z = Velocity::fromSi(now + 1.0);
                break;
            }
        }));
    }
    CHECK(fluid.definitionOf(analysis).initialCondition.velocity ==
          velocity(2.0, 3.0, 4.0));
    CHECK(fluid.definitionOf(analysis).initialCondition.pressure == 50_Pa);
}
