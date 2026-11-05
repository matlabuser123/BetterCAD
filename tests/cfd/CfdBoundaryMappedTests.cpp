// P19-BC-001: the CFD boundary conditions resolved through P16's own mapping on
// real meshes (ADR-067). THE AUTHORITY TESTS.
//
// WHY THIS FILE IS SEPARATE FROM CfdBoundaryTests.cpp. The claims here are
// about NAMING AND ATTRIBUTION, and no hand-built target can make them: that a
// `FaceName` resolves to the facets of the face it names, that a reference to a
// face the model no longer has stays unresolved and is never rebound to
// something nearby, that a drilled wall carrying no name refuses the analysis,
// and that a remesh re-resolves everything. The companion file drives the same
// `bindTargets` pass through the plain seam, where the refusals a document
// cannot produce can be presented directly. Neither replaces the other.
//
// THE ORACLES ARE INDEPENDENT WHERE THEY CAN BE. A facet's centroid is
// recomputed here from the MESH's own nodes rather than read from `CfdGeometry`,
// so the hydrostatic pressure is compared against a quantity the production
// path did not produce. The datum rule is compared against
// `pressureDatumProblem`, which is P19-DATA-001's own implementation of
// ADR-058's rule and not this milestone's.
//
// TWO TOLERANCE CLASSES, KEPT APART. An algebraic identity -- the hydrostatic
// formula at a given centroid -- is held to a few ulps. A statement about where
// a meshed facet sits relative to the CAD face carries P16's geometric
// tolerance. Conflating them would blame the boundary conversion for meshing's
// approximation.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"
#include "reference/MeshTestSupport.hpp"

#include <MeshReferenceModels.hpp>
#include <bettercad/cfd/CfdAnalysis.hpp>
#include <bettercad/cfd/CfdAnalysisObject.hpp>
#include <bettercad/cfd/CfdBoundary.hpp>
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdMesh.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using bettercad::reference::MeshReferenceModelKind;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using cfd::BoundaryFaceIndex;
using cfd::CfdAnalysisDefinition;
using cfd::CfdBoundaryCondition;
using cfd::CfdBoundaryConditionKind;
using cfd::CfdBoundaryPolicy;
using cfd::CfdBoundaryProblem;
using cfd::CfdBoundarySource;
using cfd::CfdBoundaryStaleReason;
using cfd::CfdGeometry;
using cfd::CfdGravity;
using cfd::CfdReferencePressure;
using cfd::CfdTopology;
using cfd::MovingWall;
using cfd::NoSlipWall;
using cfd::PressureOutlet;
using cfd::SlipWall;
using cfd::Velocity3;
using cfd::VelocityInlet;
using meshing::ElementId;

namespace {

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

/// A 40 x 30 x 20 mm channel: a rectangle extruded along +Z, so its two caps lie
/// at z = 0 and z = 20 mm and its four sides are vertical. EVERY CAD FACE IS
/// NAMED, which is what makes full coverage reachable, and the control is named
/// "Mesh" so `MeshedReference` adopts it.
Document channelDocument(Length depth = 20_mm) {
    Document document{"CfdChannel"};
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    (void)addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Fluid", {.profile = SketchId::fromValue(profile.value()), .depth = depth});
    REQUIRE(extrude.has_value());
    const ObjectId solid = require(document.addObject(std::move(*extrude)));
    meshing::MeshControlDefinition control;
    control.body = solid;
    auto intent = meshing::MeshControl::create("Mesh", control);
    REQUIRE(intent.has_value());
    (void)require(document.addObject(std::move(*intent)));
    return document;
}

/// The same block with a 12 mm hole drilled through it: P16's own case of an
/// UNNAMED face, because `cutHole` names a hole's flat faces and not its
/// cylindrical wall.
Document boredDocument() {
    Document document{"CfdBored"};
    auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
    (void)addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
    const ObjectId profile = require(document.addObject(std::move(sketch)));
    auto extrude = features::ExtrudeFeature::create(
        "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 10_mm});
    REQUIRE(extrude.has_value());
    const ObjectId solid = require(document.addObject(std::move(*extrude)));
    const features::HoleDefinition hole{
        .target = FeatureId::fromValue(solid.value()),
        .face = geometry::planeSignature(Point3D{0_mm, 0_mm, 0_mm}, Direction3D::unitZ().reversed()),
        .center = Point2D{20_mm, 15_mm},
        .extent = geometry::HoleExtent::Through,
        .diameter = 12_mm};
    auto bore = features::HoleFeature::create("Bore", hole);
    REQUIRE(bore.has_value());
    const ObjectId drilled = require(document.addObject(std::move(*bore)));
    meshing::MeshControlDefinition control;
    control.body = drilled;
    auto intent = meshing::MeshControl::create("Mesh", control);
    REQUIRE(intent.has_value());
    (void)require(document.addObject(std::move(*intent)));
    return document;
}

/// A meshed fluid prepared the whole production way: input, mesh contract,
/// topology, geometry. The ordinary path, re-run by `prepare()` after an edit.
struct Fluid {
    static Error notPrepared() {
        return Error{ErrorCode::FailedPrecondition, "not prepared yet"};
    }

    meshref::MeshedReference model;
    MaterialId material{};
    ObjectId body{};
    Result<cfd::CfdModel> input{std::unexpected(notPrepared())};
    Result<cfd::CfdMeshContract> contract{std::unexpected(notPrepared())};
    Result<CfdTopology> topology{std::unexpected(notPrepared())};
    Result<CfdGeometry> geometry{std::unexpected(notPrepared())};

    explicit Fluid(Document built) : model(std::move(built)) {
        assignWater(model.document(), material);
        (void)model.regenerate();
        body = model.body();
        (void)model.require();
        prepare();
    }

    explicit Fluid(MeshReferenceModelKind kind) : Fluid(referenceDocument(kind)) {}

    static Document referenceDocument(MeshReferenceModelKind kind) {
        Result<Document> built = bettercad::reference::buildMeshReferenceModel(kind);
        REQUIRE(built.has_value());
        return std::move(*built);
    }

    void prepare() {
        input = cfd::requireCfdModel(model.document(), model.regenerator(), model.mesher(),
                                     model.control());
        INFO((input.has_value() ? std::string{} : input.error().message));
        REQUIRE(input.has_value());
        contract = cfd::requireCfdMesh(*input);
        INFO((contract.has_value() ? std::string{} : contract.error().message));
        REQUIRE(contract.has_value());
        topology = cfd::requireCfdTopology(*input, *contract);
        INFO((topology.has_value() ? std::string{} : topology.error().message));
        REQUIRE(topology.has_value());
        geometry = cfd::requireCfdGeometry(*input, *topology);
        INFO((geometry.has_value() ? std::string{} : geometry.error().message));
        REQUIRE(geometry.has_value());
    }

    [[nodiscard]] const meshing::Mesh& mesh() const { return input->mesh().mesh(); }

    [[nodiscard]] Result<cfd::PreparedCfdBoundary> boundary(const CfdAnalysisDefinition& d) {
        return cfd::prepareCfdBoundary(*input, *topology, *geometry, d);
    }
    [[nodiscard]] std::optional<CfdBoundaryProblem> problem(const CfdAnalysisDefinition& d) {
        return cfd::cfdBoundaryProblem(*input, *topology, *geometry, d);
    }
    [[nodiscard]] CfdBoundarySource sourceFor(const CfdAnalysisDefinition& d) {
        return cfd::currentBoundarySource(*input, *topology, d);
    }

    void setDensity(Density density) {
        materials::MechanicalProperties mechanical =
            features::findMaterial(model.document(), material)->definition().mechanical;
        mechanical.density = materials::MaterialProperty<Density>::known(density);
        REQUIRE(features::setMaterialMechanical(model.document(), material, mechanical).has_value());
    }

    /// The centroid of @p facet, recomputed from the MESH's own nodes. An
    /// INDEPENDENT oracle: `CfdGeometry::faceCentroid` is what the production
    /// path used, so comparing against it would compare a value with itself.
    [[nodiscard]] Point3D centroidOf(ElementId facet) const {
        const meshing::Triangle* triangle = mesh().findTriangle(facet);
        REQUIRE(triangle != nullptr);
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        for (const meshing::NodeId node : triangle->nodes) {
            const meshing::Node* found = mesh().findNode(node);
            REQUIRE(found != nullptr);
            x += found->position.x.si();
            y += found->position.y.si();
            z += found->position.z.si();
        }
        return Point3D{Length::fromSi(x / 3.0), Length::fromSi(y / 3.0), Length::fromSi(z / 3.0)};
    }
};

/// The six names of a channel's body.
struct ChannelFaces {
    ObjectId feature{};
    std::array<EntityId, 4> lines{};

    [[nodiscard]] FaceName startCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}};
    }
    [[nodiscard]] FaceName endCap() const {
        return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}};
    }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(which)}};
    }
};

/// The names of the channel's faces, read off the MAP rather than reconstructed:
/// every mapped face carries its names, and a channel's six are all named.
ChannelFaces facesOf(const Fluid& fluid) {
    ChannelFaces faces;
    faces.feature = fluid.body;
    std::size_t next = 0;
    for (const meshing::MappedFace& face : fluid.input->map().faces()) {
        for (const FaceName& name : face.names) {
            if (name.face.role == FaceRole::Side && name.face.entity.has_value() && next < 4) {
                faces.lines[next++] = *name.face.entity;
            }
        }
    }
    REQUIRE(next == 4);
    return faces;
}

/// The `FaceName` of the side all of whose facets satisfy @p onPlane.
///
/// CHOSEN BY GEOMETRY, NOT BY LIST ORDER. An earlier draft took `side(0)` and
/// then reasoned that it "is the y = 0 edge" -- but `map().faces()` is
/// `geometry::listFaces(body)` order, which `GeometryMeshMap.hpp` says carries
/// no meaning, so a kernel or feature-ordering change would have moved the face
/// under the test while it kept passing for the wrong reason. A review caught
/// it. This asks the mesh instead.
template <typename Predicate>
FaceName sideWhere(const Fluid& fluid, Predicate onPlane) {
    for (const meshing::MappedFace& face : fluid.input->map().faces()) {
        if (face.names.empty() || face.facets.empty()) {
            continue;
        }
        bool all = true;
        for (const ElementId facet : face.facets) {
            const meshing::Triangle* triangle = fluid.mesh().findTriangle(facet);
            REQUIRE(triangle != nullptr);
            for (const meshing::NodeId node : triangle->nodes) {
                const meshing::Node* found = fluid.mesh().findNode(node);
                REQUIRE(found != nullptr);
                if (!onPlane(found->position)) {
                    all = false;
                    break;
                }
            }
            if (!all) {
                break;
            }
        }
        if (all) {
            return face.names.front();
        }
    }
    FAIL("no mapped face lies wholly in the requested plane");
    return FaceName{};
}

/// A channel: water enters through the start cap, leaves through the end cap at
/// 0 Pa, and the four sides are no-slip walls. SIX CONDITIONS OVER SIX NAMED
/// FACES, which is full coverage, and the outlet fixes the pressure.
CfdAnalysisDefinition channelDefinition(const Fluid& fluid, const ChannelFaces& faces) {
    CfdAnalysisDefinition definition{.mesh = fluid.model.control()};
    definition.boundaryConditions = {
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(1),
                             VelocityInlet{faces.startCap(), 0.1_m_per_s}},
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(2),
                             PressureOutlet{faces.endCap(), 0_Pa}},
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(3), NoSlipWall{faces.side(0)}},
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(4), NoSlipWall{faces.side(1)}},
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(5), NoSlipWall{faces.side(2)}},
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(6), NoSlipWall{faces.side(3)}},
    };
    return definition;
}

} // namespace

// ---------------------------------------------------------------------------
// The whole boundary, covered exactly once
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_EveryFacetOfTheChannelCarriesExactlyOneCondition", "[cfd][bc][mapping]") {
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    const CfdAnalysisDefinition definition = channelDefinition(fluid, faces);

    const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
    INFO((boundary.has_value() ? std::string{} : boundary.error().message));
    REQUIRE(boundary.has_value());

    // Every boundary face of the topology has a record, and the records partition
    // the boundary: the per-condition counts sum to the whole, once each.
    REQUIRE(boundary->boundaryFaces() == fluid.topology->boundaryFaces());
    REQUIRE(boundary->cardinality().size() == 6);

    std::size_t assigned = 0;
    for (const cfd::CfdBoundaryCardinality& record : boundary->cardinality()) {
        INFO(record.condition.value());
        CHECK(record.agrees());
        assigned += record.assigned;

        // THE COUNT THAT CAN DISAGREE. `expected == assigned` is an invariant
        // the binding maintains -- a review showed it cannot fail -- so what
        // carries the claim is the REVERSE reading, through `sourceFaceOf`,
        // which is the other direction of P16's map. On the staged path it is
        // always present.
        REQUIRE(record.attributed.has_value());
        CHECK(*record.attributed == record.expected);
    }
    CHECK(assigned == fluid.topology->boundaryFaces());

    // And the forward count, asked again directly of P16, agrees condition by
    // condition -- which is what makes `agrees()` above non-vacuous.
    for (std::size_t i = 0; i < definition.boundaryConditions.size(); ++i) {
        const Result<std::vector<ElementId>> facets =
            cfd::resolveFaceTarget(fluid.input->map(), definition.boundaryConditions[i].face());
        REQUIRE(facets.has_value());
        CHECK(facets->size() == boundary->cardinality()[i].expected);
    }

    // Each record names the condition that actually claims that facet.
    for (std::size_t b = 0; b < boundary->boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const std::optional<cfd::CfdBoundaryFace> record = boundary->face(boundaryFace);
        REQUIRE(record.has_value());
        CHECK(record->condition.isValid());
        const ElementId facet = fluid.topology->triangleOf(boundaryFace);
        const Result<meshing::FacetSource> source =
            meshing::sourceFaceOf(fluid.input->map(), facet);
        REQUIRE(source.has_value());
        // The facet's CAD face carries the name the claiming condition targets.
        bool named = false;
        for (const CfdBoundaryCondition& condition : definition.boundaryConditions) {
            if (condition.id() != record->condition) {
                continue;
            }
            named = std::ranges::find(source->names, condition.face()) != source->names.end();
        }
        CHECK(named);
    }
}

TEST_CASE("CfdBoundary_TheChannelHasSeveralPatchesWithDifferentConditions", "[cfd][bc][mapping]") {
    // "Multiple boundary patches": six patches, six conditions, three kinds, and
    // the kinds land where they were named rather than all on one patch.
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    const Result<cfd::PreparedCfdBoundary> boundary =
        fluid.boundary(channelDefinition(fluid, faces));
    REQUIRE(boundary.has_value());
    REQUIRE(fluid.contract->patches() == 6);
    REQUIRE(fluid.contract->unnamedPatches() == 0);

    std::set<CfdBoundaryConditionKind> kinds;
    std::set<std::uint64_t> conditions;
    for (std::size_t b = 0; b < boundary->boundaryFaces(); ++b) {
        const std::optional<cfd::CfdBoundaryFace> record =
            boundary->face(BoundaryFaceIndex::fromValue(b));
        REQUIRE(record.has_value());
        kinds.insert(record->kind);
        conditions.insert(record->condition.value());
    }
    CHECK(kinds.size() == 3); // inlet, outlet, wall
    CHECK(conditions.size() == 6);
}

// ---------------------------------------------------------------------------
// Naming and attribution
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_ADrilledWallWithNoNameRefusesTheAnalysis", "[cfd][bc][mapping]") {
    // CfdMesh.hpp:222's forward promise, kept here: the hole's cylindrical wall
    // is MAPPED but carries no name, so no geometry reference can reach it and
    // coverage is impossible.
    Fluid fluid(boredDocument());
    REQUIRE(fluid.input->map().report().complete());
    REQUIRE(fluid.input->map().report().unnamedFaceCount == 1);
    REQUIRE(fluid.contract->unnamedPatches() == 1);

    // Name every face that HAS a name, so the only gap is the unnamed wall. The
    // first is a pressure outlet, so the datum is not what refuses.
    CfdAnalysisDefinition definition{.mesh = fluid.model.control()};
    std::uint64_t next = 1;
    for (const meshing::MappedFace& face : fluid.input->map().faces()) {
        if (face.names.empty()) {
            continue;
        }
        const CfdBoundaryConditionId id = CfdBoundaryConditionId::fromValue(next++);
        if (definition.boundaryConditions.empty()) {
            definition.boundaryConditions.push_back(
                CfdBoundaryCondition{id, PressureOutlet{face.names.front(), 0_Pa}});
        } else {
            definition.boundaryConditions.push_back(
                CfdBoundaryCondition{id, NoSlipWall{face.names.front()}});
        }
    }
    REQUIRE_FALSE(definition.boundaryConditions.empty());

    CHECK(fluid.problem(definition) == CfdBoundaryProblem::UnnamedPatch);
    const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
    REQUIRE_FALSE(boundary.has_value());
    CHECK_THAT(boundary.error().message, ContainsSubstring("carry no face name"));
}

TEST_CASE("CfdBoundary_AnUnresolvedReferenceIsRefusedAndNothingNearbyIsSubstituted",
          "[cfd][bc][mapping]") {
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);

    SECTION("a reference to another body does not fall back to this one") {
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        const FaceName foreign{ObjectId::fromValue(9999),
                               FaceSelector{.role = FaceRole::StartCap}};
        definition.boundaryConditions[0] = CfdBoundaryCondition{
            CfdBoundaryConditionId::fromValue(1), VelocityInlet{foreign, 0.1_m_per_s}};

        CHECK(fluid.problem(definition) == CfdBoundaryProblem::TargetUnresolved);
        const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
        REQUIRE_FALSE(boundary.has_value());
        CHECK_THAT(boundary.error().message,
                   ContainsSubstring("nothing nearby is substituted"));
        CHECK(boundary.error().code == ErrorCode::NotFound);
    }

    SECTION("a side reference naming an entity the sketch does not have") {
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        const FaceName absent{fluid.body, FaceSelector{.role = FaceRole::Side,
                                                       .entity = EntityId::fromValue(4242)}};
        definition.boundaryConditions[2] =
            CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(3), NoSlipWall{absent}};
        CHECK(fluid.problem(definition) == CfdBoundaryProblem::TargetUnresolved);
    }
}

TEST_CASE("CfdBoundary_AMalformedSelectorIsAConditionFaultAndNotATargetFault",
          "[cfd][bc][mapping]") {
    // WHY THERE IS NO `TargetInvalid`. P16 reports a malformed selector as
    // `TargetProblem::SelectorInvalid`, and that value IS reachable -- asserted
    // here directly. But the gate never sees it: `validate(condition)` applies
    // core's own `validate(FaceSelector)` first, so the same condition is a
    // `ConditionInvalid` carrying P19-DATA-001's message. An adversarial pass
    // found the enumerator dead and it was removed; this is what keeps the
    // claim true rather than asserted in a comment.
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);

    // A side face names an entity. One that does not is malformed on its own
    // terms, which is core's rule and not a second opinion.
    const FaceName malformed{fluid.body, FaceSelector{.role = FaceRole::Side}};
    REQUIRE_FALSE(bettercad::validate(malformed.face).has_value());

    // P16's neutral answer, reached directly.
    CHECK(cfd::faceTargetProblem(fluid.input->map(), malformed) ==
          cfd::TargetProblem::SelectorInvalid);
    const Result<std::vector<ElementId>> refused =
        cfd::resolveFaceTarget(fluid.input->map(), malformed);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::InvalidArgument);

    // The gate's answer for the same face: a condition fault, decided before
    // any target is resolved.
    CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
    definition.boundaryConditions[2] =
        CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(3), NoSlipWall{malformed}};
    REQUIRE_FALSE(cfd::validate(definition.boundaryConditions[2]).has_value());
    CHECK(fluid.problem(definition) == CfdBoundaryProblem::ConditionInvalid);
}

TEST_CASE("CfdBoundary_AConditionFaultIsReportedBeforeAnUnresolvableTarget",
          "[cfd][bc][mapping]") {
    // THE PHASE ORDER, MEASURED. An adversarial pass found the gate resolving
    // targets before validating conditions, so a definition with both faults
    // reported the target while the header claimed the opposite. With both
    // present the CONDITION is what is reported.
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    CfdAnalysisDefinition definition = channelDefinition(fluid, faces);

    // Fault 1: an unresolvable target on one condition.
    const FaceName foreign{ObjectId::fromValue(9999), FaceSelector{.role = FaceRole::StartCap}};
    definition.boundaryConditions[0] = CfdBoundaryCondition{
        CfdBoundaryConditionId::fromValue(1), VelocityInlet{foreign, 0.1_m_per_s}};
    REQUIRE(fluid.problem(definition) == CfdBoundaryProblem::TargetUnresolved);

    // Fault 2, added on top: an invalid value on another condition.
    definition.boundaryConditions[3] = CfdBoundaryCondition{
        CfdBoundaryConditionId::fromValue(4),
        VelocityInlet{faces.side(1), Velocity::fromSi(-1.0)}};
    REQUIRE_FALSE(cfd::validate(definition.boundaryConditions[3]).has_value());
    CHECK(fluid.problem(definition) == CfdBoundaryProblem::ConditionInvalid);
}

TEST_CASE("CfdBoundary_PartialCoverageAndDoubleCoverageAreBothRefused", "[cfd][bc][mapping]") {
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);

    SECTION("a wall left off is a facet with no condition") {
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        definition.boundaryConditions.pop_back();
        CHECK(fluid.problem(definition) == CfdBoundaryProblem::FacetWithoutCondition);
        const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
        REQUIRE_FALSE(boundary.has_value());
        CHECK_THAT(boundary.error().message, ContainsSubstring("nothing prescribes"));
    }

    SECTION("a second condition on a face already covered is a facet claimed twice") {
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        definition.boundaryConditions.push_back(CfdBoundaryCondition{
            CfdBoundaryConditionId::fromValue(7), SlipWall{faces.side(0)}});
        // The definition itself accepts this: whether two FaceNames overlap is a
        // question about the geometry (CfdAnalysisObject.cpp:150).
        REQUIRE(cfd::validate(definition).has_value());
        CHECK(fluid.problem(definition) == CfdBoundaryProblem::FacetClaimedTwice);
        const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
        REQUIRE_FALSE(boundary.has_value());
        CHECK_THAT(boundary.error().message, ContainsSubstring("claimed by both"));
    }
}

// ---------------------------------------------------------------------------
// The prescribed values
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_AHydrostaticBoundaryPressureMatchesAnIndependentCentroid",
          "[cfd][bc][datum]") {
    // GRAVITY ALONG -X, so the height IS the x coordinate and the end cap --
    // which spans x in [0, 40] mm -- is NOT level. A face that was level would
    // make the hydrostatic term constant and the test vacuous.
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
    definition.gravity =
        CfdGravity{.acceleration = {.x = -9.81_m_per_s2, .y = 0_m_per_s2, .z = 0_m_per_s2},
                   .referenceHeight = 0_m};
    const Pressure prescribed = 0_Pa;

    const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
    INFO((boundary.has_value() ? std::string{} : boundary.error().message));
    REQUIRE(boundary.has_value());

    const double rho = fluid.input->density().si();
    const double g = 9.81;
    std::size_t outlets = 0;
    // SEEDED FROM THE FIRST RECORD, not from zero. An earlier draft seeded both
    // to 0.0, and because every outlet pressure in this fixture is negative
    // `highest` never moved -- so the guard measured the distance from zero and
    // a perfectly uniform field would have passed it. A review caught that.
    double lowest = std::numeric_limits<double>::infinity();
    double highest = -std::numeric_limits<double>::infinity();
    double lowestHeight = std::numeric_limits<double>::infinity();
    double highestHeight = -std::numeric_limits<double>::infinity();
    for (std::size_t b = 0; b < boundary->boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const std::optional<cfd::CfdBoundaryFace> record = boundary->face(boundaryFace);
        REQUIRE(record.has_value());
        if (record->kind != CfdBoundaryConditionKind::PressureOutlet) {
            CHECK_FALSE(record->pressure.has_value());
            continue;
        }
        ++outlets;
        REQUIRE(record->pressure.has_value());

        // THE INDEPENDENT ORACLE: the centroid from the mesh's own nodes, and
        // the formula written out again. h = x, because g_hat = -X.
        const Point3D centroid = fluid.centroidOf(fluid.topology->triangleOf(boundaryFace));
        const double h = centroid.x.si();
        const double expected = prescribed.si() - rho * g * (h - 0.0);
        CHECK_THAT(record->pressure->si(), WithinRel(expected, 1e-12));
        lowest = std::min(lowest, record->pressure->si());
        highest = std::max(highest, record->pressure->si());
        lowestHeight = std::min(lowestHeight, h);
        highestHeight = std::max(highestHeight, h);
    }
    REQUIRE(outlets > 1);
    // NOT CONSTANT over the face, which is the whole point: a genuine spread
    // between the extreme facets, both taken from the data.
    REQUIRE(std::isfinite(lowest));
    REQUIRE(std::isfinite(highest));
    CHECK(highest > lowest);

    // THE BOUND COMES FROM THE DATA, not from the body's extent. An earlier
    // draft compared the pressure spread against `rho g 0.040`, the channel's
    // whole length along x -- but a facet CENTROID sits inside its facet, so the
    // centroids never reach the extremes and the real spread is a fraction of
    // that. Measuring the heights too makes the statement exact instead of
    // approximate.
    REQUIRE(std::isfinite(lowestHeight));
    REQUIRE(std::isfinite(highestHeight));
    CHECK_THAT(highest - lowest, WithinRel(rho * g * (highestHeight - lowestHeight), 1e-12));
    // And the face really does span a usable part of the channel, so the
    // statement above is not about two facets that nearly coincide.
    // MEASURED AT 13.3 mm of the channel's 40 mm, which is 40/3: a facet
    // centroid sits at the one-third point of its facet, so the centroids never
    // reach the face's extremes. A quarter is the bound that measurement
    // justifies -- it rules out two nearly coincident facets without pretending
    // the centroids span the body.
    CHECK(highestHeight - lowestHeight > 0.25 * 0.040);
    CHECK(highestHeight - lowestHeight < 0.040);
}

TEST_CASE("CfdBoundary_WithoutGravityAPrescribedPressureIsUniform", "[cfd][bc][datum]") {
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
    definition.boundaryConditions[1] = CfdBoundaryCondition{
        CfdBoundaryConditionId::fromValue(2), PressureOutlet{faces.endCap(), 2500_Pa}};
    REQUIRE_FALSE(definition.gravity.has_value());

    const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
    REQUIRE(boundary.has_value());
    std::size_t outlets = 0;
    for (std::size_t b = 0; b < boundary->boundaryFaces(); ++b) {
        const std::optional<cfd::CfdBoundaryFace> record =
            boundary->face(BoundaryFaceIndex::fromValue(b));
        REQUIRE(record.has_value());
        if (record->kind != CfdBoundaryConditionKind::PressureOutlet) {
            continue;
        }
        ++outlets;
        REQUIRE(record->pressure.has_value());
        // EXACTLY the prescribed value: without gravity there is no height.
        CHECK_THAT(record->pressure->si(), WithinAbs(2500.0, 1e-9));
    }
    CHECK(outlets > 0);
}

TEST_CASE("CfdBoundary_TheInletVelocityOpposesTheOutwardNormalOnEveryFacet",
          "[cfd][bc][orientation]") {
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    const Result<cfd::PreparedCfdBoundary> boundary =
        fluid.boundary(channelDefinition(fluid, faces));
    REQUIRE(boundary.has_value());

    const double speed = 0.1;
    std::size_t inlets = 0;
    for (std::size_t b = 0; b < boundary->boundaryFaces(); ++b) {
        const BoundaryFaceIndex boundaryFace = BoundaryFaceIndex::fromValue(b);
        const std::optional<cfd::CfdBoundaryFace> record = boundary->face(boundaryFace);
        REQUIRE(record.has_value());
        if (record->kind != CfdBoundaryConditionKind::VelocityInlet) {
            continue;
        }
        ++inlets;
        REQUIRE(record->velocity.has_value());
        const std::optional<Direction3D> normal =
            fluid.geometry->boundaryOutwardNormal(boundaryFace);
        REQUIRE(normal.has_value());
        const double along = record->velocity->x.si() * normal->x() +
                             record->velocity->y.si() * normal->y() +
                             record->velocity->z.si() * normal->z();
        CHECK(along < 0.0);
        CHECK_THAT(along, WithinRel(-speed, 1e-12));
    }
    CHECK(inlets > 0);
}

TEST_CASE("CfdBoundary_AMovingWallIsAcceptedTangentiallyAndRefusedNormally",
          "[cfd][bc][orientation]") {
    // The channel's sides are vertical planes, so a velocity along +Z lies in
    // every one of them and a velocity along a side's own normal does not.
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);

    // THE y = 0 SIDE, identified from the mesh, so the reasoning below about its
    // normal is about the face the test actually uses.
    const FaceName wallAtY0 =
        sideWhere(fluid, [](const Point3D& p) { return std::abs(p.y.si()) < 1e-12; });
    // It is one of the four the channel definition walls, so replacing it keeps
    // coverage complete.
    std::size_t slot = 0;
    {
        const CfdAnalysisDefinition probe = channelDefinition(fluid, faces);
        bool found = false;
        for (std::size_t i = 0; i < probe.boundaryConditions.size(); ++i) {
            if (probe.boundaryConditions[i].face() == wallAtY0) {
                slot = i;
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }

    SECTION("a lid velocity in the plane of the wall is accepted and kept") {
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        const Velocity3 lid{.x = 0_m_per_s, .y = 0_m_per_s, .z = 0.5_m_per_s};
        definition.boundaryConditions[slot] = CfdBoundaryCondition{
            definition.boundaryConditions[slot].id(), MovingWall{wallAtY0, lid}};
        const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
        INFO((boundary.has_value() ? std::string{} : boundary.error().message));
        REQUIRE(boundary.has_value());

        // THE WALL'S OWN VELOCITY, on every facet of it. Without this the
        // velocity could be dropped, zeroed or replaced and the test would
        // still pass -- which a review pointed out.
        std::size_t walls = 0;
        for (std::size_t b = 0; b < boundary->boundaryFaces(); ++b) {
            const std::optional<cfd::CfdBoundaryFace> record =
                boundary->face(BoundaryFaceIndex::fromValue(b));
            REQUIRE(record.has_value());
            if (record->kind != CfdBoundaryConditionKind::MovingWall) {
                continue;
            }
            ++walls;
            REQUIRE(record->velocity.has_value());
            CHECK(*record->velocity == lid);
        }
        CHECK(walls > 0);
    }

    SECTION("a velocity along the wall's own normal is refused") {
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        // The y = 0 side's outward normal is -Y, so a velocity along +Y is
        // purely normal to it. The face is identified from the mesh above, so
        // this reasoning is about the face the condition actually targets.
        definition.boundaryConditions[slot] = CfdBoundaryCondition{
            definition.boundaryConditions[slot].id(),
            MovingWall{wallAtY0, Velocity3{.x = 0_m_per_s, .y = 0.5_m_per_s, .z = 0_m_per_s}}};
        CHECK(fluid.problem(definition) == CfdBoundaryProblem::WallVelocityNotTangential);
        const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
        REQUIRE_FALSE(boundary.has_value());
        CHECK_THAT(boundary.error().message, ContainsSubstring("not tangential"));
    }
}

// ---------------------------------------------------------------------------
// The pressure datum
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_TheDatumRuleAgreesWithP19Data001sOwnFunction", "[cfd][bc][datum]") {
    // ONE RULE. `pressureDatumProblem` is P19-DATA-001's implementation of
    // ADR-058's rule; this milestone asks the same question of the resolved
    // targets. An INDEPENDENT ORACLE for the datum arm.
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);

    SECTION("an outlet fixes the pressure: neither refuses") {
        const CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        CHECK(cfd::pressureDatumProblem(definition) == std::nullopt);
        CHECK(fluid.problem(definition) == std::nullopt);
    }

    SECTION("walls only: both say the datum is missing") {
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        definition.boundaryConditions[1] = CfdBoundaryCondition{
            CfdBoundaryConditionId::fromValue(2), NoSlipWall{faces.endCap()}};
        REQUIRE(cfd::pressureDatumProblem(definition) ==
                cfd::PressureDatumProblem::Missing);
        CHECK(fluid.problem(definition) == CfdBoundaryProblem::PressureDatumMissing);
    }

    SECTION("a reference point inside the fluid fixes it, and is located") {
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        definition.boundaryConditions[1] = CfdBoundaryCondition{
            CfdBoundaryConditionId::fromValue(2), NoSlipWall{faces.endCap()}};
        definition.referencePressure =
            CfdReferencePressure{.value = 0_Pa, .location = Point3D{20_mm, 15_mm, 10_mm}};
        REQUIRE(cfd::pressureDatumProblem(definition) == std::nullopt);

        const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
        INFO((boundary.has_value() ? std::string{} : boundary.error().message));
        REQUIRE(boundary.has_value());
        REQUIRE(boundary->referenceCell().has_value());
        CHECK(boundary->referenceCell()->value() < fluid.topology->cells());
        // The located cell really does contain the point, checked against the
        // locator's own seam on the same mesh.
        CHECK(cfd::locatePoint(*fluid.topology, fluid.mesh(),
                               definition.referencePressure->location) ==
              boundary->referenceCell());
    }

    SECTION("an outlet AND a reference pressure is overdetermined in both") {
        // THE ARM THE TWO RULES COULD DISAGREE ON, and an earlier draft of this
        // test did not have it -- which is how the gate came to accept a
        // definition `validate` refuses. Found by review.
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        definition.referencePressure =
            CfdReferencePressure{.value = 0_Pa, .location = Point3D{20_mm, 15_mm, 10_mm}};
        REQUIRE(cfd::pressureDatumProblem(definition) ==
                cfd::PressureDatumProblem::Overdetermined);
        CHECK(fluid.problem(definition) == CfdBoundaryProblem::PressureDatumOverdetermined);
        // And `validate(definition)` refuses it too, so the gate and the
        // definition now agree on all four combinations.
        CHECK_FALSE(cfd::validate(definition).has_value());
    }

    SECTION("a reference point outside the body is refused by name") {
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        definition.boundaryConditions[1] = CfdBoundaryCondition{
            CfdBoundaryConditionId::fromValue(2), NoSlipWall{faces.endCap()}};
        definition.referencePressure =
            CfdReferencePressure{.value = 0_Pa, .location = Point3D{500_mm, 500_mm, 500_mm}};
        REQUIRE(cfd::pressureDatumProblem(definition) == std::nullopt);
        CHECK(fluid.problem(definition) == CfdBoundaryProblem::ReferencePointOutsideFluid);
        const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
        REQUIRE_FALSE(boundary.has_value());
        CHECK_THAT(boundary.error().message, ContainsSubstring("lies in no cell of the fluid"));
    }
}

// ---------------------------------------------------------------------------
// Source binding and staleness
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_ThePreparedBoundaryBindsTheExactMeshSource", "[cfd][bc][currentness]") {
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    const CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
    const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
    REQUIRE(boundary.has_value());

    const CfdBoundarySource source = fluid.sourceFor(definition);
    CHECK(cfd::isCurrent(*boundary, source));
    CHECK(cfd::staleReasons(*boundary, source).empty());

    // The stamp AND the counts, as the header says.
    CHECK(boundary->source().mesh == fluid.mesh().stamp());
    CHECK(boundary->source().cells == fluid.topology->cells());
    CHECK(boundary->source().boundaryFaces == fluid.topology->boundaryFaces());
    CHECK(boundary->source().body == fluid.body);
    CHECK(boundary->source().conditions == definition.boundaryConditions);
    // No gravity here, so the density did NOT enter a value and is not carried.
    CHECK_FALSE(boundary->source().density.has_value());
}

TEST_CASE("CfdBoundary_EveryStaleReasonIsReachableAndNamed", "[cfd][bc][currentness]") {
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    const CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
    const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
    REQUIRE(boundary.has_value());

    SECTION("a condition edit") {
        CfdAnalysisDefinition edited = definition;
        edited.boundaryConditions[0] = CfdBoundaryCondition{
            CfdBoundaryConditionId::fromValue(1), VelocityInlet{faces.startCap(), 0.2_m_per_s}};
        const std::vector<CfdBoundaryStaleReason> reasons =
            cfd::staleReasons(*boundary, fluid.sourceFor(edited));
        REQUIRE(reasons.size() == 1);
        CHECK(reasons[0] == CfdBoundaryStaleReason::Conditions);
    }

    SECTION("a datum edit") {
        CfdAnalysisDefinition edited = definition;
        edited.gravity =
            CfdGravity{.acceleration = {.x = 0_m_per_s2, .y = 0_m_per_s2, .z = -9.81_m_per_s2},
                       .referenceHeight = 0_m};
        const std::vector<CfdBoundaryStaleReason> reasons =
            cfd::staleReasons(*boundary, fluid.sourceFor(edited));
        // Gravity moves the datum AND, because an outlet fixes the pressure,
        // brings the density in: both are real dependencies of a prescribed
        // pressure, so both are named.
        CHECK(std::ranges::find(reasons, CfdBoundaryStaleReason::Datum) != reasons.end());
        CHECK(std::ranges::find(reasons, CfdBoundaryStaleReason::Fluid) != reasons.end());
    }

    SECTION("a solver, output, mode or initial-condition edit does NOT stale it") {
        CfdAnalysisDefinition edited = definition;
        edited.iteration.velocityRelaxation = 0.5;
        edited.convergence.momentumResidualTolerance = 1e-7;
        edited.linearSolver.relativeResidualTolerance = 1e-8;
        edited.output.residualHistory = cfd::CfdResidualHistory::EveryIteration;
        // The two ADR-067 Decision 1 names that an earlier draft left out.
        edited.initialCondition.velocity =
            Velocity3{.x = 1_m_per_s, .y = 0_m_per_s, .z = 0_m_per_s};
        edited.initialCondition.pressure = 10_Pa;
        CHECK(cfd::isCurrent(*boundary, fluid.sourceFor(edited)));
        // And the prepared value is unchanged, not merely called current.
        const Result<cfd::PreparedCfdBoundary> again = fluid.boundary(edited);
        REQUIRE(again.has_value());
        CHECK(*again == *boundary);
    }

    SECTION("the body, the geometry and the counts each stale it on their own") {
        // HAND-BUILT SOURCES, because two documents built the same way share
        // their object ids and two meshes of one body share their counts, so
        // these three arms are not reachable by editing a model. A review found
        // all three unreached, which left three comparisons free to be deleted.
        const CfdBoundarySource current = fluid.sourceFor(definition);
        REQUIRE(cfd::isCurrent(*boundary, current));

        CfdBoundarySource otherBody = current;
        otherBody.body = ObjectId::fromValue(current.body.value() + 1);
        const std::vector<CfdBoundaryStaleReason> bodyMoved =
            cfd::staleReasons(*boundary, otherBody);
        REQUIRE(bodyMoved.size() == 1);
        CHECK(bodyMoved[0] == CfdBoundaryStaleReason::Body);

        CfdBoundarySource otherGeometry = current;
        otherGeometry.geometry =
            meshing::GeometryRevision{current.geometry.value + 1};
        const std::vector<CfdBoundaryStaleReason> geometryMoved =
            cfd::staleReasons(*boundary, otherGeometry);
        REQUIRE(geometryMoved.size() == 1);
        CHECK(geometryMoved[0] == CfdBoundaryStaleReason::Geometry);

        // THE SAME STAMP WITH A DIFFERENT COUNT, which is the "a stamp
        // identifies the BUILDER, not the snapshot" lesson. Nothing else in the
        // suite presents it.
        CfdBoundarySource otherCells = current;
        otherCells.cells = current.cells + 1;
        const std::vector<CfdBoundaryStaleReason> cellsMoved =
            cfd::staleReasons(*boundary, otherCells);
        REQUIRE(cellsMoved.size() == 1);
        CHECK(cellsMoved[0] == CfdBoundaryStaleReason::Mesh);

        CfdBoundarySource otherFaces = current;
        otherFaces.boundaryFaces = current.boundaryFaces + 1;
        const std::vector<CfdBoundaryStaleReason> facesMoved =
            cfd::staleReasons(*boundary, otherFaces);
        REQUIRE(facesMoved.size() == 1);
        CHECK(facesMoved[0] == CfdBoundaryStaleReason::Mesh);

        // THE REFERENCE PRESSURE ON ITS OWN, not only through gravity.
        CfdBoundarySource otherDatum = current;
        otherDatum.referencePressure =
            CfdReferencePressure{.value = 1_Pa, .location = Point3D{1_mm, 1_mm, 1_mm}};
        const std::vector<CfdBoundaryStaleReason> datumMoved =
            cfd::staleReasons(*boundary, otherDatum);
        REQUIRE(datumMoved.size() == 1);
        CHECK(datumMoved[0] == CfdBoundaryStaleReason::Datum);

        // AND THE INVARIANT THAT TIES THE TWO TOGETHER: `staleReasons` compares
        // the fields by hand, so a dependency added to the struct without being
        // compared would make the hand-written comparison disagree with the
        // field-complete `operator==`. This is what would notice.
        for (const CfdBoundarySource& candidate :
             {current, otherBody, otherGeometry, otherCells, otherFaces, otherDatum}) {
            CHECK(cfd::isCurrent(*boundary, candidate) ==
                  (boundary->source() == candidate));
        }
    }

    SECTION("another body's mesh generation stales it, and the revision does not move") {
        // MEASURED, AND IT CORRECTED AN ASSUMPTION. This was written as "a
        // geometry edit stales the mesh AND the geometry", expecting a
        // different extrusion depth to move the geometry revision. It does not:
        // a `GeometryRevision` is a mix of the source object's revision and
        // those of its transitive dependencies, explicitly "never a hash of
        // BRep bytes" (GeometryPreparation.hpp), so two documents built by the
        // same sequence of calls carry the same revision whatever their
        // dimensions. What differs here is the MESH GENERATION, and that alone
        // is enough. The Geometry arm is reached by the hand-built source in
        // the section above, which is the only way to reach it.
        Fluid deeper(channelDocument(25_mm));
        const ChannelFaces deeperFaces = facesOf(deeper);
        const CfdBoundarySource other =
            deeper.sourceFor(channelDefinition(deeper, deeperFaces));
        const std::vector<CfdBoundaryStaleReason> reasons = cfd::staleReasons(*boundary, other);
        CHECK(std::ranges::find(reasons, CfdBoundaryStaleReason::Mesh) != reasons.end());
        CHECK_FALSE(cfd::isCurrent(*boundary, other));
        // The measurement that justifies the paragraph above.
        CHECK(other.geometry == boundary->source().geometry);
        CHECK_FALSE(other.mesh == boundary->source().mesh);
        // Reasons come back in the enum's order.
        std::vector<CfdBoundaryStaleReason> sorted = reasons;
        std::ranges::sort(sorted, [](CfdBoundaryStaleReason a, CfdBoundaryStaleReason b) {
            return static_cast<std::uint8_t>(a) < static_cast<std::uint8_t>(b);
        });
        CHECK(sorted == reasons);
    }
}

TEST_CASE("CfdBoundary_AMaterialEditStalesAHydrostaticBoundaryAndNotAGravityFreeOne",
          "[cfd][bc][currentness]") {
    // ADR-067 Decision 8's sharp edge, and the reason the density is an OPTIONAL
    // rather than always carried. Both arms are asserted, because carrying it
    // unconditionally would pass the first arm and fail the second.
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);

    SECTION("under gravity, with a pressure boundary: the density entered a value") {
        CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        definition.gravity =
            CfdGravity{.acceleration = {.x = -9.81_m_per_s2, .y = 0_m_per_s2, .z = 0_m_per_s2},
                       .referenceHeight = 0_m};
        const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
        REQUIRE(boundary.has_value());
        REQUIRE(boundary->source().density.has_value());
        CHECK(cfd::isCurrent(*boundary, fluid.sourceFor(definition)));

        fluid.setDensity(1200_kg_per_m3);
        fluid.prepare(); // the material edit does not touch the mesh
        const std::vector<CfdBoundaryStaleReason> reasons =
            cfd::staleReasons(*boundary, fluid.sourceFor(definition));
        REQUIRE(reasons.size() == 1);
        CHECK(reasons[0] == CfdBoundaryStaleReason::Fluid);

        // And the prescribed pressures really did change, which is why it is
        // stale rather than merely compared.
        const Result<cfd::PreparedCfdBoundary> again = fluid.boundary(definition);
        REQUIRE(again.has_value());
        CHECK_FALSE(*again == *boundary);
    }

    SECTION("without gravity: a material edit cannot change a prescribed value") {
        const CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
        const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
        REQUIRE(boundary.has_value());
        REQUIRE_FALSE(boundary->source().density.has_value());

        fluid.setDensity(1200_kg_per_m3);
        fluid.prepare();
        CHECK(cfd::isCurrent(*boundary, fluid.sourceFor(definition)));
        const Result<cfd::PreparedCfdBoundary> again = fluid.boundary(definition);
        REQUIRE(again.has_value());
        CHECK(*again == *boundary);
    }
}

TEST_CASE("CfdBoundary_ATopologyOfAnotherGenerationIsRefused", "[cfd][bc][binding]") {
    // STAMPS, NEVER COUNTS: two independently meshed copies of one body have
    // equal censuses and different stamps.
    Fluid first(channelDocument());
    Fluid second(channelDocument());
    const ChannelFaces faces = facesOf(first);
    const CfdAnalysisDefinition definition = channelDefinition(first, faces);

    REQUIRE(first.topology->cells() == second.topology->cells());
    REQUIRE(first.topology->boundaryFaces() == second.topology->boundaryFaces());

    const std::optional<CfdBoundaryProblem> problem = cfd::cfdBoundaryProblem(
        *first.input, *second.topology, *second.geometry, definition);
    CHECK(problem == CfdBoundaryProblem::TopologyOfAnotherMesh);
    const Result<cfd::PreparedCfdBoundary> boundary = cfd::prepareCfdBoundary(
        *first.input, *second.topology, *second.geometry, definition);
    REQUIRE_FALSE(boundary.has_value());
    CHECK_THAT(boundary.error().message, ContainsSubstring("not derived from the mesh"));
}

// ---------------------------------------------------------------------------
// Determinism and purity
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_RepeatedPreparationIsBitIdentical", "[cfd][bc][determinism]") {
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    const CfdAnalysisDefinition definition = channelDefinition(fluid, faces);

    const Result<cfd::PreparedCfdBoundary> first = fluid.boundary(definition);
    REQUIRE(first.has_value());
    for (int repeat = 0; repeat < 20; ++repeat) {
        const Result<cfd::PreparedCfdBoundary> again = fluid.boundary(definition);
        REQUIRE(again.has_value());
        CHECK(*again == *first);
    }

    // Equality discriminates, so the comparison means something.
    CfdAnalysisDefinition other = definition;
    other.boundaryConditions[1] = CfdBoundaryCondition{
        CfdBoundaryConditionId::fromValue(2), PressureOutlet{faces.endCap(), 10_Pa}};
    const Result<cfd::PreparedCfdBoundary> different = fluid.boundary(other);
    REQUIRE(different.has_value());
    CHECK_FALSE(*different == *first);
}

TEST_CASE("CfdBoundary_TheDiagnosticDoesNotDependOnTheOrderConditionsAreListedIn",
          "[cfd][bc][determinism]") {
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    CfdAnalysisDefinition definition = channelDefinition(fluid, faces);
    definition.boundaryConditions.pop_back(); // one wall missing

    const std::optional<CfdBoundaryProblem> problem = fluid.problem(definition);
    REQUIRE(problem == CfdBoundaryProblem::FacetWithoutCondition);

    CfdAnalysisDefinition shuffled = definition;
    std::ranges::reverse(shuffled.boundaryConditions);
    CHECK(fluid.problem(shuffled) == problem);
}

TEST_CASE("CfdBoundary_PreparationMutatesNoCanonicalStateAndNoMesh", "[cfd][bc][purity]") {
    Fluid fluid(channelDocument());
    const ChannelFaces faces = facesOf(fluid);
    const CfdAnalysisDefinition definition = channelDefinition(fluid, faces);

    const CfdAnalysisDefinition before = definition;
    const meshing::MeshStamp stamp = fluid.mesh().stamp();
    const std::size_t triangles = fluid.mesh().triangles().size();
    const CfdTopology topologyBefore = *fluid.topology;

    const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
    REQUIRE(boundary.has_value());

    CHECK(definition == before);
    CHECK(fluid.mesh().stamp() == stamp);
    CHECK(fluid.mesh().triangles().size() == triangles);
    CHECK(*fluid.topology == topologyBefore);
}

// ---------------------------------------------------------------------------
// Every reference mesh
// ---------------------------------------------------------------------------

TEST_CASE("CfdBoundary_EveryP16ReferenceMeshIsEitherCoveredOrRefusedForAStatedReason",
          "[cfd][bc][mapping]") {
    // THE SWEEP. For each reference body, one condition per DISTINCT first name
    // across the mapped faces -- so a name covering several patches is one
    // condition, which is ADR-063 Decision 3's case -- with the first made a
    // pressure outlet so the datum is never what refuses.
    //
    // The outcome must be one of exactly three, each with a reason that is
    // checked rather than assumed. Anything else fails, which is what stops this
    // being a test that passes by accepting whatever happens.
    std::size_t covered = 0;
    std::size_t refusedUnnamed = 0;
    std::size_t refusedOverlap = 0;

    for (const bettercad::reference::MeshReferenceModelInfo& info :
         bettercad::reference::kMeshReferenceModels) {
        if (!info.expectMesh) {
            continue;
        }
        INFO(info.id << " " << info.name);
        Fluid fluid(info.kind);

        std::vector<FaceName> names;
        for (const meshing::MappedFace& face : fluid.input->map().faces()) {
            if (face.names.empty()) {
                continue;
            }
            if (std::ranges::find(names, face.names.front()) == names.end()) {
                names.push_back(face.names.front());
            }
        }

        CfdAnalysisDefinition definition{.mesh = fluid.model.control()};
        std::uint64_t next = 1;
        for (const FaceName& name : names) {
            if (next == 1) {
                definition.boundaryConditions.push_back(
                    CfdBoundaryCondition{CfdBoundaryConditionId::fromValue(next++),
                                         PressureOutlet{name, 0_Pa}});
            } else {
                definition.boundaryConditions.push_back(CfdBoundaryCondition{
                    CfdBoundaryConditionId::fromValue(next++), NoSlipWall{name}});
            }
        }
        REQUIRE_FALSE(definition.boundaryConditions.empty());
        REQUIRE(cfd::validate(definition).has_value());

        const std::optional<CfdBoundaryProblem> problem = fluid.problem(definition);
        if (!problem.has_value()) {
            ++covered;
            const Result<cfd::PreparedCfdBoundary> boundary = fluid.boundary(definition);
            REQUIRE(boundary.has_value());
            std::size_t assigned = 0;
            for (const cfd::CfdBoundaryCardinality& record : boundary->cardinality()) {
                CHECK(record.agrees());
                assigned += record.assigned;
            }
            CHECK(assigned == fluid.topology->boundaryFaces());
            CHECK(boundary->boundaryFaces() == fluid.topology->boundaryFaces());
            continue;
        }
        if (*problem == CfdBoundaryProblem::UnnamedPatch) {
            ++refusedUnnamed;
            // The refusal has a cause that is independently visible.
            CHECK(fluid.contract->unnamedPatches() > 0);
            continue;
        }
        if (*problem == CfdBoundaryProblem::FacetClaimedTwice) {
            ++refusedOverlap;
            // Then some patch really does carry more than one name, which is
            // ADR-063's "one patch may carry several names".
            std::size_t multiplyNamed = 0;
            for (const meshing::MappedFace& face : fluid.input->map().faces()) {
                if (face.names.size() > 1) {
                    ++multiplyNamed;
                }
            }
            CHECK(multiplyNamed > 0);
            continue;
        }
        FAIL("unexpected refusal: " << cfd::toString(*problem));
    }

    // At least one body is fully covered, or the sweep proves nothing.
    // MEASURED, NOT HOPED FOR: all eight meshable reference bodies come out
    // FULLY COVERED -- a block, a cylinder, a plate with a hole, a tube, a thin
    // plate, a block at the origin and the same block rotated and moved, and a
    // locally refined block. None is refused, so the two refusal arms above are
    // not exercised by this sweep; they are reached by the bored body and by the
    // double-coverage case, which is why those tests exist separately.
    //
    // The equality is deliberate rather than `> 0`. If a later change made one
    // of these bodies lose a name or overlap a patch, a `> 0` sweep would still
    // pass while quietly testing less, and this fails instead.
    CHECK(covered == 8);
    CHECK(refusedUnnamed == 0);
    CHECK(refusedOverlap == 0);
    CHECK(covered + refusedUnnamed + refusedOverlap == 8);
}
