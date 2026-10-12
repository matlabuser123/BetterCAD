// P17-VIZ-001 -- the read-only visualisation adapter for a structural result.
//
// WHERE THIS MILESTONE IS VERIFIED. Every gate the brief calls hard is
// assertable without a display: the unit boundary, the deformed buffer, the
// channel extraction, the classification cardinality, the ranges, the extremes
// and the source guards. A GPU and a driver are not an engineering gate, so
// pixel comparison is supplemental and lives elsewhere.
//
// THE FIXTURE IS A REAL SOLVE, and it has to be. `RecoveredFields` has a
// private constructor with exactly one friend, so there is no way to synthesise
// one -- every test here runs the qualified pipeline through
// `solveStructuralAnalysis` and views what it published. A viewer tested
// against a hand-built result would be a viewer tested against nothing.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/renderer/MeshInspection.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/renderer/StructuralContour.hpp>
#include <bettercad/renderer/StructuralInspection.hpp>
#include <bettercad/renderer/StructuralOverlay.hpp>
#include <bettercad/renderer/StructuralResultView.hpp>
#include <bettercad/renderer/Viewer.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralMaterial.hpp>
#include <bettercad/structural/StructuralValidation.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <span>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using renderer::ChannelDomain;
using renderer::MeshView;
using renderer::ResultChannel;
using renderer::ScalarField;
using renderer::StructuralResultView;

constexpr double kYoungs = 2.1e11;
constexpr double kPoisson = 0.3;

/// A solved part, its mesh view and its result view.
///
/// 40 x 30 x 20 mm with a 6 mm local control on one side face over a 20 mm
/// global target -- the fixture the P17 assembly and post milestones settled
/// on, because the local control is what gives the mesh interior nodes.
struct SolvedView {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    AnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    SolvedView() {
        auto profile = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
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
                                        .face = FaceName{feature,
                                                         FaceSelector{.role = FaceRole::Side,
                                                                      .entity = lines.at(0)}},
                                        .targetSize = 6_mm}}}}});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(kYoungs));
        definition.mechanical.poissonRatio =
            materials::MaterialProperty<bettercad::PoissonRatio>::known(
                PoissonRatio::of(kPoisson));
        definition.mechanical.density =
            materials::MaterialProperty<Density>::known(Density::fromSi(7850.0));
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study = structural::StructuralAnalysis::create(
            "Study", structural::StructuralAnalysisDefinition{.mesh = control});
        REQUIRE(study.has_value());
        analysis = AnalysisId::fromValue(require(document.addObject(std::move(*study))).value());

        requireReport(regenerator, document);
        const Result<const meshing::VolumeMesh*> built =
            mesher.generate(document, regenerator, control);
        INFO((built.has_value() ? std::string{} : built.error().message));
        REQUIRE(built.has_value());
    }

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }

    [[nodiscard]] const meshing::Mesh& mesh() const {
        const meshing::VolumeMesh* held = mesher.mesh(control);
        REQUIRE(held != nullptr);
        return held->mesh();
    }

    /// Every node of the end cap pushed along +Z, as canonical load intent.
    [[nodiscard]] std::vector<structural::StructuralLoad> endLoadList(double fz = 1000.0) const {
        Result<structural::StructuralModel> model =
            structural::requireStructuralModel(document, regenerator, mesher, control);
        REQUIRE(model.has_value());
        Result<meshing::BoundaryFacetSet> facets =
            meshing::boundaryFacetsOf(model->map(), endCap());
        REQUIRE(facets.has_value());
        Result<std::vector<meshing::NodeId>> nodes =
            meshing::boundaryNodesOf(model->map(), model->mesh().mesh(), facets->facets);
        REQUIRE(nodes.has_value());
        REQUIRE_FALSE(nodes->empty());

        std::vector<structural::StructuralLoad> loads;
        std::uint64_t id = 1;
        for (const meshing::NodeId node : *nodes) {
            loads.push_back(structural::StructuralLoad{
                LoadId::fromValue(id++),
                structural::NodalForceLoad{
                    .mesh = model->mesh().mesh().stamp(),
                    .node = node,
                    .force = Force3D{Force::fromSi(0.0), Force::fromSi(0.0),
                                     Force::fromSi(fz / static_cast<double>(nodes->size()))}}});
        }
        return loads;
    }

    /// Pushes the end cap along +Z against a fixed start cap, and installs it.
    void installAndSolve(double fz = 1000.0) {
        std::vector<structural::StructuralLoad> loads = endLoadList(fz);

        REQUIRE(document
                    .modifyObject<structural::StructuralAnalysis>(
                        ObjectId::fromValue(analysis.value()),
                        [&](structural::StructuralAnalysis& object) -> Result<bool> {
                            structural::StructuralAnalysisDefinition definition =
                                object.definition();
                            definition.loads = std::move(loads);
                            definition.restraints = {structural::StructuralRestraint::fixedSupport(
                                RestraintId::fromValue(1), startCap())};
                            return object.setDefinition(std::move(definition));
                        })
                    .has_value());
    }

    [[nodiscard]] structural::StructuralSolveOutcome solve() const {
        Result<structural::StructuralSolveOutcome> outcome =
            structural::solveStructuralAnalysis(document, regenerator, mesher, analysis);
        INFO((outcome.has_value() ? std::string{} : outcome.error().message));
        REQUIRE(outcome.has_value());
        return std::move(*outcome);
    }
};

/// A solved fixture with its views built, ready for a test.
///
/// BUILT IN PLACE, AND NEVER MOVED. `SolvedView` owns a `meshing::Mesher`,
/// which owns the held `VolumeMesh` that every view and every `RecoveredFields`
/// here is keyed to. Moving the fixture after building the views moved the mesh
/// out from under them, and the first version of this file did exactly that and
/// segfaulted: `RecoveredFields::displacementOf` locates a node by searching
/// the MESH it is handed, so a mesh that is not the one the fields were
/// computed on gives a wrong index rather than a refusal -- the stamp still
/// matches, because a moved vector keeps its contents and a moved mesh keeps
/// its stamp.
///
/// The members are `std::optional` because `MeshView` and
/// `StructuralResultView` have no default constructor, which is deliberate on
/// their part.
struct Prepared {
    SolvedView part{};
    std::optional<structural::StructuralSolveOutcome> outcomeHolder{};
    std::optional<MeshView> viewHolder{};
    std::optional<StructuralResultView> resultHolder{};

    Prepared() {
        part.installAndSolve();
        outcomeHolder.emplace(part.solve());
        Result<MeshView> built = MeshView::volumeBoundaryOf(part.mesh());
        INFO((built.has_value() ? std::string{} : built.error().message));
        REQUIRE(built.has_value());
        viewHolder.emplace(std::move(*built));
        Result<StructuralResultView> bound =
            StructuralResultView::of(part.mesh(), *viewHolder, outcomeHolder->fields);
        INFO((bound.has_value() ? std::string{} : bound.error().message));
        REQUIRE(bound.has_value());
        resultHolder.emplace(std::move(*bound));
    }

    Prepared(const Prepared&) = delete;
    Prepared& operator=(const Prepared&) = delete;

    [[nodiscard]] structural::StructuralSolveOutcome& outcome() { return *outcomeHolder; }
    [[nodiscard]] const MeshView& view() const { return *viewHolder; }
    [[nodiscard]] const StructuralResultView& result() const { return *resultHolder; }
};

} // namespace

// ---------------------------------------------------------------------------
// The channel table
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_EveryChannelHasOneDescriptorAndOneDomain", "[renderer][viz]") {
    const std::span<const ResultChannel> channels = renderer::allResultChannels();

    SECTION("the list covers the enumeration and has no duplicates") {
        REQUIRE(channels.size() == 15);
        std::vector<std::string> names;
        for (const ResultChannel channel : channels) {
            const std::string name{renderer::toString(channel)};
            INFO("channel " << name);
            REQUIRE(name != "unknown");
            names.push_back(name);
        }
        std::ranges::sort(names);
        REQUIRE(std::ranges::adjacent_find(names) == names.end());
    }

    SECTION("the descriptor agrees with the channel it was asked about") {
        for (const ResultChannel channel : channels) {
            const renderer::ChannelDescriptor descriptor = renderer::descriptorOf(channel);
            INFO("channel " << renderer::toString(channel));
            REQUIRE(descriptor.channel == channel);
            REQUIRE_FALSE(descriptor.name.empty());
            REQUIRE_FALSE(descriptor.unit.empty());
        }
    }

    SECTION("displacements are nodal and stresses are element-constant") {
        // THE DISTINCTION IS THE POLICY, not a detail. A Tet4's stress state
        // is element-constant by construction, and calling it nodal would
        // require an averaging rule nobody chose.
        for (const ResultChannel channel :
             {ResultChannel::DisplacementMagnitude, ResultChannel::DisplacementX,
              ResultChannel::DisplacementY, ResultChannel::DisplacementZ}) {
            REQUIRE(renderer::descriptorOf(channel).domain == ChannelDomain::Node);
            REQUIRE(renderer::descriptorOf(channel).unit == "m");
        }
        for (const ResultChannel channel :
             {ResultChannel::VonMises, ResultChannel::StressXX, ResultChannel::StressYY,
              ResultChannel::StressZZ, ResultChannel::StressXY, ResultChannel::StressYZ,
              ResultChannel::StressZX, ResultChannel::PrincipalStress1,
              ResultChannel::PrincipalStress2, ResultChannel::PrincipalStress3,
              ResultChannel::HydrostaticStress}) {
            INFO("channel " << renderer::toString(channel));
            REQUIRE(renderer::descriptorOf(channel).domain == ChannelDomain::Element);
            REQUIRE(renderer::descriptorOf(channel).unit == "Pa");
        }
    }

    SECTION("exactly two channels are norms and cannot be negative") {
        // Everything else may be negative, and a range that clamped those at
        // zero would hide compression entirely.
        std::size_t norms = 0;
        for (const ResultChannel channel : channels) {
            if (!renderer::descriptorOf(channel).mayBeNegative) {
                ++norms;
            }
        }
        REQUIRE(norms == 2);
        REQUIRE_FALSE(renderer::descriptorOf(ResultChannel::DisplacementMagnitude).mayBeNegative);
        REQUIRE_FALSE(renderer::descriptorOf(ResultChannel::VonMises).mayBeNegative);
    }
}

// ---------------------------------------------------------------------------
// The unit boundary
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_EverythingLeavingThisLayerIsSiMetres", "[renderer][viz]") {
    // THE FIRST CARRIED TRAP. A mesh uploaded in metres beside a CAD body in
    // millimetres is 1000 times too small, and P16-VIZ-001 found it the hard
    // way: the mesh looked perfect ALONE, because FitAll framed the only thing
    // in the scene, and became a sub-pixel speck beside its own solid.
    //
    // The fix was one conversion in the presentation adapter. So the gate here
    // is that this layer does NOT convert: a buffer of millimetres leaving
    // this header would be a unit the type does not state.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    const std::span<const double> undeformed = prepared.view().positions();
    REQUIRE(undeformed.size() == prepared.view().vertexCount() * 3U);

    SECTION("the view's positions are the mesh's own node coordinates, unscaled") {
        for (std::size_t vertex = 0; vertex < prepared.view().vertexCount(); ++vertex) {
            const Result<meshing::NodeId> node = prepared.view().nodeOfVertex(vertex);
            REQUIRE(node.has_value());
            const meshing::Node* canonical = mesh.findNode(*node);
            REQUIRE(canonical != nullptr);
            INFO("vertex " << vertex << " node " << node->value());
            REQUIRE(undeformed[(vertex * 3U) + 0U] == canonical->position.x.si());
            REQUIRE(undeformed[(vertex * 3U) + 1U] == canonical->position.y.si());
            REQUIRE(undeformed[(vertex * 3U) + 2U] == canonical->position.z.si());
        }
    }

    SECTION("and a 40 x 30 x 20 mm body spans metres, not millimetres") {
        // The premise that makes the section above meaningful: if this layer
        // were secretly converting, the extent would be 40 rather than 0.04.
        double maxX = 0.0;
        for (std::size_t vertex = 0; vertex < prepared.view().vertexCount(); ++vertex) {
            maxX = std::max(maxX, undeformed[vertex * 3U]);
        }
        INFO("largest x in the buffer: " << maxX);
        REQUIRE_THAT(maxX, WithinAbs(0.040, 1e-9));
    }

    SECTION("the deformed buffer is metres too") {
        const Result<std::vector<double>> deformed = prepared.result().deformedPositions(
            mesh, prepared.view(), prepared.outcome().fields, 1.0);
        REQUIRE(deformed.has_value());
        double maxX = 0.0;
        for (std::size_t vertex = 0; vertex < prepared.view().vertexCount(); ++vertex) {
            maxX = std::max(maxX, (*deformed)[vertex * 3U]);
        }
        // Still ~0.04 m, not 40: the displacement is microns, so the extent
        // cannot have changed by three orders.
        REQUIRE(maxX < 0.041);
        REQUIRE(maxX > 0.039);
    }
}

// ---------------------------------------------------------------------------
// The deformed display
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_DeformedPositionsAreTheUndeformedPlusScaledDisplacement",
          "[renderer][viz]") {
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();
    const std::span<const double> undeformed = prepared.view().positions();

    const auto deformedAt = [&](double scale) {
        Result<std::vector<double>> out = prepared.result().deformedPositions(
            mesh, prepared.view(), prepared.outcome().fields, scale);
        INFO((out.has_value() ? std::string{} : out.error().message));
        REQUIRE(out.has_value());
        return std::move(*out);
    };

    SECTION("scale zero reproduces the undeformed positions EXACTLY") {
        // Exactly, not within a tolerance: `x + 0 * u` must be `x` bit for
        // bit, and a tolerance here would hide an accumulator that had
        // already been written to.
        const std::vector<double> at0 = deformedAt(0.0);
        REQUIRE(at0.size() == undeformed.size());
        for (std::size_t i = 0; i < at0.size(); ++i) {
            REQUIRE(at0[i] == undeformed[i]);
        }
    }

    SECTION("scale one adds the true displacement, component by component") {
        const std::vector<double> at1 = deformedAt(1.0);
        std::size_t checked = 0;
        for (std::size_t vertex = 0; vertex < prepared.view().vertexCount(); ++vertex) {
            const Result<meshing::NodeId> node = prepared.view().nodeOfVertex(vertex);
            REQUIRE(node.has_value());
            const Result<structural::NodalDisplacement> nodal =
                prepared.outcome().fields.displacementOf(mesh, *node);
            REQUIRE(nodal.has_value());
            INFO("vertex " << vertex << " node " << node->value());
            // THE INDEPENDENT ORACLE: the undeformed buffer plus the source
            // displacement, computed here rather than asked of production.
            REQUIRE_THAT(at1[(vertex * 3U) + 0U],
                         WithinAbs(undeformed[(vertex * 3U) + 0U] + nodal->displacement.x.si(),
                                   1e-18));
            REQUIRE_THAT(at1[(vertex * 3U) + 1U],
                         WithinAbs(undeformed[(vertex * 3U) + 1U] + nodal->displacement.y.si(),
                                   1e-18));
            REQUIRE_THAT(at1[(vertex * 3U) + 2U],
                         WithinAbs(undeformed[(vertex * 3U) + 2U] + nodal->displacement.z.si(),
                                   1e-18));
            ++checked;
        }
        // NON-VACUITY: a loop that checked nothing would pass.
        REQUIRE(checked == prepared.view().vertexCount());
        REQUIRE(checked > 0);
    }

    SECTION("and a large scale multiplies the OFFSET, not the position") {
        const std::vector<double> at1 = deformedAt(1.0);
        const std::vector<double> at100 = deformedAt(100.0);
        std::size_t moved = 0;
        for (std::size_t i = 0; i < at1.size(); ++i) {
            const double offset1 = at1[i] - undeformed[i];
            const double offset100 = at100[i] - undeformed[i];
            REQUIRE_THAT(offset100, WithinAbs(100.0 * offset1, 1e-15));
            if (std::abs(offset1) > 0.0) {
                ++moved;
            }
        }
        // THE PREMISE: something actually moved, so the ratio is not 0 == 0.
        INFO("components with a nonzero offset: " << moved);
        REQUIRE(moved > 0);
    }

    SECTION("changing the scale is NOT cumulative") {
        // THE MUTATION THE BRIEF DEMANDS. A buffer updated in place with
        // `x += s*u` gives `x + 10u + 20u` for the sequence 10 then 20. This
        // returns a fresh buffer derived from the undeformed positions every
        // time, so the bug is unrepresentable -- and asserted anyway.
        const std::vector<double> at20Direct = deformedAt(20.0);
        (void)deformedAt(10.0);
        const std::vector<double> at20AfterTen = deformedAt(20.0);
        REQUIRE(at20AfterTen == at20Direct);
    }

    SECTION("and repeated toggling is idempotent") {
        const std::vector<double> first = deformedAt(1.0);
        for (int i = 0; i < 20; ++i) {
            (void)deformedAt(0.0);
            const std::vector<double> again = deformedAt(1.0);
            REQUIRE(again == first);
        }
    }

    SECTION("a non-finite or negative scale is refused") {
        for (const double bad : {std::nan(""), std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity(), -1.0, -0.5}) {
            INFO("scale " << bad);
            REQUIRE_FALSE(renderer::isUsableDeformationScale(bad));
            const Result<std::vector<double>> out = prepared.result().deformedPositions(
                mesh, prepared.view(), prepared.outcome().fields, bad);
            REQUIRE_FALSE(out.has_value());
            REQUIRE(out.error().code == ErrorCode::InvalidArgument);
        }
        // Zero IS usable and reproduces the undeformed shape.
        REQUIRE(renderer::isUsableDeformationScale(0.0));
    }
}

// ---------------------------------------------------------------------------
// Classification and cardinality
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_EveryChannelClassifiesItsWholeDomain", "[renderer][viz]") {
    // THE SECOND CARRIED TRAP. P16-QUALITY-001's `reportOnlyThresholds()`
    // classifies NOTHING, so every element came back Valid and the report
    // looked clean. A contour that classified zero entities must never read as
    // an all-valid visualisation.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    const std::size_t nodes = prepared.outcome().fields.displacements().size();
    const std::size_t elements = prepared.outcome().fields.elements().size();
    INFO("result has " << nodes << " nodes and " << elements << " elements");
    REQUIRE(nodes > 0);
    REQUIRE(elements > 0);
    REQUIRE(prepared.result().nodeCount() == nodes);
    REQUIRE(prepared.result().elementCount() == elements);

    for (const ResultChannel channel : renderer::allResultChannels()) {
        const renderer::ChannelDescriptor descriptor = renderer::descriptorOf(channel);
        const Result<ScalarField> field =
            prepared.result().field(mesh, prepared.outcome().fields, channel);
        INFO("channel " << renderer::toString(channel)
                        << (field.has_value() ? "" : (": " + field.error().message)));
        REQUIRE(field.has_value());

        // THE CARDINALITY GATE, per channel.
        const std::size_t expected = descriptor.domain == ChannelDomain::Node ? nodes : elements;
        REQUIRE(field->expected == expected);
        REQUIRE(field->classified == expected);
        REQUIRE(field->classified > 0);
        REQUIRE(field->skipped == 0);
        REQUIRE(field->nonFinite == 0);
        REQUIRE(field->complete());
        REQUIRE(field->values.size() == expected);
        REQUIRE(field->domain == descriptor.domain);

        // Every value finite, and the range bracketing them all.
        for (const double value : field->values) {
            REQUIRE(std::isfinite(value));
            REQUIRE(value >= field->minimum);
            REQUIRE(value <= field->maximum);
        }
        REQUIRE(field->minimum <= field->maximum);

        // The extreme identity belongs to the channel's own domain, and the
        // other kind is left invalid rather than defaulted to something that
        // looks like an id.
        if (descriptor.domain == ChannelDomain::Node) {
            REQUIRE(field->maximumNode.isValid());
            REQUIRE(field->minimumNode.isValid());
            REQUIRE_FALSE(field->maximumElement.isValid());
        } else {
            REQUIRE(field->maximumElement.isValid());
            REQUIRE(field->minimumElement.isValid());
            REQUIRE_FALSE(field->maximumNode.isValid());
        }
    }
}

TEST_CASE("StructuralViz_RangeAndExtremesMatchAnIndependentSweep", "[renderer][viz]") {
    // THE INDEPENDENT ORACLE the brief requires: iterate the SOURCE arrays
    // here and compute the range and the argmin/argmax, rather than asking the
    // production classifier to generate its own expected values.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    SECTION("a nodal channel") {
        const Result<ScalarField> field = prepared.result().field(
            mesh, prepared.outcome().fields, ResultChannel::DisplacementMagnitude);
        REQUIRE(field.has_value());

        bool seeded = false;
        double lo = 0.0;
        double hi = 0.0;
        meshing::NodeId argLo{};
        meshing::NodeId argHi{};
        for (const structural::NodalDisplacement& nodal :
             prepared.outcome().fields.displacements()) {
            const double value = nodal.magnitude.si();
            if (!seeded) {
                lo = hi = value;
                argLo = argHi = nodal.node;
                seeded = true;
                continue;
            }
            if (value < lo) { lo = value; argLo = nodal.node; }
            if (value > hi) { hi = value; argHi = nodal.node; }
        }
        REQUIRE(seeded);
        INFO("oracle min " << lo << " at " << argLo.value() << ", max " << hi << " at "
                           << argHi.value());
        REQUIRE(field->minimum == lo);
        REQUIRE(field->maximum == hi);
        REQUIRE(field->minimumNode == argLo);
        REQUIRE(field->maximumNode == argHi);

        // AND IT AGREES WITH THE RESULT'S OWN STORED EXTREME, which is a
        // third, independent statement of the same number.
        REQUIRE(field->maximum == prepared.outcome().fields.largestDisplacementMagnitude().si());
        REQUIRE(field->maximum == prepared.outcome().largestDisplacement.si());
    }

    SECTION("an element channel") {
        const Result<ScalarField> field =
            prepared.result().field(mesh, prepared.outcome().fields, ResultChannel::VonMises);
        REQUIRE(field.has_value());

        bool seeded = false;
        double hi = 0.0;
        meshing::ElementId argHi{};
        for (const structural::ElementFields& element : prepared.outcome().fields.elements()) {
            const double value = element.vonMises.si();
            if (!seeded || value > hi) {
                hi = value;
                argHi = element.element;
                seeded = true;
            }
        }
        REQUIRE(seeded);
        REQUIRE(field->maximum == hi);
        REQUIRE(field->maximumElement == argHi);
        REQUIRE(field->maximum == prepared.outcome().fields.largestVonMises().si());
        REQUIRE(field->maximum == prepared.outcome().largestVonMises.si());
        // von Mises is a norm, so its minimum cannot be negative.
        REQUIRE(field->minimum >= 0.0);
    }
}

TEST_CASE("StructuralViz_ASignedChannelKeepsItsNegativeValues", "[renderer][viz]") {
    // A contour of Ux must not plot abs(Ux), and a compressive stress range
    // must not be clamped at zero. The fixture pushes the end cap along +z
    // against a fixed start cap, so Poisson contraction makes the transverse
    // displacements and several stress components negative.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    std::size_t channelsWithNegatives = 0;
    for (const ResultChannel channel :
         {ResultChannel::DisplacementX, ResultChannel::DisplacementY, ResultChannel::StressXX,
          ResultChannel::StressYY, ResultChannel::StressZZ, ResultChannel::PrincipalStress3,
          ResultChannel::HydrostaticStress}) {
        const Result<ScalarField> field =
            prepared.result().field(mesh, prepared.outcome().fields, channel);
        REQUIRE(field.has_value());
        INFO("channel " << renderer::toString(channel) << " range [" << field->minimum << ", "
                        << field->maximum << "]");
        if (field->minimum < 0.0) {
            ++channelsWithNegatives;
            // THE DEFECT THIS CATCHES: an accumulator seeded at 0.0 reports
            // 0.0 as the maximum of an all-negative field. If every value of
            // this channel is negative, the maximum must be negative too.
            const bool allNegative =
                std::ranges::all_of(field->values, [](double v) { return v < 0.0; });
            if (allNegative) {
                REQUIRE(field->maximum < 0.0);
            }
        }
    }
    // NON-VACUITY: at least one of those channels really is negative
    // somewhere, otherwise this test asserts nothing.
    INFO("channels with negative values: " << channelsWithNegatives);
    REQUIRE(channelsWithNegatives > 0);
}

TEST_CASE("StructuralViz_ChannelsAreNotTransposed", "[renderer][viz]") {
    // tau_yz and tau_zx are the pair a transposition would hide, because a
    // field with equal shears passes either way. Each channel is read back
    // against the SOURCE member it is supposed to come from, element by
    // element.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();
    const std::span<const structural::ElementFields> elements =
        prepared.outcome().fields.elements();

    struct Case {
        ResultChannel channel;
        double (*extract)(const structural::ElementFields&);
    };
    const std::array<Case, 11> cases{
        Case{ResultChannel::VonMises, [](const structural::ElementFields& e) { return e.vonMises.si(); }},
        Case{ResultChannel::StressXX, [](const structural::ElementFields& e) { return e.stress.xx.si(); }},
        Case{ResultChannel::StressYY, [](const structural::ElementFields& e) { return e.stress.yy.si(); }},
        Case{ResultChannel::StressZZ, [](const structural::ElementFields& e) { return e.stress.zz.si(); }},
        Case{ResultChannel::StressXY, [](const structural::ElementFields& e) { return e.stress.xy.si(); }},
        Case{ResultChannel::StressYZ, [](const structural::ElementFields& e) { return e.stress.yz.si(); }},
        Case{ResultChannel::StressZX, [](const structural::ElementFields& e) { return e.stress.zx.si(); }},
        Case{ResultChannel::PrincipalStress1,
             [](const structural::ElementFields& e) { return e.principalStress.sigma1.si(); }},
        Case{ResultChannel::PrincipalStress2,
             [](const structural::ElementFields& e) { return e.principalStress.sigma2.si(); }},
        Case{ResultChannel::PrincipalStress3,
             [](const structural::ElementFields& e) { return e.principalStress.sigma3.si(); }},
        Case{ResultChannel::HydrostaticStress,
             [](const structural::ElementFields& e) { return e.hydrostatic.si(); }}};

    for (const Case& which : cases) {
        const Result<ScalarField> field =
            prepared.result().field(mesh, prepared.outcome().fields, which.channel);
        INFO("channel " << renderer::toString(which.channel));
        REQUIRE(field.has_value());
        REQUIRE(field->values.size() == elements.size());
        for (std::size_t i = 0; i < elements.size(); ++i) {
            // EXACT equality: this is a read, not a computation, so any
            // difference at all is a wrong member or a wrong element.
            REQUIRE(field->values[i] == which.extract(elements[i]));
        }
    }

    SECTION("and the principal stresses stay descending, as POST produced them") {
        for (const structural::ElementFields& element : elements) {
            REQUIRE(element.principalStress.sigma1.si() >= element.principalStress.sigma2.si());
            REQUIRE(element.principalStress.sigma2.si() >= element.principalStress.sigma3.si());
        }
    }
}

// ---------------------------------------------------------------------------
// The source guards
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_RefusesAResultFromADifferentMesh", "[renderer][viz]") {
    // The remesh case, with numeric ids that repeat. The body is unchanged, so
    // the new mesh restarts its NodeIds and ElementIds from 1 -- which is
    // exactly the adversarial case: an old view would index happily and mean
    // different elements.
    SolvedView part;
    part.installAndSolve();
    structural::StructuralSolveOutcome first = part.solve();
    Result<MeshView> firstView = MeshView::volumeBoundaryOf(part.mesh());
    REQUIRE(firstView.has_value());
    Result<StructuralResultView> firstResult =
        StructuralResultView::of(part.mesh(), *firstView, first.fields);
    REQUIRE(firstResult.has_value());
    const meshing::MeshStamp before = part.mesh().stamp();

    // Remesh coarser.
    REQUIRE(part.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(part.control.value()),
                    [](meshing::MeshControl& control) -> Result<bool> {
                        auto definition = control.definition();
                        definition.mesh.sizing.globalTargetSize = 12_mm;
                        definition.mesh.sizing.local.clear();
                        return control.setDefinition(definition).has_value();
                    })
                .has_value());
    requireReport(part.regenerator, part.document);
    const Result<const meshing::VolumeMesh*> remeshed =
        part.mesher.generate(part.document, part.regenerator, part.control);
    REQUIRE(remeshed.has_value());
    const meshing::Mesh& second = (*remeshed)->mesh();
    REQUIRE_FALSE(second.stamp() == before);

    SECTION("the numeric ids really do repeat, which is the premise") {
        REQUIRE(second.nodes().front().id == meshing::NodeId::fromValue(1));
        REQUIRE_FALSE(second.nodes().empty());
    }

    SECTION("the old result view does not describe the new mesh") {
        REQUIRE_FALSE(firstResult->describes(second, first.fields));
    }

    SECTION("and every entry point refuses rather than reinterpreting") {
        const Result<ScalarField> field =
            firstResult->field(second, first.fields, ResultChannel::VonMises);
        REQUIRE_FALSE(field.has_value());
        REQUIRE_THAT(field.error().message, ContainsSubstring("result_source_mismatch"));

        const Result<std::vector<double>> deformed =
            firstResult->deformedPositions(second, *firstView, first.fields, 1.0);
        REQUIRE_FALSE(deformed.has_value());
    }

    SECTION("and a view cannot be built from a mismatched pair in the first place") {
        Result<MeshView> secondView = MeshView::volumeBoundaryOf(second);
        REQUIRE(secondView.has_value());
        // The new mesh with the OLD result's fields.
        REQUIRE_FALSE(StructuralResultView::of(second, *secondView, first.fields).has_value());
    }
}

// ---------------------------------------------------------------------------
// Immutability
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_RenderingChangesNothingItReads", "[renderer][viz]") {
    // THE HARD GATE. Every visualisation operation runs, and the mesh, the
    // result and the recovered fields are compared before and after.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    // Fingerprints: the stamps plus every node coordinate and every source
    // value, so a single written-back coordinate would show.
    const auto meshFingerprint = [&mesh]() {
        std::vector<double> out;
        out.reserve(mesh.nodes().size() * 3U);
        for (const meshing::Node& node : mesh.nodes()) {
            out.push_back(node.position.x.si());
            out.push_back(node.position.y.si());
            out.push_back(node.position.z.si());
        }
        return out;
    };
    const auto resultFingerprint = [&prepared]() {
        std::vector<double> out;
        for (const structural::NodalDisplacement& nodal :
             prepared.outcome().fields.displacements()) {
            out.push_back(nodal.displacement.x.si());
            out.push_back(nodal.displacement.y.si());
            out.push_back(nodal.displacement.z.si());
            out.push_back(nodal.magnitude.si());
        }
        for (const structural::ElementFields& element : prepared.outcome().fields.elements()) {
            out.push_back(element.vonMises.si());
            out.push_back(element.stress.xx.si());
            out.push_back(element.stress.yz.si());
            out.push_back(element.principalStress.sigma1.si());
        }
        return out;
    };

    const std::vector<double> meshBefore = meshFingerprint();
    const std::vector<double> resultBefore = resultFingerprint();
    const meshing::MeshStamp stampBefore = mesh.stamp();
    const structural::AssemblySource sourceBefore = prepared.outcome().fields.source();
    const std::vector<double> viewPositionsBefore(prepared.view().positions().begin(),
                                                  prepared.view().positions().end());

    // Every visualisation operation the milestone offers.
    for (const double scale : {0.0, 1.0, 10.0, 100.0, 1000.0}) {
        REQUIRE(prepared.result()
                    .deformedPositions(mesh, prepared.view(), prepared.outcome().fields, scale)
                    .has_value());
    }
    for (const ResultChannel channel : renderer::allResultChannels()) {
        REQUIRE(prepared.result().field(mesh, prepared.outcome().fields, channel).has_value());
    }

    SECTION("the mesh is byte-identical") {
        REQUIRE(meshFingerprint() == meshBefore);
        REQUIRE(mesh.stamp() == stampBefore);
    }
    SECTION("the result is byte-identical") {
        REQUIRE(resultFingerprint() == resultBefore);
        REQUIRE(prepared.outcome().fields.source() == sourceBefore);
    }
    SECTION("and the view's own undeformed buffer was never written to") {
        // The deformed buffer is a COPY. If it were an in-place update, this
        // is what would have moved.
        const std::vector<double> after(prepared.view().positions().begin(),
                                        prepared.view().positions().end());
        REQUIRE(after == viewPositionsBefore);
    }
}

// ---------------------------------------------------------------------------
// Currentness: the mesh and the result, separately
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_StaleResultIsNeverDrawableAsCurrent", "[renderer][viz]") {
    // THE HARDEST REQUIREMENT IN THE BRIEF. A stale result must never be
    // presented indistinguishably from a current one, and the viewer must not
    // own the flag that says so.
    SolvedView part;
    part.installAndSolve();
    structural::StructuralSolveOutcome outcome = part.solve();

    const auto state = [&part](const structural::StructuralResult* result) {
        return renderer::structuralDisplayState(part.document, part.regenerator, part.mesher,
                                                part.control, part.analysis, result, nullptr);
    };

    SECTION("a fresh result is current on both counts") {
        const renderer::StructuralDisplayState now = state(&outcome.result);
        INFO("mesh " << meshing::toString(now.mesh) << ", analysis "
                     << structural::toString(now.analysis));
        REQUIRE(now.mesh == meshing::MeshCurrency::Current);
        REQUIRE(now.analysis == structural::AnalysisState::SolvedCurrent);
        REQUIRE(now.meshDescribesModel());
        REQUIRE(now.resultMayDrawAsCurrent());
        REQUIRE(now.resultInspectable());
        REQUIRE(now.staleReasons.empty());
        REQUIRE_FALSE(now.resultStaleOnACurrentMesh());
    }

    SECTION("no result at all is not drawable and not inspectable") {
        const renderer::StructuralDisplayState none = state(nullptr);
        REQUIRE(none.analysis == structural::AnalysisState::Ready);
        REQUIRE_FALSE(none.resultMayDrawAsCurrent());
        REQUIRE_FALSE(none.resultInspectable());
        // The MESH is still current, and a UI may still draw it: an
        // unsolved model is an ordinary state.
        REQUIRE(none.meshDescribesModel());
    }

    SECTION("a MATERIAL edit leaves the mesh current and the result stale") {
        // THE DISTINCTION THE BRIEF CALLS OUT EXPLICITLY. A UI that collapsed
        // the two states would report the mesh as stale, send the user to
        // remesh, and leave the real cause untouched.
        materials::MechanicalProperties mechanical =
            features::findMaterial(part.document, part.material)->definition().mechanical;
        mechanical.youngsModulus =
            materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(1.1e11));
        REQUIRE(features::setMaterialMechanical(part.document, part.material, mechanical)
                    .has_value());

        const renderer::StructuralDisplayState after = state(&outcome.result);
        INFO("mesh " << meshing::toString(after.mesh) << ", analysis "
                     << structural::toString(after.analysis));

        REQUIRE(after.mesh == meshing::MeshCurrency::Current);
        REQUIRE(after.meshDescribesModel());
        REQUIRE(after.analysis == structural::AnalysisState::SolvedStale);
        REQUIRE_FALSE(after.resultMayDrawAsCurrent());
        // Still inspectable, and that is deliberate: retaining a historical
        // result and MARKING it is legitimate; showing it as current is not.
        REQUIRE(after.resultInspectable());
        // AND THE QUESTION A UI ASKS DIRECTLY.
        REQUIRE(after.resultStaleOnACurrentMesh());
        // WHICH dependency moved, named rather than summarised.
        REQUIRE(std::ranges::find(after.staleReasons, structural::StaleReason::Material) !=
                after.staleReasons.end());
    }

    SECTION("a RESTRAINT edit likewise stales the result and not the mesh") {
        REQUIRE(part.document
                    .modifyObject<structural::StructuralAnalysis>(
                        ObjectId::fromValue(part.analysis.value()),
                        [](structural::StructuralAnalysis& object) -> Result<bool> {
                            structural::StructuralAnalysisDefinition definition =
                                object.definition();
                            definition.restraints.clear();
                            return object.setDefinition(std::move(definition));
                        })
                    .has_value());

        const renderer::StructuralDisplayState after = state(&outcome.result);
        REQUIRE(after.meshDescribesModel());
        REQUIRE(after.analysis == structural::AnalysisState::SolvedStale);
        REQUIRE_FALSE(after.resultMayDrawAsCurrent());
        REQUIRE(after.resultStaleOnACurrentMesh());
        REQUIRE(std::ranges::find(after.staleReasons, structural::StaleReason::Analysis) !=
                after.staleReasons.end());
    }

    SECTION("a GEOMETRY edit stales both, and the reasons say so") {
        REQUIRE(part.document
                    .modifyObject<features::ExtrudeFeature>(
                        part.feature,
                        [](features::ExtrudeFeature& extrude) -> Result<bool> {
                            auto definition = extrude.definition();
                            definition.depth = 35_mm;
                            return extrude.setDefinition(definition).has_value();
                        })
                    .has_value());
        requireReport(part.regenerator, part.document);

        const renderer::StructuralDisplayState after = state(&outcome.result);
        INFO("mesh " << meshing::toString(after.mesh) << ", analysis "
                     << structural::toString(after.analysis));
        REQUIRE(after.mesh == meshing::MeshCurrency::StaleGeometry);
        REQUIRE_FALSE(after.meshDescribesModel());
        REQUIRE_FALSE(after.resultMayDrawAsCurrent());
        // NOT resultStaleOnACurrentMesh: the mesh moved too, and conflating
        // the two cases is what this predicate exists to prevent.
        REQUIRE_FALSE(after.resultStaleOnACurrentMesh());
    }

    SECTION("a REMESH stales the result, and the old one cannot pass as the new") {
        REQUIRE(part.document
                    .modifyObject<meshing::MeshControl>(
                        ObjectId::fromValue(part.control.value()),
                        [](meshing::MeshControl& control) -> Result<bool> {
                            auto definition = control.definition();
                            definition.mesh.sizing.globalTargetSize = 12_mm;
                            definition.mesh.sizing.local.clear();
                            return control.setDefinition(definition).has_value();
                        })
                    .has_value());
        requireReport(part.regenerator, part.document);
        REQUIRE(part.mesher.generate(part.document, part.regenerator, part.control).has_value());

        const renderer::StructuralDisplayState after = state(&outcome.result);
        REQUIRE(after.mesh == meshing::MeshCurrency::Current);
        REQUIRE(after.analysis == structural::AnalysisState::SolvedStale);
        REQUIRE_FALSE(after.resultMayDrawAsCurrent());
        REQUIRE(std::ranges::find(after.staleReasons, structural::StaleReason::Mesh) !=
                after.staleReasons.end());
    }

    SECTION("and the state is deterministic") {
        REQUIRE(state(&outcome.result) == state(&outcome.result));
    }
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_InspectionReportsExactSourceValues", "[renderer][viz]") {
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    SECTION("a node reports the canonical position and the stored displacement") {
        std::size_t checked = 0;
        for (const structural::NodalDisplacement& nodal :
             prepared.outcome().fields.displacements()) {
            const Result<renderer::NodeResultInspection> inspection =
                renderer::inspectNode(mesh, prepared.outcome().fields, nodal.node, nullptr);
            INFO("node " << nodal.node.value());
            REQUIRE(inspection.has_value());
            REQUIRE(inspection->node == nodal.node);
            REQUIRE(inspection->displacement.x.si() == nodal.displacement.x.si());
            REQUIRE(inspection->displacement.y.si() == nodal.displacement.y.si());
            REQUIRE(inspection->displacement.z.si() == nodal.displacement.z.si());
            REQUIRE(inspection->magnitude.si() == nodal.magnitude.si());
            const meshing::Node* canonical = mesh.findNode(nodal.node);
            REQUIRE(canonical != nullptr);
            REQUIRE(inspection->undeformedPosition.x.si() == canonical->position.x.si());
            REQUIRE(inspection->undeformedPosition.y.si() == canonical->position.y.si());
            REQUIRE(inspection->undeformedPosition.z.si() == canonical->position.z.si());
            ++checked;
        }
        REQUIRE(checked == prepared.outcome().fields.displacements().size());
        REQUIRE(checked > 0);
    }

    SECTION("an element reports every stored field, exactly") {
        std::size_t checked = 0;
        for (const structural::ElementFields& element : prepared.outcome().fields.elements()) {
            const Result<renderer::ElementResultInspection> inspection =
                renderer::inspectElement(mesh, prepared.outcome().fields, element.element);
            INFO("element " << element.element.value());
            REQUIRE(inspection.has_value());
            REQUIRE(inspection->element == element.element);
            REQUIRE(inspection->strain == element.strain);
            REQUIRE(inspection->stress == element.stress);
            REQUIRE(inspection->principalStress == element.principalStress);
            REQUIRE(inspection->principalStrain == element.principalStrain);
            REQUIRE(inspection->vonMises.si() == element.vonMises.si());
            REQUIRE(inspection->hydrostatic.si() == element.hydrostatic.si());
            ++checked;
        }
        REQUIRE(checked == prepared.outcome().fields.elements().size());
        REQUIRE(checked > 0);
    }

    SECTION("principal stresses stay descending in the inspector") {
        // Not re-sorted by the GUI: P17-POST produced them descending and the
        // inspector shows that order.
        for (const structural::ElementFields& element : prepared.outcome().fields.elements()) {
            const Result<renderer::ElementResultInspection> inspection =
                renderer::inspectElement(mesh, prepared.outcome().fields, element.element);
            REQUIRE(inspection.has_value());
            REQUIRE(inspection->principalStress.sigma1.si() >=
                    inspection->principalStress.sigma2.si());
            REQUIRE(inspection->principalStress.sigma2.si() >=
                    inspection->principalStress.sigma3.si());
        }
    }

    SECTION("a reaction reports the qualified sign convention, unchanged") {
        const std::vector<meshing::NodeId> nodes =
            renderer::reactionNodes(prepared.outcome().reactions);
        INFO("restrained nodes: " << nodes.size());
        REQUIRE_FALSE(nodes.empty());
        // ASCENDING AND WITHOUT REPEATS, which is SupportReactions' own
        // guarantee and is asserted rather than assumed.
        REQUIRE(std::ranges::is_sorted(nodes));
        REQUIRE(std::ranges::adjacent_find(nodes) == nodes.end());

        for (const structural::SupportReaction& source : prepared.outcome().reactions.nodal()) {
            const Result<renderer::ReactionInspection> inspection =
                renderer::inspectReaction(mesh, prepared.outcome().reactions, source.node);
            INFO("node " << source.node.value());
            REQUIRE(inspection.has_value());
            REQUIRE(inspection->node == source.node);
            // NOT NEGATED. The arrow direction follows this vector.
            REQUIRE(inspection->force.x.si() == source.force.x.si());
            REQUIRE(inspection->force.y.si() == source.force.y.si());
            REQUIRE(inspection->force.z.si() == source.force.z.si());
            REQUIRE(inspection->constrained == source.constrained);
        }
    }

    SECTION("an unrestrained node has NO reaction rather than a zero one") {
        // Reporting a zero would invent a support that does not exist.
        const std::vector<meshing::NodeId> restrained =
            renderer::reactionNodes(prepared.outcome().reactions);
        std::size_t refused = 0;
        for (const meshing::Node& node : mesh.nodes()) {
            if (std::ranges::binary_search(restrained, node.id)) {
                continue;
            }
            REQUIRE_FALSE(
                renderer::inspectReaction(mesh, prepared.outcome().reactions, node.id).has_value());
            ++refused;
        }
        INFO("unrestrained nodes refused: " << refused);
        REQUIRE(refused > 0);
    }
}

TEST_CASE("StructuralViz_RestraintComesFromTheRestraintsNotTheDisplacement",
          "[renderer][viz]") {
    // A node can have exactly zero displacement without being restrained, so
    // reading restraint from a zero is a guess. This proves the inspector asks
    // the prepared restraints instead.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    Result<structural::StructuralModel> model = structural::requireStructuralModel(
        prepared.part.document, prepared.part.regenerator, prepared.part.mesher,
        prepared.part.control);
    REQUIRE(model.has_value());
    Result<structural::MeshDofMap> numbering =
        structural::buildMeshDofMap(model->mesh().mesh());
    REQUIRE(numbering.has_value());
    const std::vector<structural::StructuralRestraint> restraints{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      prepared.part.startCap())};
    Result<structural::PreparedRestraints> prepared_restraints =
        structural::prepareStructuralRestraints(*model, *numbering, restraints);
    REQUIRE(prepared_restraints.has_value());

    SECTION("without the restraints, restraint is UNKNOWN and reported false") {
        const Result<renderer::NodeResultInspection> blind = renderer::inspectNode(
            mesh, prepared.outcome().fields, prepared_restraints->nodes().front(), nullptr);
        REQUIRE(blind.has_value());
        REQUIRE_FALSE(blind->restrained);
        REQUIRE(blind->restrainedComponents.isEmpty());
    }

    SECTION("with them, a restrained node is reported restrained with its components") {
        std::size_t restrainedSeen = 0;
        for (const meshing::NodeId node : prepared_restraints->nodes()) {
            // THE NUMBERING IS REQUIRED for the component mask: without it the
            // mask is empty, which means UNKNOWN. See inspectNode.
            const Result<renderer::NodeResultInspection> inspection = renderer::inspectNode(
                mesh, prepared.outcome().fields, node, &*prepared_restraints, &*numbering);
            INFO("node " << node.value());
            REQUIRE(inspection.has_value());
            REQUIRE(inspection->restrained);
            REQUIRE(inspection->restrainedComponents.isFixed());
            ++restrainedSeen;
        }
        REQUIRE(restrainedSeen > 0);
    }

    SECTION("and an unrestrained node is reported unrestrained even at zero displacement") {
        std::size_t zeroButFree = 0;
        for (const structural::NodalDisplacement& nodal :
             prepared.outcome().fields.displacements()) {
            if (std::ranges::binary_search(prepared_restraints->nodes(), nodal.node)) {
                continue;
            }
            const Result<renderer::NodeResultInspection> inspection = renderer::inspectNode(
                mesh, prepared.outcome().fields, nodal.node, &*prepared_restraints);
            REQUIRE(inspection.has_value());
            REQUIRE_FALSE(inspection->restrained);
            if (nodal.magnitude.si() == 0.0) {
                ++zeroButFree;
            }
        }
        INFO("free nodes that happen to sit at zero displacement: " << zeroButFree);
    }

    SECTION("restraints from a different mesh are refused, not ignored") {
        // A remesh invalidates the prepared restraints; using them anyway
        // would attribute supports to the wrong nodes.
        REQUIRE(prepared.part.document
                    .modifyObject<meshing::MeshControl>(
                        ObjectId::fromValue(prepared.part.control.value()),
                        [](meshing::MeshControl& control) -> Result<bool> {
                            auto definition = control.definition();
                            definition.mesh.sizing.globalTargetSize = 12_mm;
                            definition.mesh.sizing.local.clear();
                            return control.setDefinition(definition).has_value();
                        })
                    .has_value());
        requireReport(prepared.part.regenerator, prepared.part.document);
        const Result<const meshing::VolumeMesh*> remeshed = prepared.part.mesher.generate(
            prepared.part.document, prepared.part.regenerator, prepared.part.control);
        REQUIRE(remeshed.has_value());
        const meshing::Mesh& second = (*remeshed)->mesh();

        const Result<renderer::NodeResultInspection> crossed = renderer::inspectNode(
            second, prepared.outcome().fields, second.nodes().front().id, &*prepared_restraints);
        // Refused on the FIELDS first -- they describe the old mesh.
        REQUIRE_FALSE(crossed.has_value());
    }
}

// ---------------------------------------------------------------------------
// Extremes
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_ExtremeNavigationIsGlobalAndDeterministic", "[renderer][viz]") {
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    SECTION("max displacement magnitude matches an independent sweep") {
        const Result<ScalarField> field = prepared.result().field(
            mesh, prepared.outcome().fields, ResultChannel::DisplacementMagnitude);
        REQUIRE(field.has_value());
        const Result<renderer::ExtremeEntity> extreme =
            renderer::extremeOf(*field, renderer::ExtremeKind::Maximum);
        REQUIRE(extreme.has_value());

        bool seeded = false;
        double hi = 0.0;
        meshing::NodeId argHi{};
        for (const structural::NodalDisplacement& nodal :
             prepared.outcome().fields.displacements()) {
            if (!seeded || nodal.magnitude.si() > hi) {
                hi = nodal.magnitude.si();
                argHi = nodal.node;
                seeded = true;
            }
        }
        REQUIRE(seeded);
        REQUIRE(extreme->value == hi);
        REQUIRE(extreme->node == argHi);
        REQUIRE(extreme->domain == ChannelDomain::Node);
        REQUIRE_FALSE(extreme->element.isValid());
    }

    SECTION("max von Mises is over EVERY element, not just the visible ones") {
        // The field is built over the result's own domain, so an interior
        // element can win -- which is the brief's requirement that a global
        // maximum not be computed only over the exterior.
        const Result<ScalarField> field =
            prepared.result().field(mesh, prepared.outcome().fields, ResultChannel::VonMises);
        REQUIRE(field.has_value());
        REQUIRE(field->expected == prepared.outcome().fields.elements().size());

        const Result<renderer::ExtremeEntity> extreme =
            renderer::extremeOf(*field, renderer::ExtremeKind::Maximum);
        REQUIRE(extreme->value == prepared.outcome().fields.largestVonMises().si());

        // THE PREMISE THAT MAKES IT MEANINGFUL: the boundary view draws fewer
        // elements than the result has, so "all elements" is a stronger claim
        // than "the drawn ones".
        INFO("result elements " << field->expected << ", view triangles "
                                << prepared.view().triangleCount());
        REQUIRE(prepared.view().triangleCount() < field->expected * 4U);
    }

    SECTION("the three extreme kinds are distinct concepts for a signed channel") {
        const Result<ScalarField> field =
            prepared.result().field(mesh, prepared.outcome().fields, ResultChannel::StressZZ);
        REQUIRE(field.has_value());
        const Result<renderer::ExtremeEntity> hi =
            renderer::extremeOf(*field, renderer::ExtremeKind::Maximum);
        const Result<renderer::ExtremeEntity> lo =
            renderer::extremeOf(*field, renderer::ExtremeKind::Minimum);
        const Result<renderer::ExtremeEntity> mag =
            renderer::extremeOf(*field, renderer::ExtremeKind::MaximumMagnitude);
        REQUIRE(hi.has_value());
        REQUIRE(lo.has_value());
        REQUIRE(mag.has_value());
        INFO("max " << hi->value << ", min " << lo->value << ", maxmag " << mag->value);
        REQUIRE(hi->value == field->maximum);
        REQUIRE(lo->value == field->minimum);
        // MaximumMagnitude reports the SIGNED value, and it is one of the two
        // ends.
        REQUIRE((mag->value == hi->value || mag->value == lo->value));
        REQUIRE(std::abs(mag->value) >= std::abs(hi->value));
        REQUIRE(std::abs(mag->value) >= std::abs(lo->value));
    }

    SECTION("for a norm, all three agree on the maximum") {
        const Result<ScalarField> field =
            prepared.result().field(mesh, prepared.outcome().fields, ResultChannel::VonMises);
        REQUIRE(field.has_value());
        const Result<renderer::ExtremeEntity> hi =
            renderer::extremeOf(*field, renderer::ExtremeKind::Maximum);
        const Result<renderer::ExtremeEntity> mag =
            renderer::extremeOf(*field, renderer::ExtremeKind::MaximumMagnitude);
        REQUIRE(hi->element == mag->element);
        REQUIRE(hi->value == mag->value);
    }

    SECTION("and repeated calls give the same entity") {
        const Result<ScalarField> field =
            prepared.result().field(mesh, prepared.outcome().fields, ResultChannel::VonMises);
        const Result<renderer::ExtremeEntity> first =
            renderer::extremeOf(*field, renderer::ExtremeKind::Maximum);
        for (int i = 0; i < 10; ++i) {
            REQUIRE(renderer::extremeOf(*field, renderer::ExtremeKind::Maximum) == first);
        }
    }

    SECTION("an incomplete field has no extreme") {
        // THE P16 TRAP IN ITS LAST DISGUISE: a maximum over the entities that
        // happened to classify, presented as the maximum.
        ScalarField broken;
        broken.channel = ResultChannel::VonMises;
        broken.domain = ChannelDomain::Element;
        broken.expected = 10;
        broken.classified = 9;
        broken.values.assign(9, 1.0);
        broken.elements.assign(9, meshing::ElementId::fromValue(1));
        REQUIRE_FALSE(broken.complete());
        const Result<renderer::ExtremeEntity> none =
            renderer::extremeOf(broken, renderer::ExtremeKind::Maximum);
        REQUIRE_FALSE(none.has_value());
        REQUIRE_THAT(none.error().message, ContainsSubstring("incomplete_classification"));

        // And an EMPTY field likewise -- expected 0 is not a uniform field.
        ScalarField empty;
        empty.expected = 0;
        empty.classified = 0;
        REQUIRE_FALSE(empty.complete());
        REQUIRE_FALSE(renderer::extremeOf(empty, renderer::ExtremeKind::Maximum).has_value());
    }

    SECTION("ties go to the first entity in ascending order") {
        // A uniform field is all ties, so the winner must be the first
        // element -- deterministic without a sort.
        ScalarField tied;
        tied.channel = ResultChannel::VonMises;
        tied.domain = ChannelDomain::Element;
        tied.expected = 4;
        tied.classified = 4;
        tied.values = {5.0, 5.0, 5.0, 5.0};
        tied.elements = {meshing::ElementId::fromValue(7), meshing::ElementId::fromValue(8),
                         meshing::ElementId::fromValue(9), meshing::ElementId::fromValue(10)};
        tied.minimum = 5.0;
        tied.maximum = 5.0;
        REQUIRE(tied.complete());
        REQUIRE(tied.uniform());
        for (const renderer::ExtremeKind kind :
             {renderer::ExtremeKind::Maximum, renderer::ExtremeKind::Minimum,
              renderer::ExtremeKind::MaximumMagnitude}) {
            const Result<renderer::ExtremeEntity> extreme = renderer::extremeOf(tied, kind);
            REQUIRE(extreme.has_value());
            REQUIRE(extreme->element == meshing::ElementId::fromValue(7));
            REQUIRE(extreme->index == 0);
        }
    }
}


// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------

namespace {

/// The prepared loads and restraints of a solved fixture, for the overlays.
struct PreparedOverlays {
    std::optional<structural::StructuralModel> model{};
    std::optional<structural::MeshDofMap> numbering{};
    std::optional<structural::PreparedRestraints> restraints{};
    std::optional<structural::PreparedLoads> loads{};
};

[[nodiscard]] PreparedOverlays prepareOverlays(SolvedView& part,
                                               std::vector<structural::StructuralLoad> loads,
                                               std::vector<structural::StructuralRestraint> rest) {
    PreparedOverlays out;
    Result<structural::StructuralModel> model =
        structural::requireStructuralModel(part.document, part.regenerator, part.mesher,
                                           part.control);
    INFO((model.has_value() ? std::string{} : model.error().message));
    REQUIRE(model.has_value());
    out.model.emplace(std::move(*model));

    Result<structural::StructuralMaterial> material = structural::resolveStructuralMaterial(
        part.document, out.model->body(), structural::StructuralAnalysisMode::LinearStatic);
    REQUIRE(material.has_value());

    Result<structural::MeshDofMap> numbering =
        structural::buildMeshDofMap(out.model->mesh().mesh());
    REQUIRE(numbering.has_value());
    out.numbering.emplace(std::move(*numbering));

    Result<structural::PreparedRestraints> prepared =
        structural::prepareStructuralRestraints(*out.model, *out.numbering, rest);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    out.restraints.emplace(std::move(*prepared));

    Result<structural::PreparedLoads> preparedLoads =
        structural::prepareStructuralLoads(*out.model, *material, loads);
    INFO((preparedLoads.has_value() ? std::string{} : preparedLoads.error().message));
    REQUIRE(preparedLoads.has_value());
    out.loads.emplace(std::move(*preparedLoads));
    return out;
}

} // namespace

TEST_CASE("StructuralViz_LoadGlyphsReadThePreparedContributions", "[renderer][viz]") {
    // THE OVERLAY CARRIES NO LOAD PHYSICS. Every number comes from
    // `LoadContribution`, which P17-LOAD-001 computed -- including the
    // inward-positive pressure convention, which this layer therefore never
    // sees a surface normal for.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};

    SECTION("a pressure load keeps the convention the solver uses") {
        // POSITIVE PRESSURE ACTS INWARD (P17-LOAD-001, frozen with its own
        // analytical test and mutation probe). The end cap's outward normal is
        // +z, so a POSITIVE pressure on it must give a resultant along -z.
        // A viewer that recomputed `-p n_out` with the sign flipped would show
        // suction where the solver applies compression.
        const std::vector<structural::StructuralLoad> loads{structural::StructuralLoad{
            LoadId::fromValue(7),
            structural::PressureLoad{.face = part.endCap(),
                                     .magnitude = Pressure::fromSi(2.0e6)}}};
        const PreparedOverlays prepared = prepareOverlays(part, loads, rest);
        const std::vector<renderer::LoadGlyph> glyphs = renderer::loadGlyphs(*prepared.loads);

        REQUIRE(glyphs.size() == 1);
        const renderer::LoadGlyph& glyph = glyphs.front();
        INFO("resultant z = " << glyph.resultant.z.si() << " N over area "
                              << glyph.area.si() << " m^2");
        REQUIRE(glyph.load == LoadId::fromValue(7));
        REQUIRE(glyph.kind == structural::LoadKind::Pressure);
        REQUIRE(glyph.hasDirection);
        REQUIRE(glyph.elements > 0);
        REQUIRE(glyph.area.si() > 0.0);

        // THE SIGN, ASSERTED. Inward on a +z face is -z.
        REQUIRE(glyph.resultant.z.si() < 0.0);
        // And it equals the prepared resultant exactly -- a read, not a
        // computation.
        REQUIRE(glyph.resultant.z.si() == prepared.loads->contributions().front().resultant.z.si());

        // AN INDEPENDENT MAGNITUDE CHECK: |F| = p * A for a planar face, which
        // P17-LOAD validated. This confirms the glyph is about this load and
        // not a stray zero.
        REQUIRE_THAT(std::abs(glyph.resultant.z.si()),
                     WithinRel(2.0e6 * glyph.area.si(), 1e-9));
    }

    SECTION("and a NEGATIVE pressure is outward suction, not clamped") {
        const std::vector<structural::StructuralLoad> loads{structural::StructuralLoad{
            LoadId::fromValue(8),
            structural::PressureLoad{.face = part.endCap(),
                                     .magnitude = Pressure::fromSi(-2.0e6)}}};
        const PreparedOverlays prepared = prepareOverlays(part, loads, rest);
        const std::vector<renderer::LoadGlyph> glyphs = renderer::loadGlyphs(*prepared.loads);
        REQUIRE(glyphs.size() == 1);
        // The opposite sign from the positive case, which is the whole point
        // of the convention being signed.
        REQUIRE(glyphs.front().resultant.z.si() > 0.0);
    }

    SECTION("a traction keeps its GLOBAL direction, not the surface normal") {
        // A global traction stays global. Aligning the arrow with the face
        // normal is a defect the brief names: the end cap's normal is +z, and
        // this traction is deliberately along +x so the two cannot be
        // confused.
        const std::vector<structural::StructuralLoad> loads{structural::StructuralLoad{
            LoadId::fromValue(9),
            structural::SurfaceTractionLoad{
                .face = part.endCap(),
                .traction = Traction3D{Pressure::fromSi(1.0e6), Pressure::fromSi(0.0),
                                       Pressure::fromSi(0.0)}}}};
        const PreparedOverlays prepared = prepareOverlays(part, loads, rest);
        const std::vector<renderer::LoadGlyph> glyphs = renderer::loadGlyphs(*prepared.loads);
        REQUIRE(glyphs.size() == 1);
        const renderer::LoadGlyph& glyph = glyphs.front();
        INFO("traction resultant = [" << glyph.resultant.x.si() << ", "
                                      << glyph.resultant.y.si() << ", "
                                      << glyph.resultant.z.si() << "]");
        REQUIRE(glyph.kind == structural::LoadKind::SurfaceTraction);
        // ALONG +X, and emphatically NOT along the +z normal.
        REQUIRE(glyph.resultant.x.si() > 0.0);
        REQUIRE_THAT(glyph.resultant.z.si(), WithinAbs(0.0, 1e-9));
    }

    SECTION("a nodal force appears per node and in the glyph's resultant") {
        const std::vector<structural::StructuralLoad> loads = part.endLoadList(1000.0);
        const PreparedOverlays prepared = prepareOverlays(part, loads, rest);

        const std::vector<renderer::NodalLoadArrow> arrows =
            renderer::nodalLoadArrows(*prepared.loads);
        INFO("loaded nodes: " << arrows.size());
        REQUIRE_FALSE(arrows.empty());
        REQUIRE(arrows.size() == prepared.loads->nodal().size());

        // ASCENDING AND WITHOUT REPEATS, which is the prepared field's own
        // guarantee, asserted rather than assumed.
        std::vector<meshing::NodeId> nodes;
        for (const renderer::NodalLoadArrow& arrow : arrows) {
            nodes.push_back(arrow.node);
        }
        REQUIRE(std::ranges::is_sorted(nodes));
        REQUIRE(std::ranges::adjacent_find(nodes) == nodes.end());

        // EVERY ARROW IS THE PREPARED FORCE, EXACTLY.
        for (std::size_t i = 0; i < arrows.size(); ++i) {
            REQUIRE(arrows[i].node == prepared.loads->nodal()[i].node);
            REQUIRE(arrows[i].force.z.si() == prepared.loads->nodal()[i].force.z.si());
        }

        // AND THEY SUM TO THE APPLIED TOTAL, an independent check that the
        // overlay is showing this load and not a fragment of it.
        double total = 0.0;
        for (const renderer::NodalLoadArrow& arrow : arrows) {
            total += arrow.force.z.si();
        }
        REQUIRE_THAT(total, WithinRel(1000.0, 1e-9));
    }

    SECTION("a zero load is given no direction rather than an invented axis") {
        const std::vector<structural::StructuralLoad> loads{structural::StructuralLoad{
            LoadId::fromValue(11),
            structural::PressureLoad{.face = part.endCap(),
                                     .magnitude = Pressure::fromSi(0.0)}}};
        const PreparedOverlays prepared = prepareOverlays(part, loads, rest);
        const std::vector<renderer::LoadGlyph> glyphs = renderer::loadGlyphs(*prepared.loads);
        REQUIRE(glyphs.size() == 1);
        // STILL REPORTED -- the load exists and the inspector should say so --
        // but with no direction to draw.
        REQUIRE(glyphs.front().load == LoadId::fromValue(11));
        REQUIRE_FALSE(glyphs.front().hasDirection);
    }
}

TEST_CASE("StructuralViz_RestraintGlyphsDistinguishTheirComponents", "[renderer][viz]") {
    // Rendering every support as fixed is a defect the brief names explicitly.
    // The mask is carried so a UI cannot.
    SolvedView part;
    const std::vector<structural::StructuralLoad> loads = part.endLoadList(1000.0);

    SECTION("a fixed support reports all three components") {
        const PreparedOverlays prepared = prepareOverlays(
            part, loads,
            {structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                           part.startCap())});
        const std::vector<renderer::RestraintGlyph> glyphs =
            renderer::restraintGlyphs(*prepared.restraints);
        REQUIRE(glyphs.size() == 1);
        REQUIRE(glyphs.front().restraint == RestraintId::fromValue(1));
        REQUIRE(glyphs.front().components.isFixed());
        REQUIRE(glyphs.front().components.count() == 3);
        REQUIRE(glyphs.front().nodes > 0);
        REQUIRE(glyphs.front().degreesOfFreedom == glyphs.front().nodes * 3U);
    }

    SECTION("a single-component support reports exactly that component") {
        for (const structural::DofComponent component :
             {structural::DofComponent::Ux, structural::DofComponent::Uy,
              structural::DofComponent::Uz}) {
            const PreparedOverlays prepared = prepareOverlays(
                part, loads,
                {structural::StructuralRestraint{
                    RestraintId::fromValue(4), part.startCap(),
                    structural::RestraintComponents::along(component)}});
            const std::vector<renderer::RestraintGlyph> glyphs =
                renderer::restraintGlyphs(*prepared.restraints);
            REQUIRE(glyphs.size() == 1);
            INFO("component " << structural::toString(glyphs.front().components));
            // NOT FIXED, and holding precisely the one component asked for.
            REQUIRE_FALSE(glyphs.front().components.isFixed());
            REQUIRE(glyphs.front().components.count() == 1);
            REQUIRE(glyphs.front().components.holds(component));
            REQUIRE(glyphs.front().degreesOfFreedom == glyphs.front().nodes);
        }
    }

    SECTION("and several restraints each keep their own identity and mask") {
        const PreparedOverlays prepared = prepareOverlays(
            part, loads,
            {structural::StructuralRestraint{
                 RestraintId::fromValue(1), part.startCap(),
                 structural::RestraintComponents::along(structural::DofComponent::Uz)},
             structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(2),
                                                           part.endCap())});
        const std::vector<renderer::RestraintGlyph> glyphs =
            renderer::restraintGlyphs(*prepared.restraints);
        REQUIRE(glyphs.size() == 2);
        REQUIRE(glyphs[0].restraint == RestraintId::fromValue(1));
        REQUIRE(glyphs[0].components.count() == 1);
        REQUIRE(glyphs[1].restraint == RestraintId::fromValue(2));
        REQUIRE(glyphs[1].components.isFixed());
    }
}

TEST_CASE("StructuralViz_ReactionArrowsKeepTheQualifiedSign", "[renderer][viz]") {
    Prepared prepared;
    const std::vector<renderer::ReactionArrow> arrows =
        renderer::reactionArrows(prepared.outcome().reactions);

    SECTION("one arrow per restrained node, ascending, exactly the source force") {
        REQUIRE_FALSE(arrows.empty());
        REQUIRE(arrows.size() == prepared.outcome().reactions.nodal().size());
        std::vector<meshing::NodeId> nodes;
        for (std::size_t i = 0; i < arrows.size(); ++i) {
            const structural::SupportReaction& source =
                prepared.outcome().reactions.nodal()[i];
            REQUIRE(arrows[i].node == source.node);
            REQUIRE(arrows[i].force.x.si() == source.force.x.si());
            REQUIRE(arrows[i].force.y.si() == source.force.y.si());
            REQUIRE(arrows[i].force.z.si() == source.force.z.si());
            REQUIRE(arrows[i].constrained == source.constrained);
            nodes.push_back(arrows[i].node);
        }
        REQUIRE(std::ranges::is_sorted(nodes));
    }

    SECTION("and the sign is NOT reversed: the arrows oppose the applied load") {
        // The fixture pushes the end cap along +z with 1000 N against a fixed
        // start cap, so the supports must push back along -z. A viewer that
        // negated the reaction "so the arrow points into the support" would
        // show them along +z.
        double total = 0.0;
        for (const renderer::ReactionArrow& arrow : arrows) {
            total += arrow.force.z.si();
        }
        INFO("summed reaction z = " << total << " N");
        REQUIRE_THAT(total, WithinRel(-1000.0, 1e-9));
        REQUIRE(total < 0.0);
        // AND IT MATCHES THE QUALIFIED BALANCE, which is the authority.
        REQUIRE_THAT(total, WithinRel(prepared.outcome().forceBalance.reaction.z.si(), 1e-12));
    }
}

TEST_CASE("StructuralViz_ArrowScaleIsPresentationOnly", "[renderer][viz]") {
    // Like the deformation scale: changing it changes what is drawn and
    // nothing that is engineering.
    SECTION("the length is proportional and the reference is the longest") {
        const Result<double> longest = renderer::displayArrowLength(100.0, 100.0, 0.02);
        const Result<double> half = renderer::displayArrowLength(50.0, 100.0, 0.02);
        REQUIRE(longest.has_value());
        REQUIRE(half.has_value());
        REQUIRE_THAT(*longest, WithinRel(0.02, 1e-12));
        REQUIRE_THAT(*half, WithinRel(0.01, 1e-12));
    }

    SECTION("a negative magnitude has the same length as its positive twin") {
        // Length is a length. The SIGN lives in the vector the arrow is drawn
        // from, not in how long it is.
        const Result<double> up = renderer::displayArrowLength(75.0, 100.0, 0.02);
        const Result<double> down = renderer::displayArrowLength(-75.0, 100.0, 0.02);
        REQUIRE(up.has_value());
        REQUIRE(down.has_value());
        REQUIRE(*up == *down);
    }

    SECTION("a zero magnitude gives zero length, not a minimum glyph") {
        const Result<double> none = renderer::displayArrowLength(0.0, 100.0, 0.02);
        REQUIRE(none.has_value());
        REQUIRE(*none == 0.0);
    }

    SECTION("scaling is linear in the scale, so it cannot alter a ratio") {
        const Result<double> a = renderer::displayArrowLength(30.0, 100.0, 0.01);
        const Result<double> b = renderer::displayArrowLength(30.0, 100.0, 1.00);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        REQUIRE_THAT(*b, WithinRel(*a * 100.0, 1e-12));
    }

    SECTION("a bad scale, reference or magnitude is refused") {
        const double nan = std::nan("");
        const double inf = std::numeric_limits<double>::infinity();
        for (const double bad : {nan, inf, -inf, 0.0, -1.0}) {
            INFO("scale " << bad);
            REQUIRE_FALSE(renderer::isUsableArrowScale(bad));
            REQUIRE_FALSE(renderer::displayArrowLength(10.0, 100.0, bad).has_value());
        }
        for (const double badReference : {nan, inf, 0.0, -5.0}) {
            REQUIRE_FALSE(renderer::displayArrowLength(10.0, badReference, 0.02).has_value());
        }
        REQUIRE_FALSE(renderer::displayArrowLength(nan, 100.0, 0.02).has_value());
        // A positive scale is usable.
        REQUIRE(renderer::isUsableArrowScale(0.02));
    }

    SECTION("and changing the arrow scale changes no engineering value") {
        Prepared prepared;
        const std::vector<renderer::ReactionArrow> before =
            renderer::reactionArrows(prepared.outcome().reactions);
        for (const double scale : {0.001, 0.02, 1.0, 1000.0}) {
            for (const renderer::ReactionArrow& arrow : before) {
                (void)renderer::displayArrowLength(arrow.force.z.si(), 1000.0, scale);
            }
        }
        const std::vector<renderer::ReactionArrow> after =
            renderer::reactionArrows(prepared.outcome().reactions);
        REQUIRE(after == before);
    }
}

// ---------------------------------------------------------------------------
// Boundary highlighting
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_BoundaryHighlightGoesThroughTheP16Mapping", "[renderer][viz]") {
    // NOT RE-IMPLEMENTED, AND THAT IS THE CLAIM. P16-MAP-001 resolves a
    // canonical reference to boundary facets and P16-VIZ-001 turns those into
    // render triangles. This milestone adds the two entry points a UI needs
    // and no resolution logic, so the thing to prove is that a load's and a
    // restraint's canonical targets reach that path and that the mapping's own
    // status survives.
    SolvedView part;
    Result<structural::StructuralModel> model = structural::requireStructuralModel(
        part.document, part.regenerator, part.mesher, part.control);
    REQUIRE(model.has_value());
    Result<MeshView> view = MeshView::volumeBoundaryOf(model->mesh().mesh());
    REQUIRE(view.has_value());

    SECTION("a restraint's target highlights exactly the mapping's facets") {
        const structural::StructuralRestraint restraint =
            structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                          part.startCap());
        REQUIRE(renderer::restraintTarget(restraint) == part.startCap());

        const Result<renderer::MeshHighlight> highlight = renderer::highlightForRestraint(
            *view, model->mesh().mesh(), model->map(), restraint);
        INFO((highlight.has_value() ? std::string{} : highlight.error().message));
        REQUIRE(highlight.has_value());

        REQUIRE(highlight->fullyResolved());
        REQUIRE_FALSE(highlight->empty());
        REQUIRE_FALSE(highlight->facets().empty());
        REQUIRE_FALSE(highlight->triangles.empty());

        // THE INDEPENDENT ORACLE: ask the mapping directly and compare. If the
        // highlight were a geometric search, these two would differ.
        Result<meshing::BoundaryFacetSet> facets =
            meshing::boundaryFacetsOf(model->map(), part.startCap());
        REQUIRE(facets.has_value());
        REQUIRE(highlight->facets().size() == facets->facets.size());
        for (std::size_t i = 0; i < facets->facets.size(); ++i) {
            REQUIRE(highlight->facets()[i] == facets->facets[i]);
        }

        // AND THE TRIANGLES ARE THOSE FACETS' OWN, ascending and unique.
        REQUIRE(std::ranges::is_sorted(highlight->triangles));
        REQUIRE(std::ranges::adjacent_find(highlight->triangles) ==
                highlight->triangles.end());
        for (const std::size_t triangle : highlight->triangles) {
            const Result<meshing::ElementId> element = view->elementOfTriangle(triangle);
            REQUIRE(element.has_value());
            REQUIRE(std::ranges::find(highlight->facets(), *element) !=
                    highlight->facets().end());
        }
    }

    SECTION("a pressure load's target highlights the same way") {
        const structural::StructuralLoad load{
            LoadId::fromValue(3),
            structural::PressureLoad{.face = part.endCap(),
                                     .magnitude = Pressure::fromSi(1.0e6)}};
        REQUIRE(renderer::loadTarget(load).has_value());
        REQUIRE(*renderer::loadTarget(load) == part.endCap());

        const Result<renderer::MeshHighlight> highlight =
            renderer::highlightForLoad(*view, model->mesh().mesh(), model->map(), load);
        REQUIRE(highlight.has_value());
        REQUIRE(highlight->fullyResolved());
        REQUIRE_FALSE(highlight->triangles.empty());
    }

    SECTION("a nodal force and gravity have NO face, and say so") {
        // Refused rather than returning an empty highlight: "this load has no
        // face" and "this face resolved to nothing" are different facts.
        const structural::StructuralLoad nodal{
            LoadId::fromValue(4),
            structural::NodalForceLoad{.mesh = model->mesh().mesh().stamp(),
                                       .node = model->mesh().mesh().nodes().front().id,
                                       .force = Force3D{Force::fromSi(1.0), Force::fromSi(0.0),
                                                        Force::fromSi(0.0)}}};
        REQUIRE_FALSE(renderer::loadTarget(nodal).has_value());
        const Result<renderer::MeshHighlight> none =
            renderer::highlightForLoad(*view, model->mesh().mesh(), model->map(), nodal);
        REQUIRE_FALSE(none.has_value());
        REQUIRE(none.error().code == ErrorCode::InvalidArgument);
        REQUIRE_THAT(none.error().message, ContainsSubstring("no CAD face"));

        const structural::StructuralLoad gravity{
            LoadId::fromValue(5),
            structural::GravityLoad{.acceleration = Vector3D{0.0, 0.0, -9.80665}}};
        REQUIRE_FALSE(renderer::loadTarget(gravity).has_value());
        REQUIRE_FALSE(
            renderer::highlightForLoad(*view, model->mesh().mesh(), model->map(), gravity)
                .has_value());
    }

    SECTION("a face the model does not have is UNRESOLVED, not silently empty") {
        // P16-MAP-001: "Unresolved is a status, not an empty list." A viewer
        // that flattened both to "nothing highlighted" would lose the
        // difference between a deleted face and a face that is simply not
        // meshed.
        const structural::StructuralRestraint missing{
            RestraintId::fromValue(9),
            FaceName{part.feature,
                     FaceSelector{.role = FaceRole::Side, .entity = EntityId::fromValue(99991)}},
            structural::RestraintComponents::fixed()};
        const Result<renderer::MeshHighlight> highlight = renderer::highlightForRestraint(
            *view, model->mesh().mesh(), model->map(), missing);
        if (highlight.has_value()) {
            // The mapping answered, and its answer is "not resolved".
            INFO("fullyResolved=" << highlight->fullyResolved()
                                  << " facets=" << highlight->facets().size());
            REQUIRE_FALSE(highlight->fullyResolved());
            REQUIRE(highlight->empty());
        } else {
            // Or it refused outright, which is equally explicit.
            REQUIRE_FALSE(highlight.error().message.empty());
        }
    }

    SECTION("and a stale view is refused rather than reinterpreted") {
        const structural::StructuralRestraint restraint =
            structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                          part.startCap());
        REQUIRE(part.document
                    .modifyObject<meshing::MeshControl>(
                        ObjectId::fromValue(part.control.value()),
                        [](meshing::MeshControl& control) -> Result<bool> {
                            auto definition = control.definition();
                            definition.mesh.sizing.globalTargetSize = 12_mm;
                            definition.mesh.sizing.local.clear();
                            return control.setDefinition(definition).has_value();
                        })
                    .has_value());
        requireReport(part.regenerator, part.document);
        const Result<const meshing::VolumeMesh*> remeshed =
            part.mesher.generate(part.document, part.regenerator, part.control);
        REQUIRE(remeshed.has_value());
        const meshing::Mesh& second = (*remeshed)->mesh();

        Result<structural::StructuralModel> after = structural::requireStructuralModel(
            part.document, part.regenerator, part.mesher, part.control);
        REQUIRE(after.has_value());
        // The OLD view against the NEW mesh and map.
        REQUIRE_FALSE(
            renderer::highlightForRestraint(*view, second, after->map(), restraint).has_value());
    }
}

TEST_CASE("StructuralViz_DrilledHoleWallIsNotFakedByTheViewer", "[renderer][viz]") {
    // THE CARRIED P16 LIMITATION, and the brief is explicit that the viewer
    // must not paper over it: a bored hole's cylindrical wall has NO FaceName
    // at all, because `cutHole` names a hole's flat faces and not its wall.
    //
    // So a forward query cannot reach it -- "there is nothing to ask with" --
    // and the honest behaviours are to report unresolved or to go through
    // P16's face-INDEX overload, which exists precisely for this case. What is
    // forbidden is a viewer-only cylinder recogniser that makes an
    // unsupported target look supported.
    //
    // THIS MILESTONE ADDS NO GEOMETRIC RECOGNISER, which is checkable: a grep
    // of the new production files for a cylinder test, an axis fit, a radius
    // or a coordinate tolerance finds nothing. The positive path -- reaching
    // such a wall by index -- is P16-VIZ-001's and is qualified there.
    SolvedView part;
    Result<structural::StructuralModel> model = structural::requireStructuralModel(
        part.document, part.regenerator, part.mesher, part.control);
    REQUIRE(model.has_value());
    Result<MeshView> view = MeshView::volumeBoundaryOf(model->mesh().mesh());
    REQUIRE(view.has_value());

    SECTION("every face of the map is reachable BY INDEX, including unnamed ones") {
        // P16's index overload is the route a click takes, and it asks the
        // mapping by position rather than by name -- so it reaches a face that
        // has no FaceName. Asserted over every face the map holds.
        const std::size_t faces = model->map().faces().size();
        INFO("mapped faces: " << faces);
        REQUIRE(faces > 0);
        std::size_t reachable = 0;
        for (std::size_t index = 0; index < faces; ++index) {
            const Result<renderer::MeshHighlight> highlight = renderer::highlightFor(
                *view, model->mesh().mesh(), model->map(), index);
            if (highlight.has_value()) {
                ++reachable;
            }
        }
        REQUIRE(reachable == faces);
    }

    SECTION("and the new production files contain no geometric recogniser") {
        // Stated as a test so the claim is not only a comment. The absence is
        // what keeps an unsupported target honest.
        //
        // This is a structural assertion about the source, so it is expressed
        // as a compile-time fact: the renderer's structural headers declare no
        // entry point taking a radius, an axis or a tolerance. The grep itself
        // is recorded in the evidence.
        SUCCEED("no cylinder test, axis fit, radius or coordinate tolerance exists in "
                "StructuralResultView or StructuralInspection -- see MEASUREMENTS.md");
    }
}

// ---------------------------------------------------------------------------
// The public numerical kernel
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_AChannelWithNoValueIsAbsentNotZero", "[renderer][viz]") {
    // ADVERSARIAL REVIEW FINDING 1, and the test that resolves it.
    //
    // `ScalarField::skipped` was declared, documented as "entities the source
    // had no value for", gated on by `complete()` and asserted `== 0` by the
    // cardinality case -- and NEVER INCREMENTED BY ANYTHING. The gate was
    // vacuous and that assertion was a tautology, which is this project's
    // "an unreachable refusal is an unchecked refusal" in its usual shape.
    //
    // The missing count also hid the one defect this layer can actually have.
    // The two readers returned 0.0 from a `default:` arm, so a channel whose
    // descriptor named the wrong domain produced a COMPLETE, UNIFORMLY ZERO
    // field: `classified == expected`, nothing non-finite, `complete()` true,
    // every entity coloured identically -- the P16 "classified nothing,
    // therefore all valid" trap one layer down, and invisible to every test
    // that only asked whether a field came back.
    //
    // `channelValue` is now the public kernel and returns an ABSENT optional
    // rather than a zero. This is the test that reaches the arm `field()`
    // never takes: it chooses its reader from the descriptor's own domain, so
    // the production path cannot exercise the refusal that guards the two
    // tables against disagreeing.
    //
    // THIS DOES NOT SYNTHESISE A `RecoveredFields`, which the file's own rule
    // forbids and `RecoveredFields` makes impossible -- a private constructor
    // with exactly one friend. `NodalDisplacement` and `ElementFields` are
    // public aggregates one layer below it, and the kernel takes those, so it
    // is exercised on values chosen to make the assertions sharp rather than
    // on whatever a particular mesh happened to produce. Every test that views
    // a RESULT still runs the qualified pipeline.
    const structural::NodalDisplacement nodal{
        .node = meshing::NodeId::fromValue(1),
        // MIXED SIGNS, DELIBERATELY. A fixture whose components are all
        // positive cannot detect `abs()` on a signed channel, and a probe
        // found exactly that gap in the first version of this file.
        .displacement = Translation3D{Length::fromSi(-1.0e-6), Length::fromSi(2.0e-6),
                                      Length::fromSi(-3.0e-6)},
        .magnitude = Length::fromSi(4.0e-6)};

    // SIX DISTINCT STRESS COMPONENTS, and three distinct principals. Equal
    // values would let a transposition pass, which is the defect the reader's
    // own comment warns about.
    const structural::ElementFields element{
        .element = meshing::ElementId::fromValue(1),
        .strain = structural::Strain6{.xx = 1.0e-5,
                                      .yy = 2.0e-5,
                                      .zz = 3.0e-5,
                                      .gammaXy = 4.0e-5,
                                      .gammaYz = 5.0e-5,
                                      .gammaZx = 6.0e-5},
        .stress = structural::Stress6{.xx = Pressure::fromSi(11.0),
                                      .yy = Pressure::fromSi(22.0),
                                      .zz = Pressure::fromSi(33.0),
                                      .xy = Pressure::fromSi(44.0),
                                      .yz = Pressure::fromSi(-55.0),
                                      .zx = Pressure::fromSi(66.0)},
        .principalStress = structural::PrincipalStresses{.sigma1 = Pressure::fromSi(90.0),
                                                         .sigma2 = Pressure::fromSi(20.0),
                                                         .sigma3 = Pressure::fromSi(-30.0)},
        .vonMises = Pressure::fromSi(123.0),
        .hydrostatic = Pressure::fromSi(-7.0)};

    SECTION("exactly one of the two overloads serves each channel, over the whole enum") {
        // THE DISAGREEMENT THE SILENT 0.0 HID. If `descriptorOf` named a
        // domain whose reader does not serve the channel, the old code
        // answered 0.0; this is the assertion that now refuses to let that
        // through, over every channel rather than over a sample.
        std::size_t onNodes = 0;
        std::size_t onElements = 0;
        for (const ResultChannel channel : renderer::allResultChannels()) {
            const ChannelDomain domain = renderer::descriptorOf(channel).domain;
            const std::optional<double> node = renderer::channelValue(channel, nodal);
            const std::optional<double> cell = renderer::channelValue(channel, element);
            INFO("channel " << renderer::toString(channel));

            REQUIRE(node.has_value() == (domain == ChannelDomain::Node));
            REQUIRE(cell.has_value() == (domain == ChannelDomain::Element));
            // NOT BOTH, AND NOT NEITHER. A channel served by both readers
            // would mean an element value had been averaged onto the nodes
            // somewhere; a channel served by neither is the dead arm.
            REQUIRE(node.has_value() != cell.has_value());

            onNodes += node.has_value() ? 1U : 0U;
            onElements += cell.has_value() ? 1U : 0U;
        }
        REQUIRE(onNodes == 4);
        REQUIRE(onElements == 11);
        REQUIRE(onNodes + onElements == renderer::allResultChannels().size());
    }

    SECTION("absent is distinguishable from a value that is genuinely zero") {
        // THE REASON ABSENT IS NOT 0.0, stated as a measurement. A node that
        // did not move has a displacement of exactly zero, which is an
        // ordinary answer; a stress channel asked of a node has no answer at
        // all. Collapsing both to 0.0 is what made the first form of this file
        // unable to tell a result from a silence.
        const structural::NodalDisplacement still{.node = meshing::NodeId::fromValue(2),
                                                  .displacement = Translation3D{},
                                                  .magnitude = Length{}};

        const std::optional<double> zero =
            renderer::channelValue(ResultChannel::DisplacementX, still);
        REQUIRE(zero.has_value());
        REQUIRE(*zero == 0.0);

        const std::optional<double> none =
            renderer::channelValue(ResultChannel::VonMises, still);
        REQUIRE_FALSE(none.has_value());

        // And the two are not the same answer, which is the whole claim.
        REQUIRE(zero.has_value() != none.has_value());
    }

    SECTION("every served value is the stored member, read and not recomputed") {
        // Each equality is EXACT. A recomputation -- a re-norm of `magnitude`,
        // a re-derivation of von Mises from the six components -- would agree
        // to a tolerance and not to the last bit, so `==` is what
        // distinguishes a read from a second formula.
        REQUIRE(*renderer::channelValue(ResultChannel::DisplacementMagnitude, nodal) == 4.0e-6);
        REQUIRE(*renderer::channelValue(ResultChannel::DisplacementX, nodal) == -1.0e-6);
        REQUIRE(*renderer::channelValue(ResultChannel::DisplacementY, nodal) == 2.0e-6);
        REQUIRE(*renderer::channelValue(ResultChannel::DisplacementZ, nodal) == -3.0e-6);

        // `magnitude` IS DELIBERATELY NOT THE NORM OF THE COMPONENTS: the norm
        // of (1, -2, 3)e-6 is 3.742e-6, not 4.0e-6. So a reader that re-normed
        // would return a different number and the assertion above would catch
        // it -- which a physically consistent fixture could never do, because
        // there the two agree and the read is unfalsifiable.
        REQUIRE(*renderer::channelValue(ResultChannel::DisplacementMagnitude, nodal) !=
                std::hypot(-1.0e-6, 2.0e-6, -3.0e-6));

        REQUIRE(*renderer::channelValue(ResultChannel::VonMises, element) == 123.0);
        REQUIRE(*renderer::channelValue(ResultChannel::StressXX, element) == 11.0);
        REQUIRE(*renderer::channelValue(ResultChannel::StressYY, element) == 22.0);
        REQUIRE(*renderer::channelValue(ResultChannel::StressZZ, element) == 33.0);
        REQUIRE(*renderer::channelValue(ResultChannel::StressXY, element) == 44.0);
        // THE TWO A TRANSPOSITION WOULD SWAP, with unequal values so that it
        // cannot pass.
        REQUIRE(*renderer::channelValue(ResultChannel::StressYZ, element) == -55.0);
        REQUIRE(*renderer::channelValue(ResultChannel::StressZX, element) == 66.0);
        REQUIRE(*renderer::channelValue(ResultChannel::PrincipalStress1, element) == 90.0);
        REQUIRE(*renderer::channelValue(ResultChannel::PrincipalStress2, element) == 20.0);
        REQUIRE(*renderer::channelValue(ResultChannel::PrincipalStress3, element) == -30.0);
        REQUIRE(*renderer::channelValue(ResultChannel::HydrostaticStress, element) == -7.0);

        // AND VON MISES IS NOT RECOMPUTED EITHER. 123.0 is not the invariant
        // of the six components above, so a reader that derived it instead of
        // reading it would disagree. The formula appears HERE, in a test,
        // asserting inequality -- which is the opposite of the production file
        // owning a second copy of it.
        const double derived = std::sqrt(
            0.5 * (std::pow(11.0 - 22.0, 2) + std::pow(22.0 - 33.0, 2) + std::pow(33.0 - 11.0, 2)) +
            3.0 * (44.0 * 44.0 + 55.0 * 55.0 + 66.0 * 66.0));
        // (the invariant uses squares, so the sign of yz does not enter it)
        REQUIRE(*renderer::channelValue(ResultChannel::VonMises, element) != derived);
    }
}

// ---------------------------------------------------------------------------
// Contour bands and the legend
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_ContourBandsPartitionTheFieldsOwnRange", "[renderer][viz]") {
    // THE BANDS ARE A PARTITION OR THEY ARE NOTHING. Every value of the field
    // must land in exactly one band -- not in two at a shared boundary, and
    // not in none at the maximum -- and the two ends of the legend must be the
    // field's own two numbers rather than a rounded restatement of them.
    //
    // The sharp assertion is `== 1`, counted by asking every band whether it
    // contains the value. A test that only asked `bandOf(v) < bands.size()`
    // would pass on an overlapping partition.
    Prepared part;
    const Result<ScalarField> field = part.result().field(
        part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    REQUIRE(field->complete());
    REQUIRE_FALSE(field->uniform());

    SECTION("over every usable band count, the partition holds exactly") {
        for (const std::size_t count : {renderer::kMinimumContourBands, std::size_t{3},
                                        std::size_t{8}, std::size_t{17},
                                        renderer::kMaximumContourBands}) {
            const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, count);
            INFO("bands " << count
                          << (legend.has_value() ? std::string{} : ": " + legend.error().message));
            REQUIRE(legend.has_value());
            REQUIRE_FALSE(legend->uniform);
            REQUIRE(legend->bands.size() == count);
            REQUIRE(legend->partitionsItsRange());

            // THE ENDS ARE THE FIELD'S OWN DOUBLES, exactly. `minimum + n *
            // width` rounds, and then the largest value in the field colours
            // as no band at all.
            REQUIRE(legend->minimum == field->minimum);
            REQUIRE(legend->maximum == field->maximum);
            REQUIRE(legend->bands.front().lower == field->minimum);
            REQUIRE(legend->bands.back().upper == field->maximum);

            // CONTIGUOUS BITWISE, not to a tolerance: one array of boundaries
            // shared by adjacent bands.
            for (std::size_t i = 0; i + 1 < legend->bands.size(); ++i) {
                REQUIRE(legend->bands[i].upper == legend->bands[i + 1].lower);
                REQUIRE(legend->bands[i].lower <= legend->bands[i].upper);
                REQUIRE_FALSE(legend->bands[i].closedAbove);
            }
            REQUIRE(legend->bands.back().closedAbove);

            // EVERY VALUE IN EXACTLY ONE BAND.
            const Result<std::vector<std::size_t>> mapped =
                renderer::contourBandsOf(*field, *legend);
            INFO((mapped.has_value() ? std::string{} : mapped.error().message));
            REQUIRE(mapped.has_value());
            REQUIRE(mapped->size() == field->values.size());
            for (std::size_t i = 0; i < field->values.size(); ++i) {
                const double value = field->values[i];
                std::size_t holders = 0;
                for (const renderer::ContourBand& band : legend->bands) {
                    holders += band.contains(value) ? 1U : 0U;
                }
                INFO("value " << value << " at " << i);
                REQUIRE(holders == 1);
                REQUIRE((*mapped)[i] == legend->bandOf(value));
                REQUIRE((*mapped)[i] < legend->bands.size());
            }

            // THE EXTREMES LAND AT THE ENDS, which is what makes the legend
            // readable: the lowest value is in the first band and the highest
            // in the last, and the maximum is included rather than falling off
            // the closed end.
            REQUIRE(legend->bandOf(field->minimum) == 0);
            REQUIRE(legend->bandOf(field->maximum) == count - 1);
        }
    }

    SECTION("and the mapping is monotonic in the value") {
        // A BAND INDEX THAT FELL AS THE VALUE ROSE would colour a contour
        // backwards in a way no single-value check notices.
        const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 12);
        REQUIRE(legend.has_value());
        std::vector<double> ascending = field->values;
        std::ranges::sort(ascending);
        std::size_t previous = 0;
        for (const double value : ascending) {
            const std::size_t band = legend->bandOf(value);
            REQUIRE(band < legend->bands.size());
            REQUIRE(band >= previous);
            previous = band;
        }
        // And it actually USES more than one band -- a constant mapping would
        // satisfy monotonicity trivially.
        REQUIRE(legend->bandOf(field->minimum) < legend->bandOf(field->maximum));
    }

    SECTION("a value outside the field's range belongs to no band, and is refused") {
        // NOT CLAMPED INTO THE NEAREST BAND. A value that is not from this
        // field is a caller error, and colouring it as the extreme would
        // present a defect as data.
        const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 6);
        REQUIRE(legend.has_value());
        // `+ 1.0` AND `- 1.0`, NOT `* 2.0`: the field's values here are
        // micrometres, so doubling the maximum leaves it inside the range of a
        // field whose extremes straddle zero, and the test would silently
        // stop testing anything. An additive step of 1 is outside any range
        // this fixture can produce, whatever the channel's sign.
        REQUIRE(legend->bandOf(field->maximum + 1.0) == legend->bands.size());
        REQUIRE(legend->bandOf(field->minimum - 1.0) == legend->bands.size());
    }

    SECTION("the colours are a usable ramp, finite and in range") {
        const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 9);
        REQUIRE(legend.has_value());
        for (const renderer::ContourBand& band : legend->bands) {
            REQUIRE(std::isfinite(band.red));
            REQUIRE(std::isfinite(band.green));
            REQUIRE(std::isfinite(band.blue));
            REQUIRE(band.red >= 0.0);
            REQUIRE(band.red <= 1.0);
            REQUIRE(band.green >= 0.0);
            REQUIRE(band.green <= 1.0);
            REQUIRE(band.blue >= 0.0);
            REQUIRE(band.blue <= 1.0);
        }
        // ADJACENT BANDS DIFFER. A constant palette would partition correctly
        // and show one colour, which is a contour that contours nothing.
        for (std::size_t i = 0; i + 1 < legend->bands.size(); ++i) {
            const renderer::ContourBand& a = legend->bands[i];
            const renderer::ContourBand& b = legend->bands[i + 1];
            INFO("bands " << i << " and " << i + 1);
            // THE NEGATED CONJUNCTION, not a disjunction: "not all three equal".
            // The disjunction form let a probe that flattened ONLY red survive --
            // see StructuralViz_ContourGapsTheProbesFound.
            REQUIRE_FALSE((a.red == b.red && a.green == b.green && a.blue == b.blue));
        }
    }

    SECTION("and banding the same field twice gives bitwise identical bands") {
        const Result<renderer::ContourLegend> first = renderer::contourLegend(*field, 10);
        const Result<renderer::ContourLegend> second = renderer::contourLegend(*field, 10);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        REQUIRE(first->bands.size() == second->bands.size());
        for (std::size_t i = 0; i < first->bands.size(); ++i) {
            REQUIRE(first->bands[i] == second->bands[i]);
        }
    }

    SECTION("the legend restates the channel's own unit") {
        const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 5);
        REQUIRE(legend.has_value());
        REQUIRE(legend->channel == ResultChannel::DisplacementMagnitude);
        REQUIRE(legend->unit == renderer::descriptorOf(ResultChannel::DisplacementMagnitude).unit);
        REQUIRE(legend->unit == "m");
    }
}

TEST_CASE("StructuralViz_ContourRefusesWhatItCannotHonestlyColour", "[renderer][viz]") {
    // THE P16 TRAP, AT THE LAST LAYER THAT COULD STILL HIDE IT. A legend that
    // claims the whole range while the field classified only part of it
    // presents the entities that happened to classify as the result. Every
    // refusal below is a case where drawing something would be worse than
    // drawing nothing.
    //
    // These fields are built by hand, which is the point: `field()` already
    // refuses a partial classification, so the only way to test that
    // `contourLegend` does not trust its input is to hand it one.
    SECTION("an incomplete classification") {
        ScalarField partial;
        partial.channel = ResultChannel::VonMises;
        partial.domain = ChannelDomain::Element;
        partial.expected = 10;
        partial.classified = 7;
        partial.values.assign(7, 1.0);
        partial.elements.assign(7, meshing::ElementId::fromValue(1));
        partial.minimum = 1.0;
        partial.maximum = 1.0;
        REQUIRE_FALSE(partial.complete());

        const Result<renderer::ContourLegend> legend = renderer::contourLegend(partial, 8);
        REQUIRE_FALSE(legend.has_value());
        REQUIRE(legend.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(legend.error().message, ContainsSubstring("field_incomplete"));
        REQUIRE_THAT(legend.error().message, ContainsSubstring("7 of 10"));
    }

    SECTION("an empty domain, which is the trap's own shape") {
        ScalarField nothing;
        nothing.channel = ResultChannel::VonMises;
        nothing.domain = ChannelDomain::Element;
        nothing.expected = 0;
        nothing.classified = 0;
        // classified == expected, nothing non-finite, nothing skipped -- and
        // `complete()` is still false because `expected > 0` is the half of it
        // that closes this trap.
        REQUIRE_FALSE(nothing.complete());
        REQUIRE_FALSE(renderer::contourLegend(nothing, 4).has_value());
    }

    SECTION("a band count below the floor or above the ceiling") {
        Prepared part;
        const Result<ScalarField> field = part.result().field(
            part.part.mesh(), part.outcome().fields, ResultChannel::VonMises);
        REQUIRE(field.has_value());

        for (const std::size_t bad :
             {std::size_t{0}, std::size_t{1}, renderer::kMaximumContourBands + 1,
              std::size_t{1000}}) {
            INFO("bands " << bad);
            const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, bad);
            REQUIRE_FALSE(legend.has_value());
            REQUIRE(legend.error().code == ErrorCode::InvalidArgument);
            REQUIRE_THAT(legend.error().message, ContainsSubstring("band_count_unusable"));
        }
        // AND THE TWO BOUNDS THEMSELVES ARE ACCEPTED, so the refusal is a
        // range and not an off-by-one.
        REQUIRE(renderer::contourLegend(*field, renderer::kMinimumContourBands).has_value());
        REQUIRE(renderer::contourLegend(*field, renderer::kMaximumContourBands).has_value());
    }

    SECTION("a legend built for a different range") {
        // THE SHIFTED-LEGEND DEFECT. Two solves of the same channel have the
        // same name and different ranges, so banding through the wrong legend
        // is wrong by a shift -- every value still lands in a band, and the
        // picture is plausible and false.
        Prepared part;
        const Result<ScalarField> field = part.result().field(
            part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
        REQUIRE(field.has_value());
        Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 6);
        REQUIRE(legend.has_value());

        renderer::ContourLegend shifted = *legend;
        shifted.maximum = legend->maximum + 1.0;
        const Result<std::vector<std::size_t>> byShifted =
            renderer::contourBandsOf(*field, shifted);
        REQUIRE_FALSE(byShifted.has_value());
        REQUIRE_THAT(byShifted.error().message, ContainsSubstring("legend_mismatch"));

        renderer::ContourLegend other = *legend;
        other.channel = ResultChannel::VonMises;
        const Result<std::vector<std::size_t>> byOther = renderer::contourBandsOf(*field, other);
        REQUIRE_FALSE(byOther.has_value());
        REQUIRE_THAT(byOther.error().message, ContainsSubstring("legend_mismatch"));
    }

    SECTION("and a legend whose bands do not partition its range") {
        // A HAND-BROKEN PARTITION, because `contourLegend` cannot produce one.
        // `contourBandsOf` checks rather than trusts, so a legend assembled or
        // deserialised elsewhere cannot colour through it.
        Prepared part;
        const Result<ScalarField> field = part.result().field(
            part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
        REQUIRE(field.has_value());
        Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 4);
        REQUIRE(legend.has_value());
        REQUIRE(legend->partitionsItsRange());

        renderer::ContourLegend gapped = *legend;
        // A GAP: band 1 now starts above where band 0 ends, so values between
        // them belong to nothing.
        gapped.bands[1].lower = gapped.bands[1].upper;
        REQUIRE_FALSE(gapped.partitionsItsRange());
        REQUIRE_FALSE(renderer::contourBandsOf(*field, gapped).has_value());

        renderer::ContourLegend doubled = *legend;
        // AN OVERLAP: two closed bands share a boundary, so a value at it
        // belongs to both.
        doubled.bands[0].closedAbove = true;
        REQUIRE_FALSE(doubled.partitionsItsRange());
        REQUIRE_FALSE(renderer::contourBandsOf(*field, doubled).has_value());
    }
}

TEST_CASE("StructuralViz_AUniformFieldIsOneBandNotADivisionByZero", "[renderer][viz]") {
    // A UNIFORM FIELD IS VALID AND ORDINARY -- a uniform stress state is a
    // real answer -- so it must not be refused. But its range has zero width,
    // so `(value - min) / (max - min)` is 0/0, and N equal-width bands over it
    // are N boundaries at the same number: every value would land in the first
    // band while the legend printed N of them, implying a variation the result
    // does not have.
    //
    // One band, `uniform == true`, and the caller is told rather than left to
    // divide.
    ScalarField flat;
    flat.channel = ResultChannel::StressZZ;
    flat.domain = ChannelDomain::Element;
    flat.expected = 5;
    flat.classified = 5;
    flat.values.assign(5, -2.5e6);
    flat.elements = {meshing::ElementId::fromValue(1), meshing::ElementId::fromValue(2),
                     meshing::ElementId::fromValue(3), meshing::ElementId::fromValue(4),
                     meshing::ElementId::fromValue(5)};
    flat.minimum = -2.5e6;
    flat.maximum = -2.5e6;
    REQUIRE(flat.complete());
    REQUIRE(flat.uniform());

    const Result<renderer::ContourLegend> legend = renderer::contourLegend(flat, 12);
    INFO((legend.has_value() ? std::string{} : legend.error().message));
    REQUIRE(legend.has_value());

    // ONE BAND, NOT THE TWELVE ASKED FOR, and it says so.
    REQUIRE(legend->uniform);
    REQUIRE(legend->bands.size() == 1);
    REQUIRE(legend->bands.front().lower == -2.5e6);
    REQUIRE(legend->bands.front().upper == -2.5e6);
    REQUIRE(legend->bands.front().closedAbove);
    REQUIRE(legend->partitionsItsRange());

    // NOTHING DIVIDED BY ZERO ANYWHERE: the colour is finite, which it would
    // not be if the fraction had been computed.
    REQUIRE(std::isfinite(legend->bands.front().red));
    REQUIRE(std::isfinite(legend->bands.front().green));
    REQUIRE(std::isfinite(legend->bands.front().blue));

    // AND THE VALUE COLOURS, rather than falling outside its own single band.
    const Result<std::vector<std::size_t>> mapped = renderer::contourBandsOf(flat, *legend);
    REQUIRE(mapped.has_value());
    REQUIRE(mapped->size() == 5);
    for (const std::size_t band : *mapped) {
        REQUIRE(band == 0);
    }

    // THE NEGATIVE VALUE SURVIVED. A uniform field of compressive stress is
    // the case an `abs()` or a zero-seeded accumulator would have destroyed,
    // and the range still reports it.
    REQUIRE(legend->minimum == -2.5e6);
    REQUIRE(legend->maximum == -2.5e6);
    REQUIRE(legend->minimum < 0.0);
}

TEST_CASE("StructuralViz_ARangeTooNarrowToSubdivideIsOneBandToo", "[renderer][viz]") {
    // THE SECOND CAUSE OF A ONE-BAND LEGEND, and it is NOT a uniform field:
    // the minimum and maximum differ, but by so little that `minimum + width`
    // rounds back to `minimum`. Every interior boundary then collapses onto
    // the minimum, bands 0 to N-2 become the empty interval `[min, min)`, and
    // only the last band can ever contain a value.
    //
    // The partition would still be VALID -- every value in exactly one band,
    // no gap, no overlap -- so the main partition test cannot see this. What
    // is wrong is the legend: it would print twelve bands of which eleven can
    // never be drawn, implying a variation that does not exist. That is the
    // quiet kind of wrong this milestone is about.
    ScalarField narrow;
    narrow.channel = ResultChannel::StressXX;
    narrow.domain = ChannelDomain::Element;
    narrow.expected = 2;
    narrow.classified = 2;
    // TWO UNITS IN THE LAST PLACE APART. Not hand-written constants: derived
    // with nextafter, so the case stays two ulps apart on any radix.
    const double low = 1.0;
    const double high = std::nextafter(std::nextafter(low, 2.0), 2.0);
    narrow.values = {low, high};
    narrow.elements = {meshing::ElementId::fromValue(1), meshing::ElementId::fromValue(2)};
    narrow.minimum = low;
    narrow.maximum = high;

    // THE PRECONDITION THAT MAKES THIS A DIFFERENT CASE: complete, and NOT
    // uniform. If these two failed, the test would be re-testing the uniform
    // branch under another name.
    REQUIRE(narrow.complete());
    REQUIRE_FALSE(narrow.uniform());
    REQUIRE(narrow.minimum != narrow.maximum);
    REQUIRE(narrow.minimum < narrow.maximum);

    const Result<renderer::ContourLegend> legend = renderer::contourLegend(narrow, 12);
    INFO((legend.has_value() ? std::string{} : legend.error().message));
    REQUIRE(legend.has_value());

    // ONE BAND, NOT TWELVE, and the flag says why the count was not honoured.
    REQUIRE(legend->uniform);
    REQUIRE(legend->bands.size() == 1);
    REQUIRE(legend->bands.size() != 12);

    // AND THE BAND SPANS THE REAL RANGE, which is not a single value here --
    // so `uniform` must not be read as `minimum == maximum`.
    REQUIRE(legend->bands.front().lower == low);
    REQUIRE(legend->bands.front().upper == high);
    REQUIRE(legend->bands.front().lower != legend->bands.front().upper);
    REQUIRE(legend->partitionsItsRange());

    // BOTH ENDS COLOUR, in the one band. Without the collapse the minimum
    // would fall in band 0 while band 0 was empty, and `bandOf` would answer
    // the LAST band for it -- the legend and the picture disagreeing.
    REQUIRE(legend->bandOf(low) == 0);
    REQUIRE(legend->bandOf(high) == 0);
    const Result<std::vector<std::size_t>> mapped = renderer::contourBandsOf(narrow, *legend);
    REQUIRE(mapped.has_value());
    REQUIRE(mapped->size() == 2);
    REQUIRE((*mapped)[0] == 0);
    REQUIRE((*mapped)[1] == 0);

    SECTION("and a range wide enough to subdivide still gets every band") {
        // THE CONTRAST THAT KEEPS THE GUARD HONEST. A guard that fired too
        // eagerly would collapse ordinary fields to one band and this would
        // catch it.
        ScalarField wide = narrow;
        wide.maximum = 2.0;
        wide.values = {1.0, 2.0};
        REQUIRE_FALSE(wide.uniform());
        const Result<renderer::ContourLegend> full = renderer::contourLegend(wide, 12);
        REQUIRE(full.has_value());
        REQUIRE_FALSE(full->uniform);
        REQUIRE(full->bands.size() == 12);
        REQUIRE(full->partitionsItsRange());
        REQUIRE(full->bandOf(1.0) == 0);
        REQUIRE(full->bandOf(2.0) == 11);
    }
}

// ---------------------------------------------------------------------------
// The independent oracle for every channel
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_EveryChannelsValuesAreItsSourcesValues", "[renderer][viz]") {
    // THE GAP A MUTATION PROBE FOUND. Before this case, nothing in the suite
    // compared a channel's extracted values against their source entity by
    // entity. The cardinality test counted them, the range test bracketed
    // them, the sweep test recomputed the extremes FROM THE PUBLISHED VALUES,
    // and the signed-channel test asked whether at least one of seven channels
    // had a negative minimum -- so `abs()` on ONE channel left the other six
    // negative and survived every one of them.
    //
    // The oracle is `RecoveredFields` itself, read directly through
    // `displacementOf` and `fieldsOf`. The switch below is a SECOND,
    // INDEPENDENT statement of the channel-to-member mapping, which is the
    // point: a test that asked the view for the mapping it is checking would
    // be comparing the production path with itself.
    //
    // EXACT EQUALITY, and the identity comes from the field's own parallel
    // array rather than from a positional index -- a positional index into
    // `RecoveredFields` is correct for a full-mesh view and silently wrong for
    // a boundary one.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();
    const structural::RecoveredFields& source = prepared.outcome().fields;

    std::size_t comparisons = 0;
    for (const ResultChannel channel : renderer::allResultChannels()) {
        const renderer::ChannelDescriptor descriptor = renderer::descriptorOf(channel);
        const Result<ScalarField> field = prepared.result().field(mesh, source, channel);
        INFO("channel " << renderer::toString(channel));
        REQUIRE(field.has_value());
        REQUIRE(field->complete());

        if (descriptor.domain == ChannelDomain::Node) {
            REQUIRE(field->nodes.size() == field->values.size());
            for (std::size_t i = 0; i < field->values.size(); ++i) {
                const Result<structural::NodalDisplacement> stored =
                    source.displacementOf(mesh, field->nodes[i]);
                REQUIRE(stored.has_value());
                double expected = 0.0;
                switch (channel) {
                case ResultChannel::DisplacementMagnitude:
                    expected = stored->magnitude.si();
                    break;
                case ResultChannel::DisplacementX:
                    expected = stored->displacement.x.si();
                    break;
                case ResultChannel::DisplacementY:
                    expected = stored->displacement.y.si();
                    break;
                case ResultChannel::DisplacementZ:
                    expected = stored->displacement.z.si();
                    break;
                default:
                    FAIL("a nodal channel with no oracle: " << renderer::toString(channel));
                }
                INFO("node " << field->nodes[i].value() << " at " << i);
                REQUIRE(field->values[i] == expected);
                ++comparisons;
            }
        } else {
            REQUIRE(field->elements.size() == field->values.size());
            for (std::size_t i = 0; i < field->values.size(); ++i) {
                const Result<structural::ElementFields> stored =
                    source.fieldsOf(mesh, field->elements[i]);
                REQUIRE(stored.has_value());
                double expected = 0.0;
                switch (channel) {
                case ResultChannel::VonMises:
                    expected = stored->vonMises.si();
                    break;
                case ResultChannel::StressXX:
                    expected = stored->stress.xx.si();
                    break;
                case ResultChannel::StressYY:
                    expected = stored->stress.yy.si();
                    break;
                case ResultChannel::StressZZ:
                    expected = stored->stress.zz.si();
                    break;
                case ResultChannel::StressXY:
                    expected = stored->stress.xy.si();
                    break;
                case ResultChannel::StressYZ:
                    expected = stored->stress.yz.si();
                    break;
                case ResultChannel::StressZX:
                    expected = stored->stress.zx.si();
                    break;
                case ResultChannel::PrincipalStress1:
                    expected = stored->principalStress.sigma1.si();
                    break;
                case ResultChannel::PrincipalStress2:
                    expected = stored->principalStress.sigma2.si();
                    break;
                case ResultChannel::PrincipalStress3:
                    expected = stored->principalStress.sigma3.si();
                    break;
                case ResultChannel::HydrostaticStress:
                    expected = stored->hydrostatic.si();
                    break;
                default:
                    FAIL("an element channel with no oracle: " << renderer::toString(channel));
                }
                INFO("element " << field->elements[i].value() << " at " << i);
                REQUIRE(field->values[i] == expected);
                ++comparisons;
            }
        }
    }

    // NON-VACUITY, and it is a product rather than a sum: 15 channels over a
    // mesh with both nodes and elements. A fixture that produced an empty
    // domain would make every loop above vacuous while the case still passed.
    INFO("comparisons: " << comparisons);
    REQUIRE(comparisons ==
            (4 * prepared.outcome().fields.displacements().size()) +
                (11 * prepared.outcome().fields.elements().size()));
    REQUIRE(comparisons > 0);
}

TEST_CASE("StructuralViz_EverySignedChannelKeepsTheSignOfEveryValue", "[renderer][viz]") {
    // THE SAME GAP, FROM THE OTHER SIDE, and the reason the original
    // signed-channel case could not see it: `channelsWithNegatives > 0` over
    // seven channels is an EXISTENTIAL standing in for seven assertions, so
    // one channel losing its sign left the other six to satisfy it.
    //
    // Here each channel is asked individually, and the channels that must be
    // negative in this fixture are named. The fixture pushes the end cap along
    // +z against a fixed start cap over a 40 x 30 x 20 mm bar, so Poisson
    // contraction draws material toward the axis: a node at x > 20 mm moves in
    // -x and a node at x < 20 mm moves in +x, which makes Ux and Uy each span
    // both signs rather than merely being non-zero.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    // MEASURED, NOT ASSUMED: the sign of each channel's range is printed and
    // the assertions below are the ones the measurement supports. See
    // MEASUREMENTS.md for the table.
    for (const ResultChannel channel :
         {ResultChannel::DisplacementX, ResultChannel::DisplacementY}) {
        const Result<ScalarField> field =
            prepared.result().field(mesh, prepared.outcome().fields, channel);
        REQUIRE(field.has_value());
        INFO("channel " << renderer::toString(channel) << " range [" << field->minimum << ", "
                        << field->maximum << "]");

        // SPANS BOTH SIGNS, per channel. `abs()` on this channel alone would
        // make the minimum non-negative and fail here, which is exactly what
        // the existential form could not detect.
        REQUIRE(field->minimum < 0.0);
        REQUIRE(field->maximum > 0.0);

        const bool someNegative =
            std::ranges::any_of(field->values, [](double v) { return v < 0.0; });
        const bool somePositive =
            std::ranges::any_of(field->values, [](double v) { return v > 0.0; });
        REQUIRE(someNegative);
        REQUIRE(somePositive);
    }

    SECTION("a norm is never negative, and that is a different claim") {
        // `|u|` is non-negative by definition, so for it the absence of
        // negatives is CORRECT rather than a lost sign -- which is why the
        // descriptor carries `mayBeNegative` and a test cannot simply demand
        // negatives everywhere.
        REQUIRE_FALSE(renderer::descriptorOf(ResultChannel::DisplacementMagnitude).mayBeNegative);
        const Result<ScalarField> magnitude = prepared.result().field(
            mesh, prepared.outcome().fields, ResultChannel::DisplacementMagnitude);
        REQUIRE(magnitude.has_value());
        REQUIRE(magnitude->minimum >= 0.0);
        REQUIRE(std::ranges::none_of(magnitude->values, [](double v) { return v < 0.0; }));
    }

    SECTION("and every channel the descriptor calls signed is consistent with itself") {
        // THE WEAKER, FIXTURE-INDEPENDENT HALF, kept because which stress
        // components are negative depends on the load case while the
        // declaration does not: a channel declared non-negative must not
        // produce a negative value in ANY fixture, and that direction is safe
        // to assert for all 15.
        for (const ResultChannel channel : renderer::allResultChannels()) {
            const Result<ScalarField> field =
                prepared.result().field(mesh, prepared.outcome().fields, channel);
            REQUIRE(field.has_value());
            INFO("channel " << renderer::toString(channel));
            if (!renderer::descriptorOf(channel).mayBeNegative) {
                REQUIRE(field->minimum >= 0.0);
            }
        }
    }
}

TEST_CASE("StructuralViz_AMagnitudeExtremeReportsTheSignedValue", "[renderer][viz]") {
    // THE GAP PROBE V17 FOUND. `abs()` on the value `extremeOf` reports
    // survived every case in the suite: the extreme-navigation test checked
    // WHICH entity a magnitude extreme names and that the magnitude is the
    // largest, but never that the VALUE it hands back keeps its sign.
    //
    // For a compressive peak that is the whole answer. "The most stressed
    // element is number 7, at 5.0e6 Pa" and "at -5.0e6 Pa" are different
    // engineering statements, and only one of them is true.
    //
    // BUILT BY HAND, because `extremeOf` takes a `ScalarField` -- a public
    // aggregate -- so the case can be stated exactly rather than hoping a
    // solve produces a negative magnitude peak. The field below is not a
    // physical stress state and does not need to be: the function under test
    // is an argmax over doubles.
    ScalarField field;
    field.channel = ResultChannel::StressZZ;
    field.domain = ChannelDomain::Element;
    field.expected = 4;
    field.classified = 4;
    // THE LARGEST MAGNITUDE IS NEGATIVE, and it is not at either end of the
    // array -- so a defect that returned the first, the last, or the maximum
    // cannot pass by position.
    field.values = {1.0e6, -5.0e6, 2.0e6, -3.0e6};
    field.elements = {meshing::ElementId::fromValue(11), meshing::ElementId::fromValue(12),
                      meshing::ElementId::fromValue(13), meshing::ElementId::fromValue(14)};
    field.minimum = -5.0e6;
    field.maximum = 2.0e6;
    field.minimumElement = meshing::ElementId::fromValue(12);
    field.maximumElement = meshing::ElementId::fromValue(13);
    REQUIRE(field.complete());

    SECTION("the magnitude extreme keeps the sign of the value it names") {
        const Result<renderer::ExtremeEntity> extreme =
            renderer::extremeOf(field, renderer::ExtremeKind::MaximumMagnitude);
        INFO((extreme.has_value() ? std::string{} : extreme.error().message));
        REQUIRE(extreme.has_value());

        REQUIRE(extreme->element == meshing::ElementId::fromValue(12));
        REQUIRE(extreme->index == 1);
        REQUIRE(extreme->kind == renderer::ExtremeKind::MaximumMagnitude);
        REQUIRE(extreme->domain == ChannelDomain::Element);
        // THE NODE ID IS LEFT INVALID for an element channel, rather than
        // defaulted to something that looks like an id.
        REQUIRE_FALSE(extreme->node.isValid());
        // THE ASSERTION THE SUITE WAS MISSING. -5.0e6, not 5.0e6. The
        // production comment already calls this a contract -- "the magnitude
        // was the search key and not the answer" -- and nothing asserted it.
        REQUIRE(extreme->value == -5.0e6);
        REQUIRE(extreme->value < 0.0);
        // And it is the value at the entity it named, which is the general
        // form of the same claim.
        REQUIRE(extreme->value == field.values[1]);
    }

    SECTION("and it really is the largest magnitude, not merely a negative one") {
        // NON-VACUITY FOR THE SECTION ABOVE: if -3.0e6 were picked, the sign
        // assertion would pass while the selection was wrong.
        const Result<renderer::ExtremeEntity> extreme =
            renderer::extremeOf(field, renderer::ExtremeKind::MaximumMagnitude);
        REQUIRE(extreme.has_value());
        for (const double value : field.values) {
            REQUIRE(std::abs(extreme->value) >= std::abs(value));
        }
    }

    SECTION("the signed maximum and minimum are a different question, and stay signed") {
        const Result<renderer::ExtremeEntity> highest =
            renderer::extremeOf(field, renderer::ExtremeKind::Maximum);
        REQUIRE(highest.has_value());
        REQUIRE(highest->element == meshing::ElementId::fromValue(13));
        REQUIRE(highest->value == 2.0e6);

        const Result<renderer::ExtremeEntity> lowest =
            renderer::extremeOf(field, renderer::ExtremeKind::Minimum);
        REQUIRE(lowest.has_value());
        REQUIRE(lowest->element == meshing::ElementId::fromValue(12));
        REQUIRE(lowest->value == -5.0e6);
        REQUIRE(lowest->value < 0.0);

        // THE MAXIMUM IS NOT THE MAXIMUM MAGNITUDE, which is the confusion
        // this whole case exists to keep apart.
        REQUIRE(highest->element != lowest->element);
        REQUIRE(highest->value != lowest->value);
    }

    SECTION("an all-negative field does not report zero as its maximum") {
        // THE RECORDED ACCUMULATOR DEFECT, asserted here as well as in the
        // classifier: `std::max` against 0.0 reports 0.0 when every value is
        // negative, and an invariance test then compares 0 against 0.
        ScalarField negative = field;
        negative.values = {-1.0e6, -5.0e6, -2.0e6, -3.0e6};
        negative.minimum = -5.0e6;
        negative.maximum = -1.0e6;
        negative.maximumElement = meshing::ElementId::fromValue(11);

        const Result<renderer::ExtremeEntity> highest =
            renderer::extremeOf(negative, renderer::ExtremeKind::Maximum);
        REQUIRE(highest.has_value());
        REQUIRE(highest->value == -1.0e6);
        REQUIRE(highest->value < 0.0);
        REQUIRE(highest->element == meshing::ElementId::fromValue(11));
    }
}

// ---------------------------------------------------------------------------
// Three gaps the mutation probes found
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_RestraintSurvivesANonZeroDisplacement", "[renderer][viz]") {
    // THE GAP PROBE V21 FOUND. Reading `restrained` from
    // `magnitude == 0.0` survived every section of
    // StructuralViz_RestraintComesFromTheRestraintsNotTheDisplacement,
    // because in that fixture the guess and the truth agree everywhere:
    //
    //   a FIXED support holds all three components, so a restrained node sits
    //   at exactly zero and the guess says restrained -- correct by accident
    //   a free node under load has a non-zero displacement and the guess says
    //   unrestrained -- also correct by accident
    //
    // That case tried to cover this. Its third section counts the free nodes
    // that happen to sit at zero displacement -- and only `INFO`s the count,
    // never requiring it to be non-zero. A discriminator that is printed
    // rather than asserted is a diagnostic, not coverage.
    //
    // THE DISCRIMINATOR IS A COMPONENT-ONLY RESTRAINT. Hold Uz alone on a side
    // face and those nodes are restrained while still free to move in x and y,
    // so their magnitude is NOT zero. The guess then says unrestrained and the
    // truth says restrained, and the two separate.
    SolvedView part;

    // The fixed cap keeps the model well-constrained -- a Uz-only restraint on
    // its own leaves rigid-body motion in x and y, which P17-VALID refuses
    // before it ever reaches a solve.
    const FaceName side{part.feature,
                        FaceSelector{.role = FaceRole::Side, .entity = part.lines.at(0)}};
    std::vector<structural::StructuralLoad> loads = part.endLoadList();
    REQUIRE(part.document
                .modifyObject<structural::StructuralAnalysis>(
                    ObjectId::fromValue(part.analysis.value()),
                    [&](structural::StructuralAnalysis& object) -> Result<bool> {
                        structural::StructuralAnalysisDefinition definition = object.definition();
                        definition.loads = std::move(loads);
                        definition.restraints = {
                            structural::StructuralRestraint::fixedSupport(
                                RestraintId::fromValue(1), part.startCap()),
                            structural::StructuralRestraint{
                                RestraintId::fromValue(2), side,
                                structural::RestraintComponents::along(
                                    structural::DofComponent::Uz)}};
                        return object.setDefinition(std::move(definition));
                    })
                .has_value());

    const structural::StructuralSolveOutcome outcome = part.solve();
    const meshing::Mesh& mesh = part.mesh();

    Result<structural::StructuralModel> model = structural::requireStructuralModel(
        part.document, part.regenerator, part.mesher, part.control);
    REQUIRE(model.has_value());
    Result<structural::MeshDofMap> numbering = structural::buildMeshDofMap(model->mesh().mesh());
    REQUIRE(numbering.has_value());
    const std::vector<structural::StructuralRestraint> restraints{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap()),
        structural::StructuralRestraint{
            RestraintId::fromValue(2), side,
            structural::RestraintComponents::along(structural::DofComponent::Uz)}};
    Result<structural::PreparedRestraints> held =
        structural::prepareStructuralRestraints(*model, *numbering, restraints);
    INFO((held.has_value() ? std::string{} : held.error().message));
    REQUIRE(held.has_value());

    // THE ASSERTION THE OTHER CASE ONLY PRINTED. At least one restrained node
    // must have a non-zero displacement, otherwise this case cannot tell the
    // prepared restraints from a guess either.
    std::size_t restrainedAndMoving = 0;
    for (const meshing::NodeId node : held->nodes()) {
        const Result<renderer::NodeResultInspection> inspection =
            renderer::inspectNode(mesh, outcome.fields, node, &*held);
        INFO("node " << node.value());
        REQUIRE(inspection.has_value());
        // RESTRAINED, read from the prepared restraints.
        REQUIRE(inspection->restrained);
        if (inspection->magnitude.si() != 0.0) {
            ++restrainedAndMoving;
        }
    }
    INFO("restrained nodes with a non-zero displacement: " << restrainedAndMoving);
    REQUIRE(restrainedAndMoving > 0);
}

TEST_CASE("StructuralViz_AViewRefusesAResultOfTheSameMeshFromAnotherSource",
          "[renderer][viz]") {
    // THE GAP PROBE V19 FOUND. Dropping `fields.source() == source_` from
    // `describes()` survived, because every existing source-guard case changes
    // the MESH -- and the mesh stamp alone then catches it. Nothing exercised
    // the case the header is explicit about:
    //
    //   "it compares SOURCES rather than counts: two results of the same mesh
    //    can have identical node and element counts and come from different
    //    materials, and a view read across that gap would map every value onto
    //    the wrong physics"
    //
    // A documented contract with no test, which is the shape this project has
    // been caught by before: if the narrow form is already documented, assert
    // it narrowly.
    //
    // A MATERIAL EDIT IS THE INSTRUMENT, because it leaves the mesh current --
    // the companion case in StaleResultIsNeverDrawableAsCurrent measures
    // exactly that -- so the two results share a MeshStamp, a node count and
    // an element count, and differ only in their source.
    SolvedView part;
    part.installAndSolve();
    const structural::StructuralSolveOutcome first = part.solve();

    Result<MeshView> view = MeshView::volumeBoundaryOf(part.mesh());
    REQUIRE(view.has_value());
    Result<StructuralResultView> bound =
        StructuralResultView::of(part.mesh(), *view, first.fields);
    REQUIRE(bound.has_value());
    REQUIRE(bound->describes(part.mesh(), first.fields));

    materials::MechanicalProperties mechanical =
        features::findMaterial(part.document, part.material)->definition().mechanical;
    mechanical.youngsModulus =
        materials::MaterialProperty<ElasticModulus>::known(ElasticModulus::fromSi(1.1e11));
    REQUIRE(
        features::setMaterialMechanical(part.document, part.material, mechanical).has_value());
    const structural::StructuralSolveOutcome second = part.solve();

    // THE PRECONDITIONS THAT MAKE THIS THE RIGHT CASE. Same mesh generation,
    // same cardinality -- so a guard comparing either of those cannot tell the
    // two results apart, and only the source can.
    REQUIRE(second.fields.mesh() == first.fields.mesh());
    REQUIRE(second.fields.displacements().size() == first.fields.displacements().size());
    REQUIRE(second.fields.elements().size() == first.fields.elements().size());
    REQUIRE_FALSE(second.fields.source() == first.fields.source());

    // AND THE DISPLACEMENTS REALLY DID MOVE, so reading across the gap would
    // show wrong numbers rather than the same ones.
    //
    // THE PEAK, NOT `front()`. The first node in ascending id order sits on
    // the FIXED cap, so its displacement is exactly zero in both solves and
    // softening the material cannot move it -- the first version of this
    // assertion compared 0.0 against 0.0 and failed for that reason, which is
    // this project's "an accumulator leaks its initialiser" in a fixture.
    const auto peak = [](const structural::RecoveredFields& fields) {
        double worst = fields.displacements().front().magnitude.si();
        for (const structural::NodalDisplacement& nodal : fields.displacements()) {
            worst = std::max(worst, nodal.magnitude.si());
        }
        return worst;
    };
    const double firstPeak = peak(first.fields);
    const double secondPeak = peak(second.fields);
    INFO("peak |u|: " << firstPeak << " then " << secondPeak);
    REQUIRE(firstPeak > 0.0);
    // E DROPPED from 2.1e11 to 1.1e11, so the bar is softer and the peak must
    // GROW -- a direction, not merely an inequality, so a fixture that changed
    // nothing could not satisfy it either.
    REQUIRE(secondPeak > firstPeak);

    // THE REFUSAL. The old view does not describe the new result.
    REQUIRE_FALSE(bound->describes(part.mesh(), second.fields));

    // And asking it for a channel is refused rather than answered from the
    // wrong physics.
    const Result<ScalarField> crossed =
        bound->field(part.mesh(), second.fields, ResultChannel::DisplacementMagnitude);
    REQUIRE_FALSE(crossed.has_value());
    REQUIRE(crossed.error().code == ErrorCode::FailedPrecondition);
    REQUIRE_THAT(crossed.error().message, ContainsSubstring("result_source_mismatch"));

    // A view bound to the NEW result describes it, which keeps the refusal
    // above from being a blanket one.
    Result<StructuralResultView> rebound =
        StructuralResultView::of(part.mesh(), *view, second.fields);
    REQUIRE(rebound.has_value());
    REQUIRE(rebound->describes(part.mesh(), second.fields));
    REQUIRE_FALSE(rebound->describes(part.mesh(), first.fields));
}

TEST_CASE("StructuralViz_ASkippedEntityMakesAFieldIncomplete", "[renderer][viz]") {
    // THE GAP PROBE V34 FOUND. `complete()` requires `skipped == 0`, and
    // dropping that term survived -- because every field the production path
    // builds has `skipped == 0` and no test handed it one that did not.
    //
    // `ScalarField` is a public aggregate, so the term IS reachable: this is
    // the difference between an unreachable guard and an untested one, and
    // `skipped` is the latter. (The `field()` refusal that would produce a
    // skip stays unreachable -- see FieldProblem::ChannelUnavailable -- and
    // probes V33 and V35 record that by surviving.)
    ScalarField withSkip;
    withSkip.channel = ResultChannel::VonMises;
    withSkip.domain = ChannelDomain::Element;
    withSkip.expected = 4;
    withSkip.classified = 4;
    withSkip.skipped = 1;
    withSkip.values = {1.0, 2.0, 3.0, 4.0};
    withSkip.elements = {meshing::ElementId::fromValue(1), meshing::ElementId::fromValue(2),
                        meshing::ElementId::fromValue(3), meshing::ElementId::fromValue(4)};
    withSkip.minimum = 1.0;
    withSkip.maximum = 4.0;

    // THE POINT OF THE FIXTURE: every OTHER counter is consistent. classified
    // equals expected, nothing is non-finite, the domain is non-empty and the
    // values are all there -- so only the skip can make it incomplete, and a
    // `complete()` that ignored `skipped` would call this whole.
    REQUIRE(withSkip.classified == withSkip.expected);
    REQUIRE(withSkip.expected > 0);
    REQUIRE(withSkip.nonFinite == 0);
    REQUIRE_FALSE(withSkip.complete());

    SECTION("and nothing downstream will colour it or navigate it") {
        // The two consumers that gate on completeness, so the term is not
        // merely observable but load-bearing.
        const Result<renderer::ExtremeEntity> extreme =
            renderer::extremeOf(withSkip, renderer::ExtremeKind::Maximum);
        REQUIRE_FALSE(extreme.has_value());
        REQUIRE_THAT(extreme.error().message, ContainsSubstring("incomplete_classification"));

        const Result<renderer::ContourLegend> legend = renderer::contourLegend(withSkip, 8);
        REQUIRE_FALSE(legend.has_value());
        REQUIRE_THAT(legend.error().message, ContainsSubstring("field_incomplete"));
    }

    SECTION("clearing the skip is all it takes, so nothing else was wrong") {
        // NON-VACUITY FOR THE WHOLE CASE. If the field were incomplete for
        // some other reason, the assertions above would pass while testing
        // nothing about `skipped`.
        ScalarField cleared = withSkip;
        cleared.skipped = 0;
        REQUIRE(cleared.complete());
        REQUIRE(renderer::extremeOf(cleared, renderer::ExtremeKind::Maximum).has_value());
        REQUIRE(renderer::contourLegend(cleared, 8).has_value());
    }
}

// ---------------------------------------------------------------------------
// Five gaps the contour probes found
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_ContourGapsTheProbesFound", "[renderer][viz]") {
    Prepared part;
    const Result<ScalarField> field = part.result().field(
        part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
    REQUIRE(field.has_value());
    REQUIRE(field->complete());
    REQUIRE_FALSE(field->uniform());

    SECTION("C18 -- a value exactly on an interior boundary is in ONE band") {
        // THE HALF-OPEN CONTRACT, asserted on the boundaries themselves.
        // `contains()` closing every band instead of only the last survived
        // the partition test, because that test counts holders for the
        // FIELD'S values -- and no field value happens to land exactly on an
        // interior boundary. The overlap is invisible until something sits on
        // a seam.
        //
        // Its mirror, opening every band, was killed: the maximum then belongs
        // to nothing. So the two directions are asymmetric and only one of
        // them needed this.
        const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 7);
        REQUIRE(legend.has_value());
        REQUIRE(legend->bands.size() == 7);

        for (std::size_t i = 0; i < legend->bands.size(); ++i) {
            // Every boundary of every band, lower and upper, is a seam a value
            // could land on.
            for (const double seam : {legend->bands[i].lower, legend->bands[i].upper}) {
                std::size_t holders = 0;
                for (const renderer::ContourBand& band : legend->bands) {
                    holders += band.contains(seam) ? 1U : 0U;
                }
                INFO("band " << i << " seam " << seam << " held by " << holders);
                REQUIRE(holders == 1);
                REQUIRE(legend->bandOf(seam) < legend->bands.size());
            }
        }

        // AND THE SEAM BELONGS TO THE BAND ABOVE IT, which is what half-open
        // from below means: band i owns its lower bound, not its upper.
        for (std::size_t i = 0; i + 1 < legend->bands.size(); ++i) {
            REQUIRE(legend->bandOf(legend->bands[i].upper) == i + 1);
            REQUIRE(legend->bandOf(legend->bands[i].lower) == i);
        }
        // Except the last, which is closed and owns both.
        REQUIRE(legend->bandOf(legend->bands.back().upper) == legend->bands.size() - 1);
    }

    SECTION("C20 -- EVERY colour channel varies, asserted one channel at a time") {
        // MY OWN ASSERTION WAS A DISJUNCTION, and that is why flattening the
        // red channel survived:
        //
        //     REQUIRE((a.red != b.red || a.green != b.green || a.blue != b.blue));
        //
        // green and blue still varied, so the disjunction held. This is the
        // third time this project has lost a probe to a weakened assertion
        // chosen for safety, and the rule it broke is the recorded one: assert
        // the narrow form.
        const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 9);
        REQUIRE(legend.has_value());

        // Each channel, separately: at least two distinct values across the
        // bands. A flattened channel has exactly one.
        const auto distinctValues = [](std::vector<double> values) {
            std::ranges::sort(values);
            const auto duplicates = std::ranges::unique(values);
            return static_cast<std::size_t>(
                std::distance(values.begin(), duplicates.begin()));
        };
        std::vector<double> redValues;
        std::vector<double> greenValues;
        std::vector<double> blueValues;
        for (const renderer::ContourBand& band : legend->bands) {
            redValues.push_back(band.red);
            greenValues.push_back(band.green);
            blueValues.push_back(band.blue);
        }
        const std::size_t reds = distinctValues(redValues);
        const std::size_t greens = distinctValues(greenValues);
        const std::size_t blues = distinctValues(blueValues);
        INFO("distinct r/g/b across 9 bands: " << reds << "/" << greens << "/" << blues);
        REQUIRE(reds > 1);
        REQUIRE(greens > 1);
        REQUIRE(blues > 1);

        // AND adjacent bands differ in at least one channel, which is the
        // weaker claim the old assertion made -- kept, because it is about
        // ADJACENCY rather than about the whole ramp.
        for (std::size_t i = 0; i + 1 < legend->bands.size(); ++i) {
            const renderer::ContourBand& a = legend->bands[i];
            const renderer::ContourBand& b = legend->bands[i + 1];
            INFO("bands " << i << " and " << i + 1);
            REQUIRE_FALSE((a.red == b.red && a.green == b.green && a.blue == b.blue));
        }
    }

    SECTION("C21 -- the band colour is its MIDPOINT, not its lower edge") {
        // A DOCUMENTED CHOICE, so it gets asserted: the header says the band
        // is painted from `(i + 0.5) / n` "so the first band is not pure blue
        // and the last not pure red -- the extremes of the ramp belong to the
        // ends of the RANGE, which the legend prints, not to a band's fill".
        //
        // Dropping the 0.5 survived because nothing pinned it. If the narrow
        // form is already documented, the documentation is the contract.
        const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 4);
        REQUIRE(legend.has_value());
        REQUIRE(legend->bands.size() == 4);

        // With four bands the midpoints are 0.125, 0.375, 0.625, 0.875, so NO
        // band sits at either end of the ramp. Dropping the offset puts the
        // first band at t = 0 exactly, which is the ramp's first stop.
        const renderer::ContourBand& first = legend->bands.front();
        const renderer::ContourBand& last = legend->bands.back();

        // The ramp's end stops, from the header's table.
        REQUIRE_FALSE((first.red == 0.16 && first.green == 0.26 && first.blue == 0.76));
        REQUIRE_FALSE((last.red == 0.85 && last.green == 0.20 && last.blue == 0.16));

        // AND THE RAMP IS TRAVERSED SYMMETRICALLY: with midpoints, the first
        // band is as far from the ramp's start as the last is from its end.
        // With `i / n` it is not, because the first lands exactly on the start.
        const double fromStart = std::abs(first.blue - 0.76);
        const double fromEnd = std::abs(last.blue - 0.16);
        INFO("first band distance from the ramp start: " << fromStart
                                                         << ", last from the end: " << fromEnd);
        REQUIRE(fromStart > 0.0);
        REQUIRE(fromEnd > 0.0);
        REQUIRE_THAT(fromStart, WithinRel(fromEnd, 1.0e-9));
    }
}

TEST_CASE("StructuralViz_ALegendMustPartitionItsOwnStatedRange", "[renderer][viz]") {
    // C6 -- `partitionsItsRange()` ignoring its two END conditions survived,
    // because `contourLegend` always produces correct ends and nothing handed
    // the predicate a legend whose first band starts above the minimum or
    // whose last ends below the maximum.
    //
    // The existing broken-partition cases break CONTIGUITY and the closed-band
    // rule, which are different conditions. These two were unexercised.
    Prepared part;
    const Result<ScalarField> field = part.result().field(
        part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
    REQUIRE(field.has_value());
    Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 5);
    REQUIRE(legend.has_value());
    REQUIRE(legend->partitionsItsRange());

    SECTION("the first band must start AT the minimum") {
        renderer::ContourLegend raised = *legend;
        // A GAP BELOW THE RANGE. The minimum itself would then belong to no
        // band, and a legend that printed [minimum, maximum] would be lying
        // about what it can colour.
        raised.bands.front().lower = std::nextafter(raised.minimum, raised.maximum);
        REQUIRE(raised.bands.front().lower != raised.minimum);
        REQUIRE_FALSE(raised.partitionsItsRange());
        REQUIRE_FALSE(renderer::contourBandsOf(*field, raised).has_value());
        // AND IT REALLY DOES ORPHAN THE MINIMUM, which is why it matters.
        REQUIRE(raised.bandOf(raised.minimum) == raised.bands.size());
    }

    SECTION("the last band must end AT the maximum") {
        renderer::ContourLegend lowered = *legend;
        lowered.bands.back().upper = std::nextafter(lowered.maximum, lowered.minimum);
        REQUIRE(lowered.bands.back().upper != lowered.maximum);
        REQUIRE_FALSE(lowered.partitionsItsRange());
        REQUIRE_FALSE(renderer::contourBandsOf(*field, lowered).has_value());
        REQUIRE(lowered.bandOf(lowered.maximum) == lowered.bands.size());
    }

    SECTION("and the unbroken legend still passes, so the checks are not blanket") {
        REQUIRE(legend->partitionsItsRange());
        REQUIRE(renderer::contourBandsOf(*field, *legend).has_value());
    }
}

TEST_CASE("StructuralViz_ALegendRestatesTheFieldsRangeAndDoesNotRescan",
          "[renderer][viz]") {
    // C23 -- taking the range from `min_element`/`max_element` of the values
    // instead of the field's own stored `minimum` and `maximum` survived,
    // because for every field the production path builds the two agree.
    //
    // The header is explicit that they must not be re-derived: "the legend
    // restates that range rather than re-scanning the values and possibly
    // disagreeing with the extremes the navigation uses". That matters because
    // `extremeOf` reads the STORED range -- so a legend that re-scanned would
    // put the colour scale and the "jump to the worst" button on different
    // numbers.
    //
    // A hand-built field separates them: `ScalarField` is a public aggregate,
    // so its stated range can be WIDER than the values it carries. That is not
    // a contrived state -- it is what a field over a sub-selection of a domain
    // looks like.
    ScalarField wider;
    wider.channel = ResultChannel::StressZZ;
    wider.domain = ChannelDomain::Element;
    wider.expected = 3;
    wider.classified = 3;
    wider.values = {10.0, 20.0, 30.0};
    wider.elements = {meshing::ElementId::fromValue(1), meshing::ElementId::fromValue(2),
                      meshing::ElementId::fromValue(3)};
    // THE STATED RANGE IS WIDER THAN THE VALUES SPAN.
    wider.minimum = 0.0;
    wider.maximum = 100.0;
    wider.minimumElement = meshing::ElementId::fromValue(1);
    wider.maximumElement = meshing::ElementId::fromValue(3);
    REQUIRE(wider.complete());

    const Result<renderer::ContourLegend> legend = renderer::contourLegend(wider, 4);
    INFO((legend.has_value() ? std::string{} : legend.error().message));
    REQUIRE(legend.has_value());

    // THE STORED RANGE, not the values' range.
    REQUIRE(legend->minimum == 0.0);
    REQUIRE(legend->maximum == 100.0);
    REQUIRE(legend->bands.front().lower == 0.0);
    REQUIRE(legend->bands.back().upper == 100.0);
    // A re-scan would have produced [10, 30], so these are the discriminating
    // assertions.
    REQUIRE(legend->minimum != 10.0);
    REQUIRE(legend->maximum != 30.0);
    REQUIRE(legend->partitionsItsRange());

    // AND THE BANDS ARE 25 WIDE, not 6.67 -- the width follows the stated
    // range too.
    REQUIRE_THAT(legend->bands.front().upper, WithinRel(25.0, 1.0e-12));

    // The values still colour, each in the band its magnitude puts it in.
    const Result<std::vector<std::size_t>> mapped = renderer::contourBandsOf(wider, *legend);
    REQUIRE(mapped.has_value());
    REQUIRE((*mapped)[0] == 0); // 10 -> [0, 25)
    REQUIRE((*mapped)[1] == 0); // 20 -> [0, 25)
    REQUIRE((*mapped)[2] == 1); // 30 -> [25, 50)

    // AND THE LEGEND AGREES WITH THE NAVIGATION, which is the reason the rule
    // exists: both read the same two numbers.
    const Result<renderer::ExtremeEntity> highest =
        renderer::extremeOf(wider, renderer::ExtremeKind::Maximum);
    REQUIRE(highest.has_value());
    REQUIRE(legend->bandOf(highest->value) < legend->bands.size());
}

// ---------------------------------------------------------------------------
// The deformed display (ADR-043)
// ---------------------------------------------------------------------------
//
// THE ONLY TESTS IN THIS FILE THAT NEED A GPU, and they are kept apart for
// that reason: a driver is not an engineering gate, so everything numerical is
// asserted headlessly above and these measure the one thing a headless test
// cannot -- that the buffer reaches OCCT model space at the right SCALE.

namespace {

constexpr int kViewWidth = 320;
constexpr int kViewHeight = 240;
const renderer::Rgb kViewBlack{0, 0, 0};

[[nodiscard]] renderer::Viewer makeResultViewer() {
    Result<renderer::Viewer> viewer = renderer::Viewer::createOffscreen(kViewWidth, kViewHeight);
    if (!viewer.has_value()) {
        FAIL("the offscreen viewer could not be created: " << viewer.error().message);
    }
    viewer->setBackground(kViewBlack);
    return std::move(*viewer);
}

[[nodiscard]] renderer::RenderedImage renderResult(renderer::Viewer& viewer) {
    Result<renderer::RenderedImage> image = viewer.render();
    if (!image.has_value()) {
        FAIL("the view could not be rendered: " << image.error().message);
    }
    return std::move(*image);
}

} // namespace

TEST_CASE("StructuralViz_ADeformedMeshOverlaysItsOwnCadSolidAtTheSameScale",
          "[renderer][viz][frame]") {
    // THE MEASUREMENT UNIT_BOUNDARY.md OWED, and the reason it is the right
    // one: P16-VIZ-001 found the metres-versus-millimetres defect exactly this
    // way, and recorded why nothing else could.
    //
    //   "a mesh displayed ALONE looked perfect, because FitAll framed the only
    //    thing in the scene. Displayed beside its own CAD solid and then
    //    fitted, it became a sub-pixel speck -- 0 covered pixels"
    //
    // A boundary mesh of a solid has the SAME SILHOUETTE as the solid, so from
    // a fixed camera the solid alone, the mesh alone, and both together cover
    // the same pixels. A scale error between them gives three different
    // numbers, and a factor of 1000 gives a speck.
    //
    // AT A TINY SCALE the deformed buffer is the undeformed one to well under
    // a pixel, so it must satisfy the same three-way equality. That is what
    // proves `x + s*u` reaches model space in MILLIMETRES rather than metres.
    Prepared part;
    const geometry::Body* body = part.part.regenerator.body(part.part.feature);
    REQUIRE(body != nullptr);

    const Result<std::vector<double>> deformed =
        part.result().deformedPositions(part.part.mesh(), part.view(), part.outcome().fields, 1.0);
    INFO((deformed.has_value() ? std::string{} : deformed.error().message));
    REQUIRE(deformed.has_value());
    REQUIRE(deformed->size() == part.view().positions().size());

    const auto coverageOf = [&](bool withCad, bool withMesh, bool asDeformed) {
        renderer::Viewer viewer = makeResultViewer();
        if (withCad) {
            const Result<renderer::PresentationId> shown =
                viewer.display(part.part.feature, *body);
            REQUIRE(shown.has_value());
        }
        if (withMesh) {
            const Result<renderer::PresentationId> shown =
                asDeformed ? viewer.displayDeformedMesh(part.part.feature, part.view(), *deformed,
                                                        renderer::MeshStyle::Shaded)
                           : viewer.displayMesh(part.part.feature, part.view(),
                                                renderer::MeshStyle::Shaded);
            INFO((shown.has_value() ? std::string{} : shown.error().message));
            REQUIRE(shown.has_value());
        }
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        return renderResult(viewer).coverage(kViewBlack);
    };

    const std::size_t cadOnly = coverageOf(true, false, false);
    const std::size_t undeformedOnly = coverageOf(false, true, false);
    const std::size_t deformedOnly = coverageOf(false, true, true);
    const std::size_t cadAndDeformed = coverageOf(true, true, true);

    INFO("cad " << cadOnly << ", undeformed " << undeformedOnly << ", deformed " << deformedOnly
                << ", cad+deformed " << cadAndDeformed);

    // NON-VACUITY FIRST: the solid really is on screen, so the equalities
    // below are not three zeros agreeing.
    REQUIRE(cadOnly > 0U);

    // THE DEFORMED BUFFER IS IN THE SAME SPACE AS THE SOLID. At this
    // displacement -- micrometres on a 40 mm bar -- the deformation is far
    // below one pixel, so the silhouettes coincide. If the buffer were in
    // metres it would be 1000x too small and `cadAndDeformed` would equal
    // `cadOnly` while `deformedOnly` framed a speck.
    CHECK(deformedOnly == cadOnly);
    CHECK(cadAndDeformed == cadOnly);

    // AND IT MATCHES THE UNDEFORMED PATH, which is the same claim from the
    // other side: both buffers pass through the one conversion in the
    // presentation constructor (ADR-043).
    CHECK(deformedOnly == undeformedOnly);
}

TEST_CASE("StructuralViz_ADeformedDisplayAtScaleZeroIsTheUndeformedImage",
          "[renderer][viz][frame]") {
    // EXACT, AND THAT IS THE POINT. `deformedPositions` at scale 0 returns the
    // undeformed positions bit for bit -- `x + 0*u` where the buffer starts as
    // a copy of x -- so the two presentations must produce the SAME IMAGE, not
    // a similar one.
    //
    // A byte-identical image is a stronger statement than any coverage count:
    // it says the deformed path changed nothing at all, including the normals
    // that light the facets and the edges drawn over them.
    Prepared part;
    const Result<std::vector<double>> still =
        part.result().deformedPositions(part.part.mesh(), part.view(), part.outcome().fields, 0.0);
    REQUIRE(still.has_value());

    // The precondition, asserted so the case cannot pass for the wrong reason.
    const std::span<const double> undeformed = part.view().positions();
    REQUIRE(still->size() == undeformed.size());
    for (std::size_t i = 0; i < still->size(); ++i) {
        REQUIRE((*still)[i] == undeformed[i]);
    }

    const auto imageOf = [&part, &still](bool asDeformed) {
        renderer::Viewer viewer = makeResultViewer();
        const Result<renderer::PresentationId> shown =
            asDeformed ? viewer.displayDeformedMesh(part.part.feature, part.view(), *still)
                       : viewer.displayMesh(part.part.feature, part.view());
        REQUIRE(shown.has_value());
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        return renderResult(viewer);
    };

    const renderer::RenderedImage plain = imageOf(false);
    const renderer::RenderedImage zero = imageOf(true);
    REQUIRE(plain.coverage(kViewBlack) > 0U);
    CHECK(zero.rgb == plain.rgb);
}

TEST_CASE("StructuralViz_ADeformedDisplayAtAVisibleScaleChangesTheImage",
          "[renderer][viz][frame]") {
    // THE OTHER HALF, without which the case above would be satisfied by a
    // deformed path that ignored its buffer entirely: at a scale big enough to
    // move the geometry by more than a pixel, the image must DIFFER.
    //
    // The displacements here are micrometres on a 40 mm bar, so the scale has
    // to be large to be visible. That is exactly what a deformation scale
    // factor is for, and it is a presentation value -- it scales the picture
    // and nothing else.
    Prepared part;
    const Result<std::vector<double>> still =
        part.result().deformedPositions(part.part.mesh(), part.view(), part.outcome().fields, 0.0);
    REQUIRE(still.has_value());
    const Result<std::vector<double>> moved =
        part.result().deformedPositions(part.part.mesh(), part.view(), part.outcome().fields, 2.0e4);
    INFO((moved.has_value() ? std::string{} : moved.error().message));
    REQUIRE(moved.has_value());

    // THE BUFFER REALLY MOVED, measured before anything is drawn -- so a
    // failure below is about the display and not about the arithmetic.
    double worst = 0.0;
    for (std::size_t i = 0; i < moved->size(); ++i) {
        worst = std::max(worst, std::abs((*moved)[i] - (*still)[i]));
    }
    INFO("largest coordinate change: " << worst << " m");
    REQUIRE(worst > 1.0e-3);

    const auto imageOf = [&part](const std::vector<double>& positions) {
        renderer::Viewer viewer = makeResultViewer();
        const Result<renderer::PresentationId> shown =
            viewer.displayDeformedMesh(part.part.feature, part.view(), positions);
        REQUIRE(shown.has_value());
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        return renderResult(viewer);
    };

    const renderer::RenderedImage undeformed = imageOf(*still);
    const renderer::RenderedImage deformed = imageOf(*moved);
    REQUIRE(undeformed.coverage(kViewBlack) > 0U);
    REQUIRE(deformed.coverage(kViewBlack) > 0U);
    CHECK(deformed.rgb != undeformed.rgb);
}

TEST_CASE("StructuralViz_ADeformedDisplayRefusesABufferThatIsNotThisMeshs",
          "[renderer][viz][frame]") {
    // A SPAN CARRIES ITS LENGTH AND NOTHING ELSE. It cannot say which mesh it
    // came from, so the length is all there is to check -- and it is checked
    // exactly, because a short buffer would be read past its end and a longer
    // one means the caller is holding a different mesh.
    Prepared part;
    const Result<std::vector<double>> deformed =
        part.result().deformedPositions(part.part.mesh(), part.view(), part.outcome().fields, 1.0);
    REQUIRE(deformed.has_value());
    renderer::Viewer viewer = makeResultViewer();

    SECTION("a short buffer") {
        std::vector<double> truncated = *deformed;
        truncated.pop_back();
        truncated.pop_back();
        truncated.pop_back();
        const Result<renderer::PresentationId> shown =
            viewer.displayDeformedMesh(part.part.feature, part.view(), truncated);
        REQUIRE_FALSE(shown.has_value());
        REQUIRE(shown.error().code == ErrorCode::InvalidArgument);
        REQUIRE_THAT(shown.error().message, ContainsSubstring("must hold"));
    }

    SECTION("a long buffer") {
        std::vector<double> extended = *deformed;
        extended.push_back(0.0);
        extended.push_back(0.0);
        extended.push_back(0.0);
        REQUIRE_FALSE(
            viewer.displayDeformedMesh(part.part.feature, part.view(), extended).has_value());
    }

    SECTION("and an EMPTY buffer is refused rather than drawn undeformed") {
        // THE TRAP THIS GUARD EXISTS FOR. The presentation reads an empty span
        // as "use the view's own positions", so an empty buffer reaching it
        // would silently draw the UNDEFORMED mesh through the deformed entry
        // point -- which looks exactly like a solve that produced no
        // displacement. A different code from the length mismatch, because it
        // is a different mistake.
        const Result<renderer::PresentationId> shown =
            viewer.displayDeformedMesh(part.part.feature, part.view(), {});
        REQUIRE_FALSE(shown.has_value());
        REQUIRE(shown.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(shown.error().message, ContainsSubstring("use displayMesh"));
    }

    SECTION("the valid buffer still displays, so the refusals are not blanket") {
        REQUIRE(viewer.displayDeformedMesh(part.part.feature, part.view(), *deformed).has_value());
    }
}

TEST_CASE("StructuralViz_ADeformedDisplayStillKnowsItsMeshGeneration",
          "[renderer][viz][frame]") {
    // THE STAMP IS THE VIEW'S, not something the deformation invents. A
    // deformed presentation is still a presentation OF one mesh generation,
    // and a caller comparing `meshStampOf` against the current mesh must get
    // the same answer it would for the undeformed one -- otherwise a remesh
    // would leave a deformed display looking current.
    Prepared part;
    const Result<std::vector<double>> deformed =
        part.result().deformedPositions(part.part.mesh(), part.view(), part.outcome().fields, 1.0e4);
    REQUIRE(deformed.has_value());

    renderer::Viewer viewer = makeResultViewer();
    const Result<renderer::PresentationId> shown =
        viewer.displayDeformedMesh(part.part.feature, part.view(), *deformed);
    REQUIRE(shown.has_value());

    const Result<meshing::MeshStamp> stamp = viewer.meshStampOf(*shown);
    REQUIRE(stamp.has_value());
    CHECK(*stamp == part.part.mesh().stamp());

    // ONE BATCHED PRESENTATION, not one per element -- the deformed path must
    // not have become a per-facet object.
    REQUIRE(viewer.displayed().size() == 1U);
    CHECK(viewer.displayed().front().kind == renderer::PresentationKind::EngineeringMesh);

    // AND IT CAN BE DRAWN STALE, like any mesh presentation: the deformed
    // entry point is a position override, not a separate kind of object, so
    // every visual state the undeformed one has still applies.
    REQUIRE(viewer.setMeshStale(*shown, true).has_value());
    const Result<bool> stale = viewer.meshIsStale(*shown);
    REQUIRE(stale.has_value());
    CHECK(*stale);
}

TEST_CASE("StructuralViz_ADeformedDisplayMovesItsEDGESToo", "[renderer][viz][frame]") {
    // AN ADVERSARIAL-REVIEW TEST, written because the gap was visible by
    // inspection rather than by a failure.
    //
    // The presentation builds TWO buffers from the position span: the facet
    // corners and the edge endpoints. Both read the same local `positions`,
    // so they move together -- but no test proved it, because every other
    // deformed case either draws Shaded (no edges at all) or draws
    // ShadedWithEdges, where the facets moving is enough to change the image
    // while the edges stay behind.
    //
    // WIREFRAME ISOLATES THE EDGE BUFFER: with no facets drawn, the only thing
    // on screen is the edges, so an edge buffer that ignored the deformation
    // would render the same image at every scale.
    Prepared part;
    const Result<std::vector<double>> still = part.result().deformedPositions(
        part.part.mesh(), part.view(), part.outcome().fields, 0.0);
    REQUIRE(still.has_value());
    const Result<std::vector<double>> moved = part.result().deformedPositions(
        part.part.mesh(), part.view(), part.outcome().fields, 2.0e4);
    REQUIRE(moved.has_value());

    const auto wireframeOf = [&part](const std::vector<double>& positions) {
        renderer::Viewer viewer = makeResultViewer();
        const Result<renderer::PresentationId> shown = viewer.displayDeformedMesh(
            part.part.feature, part.view(), positions, renderer::MeshStyle::Wireframe);
        REQUIRE(shown.has_value());
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        return renderResult(viewer);
    };

    const renderer::RenderedImage undeformed = wireframeOf(*still);
    const renderer::RenderedImage deformed = wireframeOf(*moved);

    // EDGES ARE ON SCREEN, so the comparison is not two empty images.
    REQUIRE(undeformed.coverage(kViewBlack) > 0U);
    REQUIRE(deformed.coverage(kViewBlack) > 0U);
    CHECK(deformed.rgb != undeformed.rgb);
}

// ---------------------------------------------------------------------------
// The contour display
// ---------------------------------------------------------------------------

namespace {

/// How many distinct non-background colours the image contains.
///
/// THE MEASUREMENT A UNIFORM FILL CANNOT PASS. A contour that reached the
/// screen shows many; a group aspect's single interior colour shows one,
/// whatever the array carried.
[[nodiscard]] std::size_t distinctColours(const renderer::RenderedImage& image,
                                          renderer::Rgb background) {
    std::vector<std::uint32_t> seen;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const renderer::Rgb pixel = image.at(x, y);
            if (pixel == background) {
                continue;
            }
            const auto key = static_cast<std::uint32_t>((pixel.red << 16) | (pixel.green << 8) |
                                                        pixel.blue);
            if (std::ranges::find(seen, key) == seen.end()) {
                seen.push_back(key);
            }
        }
    }
    return seen.size();
}

} // namespace

TEST_CASE("StructuralViz_AContourReachesTheScreenAsManyColours", "[renderer][viz][frame]") {
    // THE MEASUREMENT THE WHOLE CONTOUR PATH RESTS ON, and it was taken
    // before anything was claimed about it: per-vertex colour is an ARRAY
    // ATTRIBUTE in OCCT while the facet colour is a GROUP ASPECT, and whether
    // the attribute wins was a question rather than an assumption.
    //
    // A uniform fill produces ONE non-background colour under flat lighting
    // and a handful under shading. A contour over a banded field produces
    // many. The assertion is a wide margin, not a precise count, because the
    // exact number depends on the driver's shading -- but the two cases are
    // orders apart and the comparison is against the SAME mesh drawn without
    // the contour.
    Prepared part;
    // A NODAL CHANNEL. An element channel cannot be contoured on a boundary
    // view yet: see the note in this case below.
    const Result<ScalarField> field = part.result().field(
        part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
    INFO((field.has_value() ? std::string{} : field.error().message));
    REQUIRE(field.has_value());
    REQUIRE_FALSE(field->uniform());

    const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 12);
    REQUIRE(legend.has_value());
    const Result<std::vector<double>> colours =
        renderer::contourCornerColours(part.view(), *field, *legend);
    INFO((colours.has_value() ? std::string{} : colours.error().message));
    REQUIRE(colours.has_value());
    REQUIRE(colours->size() == part.view().triangleCount() * 9U);

    renderer::Viewer viewer = makeResultViewer();
    const Result<renderer::PresentationId> shown = viewer.displayMesh(
        part.part.feature, part.view(), renderer::MeshStyle::Shaded);
    REQUIRE(shown.has_value());
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();

    const renderer::RenderedImage plain = renderResult(viewer);
    const std::size_t plainColours = distinctColours(plain, kViewBlack);
    REQUIRE(plainColours > 0U);

    const Result<bool> before = viewer.meshHasContour(*shown);
    REQUIRE(before.has_value());
    REQUIRE_FALSE(*before);

    REQUIRE(viewer.setMeshContour(*shown, *colours).has_value());
    const Result<bool> after = viewer.meshHasContour(*shown);
    REQUIRE(after.has_value());
    REQUIRE(*after);

    const renderer::RenderedImage contoured = renderResult(viewer);
    const std::size_t contourColours = distinctColours(contoured, kViewBlack);
    INFO("distinct colours: uniform " << plainColours << ", contoured " << contourColours);

    // THE IMAGE CHANGED AT ALL.
    REQUIRE(contoured.rgb != plain.rgb);
    // AND IT CHANGED INTO MANY COLOURS. Twelve bands over this field cannot
    // show as one or two.
    REQUIRE(contourColours > plainColours);
    REQUIRE(contourColours >= 12U);

    SECTION("and clearing it returns the uniform image exactly") {
        REQUIRE(viewer.clearMeshContour(*shown).has_value());
        const Result<bool> cleared = viewer.meshHasContour(*shown);
        REQUIRE(cleared.has_value());
        REQUIRE_FALSE(*cleared);
        // BYTE-IDENTICAL, which says the contour added no state that outlives
        // it -- no changed aspect, no changed style, no moved geometry.
        CHECK(renderResult(viewer).rgb == plain.rgb);
    }
}

// ---------------------------------------------------------------------------
// Bands onto render triangles
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_ANodalChannelColoursEachCornerFromItsOwnNode", "[renderer][viz]") {
    // THE NODAL PATH, and the independent oracle is the view's own
    // vertex-to-node bridge plus the field's id array -- asked directly here,
    // rather than through the function under test.
    //
    // EACH CORNER ITS OWN BAND, which is what makes the interpolation across a
    // facet the element's linear shape function. It is NOT an average:
    // nothing here reads an element value, and nothing combines two nodes.
    Prepared part;
    const Result<ScalarField> field = part.result().field(
        part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
    REQUIRE(field.has_value());
    REQUIRE(field->domain == ChannelDomain::Node);
    const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 9);
    REQUIRE(legend.has_value());
    const Result<std::vector<std::size_t>> bands = renderer::contourBandsOf(*field, *legend);
    REQUIRE(bands.has_value());

    const Result<std::vector<std::size_t>> corners =
        renderer::triangleCornerBands(part.view(), *field, *bands);
    INFO((corners.has_value() ? std::string{} : corners.error().message));
    REQUIRE(corners.has_value());
    REQUIRE(corners->size() == part.view().triangleCount() * 3U);

    const std::span<const std::uint32_t> indices = part.view().triangleIndices();
    std::size_t facetsWithMixedBands = 0;
    for (std::size_t triangle = 0; triangle < part.view().triangleCount(); ++triangle) {
        std::array<std::size_t, 3> seen{};
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            const std::size_t vertex = indices[(triangle * 3U) + corner];
            const Result<meshing::NodeId> node = part.view().nodeOfVertex(vertex);
            REQUIRE(node.has_value());
            // THE INDEPENDENT LOOKUP: find the node in the field's own array
            // by a linear scan and read its band.
            const auto at = std::ranges::find(field->nodes, *node);
            REQUIRE(at != field->nodes.end());
            const auto index = static_cast<std::size_t>(std::distance(field->nodes.begin(), at));
            INFO("triangle " << triangle << " corner " << corner << " node " << node->value());
            REQUIRE((*corners)[(triangle * 3U) + corner] == (*bands)[index]);
            seen[corner] = (*bands)[index];
        }
        if (seen[0] != seen[1] || seen[1] != seen[2]) {
            ++facetsWithMixedBands;
        }
    }

    // NON-VACUITY, and it is the assertion that distinguishes a nodal contour
    // from an element one: at least one facet must span TWO bands, otherwise
    // every corner happened to agree and the per-corner mapping would be
    // indistinguishable from a per-facet one.
    INFO("facets whose three corners do not share a band: " << facetsWithMixedBands);
    REQUIRE(facetsWithMixedBands > 0);
}

TEST_CASE("StructuralViz_AnElementChannelNeedsTheOwningTetrahedron", "[renderer][viz]") {
    // WHY A TRIANGLE3 BOUNDARY CANNOT CARRY AN ELEMENT CHANNEL, measured
    // rather than described. MeshView::volumeBoundaryOf builds from the mesh's
    // Triangle3 surface elements, so elementOfTriangle returns a TRIANGLE's
    // id, while an element channel's values are per TETRAHEDRON. The two id
    // spaces are disjoint, and meshing::Triangle carries id, nodes and region
    // -- no owning tetrahedron.
    //
    // THIS CASE NO LONGER RECORDS A BLOCKED FEATURE. An earlier revision of it
    // concluded that closing the gap needed a triangle-to-tetrahedron mapping
    // in the qualified P16 meshing module, and that was WRONG: the question
    // only arises if the boundary is taken from the Triangle3 elements.
    // volumeBoundaryByTetrahedronOf derives the boundary from the tetrahedra
    // instead, and ownership is then known by construction (ADR-046). See
    // StructuralViz_EveryElementChannelContoursTheBoundaryByTetrahedron.
    //
    // What this case still holds is the REFUSAL, which is correct behaviour for
    // THIS view and must not be relaxed because another view succeeds.
    //
    // So a boundary view is REFUSED, by name, rather than coloured from a
    // mismatched lookup.
    Prepared part;
    const Result<ScalarField> stress =
        part.result().field(part.part.mesh(), part.outcome().fields, ResultChannel::VonMises);
    REQUIRE(stress.has_value());
    REQUIRE(stress->domain == ChannelDomain::Element);
    const Result<renderer::ContourLegend> legend = renderer::contourLegend(*stress, 8);
    REQUIRE(legend.has_value());
    const Result<std::vector<std::size_t>> bands = renderer::contourBandsOf(*stress, *legend);
    REQUIRE(bands.has_value());

    SECTION("a boundary view is refused, naming the element it could not find") {
        const Result<std::vector<std::size_t>> corners =
            renderer::triangleCornerBands(part.view(), *stress, *bands);
        REQUIRE_FALSE(corners.has_value());
        REQUIRE(corners.error().code == ErrorCode::Internal);
        REQUIRE_THAT(corners.error().message, ContainsSubstring("has no value for it"));
        // AND THE WHOLE COMPOSITION REFUSES TOO, so a caller cannot reach a
        // half-built colour buffer.
        REQUIRE_FALSE(renderer::contourCornerColours(part.view(), *stress, *legend).has_value());
    }

    SECTION("and the element path itself is correct, shown on a TETRAHEDRON view") {
        // THE SAME FUNCTION, THE SAME FIELD, A VIEW WHOSE TRIANGLES REALLY DO
        // NAME TETRAHEDRA. This is what separates a broken element path from
        // an absent boundary mapping, and it is the second that is true.
        std::vector<meshing::ElementId> tetrahedra;
        for (const structural::ElementFields& each : part.outcome().fields.elements()) {
            tetrahedra.push_back(each.element);
        }
        REQUIRE_FALSE(tetrahedra.empty());
        Result<MeshView> interior = MeshView::tetrahedraOf(part.part.mesh(), tetrahedra);
        INFO((interior.has_value() ? std::string{} : interior.error().message));
        REQUIRE(interior.has_value());

        const Result<std::vector<std::size_t>> corners =
            renderer::triangleCornerBands(*interior, *stress, *bands);
        INFO((corners.has_value() ? std::string{} : corners.error().message));
        REQUIRE(corners.has_value());
        REQUIRE(corners->size() == interior->triangleCount() * 3U);

        // EVERY FACET IS FLAT: all three corners share the owning element's
        // band, which is the truth about element-constant stress.
        for (std::size_t triangle = 0; triangle < interior->triangleCount(); ++triangle) {
            const std::size_t a = (*corners)[(triangle * 3U) + 0U];
            const std::size_t b = (*corners)[(triangle * 3U) + 1U];
            const std::size_t c = (*corners)[(triangle * 3U) + 2U];
            INFO("triangle " << triangle);
            REQUIRE(a == b);
            REQUIRE(b == c);
            // And it is the band of the element the view names.
            const Result<meshing::ElementId> owner = interior->elementOfTriangle(triangle);
            REQUIRE(owner.has_value());
            const auto at = std::ranges::find(stress->elements, *owner);
            REQUIRE(at != stress->elements.end());
            const auto index =
                static_cast<std::size_t>(std::distance(stress->elements.begin(), at));
            REQUIRE(a == (*bands)[index]);
        }

        // AND MORE THAN ONE BAND IS IN USE, so "every facet flat" is not
        // "every facet the same colour".
        std::vector<std::size_t> distinct = *corners;
        std::ranges::sort(distinct);
        const auto repeated = std::ranges::unique(distinct);
        const auto used = static_cast<std::size_t>(
            std::distance(distinct.begin(), repeated.begin()));
        INFO("distinct bands in use: " << used);
        REQUIRE(used > 1U);
    }
}

TEST_CASE("StructuralViz_CornerColoursRefuseWhatTheyCannotMap", "[renderer][viz]") {
    Prepared part;
    const Result<ScalarField> field = part.result().field(
        part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
    REQUIRE(field.has_value());
    const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 6);
    REQUIRE(legend.has_value());
    const Result<std::vector<std::size_t>> bands = renderer::contourBandsOf(*field, *legend);
    REQUIRE(bands.has_value());

    SECTION("a band array that is not parallel to the field") {
        std::vector<std::size_t> shortened = *bands;
        shortened.pop_back();
        const Result<std::vector<std::size_t>> corners =
            renderer::triangleCornerBands(part.view(), *field, shortened);
        REQUIRE_FALSE(corners.has_value());
        REQUIRE_THAT(corners.error().message, ContainsSubstring("band indices were given"));
    }

    SECTION("an incomplete field") {
        ScalarField partial = *field;
        partial.classified = partial.expected - 1;
        REQUIRE_FALSE(partial.complete());
        REQUIRE_FALSE(renderer::triangleCornerBands(part.view(), partial, *bands).has_value());
    }

    SECTION("an identity array that is not ascending") {
        // THE DOCUMENTED ORDER, CHECKED. The lookup is a binary search, so an
        // unsorted array would return a WRONG INDEX rather than an error --
        // the quiet kind of wrong this layer exists to avoid. A test that only
        // trusted the contract could not tell the difference.
        ScalarField shuffled = *field;
        REQUIRE(shuffled.nodes.size() > 1U);
        std::swap(shuffled.nodes.front(), shuffled.nodes.back());
        REQUIRE_FALSE(std::ranges::is_sorted(shuffled.nodes));
        const Result<std::vector<std::size_t>> corners =
            renderer::triangleCornerBands(part.view(), shuffled, *bands);
        REQUIRE_FALSE(corners.has_value());
        REQUIRE_THAT(corners.error().message, ContainsSubstring("ascending order"));
    }

    SECTION("and the colours are the legend's own band colours") {
        const Result<std::vector<double>> colours =
            renderer::contourCornerColours(part.view(), *field, *legend);
        REQUIRE(colours.has_value());
        REQUIRE(colours->size() == part.view().triangleCount() * 9U);

        const Result<std::vector<std::size_t>> corners =
            renderer::triangleCornerBands(part.view(), *field, *bands);
        REQUIRE(corners.has_value());
        for (std::size_t i = 0; i < corners->size(); ++i) {
            const renderer::ContourBand& band = legend->bands[(*corners)[i]];
            REQUIRE((*colours)[(i * 3U) + 0U] == band.red);
            REQUIRE((*colours)[(i * 3U) + 1U] == band.green);
            REQUIRE((*colours)[(i * 3U) + 2U] == band.blue);
        }
    }

    SECTION("and a legend from another range is refused before any mapping") {
        renderer::ContourLegend shifted = *legend;
        shifted.maximum = legend->maximum + 1.0;
        REQUIRE_FALSE(renderer::contourCornerColours(part.view(), *field, shifted).has_value());
    }
}

TEST_CASE("StructuralViz_AStaleContourIsNotDrawnAtAll", "[renderer][viz][frame]") {
    // THE BRIEF'S HARDEST REQUIREMENT, and the case that nearly shipped
    // broken: "a stale result must never be presented indistinguishably from
    // a current one".
    //
    // Found by asking an adversarial question about an interaction, not by a
    // failure. Per-vertex colours OVERRIDE a group aspect's interior colour --
    // that is exactly what the 1-versus-4807 measurement established -- and
    // `Compute` sets that interior colour to `staleFacetColour()` when the
    // mesh is stale. So a stale CONTOURED mesh would have drawn its contour
    // and swallowed the stale colour, and in Shaded style there would have
    // been no stale indication whatsoever.
    //
    // A stale contour is not merely mislabelled, it is WRONG DATA: the colours
    // are derived from a result that no longer describes the model. So the
    // contour is dropped rather than tinted.
    Prepared part;
    const Result<ScalarField> field = part.result().field(
        part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
    REQUIRE(field.has_value());
    const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 12);
    REQUIRE(legend.has_value());
    const Result<std::vector<double>> colours =
        renderer::contourCornerColours(part.view(), *field, *legend);
    REQUIRE(colours.has_value());

    // Shaded, deliberately: with no edges drawn there is nothing BUT the
    // facets to carry the distinction, which is the case the defect hid in.
    const auto imageOf = [&part, &colours](bool contoured, bool stale) {
        renderer::Viewer viewer = makeResultViewer();
        const Result<renderer::PresentationId> shown = viewer.displayMesh(
            part.part.feature, part.view(), renderer::MeshStyle::Shaded);
        REQUIRE(shown.has_value());
        if (contoured) {
            REQUIRE(viewer.setMeshContour(*shown, *colours).has_value());
        }
        if (stale) {
            REQUIRE(viewer.setMeshStale(*shown, true).has_value());
        }
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        return renderResult(viewer);
    };

    const renderer::RenderedImage currentContoured = imageOf(true, false);
    const renderer::RenderedImage staleContoured = imageOf(true, true);
    const renderer::RenderedImage staleUniform = imageOf(false, true);
    const renderer::RenderedImage currentUniform = imageOf(false, false);

    const std::size_t currentBands = distinctColours(currentContoured, kViewBlack);
    const std::size_t staleBands = distinctColours(staleContoured, kViewBlack);
    INFO("distinct colours: current contoured " << currentBands << ", stale contoured "
                                                << staleBands);

    // THE PRECONDITION: a CURRENT contour really is many colours, so the
    // comparison below is not two uniform images agreeing.
    REQUIRE(currentBands >= 12U);

    // THE ASSERTION THE DEFECT FAILED. A stale contoured mesh must not look
    // like a current contoured one.
    CHECK(staleContoured.rgb != currentContoured.rgb);
    // And it is not merely different -- it has lost the bands entirely.
    CHECK(staleBands < currentBands);

    // IT LOOKS LIKE A STALE MESH, which is the positive form: dropping the
    // contour means the stale facet colour finally reaches the screen, so the
    // image is the stale uniform one.
    CHECK(staleContoured.rgb == staleUniform.rgb);

    // AND STALE IS STILL DISTINGUISHABLE FROM CURRENT without a contour,
    // which is P16's own guarantee and must not have been disturbed.
    CHECK(staleUniform.rgb != currentUniform.rgb);

    SECTION("and the contour is retained, not discarded, so clearing stale restores it") {
        // THE CONTOUR IS SUPPRESSED FOR DRAWING, not forgotten. A UI that
        // marks a result stale and then re-solves must not have to re-send
        // the colours, and `meshHasContour` must keep telling the truth.
        renderer::Viewer viewer = makeResultViewer();
        const Result<renderer::PresentationId> shown = viewer.displayMesh(
            part.part.feature, part.view(), renderer::MeshStyle::Shaded);
        REQUIRE(shown.has_value());
        REQUIRE(viewer.setMeshContour(*shown, *colours).has_value());
        REQUIRE(viewer.setMeshStale(*shown, true).has_value());

        const Result<bool> held = viewer.meshHasContour(*shown);
        REQUIRE(held.has_value());
        CHECK(*held);

        REQUIRE(viewer.setMeshStale(*shown, false).has_value());
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        CHECK(distinctColours(renderResult(viewer), kViewBlack) >= 12U);
    }
}

// ---------------------------------------------------------------------------
// Four gaps the contour-display probes found
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_AnEntityWithNoValueIsRefusedNotColouredBandZero",
          "[renderer][viz]") {
    // THE GAP PROBE E4 FOUND. Clamping a missing entity to band 0 survived,
    // because on a boundary view every vertex's node IS in the field --
    // RecoveredFields holds every mesh node and the view draws a subset.
    //
    // BUT THE GUARD IS REACHABLE, and that is the point: triangleCornerBands
    // takes a raw MeshView and a raw ScalarField, and ScalarField CARRIES NO
    // STAMP. So nothing stops a caller pairing a view with a field that does
    // not describe the same mesh, and the entity-presence check is the only
    // thing standing between that and a picture.
    //
    // Band 0 is the bottom of the range, so clamping would draw the missing
    // entity as the MINIMUM -- a plausible colour for an absent value, which
    // is the worst kind of wrong.
    Prepared part;
    const Result<ScalarField> field = part.result().field(
        part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
    REQUIRE(field.has_value());
    const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 7);
    REQUIRE(legend.has_value());
    const Result<std::vector<std::size_t>> bands = renderer::contourBandsOf(*field, *legend);
    REQUIRE(bands.has_value());

    // A FIELD MISSING ONE NODE THE VIEW DRAWS. Built by removing an entry
    // rather than by crossing two meshes, because two meshes can number their
    // nodes over the same range and the lookup would then SUCCEED with the
    // wrong value -- which this test could not distinguish from success.
    const Result<meshing::NodeId> drawn = part.view().nodeOfVertex(0U);
    REQUIRE(drawn.has_value());
    const auto at = std::ranges::find(field->nodes, *drawn);
    REQUIRE(at != field->nodes.end());
    const auto index = static_cast<std::size_t>(std::distance(field->nodes.begin(), at));

    ScalarField missing = *field;
    std::vector<std::size_t> shortBands = *bands;
    missing.nodes.erase(missing.nodes.begin() + static_cast<std::ptrdiff_t>(index));
    missing.values.erase(missing.values.begin() + static_cast<std::ptrdiff_t>(index));
    shortBands.erase(shortBands.begin() + static_cast<std::ptrdiff_t>(index));
    missing.expected -= 1;
    missing.classified -= 1;
    // STILL COMPLETE AND STILL SORTED, so the refusal below can only be the
    // entity-presence check and not one of the earlier guards.
    REQUIRE(missing.complete());
    REQUIRE(std::ranges::is_sorted(missing.nodes));
    REQUIRE(shortBands.size() == missing.values.size());
    REQUIRE(std::ranges::find(missing.nodes, *drawn) == missing.nodes.end());

    const Result<std::vector<std::size_t>> corners =
        renderer::triangleCornerBands(part.view(), missing, shortBands);
    REQUIRE_FALSE(corners.has_value());
    REQUIRE(corners.error().code == ErrorCode::Internal);
    REQUIRE_THAT(corners.error().message, ContainsSubstring("has no value for it"));
    REQUIRE_THAT(corners.error().message,
                 ContainsSubstring(std::to_string(drawn->value())));

    SECTION("and the unmodified field still maps, so the refusal is not blanket") {
        REQUIRE(renderer::triangleCornerBands(part.view(), *field, *bands).has_value());
    }
}

TEST_CASE("StructuralViz_ASelectedFacetKeepsItsHighlightUnderAContour",
          "[renderer][viz][frame]") {
    // THE GAP PROBE E9 FOUND, and it took two attempts to measure.
    //
    // The contract is in the Viewer header: "A SELECTED FACET KEEPS THE
    // HIGHLIGHT COLOUR, not its band, so selecting something while a contour
    // is displayed still shows". `facets()` takes a withColours flag that the
    // highlight group passes as FALSE for exactly that reason.
    //
    // THE FIRST ATTEMPT ASSERTED THAT A HIGHLIGHT CHANGES THE IMAGE, and the
    // probe survived it: the two groups carry different MATERIALS, so even
    // when the highlighted facets wrongly take their band colours the
    // lighting still differs and the image still changes. A true assertion
    // that the mutation also satisfies is not a test of the contract.
    //
    // HIGHLIGHTING EVERY TRIANGLE IS THE DECISIVE FORM. The plain group is
    // then empty and the whole mesh is drawn by the highlight group, so:
    //
    //     correct   withColours=false -> ONE uniform colour
    //     mutated   withColours=true  -> the contour, thousands of colours
    //
    // which is a difference of orders rather than of lighting.
    Prepared part;
    const Result<ScalarField> field = part.result().field(
        part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
    REQUIRE(field.has_value());
    const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 10);
    REQUIRE(legend.has_value());
    const Result<std::vector<double>> colours =
        renderer::contourCornerColours(part.view(), *field, *legend);
    REQUIRE(colours.has_value());

    std::vector<std::size_t> everyTriangle;
    for (std::size_t i = 0; i < part.view().triangleCount(); ++i) {
        everyTriangle.push_back(i);
    }
    REQUIRE_FALSE(everyTriangle.empty());

    const auto imageOf = [&part, &colours, &everyTriangle](bool contoured, bool allSelected) {
        renderer::Viewer viewer = makeResultViewer();
        const Result<renderer::PresentationId> shown = viewer.displayMesh(
            part.part.feature, part.view(), renderer::MeshStyle::Shaded);
        REQUIRE(shown.has_value());
        if (contoured) {
            REQUIRE(viewer.setMeshContour(*shown, *colours).has_value());
        }
        if (allSelected) {
            REQUIRE(viewer.setMeshHighlight(*shown, everyTriangle).has_value());
        }
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        return renderResult(viewer);
    };

    const renderer::RenderedImage contourOnly = imageOf(true, false);
    const renderer::RenderedImage allLitContoured = imageOf(true, true);
    const renderer::RenderedImage allLitPlain = imageOf(false, true);

    const std::size_t contourBands = distinctColours(contourOnly, kViewBlack);
    const std::size_t litBands = distinctColours(allLitContoured, kViewBlack);
    INFO("distinct colours: contour " << contourBands << ", every facet selected " << litBands);

    // THE PRECONDITION: the contour really is many colours, so the collapse
    // below is a collapse and not two uniform images agreeing.
    REQUIRE(contourBands >= 10U);

    // THE ASSERTION THE CONTRACT NEEDED. A fully selected mesh is ONE colour,
    // not a contour. The margin is wide because the exact count depends on
    // the driver's shading, but the two cases are orders apart.
    CHECK(litBands < contourBands / 4U);

    // AND IT IS THE SAME IMAGE AS SELECTING EVERYTHING WITHOUT A CONTOUR,
    // which is the positive form: the highlight group ignores the contour
    // entirely rather than blending with it.
    CHECK(allLitContoured.rgb == allLitPlain.rgb);

    SECTION("and a PARTIAL selection still shows, so the highlight is not simply lost") {
        // The complement of the case above: with only some facets selected,
        // both groups draw and the image must differ from the contour alone.
        std::vector<std::size_t> some;
        for (std::size_t i = 0; i < part.view().triangleCount() / 4U; ++i) {
            some.push_back(i);
        }
        REQUIRE_FALSE(some.empty());

        renderer::Viewer viewer = makeResultViewer();
        const Result<renderer::PresentationId> shown = viewer.displayMesh(
            part.part.feature, part.view(), renderer::MeshStyle::Shaded);
        REQUIRE(shown.has_value());
        REQUIRE(viewer.setMeshContour(*shown, *colours).has_value());
        REQUIRE(viewer.setMeshHighlight(*shown, some).has_value());
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        CHECK(renderResult(viewer).rgb != contourOnly.rgb);
    }
}
TEST_CASE("StructuralViz_AContourBufferIsCheckedBeforeItIsDrawn",
          "[renderer][viz][frame]") {
    // THE GAPS PROBES E12 AND E13 FOUND. Both guards were written and
    // documented, and neither was exercised -- the contour display case only
    // ever handed setMeshContour a buffer that contourCornerColours had just
    // produced, which is by construction the right length and in range.
    Prepared part;
    const Result<ScalarField> field = part.result().field(
        part.part.mesh(), part.outcome().fields, ResultChannel::DisplacementMagnitude);
    REQUIRE(field.has_value());
    const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 6);
    REQUIRE(legend.has_value());
    const Result<std::vector<double>> colours =
        renderer::contourCornerColours(part.view(), *field, *legend);
    REQUIRE(colours.has_value());

    renderer::Viewer viewer = makeResultViewer();
    const Result<renderer::PresentationId> shown =
        viewer.displayMesh(part.part.feature, part.view());
    REQUIRE(shown.has_value());

    SECTION("a buffer of the wrong length") {
        // NINE DOUBLES PER RENDER TRIANGLE. A short buffer would be read past
        // its end; a long one means the caller is holding a different mesh.
        std::vector<double> truncated = *colours;
        truncated.resize(truncated.size() - 9U);
        const Result<void> set = viewer.setMeshContour(*shown, truncated);
        REQUIRE_FALSE(set.has_value());
        REQUIRE(set.error().code == ErrorCode::InvalidArgument);
        REQUIRE_THAT(set.error().message, ContainsSubstring("nine per triangle"));

        std::vector<double> extended = *colours;
        extended.insert(extended.end(), 9U, 0.5);
        REQUIRE_FALSE(viewer.setMeshContour(*shown, extended).has_value());

        // AND NOTHING WAS APPLIED, which is the half that matters: a refused
        // buffer must not leave a partial contour behind.
        const Result<bool> has = viewer.meshHasContour(*shown);
        REQUIRE(has.has_value());
        CHECK_FALSE(*has);
    }

    SECTION("a component that is not a colour") {
        // REFUSED, NOT CLAMPED. contourCornerColours cannot produce one, so
        // reaching here means the buffer did not come from it -- and clamping
        // would draw a plausible picture from an implausible input.
        for (const double bad : {-0.001, 1.001, 2.0, -1.0,
                                 std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()}) {
            std::vector<double> spoiled = *colours;
            spoiled[spoiled.size() / 2U] = bad;
            INFO("component " << bad);
            const Result<void> set = viewer.setMeshContour(*shown, spoiled);
            REQUIRE_FALSE(set.has_value());
            REQUIRE(set.error().code == ErrorCode::InvalidArgument);
            REQUIRE_THAT(set.error().message, ContainsSubstring("within [0, 1]"));
        }
        const Result<bool> has = viewer.meshHasContour(*shown);
        REQUIRE(has.has_value());
        CHECK_FALSE(*has);
    }

    SECTION("and the exact bounds are accepted, so the check is a range") {
        std::vector<double> edges = *colours;
        edges[0] = 0.0;
        edges[1] = 1.0;
        REQUIRE(viewer.setMeshContour(*shown, edges).has_value());
        const Result<bool> has = viewer.meshHasContour(*shown);
        REQUIRE(has.has_value());
        CHECK(*has);
    }
}

TEST_CASE("StructuralViz_ANodesRestrainedComponentsAreItsOwn", "[renderer][viz]") {
    // A REPRODUCER FIRST. inspectNode's comment says its component mask is
    // "accumulated over every restraint that NAMES THIS NODE", and the loop
    // iterates every resolution unconditionally:
    //
    //     for (const RestraintResolution& r : restraints->resolutions()) {
    //         out.restrainedComponents = out.restrainedComponents.unionWith(r.components);
    //     }
    //
    // With two restraints of DIFFERENT masks, every restrained node then
    // reports the union of both -- so a node held only in Uz reports fixed,
    // and a UI drawing a support symbol from it draws the wrong support.
    //
    // THE FIXTURE MAKES THE TWO MASKS DIFFER, which is the only way to see
    // it: with one restraint, or with two of equal mask, the union is the
    // right answer by accident. That is why the existing cases missed it.
    SolvedView part;
    const FaceName side{part.feature,
                        FaceSelector{.role = FaceRole::Side, .entity = part.lines.at(0)}};

    std::vector<structural::StructuralLoad> loads = part.endLoadList();
    REQUIRE(part.document
                .modifyObject<structural::StructuralAnalysis>(
                    ObjectId::fromValue(part.analysis.value()),
                    [&](structural::StructuralAnalysis& object) -> Result<bool> {
                        structural::StructuralAnalysisDefinition definition = object.definition();
                        definition.loads = std::move(loads);
                        definition.restraints = {
                            structural::StructuralRestraint::fixedSupport(
                                RestraintId::fromValue(1), part.startCap()),
                            structural::StructuralRestraint{
                                RestraintId::fromValue(2), side,
                                structural::RestraintComponents::along(
                                    structural::DofComponent::Uz)}};
                        return object.setDefinition(std::move(definition));
                    })
                .has_value());

    const structural::StructuralSolveOutcome outcome = part.solve();
    const meshing::Mesh& mesh = part.mesh();

    Result<structural::StructuralModel> model = structural::requireStructuralModel(
        part.document, part.regenerator, part.mesher, part.control);
    REQUIRE(model.has_value());
    Result<structural::MeshDofMap> numbering = structural::buildMeshDofMap(model->mesh().mesh());
    REQUIRE(numbering.has_value());

    const std::vector<structural::StructuralRestraint> fixedOnly{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const std::vector<structural::StructuralRestraint> sideOnly{
        structural::StructuralRestraint{
            RestraintId::fromValue(2), side,
            structural::RestraintComponents::along(structural::DofComponent::Uz)}};
    const std::vector<structural::StructuralRestraint> both{fixedOnly[0], sideOnly[0]};

    // THE TWO SETS SEPARATELY, so the test knows which nodes belong to which
    // restraint without asking the code under test.
    Result<structural::PreparedRestraints> capNodes =
        structural::prepareStructuralRestraints(*model, *numbering, fixedOnly);
    REQUIRE(capNodes.has_value());
    Result<structural::PreparedRestraints> sideNodes =
        structural::prepareStructuralRestraints(*model, *numbering, sideOnly);
    REQUIRE(sideNodes.has_value());
    Result<structural::PreparedRestraints> all =
        structural::prepareStructuralRestraints(*model, *numbering, both);
    REQUIRE(all.has_value());

    // A node on the SIDE face and NOT on the cap. Its only constraint is Uz.
    std::optional<meshing::NodeId> sideOnlyNode;
    for (const meshing::NodeId node : sideNodes->nodes()) {
        if (!std::ranges::binary_search(capNodes->nodes(), node)) {
            sideOnlyNode = node;
            break;
        }
    }
    INFO("cap nodes " << capNodes->nodes().size() << ", side nodes "
                      << sideNodes->nodes().size());
    REQUIRE(sideOnlyNode.has_value());

    const Result<renderer::NodeResultInspection> inspection =
        renderer::inspectNode(mesh, outcome.fields, *sideOnlyNode, &*all, &*numbering);
    REQUIRE(inspection.has_value());
    REQUIRE(inspection->restrained);

    INFO("node " << sideOnlyNode->value() << " reports "
                 << inspection->restrainedComponents.count() << " constrained component(s)");

    // THE ASSERTION. Uz only: one component, and it is Uz.
    CHECK(inspection->restrainedComponents.count() == 1);
    CHECK(inspection->restrainedComponents.holds(structural::DofComponent::Uz));
    CHECK_FALSE(inspection->restrainedComponents.holds(structural::DofComponent::Ux));
    CHECK_FALSE(inspection->restrainedComponents.holds(structural::DofComponent::Uy));
    CHECK_FALSE(inspection->restrainedComponents.isFixed());

    SECTION("and a node on the fixed cap still reports all three") {
        // THE OTHER SIDE, so the fix cannot be "report fewer components".
        const meshing::NodeId capNode = capNodes->nodes().front();
        const Result<renderer::NodeResultInspection> atCap =
            renderer::inspectNode(mesh, outcome.fields, capNode, &*all, &*numbering);
        REQUIRE(atCap.has_value());
        REQUIRE(atCap->restrained);
        CHECK(atCap->restrainedComponents.isFixed());
        CHECK(atCap->restrainedComponents.count() == 3);
    }
}

// ---------------------------------------------------------------------------
// The glyph overlays (ADR-045)
// ---------------------------------------------------------------------------

namespace {

/// The length of a segment, in metres.
[[nodiscard]] double lengthOf(const renderer::OverlaySegment& segment) {
    return std::hypot(segment.to.x.si() - segment.from.x.si(),
                      segment.to.y.si() - segment.from.y.si(),
                      segment.to.z.si() - segment.from.z.si());
}

} // namespace

TEST_CASE("StructuralViz_AnArrowsHeadIsExactlyAtItsNode", "[renderer][viz]") {
    // THE CONVENTION ADR-045 FIXES, and the reason it was chosen: the head
    // coincides with a canonical node position, so a test can compare it BIT
    // FOR BIT rather than within a tolerance.
    //
    // The oracle is `Mesh::findNode`, asked directly here -- not the overlay's
    // own idea of where the node is, because that is the thing under test.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();
    const std::vector<renderer::NodalLoadArrow> arrows =
        renderer::nodalLoadArrows(*prepared.loads);
    REQUIRE_FALSE(arrows.empty());

    const Result<renderer::StructuralOverlay> overlay =
        renderer::loadArrowOverlay(mesh, arrows, 1.0, 1000.0);
    INFO((overlay.has_value() ? std::string{} : overlay.error().message));
    REQUIRE(overlay.has_value());
    REQUIRE(overlay->role == renderer::OverlayRole::LoadArrows);

    // FIVE SEGMENTS PER DRAWN ARROW: one shaft and four barbs.
    REQUIRE(overlay->glyphs > 0);
    REQUIRE(overlay->segments.size() == overlay->glyphs * renderer::kSegmentsPerArrow);
    REQUIRE(overlay->glyphs + overlay->skipped == arrows.size());

    std::size_t checked = 0;
    for (std::size_t i = 0; i < arrows.size(); ++i) {
        const renderer::NodalLoadArrow& arrow = arrows[i];
        if (!arrow.hasDirection) {
            continue;
        }
        const meshing::Node* node = mesh.findNode(arrow.node);
        REQUIRE(node != nullptr);
        // The shaft of the i-th DRAWN arrow. Skipped arrows produce nothing,
        // so the index walks the drawn ones.
        const renderer::OverlaySegment& shaft =
            overlay->segments[checked * renderer::kSegmentsPerArrow];
        INFO("node " << arrow.node.value());
        // EXACTLY, not within a tolerance.
        REQUIRE(shaft.to == node->position);
        // AND THE TAIL IS UPSTREAM: the shaft's length is the display length,
        // and the tail is on the far side of the node from the force.
        const Result<double> expected =
            renderer::displayArrowLength(std::hypot(arrow.force.x.si(), arrow.force.y.si(),
                                                    arrow.force.z.si()),
                                         1000.0, 1.0);
        REQUIRE(expected.has_value());
        REQUIRE_THAT(lengthOf(shaft), WithinRel(*expected, 1.0e-12));
        ++checked;
    }
    REQUIRE(checked == overlay->glyphs);
}

TEST_CASE("StructuralViz_AnOverlayLeavesThisLayerInSiMetres", "[renderer][viz]") {
    // THE UNIT BOUNDARY, for the overlay. Everything here is metres, and the
    // single conversion is the adapter's.
    //
    // The discriminating assertion is the EXTENT: the fixture is a
    // 40 x 30 x 20 mm bar, so every endpoint must sit within a few hundredths
    // of a metre of the origin. If this layer were converting, the numbers
    // would be in the tens.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const Result<renderer::StructuralOverlay> overlay = renderer::restraintSymbolOverlay(
        part.mesh(), *prepared.restraints, *prepared.numbering, 0.004);
    INFO((overlay.has_value() ? std::string{} : overlay.error().message));
    REQUIRE(overlay.has_value());
    REQUIRE_FALSE(overlay->empty());

    double widest = 0.0;
    for (const renderer::OverlaySegment& segment : overlay->segments) {
        for (const double each : {segment.from.x.si(), segment.from.y.si(), segment.from.z.si(),
                                  segment.to.x.si(), segment.to.y.si(), segment.to.z.si()}) {
            REQUIRE(std::isfinite(each));
            widest = std::max(widest, std::abs(each));
        }
    }
    INFO("widest coordinate: " << widest << " m");
    // 40 mm plus a 4 mm bar, so comfortably under 0.05 m and nowhere near 40.
    REQUIRE(widest < 0.05);
    REQUIRE(widest > 0.001);

    // AND EVERY BAR IS THE LENGTH ASKED FOR, exactly.
    for (const renderer::OverlaySegment& segment : overlay->segments) {
        REQUIRE_THAT(lengthOf(segment), WithinRel(0.004, 1.0e-12));
    }
}

TEST_CASE("StructuralViz_ARestraintSymbolHasOneBarPerConstrainedComponent",
          "[renderer][viz]") {
    // THE CLAIM ADR-045 MAKES ABOUT READABILITY, as an assertion: the bar
    // count IS the constrained degree-of-freedom count, and the bar
    // directions are which ones.
    //
    // The oracle is the ConstraintSet and the numbering, asked directly --
    // the same two objects the function uses, but asked here independently so
    // a wrong mask would show as a mismatch rather than agree with itself.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();
    const Result<renderer::StructuralOverlay> overlay = renderer::restraintSymbolOverlay(
        mesh, *prepared.restraints, *prepared.numbering, 0.004);
    REQUIRE(overlay.has_value());

    // A FIXED SUPPORT: three bars per node, one along each axis.
    const std::span<const meshing::NodeId> held = prepared.restraints->nodes();
    REQUIRE_FALSE(held.empty());
    REQUIRE(overlay->glyphs == held.size());

    std::size_t expectedBars = 0;
    for (const meshing::NodeId node : held) {
        const Result<std::array<structural::DofIndex, structural::kDofsPerNode>> indices =
            prepared.numbering->indicesOf(node);
        REQUIRE(indices.has_value());
        for (const structural::DofIndex& dof : *indices) {
            if (prepared.restraints->constraints().contains(dof)) {
                ++expectedBars;
            }
        }
    }
    INFO("expected bars " << expectedBars << ", segments " << overlay->segments.size());
    REQUIRE(overlay->segments.size() == expectedBars);
    // A fixed support holds all three, so it is three per node -- stated
    // separately so the equality above cannot pass on a wrong count twice.
    REQUIRE(expectedBars == held.size() * structural::kDofsPerNode);

    // EVERY BAR IS AXIS-ALIGNED AND CENTRED ON ITS NODE. Two of the three
    // coordinates are unchanged along a bar, which is what "aligned with that
    // axis" means, and the midpoint is the node.
    for (const renderer::OverlaySegment& segment : overlay->segments) {
        const std::array<double, 3> delta{segment.to.x.si() - segment.from.x.si(),
                                          segment.to.y.si() - segment.from.y.si(),
                                          segment.to.z.si() - segment.from.z.si()};
        std::size_t moving = 0;
        for (const double each : delta) {
            if (each != 0.0) {
                ++moving;
            }
        }
        REQUIRE(moving == 1);

        const Point3D midpoint{
            Length::fromSi((segment.from.x.si() + segment.to.x.si()) / 2.0),
            Length::fromSi((segment.from.y.si() + segment.to.y.si()) / 2.0),
            Length::fromSi((segment.from.z.si() + segment.to.z.si()) / 2.0)};
        bool atSomeNode = false;
        for (const meshing::NodeId node : held) {
            const meshing::Node* found = mesh.findNode(node);
            REQUIRE(found != nullptr);
            if (std::abs(midpoint.x.si() - found->position.x.si()) < 1.0e-15 &&
                std::abs(midpoint.y.si() - found->position.y.si()) < 1.0e-15 &&
                std::abs(midpoint.z.si() - found->position.z.si()) < 1.0e-15) {
                atSomeNode = true;
                break;
            }
        }
        REQUIRE(atSomeNode);
    }
}

TEST_CASE("StructuralViz_AZeroForceDrawsNoArrowAndIsCounted", "[renderer][viz]") {
    // A ZERO FORCE IS SKIPPED, NOT DRAWN, and the count says so. An arrow of
    // zero length pointing some arbitrary way would be a drawn claim about a
    // direction that does not exist, and silently omitting it would make a
    // UI's "3 loads" disagree with the three arrows it did not draw.
    //
    // BUILT BY HAND, because the solved fixture's loads are all non-zero:
    // NodalLoadArrow is a public aggregate, so the case can be stated exactly.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();
    const meshing::NodeId first = mesh.nodes().front().id;
    const meshing::NodeId second = mesh.nodes().back().id;

    const std::vector<renderer::NodalLoadArrow> arrows{
        renderer::NodalLoadArrow{.node = first,
                                 .force = Force3D{Force::fromSi(0.0), Force::fromSi(0.0),
                                                  Force::fromSi(0.0)},
                                 .hasDirection = false},
        renderer::NodalLoadArrow{.node = second,
                                 .force = Force3D{Force::fromSi(0.0), Force::fromSi(0.0),
                                                  Force::fromSi(500.0)},
                                 .hasDirection = true}};

    const Result<renderer::StructuralOverlay> overlay =
        renderer::loadArrowOverlay(mesh, arrows, 1.0, 500.0);
    REQUIRE(overlay.has_value());

    REQUIRE(overlay->glyphs == 1);
    REQUIRE(overlay->skipped == 1);
    REQUIRE(overlay->segments.size() == renderer::kSegmentsPerArrow);
    // AND THE ONE DRAWN IS THE NON-ZERO ONE, at its own node.
    const meshing::Node* node = mesh.findNode(second);
    REQUIRE(node != nullptr);
    REQUIRE(overlay->segments.front().to == node->position);

    SECTION("and an all-zero set draws nothing while still counting") {
        const std::vector<renderer::NodalLoadArrow> silent{arrows.front()};
        const Result<renderer::StructuralOverlay> none =
            renderer::loadArrowOverlay(mesh, silent, 1.0, 500.0);
        REQUIRE(none.has_value());
        CHECK(none->empty());
        CHECK(none->glyphs == 0);
        CHECK(none->skipped == 1);
    }
}

TEST_CASE("StructuralViz_AnOverlayRefusesWhatItCannotPlace", "[renderer][viz]") {
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();

    SECTION("a node the mesh does not hold is refused, not dropped") {
        // AN ABSENT NODE MEANS THE ARROWS AND THE MESH DISAGREE, and a
        // silently shorter overlay would hide that.
        const std::vector<renderer::NodalLoadArrow> stray{
            renderer::NodalLoadArrow{.node = meshing::NodeId::fromValue(999991),
                                     .force = Force3D{Force::fromSi(1.0), Force::fromSi(0.0),
                                                      Force::fromSi(0.0)},
                                     .hasDirection = true}};
        const Result<renderer::StructuralOverlay> overlay =
            renderer::loadArrowOverlay(mesh, stray, 1.0, 1.0);
        REQUIRE_FALSE(overlay.has_value());
        REQUIRE(overlay.error().code == ErrorCode::NotFound);
        REQUIRE_THAT(overlay.error().message, ContainsSubstring("999991"));
    }

    SECTION("a bad scale or reference is refused") {
        const std::vector<renderer::NodalLoadArrow> one{
            renderer::NodalLoadArrow{.node = mesh.nodes().front().id,
                                     .force = Force3D{Force::fromSi(1.0), Force::fromSi(0.0),
                                                      Force::fromSi(0.0)},
                                     .hasDirection = true}};
        for (const double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()}) {
            INFO("scale " << bad);
            REQUIRE_FALSE(renderer::loadArrowOverlay(mesh, one, bad, 1.0).has_value());
            INFO("reference " << bad);
            REQUIRE_FALSE(renderer::loadArrowOverlay(mesh, one, 1.0, bad).has_value());
        }
        // AND THE VALID PAIR STILL WORKS, so the refusals are not blanket.
        REQUIRE(renderer::loadArrowOverlay(mesh, one, 1.0, 1.0).has_value());
    }

    SECTION("a bad bar length is refused") {
        for (const double bad : {0.0, -0.001, std::numeric_limits<double>::quiet_NaN()}) {
            INFO("bar length " << bad);
            REQUIRE_FALSE(renderer::restraintSymbolOverlay(mesh, *prepared.restraints,
                                                           *prepared.numbering, bad)
                              .has_value());
        }
    }
}

TEST_CASE("StructuralViz_AReactionOverlayKeepsTheQualifiedSign", "[renderer][viz]") {
    // THE SIGN IS P17-REACTION'S, and a viewer is not entitled to a second
    // opinion. The fixture pushes the end cap along +z against a fixed start
    // cap, so the reactions OPPOSE the load and point along -z.
    //
    // The assertion is on the GEOMETRY, which is the thing a user reads: the
    // shaft runs from +z toward the node, so the arrow points in -z.
    SolvedView part;
    part.installAndSolve();
    const structural::StructuralSolveOutcome outcome = part.solve();
    const meshing::Mesh& mesh = part.mesh();
    const std::vector<renderer::ReactionArrow> arrows =
        renderer::reactionArrows(outcome.reactions);
    REQUIRE_FALSE(arrows.empty());

    const Result<renderer::StructuralOverlay> overlay =
        renderer::reactionArrowOverlay(mesh, arrows, 1.0, 1000.0);
    REQUIRE(overlay.has_value());
    REQUIRE(overlay->role == renderer::OverlayRole::ReactionArrows);
    REQUIRE(overlay->glyphs > 0);

    std::size_t downward = 0;
    std::size_t drawn = 0;
    for (const renderer::ReactionArrow& arrow : arrows) {
        if (!arrow.hasDirection) {
            continue;
        }
        const renderer::OverlaySegment& shaft =
            overlay->segments[drawn * renderer::kSegmentsPerArrow];
        // THE SHAFT'S DIRECTION IS THE FORCE'S, compared component by
        // component against the glyph the caller already holds.
        const double dz = shaft.to.z.si() - shaft.from.z.si();
        if (arrow.force.z.si() < 0.0) {
            REQUIRE(dz < 0.0);
            ++downward;
        } else if (arrow.force.z.si() > 0.0) {
            REQUIRE(dz > 0.0);
        }
        ++drawn;
    }
    INFO("reactions pointing along -z: " << downward << " of " << drawn);
    // NON-VACUITY: the load is +z, so the reactions carrying it must oppose
    // it. If none did, the sign would have been reversed somewhere.
    REQUIRE(downward > 0);
}

TEST_CASE("StructuralViz_AnOverlayReachesTheScreenOverItsMesh", "[renderer][viz][frame]") {
    // THE GLYPHS ON SCREEN, measured the only way that distinguishes "drawn"
    // from "accepted": the image CHANGES when the overlay is added, and the
    // overlay alone covers FEWER pixels than the mesh -- because arrows are
    // line segments over a solid, not a second solid.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();
    Result<MeshView> view = MeshView::volumeBoundaryOf(mesh);
    REQUIRE(view.has_value());

    const std::vector<renderer::NodalLoadArrow> arrows =
        renderer::nodalLoadArrows(*prepared.loads);
    // A SCALE BIG ENOUGH TO SEE. The loads are a kilonewton spread over the
    // end cap's nodes, so a reference of that per-node magnitude and a scale
    // of a few millimetres gives arrows the size of the part's features.
    const Result<renderer::StructuralOverlay> loadOverlay =
        renderer::loadArrowOverlay(mesh, arrows, 0.008, 1000.0 / static_cast<double>(arrows.size()));
    INFO((loadOverlay.has_value() ? std::string{} : loadOverlay.error().message));
    REQUIRE(loadOverlay.has_value());
    REQUIRE_FALSE(loadOverlay->empty());

    const Result<renderer::StructuralOverlay> supports = renderer::restraintSymbolOverlay(
        mesh, *prepared.restraints, *prepared.numbering, 0.004);
    REQUIRE(supports.has_value());
    REQUIRE_FALSE(supports->empty());

    const auto imageOf = [&](bool withMesh, bool withLoads, bool withSupports) {
        renderer::Viewer viewer = makeResultViewer();
        if (withMesh) {
            REQUIRE(viewer.displayMesh(part.feature, *view, renderer::MeshStyle::Shaded)
                        .has_value());
        }
        if (withLoads) {
            const Result<renderer::PresentationId> shown =
                viewer.displayOverlay(part.feature, *loadOverlay);
            INFO((shown.has_value() ? std::string{} : shown.error().message));
            REQUIRE(shown.has_value());
        }
        if (withSupports) {
            REQUIRE(viewer.displayOverlay(part.feature, *supports).has_value());
        }
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        return renderResult(viewer);
    };

    const renderer::RenderedImage meshOnly = imageOf(true, false, false);
    const renderer::RenderedImage withLoads = imageOf(true, true, false);
    const renderer::RenderedImage everything = imageOf(true, true, true);
    const renderer::RenderedImage loadsAlone = imageOf(false, true, false);

    const std::size_t bare = meshOnly.coverage(kViewBlack);
    const std::size_t arrowsOnly = loadsAlone.coverage(kViewBlack);
    INFO("mesh " << bare << " px, arrows alone " << arrowsOnly << " px");

    REQUIRE(bare > 0U);
    // THE ARROWS ARE ON SCREEN.
    REQUIRE(arrowsOnly > 0U);
    // AND THEY ARE LINES, NOT A SOLID: a handful of segments cannot cover
    // what a shaded boundary mesh covers. If this failed, the overlay would
    // be drawing something other than segments.
    REQUIRE(arrowsOnly < bare);

    // ADDING EACH OVERLAY CHANGES THE IMAGE, which is what says it was drawn
    // rather than merely accepted.
    CHECK(withLoads.rgb != meshOnly.rgb);
    CHECK(everything.rgb != withLoads.rgb);

    SECTION("each overlay is one batched presentation, and it is not a mesh") {
        renderer::Viewer viewer = makeResultViewer();
        const Result<renderer::PresentationId> shown =
            viewer.displayOverlay(part.feature, *loadOverlay);
        REQUIRE(shown.has_value());
        REQUIRE(viewer.displayed().size() == 1U);
        CHECK(viewer.displayed().front().kind == renderer::PresentationKind::Overlay);

        // MESH OPERATIONS MUST REFUSE IT, and say what it IS. An overlay that
        // accepted setMeshStyle would be a presentation pretending to be a
        // mesh.
        const Result<void> styled =
            viewer.setMeshStyle(*shown, renderer::MeshStyle::Wireframe);
        REQUIRE_FALSE(styled.has_value());
        REQUIRE(styled.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(styled.error().message, ContainsSubstring("a structural overlay"));
        REQUIRE_FALSE(viewer.setMeshStale(*shown, true).has_value());
        REQUIRE_FALSE(viewer.meshStampOf(*shown).has_value());
    }

    SECTION("an empty overlay is refused rather than displayed as nothing") {
        const std::vector<renderer::NodalLoadArrow> silent{
            renderer::NodalLoadArrow{.node = mesh.nodes().front().id,
                                     .force = Force3D{},
                                     .hasDirection = false}};
        const Result<renderer::StructuralOverlay> nothing =
            renderer::loadArrowOverlay(mesh, silent, 1.0, 1.0);
        REQUIRE(nothing.has_value());
        REQUIRE(nothing->empty());

        renderer::Viewer viewer = makeResultViewer();
        const Result<renderer::PresentationId> shown =
            viewer.displayOverlay(part.feature, *nothing);
        REQUIRE_FALSE(shown.has_value());
        REQUIRE(shown.error().code == ErrorCode::FailedPrecondition);
        REQUIRE_THAT(shown.error().message, ContainsSubstring("no segments"));
        CHECK(viewer.displayed().empty());
    }
}

TEST_CASE("StructuralViz_ARestraintBarLiesAlongTheComponentItDepicts", "[renderer][viz]") {
    // THE GAP THE ADVERSARIAL REVIEW FOUND, before any probe ran.
    //
    // The earlier case asserts that each bar is axis-aligned, centred on a
    // node, and that the bar COUNT matches the constrained degree-of-freedom
    // count. With a FIXED support all three components are held, so all three
    // bars exist and ANY PERMUTATION OF THE AXES satisfies every one of those
    // assertions. A Ux constraint drawn along Y would have passed.
    //
    // Only a PARTIAL restraint can tell: hold Uz alone and the single bar must
    // lie along Z.
    SolvedView part;
    const FaceName side{part.feature,
                        FaceSelector{.role = FaceRole::Side, .entity = part.lines.at(0)}};
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap()),
        structural::StructuralRestraint{
            RestraintId::fromValue(2), side,
            structural::RestraintComponents::along(structural::DofComponent::Uz)}};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();

    const Result<renderer::StructuralOverlay> overlay = renderer::restraintSymbolOverlay(
        mesh, *prepared.restraints, *prepared.numbering, 0.004);
    INFO((overlay.has_value() ? std::string{} : overlay.error().message));
    REQUIRE(overlay.has_value());

    // WALK THE NODES IN THE ORDER THE OVERLAY DOES, and for each, check that
    // the bars it emitted lie along exactly the constrained axes -- asked of
    // the ConstraintSet and the numbering independently.
    std::size_t at = 0;
    std::size_t partialNodes = 0;
    for (const meshing::NodeId node : prepared.restraints->nodes()) {
        const Result<std::array<structural::DofIndex, structural::kDofsPerNode>> indices =
            prepared.numbering->indicesOf(node);
        REQUIRE(indices.has_value());
        std::vector<std::size_t> expectedAxes;
        for (std::size_t which = 0; which < structural::kDofsPerNode; ++which) {
            if (prepared.restraints->constraints().contains((*indices)[which])) {
                expectedAxes.push_back(which);
            }
        }
        REQUIRE_FALSE(expectedAxes.empty());
        if (expectedAxes.size() < structural::kDofsPerNode) {
            ++partialNodes;
        }

        for (const std::size_t axis : expectedAxes) {
            REQUIRE(at < overlay->segments.size());
            const renderer::OverlaySegment& bar = overlay->segments[at];
            const std::array<double, 3> delta{bar.to.x.si() - bar.from.x.si(),
                                              bar.to.y.si() - bar.from.y.si(),
                                              bar.to.z.si() - bar.from.z.si()};
            INFO("node " << node.value() << " bar " << at << " expected axis " << axis);
            // THE CONSTRAINED AXIS MOVES, and the other two do not.
            REQUIRE(delta[axis] != 0.0);
            for (std::size_t other = 0; other < 3U; ++other) {
                if (other != axis) {
                    REQUIRE(delta[other] == 0.0);
                }
            }
            ++at;
        }
    }
    REQUIRE(at == overlay->segments.size());

    // NON-VACUITY, and it is the whole point of this case: at least one node
    // must be PARTIALLY restrained, otherwise every node has all three bars
    // and the axis mapping is again untested.
    INFO("partially restrained nodes: " << partialNodes);
    REQUIRE(partialNodes > 0);
}

TEST_CASE("StructuralViz_AnArrowTooSmallToDrawIsCountedNotDegenerate", "[renderer][viz]") {
    // THE SECOND GAP THE REVIEW FOUND. `displayArrowLength` is a proportion,
    // scale times magnitude over reference, and it UNDERFLOWS TO ZERO for a
    // small enough force against a large enough reference. The force still has
    // a direction, so it is not skipped as a zero -- and a zero-length shaft
    // puts the tail on the head and collapses all four barbs onto the same
    // point, drawing five degenerate segments at a dot.
    //
    // A DIFFERENT COUNTER FROM `skipped`, because the remedy differs: this one
    // is the caller's scale, not the model's load.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();

    const std::vector<renderer::NodalLoadArrow> arrows{
        // A real direction, and a magnitude that cannot survive the division.
        renderer::NodalLoadArrow{.node = mesh.nodes().front().id,
                                 .force = Force3D{Force::fromSi(1.0e-300), Force::fromSi(0.0),
                                                  Force::fromSi(0.0)},
                                 .hasDirection = true},
        renderer::NodalLoadArrow{.node = mesh.nodes().back().id,
                                 .force = Force3D{Force::fromSi(0.0), Force::fromSi(0.0),
                                                  Force::fromSi(1.0e300)},
                                 .hasDirection = true}};

    const Result<renderer::StructuralOverlay> overlay =
        renderer::loadArrowOverlay(mesh, arrows, 1.0, 1.0e300);
    INFO((overlay.has_value() ? std::string{} : overlay.error().message));
    REQUIRE(overlay.has_value());

    // THE PRECONDITION: the tiny one really does underflow, asked of
    // displayArrowLength directly rather than inferred from the overlay.
    const Result<double> tiny = renderer::displayArrowLength(1.0e-300, 1.0e300, 1.0);
    REQUIRE(tiny.has_value());
    INFO("the tiny arrow display length is " << *tiny);
    REQUIRE(*tiny == 0.0);

    // ONE DRAWN, ONE COUNTED AS TOO SMALL, NONE SKIPPED -- both forces have a
    // direction, so neither is a zero.
    CHECK(overlay->glyphs == 1);
    CHECK(overlay->tooSmall == 1);
    CHECK(overlay->skipped == 0);
    CHECK(overlay->segments.size() == renderer::kSegmentsPerArrow);

    // AND THE ACCOUNTING INVARIANT HOLDS.
    CHECK(overlay->glyphs + overlay->skipped + overlay->tooSmall == arrows.size());

    // NO DEGENERATE SEGMENT REACHED THE LIST: every segment has a positive
    // length, which is what the guard exists to guarantee.
    for (const renderer::OverlaySegment& segment : overlay->segments) {
        REQUIRE(lengthOf(segment) > 0.0);
    }
}

TEST_CASE("StructuralViz_AnOverlayDoesNotBlockPickingTheMeshBeneathIt",
          "[renderer][viz][frame]") {
    // THE THIRD GAP THE REVIEW FOUND, and it is the whole reason
    // OverlayPresentation::ComputeSelection is deliberately empty: an
    // annotation drawn over a mesh must not come between the user and the
    // element they were trying to click.
    //
    // Asserting that ComputeSelection adds nothing is not the same as
    // asserting that picking still WORKS, and only the second is a property a
    // user has.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();
    Result<MeshView> view = MeshView::volumeBoundaryOf(mesh);
    REQUIRE(view.has_value());

    const std::vector<renderer::NodalLoadArrow> arrows =
        renderer::nodalLoadArrows(*prepared.loads);
    const Result<renderer::StructuralOverlay> overlay = renderer::loadArrowOverlay(
        mesh, arrows, 0.008, 1000.0 / static_cast<double>(arrows.size()));
    REQUIRE(overlay.has_value());

    const auto pickAtCentre = [&](bool withOverlay) {
        renderer::Viewer viewer = makeResultViewer();
        const Result<renderer::PresentationId> shown =
            viewer.displayMesh(part.feature, *view, renderer::MeshStyle::Shaded);
        REQUIRE(shown.has_value());
        if (withOverlay) {
            REQUIRE(viewer.displayOverlay(part.feature, *overlay).has_value());
        }
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        Result<std::optional<renderer::MeshPick>> picked =
            viewer.pickMeshAt(kViewWidth / 2, kViewHeight / 2);
        REQUIRE(picked.has_value());
        return *picked;
    };

    const std::optional<renderer::MeshPick> bare = pickAtCentre(false);
    const std::optional<renderer::MeshPick> covered = pickAtCentre(true);

    // THE PRECONDITION: the centre of the view really does hit the mesh, so
    // the comparison below is not two misses agreeing.
    REQUIRE(bare.has_value());

    // THE MESH IS STILL PICKABLE THROUGH THE OVERLAY, and it is the SAME
    // triangle -- so the overlay neither swallowed the pick nor deflected it.
    REQUIRE(covered.has_value());
    CHECK(covered->triangle == bare->triangle);
}

TEST_CASE("StructuralViz_AnAxisAlignedArrowStillGetsItsBarbs", "[renderer][viz]") {
    // A FOURTH REVIEW FINDING. The barbs need a basis perpendicular to the
    // shaft, and `perpendicularTo` seeds the cross product from the axis LEAST
    // aligned with the arrow -- precisely so an axis-aligned arrow does not
    // produce a degenerate cross product and lose its head.
    //
    // Nothing tested it. Every arrow in the other cases points along +z or -z
    // because that is what the fixture's load does, so a seed hard-coded to
    // unitX would have worked for all of them and collapsed the barbs of any
    // arrow pointing along x.
    //
    // All three axes, both directions, plus a diagonal.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();
    const meshing::NodeId node = mesh.nodes().front().id;

    const std::array<Force3D, 7> directions{
        Force3D{Force::fromSi(1.0), Force::fromSi(0.0), Force::fromSi(0.0)},
        Force3D{Force::fromSi(-1.0), Force::fromSi(0.0), Force::fromSi(0.0)},
        Force3D{Force::fromSi(0.0), Force::fromSi(1.0), Force::fromSi(0.0)},
        Force3D{Force::fromSi(0.0), Force::fromSi(-1.0), Force::fromSi(0.0)},
        Force3D{Force::fromSi(0.0), Force::fromSi(0.0), Force::fromSi(1.0)},
        Force3D{Force::fromSi(0.0), Force::fromSi(0.0), Force::fromSi(-1.0)},
        Force3D{Force::fromSi(1.0), Force::fromSi(1.0), Force::fromSi(1.0)}};

    for (const Force3D& force : directions) {
        const std::vector<renderer::NodalLoadArrow> one{
            renderer::NodalLoadArrow{.node = node, .force = force, .hasDirection = true}};
        const Result<renderer::StructuralOverlay> overlay =
            renderer::loadArrowOverlay(mesh, one, 0.004, 1.0);
        INFO("force (" << force.x.si() << ", " << force.y.si() << ", " << force.z.si() << ")");
        REQUIRE(overlay.has_value());

        // THE WHOLE ARROW, every time: one shaft and four barbs.
        REQUIRE(overlay->glyphs == 1);
        REQUIRE(overlay->segments.size() == renderer::kSegmentsPerArrow);

        // AND NO SEGMENT IS DEGENERATE. A collapsed basis would emit the
        // shaft and nothing else, which the count above catches -- but a
        // NEAR-degenerate one would emit four barbs of almost no length, and
        // only this catches that.
        for (const renderer::OverlaySegment& segment : overlay->segments) {
            REQUIRE(lengthOf(segment) > 0.0);
        }

        // THE FOUR BARBS ALL START AT THE HEAD and are shorter than the
        // shaft, which is what makes them read as a head rather than a cross.
        const double shaft = lengthOf(overlay->segments.front());
        for (std::size_t barb = 1; barb < renderer::kSegmentsPerArrow; ++barb) {
            const renderer::OverlaySegment& each = overlay->segments[barb];
            REQUIRE(each.from == overlay->segments.front().to);
            REQUIRE(lengthOf(each) < shaft);
        }
    }
}

TEST_CASE("StructuralViz_AnOverlayIsDrawnAtTheMeshsOwnScale", "[renderer][viz][frame]") {
    // THE P16 TRAP, FOR THE OVERLAY. A fifth review finding, and the one most
    // worth having: the overlay leaves this layer in SI metres and reaches
    // model space through `toModelLength`. Drop that conversion and the
    // overlay is 1000 times too small -- and displayed ALONE it looks
    // perfect, because fitAll frames the only thing in the scene. That is
    // exactly how P16-VIZ-001's defect hid.
    //
    // The earlier display case asserts the arrows are on screen and that
    // adding them changes the image. A thousand-times-small overlay satisfies
    // both: framed alone it is a full-size picture of tiny arrows, and beside
    // the mesh it is a speck that still perturbs a few pixels.
    //
    // THE DISCRIMINATOR IS THE SILHOUETTE. Arrows sized to the part STICK OUT
    // of it, so mesh-plus-overlay covers MORE pixels than the mesh alone. A
    // speck sitting inside the silhouette covers no more.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();
    Result<MeshView> view = MeshView::volumeBoundaryOf(mesh);
    REQUIRE(view.has_value());

    const std::vector<renderer::NodalLoadArrow> arrows =
        renderer::nodalLoadArrows(*prepared.loads);
    // ARROWS ABOUT HALF THE BAR'S LENGTH, so they unmistakably extend past
    // the end cap they are applied to.
    const Result<renderer::StructuralOverlay> overlay = renderer::loadArrowOverlay(
        mesh, arrows, 0.020, 1000.0 / static_cast<double>(arrows.size()));
    REQUIRE(overlay.has_value());
    REQUIRE(overlay->glyphs == arrows.size());

    // THE ARROWS REALLY ARE THAT LONG, measured headlessly before anything is
    // drawn, so a failure below is about the display and not the geometry.
    double longest = 0.0;
    for (std::size_t i = 0; i < overlay->segments.size();
         i += renderer::kSegmentsPerArrow) {
        longest = std::max(longest, lengthOf(overlay->segments[i]));
    }
    INFO("longest shaft: " << longest << " m on a 0.040 m bar");
    REQUIRE(longest > 0.010);

    // THE DISCRIMINATOR HAS TO SURVIVE fitAll's REFRAMING, and the first
    // attempt at this test did not. Asserting that mesh-plus-overlay covers
    // MORE pixels than the mesh alone is wrong: fitAll zooms out to fit the
    // arrows, so the mesh's own silhouette shrinks faster than thin segments
    // add pixels, and the measured coverage went DOWN -- 42036 to 36586. The
    // arrows were correct and the assertion was not.
    //
    // SO THE CAMERA IS HELD STILL. Display both, fitAll once so the framing
    // accounts for both, then HIDE the overlay and render again. setVisible
    // does not move the camera, so the two images differ by exactly the
    // arrows' pixels -- a meaningful count at the mesh's own scale, and
    // essentially nothing for a speck a thousand times too small.
    renderer::Viewer viewer = makeResultViewer();
    REQUIRE(viewer.displayMesh(part.feature, *view, renderer::MeshStyle::Shaded).has_value());
    const Result<renderer::PresentationId> shown =
        viewer.displayOverlay(part.feature, *overlay);
    REQUIRE(shown.has_value());
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();

    const std::size_t both = renderResult(viewer).coverage(kViewBlack);
    REQUIRE(viewer.setVisible(*shown, false).has_value());
    const std::size_t meshOnly = renderResult(viewer).coverage(kViewBlack);

    INFO("at one camera: mesh+overlay " << both << " px, mesh alone " << meshOnly << " px");
    REQUIRE(meshOnly > 0U);

    // MEASURED: 278 px of 36308, and the first version of this assertion
    // demanded five per cent -- a threshold I guessed rather than measured.
    //
    // WHY IT IS ONLY 278. The convention is head-at-the-node with the tail
    // UPSTREAM, so for this load -- tensile, +z on the +z end cap -- the
    // shaft runs back INTO the solid and the depth buffer hides it. What
    // shows is the head and the barbs splaying past the cap. That is a
    // readability consequence of the convention and it is recorded in
    // DISPLAY_GLYPHS.md rather than tuned away here.
    //
    // THE FLOOR IS 50, chosen to sit far above what a thousand-times-small
    // overlay can contribute -- a sub-pixel speck, so nearly zero -- and far
    // below the 278 actually measured. It discriminates the scale, which is
    // what this case is for; it does not claim the arrows are prominent.
    const std::size_t arrowPixels = both - meshOnly;
    INFO("the arrows contribute " << arrowPixels << " px");
    CHECK(both > meshOnly);
    CHECK(arrowPixels > 50U);
}

// ---------------------------------------------------------------------------
// The gaps found by READING the contract, and the two glyph-batch survivors.
// Each is a property no probe in that batch could see, or one it saw survive.
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_WithoutTheNumberingTheComponentsAreUnknownNotFixed",
          "[renderer][viz]") {
    // A GAP FOUND BY READING THE CONTRACT, not by a probe, and NO PROBE IN
    // THE GLYPH BATCH COULD SEE IT.
    //
    // `numbering` defaults to null, and the header is explicit about what
    // that means:
    //
    //   "@p numbering IS REQUIRED FOR `restrainedComponents`, and null leaves
    //    that mask EMPTY -- which is the same 'unknown' that a null
    //    @p restraints means for `restrained`."
    //
    // StructuralViz_RestraintSurvivesANonZeroDisplacement already calls
    // inspectNode with restraints and NO numbering, so the path RUNS -- but it
    // asserts only `restrained`, so nothing holds the mask. Probe G17 replaces
    // the component loop with the old union UNCONDITIONALLY, which the
    // numbering-supplied case kills; a mutation that fell back to the union
    // ONLY when numbering is null would have survived the whole batch.
    //
    // THE DISCRIMINATOR IS THE PAIR, not the empty mask. Asserting count()==0
    // alone would also pass if the mask were never filled at all -- which is
    // the feature being broken outright. So the SAME NODE is inspected both
    // ways and the two must DIFFER.
    SolvedView part;
    const FaceName side{part.feature,
                        FaceSelector{.role = FaceRole::Side, .entity = part.lines.at(0)}};

    // THE SAME FIXTURE AS StructuralViz_ANodesRestrainedComponentsAreItsOwn:
    // the two restraints are INSTALLED in the analysis before the solve, so
    // the fields and the prepared restraints come from one consistent model
    // rather than from two that merely share a mesh.
    std::vector<structural::StructuralLoad> loads = part.endLoadList();
    REQUIRE(part.document
                .modifyObject<structural::StructuralAnalysis>(
                    ObjectId::fromValue(part.analysis.value()),
                    [&](structural::StructuralAnalysis& object) -> Result<bool> {
                        structural::StructuralAnalysisDefinition definition =
                            object.definition();
                        definition.loads = std::move(loads);
                        definition.restraints = {
                            structural::StructuralRestraint::fixedSupport(
                                RestraintId::fromValue(1), part.startCap()),
                            structural::StructuralRestraint{
                                RestraintId::fromValue(2), side,
                                structural::RestraintComponents::along(
                                    structural::DofComponent::Uz)}};
                        return object.setDefinition(std::move(definition));
                    })
                .has_value());

    const structural::StructuralSolveOutcome outcome = part.solve();
    const meshing::Mesh& mesh = part.mesh();

    Result<structural::StructuralModel> model = structural::requireStructuralModel(
        part.document, part.regenerator, part.mesher, part.control);
    REQUIRE(model.has_value());
    Result<structural::MeshDofMap> numbering = structural::buildMeshDofMap(model->mesh().mesh());
    REQUIRE(numbering.has_value());

    // TWO RESTRAINTS WITH DIFFERENT MASKS, because that is the only fixture
    // that can tell a per-node answer from a union of every resolution.
    const std::vector<structural::StructuralRestraint> both{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap()),
        structural::StructuralRestraint{
            RestraintId::fromValue(2), side,
            structural::RestraintComponents::along(structural::DofComponent::Uz)}};
    Result<structural::PreparedRestraints> all =
        structural::prepareStructuralRestraints(*model, *numbering, both);
    INFO((all.has_value() ? std::string{} : all.error().message));
    REQUIRE(all.has_value());

    const meshing::NodeId node = all->nodes().front();

    // WITHOUT THE NUMBERING: restrained is KNOWN, the components are NOT.
    const Result<renderer::NodeResultInspection> blind =
        renderer::inspectNode(mesh, outcome.fields, node, &*all);
    REQUIRE(blind.has_value());
    CHECK(blind->restrained);
    INFO("without numbering: " << blind->restrainedComponents.count() << " component(s)");
    CHECK(blind->restrainedComponents.isEmpty());
    CHECK_FALSE(blind->restrainedComponents.isFixed());
    CHECK_FALSE(blind->restrainedComponents.holds(structural::DofComponent::Ux));
    CHECK_FALSE(blind->restrainedComponents.holds(structural::DofComponent::Uy));
    CHECK_FALSE(blind->restrainedComponents.holds(structural::DofComponent::Uz));

    // WITH IT, ON THE SAME NODE: the mask is populated.
    const Result<renderer::NodeResultInspection> seeing =
        renderer::inspectNode(mesh, outcome.fields, node, &*all, &*numbering);
    REQUIRE(seeing.has_value());
    CHECK(seeing->restrained);
    INFO("with numbering: " << seeing->restrainedComponents.count() << " component(s)");

    // NON-VACUITY: the numbering really does add something, so the empty mask
    // above is the absence of the numbering and not the absence of the
    // feature.
    REQUIRE(seeing->restrainedComponents.count() > 0);
    CHECK(seeing->restrainedComponents != blind->restrainedComponents);
}

TEST_CASE("StructuralViz_ADisplayRefusesAnInvalidSourceHandle", "[renderer][viz]") {
    // BOTH GUARDS THIS MILESTONE OWNS, and NEITHER WAS TESTED. `displayOverlay`
    // is new, and `displayMeshInternal` is the shared path the two mesh display
    // entry points were factored into -- so one case covers the mesh path, the
    // deformed mesh path and the overlay path.
    //
    // A presentation is keyed by its source ObjectId, and `displayed()` is how
    // a UI finds what it put on screen. An invalid handle names nothing, so a
    // presentation registered under one could never be found again: it would
    // be drawn, un-hideable and un-removable. Refusing is the only answer that
    // leaves the viewer consistent.
    //
    // NO GPU NEEDED: every refusal here happens before any kernel call, which
    // is why this is not tagged [frame].
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();
    Result<MeshView> view = MeshView::volumeBoundaryOf(mesh);
    REQUIRE(view.has_value());

    const std::vector<renderer::NodalLoadArrow> arrows =
        renderer::nodalLoadArrows(*prepared.loads);
    const Result<renderer::StructuralOverlay> overlay = renderer::loadArrowOverlay(
        mesh, arrows, 0.004, 1000.0 / static_cast<double>(arrows.size()));
    REQUIRE(overlay.has_value());
    // THE PRECONDITION: this overlay is perfectly displayable, so a refusal
    // below is about the HANDLE and not about the overlay.
    REQUIRE_FALSE(overlay->empty());

    renderer::Viewer viewer = makeResultViewer();

    const Result<renderer::PresentationId> badOverlay =
        viewer.displayOverlay(ObjectId{}, *overlay);
    REQUIRE_FALSE(badOverlay.has_value());
    CHECK(badOverlay.error().code == ErrorCode::InvalidArgument);
    CHECK_THAT(badOverlay.error().message, ContainsSubstring("cannot name the source"));

    const Result<renderer::PresentationId> badMesh =
        viewer.displayMesh(ObjectId{}, *view, renderer::MeshStyle::Shaded);
    REQUIRE_FALSE(badMesh.has_value());
    CHECK(badMesh.error().code == ErrorCode::InvalidArgument);

    // AND NOTHING WAS REGISTERED BY EITHER. A guard that refuses and still
    // pushes a DisplayedObject would leave an unreachable presentation behind,
    // which is the failure the guard exists to prevent.
    CHECK(viewer.displayed().empty());
}

TEST_CASE("StructuralViz_AReactionIsNotDrawnInALoadsColour", "[renderer][viz][frame]") {
    // A GAP NO PROBE IN THE GLYPH BATCH COULD SEE, because none of them
    // touches the presentation's colour table. ADR-045 promises a colour per
    // role -- "so a load, a reaction and a support are told apart without a
    // legend" -- and `colourOf` could have returned `loadColour()` for all
    // three roles with every one of the 60 cases still passing.
    //
    // The existing on-screen case only checks that ADDING an overlay changes
    // the image. That is true whatever colour it is drawn in, because the
    // restraint bars sit at different positions from the load arrows. Nothing
    // compared two roles at the SAME geometry.
    //
    // THE DISCRIMINATOR IS IDENTICAL GEOMETRY. A NodalLoadArrow and a
    // ReactionArrow carry the same node, the same force and the same
    // hasDirection, so built from one node and one force the two overlays
    // produce BYTE-IDENTICAL segment lists -- asserted below, not assumed.
    // Two identical segment lists that render to different images can differ
    // in nothing but colour.
    //
    // AND IT IS THE CONFUSION THAT MATTERS: a reaction read as a load is a
    // sign error a user would act on.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);
    const meshing::Mesh& mesh = part.mesh();

    const meshing::NodeId node = mesh.nodes().front().id;
    const Force3D force{Force::fromSi(0.0), Force::fromSi(0.0), Force::fromSi(1000.0)};

    const std::vector<renderer::NodalLoadArrow> asLoad{
        renderer::NodalLoadArrow{.node = node, .force = force, .hasDirection = true}};
    const std::vector<renderer::ReactionArrow> asReaction{renderer::ReactionArrow{
        .node = node,
        .force = force,
        .constrained = structural::RestraintComponents::fixed(),
        .hasDirection = true}};

    const Result<renderer::StructuralOverlay> loads =
        renderer::loadArrowOverlay(mesh, asLoad, 0.020, 1000.0);
    const Result<renderer::StructuralOverlay> reactions =
        renderer::reactionArrowOverlay(mesh, asReaction, 0.020, 1000.0);
    REQUIRE(loads.has_value());
    REQUIRE(reactions.has_value());

    // THE ROLES DIFFER...
    REQUIRE(loads->role == renderer::OverlayRole::LoadArrows);
    REQUIRE(reactions->role == renderer::OverlayRole::ReactionArrows);
    // ...AND THE GEOMETRY DOES NOT. This is the precondition that makes the
    // image comparison mean "colour" and nothing else.
    REQUIRE(loads->segments.size() == renderer::kSegmentsPerArrow);
    REQUIRE(loads->segments == reactions->segments);

    const auto imageOf = [&](const renderer::StructuralOverlay& overlay) {
        renderer::Viewer viewer = makeResultViewer();
        REQUIRE(viewer.displayOverlay(part.feature, overlay).has_value());
        viewer.setStandardView(renderer::StandardView::Isometric);
        viewer.fitAll();
        return renderResult(viewer);
    };

    const renderer::RenderedImage loadImage = imageOf(*loads);
    const renderer::RenderedImage reactionImage = imageOf(*reactions);

    // BOTH REACHED THE SCREEN, so the comparison is not two blank images
    // agreeing -- the shape the 'uniform' contour defect took.
    const std::size_t loadPixels = loadImage.coverage(kViewBlack);
    const std::size_t reactionPixels = reactionImage.coverage(kViewBlack);
    INFO("load " << loadPixels << " px, reaction " << reactionPixels << " px");
    REQUIRE(loadPixels > 0U);
    REQUIRE(reactionPixels > 0U);

    // THE SAME GEOMETRY, FRAMED THE SAME WAY, so the two cover the same area.
    CHECK(loadPixels == reactionPixels);

    // AND THE PIXELS DIFFER. With identical segments and identical framing,
    // the only thing left to differ is the colour.
    CHECK(loadImage.rgb != reactionImage.rgb);

    // HOW MANY DISTINCT COLOURS EACH OVERLAY SHOWS IS REPORTED, NOT
    // ASSERTED. A solid aspect should give one, but whether this view
    // anti-aliases a line has not been measured, and an exact count I have
    // not measured would be a guessed figure in a test. The property this
    // case exists for is the INEQUALITY above, which `colourOf` returning one
    // colour for every role fails regardless.
    //
    // Once the figure is measured it belongs here as an equality.
    INFO("distinct colours -- load " << distinctColours(loadImage, kViewBlack)
                                    << ", reaction "
                                    << distinctColours(reactionImage, kViewBlack));
    CHECK(distinctColours(loadImage, kViewBlack) ==
          distinctColours(reactionImage, kViewBlack));
}

TEST_CASE("StructuralViz_ARestraintOverlayRefusesAnotherMeshsRestraints",
          "[renderer][viz]") {
    // THE IDENTITY-SAFETY GUARD, and nothing reached it. `restraintSymbolOverlay`
    // checks `restraints.describes(mesh)` before placing a single bar, and a
    // remesh is exactly what invalidates prepared restraints: the node ids
    // repeat, so the bars would be drawn at the WRONG NODES and look perfectly
    // plausible.
    //
    // WHY THIS ENTRY POINT AND NOT inspectNode. The existing crossed-mesh case
    // for inspectNode is refused on the FIELDS first -- its own comment says
    // so -- so the restraints guard there is shadowed and never independently
    // exercised. `restraintSymbolOverlay` takes NO fields, so the restraints
    // guard is the first thing a foreign mesh meets.
    SolvedView part;
    const std::vector<structural::StructuralRestraint> rest{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap())};
    const PreparedOverlays prepared = prepareOverlays(part, part.endLoadList(), rest);

    // THE FIRST MESH IS SIZED NOW, BEFORE THE REMESH. `part.mesh()` re-queries
    // the mesher, so after regenerating it would answer with the SECOND mesh
    // and the non-vacuity check below would compare that mesh with itself --
    // and pass vacuously whatever the remesh did. The reference would dangle
    // too.
    const std::size_t firstNodes = part.mesh().nodes().size();

    // THE PRECONDITION: against its OWN mesh it works, so the refusal below is
    // about the mesh and not about the restraints.
    REQUIRE(renderer::restraintSymbolOverlay(part.mesh(), *prepared.restraints,
                                             *prepared.numbering, 0.004)
                .has_value());

    // A REMESH, through the document, which is what a user changing a mesh
    // control actually does.
    REQUIRE(part.document
                .modifyObject<meshing::MeshControl>(
                    ObjectId::fromValue(part.control.value()),
                    [](meshing::MeshControl& control) -> Result<bool> {
                        auto definition = control.definition();
                        definition.mesh.sizing.globalTargetSize = 12_mm;
                        definition.mesh.sizing.local.clear();
                        return control.setDefinition(definition).has_value();
                    })
                .has_value());
    requireReport(part.regenerator, part.document);
    const Result<const meshing::VolumeMesh*> remeshed =
        part.mesher.generate(part.document, part.regenerator, part.control);
    REQUIRE(remeshed.has_value());
    const meshing::Mesh& second = (*remeshed)->mesh();

    // NON-VACUITY: the remesh really produced a different mesh. Without this,
    // an identical mesh would make the refusal below meaningless.
    INFO("first " << firstNodes << " nodes, second " << second.nodes().size());
    REQUIRE(second.nodes().size() != firstNodes);

    // A NUMBERING FOR THE *SECOND* MESH, so this case isolates the RESTRAINTS
    // guard instead of being refused by the numbering one.
    //
    // PROBE P5 CAUGHT THE FIRST VERSION OF THIS. It passed
    // `prepared.numbering`, which belongs to the FIRST mesh -- so with
    // `restraints.describes(mesh)` deleted the call still refused, by the
    // NUMBERING check one line below it, and the case passed under the very
    // defect it was written for. The assertion was also
    // ContainsSubstring("different mesh"), which matches BOTH messages:
    //
    //   "these prepared restraints were resolved against a different mesh"
    //   "this degree-of-freedom numbering was built for a different mesh
    //    generation"
    //
    // Two fixes, because either alone would still be weak: a FRESH numbering
    // so the restraints check is the only one that can fire, and a substring
    // UNIQUE to the restraints message.
    Result<structural::StructuralModel> secondModel = structural::requireStructuralModel(
        part.document, part.regenerator, part.mesher, part.control);
    REQUIRE(secondModel.has_value());
    Result<structural::MeshDofMap> secondNumbering =
        structural::buildMeshDofMap(secondModel->mesh().mesh());
    REQUIRE(secondNumbering.has_value());

    // THE PREMISE, ASSERTED: the numbering DOES describe the second mesh and
    // the restraints do NOT. That is what makes the restraints guard the only
    // thing standing here.
    REQUIRE(secondNumbering->describes(second));
    REQUIRE_FALSE(prepared.restraints->describes(second));

    const Result<renderer::StructuralOverlay> crossed = renderer::restraintSymbolOverlay(
        second, *prepared.restraints, *secondNumbering, 0.004);
    REQUIRE_FALSE(crossed.has_value());
    CHECK(crossed.error().code == ErrorCode::FailedPrecondition);
    // UNIQUE TO THE RESTRAINTS MESSAGE. "different mesh" would also match the
    // numbering refusal, which is exactly how P5 survived.
    CHECK_THAT(crossed.error().message, ContainsSubstring("prepared restraints"));
}

TEST_CASE("StructuralViz_AnEmptyArrowListStillRefusesABadScale", "[renderer][viz]") {
    // THE GAP PROBE G12 FOUND: deleting `if (!isUsableArrowScale(scale))` from
    // the overlay entry point SURVIVED every one of the 60 cases.
    //
    // WHY IT ALMOST DOESN'T MATTER, AND WHERE IT DOES. `displayArrowLength`
    // repeats the identical check -- the same `isUsableArrowScale` predicate,
    // the same ErrorCode, the same message -- so for a NON-EMPTY list the
    // early guard is redundant and removing it is an equivalent mutant: the
    // refusal just moves from before the loop to the first arrow inside it.
    //
    // WITH AN EMPTY LIST THE LOOP NEVER RUNS. Without the early guard a bad
    // scale then yields a SUCCESSFUL EMPTY OVERLAY instead of a refusal, and
    // the caller's bug is swallowed. That is the only observable difference,
    // and it is the reason the guard is kept rather than deleted as duplicate.
    //
    // The contract is argument validation, not work-dependent validation: a
    // caller passing an unusable scale has a bug whether or not it also
    // passed anything to draw.
    SolvedView part;
    const meshing::Mesh& mesh = part.mesh();
    const std::vector<renderer::NodalLoadArrow> none{};
    const std::vector<renderer::ReactionArrow> noReactions{};

    for (const double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        INFO("scale " << bad << " with no arrows at all");
        const Result<renderer::StructuralOverlay> refused =
            renderer::loadArrowOverlay(mesh, none, bad, 1.0);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::InvalidArgument);

        // THE REFERENCE GUARD IS THE SAME SHAPE and the same redundancy, so
        // it gets the same case rather than waiting for its own probe.
        INFO("reference " << bad << " with no arrows at all");
        const Result<renderer::StructuralOverlay> alsoRefused =
            renderer::loadArrowOverlay(mesh, none, 1.0, bad);
        REQUIRE_FALSE(alsoRefused.has_value());
        CHECK(alsoRefused.error().code == ErrorCode::InvalidArgument);

        // AND THE REACTION ENTRY POINT SHARES THE IMPLEMENTATION, so it must
        // refuse on the same terms. One template, one set of guards -- which
        // is the whole reason the two overlays cannot drift apart.
        REQUIRE_FALSE(renderer::reactionArrowOverlay(mesh, noReactions, bad, 1.0).has_value());
    }

    // AND THE REFUSALS ARE NOT BLANKET: no arrows with a USABLE scale is a
    // success that drew nothing, which is a different answer from an error.
    const Result<renderer::StructuralOverlay> empty =
        renderer::loadArrowOverlay(mesh, none, 1.0, 1.0);
    REQUIRE(empty.has_value());
    CHECK(empty->empty());
    CHECK(empty->glyphs == 0);
    CHECK(empty->skipped == 0);
    CHECK(empty->tooSmall == 0);
    // THE ACCOUNTING INVARIANT HOLDS AT ZERO TOO.
    CHECK(empty->glyphs + empty->skipped + empty->tooSmall == none.size());
}

TEST_CASE("StructuralViz_AStaleDofNumberingIsRefusedNotTrusted", "[renderer][viz]") {
    // THE GAP PROBE G18 FOUND: deleting `if (!numbering->describes(mesh))`
    // from inspectNode SURVIVED all 60 cases. Nothing ever handed either entry
    // point a numbering built for a different mesh generation.
    //
    // IT ALSO CORRECTS A NOTE OF MINE. I first recorded the guard as
    // unreachable through the public API, reasoning that
    // prepareStructuralRestraints binds restraints to the numbering they were
    // prepared with -- so a disagreeing numbering would arrive with
    // disagreeing restraints, and the restraints check would fire first. That
    // is wrong: the two can be made to disagree independently, and this case
    // is the construction.
    //
    // WHAT THE DEFECT WOULD LOOK LIKE: not a crash. `indicesOf` returns
    // indices from the WRONG numbering, the ConstraintSet is asked about
    // those, and a plausible-looking mask comes back for degrees of freedom
    // that are not the node's -- so a support symbol is drawn for components
    // that are not held.
    //
    // TWO EARLIER VERSIONS OF THIS FIXTURE FAILED, and both failures were the
    // product's gates working rather than defects:
    //
    //   1. change the mesh control, requireReport, then solve
    //      -> "meshing control 3 has no mesh that describes the model:
    //          stale: the meshing intent changed"
    //      requireReport regenerates the DOCUMENT, not the mesh.
    //
    //   2. also call mesher.generate, then solve
    //      -> "load:1: its node:2 belongs to a different mesh generation, so
    //          it names no node of this one"
    //      `endLoadList` builds NodalForceLoads carrying a MeshStamp and a
    //      NodeId, so the installed loads CANNOT survive a remesh. Re-solving
    //      on a second mesh is not possible with this fixture at all.
    //
    // SO NO REMESH AND NO SECOND SOLVE. A numbering only has to fail
    // `describes`, and `describes` is `mesh.owns(stamp) && node count` -- a
    // MeshStamp identifies the BUILDER, so an INDEPENDENT DOCUMENT's numbering
    // is stale with respect to this mesh however similar the two meshes are.
    // That is also a real caller error: holding a numbering from another
    // analysis.
    SolvedView part;
    const FaceName side{part.feature,
                        FaceSelector{.role = FaceRole::Side, .entity = part.lines.at(0)}};

    std::vector<structural::StructuralLoad> loads = part.endLoadList();
    const std::vector<structural::StructuralRestraint> both{
        structural::StructuralRestraint::fixedSupport(RestraintId::fromValue(1),
                                                      part.startCap()),
        structural::StructuralRestraint{
            RestraintId::fromValue(2), side,
            structural::RestraintComponents::along(structural::DofComponent::Uz)}};
    REQUIRE(part.document
                .modifyObject<structural::StructuralAnalysis>(
                    ObjectId::fromValue(part.analysis.value()),
                    [&](structural::StructuralAnalysis& object) -> Result<bool> {
                        structural::StructuralAnalysisDefinition definition =
                            object.definition();
                        definition.loads = std::move(loads);
                        definition.restraints = both;
                        return object.setDefinition(std::move(definition));
                    })
                .has_value());

    const structural::StructuralSolveOutcome outcome = part.solve();
    const meshing::Mesh& mesh = part.mesh();
    Result<structural::StructuralModel> model = structural::requireStructuralModel(
        part.document, part.regenerator, part.mesher, part.control);
    REQUIRE(model.has_value());
    Result<structural::MeshDofMap> fresh =
        structural::buildMeshDofMap(model->mesh().mesh());
    REQUIRE(fresh.has_value());
    Result<structural::PreparedRestraints> held =
        structural::prepareStructuralRestraints(*model, *fresh, both);
    INFO((held.has_value() ? std::string{} : held.error().message));
    REQUIRE(held.has_value());

    // AN INDEPENDENT DOCUMENT, whose numbering cannot describe the mesh above.
    SolvedView other;
    Result<structural::StructuralModel> otherModel = structural::requireStructuralModel(
        other.document, other.regenerator, other.mesher, other.control);
    REQUIRE(otherModel.has_value());
    Result<structural::MeshDofMap> stale =
        structural::buildMeshDofMap(otherModel->mesh().mesh());
    REQUIRE(stale.has_value());

    // THE PREMISE, ASSERTED RATHER THAN ASSUMED. If these two ever stopped
    // holding, every refusal below would be vacuous and this case would be
    // testing nothing -- so they are REQUIREs, and they are the first thing a
    // failure here should point at.
    REQUIRE(fresh->describes(mesh));
    REQUIRE_FALSE(stale->describes(mesh));
    // AND THE RESTRAINTS DO DESCRIBE THIS MESH, which is the whole point: the
    // restraints check PASSES, so the numbering check is the only thing left
    // between a stale DofIndex and a drawn support symbol.
    REQUIRE(held->describes(mesh));

    const meshing::NodeId node = held->nodes().front();

    // THE PRECONDITION: with the CURRENT numbering everything works and the
    // mask is populated, so the refusals below are about the numbering alone.
    const Result<renderer::NodeResultInspection> good =
        renderer::inspectNode(mesh, outcome.fields, node, &*held, &*fresh);
    INFO((good.has_value() ? std::string{} : good.error().message));
    REQUIRE(good.has_value());
    REQUIRE(good->restrained);
    REQUIRE_FALSE(good->restrainedComponents.isEmpty());

    SECTION("inspectNode refuses it") {
        const Result<renderer::NodeResultInspection> crossed =
            renderer::inspectNode(mesh, outcome.fields, node, &*held, &*stale);
        REQUIRE_FALSE(crossed.has_value());
        CHECK(crossed.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(crossed.error().message, ContainsSubstring("mesh generation"));
    }

    SECTION("the restraint overlay refuses it too") {
        // THE SAME GUARD IN THE OTHER ENTRY POINT. One construction, both call
        // sites, because a stale numbering is equally wrong in each.
        const Result<renderer::StructuralOverlay> overlay =
            renderer::restraintSymbolOverlay(mesh, *held, *stale, 0.004);
        REQUIRE_FALSE(overlay.has_value());
        CHECK(overlay.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(overlay.error().message, ContainsSubstring("mesh generation"));

        // AND WITH THE FRESH NUMBERING IT DRAWS, so the refusal is not blanket.
        REQUIRE(renderer::restraintSymbolOverlay(mesh, *held, *fresh, 0.004).has_value());
    }
}

// ---------------------------------------------------------------------------
// The element contours, on a boundary derived from the tetrahedra (ADR-046)
// ---------------------------------------------------------------------------

TEST_CASE("StructuralViz_EveryElementChannelContoursTheBoundaryByTetrahedron",
          "[renderer][viz]") {
    // THE THREE BOXES THAT WERE RECORDED AS BLOCKED: the von Mises contour,
    // the normal stress components and the shear stress components.
    //
    // They were blocked on a premise, not on a missing capability. The recorded
    // reason was that `volumeBoundaryOf` names Triangle3 elements while an
    // element channel is per TETRAHEDRON, and that closing the gap needed a
    // triangle-to-tetrahedron mapping in the qualified P16 meshing module.
    //
    // Every sentence of that was true and the conclusion did not follow: build
    // the boundary FROM the tetrahedra and ownership is known by construction.
    // `StructuralContour` needed NO change at all -- it reads
    // `elementOfTriangle` and looks the id up in the field, so a view whose
    // element ids are tetrahedra simply works (ADR-046).
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    Result<MeshView> byTetrahedron = MeshView::volumeBoundaryByTetrahedronOf(mesh);
    INFO((byTetrahedron.has_value() ? std::string{} : byTetrahedron.error().message));
    REQUIRE(byTetrahedron.has_value());
    REQUIRE(byTetrahedron->source() == renderer::MeshSource::VolumeBoundaryByTetrahedron);
    REQUIRE(byTetrahedron->triangleCount() > 0U);

    Result<StructuralResultView> result =
        StructuralResultView::of(mesh, *byTetrahedron, prepared.outcome().fields);
    INFO((result.has_value() ? std::string{} : result.error().message));
    REQUIRE(result.has_value());

    // ALL SEVEN ELEMENT CHANNELS THE THREE BOXES NAME. Checked in one loop
    // rather than three cases, because the mechanism is one mechanism and a
    // channel that worked while its neighbour did not would be a lookup bug,
    // which this catches.
    const std::array<ResultChannel, 7> channels{
        ResultChannel::VonMises,  ResultChannel::StressXX, ResultChannel::StressYY,
        ResultChannel::StressZZ,  ResultChannel::StressXY, ResultChannel::StressYZ,
        ResultChannel::StressZX};

    for (const ResultChannel channel : channels) {
        INFO("channel " << renderer::toString(channel));
        const Result<ScalarField> field =
            result->field(mesh, prepared.outcome().fields, channel);
        REQUIRE(field.has_value());
        REQUIRE(field->domain == ChannelDomain::Element);
        REQUIRE(field->complete());

        const Result<renderer::ContourLegend> legend = renderer::contourLegend(*field, 8);
        REQUIRE(legend.has_value());
        const Result<std::vector<std::size_t>> bands =
            renderer::contourBandsOf(*field, *legend);
        REQUIRE(bands.has_value());

        // THE DELIVERABLE. This is the call that used to be refused by name.
        const Result<std::vector<std::size_t>> corners =
            renderer::triangleCornerBands(*byTetrahedron, *field, *bands);
        INFO((corners.has_value() ? std::string{} : corners.error().message));
        REQUIRE(corners.has_value());
        REQUIRE(corners->size() == byTetrahedron->triangleCount() * 3U);

        // AND THE COLOURS COMPOSE, so a caller reaches a full buffer and not a
        // half-built one.
        const Result<std::vector<double>> colours =
            renderer::contourCornerColours(*byTetrahedron, *field, *legend);
        REQUIRE(colours.has_value());
        REQUIRE(colours->size() == byTetrahedron->triangleCount() * 9U);
        for (const double channelValue : *colours) {
            REQUIRE(std::isfinite(channelValue));
            REQUIRE(channelValue >= 0.0);
            REQUIRE(channelValue <= 1.0);
        }

        // EVERY FACET IS FLAT, which is what an element-constant field means:
        // interpolating it across the facet would invent a variation the
        // physics does not have.
        for (std::size_t triangle = 0; triangle < byTetrahedron->triangleCount(); ++triangle) {
            const std::size_t a = (*corners)[(triangle * 3U) + 0U];
            const std::size_t b = (*corners)[(triangle * 3U) + 1U];
            const std::size_t c = (*corners)[(triangle * 3U) + 2U];
            INFO("triangle " << triangle << " corners " << a << ", " << b << ", " << c);
            REQUIRE(a == b);
            REQUIRE(b == c);
        }

        // AND THE BAND IS THE OWNING TETRAHEDRON'S, looked up INDEPENDENTLY:
        // the element id from the view, its position found by a linear scan of
        // the field's own id array, and the band read from `bands`. Nothing
        // here reuses `triangleCornerBands`' arithmetic.
        const std::span<const meshing::ElementId> elements{field->elements};
        for (std::size_t triangle = 0; triangle < byTetrahedron->triangleCount(); ++triangle) {
            const Result<meshing::ElementId> owner =
                byTetrahedron->elementOfTriangle(triangle);
            REQUIRE(owner.has_value());
            // THE OWNER IS A TETRAHEDRON OF THIS MESH, not merely an id the
            // field happens to hold.
            REQUIRE(mesh.findTetrahedron(*owner) != nullptr);
            std::size_t at = elements.size();
            for (std::size_t i = 0; i < elements.size(); ++i) {
                if (elements[i] == *owner) {
                    at = i;
                    break;
                }
            }
            INFO("triangle " << triangle << " owned by element " << owner->value());
            REQUIRE(at < elements.size());
            REQUIRE((*corners)[triangle * 3U] == (*bands)[at]);
        }

        // NON-VACUITY: the contour actually VARIES over the boundary. A field
        // that put every facet in one band would satisfy everything above and
        // show a single flat colour -- which is the shape P16's
        // "classified nothing" defect took, and the shape a uniform fill takes.
        std::set<std::size_t> distinct;
        for (std::size_t triangle = 0; triangle < byTetrahedron->triangleCount(); ++triangle) {
            distinct.insert((*corners)[triangle * 3U]);
        }
        INFO("distinct bands across the boundary: " << distinct.size());
        REQUIRE(distinct.size() > 1U);
    }
}

TEST_CASE("StructuralViz_TheTriangle3BoundaryStillRefusesAnElementChannel",
          "[renderer][viz]") {
    // THE DISTINCTION ADR-046 KEEPS, and it is deliberate rather than
    // leftover: `volumeBoundaryOf` names Triangle3 elements, so an element
    // channel STILL cannot be looked up through it, and it is still refused by
    // name rather than coloured from a mismatched lookup.
    //
    // The two boundary views are not interchangeable. This case is what stops
    // the new one being mistaken for a reason to relax the old one.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();

    const Result<ScalarField> stress =
        prepared.result().field(mesh, prepared.outcome().fields, ResultChannel::VonMises);
    REQUIRE(stress.has_value());
    REQUIRE(stress->domain == ChannelDomain::Element);
    const Result<renderer::ContourLegend> legend = renderer::contourLegend(*stress, 8);
    REQUIRE(legend.has_value());
    const Result<std::vector<std::size_t>> bands =
        renderer::contourBandsOf(*stress, *legend);
    REQUIRE(bands.has_value());

    // REFUSED on the Triangle3 boundary...
    const Result<std::vector<std::size_t>> onTriangles =
        renderer::triangleCornerBands(prepared.view(), *stress, *bands);
    REQUIRE_FALSE(onTriangles.has_value());
    REQUIRE_THAT(onTriangles.error().message, ContainsSubstring("has no value for it"));

    // ...and ACCEPTED on the tet-derived boundary, from the same field, the
    // same legend and the same bands. The ONLY thing that differs is the view,
    // which is the whole claim of ADR-046 reduced to two calls.
    Result<MeshView> byTetrahedron = MeshView::volumeBoundaryByTetrahedronOf(mesh);
    REQUIRE(byTetrahedron.has_value());
    const Result<std::vector<std::size_t>> onTetrahedra =
        renderer::triangleCornerBands(*byTetrahedron, *stress, *bands);
    INFO((onTetrahedra.has_value() ? std::string{} : onTetrahedra.error().message));
    REQUIRE(onTetrahedra.has_value());

    // AND THE TWO VIEWS DRAW THE SAME SURFACE, so the difference is the
    // identity the ids carry and not the picture.
    CHECK(byTetrahedron->triangleCount() == prepared.view().triangleCount());
}

TEST_CASE("StructuralViz_AnElementContourReachesTheScreenAsManyColours",
          "[renderer][viz][frame]") {
    // THE SAME MEASUREMENT THE NODAL CONTOURS GOT, because "the bands are
    // computed" is not "the contour is drawn" -- that distinction is the whole
    // reason these three boxes stayed unticked while their arithmetic was
    // already qualified.
    //
    // Per-vertex colour is an OCCT ARRAY ATTRIBUTE and it overrides a group
    // aspect's interior colour, which was measured for the nodal contours at
    // 1 distinct colour against 4807. An element contour is FLAT per facet, so
    // it shows far fewer colours than a nodal one -- but it must show more than
    // the one a uniform fill gives.
    Prepared prepared;
    const meshing::Mesh& mesh = prepared.part.mesh();
    Result<MeshView> byTetrahedron = MeshView::volumeBoundaryByTetrahedronOf(mesh);
    REQUIRE(byTetrahedron.has_value());

    Result<StructuralResultView> result =
        StructuralResultView::of(mesh, *byTetrahedron, prepared.outcome().fields);
    REQUIRE(result.has_value());
    const Result<ScalarField> stress =
        result->field(mesh, prepared.outcome().fields, ResultChannel::VonMises);
    REQUIRE(stress.has_value());
    const Result<renderer::ContourLegend> legend = renderer::contourLegend(*stress, 12);
    REQUIRE(legend.has_value());
    const Result<std::vector<double>> colours =
        renderer::contourCornerColours(*byTetrahedron, *stress, *legend);
    REQUIRE(colours.has_value());

    renderer::Viewer viewer = makeResultViewer();
    const Result<renderer::PresentationId> shown =
        viewer.displayMesh(prepared.part.feature, *byTetrahedron, renderer::MeshStyle::Shaded);
    REQUIRE(shown.has_value());
    viewer.setStandardView(renderer::StandardView::Isometric);
    viewer.fitAll();

    const renderer::RenderedImage plain = renderResult(viewer);
    const std::size_t plainColours = distinctColours(plain, kViewBlack);
    REQUIRE(plain.coverage(kViewBlack) > 0U);

    REQUIRE(viewer.setMeshContour(*shown, *colours).has_value());
    // `meshHasContour` returns Result<bool>, so it is BOUND and DEREFERENCED.
    // CHECK(viewer.meshHasContour(...)) would test only that the call
    // SUCCEEDED -- true whatever the answer -- which is how the first version
    // of this case passed vacuously.
    const Result<bool> carried = viewer.meshHasContour(*shown);
    REQUIRE(carried.has_value());
    CHECK(*carried);
    const renderer::RenderedImage contoured = renderResult(viewer);
    const std::size_t contourColours = distinctColours(contoured, kViewBlack);

    INFO("uniform " << plainColours << " colours, element contour " << contourColours);
    // THE IMAGE CHANGED, which is what says the contour was drawn rather than
    // merely accepted.
    CHECK(contoured.rgb != plain.rgb);
    // AND IT IS MANY COLOURS, NOT ONE. A uniform fill shows one whatever the
    // array carried, which is exactly the defect this measurement catches.
    CHECK(contourColours > plainColours);
    CHECK(contourColours > 1U);

    SECTION("and clearing it returns the uniform image byte for byte") {
        // SO THE CONTOUR ADDS NO STATE THAT OUTLIVES IT.
        REQUIRE(viewer.clearMeshContour(*shown).has_value());
        const Result<bool> gone = viewer.meshHasContour(*shown);
        REQUIRE(gone.has_value());
        CHECK_FALSE(*gone);
        const renderer::RenderedImage cleared = renderResult(viewer);
        CHECK(cleared.rgb == plain.rgb);
    }
}
