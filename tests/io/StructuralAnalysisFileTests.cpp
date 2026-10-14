// P17-PERSIST-001: persisting canonical structural-analysis intent.
//
// THE RULE THESE TESTS EXIST FOR:
//
//     save  ->  structural-analysis intent
//     load  ->  restore intent; regenerate the mesh through P16;
//               solve again when asked
//
// NOT: save the displacement array and trust it afterwards. So the comparisons
// here are of CANONICAL INTENT -- read through a fingerprint that reads the
// mesh control, the mode, the loads with their targets and values, the
// restraints with their masks and the solver settings, and reads nothing
// generated. A fingerprint that matched because a cached result happened to
// match would be no evidence at all.
//
// THE HARD GATE IS BYTE IDENTITY ACROSS THE SOLVE. P16's equivalent was that
// the file does not grow with mesh density; P17's is stronger and exact,
// because the canonical intent is not merely similar across the two saves but
// literally unchanged: the same document, saved before and after a solve, must
// produce the same bytes. Four states are compared, not two -- no result, a
// current result, a STALE result and a FAILED attempt -- because a format that
// wrote a result only when it was valid would pass the two-state version.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralCommands.hpp>
#include <bettercad/structural/StructuralValidation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using structural::DofComponent;
using structural::GravityLoad;
using structural::PressureLoad;
using structural::RestraintComponents;
using structural::StructuralAnalysis;
using structural::StructuralAnalysisDefinition;
using structural::StructuralAnalysisMode;
using structural::StructuralLoad;
using structural::StructuralRestraint;
using structural::SurfaceTractionLoad;

namespace {

constexpr double kYoungs = 2.1e11;
constexpr double kPoisson = 0.3;
constexpr double kDensity = 7850.0;

/// The non-default solver settings the fixture carries.
///
/// DELIBERATELY UGLY NUMBERS. A tolerance of 1e-8 would round-trip through a
/// low-precision formatter and still look right; 1.23456789e-8 would not, which
/// is the brief's own example. And both differ from the constructor defaults
/// (1e-9 and 1e-12), so a dropped field cannot be hidden by a default that
/// happens to match -- the silent pass that makes a settings round-trip test
/// worthless.
constexpr double kTolerance = 1.23456789e-8;
constexpr double kPivotFloor = 2.5e-11;

/// A plate with a material, a meshing control and a structural analysis.
///
/// THE SHAPE AND SIZES ARE P17's STANDARD FIXTURE -- a 40 x 30 x 20 block at a
/// 20 mm global size with a 6 mm local control -- so that a result computed
/// here is comparable with the ones the solve, post and reaction milestones
/// measured, and so the mesh is small enough for a persistence test to solve
/// several times.
struct AnalysedPart {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    AnalysedPart() {
        auto profile = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        // 40 x 30, not square: an asymmetric body cannot have two faces
        // accidentally interchangeable, so a reference that bound to the wrong
        // one would show.
        lines = addRectangle(*profile, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId sketchId = require(document.addObject(std::move(profile)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(sketchId.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create(
            "Mesh",
            meshing::MeshControlDefinition{
                .body = feature,
                .mesh = {.sizing = {.globalTargetSize = 20_mm,
                                    .local = {meshing::LocalMeshSizing{
                                        .face = side(0), .targetSize = 6_mm}}}}});
        INFO((intent ? std::string{} : intent.error().message));
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kYoungs));
        definition.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(kPoisson));
        definition.mechanical.density =
            materials::MaterialProperty<Density>::known(Density::fromSi(kDensity));
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study = StructuralAnalysis::create("Study", StructuralAnalysisDefinition{
                                                             .mesh = control,
                                                             .solver = settings()});
        INFO((study ? std::string{} : study.error().message));
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
    }

    [[nodiscard]] static structural::SolverSettings settings() {
        return structural::SolverSettings{.algorithm = structural::SolverAlgorithm::SimplicialLdlt,
                                          .relativeResidualTolerance = kTolerance,
                                          .pivotFloor = kPivotFloor};
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines[which]}};
    }
    /// Well formed and names an entity no face is generated by: UNRESOLVED,
    /// not malformed. The distinction is the whole of the brief's §17.
    [[nodiscard]] FaceName unresolvable() const {
        return FaceName{feature,
                        FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(9991U)}};
    }

    const meshing::VolumeMesh& remesh() {
        const Result<const meshing::VolumeMesh*> generated =
            mesher.generate(document, regenerator, control);
        INFO((generated ? std::string{} : generated.error().message));
        REQUIRE(generated.has_value());
        return **generated;
    }

    void setAnalysis(StructuralAnalysisDefinition definition) {
        const Result<bool> changed = document.modifyObject<StructuralAnalysis>(
            ObjectId::fromValue(analysis.value()),
            [&](StructuralAnalysis& object) -> Result<bool> {
                return object.setDefinition(std::move(definition));
            });
        INFO((changed ? std::string{} : changed.error().message));
        REQUIRE(changed.has_value());
    }

    /// The production entry, and the only one these tests use to solve.
    [[nodiscard]] Result<structural::StructuralSolveOutcome> solve() const {
        return structural::solveStructuralAnalysis(document, regenerator, mesher, analysis);
    }

    [[nodiscard]] structural::StructuralValidationReport validate() const {
        return structural::validateStructuralAnalysisForSolve(document, regenerator, mesher,
                                                              analysis);
    }

    /// Why @p outcome's result would no longer be current, through P17-DATA's
    /// own comparison. Empty means current.
    [[nodiscard]] std::vector<structural::StaleReason> staleReasonsOf(
        const structural::StructuralSolveOutcome& outcome) const {
        const Result<structural::StructuralResultSource> current =
            structural::currentResultSource(document, regenerator, mesher, analysis);
        INFO((current ? std::string{} : current.error().message));
        REQUIRE(current.has_value());
        return structural::staleReasons(outcome.result.source(), *current);
    }

    /// The definition that solves: a pressure on the end cap and a fixed
    /// support on the start cap.
    [[nodiscard]] StructuralAnalysisDefinition solvable() const {
        StructuralAnalysisDefinition definition;
        definition.mesh = control;
        definition.solver = settings();
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(1U),
            PressureLoad{.face = endCap(), .magnitude = Pressure::fromSi(2.5e6)}});
        definition.restraints.push_back(
            StructuralRestraint::fixedSupport(RestraintId::fromValue(1U), startCap()));
        return definition;
    }

    /// The full fixture: three loads of the three persistable kinds, three
    /// restraints covering a single component, a combined pair and a fixed
    /// support, two distinct resolvable targets plus one that cannot resolve,
    /// mixed-sign values and non-default solver settings.
    [[nodiscard]] StructuralAnalysisDefinition full() const {
        StructuralAnalysisDefinition definition;
        definition.mesh = control;
        definition.mode = StructuralAnalysisMode::LinearStaticWithGravity;
        definition.solver = settings();
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(4U),
            PressureLoad{.face = endCap(), .magnitude = Pressure::fromSi(2.5e6)}});
        // MIXED SIGNS AND A ZERO, which is what catches a lost sign or a
        // transposed axis.
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(2U),
            SurfaceTractionLoad{.face = side(1),
                                .traction = Traction3D{.x = Pressure::fromSi(1234.5),
                                                       .y = Pressure::fromSi(-2500.0),
                                                       .z = Pressure::fromSi(0.0)}}});
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(7U),
            GravityLoad{.acceleration = Vector3D{.x = 0.0,
                                                 .y = 0.0,
                                                 .z = -structural::kStandardGravity}}});
        definition.restraints.push_back(
            StructuralRestraint::fixedSupport(RestraintId::fromValue(3U), startCap()));
        definition.restraints.push_back(StructuralRestraint{
            RestraintId::fromValue(1U), side(2), RestraintComponents::along(DofComponent::Ux)});
        // THE UNRESOLVED TARGET, carried as intent.
        definition.restraints.push_back(StructuralRestraint{
            RestraintId::fromValue(5U), unresolvable(),
            RestraintComponents::along(DofComponent::Uy).add(DofComponent::Uz)});
        return definition;
    }
};

[[nodiscard]] std::string describe(const FaceName& name) {
    const auto id = [](const auto& optional) {
        return optional.has_value() ? std::format("{}", optional->value()) : std::string{"-"};
    };
    std::string text = std::format("{},{},{},{},{},{}", name.feature.value(),
                                   static_cast<int>(name.face.role), id(name.face.entity),
                                   id(name.face.along), id(name.face.alongSketch),
                                   id(name.face.edge));
    for (const FaceCopy& copy : name.face.copies) {
        text += std::format(",copy({},{})", copy.feature.value(), copy.instance);
    }
    return text;
}

[[nodiscard]] std::string describe(const StructuralLoad& load) {
    std::string text = std::format("load[{}:{}]", load.id().value(),
                                   structural::toString(load.kind()));
    if (const SurfaceTractionLoad* traction = load.traction()) {
        text += std::format("({})=({:.17g},{:.17g},{:.17g})", describe(traction->face),
                            traction->traction.x.si(), traction->traction.y.si(),
                            traction->traction.z.si());
    } else if (const PressureLoad* pressure = load.pressure()) {
        text += std::format("({})={:.17g}", describe(pressure->face), pressure->magnitude.si());
    } else if (const GravityLoad* gravity = load.gravity()) {
        text += std::format("=({:.17g},{:.17g},{:.17g})", gravity->acceleration.x,
                            gravity->acceleration.y, gravity->acceleration.z);
    } else {
        text += "=<nodal force, which cannot be persisted>";
    }
    return text;
}

/// Canonical structural intent, and nothing else.
///
/// EVERY FIELD OF THE DEFINITION AND NO OTHER, read through the public
/// accessors: the mesh control, the mode, the loads IN ORDER with their
/// targets and values at full double precision, the restraints IN ORDER with
/// their masks, and all three solver settings. It reads no revision, no mesh,
/// no result and no derived state, so it cannot pass because something cached
/// agreed.
///
/// `{:.17g}` and not `{}`: seventeen significant digits distinguish every
/// distinct double, so a value that round-tripped to the nearest neighbour
/// would show here rather than comparing equal.
[[nodiscard]] std::string fingerprint(const StructuralAnalysis& analysis) {
    const StructuralAnalysisDefinition& d = analysis.definition();
    std::string text = std::format("mesh={}|mode={}", d.mesh.value(),
                                   structural::toString(d.mode));
    for (const StructuralLoad& load : d.loads) {
        text += std::format("|{}", describe(load));
    }
    for (const StructuralRestraint& restraint : d.restraints) {
        text += std::format("|restraint[{}]({})={}", restraint.id().value(),
                            describe(restraint.face()),
                            structural::toString(restraint.components()));
    }
    text += std::format("|solver={},{:.17g},{:.17g}", static_cast<int>(d.solver.algorithm),
                        d.solver.relativeResidualTolerance, d.solver.pivotFloor);
    return text;
}

[[nodiscard]] const StructuralAnalysis& onlyAnalysis(const Document& document) {
    const std::vector<AnalysisId> ids = structural::structuralAnalysisIds(document);
    REQUIRE(ids.size() == 1U);
    const StructuralAnalysis* found = structural::findStructuralAnalysis(document, ids.front());
    REQUIRE(found != nullptr);
    return *found;
}

/// Round trips a document through the real file path, not through a string.
[[nodiscard]] Document roundTrip(const Document& document, const std::filesystem::path& file) {
    const Result<void> saved = io::saveDocument(document, file);
    INFO((saved ? std::string{} : saved.error().message));
    REQUIRE(saved.has_value());
    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    return std::move(*loaded);
}

[[nodiscard]] std::string saveToString(const Document& document) {
    const Result<std::string> text = io::documentToJson(document);
    INFO((text ? std::string{} : text.error().message));
    REQUIRE(text.has_value());
    return *text;
}

/// The structural-analysis object's own text, from its type marker onward.
///
/// NEEDED FOR EVERY COMPARISON BETWEEN TWO DOCUMENTS, because a `Document`
/// generates a fresh UUID on construction and writes it into the header. Two
/// documents therefore NEVER have identical bytes whatever their intent, so a
/// whole-file comparison across documents would fail for a reason that has
/// nothing to do with P17. Comparing one document against itself is exact and
/// uses the whole file; comparing two uses this.
[[nodiscard]] std::string analysisSectionOf(const std::string& text) {
    const std::size_t at = text.find("\"type\": \"structural-analysis\"");
    REQUIRE(at != std::string::npos);
    return text.substr(at);
}

/// Replaces the structural analysis object's `data` in a saved file, so a
/// malformed field can be tested without hand-writing a whole document.
[[nodiscard]] std::string withAnalysisData(const std::string& json, std::string_view replacement) {
    const std::size_t typeAt = json.find("\"type\": \"structural-analysis\"");
    REQUIRE(typeAt != std::string::npos);
    const std::size_t dataAt = json.find("\"data\":", typeAt);
    REQUIRE(dataAt != std::string::npos);
    const std::size_t open = json.find('{', dataAt);
    REQUIRE(open != std::string::npos);
    std::size_t depth = 0;
    std::size_t close = open;
    for (; close < json.size(); ++close) {
        if (json[close] == '{') {
            ++depth;
        } else if (json[close] == '}') {
            --depth;
            if (depth == 0) {
                break;
            }
        }
    }
    REQUIRE(close < json.size());
    return json.substr(0, open) + std::string{replacement} + json.substr(close + 1);
}

[[nodiscard]] Error loadFailure(std::string_view json) {
    Result<Document> loaded = io::documentFromJson(json);
    REQUIRE_FALSE(loaded.has_value());
    return loaded.error();
}

} // namespace

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPersistence_TheWholeCanonicalIntentRoundTrips", "[structural][io][persist]") {
    // The primary qualification fixture: three load kinds, three restraints
    // covering one component, a combined pair and a fixed support, two
    // distinct resolvable targets plus one that cannot resolve, mixed-sign
    // values, a non-default mode and non-default solver settings.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.full());

    const std::string before = fingerprint(onlyAnalysis(part.document));
    const Document loaded = roundTrip(part.document, temp.path() / "analysis.bcad");
    const std::string after = fingerprint(onlyAnalysis(loaded));

    INFO(std::format("before: {}\nafter:  {}", before, after));
    CHECK(before == after);
    // NOT VACUOUS: the fingerprint has to actually say something, or two empty
    // strings would compare equal and this test would pass over a serializer
    // that wrote nothing.
    CHECK_THAT(before, ContainsSubstring("load[4:pressure]"));
    CHECK_THAT(before, ContainsSubstring("load[2:surface_traction]"));
    CHECK_THAT(before, ContainsSubstring("load[7:gravity]"));
    CHECK_THAT(before, ContainsSubstring("restraint[3]"));
    CHECK_THAT(before, ContainsSubstring("restraint[1]"));
    CHECK_THAT(before, ContainsSubstring("restraint[5]"));

    // And the whole definition compares equal through its own operator==,
    // which is field-complete and defaulted -- so a member added later without
    // being serialized fails here even if `fingerprint` forgot it.
    CHECK(onlyAnalysis(loaded).definition() == onlyAnalysis(part.document).definition());
}

TEST_CASE("StructuralPersistence_EveryAnalysisModeRoundTrips", "[structural][io][persist]") {
    // THE GUARD THE TABLE CANNOT BE ITS OWN. A size check cannot catch a
    // WRONG entry in the mode table, so every enumerator is put on an analysis
    // in turn and read back.
    TempDir temp;
    for (const StructuralAnalysisMode mode :
         {StructuralAnalysisMode::LinearStatic, StructuralAnalysisMode::LinearStaticWithGravity}) {
        AnalysedPart part;
        StructuralAnalysisDefinition definition = part.solvable();
        definition.mode = mode;
        part.setAnalysis(definition);

        const Document loaded =
            roundTrip(part.document,
                      temp.path() / std::format("mode-{}.bcad", static_cast<int>(mode)));
        INFO(structural::toString(mode));
        CHECK(onlyAnalysis(loaded).definition().mode == mode);
    }
}

TEST_CASE("StructuralPersistence_EveryPersistableLoadKindRoundTrips", "[structural][io][persist]") {
    TempDir temp;
    AnalysedPart part;

    SECTION("a pressure keeps its face and its signed scalar") {
        StructuralAnalysisDefinition definition = part.solvable();
        definition.loads.clear();
        // NEGATIVE: outward suction, which P17-LOAD-001 accepts rather than
        // clamping. A serializer applying abs() would pass a positive-only
        // test.
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(9U),
            PressureLoad{.face = part.side(3), .magnitude = Pressure::fromSi(-7.5e5)}});
        part.setAnalysis(definition);

        const Document loaded = roundTrip(part.document, temp.path() / "pressure.bcad");
        const StructuralAnalysisDefinition& after = onlyAnalysis(loaded).definition();
        REQUIRE(after.loads.size() == 1U);
        REQUIRE(after.loads.front().pressure() != nullptr);
        CHECK(after.loads.front().id() == LoadId::fromValue(9U));
        CHECK(after.loads.front().pressure()->face == part.side(3));
        CHECK(after.loads.front().pressure()->magnitude == Pressure::fromSi(-7.5e5));
    }

    SECTION("a surface traction keeps its face and all three components") {
        StructuralAnalysisDefinition definition = part.solvable();
        definition.loads.clear();
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(6U),
            SurfaceTractionLoad{.face = part.endCap(),
                                .traction = Traction3D{.x = Pressure::fromSi(-1000.0),
                                                       .y = Pressure::fromSi(0.0),
                                                       .z = Pressure::fromSi(2500.5)}}});
        part.setAnalysis(definition);

        const Document loaded = roundTrip(part.document, temp.path() / "traction.bcad");
        const StructuralAnalysisDefinition& after = onlyAnalysis(loaded).definition();
        REQUIRE(after.loads.size() == 1U);
        REQUIRE(after.loads.front().traction() != nullptr);
        const SurfaceTractionLoad& traction = *after.loads.front().traction();
        CHECK(traction.face == part.endCap());
        CHECK(traction.traction.x == Pressure::fromSi(-1000.0));
        CHECK(traction.traction.y == Pressure::fromSi(0.0));
        CHECK(traction.traction.z == Pressure::fromSi(2500.5));
    }

    SECTION("gravity keeps its acceleration and has no face") {
        StructuralAnalysisDefinition definition = part.solvable();
        definition.mode = StructuralAnalysisMode::LinearStaticWithGravity;
        definition.loads.clear();
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(1U),
            GravityLoad{.acceleration = Vector3D{.x = 1.5, .y = -2.5, .z = -9.80665}}});
        part.setAnalysis(definition);

        const Document loaded = roundTrip(part.document, temp.path() / "gravity.bcad");
        const StructuralAnalysisDefinition& after = onlyAnalysis(loaded).definition();
        REQUIRE(after.loads.size() == 1U);
        REQUIRE(after.loads.front().gravity() != nullptr);
        CHECK(after.loads.front().gravity()->acceleration ==
              Vector3D{.x = 1.5, .y = -2.5, .z = -9.80665});
        CHECK_FALSE(after.loads.front().target().has_value());
    }
}

TEST_CASE("StructuralPersistence_EveryRestraintComponentCombinationRoundTrips",
          "[structural][io][persist]") {
    // ALL SEVEN NON-EMPTY MASKS, which is the brief's §10 in full: three
    // singles, three pairs and the fixed support. An encoding that collapsed a
    // pair into a different spelling, or lost the distinction between ux+uy
    // and ux+uz, fails here.
    TempDir temp;
    const std::array<RestraintComponents, 7> masks{
        RestraintComponents::along(DofComponent::Ux),
        RestraintComponents::along(DofComponent::Uy),
        RestraintComponents::along(DofComponent::Uz),
        RestraintComponents::along(DofComponent::Ux).add(DofComponent::Uy),
        RestraintComponents::along(DofComponent::Ux).add(DofComponent::Uz),
        RestraintComponents::along(DofComponent::Uy).add(DofComponent::Uz),
        RestraintComponents::fixed(),
    };
    for (std::size_t i = 0; i < masks.size(); ++i) {
        AnalysedPart part;
        StructuralAnalysisDefinition definition = part.solvable();
        definition.restraints.clear();
        definition.restraints.push_back(
            StructuralRestraint{RestraintId::fromValue(2U), part.startCap(), masks[i]});
        part.setAnalysis(definition);

        const Document loaded =
            roundTrip(part.document, temp.path() / std::format("mask-{}.bcad", i));
        const StructuralAnalysisDefinition& after = onlyAnalysis(loaded).definition();
        REQUIRE(after.restraints.size() == 1U);
        INFO(structural::toString(masks[i]));
        CHECK(after.restraints.front().components() == masks[i]);
        CHECK(after.restraints.front().id() == RestraintId::fromValue(2U));
        CHECK(after.restraints.front().face() == part.startCap());
    }
}

TEST_CASE("StructuralPersistence_SolverSettingsRoundTripExactly", "[structural][io][persist]") {
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.solvable());

    const Document loaded = roundTrip(part.document, temp.path() / "solver.bcad");
    const structural::SolverSettings& after = onlyAnalysis(loaded).definition().solver;
    // EXACT, and against values that differ from the defaults: a dropped field
    // would read back as 1e-9 and 1e-12 and a test using the defaults would
    // not notice.
    CHECK(after.algorithm == structural::SolverAlgorithm::SimplicialLdlt);
    CHECK(after.relativeResidualTolerance == kTolerance);
    CHECK(after.pivotFloor == kPivotFloor);
    CHECK(after.relativeResidualTolerance != structural::SolverSettings{}.relativeResidualTolerance);
    CHECK(after.pivotFloor != structural::SolverSettings{}.pivotFloor);

    // ALWAYS WRITTEN, all three, so an old file can never be reconstructed
    // from a future build's defaults.
    const std::string text = saveToString(part.document);
    CHECK_THAT(text, ContainsSubstring("\"algorithm\": \"simplicial_ldlt\""));
    CHECK_THAT(text, ContainsSubstring("\"relative_residual_tolerance\""));
    CHECK_THAT(text, ContainsSubstring("\"pivot_floor\""));
}

TEST_CASE("StructuralPersistence_GeometryReferencesSurviveExactly", "[structural][io][persist]") {
    // The hardest gate in the brief. Two distinct resolvable targets and one
    // that cannot resolve, on both a load and a restraint, compared by every
    // field of the selector rather than by whether they happen to resolve.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.full());

    const Document loaded = roundTrip(part.document, temp.path() / "faces.bcad");
    const StructuralAnalysisDefinition& after = onlyAnalysis(loaded).definition();

    REQUIRE(after.loads.size() == 3U);
    CHECK(after.loads[0].target() == part.endCap());
    CHECK(after.loads[1].target() == part.side(1));
    CHECK_FALSE(after.loads[2].target().has_value());

    REQUIRE(after.restraints.size() == 3U);
    CHECK(after.restraints[0].face() == part.startCap());
    CHECK(after.restraints[1].face() == part.side(2));
    CHECK(after.restraints[2].face() == part.unresolvable());

    // THE TWO TARGETS MUST ACTUALLY DIFFER, or a serializer writing one
    // hardcoded face would pass.
    CHECK(after.loads[0].target() != after.loads[1].target());
    CHECK(after.restraints[0].face() != after.restraints[1].face());
}

TEST_CASE("StructuralPersistence_EngineeringValuesSurviveWithoutAUnitConversion",
          "[structural][io][persist][units]") {
    // The format stores bare SI doubles -- pascals, newtons, m/s^2 -- so this
    // is a test that the SI boundary is crossed exactly once in each
    // direction. A factor of 1e6 (Pa to MPa) or 1e3 (N to kN) is the failure
    // mode that would look plausible in a file and be catastrophic in a solve.
    TempDir temp;
    AnalysedPart part;
    StructuralAnalysisDefinition definition = part.solvable();
    definition.loads.clear();
    definition.mode = StructuralAnalysisMode::LinearStaticWithGravity;
    definition.loads.push_back(StructuralLoad{
        LoadId::fromValue(1U),
        PressureLoad{.face = part.endCap(), .magnitude = Pressure::fromSi(2.5e6)}});
    definition.loads.push_back(StructuralLoad{
        LoadId::fromValue(2U),
        SurfaceTractionLoad{.face = part.side(1),
                            .traction = Traction3D{.x = Pressure::fromSi(1234.5),
                                                   .y = Pressure::fromSi(0.0),
                                                   .z = Pressure::fromSi(0.0)}}});
    definition.loads.push_back(StructuralLoad{
        LoadId::fromValue(3U),
        GravityLoad{.acceleration = Vector3D{.x = 0.0, .y = 0.0, .z = -9.80665}}});
    part.setAnalysis(definition);

    // The written text carries SI, by inspection of the bytes.
    const std::string text = saveToString(part.document);
    CHECK_THAT(text, ContainsSubstring("2500000.0"));
    CHECK_THAT(text, ContainsSubstring("1234.5"));
    CHECK_THAT(text, ContainsSubstring("-9.80665"));
    // And NOT the display spellings a GUI would show.
    CHECK(text.find("\"2.5 MPa\"") == std::string::npos);
    CHECK(text.find("\"MPa\"") == std::string::npos);
    CHECK(text.find("\"kN\"") == std::string::npos);

    const Document loaded = roundTrip(part.document, temp.path() / "units.bcad");
    const StructuralAnalysisDefinition& after = onlyAnalysis(loaded).definition();
    REQUIRE(after.loads.size() == 3U);
    // THE PAYLOAD IS REQUIRED BEFORE IT IS DEREFERENCED, and the reason is
    // measured rather than stylistic: probe P5 reversed the written load order,
    // which made `loads[0]` the gravity load, and the unguarded
    // `pressure()->magnitude` SEGFAULTED instead of failing. A crash still
    // counts as a kill, but it takes the whole ctest process with it and tells
    // a later reader nothing -- so each payload is required by kind first.
    REQUIRE(after.loads[0].pressure() != nullptr);
    REQUIRE(after.loads[1].traction() != nullptr);
    REQUIRE(after.loads[2].gravity() != nullptr);
    CHECK(after.loads[0].pressure()->magnitude.si() == 2.5e6);
    CHECK(after.loads[1].traction()->traction.x.si() == 1234.5);
    CHECK(after.loads[2].gravity()->acceleration.z == -9.80665);
}

TEST_CASE("StructuralPersistence_ExtremeAndAwkwardFiniteValuesSurvive",
          "[structural][io][persist][units]") {
    TempDir temp;
    AnalysedPart part;
    // Within the engineering domain at both ends, and a value whose shortest
    // decimal form needs all seventeen digits.
    for (const double magnitude : {1.0e-6, 1.0e12, 0.1 + 0.2, 1.0 / 3.0}) {
        StructuralAnalysisDefinition definition = part.solvable();
        definition.loads.clear();
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(1U),
            PressureLoad{.face = part.endCap(), .magnitude = Pressure::fromSi(magnitude)}});
        part.setAnalysis(definition);

        const Document loaded =
            roundTrip(part.document, temp.path() / std::format("value-{:.17g}.bcad", magnitude));
        INFO(std::format("{:.17g}", magnitude));
        REQUIRE(onlyAnalysis(loaded).definition().loads.front().pressure() != nullptr);
        // BIT FOR BIT. `0.1 + 0.2` is not 0.3, and a formatter that wrote
        // "0.3" would be caught here and nowhere else.
        CHECK(onlyAnalysis(loaded).definition().loads.front().pressure()->magnitude.si() ==
              magnitude);
    }
}

TEST_CASE("StructuralPersistence_LoadAndRestraintOrderIsTheUsersAndIsPreserved",
          "[structural][io][persist]") {
    // P17-DATA-001 declares the order "THE USER'S AND IS PRESERVED" over both
    // vectors, and P17-CMD-001's probe C8 exists because an edit that silently
    // reordered the loads was a real defect. The ids are deliberately NOT in
    // ascending order, so a serializer that sorted them -- which is what P16
    // correctly does for its local sizing -- fails here.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.full());

    const Document loaded = roundTrip(part.document, temp.path() / "order.bcad");
    const StructuralAnalysisDefinition& after = onlyAnalysis(loaded).definition();

    REQUIRE(after.loads.size() == 3U);
    CHECK(after.loads[0].id() == LoadId::fromValue(4U));
    CHECK(after.loads[1].id() == LoadId::fromValue(2U));
    CHECK(after.loads[2].id() == LoadId::fromValue(7U));
    REQUIRE(after.restraints.size() == 3U);
    CHECK(after.restraints[0].id() == RestraintId::fromValue(3U));
    CHECK(after.restraints[1].id() == RestraintId::fromValue(1U));
    CHECK(after.restraints[2].id() == RestraintId::fromValue(5U));
    // NOT ASCENDING, which is what makes the three checks above a test of
    // order rather than of sorting.
    CHECK(after.loads[0].id().value() > after.loads[1].id().value());
}

TEST_CASE("StructuralPersistence_AnEmptyLoadAndRestraintSetRoundTrips",
          "[structural][io][persist]") {
    // PERSISTABLE CANONICAL INTENT IS NOT THE SAME AS A SOLVABLE ANALYSIS,
    // which is the brief's §113. An analysis with no loads and no restraints
    // is a valid record that P17-VALID refuses to SOLVE, and refusing the FILE
    // would conflate the two.
    TempDir temp;
    AnalysedPart part;
    StructuralAnalysisDefinition definition;
    definition.mesh = part.control;
    definition.solver = AnalysedPart::settings();
    part.setAnalysis(definition);

    const std::string text = saveToString(part.document);
    // Omitted rather than written as an empty array, which is P16's
    // convention. Frozen here so the byte-determinism claim has a fixed shape.
    CHECK(text.find("\"loads\"") == std::string::npos);
    CHECK(text.find("\"restraints\"") == std::string::npos);

    const Document loaded = roundTrip(part.document, temp.path() / "empty.bcad");
    const StructuralAnalysisDefinition& after = onlyAnalysis(loaded).definition();
    CHECK(after.loads.empty());
    CHECK(after.restraints.empty());
    CHECK(after.mesh == part.control);
    // And the analysis is still refused for SOLVING, by validation rather than
    // by the file reader.
    const structural::StructuralValidationReport report =
        structural::validateStructuralAnalysisForSolve(loaded, part.regenerator, part.mesher,
                                                       onlyAnalysis(loaded).analysisId());
    CHECK(report.status() == structural::ValidationStatus::Rejected);
}

TEST_CASE("StructuralPersistence_TwoAnalysesRoundTripWithoutLeaking",
          "[structural][io][persist]") {
    // LoadId and RestraintId are scoped per analysis (`Id.hpp`: "Unique within
    // the analysis that owns it"), so the SAME numeric ids are used in both --
    // which is exactly what a reader keeping one global set would break.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.solvable());

    StructuralAnalysisDefinition second;
    second.mesh = part.control;
    second.mode = StructuralAnalysisMode::LinearStaticWithGravity;
    second.solver = structural::SolverSettings{};
    second.loads.push_back(StructuralLoad{
        LoadId::fromValue(1U),
        SurfaceTractionLoad{.face = part.side(2),
                            .traction = Traction3D{.x = Pressure::fromSi(0.0),
                                                   .y = Pressure::fromSi(-500.0),
                                                   .z = Pressure::fromSi(0.0)}}});
    second.restraints.push_back(StructuralRestraint{
        RestraintId::fromValue(1U), part.endCap(), RestraintComponents::along(DofComponent::Uz)});
    auto other = StructuralAnalysis::create("Second", second);
    REQUIRE(other.has_value());
    const AnalysisId secondId =
        AnalysisId::fromValue(require(part.document.addObject(std::move(*other))).value());

    const Document loaded = roundTrip(part.document, temp.path() / "two.bcad");
    REQUIRE(structural::structuralAnalysisIds(loaded).size() == 2U);

    const StructuralAnalysis* first = structural::findStructuralAnalysis(loaded, part.analysis);
    const StructuralAnalysis* alsoSecond = structural::findStructuralAnalysis(loaded, secondId);
    REQUIRE(first != nullptr);
    REQUIRE(alsoSecond != nullptr);
    CHECK(first->definition() == part.solvable());
    CHECK(alsoSecond->definition() == second);
    // The two carry the same numeric ids and different payloads, so nothing
    // crossed between them.
    CHECK(first->definition().loads.front().id() == LoadId::fromValue(1U));
    CHECK(alsoSecond->definition().loads.front().id() == LoadId::fromValue(1U));
    CHECK(first->definition().loads.front().pressure() != nullptr);
    CHECK(alsoSecond->definition().loads.front().traction() != nullptr);
    CHECK(first->definition().solver.relativeResidualTolerance !=
          alsoSecond->definition().solver.relativeResidualTolerance);
}

// ---------------------------------------------------------------------------
// Unresolved references
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPersistence_AnUnresolvedReferenceIsPreservedAndNotRebound",
          "[structural][io][persist][reference]") {
    // THE BRIEF'S §16 AND §88. A well-formed reference that no current face
    // satisfies survives exactly: not dropped, not emptied, and above all not
    // rebound to a nearby face that does exist.
    TempDir temp;
    AnalysedPart part;
    StructuralAnalysisDefinition definition = part.solvable();
    definition.loads.push_back(StructuralLoad{
        LoadId::fromValue(8U),
        PressureLoad{.face = part.unresolvable(), .magnitude = Pressure::fromSi(1.0e5)}});
    part.setAnalysis(definition);
    part.remesh();

    // IT REALLY DOES NOT RESOLVE BEFORE THE SAVE, or the test would be about
    // nothing.
    const structural::StructuralValidationReport beforeReport = part.validate();
    CHECK(beforeReport.status() == structural::ValidationStatus::Rejected);

    const Document loaded = roundTrip(part.document, temp.path() / "unresolved.bcad");
    const StructuralAnalysisDefinition& after = onlyAnalysis(loaded).definition();

    // Still there, still two loads: not dropped.
    REQUIRE(after.loads.size() == 2U);
    // The reference is byte-for-byte the one that was written.
    REQUIRE(after.loads[1].pressure() != nullptr);
    CHECK(after.loads[1].pressure()->face == part.unresolvable());
    CHECK(after.loads[1].pressure()->magnitude == Pressure::fromSi(1.0e5));
    // NOT REBOUND. Every face the body actually has is a face it could have
    // been rebound TO, so each is checked by name.
    for (const FaceName& real :
         {part.startCap(), part.endCap(), part.side(0), part.side(1), part.side(2), part.side(3)}) {
        INFO(describe(real));
        CHECK(after.loads[1].pressure()->face != real);
    }
    // And the target is not emptied either: the selector still names its
    // entity, rather than becoming a default-constructed face.
    CHECK(after.loads[1].target().has_value());
    REQUIRE(after.loads[1].pressure()->face.face.entity.has_value());
    CHECK(after.loads[1].pressure()->face.face.entity->value() == 9991U);
}

TEST_CASE("StructuralPersistence_AnUnresolvedReferenceStillRefusesTheSolveAfterLoading",
          "[structural][io][persist][reference]") {
    // Persistence has done its job by preserving the intent; refusing the
    // solve is P17-VALID's. This is the half that proves the preserved
    // reference is still UNRESOLVED rather than quietly made to work.
    TempDir temp;
    AnalysedPart part;
    StructuralAnalysisDefinition definition = part.solvable();
    definition.restraints.push_back(StructuralRestraint{
        RestraintId::fromValue(9U), part.unresolvable(), RestraintComponents::fixed()});
    part.setAnalysis(definition);

    Document loaded = roundTrip(part.document, temp.path() / "unresolved-solve.bcad");
    features::Regenerator regenerator;
    requireReport(regenerator, loaded);
    meshing::Mesher mesher;
    const AnalysisId id = onlyAnalysis(loaded).analysisId();
    REQUIRE(mesher.generate(loaded, regenerator, onlyAnalysis(loaded).definition().mesh)
                .has_value());

    const structural::StructuralValidationReport report =
        structural::validateStructuralAnalysisForSolve(loaded, regenerator, mesher, id);
    CHECK(report.status() == structural::ValidationStatus::Rejected);
    const bool unresolved =
        std::ranges::any_of(report.findings, [](const structural::ValidationFinding& f) {
            return f.code == structural::ValidationCode::RestraintsUnresolvable;
        });
    CHECK(unresolved);
    // And the solve itself refuses, through the production entry.
    const Result<structural::StructuralSolveOutcome> solved =
        structural::solveStructuralAnalysis(loaded, regenerator, mesher, id);
    CHECK_FALSE(solved.has_value());
}

TEST_CASE("StructuralPersistence_AMalformedReferenceIsNotTheSameAsAnUnresolvedOne",
          "[structural][io][persist][malformed]") {
    // THE BRIEF'S §17, which is mandatory and is a distinction about the
    // REPRESENTATION rather than about the geometry.
    AnalysedPart part;
    part.setAnalysis(part.solvable());
    const std::string good = saveToString(part.document);
    // The valid file loads, or every rejection below could be for the wrong
    // reason.
    REQUIRE(io::documentFromJson(good).has_value());

    // THE FIXTURE'S OWN IDS, not literals. The three malformed cases below
    // fail on the reference's ENCODING before any id is consulted, so a wrong
    // one would hide in them -- but the well-formed case at the end compares
    // the decoded reference against the fixture's face, and a literal that
    // happened not to match the fixture would make it fail for the wrong
    // reason.
    const auto mesh = part.control.value();
    const auto body = part.feature.value();
    const auto analysis = [mesh](std::string_view face) {
        return std::format(R"({{"mesh": {}, "mode": "linear_static",
            "restraints": [{{"id": 1, "face": {}, "components": ["ux","uy","uz"]}}],
            "solver": {{"algorithm": "simplicial_ldlt",
                        "relative_residual_tolerance": 1e-9, "pivot_floor": 1e-12}}}})",
                           mesh, face);
    };

    SECTION("a reference whose role is not a role this build knows is MALFORMED") {
        const Error error =
            loadFailure(withAnalysisData(good, analysis(std::format(R"({{"feature": {}, "face": {{"role": "not_a_role"}}}})", body))));
        CHECK_THAT(error.message, ContainsSubstring("restraints[0].face"));
    }

    SECTION("a side reference naming no entity is refused by the selector's own rule") {
        const Error error =
            loadFailure(withAnalysisData(good, analysis(std::format(R"({{"feature": {}, "face": {{"role": "side"}}}})", body))));
        CHECK_THAT(error.message, ContainsSubstring("restraints[0].face"));
    }

    SECTION("a reference that is not an object at all is MALFORMED") {
        const Error error = loadFailure(withAnalysisData(good, analysis("42")));
        CHECK_THAT(error.message, ContainsSubstring("restraints[0].face"));
    }

    SECTION("a WELL-FORMED reference to a face that does not exist LOADS") {
        // The whole point of the distinction: this one is not corruption, so
        // the document loads and the restraint is carried as unresolved
        // intent.
        const std::string text = withAnalysisData(
            good, analysis(std::format(R"({{"feature": {}, "face": {{"role": "side", "entity": 9991}}}})", body)));
        Result<Document> loaded = io::documentFromJson(text);
        INFO((loaded ? std::string{} : loaded.error().message));
        REQUIRE(loaded.has_value());
        const StructuralAnalysisDefinition& after = onlyAnalysis(*loaded).definition();
        REQUIRE(after.restraints.size() == 1U);
        CHECK(after.restraints.front().face() == part.unresolvable());
    }
}

// ---------------------------------------------------------------------------
// Authority: no derived state in the file
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPersistence_NoSolverStateReachesTheFile",
          "[structural][io][persist][authority]") {
    // A STRUCTURAL CHECK OF THE WRITTEN BYTES, not an argument from the code.
    // The analysis is SOLVED first, so every one of these exists in the
    // process at the moment the document is saved and there is something that
    // could leak.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.solvable());
    part.remesh();
    const Result<structural::StructuralSolveOutcome> solved = part.solve();
    INFO((solved ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    REQUIRE(solved->freeEquations > 0U);
    REQUIRE(solved->largestDisplacement.si() > 0.0);

    const std::filesystem::path file = temp.path() / "authority.bcad";
    REQUIRE(io::saveDocument(part.document, file).has_value());
    const std::string whole = readFile(file);
    // SCOPED TO THE STRUCTURAL SECTION, which is both the right question and
    // the only one that can be asked precisely: the sketch legitimately writes
    // a `"constraints"` key of its own, and a whole-file scan for "constraint"
    // would report it as a leaked `ConstraintSet`.
    const std::string text = analysisSectionOf(whole);

    // Nothing generated is in the section, by name. Each of these is a real
    // member of a real type that existed in memory during the save.
    //
    // MATCHED AS A KEY, with its opening quote, and not as a bare substring:
    // `restraints` contains "strain" and `relative_residual_tolerance`
    // contains "residual", so a substring search reports two false positives
    // against the section's own legitimate keys. The quote makes the check say
    // what it means -- "no key begins with this" -- and it still catches a
    // pluralised leak like `"displacements"`.
    for (const std::string_view forbidden :
         {"node_id",      "element_id",    "node",       "tetrahedra",  "dof",
          "dof_index",    "equation",      "constraint", "prepared",    "stiffness",
          "triplet",      "csr",           "matrix",     "displacement", "strain",
          "stress",       "von_mises",     "principal",  "reaction",    "residual",
          "pivot_ratio",  "strain_energy", "equilibrium", "imbalance",  "factoriz",
          "render",       "contour",       "deformed",   "validation",  "findings",
          "result",       "facet"}) {
        INFO(forbidden);
        CHECK(text.find(std::format("\"{}", forbidden)) == std::string::npos);
    }
    // And the keys that SHOULD be there are, so the test is not passing
    // because nothing was written at all.
    CHECK_THAT(text, ContainsSubstring("\"structural-analysis\""));
    CHECK_THAT(text, ContainsSubstring("\"pressure\""));
    CHECK_THAT(text, ContainsSubstring("\"components\""));
    CHECK_THAT(text, ContainsSubstring("\"simplicial_ldlt\""));
    // The two keys whose names the scan above deliberately does not catch, so
    // the quoting rule is shown to be a precision fix and not a hole: both are
    // present, and both would have made a bare substring scan fail.
    CHECK_THAT(text, ContainsSubstring("\"restraints\""));
    CHECK_THAT(text, ContainsSubstring("\"relative_residual_tolerance\""));
}

TEST_CASE("StructuralPersistence_TheFileIsIdenticalWhateverTheSolveHasDone",
          "[structural][io][persist][authority]") {
    // THE HARD GATE, and FOUR states rather than two.
    //
    // A format that wrote a result only when it was valid would pass a
    // before/after comparison. So the same canonical intent is saved with no
    // result at all, with a CURRENT result, with a STALE one, and after a
    // FAILED solve attempt -- and all four must be byte-for-byte identical.
    // Not the same size: the same bytes.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.solvable());

    // A: nothing solved, and no mesh generated either.
    const std::string noResult = saveToString(part.document);

    // B: a current result.
    part.remesh();
    const Result<structural::StructuralSolveOutcome> first = part.solve();
    INFO((first ? std::string{} : first.error().message));
    REQUIRE(first.has_value());
    const std::string currentResult = saveToString(part.document);

    // C: a STALE result. The mesh is regenerated, which gives a new MeshStamp
    // and makes the result above stale -- while changing no document object,
    // so the canonical intent is untouched. That is what makes this a test of
    // the file and not of the edit.
    part.remesh();
    const std::vector<structural::StaleReason> stale = part.staleReasonsOf(*first);
    REQUIRE_FALSE(stale.empty());
    const std::string staleResult = saveToString(part.document);

    // D: a FAILED solve attempt, with the document again untouched: a fresh
    // Mesher has generated nothing, so the production entry refuses for want
    // of a current mesh.
    meshing::Mesher empty;
    const Result<structural::StructuralSolveOutcome> failed =
        structural::solveStructuralAnalysis(part.document, part.regenerator, empty, part.analysis);
    REQUIRE_FALSE(failed.has_value());
    const std::string afterFailure = saveToString(part.document);

    // BYTES, compared directly.
    CHECK(noResult == currentResult);
    CHECK(noResult == staleResult);
    CHECK(noResult == afterFailure);
    // Sizes recorded as well, but they are supplemental: equal sizes would not
    // have proved anything.
    WARN(std::format("no result {} bytes | current {} | stale {} | after a failed solve {}",
                     noResult.size(), currentResult.size(), staleResult.size(),
                     afterFailure.size()));

    // NOT VACUOUS: a solve really did happen and really did produce numbers.
    CHECK(first->largestVonMises.si() > 0.0);
    CHECK(first->result.displacements().size() > 0U);
}

TEST_CASE("StructuralPersistence_TheFileDoesNotFollowTheMeshOrTheResultSize",
          "[structural][io][persist][authority]") {
    // The P16 sibling of the test above, in the structural dimension: the same
    // canonical structural intent on meshes of very different size must give
    // the same P17 section. Only the MESHING intent differs between the two
    // documents, so the analysis object's bytes are compared rather than the
    // whole file.
    //
    // A CYLINDER, because a block cannot demonstrate this. Its boundary is
    // planar and exactly representable, so 20 mm and 5 mm both give 24
    // tetrahedra -- measured here, and the same thing P16-SIZE-001,
    // P16-CMD-001 and P16-PERSIST-001 each recorded. Without a curved wall
    // there is no mesh growth for the file to be independent OF.
    TempDir temp;
    const auto analysisTextAt = [&temp](Length target, const char* label) {
        Document document{"Tube"};
        features::Regenerator regenerator;
        meshing::Mesher mesher;

        auto profile = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        REQUIRE(profile->addCircle(Point2D{0_mm, 0_mm}, 10_mm).has_value());
        const ObjectId sketchId = require(document.addObject(std::move(profile)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(sketchId.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        const ObjectId feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create(
            "Mesh", meshing::MeshControlDefinition{
                        .body = feature, .mesh = {.sizing = {.globalTargetSize = target}}});
        REQUIRE(intent.has_value());
        const MeshControlId control =
            MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition material;
        material.designation = "Steel";
        material.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kYoungs));
        material.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(PoissonRatio::of(kPoisson));
        const Result<MaterialId> id = features::createMaterial(document, "Steel", material);
        REQUIRE(id.has_value());
        REQUIRE(features::assignMaterial(document, *id).has_value());

        // THE SAME CANONICAL STRUCTURAL INTENT in both documents.
        StructuralAnalysisDefinition definition;
        definition.mesh = control;
        definition.solver = AnalysedPart::settings();
        definition.loads.push_back(StructuralLoad{
            LoadId::fromValue(1U),
            PressureLoad{.face = FaceName{feature, FaceSelector{.role = FaceRole::EndCap}},
                         .magnitude = Pressure::fromSi(2.5e6)}});
        definition.restraints.push_back(StructuralRestraint::fixedSupport(
            RestraintId::fromValue(1U),
            FaceName{feature, FaceSelector{.role = FaceRole::StartCap}}));
        auto study = StructuralAnalysis::create("Study", definition);
        REQUIRE(study.has_value());
        const AnalysisId analysis =
            AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
        const Result<const meshing::VolumeMesh*> mesh =
            mesher.generate(document, regenerator, control);
        INFO((mesh ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
        const std::size_t elements = (*mesh)->mesh().elementCount();
        // SOLVED, so a result exists in the process at the moment of the save.
        const Result<structural::StructuralSolveOutcome> solved =
            structural::solveStructuralAnalysis(document, regenerator, mesher, analysis);
        INFO((solved ? std::string{} : solved.error().message));
        REQUIRE(solved.has_value());

        const std::filesystem::path file = temp.path() / std::format("{}.bcad", label);
        REQUIRE(io::saveDocument(document, file).has_value());
        return std::pair{elements, analysisSectionOf(readFile(file))};
    };

    const auto [coarseElements, coarseText] = analysisTextAt(20_mm, "coarse");
    const auto [fineElements, fineText] = analysisTextAt(2.5_mm, "fine");

    // The refinement must have done something, or the comparison is empty.
    REQUIRE(fineElements > coarseElements);
    CHECK(coarseText == fineText);
    WARN(std::format("coarse {} elements | fine {} elements | structural section {} bytes, equal",
                     coarseElements, fineElements, coarseText.size()));
}

TEST_CASE("StructuralPersistence_ANodalForceLoadIsRefusedRatherThanWritten",
          "[structural][io][persist][authority]") {
    // ADR-031: a NodeId is "mesh-local, reused across generations and never
    // persisted". P17-LOAD-001 declares `NodalForceLoad` mesh-local on purpose
    // and explicitly "NOT durable point-load intent on the model". So it is
    // refused rather than written -- and refused rather than silently dropped,
    // which is the quiet alternative this test exists to rule out.
    TempDir temp;
    AnalysedPart part;
    const meshing::VolumeMesh& mesh = part.remesh();
    StructuralAnalysisDefinition definition = part.solvable();
    definition.loads.push_back(StructuralLoad{
        LoadId::fromValue(12U),
        structural::NodalForceLoad{.mesh = mesh.mesh().stamp(),
                                   .node = meshing::NodeId::fromValue(1U),
                                   .force = Force3D{.x = Force::fromSi(0.0),
                                                    .y = Force::fromSi(0.0),
                                                    .z = Force::fromSi(-100.0)}}});
    part.setAnalysis(definition);

    const Result<std::string> written = io::documentToJson(part.document);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code == ErrorCode::InvalidArgument);
    // The diagnostic names the load and says why, rather than "cannot save".
    CHECK_THAT(written.error().message, ContainsSubstring("12"));
    CHECK_THAT(written.error().message, ContainsSubstring("nodal force"));
    CHECK_THAT(written.error().message, ContainsSubstring("ADR-031"));

    // ATOMIC: saveDocument writes no file at all, not a truncated one.
    const std::filesystem::path file = temp.path() / "refused.bcad";
    CHECK_FALSE(io::saveDocument(part.document, file).has_value());
    CHECK_FALSE(std::filesystem::exists(file));

    // And the document is untouched by the attempt, so the refusal costs the
    // user nothing but the save.
    CHECK(onlyAnalysis(part.document).definition().loads.size() == 2U);

    // THE SAME DOCUMENT WITHOUT THAT LOAD SAVES, which is what makes the
    // refusal attributable to the nodal force and not to the fixture.
    part.setAnalysis(part.solvable());
    CHECK(io::saveDocument(part.document, file).has_value());
}

TEST_CASE("StructuralPersistence_TheReaderRefusesANodalForceLoadByName",
          "[structural][io][persist][authority]") {
    // The other half: a hand-written file claiming one is refused by name
    // rather than falling through to "unknown load type", and certainly
    // rather than being read as a zero load.
    AnalysedPart part;
    part.setAnalysis(part.solvable());
    const std::string good = saveToString(part.document);
    REQUIRE(io::documentFromJson(good).has_value());

    const Error error = loadFailure(withAnalysisData(
        good, std::format(R"({{"mesh": {}, "mode": "linear_static",
            "loads": [{{"id": 3, "type": "nodal_force"}}],
            "solver": {{"algorithm": "simplicial_ldlt",
                        "relative_residual_tolerance": 1e-9, "pivot_floor": 1e-12}}}})",
                          part.control.value())));
    CHECK_THAT(error.message, ContainsSubstring("loads[0].type"));
    CHECK_THAT(error.message, ContainsSubstring("mesh node"));
    CHECK_THAT(error.message, ContainsSubstring("ADR-031"));
}

TEST_CASE("StructuralPersistence_SavingChangesNothingAboutTheDocument",
          "[structural][io][persist][authority]") {
    // The brief's §186: a save is read-only with respect to canonical
    // engineering state. It must not move a revision, stale a result or make a
    // stale one current.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.solvable());
    part.remesh();
    const Result<structural::StructuralSolveOutcome> solved = part.solve();
    REQUIRE(solved.has_value());

    const StructuralAnalysis& analysis = onlyAnalysis(part.document);
    const std::uint64_t revisionBefore = analysis.revision();
    const std::string intentBefore = fingerprint(analysis);
    const bool currentBefore = part.staleReasonsOf(*solved).empty();
    REQUIRE(currentBefore);

    REQUIRE(io::saveDocument(part.document, temp.path() / "unchanged.bcad").has_value());

    CHECK(onlyAnalysis(part.document).revision() == revisionBefore);
    CHECK(fingerprint(onlyAnalysis(part.document)) == intentBefore);
    // The result was current before the save and is still current after it:
    // saving neither staled it nor refreshed anything.
    CHECK(part.staleReasonsOf(*solved).empty());
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPersistence_SerializationIsByteIdenticalAndIndependentOfEditOrder",
          "[structural][io][persist][determinism]") {
    AnalysedPart part;
    part.setAnalysis(part.full());

    SECTION("repeated saves of one document are identical") {
        const std::string first = saveToString(part.document);
        for (int i = 0; i < 5; ++i) {
            INFO(i);
            CHECK(saveToString(part.document) == first);
        }
    }

    SECTION("a restraint mask built in a different component order is identical") {
        // `RestraintComponents` is a bitmask, so there is no insertion order
        // to lose -- and this is the test that says so rather than the comment
        // claiming it.
        //
        // The ANALYSIS SECTION, because this compares two documents and a
        // `Document` writes a fresh UUID into its header.
        AnalysedPart other;
        StructuralAnalysisDefinition definition = other.full();
        definition.restraints[2] = StructuralRestraint{
            RestraintId::fromValue(5U), other.unresolvable(),
            RestraintComponents::along(DofComponent::Uz).add(DofComponent::Uy)};
        other.setAnalysis(definition);
        CHECK(analysisSectionOf(saveToString(other.document)) ==
              analysisSectionOf(saveToString(part.document)));
        // The uy+uz mask really is in the file, so this is not comparing two
        // sections that both lack it.
        CHECK_THAT(analysisSectionOf(saveToString(other.document)),
                   ContainsSubstring("\"uy\""));
    }
}

TEST_CASE("StructuralPersistence_SaveLoadSaveIsByteIdentical",
          "[structural][io][persist][determinism]") {
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.full());

    const std::filesystem::path first = temp.path() / "first.bcad";
    REQUIRE(io::saveDocument(part.document, first).has_value());
    const std::string save1 = readFile(first);

    Result<Document> loaded = io::loadDocument(first);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());

    const std::filesystem::path second = temp.path() / "second.bcad";
    REQUIRE(io::saveDocument(*loaded, second).has_value());
    const std::string save2 = readFile(second);

    CHECK(save1 == save2);
    // A FIXED POINT, not merely a pair: a third pass must agree too, which is
    // what distinguishes convergence from a single lucky round.
    Result<Document> again = io::documentFromJson(save2);
    REQUIRE(again.has_value());
    CHECK(saveToString(*again) == save2);
}

TEST_CASE("StructuralPersistence_AnExplicitEmptyCollectionNormalisesThenIsByteStable",
          "[structural][io][persist][determinism]") {
    // FOUND BY READING THE WRITER, not by a failing test. BetterCAD omits an
    // empty collection, so a hand-written file carrying `"loads": []` is
    // well-formed but is not what this build writes: the first save
    // normalises it away, and every save after that is identical.
    //
    // That is the brief's §142 exactly -- "the first save may differ because
    // it writes current schema; then save/load/save must become byte-stable"
    // -- and it is the same property P16's `boundary_sets` has. Recorded as a
    // convergence property rather than left as a surprise.
    AnalysedPart part;
    StructuralAnalysisDefinition definition;
    definition.mesh = part.control;
    definition.solver = AnalysedPart::settings();
    part.setAnalysis(definition);
    const std::string canonical = saveToString(part.document);
    REQUIRE(canonical.find("\"loads\"") == std::string::npos);

    const std::string handWritten = withAnalysisData(
        canonical,
        std::format(R"({{"mesh": {}, "mode": "linear_static", "loads": [], "restraints": [],
            "solver": {{"algorithm": "simplicial_ldlt",
                        "relative_residual_tolerance": {:.17g},
                        "pivot_floor": {:.17g}}}}})",
                    part.control.value(), kTolerance, kPivotFloor));
    // It is accepted: an explicit empty array means the same thing as an
    // absent one, so refusing it would be refusing a file that says nothing
    // wrong.
    Result<Document> loaded = io::documentFromJson(handWritten);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    CHECK(onlyAnalysis(*loaded).definition().loads.empty());
    CHECK(onlyAnalysis(*loaded).definition().restraints.empty());

    // The first save NORMALISES -- the empty arrays are gone...
    const std::string first = saveToString(*loaded);
    CHECK(first.find("\"loads\"") == std::string::npos);
    CHECK(analysisSectionOf(first) == analysisSectionOf(canonical));
    // ...and from there it is a fixed point.
    Result<Document> again = io::documentFromJson(first);
    REQUIRE(again.has_value());
    CHECK(saveToString(*again) == first);
}

TEST_CASE("StructuralPersistence_ALoadedIdCannotBeReusedByALaterCommand",
          "[structural][io][persist][commands]") {
    // THE BRIEF'S §83, ANSWERED FOR THE IDENTITY MODEL THIS PROJECT ACTUALLY
    // HAS. `LoadId` and `RestraintId` deliberately do NOT widen to `ObjectId`
    // (`Id.hpp`: "The two IDs below deliberately do NOT widen, and that
    // difference is the whole of this milestone's identity model"), so they
    // are not document-allocated and there is no allocator to restore for
    // them. The caller chooses the id.
    //
    // So collision is prevented by REFUSAL rather than by allocation, and the
    // thing to prove is that the refusal still works against ids that came
    // from a file rather than from this session.
    TempDir temp;
    AnalysedPart part;
    StructuralAnalysisDefinition definition = part.solvable();
    definition.loads.push_back(StructuralLoad{
        LoadId::fromValue(8U),
        GravityLoad{.acceleration = Vector3D{.x = 0.0, .y = 0.0, .z = -9.80665}}});
    part.setAnalysis(definition);
    Document loaded = roundTrip(part.document, temp.path() / "ids.bcad");

    const AnalysisId id = onlyAnalysis(loaded).analysisId();
    // The loaded ids are the file's own: 1 and 8, not 1 and 2.
    REQUIRE(onlyAnalysis(loaded).definition().loads.size() == 2U);
    CHECK(onlyAnalysis(loaded).definition().loads[0].id() == LoadId::fromValue(1U));
    CHECK(onlyAnalysis(loaded).definition().loads[1].id() == LoadId::fromValue(8U));

    CommandHistory history;
    const Result<void> refused = history.execute(
        loaded, std::make_unique<structural::AddStructuralLoadCommand>(
                    id, StructuralLoad{LoadId::fromValue(8U),
                                       PressureLoad{.face = part.side(1),
                                                    .magnitude = Pressure::fromSi(1.0e5)}}));
    CHECK_FALSE(refused.has_value());
    CHECK(onlyAnalysis(loaded).definition().loads.size() == 2U);

    // And an unused id is accepted, so the refusal is about the collision and
    // not about the document having been loaded.
    CHECK(history
              .execute(loaded, std::make_unique<structural::AddStructuralLoadCommand>(
                                   id, StructuralLoad{
                                           LoadId::fromValue(9U),
                                           PressureLoad{.face = part.side(1),
                                                        .magnitude = Pressure::fromSi(1.0e5)}}))
              .has_value());
    CHECK(onlyAnalysis(loaded).definition().loads.size() == 3U);
}

TEST_CASE("StructuralPersistence_TheCommandHistoryDoesNotReachTheFile",
          "[structural][io][persist][determinism]") {
    // THE BRIEF'S §143 AND §185, and a strong P17-CMD/PERSIST integration
    // check. The same final canonical state is reached two ways -- set
    // directly, and built up through commands with undo and redo in the middle
    // -- and the files must be identical. A format that stored a revision, an
    // undo depth or an edit count would differ.
    AnalysedPart direct;
    direct.setAnalysis(direct.solvable());
    // The ANALYSIS SECTION, because this compares two documents and a
    // `Document` writes a fresh UUID into its header: a whole-file comparison
    // would fail for a reason with nothing to do with P17.
    const std::string expected = analysisSectionOf(saveToString(direct.document));

    AnalysedPart viaCommands;
    CommandHistory history;
    const StructuralLoad pressure{
        LoadId::fromValue(1U),
        PressureLoad{.face = viaCommands.endCap(), .magnitude = Pressure::fromSi(2.5e6)}};
    const StructuralLoad decoy{
        LoadId::fromValue(2U),
        PressureLoad{.face = viaCommands.side(0), .magnitude = Pressure::fromSi(9.0e5)}};

    Document& edited = viaCommands.document;
    REQUIRE(history
                .execute(edited, std::make_unique<structural::AddStructuralLoadCommand>(
                                     viaCommands.analysis, pressure))
                .has_value());
    // A decoy that is added and then undone, so the history is not empty and
    // the redo branch is not trivial.
    REQUIRE(history
                .execute(edited, std::make_unique<structural::AddStructuralLoadCommand>(
                                     viaCommands.analysis, decoy))
                .has_value());
    REQUIRE(history.undo(edited).has_value());
    REQUIRE(history.redo(edited).has_value());
    REQUIRE(history.undo(edited).has_value());
    REQUIRE(history
                .execute(edited, std::make_unique<structural::AddStructuralRestraintCommand>(
                                     viaCommands.analysis,
                                     StructuralRestraint::fixedSupport(
                                         RestraintId::fromValue(1U), viaCommands.startCap())))
                .has_value());

    // The two documents now carry the same canonical intent...
    REQUIRE(fingerprint(onlyAnalysis(viaCommands.document)) ==
            fingerprint(onlyAnalysis(direct.document)));
    // ...and the same bytes, although one of them has a four-deep history and
    // a higher revision.
    CHECK(analysisSectionOf(saveToString(viaCommands.document)) == expected);
    CHECK(onlyAnalysis(viaCommands.document).revision() !=
          onlyAnalysis(direct.document).revision());
}

// ---------------------------------------------------------------------------
// Malformed and invalid input
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPersistence_RejectsMalformedStructuralIntent",
          "[structural][io][persist][malformed]") {
    AnalysedPart part;
    part.setAnalysis(part.solvable());
    const std::string good = saveToString(part.document);
    REQUIRE(io::documentFromJson(good).has_value());

    const auto mesh = part.control.value();
    const auto data = [mesh](std::string_view body) {
        return std::format(R"({{"mesh": {}, "mode": "linear_static", {}}})", mesh, body);
    };
    // THE `"feature": 6` IN THE PAYLOADS BELOW IS DELIBERATELY ARBITRARY and
    // is not this fixture's body, which is a lower id. Every case here is a
    // REJECTION, and each is refused for the reason it names -- a missing
    // magnitude, a wrong component, a duplicate id -- before any feature id is
    // consulted. A face reference naming a feature that does not exist is
    // well-formed and unresolved, which LOADS by design; the case that depends
    // on a real one is in the reference suite above and uses the fixture's own
    // id.
    constexpr std::string_view kSolver =
        R"("solver": {"algorithm": "simplicial_ldlt", "relative_residual_tolerance": 1e-9, "pivot_floor": 1e-12})";

    SECTION("truncated JSON") {
        const Error error = loadFailure(good.substr(0, good.size() / 2));
        CHECK(error.code == ErrorCode::ParseError);
    }

    SECTION("an unknown field") {
        const Error error =
            loadFailure(withAnalysisData(good, data(std::format("{}, \"max_iterations\": 50", kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("max_iterations"));
    }

    SECTION("a missing mesh control") {
        const Error error = loadFailure(withAnalysisData(
            good, std::format(R"({{"mode": "linear_static", {}}})", kSolver)));
        CHECK_THAT(error.message, ContainsSubstring("mesh"));
    }

    SECTION("an invalid mesh handle") {
        const Error error = loadFailure(withAnalysisData(
            good, std::format(R"({{"mesh": 0, "mode": "linear_static", {}}})", kSolver)));
        CHECK_THAT(error.message, ContainsSubstring("mesh"));
    }

    SECTION("a missing mode") {
        const Error error = loadFailure(
            withAnalysisData(good, std::format(R"({{"mesh": {}, {}}})", mesh, kSolver)));
        CHECK_THAT(error.message, ContainsSubstring("mode"));
    }

    SECTION("an unknown mode") {
        const Error error = loadFailure(withAnalysisData(
            good, std::format(R"({{"mesh": {}, "mode": "nonlinear", {}}})", mesh, kSolver)));
        CHECK_THAT(error.message, ContainsSubstring("unknown analysis mode 'nonlinear'"));
    }

    SECTION("a missing solver section") {
        const Error error = loadFailure(withAnalysisData(
            good, std::format(R"({{"mesh": {}, "mode": "linear_static"}})", mesh)));
        CHECK_THAT(error.message, ContainsSubstring("solver"));
    }

    SECTION("loads that are not an array") {
        const Error error =
            loadFailure(withAnalysisData(good, data(std::format("\"loads\": 3, {}", kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("loads"));
    }

    SECTION("an unknown load type") {
        const Error error = loadFailure(withAnalysisData(
            good, data(std::format(R"("loads": [{{"id": 1, "type": "moment"}}], {})", kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("unknown load type 'moment'"));
    }

    SECTION("a pressure with no magnitude") {
        const Error error = loadFailure(withAnalysisData(
            good,
            data(std::format(
                R"("loads": [{{"id": 1, "type": "pressure", "face": {{"feature": 6, "face": {{"role": "end_cap"}}}}}}], {})",
                kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("magnitude"));
    }

    SECTION("a pressure carrying a traction vector as well") {
        // The keys a kind does not use are refused by name, so a confused file
        // is a parse error rather than a load with a quietly ignored field.
        const Error error = loadFailure(withAnalysisData(
            good,
            data(std::format(
                R"("loads": [{{"id": 1, "type": "pressure", "face": {{"feature": 6, "face": {{"role": "end_cap"}}}}, "magnitude": 1000.0, "traction": [1,2,3]}}], {})",
                kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("traction"));
        CHECK_THAT(error.message, ContainsSubstring("pressure load does not carry this"));
    }

    SECTION("a gravity load given a face") {
        const Error error = loadFailure(withAnalysisData(
            good,
            data(std::format(
                R"("loads": [{{"id": 1, "type": "gravity", "acceleration": [0,0,-9.8], "face": {{"feature": 6, "face": {{"role": "end_cap"}}}}}}], {})",
                kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("gravity load does not carry this"));
    }

    SECTION("a traction with two components instead of three") {
        const Error error = loadFailure(withAnalysisData(
            good,
            data(std::format(
                R"("loads": [{{"id": 1, "type": "surface_traction", "face": {{"feature": 6, "face": {{"role": "end_cap"}}}}, "traction": [1.0, 2.0]}}], {})",
                kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("traction"));
    }

    SECTION("a load with no id") {
        const Error error = loadFailure(withAnalysisData(
            good, data(std::format(
                      R"("loads": [{{"type": "gravity", "acceleration": [0,0,-9.8]}}], {})",
                      kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("id"));
    }

    SECTION("a load whose id is the invalid id") {
        // Refused by `validate(StructuralLoad)` -- the core's own rule, called
        // rather than copied.
        const Error error = loadFailure(withAnalysisData(
            good, data(std::format(
                      R"("loads": [{{"id": 0, "type": "gravity", "acceleration": [0,0,-9.8]}}], {})",
                      kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("valid LoadId"));
    }

    SECTION("a non-finite value cannot even be expressed") {
        // MEASURED, AND IT REFUSES EARLIER THAN EXPECTED. The parser rejects
        // an out-of-range literal outright -- "number overflow parsing
        // '-1e999'" -- so an infinity never reaches `readNumber` and never
        // reaches `validate(StructuralLoad)`. P16 recorded the same thing for
        // a mesh size.
        //
        // The validator is still the guard that matters, for the finite-but-
        // invalid values the sections above cover and for any future reader
        // that accepted such a token; it is simply not what refuses THIS file.
        const Error error = loadFailure(withAnalysisData(
            good, data(std::format(
                      R"("loads": [{{"id": 1, "type": "gravity", "acceleration": [0,0,-1e999]}}], {})",
                      kSolver))));
        CHECK(error.code == ErrorCode::ParseError);
        CHECK_THAT(error.message, ContainsSubstring("number overflow"));
    }

    SECTION("a restraint with an empty component mask") {
        // Refused by `validate(StructuralRestraint)`: a restraint that holds
        // nothing at zero is a record whose intent cannot be acted on.
        const Error error = loadFailure(withAnalysisData(
            good,
            data(std::format(
                R"("restraints": [{{"id": 1, "face": {{"feature": 6, "face": {{"role": "end_cap"}}}}, "components": []}}], {})",
                kSolver))));
        CHECK(error.code == ErrorCode::InvalidArgument);
    }

    SECTION("a restraint with an unknown component") {
        const Error error = loadFailure(withAnalysisData(
            good,
            data(std::format(
                R"("restraints": [{{"id": 1, "face": {{"feature": 6, "face": {{"role": "end_cap"}}}}, "components": ["rx"]}}], {})",
                kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("unknown displacement component 'rx'"));
    }

    SECTION("a restraint listing one component twice") {
        const Error error = loadFailure(withAnalysisData(
            good,
            data(std::format(
                R"("restraints": [{{"id": 1, "face": {{"feature": 6, "face": {{"role": "end_cap"}}}}, "components": ["ux","ux"]}}], {})",
                kSolver))));
        CHECK_THAT(error.message, ContainsSubstring("listed twice"));
    }
}

TEST_CASE("StructuralPersistence_RejectsInvalidSolverSettingsRatherThanDefaulting",
          "[structural][io][persist][malformed]") {
    AnalysedPart part;
    part.setAnalysis(part.solvable());
    const std::string good = saveToString(part.document);
    REQUIRE(io::documentFromJson(good).has_value());

    const auto withSolver = [&part, &good](std::string_view solver) {
        return withAnalysisData(
            good, std::format(R"({{"mesh": {}, "mode": "linear_static", "solver": {}}})",
                              part.control.value(), solver));
    };

    SECTION("an unknown algorithm is refused, never defaulted") {
        const Error error = loadFailure(withSolver(
            R"({"algorithm": "pardiso", "relative_residual_tolerance": 1e-9, "pivot_floor": 1e-12})"));
        CHECK_THAT(error.message, ContainsSubstring("unknown solver algorithm 'pardiso'"));
    }

    SECTION("a missing tolerance is refused, never defaulted") {
        // Every field is always written, so an absent one is a malformed file
        // rather than an older shape -- and reading it as this build's default
        // is exactly the silent substitution the always-write rule prevents.
        const Error error =
            loadFailure(withSolver(R"({"algorithm": "simplicial_ldlt", "pivot_floor": 1e-12})"));
        CHECK_THAT(error.message, ContainsSubstring("relative_residual_tolerance"));
    }

    SECTION("a missing pivot floor is refused") {
        const Error error = loadFailure(
            withSolver(R"({"algorithm": "simplicial_ldlt", "relative_residual_tolerance": 1e-9})"));
        CHECK_THAT(error.message, ContainsSubstring("pivot_floor"));
    }

    SECTION("a negative tolerance") {
        const Error error = loadFailure(withSolver(
            R"({"algorithm": "simplicial_ldlt", "relative_residual_tolerance": -1e-9, "pivot_floor": 1e-12})"));
        CHECK_THAT(error.message, ContainsSubstring("finite and positive"));
    }

    SECTION("a zero tolerance") {
        const Error error = loadFailure(withSolver(
            R"({"algorithm": "simplicial_ldlt", "relative_residual_tolerance": 0.0, "pivot_floor": 1e-12})"));
        CHECK_THAT(error.message, ContainsSubstring("finite and positive"));
    }

    SECTION("a negative pivot floor") {
        const Error error = loadFailure(withSolver(
            R"({"algorithm": "simplicial_ldlt", "relative_residual_tolerance": 1e-9, "pivot_floor": -1e-12})"));
        CHECK_THAT(error.message, ContainsSubstring("pivot floor"));
        CHECK_THAT(error.message, ContainsSubstring("finite and positive"));
    }

    SECTION("a non-finite pivot floor cannot even be expressed") {
        // Refused by the parser rather than by the validator, for the reason
        // the load suite records: an out-of-range literal never becomes a
        // number.
        const Error error = loadFailure(withSolver(
            R"({"algorithm": "simplicial_ldlt", "relative_residual_tolerance": 1e-9, "pivot_floor": 1e999})"));
        CHECK(error.code == ErrorCode::ParseError);
        CHECK_THAT(error.message, ContainsSubstring("number overflow"));
    }

    SECTION("an unknown solver field") {
        const Error error = loadFailure(withSolver(
            R"({"algorithm": "simplicial_ldlt", "relative_residual_tolerance": 1e-9, "pivot_floor": 1e-12, "ordering": "amd"})"));
        CHECK_THAT(error.message, ContainsSubstring("ordering"));
    }
}

TEST_CASE("StructuralPersistence_RejectsDuplicateIdentitiesRatherThanMerging",
          "[structural][io][persist][malformed]") {
    AnalysedPart part;
    part.setAnalysis(part.solvable());
    const std::string good = saveToString(part.document);
    REQUIRE(io::documentFromJson(good).has_value());

    const auto mesh = part.control.value();
    constexpr std::string_view kSolver =
        R"("solver": {"algorithm": "simplicial_ldlt", "relative_residual_tolerance": 1e-9, "pivot_floor": 1e-12})";

    SECTION("two loads sharing a LoadId") {
        // NOT first-wins, NOT last-wins and NOT merged: refused, because
        // choosing one would hide a modelling mistake in the file.
        const Error error = loadFailure(withAnalysisData(
            good,
            std::format(
                R"({{"mesh": {}, "mode": "linear_static", "loads": [
                    {{"id": 5, "type": "gravity", "acceleration": [0,0,-9.8]}},
                    {{"id": 5, "type": "pressure", "face": {{"feature": 6, "face": {{"role": "end_cap"}}}}, "magnitude": 1000.0}}], {}}})",
                mesh, kSolver)));
        CHECK_THAT(error.message, ContainsSubstring("the load id 5 is used twice"));
        CHECK_THAT(error.message, ContainsSubstring("loads[1].id"));
    }

    SECTION("two restraints sharing a RestraintId") {
        const Error error = loadFailure(withAnalysisData(
            good,
            std::format(
                R"({{"mesh": {}, "mode": "linear_static", "restraints": [
                    {{"id": 2, "face": {{"feature": 6, "face": {{"role": "end_cap"}}}}, "components": ["ux"]}},
                    {{"id": 2, "face": {{"feature": 6, "face": {{"role": "start_cap"}}}}, "components": ["uz"]}}], {}}})",
                mesh, kSolver)));
        CHECK_THAT(error.message, ContainsSubstring("the restraint id 2 is used twice"));
        CHECK_THAT(error.message, ContainsSubstring("restraints[1].id"));
    }

    SECTION("a LoadId and a RestraintId may share a numeric value") {
        // They are different strong types scoped to one analysis, so this is
        // ordinary and must NOT be refused.
        const std::string text = withAnalysisData(
            good,
            std::format(
                R"({{"mesh": {}, "mode": "linear_static",
                    "loads": [{{"id": 1, "type": "gravity", "acceleration": [0,0,-9.8]}}],
                    "restraints": [{{"id": 1, "face": {{"feature": 6, "face": {{"role": "end_cap"}}}}, "components": ["ux"]}}], {}}})",
                mesh, kSolver));
        Result<Document> loaded = io::documentFromJson(text);
        INFO((loaded ? std::string{} : loaded.error().message));
        REQUIRE(loaded.has_value());
        const StructuralAnalysisDefinition& after = onlyAnalysis(*loaded).definition();
        CHECK(after.loads.front().id().value() == 1U);
        CHECK(after.restraints.front().id().value() == 1U);
    }

    SECTION("two analyses sharing an object id") {
        // AnalysisId widens to ObjectId, so this is the document's own rule
        // rather than a P17 one -- and it still has to hold.
        const std::size_t at = good.rfind("    {\n      \"id\":");
        REQUIRE(at != std::string::npos);
        const std::string duplicated =
            good.substr(0, at) + good.substr(at, good.size() - at - 4) + ",\n" + good.substr(at);
        CHECK_FALSE(io::documentFromJson(duplicated).has_value());
    }
}

TEST_CASE("StructuralPersistence_ACorruptFileLeavesTheOpenDocumentAlone",
          "[structural][io][persist][malformed]") {
    // Load atomicity: a failed load publishes nothing, so a half-read P17
    // section cannot reach the document the user has open.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.full());
    const std::string intentBefore = fingerprint(onlyAnalysis(part.document));

    const std::filesystem::path file = temp.path() / "corrupt.bcad";
    const std::string good = saveToString(part.document);
    // A file whose FIRST analysis is fine and whose second is not, so a reader
    // that published as it went would leave one behind.
    writeFile(file, withAnalysisData(good, R"({"mesh": 0})"));
    Result<Document> loaded = io::loadDocument(file);
    CHECK_FALSE(loaded.has_value());

    // The open document is byte-for-byte what it was.
    CHECK(fingerprint(onlyAnalysis(part.document)) == intentBefore);
    CHECK(saveToString(part.document) == good);
}

// ---------------------------------------------------------------------------
// Backward compatibility
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPersistence_APreP17DocumentLoadsAndSimplyHasNoAnalyses",
          "[structural][io][persist][compat]") {
    // A document written before P17 existed cannot be refused for lacking
    // something it never had: an absent structural section means "no
    // analyses", which is what an older document meant by not having one. The
    // version is NOT bumped, because adding an object type is additive --
    // DocumentFile.hpp's rule, which has correctly never moved the number for
    // growth.
    const std::filesystem::path file =
        std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "meshed_plate.bcad";
    REQUIRE(std::filesystem::exists(file));
    const std::string text = readFile(file);
    // The fixture really does predate P17, or this proves nothing.
    REQUIRE(text.find("structural-analysis") == std::string::npos);

    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    CHECK(structural::structuralAnalysisIds(*loaded).empty());
    // Its own semantics are unchanged: the meshing control is still there.
    CHECK(meshing::meshControls(*loaded).size() == 1U);

    // And re-saving it does not invent a structural section.
    const std::string resaved = saveToString(*loaded);
    CHECK(resaved.find("structural-analysis") == std::string::npos);
    CHECK(resaved.find("\"version\": 2") != std::string::npos);
}

TEST_CASE("StructuralPersistence_TheFileNeedsNoSolverOrMesherToBeInterpreted",
          "[structural][io][persist][compat]") {
    // The brief's §191: opening a valid P17 document must not need a
    // factorisation, a mesh or any solver runtime work. Reading reconstructs
    // intent, and nothing else -- so this whole test runs with no `Mesher` and
    // no `Regenerator` touched after the load.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.full());
    const std::filesystem::path file = temp.path() / "intent-only.bcad";
    REQUIRE(io::saveDocument(part.document, file).has_value());

    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());
    // Every field of the intent is readable with no mesh in existence.
    const StructuralAnalysisDefinition& after = onlyAnalysis(*loaded).definition();
    CHECK(after.loads.size() == 3U);
    CHECK(after.restraints.size() == 3U);
    CHECK(after.solver.relativeResidualTolerance == kTolerance);
    CHECK(after.mode == StructuralAnalysisMode::LinearStaticWithGravity);
}

// ---------------------------------------------------------------------------
// Destroy, load, re-solve
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPersistence_ALoadedAnalysisHasIntentAndNoCurrentResult",
          "[structural][io][persist][resolve]") {
    // THE BRIEF'S §61, and it is structural rather than defended: a
    // `StructuralResult` does not live on a document object at all, so there
    // is nowhere for a loaded analysis to carry one. The test says so by
    // asking the production path, which refuses for want of a mesh.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.solvable());
    part.remesh();
    REQUIRE(part.solve().has_value());
    const std::filesystem::path file = temp.path() / "solved.bcad";
    REQUIRE(io::saveDocument(part.document, file).has_value());

    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    features::Regenerator regenerator;
    requireReport(regenerator, *loaded);
    // A FRESH MESHER THAT HAS GENERATED NOTHING. The original was solved, so
    // if anything about the result had been persisted this is where it would
    // appear.
    meshing::Mesher mesher;
    const AnalysisId id = onlyAnalysis(*loaded).analysisId();
    const structural::StructuralValidationReport report =
        structural::validateStructuralAnalysisForSolve(*loaded, regenerator, mesher, id);
    CHECK(report.status() == structural::ValidationStatus::Rejected);
    CHECK_FALSE(
        structural::solveStructuralAnalysis(*loaded, regenerator, mesher, id).has_value());

    // Loading ran no solve and no mesh generation of its own: the mesher holds
    // nothing at all, which is what "no auto-solve and no auto-remesh on load"
    // means in this architecture.
    CHECK(mesher.mesh(onlyAnalysis(*loaded).definition().mesh) == nullptr);
    CHECK(mesher.currency(*loaded, onlyAnalysis(*loaded).definition().mesh) !=
          meshing::MeshCurrency::Current);
}

TEST_CASE("StructuralPersistence_AFullDestroyAndLoadRoundTripReSolvesEquivalently",
          "[structural][io][persist][resolve]") {
    // THE CENTRAL QUALIFICATION TEST. Create, solve, save, DESTROY every
    // in-memory object, load into a fresh document, regenerate the mesh
    // through the normal P16 path, and re-solve through the normal validated
    // entry -- then compare the engineering results.
    //
    // Nothing is carried across but the file: the original document,
    // regenerator, mesher and outcome are all out of scope before the reload.
    TempDir temp;
    const std::filesystem::path file = temp.path() / "resolve.bcad";

    // The originals, recorded as plain numbers so no object outlives the scope.
    double originalEnergy = 0.0;
    double originalMaxDisplacement = 0.0;
    double originalMaxVonMises = 0.0;
    double originalReactionZ = 0.0;
    double originalResidual = 0.0;
    std::size_t originalFree = 0U;
    std::size_t originalNodes = 0U;
    std::size_t originalElements = 0U;
    std::string originalIntent;
    structural::ValidationStatus originalStatus = structural::ValidationStatus::Rejected;
    std::size_t originalFindings = 0U;
    {
        AnalysedPart part;
        part.setAnalysis(part.solvable());
        const meshing::VolumeMesh& mesh = part.remesh();
        originalNodes = mesh.mesh().nodeCount();
        originalElements = mesh.mesh().elementCount();
        const Result<structural::StructuralSolveOutcome> solved = part.solve();
        INFO((solved ? std::string{} : solved.error().message));
        REQUIRE(solved.has_value());
        REQUIRE(solved->validation.status() != structural::ValidationStatus::Rejected);
        originalStatus = solved->validation.status();
        originalFindings = solved->validation.findings.size();

        originalEnergy = solved->strainEnergy.si();
        originalMaxDisplacement = solved->largestDisplacement.si();
        originalMaxVonMises = solved->largestVonMises.si();
        originalReactionZ = solved->reactions.totalForce().z.si();
        originalResidual = solved->residual.normalized;
        originalFree = solved->freeEquations;
        originalIntent = fingerprint(onlyAnalysis(part.document));

        REQUIRE(io::saveDocument(part.document, file).has_value());
    }
    // Every object above is destroyed here. No pointer, no document, no mesh
    // and no result survives into the block below.

    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());

    // CANONICAL INTENT FIRST, before anything is regenerated.
    CHECK(fingerprint(onlyAnalysis(*loaded)) == originalIntent);

    // THE NORMAL P16 PATH for the mesh, and the NORMAL P17-VALID entry for the
    // solve. No private shortcut, and no injected saved result.
    features::Regenerator regenerator;
    requireReport(regenerator, *loaded);
    meshing::Mesher mesher;
    const AnalysisId id = onlyAnalysis(*loaded).analysisId();
    const Result<const meshing::VolumeMesh*> remeshed =
        mesher.generate(*loaded, regenerator, onlyAnalysis(*loaded).definition().mesh);
    INFO((remeshed ? std::string{} : remeshed.error().message));
    REQUIRE(remeshed.has_value());

    const Result<structural::StructuralSolveOutcome> again =
        structural::solveStructuralAnalysis(*loaded, regenerator, mesher, id);
    INFO((again ? std::string{} : again.error().message));
    REQUIRE(again.has_value());
    // THE SAME VERDICT, not merely an acceptable one. Comparing against
    // `Accepted` would pass if one run warned and the other did not, which is
    // exactly the equivalence this test is for (the brief's §155).
    CHECK(again->validation.status() == originalStatus);
    CHECK(again->validation.findings.size() == originalFindings);

    // The regenerated mesh, recorded because it helps locate a failure. P16's
    // regeneration is deterministic, so these are equalities and not bounds.
    CHECK((*remeshed)->mesh().nodeCount() == originalNodes);
    CHECK((*remeshed)->mesh().elementCount() == originalElements);
    // AND THE RESULT IS BOUND TO THE MESH THAT EXISTS NOW, which is what makes
    // this a re-solve on fresh derived state rather than a restored one: its
    // provenance matches the reloaded document's current source in every
    // field, so nothing in it came from before the save.
    //
    // NOT `generation != 0`, which was the first form of this check and was
    // simply wrong: a `MeshStamp`'s generation counts rebuilds from one source
    // and is 0 for the first mesh a fresh `Mesher` makes, which is exactly
    // what happens here.
    const Result<structural::StructuralResultSource> nowCurrent =
        structural::currentResultSource(*loaded, regenerator, mesher, id);
    INFO((nowCurrent ? std::string{} : nowCurrent.error().message));
    REQUIRE(nowCurrent.has_value());
    CHECK(structural::staleReasons(again->result.source(), *nowCurrent).empty());
    CHECK(again->result.source().mesh.isValid());

    // The engineering semantics.
    CHECK(again->freeEquations == originalFree);
    CHECK_THAT(again->strainEnergy.si(), Catch::Matchers::WithinRel(originalEnergy, 1e-12));
    CHECK_THAT(again->largestDisplacement.si(),
               Catch::Matchers::WithinRel(originalMaxDisplacement, 1e-12));
    CHECK_THAT(again->largestVonMises.si(),
               Catch::Matchers::WithinRel(originalMaxVonMises, 1e-12));
    CHECK_THAT(again->reactions.totalForce().z.si(),
               Catch::Matchers::WithinRel(originalReactionZ, 1e-12));
    // Both residuals are accepted by the same threshold, which is the
    // comparison that matters for a solver tolerance.
    CHECK(again->residual.normalized < kTolerance);
    CHECK(originalResidual < kTolerance);

    WARN(std::format("nodes {} | elements {} | free {} | U {:.17g} J | max|u| {:.17g} m | "
                     "max vM {:.17g} Pa | Rz {:.17g} N | residual {:.3e} -> {:.3e}",
                     originalNodes, originalElements, originalFree, originalEnergy,
                     originalMaxDisplacement, originalMaxVonMises, originalReactionZ,
                     originalResidual, again->residual.normalized));
}

TEST_CASE("StructuralPersistence_AReloadedFaceLoadResolvesToTheSameGeometry",
          "[structural][io][persist][resolve]") {
    // The reference did not merely compare equal as a record: it resolves,
    // through P16's own mapping, to the same face it named before the save --
    // and the load prepared from it carries the same resultant.
    TempDir temp;
    Force3D originalResultant;
    std::size_t originalConstrained = 0U;
    const std::filesystem::path file = temp.path() / "targets.bcad";
    {
        AnalysedPart part;
        part.setAnalysis(part.solvable());
        part.remesh();
        const Result<structural::StructuralSolveOutcome> solved = part.solve();
        REQUIRE(solved.has_value());
        originalResultant = solved->reactions.forceBalance().external;
        originalConstrained = solved->result.reactions().size();
        REQUIRE(io::saveDocument(part.document, file).has_value());
    }

    Result<Document> loaded = io::loadDocument(file);
    REQUIRE(loaded.has_value());
    features::Regenerator regenerator;
    requireReport(regenerator, *loaded);
    meshing::Mesher mesher;
    REQUIRE(mesher.generate(*loaded, regenerator, onlyAnalysis(*loaded).definition().mesh)
                .has_value());
    const Result<structural::StructuralSolveOutcome> again = structural::solveStructuralAnalysis(
        *loaded, regenerator, mesher, onlyAnalysis(*loaded).analysisId());
    INFO((again ? std::string{} : again.error().message));
    REQUIRE(again.has_value());

    // The prepared load resultant, which is the measurement that the face
    // reference resolved to the same area.
    CHECK_THAT(again->reactions.forceBalance().external.z.si(),
               Catch::Matchers::WithinRel(originalResultant.z.si(), 1e-12));
    CHECK(originalResultant.z.si() != 0.0);
    // And the restraint resolved to the same number of constrained nodes.
    CHECK(again->result.reactions().size() == originalConstrained);
    CHECK(originalConstrained > 0U);
}

// ---------------------------------------------------------------------------
// Integration with the command layer
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPersistence_CommandsUndoAndRedoExactlyAfterALoad",
          "[structural][io][persist][commands]") {
    // This is what proves the load restored a live document object and not a
    // frozen value: the P17-CMD-001 commands must work on it, with their undo
    // and redo guarantees intact.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.solvable());
    Document loaded = roundTrip(part.document, temp.path() / "commands.bcad");

    const AnalysisId id = onlyAnalysis(loaded).analysisId();
    const std::string before = fingerprint(onlyAnalysis(loaded));
    CommandHistory history;

    const StructuralLoad added{
        LoadId::fromValue(4U),
        SurfaceTractionLoad{.face = part.side(1),
                            .traction = Traction3D{.x = Pressure::fromSi(750.0),
                                                   .y = Pressure::fromSi(0.0),
                                                   .z = Pressure::fromSi(0.0)}}};
    REQUIRE(history
                .execute(loaded,
                         std::make_unique<structural::AddStructuralLoadCommand>(id, added))
                .has_value());
    REQUIRE(history
                .execute(loaded, std::make_unique<structural::AddStructuralRestraintCommand>(
                                     id, StructuralRestraint{
                                             RestraintId::fromValue(2U), part.endCap(),
                                             RestraintComponents::along(DofComponent::Uz)}))
                .has_value());
    const std::string edited = fingerprint(onlyAnalysis(loaded));
    CHECK(edited != before);

    REQUIRE(history.undo(loaded).has_value());
    REQUIRE(history.undo(loaded).has_value());
    CHECK(fingerprint(onlyAnalysis(loaded)) == before);
    REQUIRE(history.redo(loaded).has_value());
    REQUIRE(history.redo(loaded).has_value());
    CHECK(fingerprint(onlyAnalysis(loaded)) == edited);

    // A removal by identity works too, which needs the loaded ids to be the
    // real ones rather than reallocated.
    REQUIRE(history
                .execute(loaded, std::make_unique<structural::RemoveStructuralLoadCommand>(
                                     id, LoadId::fromValue(1U)))
                .has_value());
    CHECK(onlyAnalysis(loaded).definition().loads.size() == 1U);
    CHECK(onlyAnalysis(loaded).definition().loads.front().id() == LoadId::fromValue(4U));
}

TEST_CASE("StructuralPersistence_AnObjectCreatedAfterALoadCannotCollideWithALoadedId",
          "[structural][io][persist][commands]") {
    // The document's ID allocator is restored from `last_allocated_id`, so an
    // object created after a load is given a fresh ID rather than one that is
    // already taken. P17 inherits this; the test says so rather than assuming
    // it, because a collision would silently overwrite a loaded analysis.
    TempDir temp;
    AnalysedPart part;
    part.setAnalysis(part.solvable());
    Document loaded = roundTrip(part.document, temp.path() / "allocator.bcad");

    const AnalysisId existing = onlyAnalysis(loaded).analysisId();
    auto second = StructuralAnalysis::create(
        "Another", StructuralAnalysisDefinition{.mesh = onlyAnalysis(loaded).definition().mesh});
    REQUIRE(second.has_value());
    const ObjectId fresh = require(loaded.addObject(std::move(*second)));

    CHECK(fresh.value() != existing.value());
    // Both are present, so nothing was overwritten.
    CHECK(structural::structuralAnalysisIds(loaded).size() == 2U);
    CHECK(structural::findStructuralAnalysis(loaded, existing) != nullptr);
    // And the loaded analysis kept its original identity rather than being
    // renumbered on the way in.
    CHECK(existing.value() == part.analysis.value());
}

// ---------------------------------------------------------------------------
// The hand-written reference file
// ---------------------------------------------------------------------------

TEST_CASE("StructuralPersistence_TheHandWrittenReferenceFileLoadsAsWritten",
          "[structural][io][persist][reference]") {
    const std::filesystem::path file =
        std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "analysed_plate.bcad";
    REQUIRE(std::filesystem::exists(file));

    Result<Document> loaded = io::loadDocument(file);
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());

    const StructuralAnalysis& analysis = onlyAnalysis(*loaded);
    CHECK(analysis.name() == "PlateStudy");
    const StructuralAnalysisDefinition& d = analysis.definition();
    // The meshing control the file names, as an ID rather than a position.
    CHECK(d.mesh == MeshControlId::fromValue(7U));
    CHECK(d.mode == StructuralAnalysisMode::LinearStatic);

    const ObjectId body = ObjectId::fromValue(6U);
    // Two loads, in the file's order, with the values exactly as written.
    REQUIRE(d.loads.size() == 2U);
    CHECK(d.loads[0].id() == LoadId::fromValue(1U));
    REQUIRE(d.loads[0].pressure() != nullptr);
    CHECK(d.loads[0].pressure()->magnitude == Pressure::fromSi(2.5e6));
    CHECK(d.loads[0].pressure()->face == FaceName{body, FaceSelector{.role = FaceRole::EndCap}});
    CHECK(d.loads[1].id() == LoadId::fromValue(2U));
    REQUIRE(d.loads[1].traction() != nullptr);
    CHECK(d.loads[1].traction()->traction.x == Pressure::fromSi(1234.5));
    CHECK(d.loads[1].traction()->traction.y == Pressure::fromSi(-2500.0));
    CHECK(d.loads[1].traction()->traction.z == Pressure::fromSi(0.0));
    CHECK(d.loads[1].traction()->face ==
          FaceName{body, FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(5U)}});

    // One fixed support on the face the file names by its start cap.
    REQUIRE(d.restraints.size() == 1U);
    CHECK(d.restraints[0].id() == RestraintId::fromValue(1U));
    CHECK(d.restraints[0].components() == RestraintComponents::fixed());
    CHECK(d.restraints[0].face() == FaceName{body, FaceSelector{.role = FaceRole::StartCap}});

    // The non-default solver settings, exactly as written.
    CHECK(d.solver.algorithm == structural::SolverAlgorithm::SimplicialLdlt);
    CHECK(d.solver.relativeResidualTolerance == 1.0e-8);
    CHECK(d.solver.pivotFloor == 1.0e-12);
}

TEST_CASE("StructuralPersistence_TheReferenceFileRegeneratesMeshesAndSolves",
          "[structural][io][persist][reference]") {
    // END TO END FROM A HAND-WRITTEN FILE: load intent, regenerate geometry,
    // generate a mesh from the loaded meshing intent, and solve the loaded
    // structural intent on it. Nothing in this chain existed in memory before
    // the load, and no part of it was written by BetterCAD.
    Result<Document> loaded = io::loadDocument(
        std::filesystem::path{BETTERCAD_EXAMPLE_MODELS_DIR} / "analysed_plate.bcad");
    INFO((loaded ? std::string{} : loaded.error().message));
    REQUIRE(loaded.has_value());

    features::Regenerator regenerator;
    requireReport(regenerator, *loaded);
    meshing::Mesher mesher;
    const StructuralAnalysis& analysis = onlyAnalysis(*loaded);
    const Result<const meshing::VolumeMesh*> mesh =
        mesher.generate(*loaded, regenerator, analysis.definition().mesh);
    INFO((mesh ? std::string{} : mesh.error().message));
    REQUIRE(mesh.has_value());
    REQUIRE((*mesh)->mesh().elementCount() > 0U);

    const Result<structural::StructuralSolveOutcome> solved = structural::solveStructuralAnalysis(
        *loaded, regenerator, mesher, analysis.analysisId());
    INFO((solved ? std::string{} : solved.error().message));
    REQUIRE(solved.has_value());
    CHECK(solved->validation.status() != structural::ValidationStatus::Rejected);
    CHECK(solved->freeEquations > 0U);
    CHECK(solved->largestDisplacement.si() > 0.0);
    CHECK(solved->largestVonMises.si() > 0.0);
    // The solve met the file's own tolerance, which is the point of persisting
    // it rather than defaulting it.
    CHECK(solved->residual.normalized < analysis.definition().solver.relativeResidualTolerance);
}
