// P18-LOAD-001: canonical heat loads and their nodal heat input.
//
// WHAT THESE TESTS ARE FOR. Two kinds of claim, and they need different
// evidence. The INTEGRATION claims -- a constant flux over a linear triangle
// gives each corner q'' A / 3, a constant generation in a Tet4 gives each corner
// q''' V / 4, the totals q'' A and q''' V are conserved, contributions
// superpose and accumulate at shared nodes, the sign of a scalar load does not
// depend on a winding -- are checked against values written out BY HAND in the
// test, through the integration seams with hand-built meshes, never against a
// production helper. The AUTHORITY claims -- a facet, node or element handle is
// never load authority, an unresolved target is refused rather than bound to a
// neighbour, an empty target is not a zero load, a prepared load dies with its
// mesh even when every count and every numeric id is unchanged -- are checked by
// constructing the dangerous thing against REAL P16 meshes and watching it be
// refused.
//
// EXPECTED VALUES ARE COMPUTED HERE. `facetNodalHeat`, `elementNodalHeat` and
// the integrators are production, so the analytical tests form their
// expectations from coordinates with arithmetic written out in the test: a
// cross product for a triangle's area, a determinant for a tetrahedron's
// volume. The only production inputs an expectation shares are P16's own facet
// SETS, because which triangles lie on a CAD face is P16's question, not this
// milestone's -- and those are checked against the CAD face's known area.
//
// TWO TOLERANCE CLASSES, KEPT APART. `sum(Q) == q'' * sum(facet areas)` is an
// ALGEBRAIC identity of the integration and is held to a few ulps. `sum(facet
// areas) == A_CAD` is a statement about the MESH and carries P16's geometric
// tolerance (exact for a planar face, a polygon-in-circle defect for a
// cylinder's cap). Conflating them would blame the load conversion for
// meshing's approximation.

#include "TestHelpers.hpp"
#include "features/FeatureTestSupport.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/document/References.hpp>
#include <bettercad/core/materials/MechanicalProperties.hpp>
#include <bettercad/core/materials/ThermalProperties.hpp>
#include <bettercad/core/units/Literals.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/MeshSizing.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/thermal/ThermalAnalysis.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalLoadVector.hpp>
#include <bettercad/thermal/ThermalResult.hpp>
#include <bettercad/thermal/ThermalTarget.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using meshing::ElementId;
using meshing::NodeId;
using meshing::RegionId;
using thermal::AnalysisState;
using thermal::IntegratedHeat;
using thermal::LoadFailure;
using thermal::LoadProblem;
using thermal::LoadStaleReason;
using thermal::PreparedThermalLoads;
using thermal::PreparedThermalLoadsSource;
using thermal::ResultCurrency;
using thermal::StaleReason;
using thermal::SurfaceHeatFluxLoad;
using thermal::TargetProblem;
using thermal::ThermalAnalysis;
using thermal::ThermalAnalysisDefinition;
using thermal::ThermalDofIndex;
using thermal::ThermalDofMap;
using thermal::ThermalLoad;
using thermal::ThermalLoadContribution;
using thermal::ThermalLoadKind;
using thermal::ThermalModel;
using thermal::ThermalResult;
using thermal::ThermalResultSource;
using thermal::VolumetricHeatSourceLoad;

namespace {

using H = NodeId::ValueType;

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// The block of `LoadStudy`: 40 x 30 mm in plan, 20 mm deep. Derived by hand
/// from the dimensions, so the caps' area and the body's volume are exact.
constexpr double kCapArea = 0.040 * 0.030;
constexpr double kBlockVolume = 0.040 * 0.030 * 0.020;

[[nodiscard]] Point3D at(double x, double y, double z) {
    return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
}

[[nodiscard]] ThermalLoadId lid(std::uint64_t value) { return ThermalLoadId::fromValue(value); }

[[nodiscard]] ThermalLoad fluxLoad(std::uint64_t id, const FaceName& face, double flux) {
    return ThermalLoad{lid(id), SurfaceHeatFluxLoad{.face = face, .flux = HeatFlux::fromSi(flux)}};
}

[[nodiscard]] ThermalLoad generationLoad(std::uint64_t id, double generation) {
    return ThermalLoad{lid(id), VolumetricHeatSourceLoad{.generation = VolumetricHeatSource::fromSi(generation)}};
}

// -----------------------------------------------------------------------
// Test-side arithmetic. NOT production's.
// -----------------------------------------------------------------------

/// `0.5 |(p2-p1) x (p3-p1)|`, written out.
[[nodiscard]] double triangleAreaByHand(const Point3D& p1, const Point3D& p2, const Point3D& p3) {
    const double ux = p2.x.si() - p1.x.si();
    const double uy = p2.y.si() - p1.y.si();
    const double uz = p2.z.si() - p1.z.si();
    const double vx = p3.x.si() - p1.x.si();
    const double vy = p3.y.si() - p1.y.si();
    const double vz = p3.z.si() - p1.z.si();
    const double cx = uy * vz - uz * vy;
    const double cy = uz * vx - ux * vz;
    const double cz = ux * vy - uy * vx;
    return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

/// `(p2-p1) . ((p3-p1) x (p4-p1)) / 6`, written out. Signed.
[[nodiscard]] double tetVolumeByHand(const Point3D& p1, const Point3D& p2, const Point3D& p3, const Point3D& p4) {
    const double ax = p2.x.si() - p1.x.si();
    const double ay = p2.y.si() - p1.y.si();
    const double az = p2.z.si() - p1.z.si();
    const double bx = p3.x.si() - p1.x.si();
    const double by = p3.y.si() - p1.y.si();
    const double bz = p3.z.si() - p1.z.si();
    const double cx = p4.x.si() - p1.x.si();
    const double cy = p4.y.si() - p1.y.si();
    const double cz = p4.z.si() - p1.z.si();
    return (ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx)) / 6.0;
}

[[nodiscard]] double sumOf(std::span<const Power> values) {
    double total = 0.0;
    for (const Power value : values) {
        total += value.si();
    }
    return total;
}

[[nodiscard]] double maxAbs(std::span<const Power> values) {
    double biggest = 0.0;
    for (const Power value : values) {
        biggest = std::max(biggest, std::abs(value.si()));
    }
    return biggest;
}

[[nodiscard]] double maxDifference(std::span<const Power> a, std::span<const Power> b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i].si() - b[i].si()));
    }
    return worst;
}

[[nodiscard]] double relativeError(double actual, double expected) {
    return expected == 0.0 ? std::abs(actual) : std::abs(actual - expected) / std::abs(expected);
}

// -----------------------------------------------------------------------
// Hand-built meshes, for the integration seams
// -----------------------------------------------------------------------

struct Hand {
    meshing::Mesh mesh;
    std::vector<ElementId> triangles;
    std::vector<ElementId> tets;
    ThermalDofMap dofs;
};

using HandNodes = std::vector<std::pair<NodeId::ValueType, Point3D>>;

/// A mesh with exactly the nodes asked for (so sparse handles are possible),
/// the triangles and tetrahedra given as indices into @p nodes, and its
/// numbering.
[[nodiscard]] Hand makeHand(const HandNodes& nodes, const std::vector<std::array<std::size_t, 3>>& triangles,
                            const std::vector<std::array<std::size_t, 4>>& tets) {
    meshing::MeshBuilder builder;
    std::vector<NodeId> ids;
    for (const auto& [handle, position] : nodes) {
        const Result<NodeId> added = builder.addNode(NodeId::fromValue(handle), position);
        INFO((added.has_value() ? std::string{} : added.error().message));
        REQUIRE(added.has_value());
        ids.push_back(*added);
    }
    std::vector<ElementId> triangleIds;
    for (const auto& corners : triangles) {
        const Result<ElementId> added =
            builder.addTriangle({ids.at(corners[0]), ids.at(corners[1]), ids.at(corners[2])}, RegionId::fromValue(1));
        INFO((added.has_value() ? std::string{} : added.error().message));
        REQUIRE(added.has_value());
        triangleIds.push_back(*added);
    }
    std::vector<ElementId> tetIds;
    for (const auto& corners : tets) {
        const Result<ElementId> added = builder.addTetrahedron(
            {ids.at(corners[0]), ids.at(corners[1]), ids.at(corners[2]), ids.at(corners[3])}, RegionId::fromValue(1));
        INFO((added.has_value() ? std::string{} : added.error().message));
        REQUIRE(added.has_value());
        tetIds.push_back(*added);
    }
    meshing::Mesh mesh = builder.build();
    Result<ThermalDofMap> map = thermal::buildThermalDofMap(mesh);
    INFO((map.has_value() ? std::string{} : map.error().message));
    REQUIRE(map.has_value());
    return Hand{std::move(mesh), std::move(triangleIds), std::move(tetIds), std::move(*map)};
}

[[nodiscard]] HandNodes referenceTriangleNodes(double scale = 1.0) {
    return {{1, at(0, 0, 0)}, {2, at(2.0 * scale, 0, 0)}, {3, at(0, 3.0 * scale, 0)}};
}

[[nodiscard]] HandNodes unitTetNodes(double scale = 1.0) {
    return {{1, at(0, 0, 0)}, {2, at(scale, 0, 0)}, {3, at(0, scale, 0)}, {4, at(0, 0, scale)}};
}

[[nodiscard]] std::vector<Power> zeros(const Hand& hand) {
    return std::vector<Power>(static_cast<std::size_t>(hand.dofs.dofCount()), Power{});
}

[[nodiscard]] IntegratedHeat surface(const Hand& hand, double flux, std::vector<Power>& q,
                                     std::vector<ElementId> facets = {}) {
    if (facets.empty()) {
        facets = hand.triangles;
    }
    const std::expected<IntegratedHeat, LoadFailure> done = thermal::integrateSurfaceHeatFlux(
        hand.mesh, hand.dofs, facets, HeatFlux::fromSi(flux), q);
    INFO((done.has_value() ? std::string{} : done.error().error.message));
    REQUIRE(done.has_value());
    return *done;
}

[[nodiscard]] IntegratedHeat volume(const Hand& hand, double generation, std::vector<Power>& q) {
    const std::expected<IntegratedHeat, LoadFailure> done = thermal::integrateVolumetricHeatSource(
        hand.mesh, hand.dofs, hand.tets, VolumetricHeatSource::fromSi(generation), q);
    INFO((done.has_value() ? std::string{} : done.error().error.message));
    REQUIRE(done.has_value());
    return *done;
}

[[nodiscard]] double entryOf(const Hand& hand, const std::vector<Power>& q, NodeId::ValueType handle) {
    const Result<ThermalDofIndex> dof = hand.dofs.dofOf(NodeId::fromValue(handle));
    REQUIRE(dof.has_value());
    REQUIRE(dof->value() < q.size());
    return q[static_cast<std::size_t>(dof->value())].si();
}

// -----------------------------------------------------------------------
// Real P16 fixtures
// -----------------------------------------------------------------------

/// The block of the other thermal tests, with a thermal analysis carrying
/// @p loads and a mesh.
struct LoadStudy {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    ThermalAnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    explicit LoadStudy(std::vector<ThermalLoad> loads = {}) {
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        lines = addRectangle(*sketch, 0_mm, 0_mm, 40_mm, 30_mm);
        const ObjectId profile = require(document.addObject(std::move(sketch)));
        auto extrude = features::ExtrudeFeature::create(
            "Solid", {.profile = SketchId::fromValue(profile.value()), .depth = 20_mm});
        REQUIRE(extrude.has_value());
        feature = require(document.addObject(std::move(*extrude)));

        auto intent = meshing::MeshControl::create("Mesh", meshing::MeshControlDefinition{.body = feature});
        REQUIRE(intent.has_value());
        control = MeshControlId::fromValue(require(document.addObject(std::move(*intent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.mechanical.density = materials::MaterialProperty<Density>::known(7850_kg_per_m3);
        definition.thermal.thermalConductivity =
            materials::MaterialProperty<ThermalConductivity>::known(45_W_per_m_K);
        definition.thermal.specificHeatCapacity =
            materials::MaterialProperty<SpecificHeatCapacity>::known(460_J_per_kg_K);
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        material = *id;
        REQUIRE(features::assignMaterial(document, material).has_value());

        auto study = ThermalAnalysis::create(
            "Study", ThermalAnalysisDefinition{.mesh = control, .loads = std::move(loads)});
        INFO((study.has_value() ? std::string{} : study.error().message));
        REQUIRE(study.has_value());
        analysis = ThermalAnalysisId::fromValue(require(document.addObject(std::move(*study))).value());
        requireReport(regenerator, document);
        remesh();
    }

    void remesh() {
        const Result<const meshing::VolumeMesh*> mesh = mesher.generate(document, regenerator, control);
        INFO((mesh.has_value() ? std::string{} : mesh.error().message));
        REQUIRE(mesh.has_value());
    }

    [[nodiscard]] FaceName startCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::StartCap}}; }
    [[nodiscard]] FaceName endCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}}; }
    [[nodiscard]] FaceName side(std::size_t which) const {
        return FaceName{feature, FaceSelector{.role = FaceRole::Side, .entity = lines.at(which)}};
    }

    [[nodiscard]] Result<ThermalModel> tryModel() const {
        return thermal::requireThermalModel(document, regenerator, mesher, control);
    }

    [[nodiscard]] ThermalModel model() const {
        Result<ThermalModel> prepared = tryModel();
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }

    [[nodiscard]] const meshing::VolumeMesh& volume() const {
        const meshing::VolumeMesh* held = mesher.mesh(control);
        REQUIRE(held != nullptr);
        return *held;
    }

    [[nodiscard]] ThermalResultSource currentSource() const {
        const Result<ThermalResultSource> source =
            thermal::currentResultSource(document, regenerator, mesher, analysis);
        INFO((source.has_value() ? std::string{} : source.error().message));
        REQUIRE(source.has_value());
        return *source;
    }

    [[nodiscard]] Result<bool> setDefinition(ThermalAnalysisDefinition definition) {
        return document.modifyObject<ThermalAnalysis>(
            ObjectId::fromValue(analysis.value()),
            [&definition](ThermalAnalysis& object) -> Result<bool> { return object.setDefinition(definition); });
    }

    [[nodiscard]] ThermalAnalysisDefinition definition() const {
        const ThermalAnalysis* object = thermal::findThermalAnalysis(document, analysis);
        REQUIRE(object != nullptr);
        return object->definition();
    }
};

[[nodiscard]] ThermalDofMap dofMapOf(const ThermalModel& model) {
    Result<ThermalDofMap> map = thermal::buildThermalDofMap(model.mesh().mesh());
    INFO((map.has_value() ? std::string{} : map.error().message));
    REQUIRE(map.has_value());
    return std::move(*map);
}

[[nodiscard]] PreparedThermalLoads prepareOn(const ThermalModel& model, const ThermalDofMap& map,
                                             const std::vector<ThermalLoad>& loads) {
    Result<PreparedThermalLoads> prepared = thermal::prepareThermalLoads(model, map, loads);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    return std::move(*prepared);
}

[[nodiscard]] PreparedThermalLoads prepare(const LoadStudy& study, const std::vector<ThermalLoad>& loads) {
    const ThermalModel model = study.model();
    return prepareOn(model, dofMapOf(model), loads);
}

/// The facet set P16 resolves @p face to, REQUIRED to be fully resolved and
/// non-empty.
[[nodiscard]] std::vector<ElementId> facetsOf(const ThermalModel& model, const FaceName& face) {
    const Result<meshing::BoundaryFacetSet> set = meshing::boundaryFacetsOf(model.map(), face);
    REQUIRE(set.has_value());
    REQUIRE(set->fullyResolved());
    REQUIRE_FALSE(set->facets.empty());
    return set->facets;
}

/// Sum of the facets' areas, by the test's own cross product.
[[nodiscard]] double areaOfFacets(const meshing::Mesh& mesh, const std::vector<ElementId>& facets) {
    double total = 0.0;
    for (const ElementId id : facets) {
        const meshing::Triangle* triangle = mesh.findTriangle(id);
        REQUIRE(triangle != nullptr);
        const meshing::Node* n0 = mesh.findNode(triangle->nodes[0]);
        const meshing::Node* n1 = mesh.findNode(triangle->nodes[1]);
        const meshing::Node* n2 = mesh.findNode(triangle->nodes[2]);
        REQUIRE((n0 != nullptr && n1 != nullptr && n2 != nullptr));
        total += triangleAreaByHand(n0->position, n1->position, n2->position);
    }
    return total;
}

/// Sum of the tetrahedra's SIGNED volumes, by the test's own determinant.
[[nodiscard]] double volumeOfTetrahedra(const meshing::Mesh& mesh) {
    double total = 0.0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        std::array<const meshing::Node*, 4> n{};
        for (std::size_t i = 0; i < 4; ++i) {
            n[i] = mesh.findNode(tet.nodes[i]);
            REQUIRE(n[i] != nullptr);
        }
        total += tetVolumeByHand(n[0]->position, n[1]->position, n[2]->position, n[3]->position);
    }
    return total;
}

/// The expected nodal vector for a flux over @p facets, formed from the mesh
/// coordinates in the test: `q'' A_f / 3` at each facet corner, accumulated.
[[nodiscard]] std::vector<double> expectedSurface(const meshing::Mesh& mesh, const ThermalDofMap& map,
                                                  const std::vector<ElementId>& facets, double flux) {
    std::vector<double> q(static_cast<std::size_t>(map.dofCount()), 0.0);
    for (const ElementId id : facets) {
        const meshing::Triangle* triangle = mesh.findTriangle(id);
        REQUIRE(triangle != nullptr);
        std::array<Point3D, 3> p{};
        for (std::size_t i = 0; i < 3; ++i) {
            const meshing::Node* node = mesh.findNode(triangle->nodes[i]);
            REQUIRE(node != nullptr);
            p[i] = node->position;
        }
        const double share = flux * triangleAreaByHand(p[0], p[1], p[2]) / 3.0;
        for (std::size_t i = 0; i < 3; ++i) {
            const Result<ThermalDofIndex> dof = map.dofOf(triangle->nodes[i]);
            REQUIRE(dof.has_value());
            q[static_cast<std::size_t>(dof->value())] += share;
        }
    }
    return q;
}

/// The expected nodal vector for a generation in the whole body: `q''' V_e / 4`
/// at each element corner, accumulated.
[[nodiscard]] std::vector<double> expectedVolume(const meshing::Mesh& mesh, const ThermalDofMap& map,
                                                 double generation) {
    std::vector<double> q(static_cast<std::size_t>(map.dofCount()), 0.0);
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        std::array<Point3D, 4> p{};
        for (std::size_t i = 0; i < 4; ++i) {
            const meshing::Node* node = mesh.findNode(tet.nodes[i]);
            REQUIRE(node != nullptr);
            p[i] = node->position;
        }
        const double share = generation * tetVolumeByHand(p[0], p[1], p[2], p[3]) / 4.0;
        for (std::size_t i = 0; i < 4; ++i) {
            const Result<ThermalDofIndex> dof = map.dofOf(tet.nodes[i]);
            REQUIRE(dof.has_value());
            q[static_cast<std::size_t>(dof->value())] += share;
        }
    }
    return q;
}

[[nodiscard]] double worstAgainst(std::span<const Power> actual, const std::vector<double>& expected) {
    REQUIRE(actual.size() == expected.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        worst = std::max(worst, std::abs(actual[i].si() - expected[i]));
    }
    return worst;
}

/// A cylinder, 6 mm radius and 20 mm long, with a coarse and a fine meshing
/// control. A cylinder rather than a finer block, for the reason P16-SIZE-001
/// measured: OCCT triangulates a planar face with two triangles whatever the
/// deflection, so a block cannot be refined by a global target at all, while a
/// curved wall subdivides and the target governs.
struct RoundStudy {
    Document document{"Round"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId coarse{};
    MeshControlId fine{};
    /// A SECOND solid in the same document, with its own control, so that a
    /// prepared load can be asked about a different body.
    ObjectId otherBody{};
    MeshControlId otherControl{};

    static constexpr double kRadius = 0.006;
    static constexpr double kLength = 0.020;

    RoundStudy() {
        auto profile = std::make_unique<sketch::Sketch>("Round", Frame3D::xy());
        REQUIRE(profile->addCircle(Point2D{0_mm, 0_mm}, 6_mm).has_value());
        const ObjectId round = require(document.addObject(std::move(profile)));
        auto cylinder = features::ExtrudeFeature::create(
            "Cylinder", {.profile = SketchId::fromValue(round.value()), .depth = 20_mm});
        REQUIRE(cylinder.has_value());
        feature = require(document.addObject(std::move(*cylinder)));

        auto coarseIntent = meshing::MeshControl::create("Coarse", meshing::MeshControlDefinition{.body = feature});
        REQUIRE(coarseIntent.has_value());
        coarse = MeshControlId::fromValue(require(document.addObject(std::move(*coarseIntent))).value());

        meshing::MeshControlDefinition finer{.body = feature};
        finer.mesh.sizing.globalTargetSize = 3_mm;
        // The boundary triangulation is governed by deflection, not by the
        // volume target, so a finer VOLUME alone leaves the cap with the same
        // facets. Tightening the deflection as well makes the SURFACE differ,
        // which is the case a surface load must re-resolve.
        finer.mesh.surface.linearDeflection = Length::fromSi(5.0e-6);
        finer.mesh.surface.angularDeflection = Angle::fromSi(0.0872664626);  // 5 degrees
        auto fineIntent = meshing::MeshControl::create("Fine", finer);
        REQUIRE(fineIntent.has_value());
        fine = MeshControlId::fromValue(require(document.addObject(std::move(*fineIntent))).value());

        auto blockSketch = std::make_unique<sketch::Sketch>("BlockProfile", Frame3D::xy());
        addRectangle(*blockSketch, 0_mm, 0_mm, 10_mm, 10_mm);
        const ObjectId blockProfile = require(document.addObject(std::move(blockSketch)));
        auto blockExtrude = features::ExtrudeFeature::create(
            "Block", {.profile = SketchId::fromValue(blockProfile.value()), .depth = 5_mm});
        REQUIRE(blockExtrude.has_value());
        otherBody = require(document.addObject(std::move(*blockExtrude)));
        auto otherIntent = meshing::MeshControl::create("Other", meshing::MeshControlDefinition{.body = otherBody});
        REQUIRE(otherIntent.has_value());
        otherControl = MeshControlId::fromValue(require(document.addObject(std::move(*otherIntent))).value());

        features::MaterialDefinition definition;
        definition.designation = "Steel";
        definition.thermal.thermalConductivity =
            materials::MaterialProperty<ThermalConductivity>::known(45_W_per_m_K);
        const Result<MaterialId> id = features::createMaterial(document, "Steel", definition);
        REQUIRE(id.has_value());
        REQUIRE(features::assignMaterial(document, *id).has_value());
        requireReport(regenerator, document);
        for (const MeshControlId control : {coarse, fine, otherControl}) {
            const Result<const meshing::VolumeMesh*> mesh = mesher.generate(document, regenerator, control);
            INFO((mesh.has_value() ? std::string{} : mesh.error().message));
            REQUIRE(mesh.has_value());
        }
    }

    [[nodiscard]] FaceName endCap() const { return FaceName{feature, FaceSelector{.role = FaceRole::EndCap}}; }

    [[nodiscard]] ThermalModel model(MeshControlId control) const {
        Result<ThermalModel> prepared = thermal::requireThermalModel(document, regenerator, mesher, control);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }
};

} // namespace

// ===========================================================================
// The canonical definition
// ===========================================================================

TEST_CASE("ThermalLoad_SurfaceHeatFluxDefinition", "[thermal][load][definition]") {
    const FaceName face{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}};
    const ThermalLoad load = fluxLoad(7, face, 250.0);
    CHECK(load.id() == lid(7));
    CHECK(load.kind() == ThermalLoadKind::SurfaceHeatFlux);
    REQUIRE(load.surfaceHeatFlux() != nullptr);
    CHECK(load.surfaceHeatFlux()->flux.si() == 250.0);
    CHECK(load.surfaceHeatFlux()->face == face);
    CHECK(load.volumetricHeatSource() == nullptr);
    CHECK(load.target() == std::optional<FaceName>{face});
    CHECK(thermal::validate(load).has_value());
    // A valid identity is part of the record.
    CHECK_FALSE(thermal::validate(ThermalLoad{ThermalLoadId{}, SurfaceHeatFluxLoad{.face = face, .flux = HeatFlux::fromSi(1.0)}})
                    .has_value());
}

TEST_CASE("ThermalLoad_VolumetricGenerationDefinition", "[thermal][load][definition]") {
    const ThermalLoad load = generationLoad(8, 4.0e6);
    CHECK(load.id() == lid(8));
    CHECK(load.kind() == ThermalLoadKind::VolumetricHeatSource);
    REQUIRE(load.volumetricHeatSource() != nullptr);
    CHECK(load.volumetricHeatSource()->generation.si() == 4.0e6);
    CHECK(load.surfaceHeatFlux() == nullptr);
    // THE WHOLE ANALYSED SOLID: no target of any kind (ADR-049 scope: one solid).
    CHECK_FALSE(load.target().has_value());
    CHECK(thermal::validate(load).has_value());
}

TEST_CASE("ThermalLoad_CanonicalGeometryReferenceIsAFaceNameAndNothingMeshLocal", "[thermal][load][definition]") {
    // THE CENTRAL RULE, in compile-time form: a face load is a FaceName and a
    // physical value, and there is nowhere in it for a facet, a node, an
    // element or an unknown to hide. The mirrors declare the same members in the
    // same order, so an added field changes the size.
    struct SurfaceMirror {
        FaceName face;
        HeatFlux flux;
    };
    struct VolumeMirror {
        VolumetricHeatSource generation;
    };
    static_assert(sizeof(SurfaceHeatFluxLoad) == sizeof(SurfaceMirror),
                  "a surface heat-flux load holds a FaceName and a flux, and nothing else");
    static_assert(sizeof(VolumetricHeatSourceLoad) == sizeof(VolumeMirror),
                  "a volumetric load holds a generation, and nothing else");
    static_assert(std::is_same_v<decltype(SurfaceHeatFluxLoad::face), FaceName>);
    static_assert(std::is_same_v<decltype(SurfaceHeatFluxLoad::flux), HeatFlux>);
    static_assert(std::is_same_v<decltype(VolumetricHeatSourceLoad::generation), VolumetricHeatSource>);
    static_assert(!std::is_convertible_v<NodeId, FaceName>);
    static_assert(!std::is_convertible_v<ElementId, FaceName>);
    static_assert(!std::is_convertible_v<ThermalDofIndex, FaceName>);

    // The derived value is not reachable from the canonical load either.
    static_assert(!std::is_constructible_v<PreparedThermalLoads>,
                  "a prepared vector cannot be conjured");
    CHECK(true);
}

TEST_CASE("ThermalLoad_UnitsContract", "[thermal][load][definition]") {
    // W/m^2, W/m^3, m^2, m^3 and W, by type and by value. The product types are
    // static_asserts in the header; this restates them where a reader looks.
    STATIC_CHECK(HeatFlux::dimension == dimensions::power / dimensions::area);
    STATIC_CHECK(VolumetricHeatSource::dimension == dimensions::power / dimensions::volume);
    STATIC_CHECK(decltype(HeatFlux{} * Area{})::dimension == Power::dimension);
    STATIC_CHECK(decltype(VolumetricHeatSource{} * Volume{})::dimension == Power::dimension);
    STATIC_CHECK(HeatFlux::dimension != VolumetricHeatSource::dimension);

    // 12 W/m^2 over 3 m^2 is 36 W, a third each; 24 W/m^3 over 1/6 m^3 is 4 W,
    // a quarter each.
    CHECK(thermal::facetNodalHeat(Area::fromSi(3.0), HeatFlux::fromSi(12.0)).si() == 12.0);
    CHECK_THAT(thermal::elementNodalHeat(Volume::fromSi(1.0 / 6.0), VolumetricHeatSource::fromSi(24.0)).si(),
               WithinRel(1.0, 1e-15));
    STATIC_CHECK(std::is_same_v<decltype(thermal::facetNodalHeat(Area{}, HeatFlux{})), Power>);
    STATIC_CHECK(std::is_same_v<decltype(thermal::elementNodalHeat(Volume{}, VolumetricHeatSource{})), Power>);

    // A flux written in other units converts at construction, so the kernel
    // sees SI: 1 W/mm^2 is exactly 1e6 W/m^2.
    const Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}}, {});
    std::vector<Power> q = zeros(hand);
    const IntegratedHeat done = surface(hand, 1.0e6, q);
    CHECK_THAT(done.heatRate.si(), WithinRel(3.0e6, 1e-15));
}

TEST_CASE("ThermalLoad_SignConventionIsPositiveIntoTheBody", "[thermal][load][definition]") {
    // POSITIVE q'' is heat INTO the body, positive q''' is heat generated;
    // negative preserved exactly (no abs, no clamp); zero is a valid load.
    const Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}}, {});
    for (const double q : {12.0, -12.0, 0.0}) {
        std::vector<Power> nodal = zeros(hand);
        const IntegratedHeat done = surface(hand, q, nodal);
        INFO("q'' " << q);
        CHECK(done.heatRate.si() == 3.0 * q);
        for (const Power entry : nodal) {
            CHECK(entry.si() == q);
        }
    }
    const Hand body = makeHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    for (const double g : {24.0, -24.0, 0.0}) {
        std::vector<Power> nodal = zeros(body);
        const IntegratedHeat done = volume(body, g, nodal);
        INFO("q''' " << g);
        CHECK_THAT(done.heatRate.si(), WithinAbs(g / 6.0, 1e-15));
        CHECK_THAT(sumOf(nodal), WithinAbs(g / 6.0, 1e-15));
    }
    CHECK(thermal::validate(fluxLoad(1, FaceName{ObjectId::fromValue(3), FaceSelector{.role = FaceRole::EndCap}}, -5.0)).has_value());
    CHECK(thermal::validate(generationLoad(2, -5.0)).has_value());
    CHECK(thermal::validate(generationLoad(3, 0.0)).has_value());
}

// ===========================================================================
// Surface heat flux, by hand
// ===========================================================================

TEST_CASE("ThermalLoad_SingleTriangleHeatFluxAnalytical", "[thermal][load][surface]") {
    // THE HAND ORACLE. (0,0,0) (2,0,0) (0,3,0): A = (1/2) * 2 * 3 = 3 m^2. With
    // q'' = 12 W/m^2 the facet takes 36 W, a third of it, 12 W, at each corner.
    const Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}}, {});
    std::vector<Power> q = zeros(hand);
    const IntegratedHeat done = surface(hand, 12.0, q);

    CHECK(done.entities == 1);
    CHECK(done.area.si() == 3.0);
    CHECK(done.heatRate.si() == 36.0);
    REQUIRE(q.size() == 3);
    for (const NodeId::ValueType handle : {H{1}, H{2}, H{3}}) {
        CHECK(entryOf(hand, q, handle) == 12.0);
    }
    CHECK(sumOf(q) == 36.0);
    WARN("single triangle (0,0,0) (2,0,0) (0,3,0), q'' = 12 W/m^2: A = 3 m^2, total 36 W, nodes [12 12 12] W, "
         "sum error 0");
}

TEST_CASE("ThermalLoad_GeneralTriangleHeatFluxAnalytical", "[thermal][load][surface]") {
    // NOT AXIS-ALIGNED, so an area computed from a bounding box or from two
    // coordinates cannot pass. The expected area is the test's cross product.
    const Point3D p1 = at(0.3, -0.2, 0.5);
    const Point3D p2 = at(1.7, 0.4, 0.9);
    const Point3D p3 = at(0.1, 1.3, -0.4);
    const Hand hand = makeHand({{10, p1}, {20, p2}, {30, p3}}, {{0, 1, 2}}, {});
    const double flux = -250.5;
    const double area = triangleAreaByHand(p1, p2, p3);
    std::vector<Power> q = zeros(hand);
    const IntegratedHeat done = surface(hand, flux, q);

    CHECK_THAT(done.area.si(), WithinRel(area, 1e-14));
    CHECK_THAT(done.heatRate.si(), WithinRel(flux * area, 1e-14));
    for (const NodeId::ValueType handle : {H{10}, H{20}, H{30}}) {
        CHECK_THAT(entryOf(hand, q, handle), WithinRel(flux * area / 3.0, 1e-14));
    }
    CHECK_THAT(sumOf(q), WithinRel(flux * area, 1e-14));
    WARN(std::format("general triangle: A = {:.15g} m^2 by cross product, q'' = {} W/m^2, total {:.15g} W, "
                     "each corner {:.15g} W",
                     area, flux, flux * area, flux * area / 3.0));
}

TEST_CASE("ThermalLoad_SurfaceFluxScaling", "[thermal][load][surface]") {
    // q'' -> c q'' scales the whole vector by c, exactly.
    const Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}}, {});
    std::vector<Power> base = zeros(hand);
    (void)surface(hand, 7.5, base);
    for (const double c : {0.25, 2.0, 10.0, -3.0}) {
        std::vector<Power> scaled = zeros(hand);
        (void)surface(hand, 7.5 * c, scaled);
        for (std::size_t i = 0; i < base.size(); ++i) {
            CHECK_THAT(scaled[i].si(), WithinRel(c * base[i].si(), 1e-15));
        }
    }
}

TEST_CASE("ThermalLoad_SurfaceGeometryScaling", "[thermal][load][surface]") {
    // x' = s x: A' = s^2 A, so the same q'' gives Q' = s^2 Q. Mandatory unit
    // validation: a viewer-millimetre geometry would scale by 1e6.
    const Hand base = makeHand(referenceTriangleNodes(), {{0, 1, 2}}, {});
    std::vector<Power> q0 = zeros(base);
    const IntegratedHeat d0 = surface(base, 12.0, q0);
    for (const double s : {1.0e-3, 0.1, 2.0, 10.0, 100.0}) {
        const Hand scaled = makeHand(referenceTriangleNodes(s), {{0, 1, 2}}, {});
        std::vector<Power> q = zeros(scaled);
        const IntegratedHeat d = surface(scaled, 12.0, q);
        INFO("s " << s);
        CHECK_THAT(d.area.si() / d0.area.si(), WithinRel(s * s, 1e-14));
        CHECK_THAT(d.heatRate.si() / d0.heatRate.si(), WithinRel(s * s, 1e-14));
        CHECK_THAT(q[0].si() / q0[0].si(), WithinRel(s * s, 1e-14));
    }
    WARN("surface geometry scaling: Q'/Q = s^2 at s = 1e-3, 0.1, 2, 10, 100 (area 3 m^2 x s^2)");
}

TEST_CASE("ThermalLoad_SurfaceSharedNodeAccumulates", "[thermal][load][surface]") {
    // A 2 x 3 m rectangle, two triangles sharing the diagonal 0-2. Each triangle
    // has A = 3 m^2, so with q'' = 12 each takes 36 W: the two diagonal corners
    // receive 12 W from EACH triangle (24 W), the other two 12 W. A load that
    // OVERWROTE would leave 12 W on the diagonal and a total of 60 W, not 72.
    const Hand hand = makeHand({{1, at(0, 0, 0)}, {2, at(2, 0, 0)}, {3, at(2, 3, 0)}, {4, at(0, 3, 0)}},
                               {{0, 1, 2}, {0, 2, 3}}, {});
    std::vector<Power> q = zeros(hand);
    const IntegratedHeat done = surface(hand, 12.0, q);
    CHECK(done.entities == 2);
    CHECK(done.area.si() == 6.0);
    CHECK(done.heatRate.si() == 72.0);
    CHECK(entryOf(hand, q, 1) == 24.0);
    CHECK(entryOf(hand, q, 3) == 24.0);
    CHECK(entryOf(hand, q, 2) == 12.0);
    CHECK(entryOf(hand, q, 4) == 12.0);
    CHECK(sumOf(q) == 72.0);
}

TEST_CASE("ThermalLoad_TriangulationChangesTheDistributionAndNotTheTotal", "[thermal][load][surface]") {
    // The same rectangle cut along the other diagonal (1-3): the total is
    // q'' A either way; the nodal distribution moves to the other pair.
    const HandNodes rectangle{{1, at(0, 0, 0)}, {2, at(2, 0, 0)}, {3, at(2, 3, 0)}, {4, at(0, 3, 0)}};
    const Hand zero_two = makeHand(rectangle, {{0, 1, 2}, {0, 2, 3}}, {});
    const Hand one_three = makeHand(rectangle, {{0, 1, 3}, {1, 2, 3}}, {});
    std::vector<Power> a = zeros(zero_two);
    std::vector<Power> b = zeros(one_three);
    const IntegratedHeat da = surface(zero_two, 12.0, a);
    const IntegratedHeat db = surface(one_three, 12.0, b);
    CHECK(da.heatRate.si() == 72.0);
    CHECK(db.heatRate.si() == 72.0);
    CHECK(sumOf(a) == sumOf(b));
    // Diagonal 0-2 carries the double share in the first; 1-3 in the second.
    CHECK(entryOf(zero_two, a, 1) == 24.0);
    CHECK(entryOf(zero_two, a, 2) == 12.0);
    CHECK(entryOf(one_three, b, 2) == 24.0);
    CHECK(entryOf(one_three, b, 1) == 12.0);
    WARN("one rectangle, two triangulations: total 72 W in both; the 24 W pair moves from nodes 1,3 to nodes 2,4");
}

TEST_CASE("ThermalLoad_FacetWindingDoesNotChangeTheHeatInput", "[thermal][load][surface]") {
    // A scalar heat input has no direction, so reversing a triangle's winding
    // cannot flip it. (A pressure would flip; this is the difference.)
    const Hand forward = makeHand(referenceTriangleNodes(), {{0, 1, 2}}, {});
    const Hand reversed = makeHand(referenceTriangleNodes(), {{0, 2, 1}}, {});
    std::vector<Power> a = zeros(forward);
    std::vector<Power> b = zeros(reversed);
    (void)surface(forward, 12.0, a);
    (void)surface(reversed, 12.0, b);
    for (const NodeId::ValueType handle : {H{1}, H{2}, H{3}}) {
        CHECK(entryOf(forward, a, handle) == entryOf(reversed, b, handle));
        CHECK(entryOf(reversed, b, handle) == 12.0);
    }
}

// ===========================================================================
// Surface heat flux through P16's mapping
// ===========================================================================

TEST_CASE("ThermalLoad_SurfaceHeatRateConserved", "[thermal][load][surface]") {
    // A MAPPED CAD FACE, end to end. The end cap is 40 x 30 mm = 1.2e-3 m^2.
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const double flux = 500.0;
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), flux)};
    const PreparedThermalLoads prepared = prepareOn(model, map, loads);

    const std::vector<ElementId> facets = facetsOf(model, study.endCap());
    const double meshArea = areaOfFacets(model.mesh().mesh(), facets);

    // MESH FIDELITY: the facets tile the planar cap exactly.
    CHECK_THAT(meshArea, WithinRel(kCapArea, 1e-12));
    // ALGEBRAIC CONSERVATION: sum(Q) == q'' * sum(areas) to a few ulps.
    CHECK_THAT(sumOf(prepared.heatInput()), WithinRel(flux * meshArea, 1e-13));
    CHECK_THAT(prepared.totalHeatRate().si(), WithinRel(flux * meshArea, 1e-13));
    CHECK_THAT(prepared.appliedSurfaceHeatRate().si(), WithinRel(flux * meshArea, 1e-13));
    CHECK(prepared.volumetricGenerationRate().si() == 0.0);
    // And the CAD figure, with P16's tolerance (exact here).
    CHECK_THAT(prepared.totalHeatRate().si(), WithinRel(flux * kCapArea, 1e-12));

    // The nodal vector against the test's own integration over P16's facets.
    const std::vector<double> expected = expectedSurface(model.mesh().mesh(), map, facets, flux);
    CHECK(worstAgainst(prepared.heatInput(), expected) <= 1e-15 * flux * kCapArea);

    // Only the cap's nodes are loaded.
    std::size_t loaded = 0;
    for (const Power entry : prepared.heatInput()) {
        if (entry.si() != 0.0) {
            ++loaded;
        }
    }
    REQUIRE(prepared.contributions().size() == 1);
    CHECK(prepared.contributions()[0].integrated.entities == facets.size());
    CHECK(loaded == 4);

    WARN(std::format("end cap: {} facets, 4 loaded nodes of {}; mesh area {:.15g} m^2 vs CAD {:.15g}; q'' = {} -> "
                     "{:.15g} W (expected {:.15g}), sum error {:.2e} rel",
                     facets.size(), map.dofCount(), meshArea, kCapArea, flux, prepared.totalHeatRate().si(),
                     flux * kCapArea, relativeError(sumOf(prepared.heatInput()), flux * meshArea)));
}

TEST_CASE("ThermalLoad_GeometryReferenceResolvesCurrentBoundary", "[thermal][load][mapping]") {
    // The heat reaches EXACTLY the nodes P16 puts on the named face: the end
    // cap's four nodes at z = 20 mm, and none of the start cap's at z = 0.
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalLoads onEnd = prepareOn(model, map, {fluxLoad(1, study.endCap(), 100.0)});
    const PreparedThermalLoads onStart = prepareOn(model, map, {fluxLoad(1, study.startCap(), 100.0)});
    const meshing::Mesh& mesh = model.mesh().mesh();
    std::size_t endLoaded = 0;
    std::size_t startLoaded = 0;
    for (const meshing::Node& node : mesh.nodes()) {
        const Result<ThermalDofIndex> dof = map.dofOf(node.id);
        REQUIRE(dof.has_value());
        const bool atTop = std::abs(node.position.z.si() - 0.020) < 1e-12;
        const bool atBottom = std::abs(node.position.z.si()) < 1e-12;
        const double onEndValue = onEnd.heatInput()[static_cast<std::size_t>(dof->value())].si();
        const double onStartValue = onStart.heatInput()[static_cast<std::size_t>(dof->value())].si();
        CHECK((onEndValue != 0.0) == atTop);
        CHECK((onStartValue != 0.0) == atBottom);
        if (onEndValue != 0.0) {
            ++endLoaded;
        }
        if (onStartValue != 0.0) {
            ++startLoaded;
        }
    }
    CHECK(endLoaded == 4);
    CHECK(startLoaded == 4);
    // The two faces are equal areas, so equal totals, on different nodes.
    CHECK_THAT(onEnd.totalHeatRate().si(), WithinRel(onStart.totalHeatRate().si(), 1e-14));
}

// ===========================================================================
// Volumetric generation, by hand
// ===========================================================================

TEST_CASE("ThermalLoad_UnitTetGenerationAnalytical", "[thermal][load][volume]") {
    // THE HAND ORACLE. The unit tetrahedron has V = 1/6 m^3; q''' = 24 W/m^3
    // generates 4 W, a quarter, 1 W, at each of its four corners.
    const Hand hand = makeHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    std::vector<Power> q = zeros(hand);
    const IntegratedHeat done = volume(hand, 24.0, q);
    CHECK(done.entities == 1);
    CHECK_THAT(done.volume.si(), WithinRel(1.0 / 6.0, 1e-15));
    CHECK_THAT(done.heatRate.si(), WithinRel(4.0, 1e-15));
    REQUIRE(q.size() == 4);
    for (const NodeId::ValueType handle : {H{1}, H{2}, H{3}, H{4}}) {
        CHECK_THAT(entryOf(hand, q, handle), WithinRel(1.0, 1e-15));
    }
    CHECK_THAT(sumOf(q), WithinRel(4.0, 1e-15));
    WARN("unit tet V = 1/6 m^3, q''' = 24 W/m^3: total 4 W, nodes [1 1 1 1] W");
}

TEST_CASE("ThermalLoad_GeneralTetGenerationAnalytical", "[thermal][load][volume]") {
    // The brief's non-orthogonal tetrahedron, positively oriented; its volume
    // is the test's own determinant (4.695 / 6 = 0.7825 m^3).
    const Point3D p1 = at(0.2, -0.4, 0.1);
    const Point3D p2 = at(1.7, 0.3, 0.2);
    const Point3D p3 = at(0.4, 1.5, 0.6);
    const Point3D p4 = at(0.1, 0.2, 2.0);
    const double v = tetVolumeByHand(p1, p2, p3, p4);
    REQUIRE(v > 0.0);
    CHECK_THAT(v, WithinRel(0.7825, 1e-12));
    const Hand hand = makeHand({{5, p1}, {6, p2}, {7, p3}, {8, p4}}, {}, {{0, 1, 2, 3}});
    const double generation = 18.0;
    std::vector<Power> q = zeros(hand);
    const IntegratedHeat done = volume(hand, generation, q);
    CHECK_THAT(done.volume.si(), WithinRel(v, 1e-13));
    CHECK_THAT(done.heatRate.si(), WithinRel(generation * v, 1e-13));
    for (const NodeId::ValueType handle : {H{5}, H{6}, H{7}, H{8}}) {
        CHECK_THAT(entryOf(hand, q, handle), WithinRel(generation * v / 4.0, 1e-13));
    }
    WARN(std::format("general tet: V = {:.15g} m^3 by determinant, q''' = {} W/m^3, total {:.15g} W, each corner "
                     "{:.15g} W",
                     v, generation, generation * v, generation * v / 4.0));
}

TEST_CASE("ThermalLoad_GenerationScaling", "[thermal][load][volume]") {
    const Hand hand = makeHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    std::vector<Power> base = zeros(hand);
    (void)volume(hand, 7.5, base);
    for (const double c : {0.25, 2.0, 10.0, -3.0}) {
        std::vector<Power> scaled = zeros(hand);
        (void)volume(hand, 7.5 * c, scaled);
        for (std::size_t i = 0; i < base.size(); ++i) {
            CHECK_THAT(scaled[i].si(), WithinRel(c * base[i].si(), 1e-15));
        }
    }
}

TEST_CASE("ThermalLoad_VolumeGeometryScaling", "[thermal][load][volume]") {
    // x' = s x: V' = s^3 V, so Q' = s^3 Q. A viewer-millimetre volume would
    // scale by 1e9.
    const Hand base = makeHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    std::vector<Power> q0 = zeros(base);
    const IntegratedHeat d0 = volume(base, 24.0, q0);
    for (const double s : {1.0e-3, 0.1, 2.0, 10.0, 100.0}) {
        const Hand scaled = makeHand(unitTetNodes(s), {}, {{0, 1, 2, 3}});
        std::vector<Power> q = zeros(scaled);
        const IntegratedHeat d = volume(scaled, 24.0, q);
        INFO("s " << s);
        CHECK_THAT(d.volume.si() / d0.volume.si(), WithinRel(s * s * s, 1e-13));
        CHECK_THAT(d.heatRate.si() / d0.heatRate.si(), WithinRel(s * s * s, 1e-13));
        CHECK_THAT(q[0].si() / q0[0].si(), WithinRel(s * s * s, 1e-13));
    }
    WARN("volume geometry scaling: Q'/Q = s^3 at s = 1e-3, 0.1, 2, 10, 100");
}

TEST_CASE("ThermalLoad_SharedVolumeNodeAccumulates", "[thermal][load][volume]") {
    // Two tetrahedra on the shared face (nodes 2,3,4): V1 = 1/6 and V2 = 1/3
    // (the second is twice as tall). The shared nodes receive q''' V1/4 AND
    // q''' V2/4; the apexes one each. Overwriting would lose one of them.
    const Hand hand = makeHand({{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}, {4, at(0, 0, 1)}, {5, at(1, 1, 1)}},
                               {}, {{0, 1, 2, 3}, {4, 2, 1, 3}});
    const double v1 = tetVolumeByHand(at(0, 0, 0), at(1, 0, 0), at(0, 1, 0), at(0, 0, 1));
    const double v2 = tetVolumeByHand(at(1, 1, 1), at(0, 1, 0), at(1, 0, 0), at(0, 0, 1));
    REQUIRE(v1 > 0.0);
    REQUIRE(v2 > 0.0);
    const double g = 12.0;
    std::vector<Power> q = zeros(hand);
    const IntegratedHeat done = volume(hand, g, q);
    CHECK_THAT(done.heatRate.si(), WithinRel(g * (v1 + v2), 1e-14));
    CHECK_THAT(entryOf(hand, q, 1), WithinRel(g * v1 / 4.0, 1e-14));
    CHECK_THAT(entryOf(hand, q, 5), WithinRel(g * v2 / 4.0, 1e-14));
    for (const NodeId::ValueType shared : {H{2}, H{3}, H{4}}) {
        CHECK_THAT(entryOf(hand, q, shared), WithinRel(g * (v1 + v2) / 4.0, 1e-14));
    }
    CHECK_THAT(sumOf(q), WithinRel(g * (v1 + v2), 1e-14));
}

TEST_CASE("ThermalLoad_MultiTetBodyConservesTheHeatRate", "[thermal][load][volume]") {
    // The unit cube cut into six tetrahedra around the 0-6 diagonal (Kuhn): total
    // volume exactly 1 m^3, so q''' = 90 W/m^3 generates 90 W. Orientation is
    // fixed in the FIXTURE (a swap where the test's own determinant is negative),
    // never inside the kernel.
    const HandNodes cube{{11, at(0, 0, 0)}, {12, at(1, 0, 0)}, {13, at(0, 1, 0)}, {14, at(1, 1, 0)},
                         {15, at(0, 0, 1)}, {16, at(1, 0, 1)}, {17, at(0, 1, 1)}, {18, at(1, 1, 1)}};
    // Kuhn simplices: paths from corner 0 to corner 7 through the cube's edges.
    std::vector<std::array<std::size_t, 4>> tets{{0, 1, 3, 7}, {0, 1, 5, 7}, {0, 2, 3, 7}, {0, 2, 6, 7}, {0, 4, 5, 7}, {0, 4, 6, 7}};
    for (std::array<std::size_t, 4>& tet : tets) {
        if (tetVolumeByHand(cube[tet[0]].second, cube[tet[1]].second, cube[tet[2]].second, cube[tet[3]].second) < 0.0) {
            std::swap(tet[0], tet[1]);
        }
        REQUIRE(tetVolumeByHand(cube[tet[0]].second, cube[tet[1]].second, cube[tet[2]].second, cube[tet[3]].second) > 0.0);
    }
    const Hand hand = makeHand(cube, {}, tets);
    std::vector<Power> q = zeros(hand);
    const IntegratedHeat done = volume(hand, 90.0, q);
    CHECK(done.entities == 6);
    CHECK_THAT(done.volume.si(), WithinRel(1.0, 1e-14));
    CHECK_THAT(done.heatRate.si(), WithinRel(90.0, 1e-14));
    CHECK_THAT(sumOf(q), WithinRel(90.0, 1e-14));
    // Corners 0 and 7 belong to all six tetrahedra: 6 * (90/6)/4 = 22.5 W each.
    CHECK_THAT(entryOf(hand, q, 11), WithinRel(22.5, 1e-14));
    CHECK_THAT(entryOf(hand, q, 18), WithinRel(22.5, 1e-14));
}

TEST_CASE("ThermalLoad_VolumeHeatRateConserved", "[thermal][load][volume]") {
    // The whole block. TWO COMPARISONS, two tolerances: the conversion
    // (sum Q == q''' * sum V_tets, algebraic) and the mesh (sum V_tets == V_CAD,
    // exact for a prism of planar faces).
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const double generation = 2.5e6;
    const PreparedThermalLoads prepared = prepareOn(model, map, {generationLoad(1, generation)});

    const double meshVolume = volumeOfTetrahedra(model.mesh().mesh());
    CHECK_THAT(meshVolume, WithinRel(kBlockVolume, 1e-12));
    CHECK_THAT(sumOf(prepared.heatInput()), WithinRel(generation * meshVolume, 1e-13));
    CHECK_THAT(prepared.volumetricGenerationRate().si(), WithinRel(generation * meshVolume, 1e-13));
    CHECK(prepared.appliedSurfaceHeatRate().si() == 0.0);
    CHECK_THAT(prepared.totalHeatRate().si(), WithinRel(generation * kBlockVolume, 1e-12));

    const std::vector<double> expected = expectedVolume(model.mesh().mesh(), map, generation);
    CHECK(worstAgainst(prepared.heatInput(), expected) <= 1e-14 * generation * kBlockVolume);
    REQUIRE(prepared.contributions().size() == 1);
    CHECK(prepared.contributions()[0].integrated.entities == model.mesh().tetrahedronCount());

    // The region P16 names is the analysed body and every element of the mesh.
    const Result<meshing::MappedRegion> region = meshing::regionOf(model.map(), model.mesh().mesh());
    REQUIRE(region.has_value());
    CHECK(region->source == model.body());
    CHECK(region->elements.size() == model.mesh().tetrahedronCount());

    WARN(std::format("block: {} tetrahedra, mesh volume {:.15g} m^3 vs CAD {:.15g}; q''' = {} -> {:.15g} W "
                     "(expected {:.15g}), sum error {:.2e} rel",
                     model.mesh().tetrahedronCount(), meshVolume, kBlockVolume, generation,
                     prepared.totalHeatRate().si(), generation * kBlockVolume,
                     relativeError(sumOf(prepared.heatInput()), generation * meshVolume)));
}

TEST_CASE("ThermalLoad_VolumeGenerationIsMaterialIndependent", "[thermal][load][volume]") {
    // q''' needs no k, rho or cp: prepare a generation on a document whose
    // material has NONE of them beyond what the model needs, and observe the
    // vector is unchanged by editing them.
    LoadStudy study;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e6)};
    const PreparedThermalLoads before = prepare(study, loads);
    materials::ThermalProperties thermalProperties =
        features::findMaterial(study.document, study.material)->definition().thermal;
    thermalProperties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(77_W_per_m_K);
    thermalProperties.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(900_J_per_kg_K);
    REQUIRE(features::setMaterialThermal(study.document, study.material, thermalProperties).has_value());
    const PreparedThermalLoads after = prepare(study, loads);
    CHECK(maxDifference(before.heatInput(), after.heatInput()) == 0.0);
}

// ===========================================================================
// Superposition
// ===========================================================================

TEST_CASE("ThermalLoad_SurfaceSuperposition", "[thermal][load][superposition]") {
    // Two loads on the SAME face, with different identities, add: the
    // equivalent of one load of the sum. Nothing is deduplicated by target.
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalLoads one = prepareOn(model, map, {fluxLoad(1, study.endCap(), 300.0)});
    const PreparedThermalLoads two = prepareOn(model, map, {fluxLoad(2, study.endCap(), 125.0)});
    const PreparedThermalLoads both = prepareOn(model, map, {fluxLoad(1, study.endCap(), 300.0),
                                                              fluxLoad(2, study.endCap(), 125.0)});
    const PreparedThermalLoads summed = prepareOn(model, map, {fluxLoad(3, study.endCap(), 425.0)});

    std::vector<Power> expected;
    for (std::size_t i = 0; i < one.heatInput().size(); ++i) {
        expected.push_back(Power::fromSi(one.heatInput()[i].si() + two.heatInput()[i].si()));
    }
    const double scale = maxAbs(summed.heatInput());
    CHECK(maxDifference(both.heatInput(), expected) <= 1e-15 * scale);
    CHECK(maxDifference(both.heatInput(), summed.heatInput()) <= 1e-14 * scale);
    REQUIRE(both.contributions().size() == 2);
    CHECK_THAT(both.contributions()[0].integrated.heatRate.si(), WithinRel(300.0 * kCapArea, 1e-13));
    CHECK_THAT(both.contributions()[1].integrated.heatRate.si(), WithinRel(125.0 * kCapArea, 1e-13));
    CHECK_THAT(both.totalHeatRate().si(), WithinRel(425.0 * kCapArea, 1e-13));

    // Different faces superpose too: independent nodes, total q1 A1 + q2 A2.
    const PreparedThermalLoads faces = prepareOn(model, map, {fluxLoad(1, study.endCap(), 300.0),
                                                               fluxLoad(2, study.startCap(), -80.0)});
    CHECK_THAT(faces.totalHeatRate().si(), WithinRel((300.0 - 80.0) * kCapArea, 1e-13));
    WARN(std::format("superposition: L1 {:.6g} W + L2 {:.6g} W = {:.6g} W; max |Q12 - (Q1+Q2)| = {:.2e}",
                     one.totalHeatRate().si(), two.totalHeatRate().si(), both.totalHeatRate().si(),
                     maxDifference(both.heatInput(), expected)));
}

TEST_CASE("ThermalLoad_VolumeSuperposition", "[thermal][load][superposition]") {
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalLoads one = prepareOn(model, map, {generationLoad(1, 1.0e6)});
    const PreparedThermalLoads two = prepareOn(model, map, {generationLoad(2, 3.5e5)});
    const PreparedThermalLoads both = prepareOn(model, map, {generationLoad(1, 1.0e6), generationLoad(2, 3.5e5)});
    std::vector<Power> expected;
    for (std::size_t i = 0; i < one.heatInput().size(); ++i) {
        expected.push_back(Power::fromSi(one.heatInput()[i].si() + two.heatInput()[i].si()));
    }
    CHECK(maxDifference(both.heatInput(), expected) <= 1e-15 * maxAbs(both.heatInput()));
    CHECK_THAT(both.totalHeatRate().si(), WithinRel(1.35e6 * kBlockVolume, 1e-12));
}

TEST_CASE("ThermalLoad_MixedSuperposition", "[thermal][load][superposition]") {
    // Surface and volume on one body: the total is q'' A + q''' V under the
    // positive-into-the-body convention, and the two are attributed separately.
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const double flux = 750.0;
    const double generation = 4.0e5;
    const PreparedThermalLoads surfaceOnly = prepareOn(model, map, {fluxLoad(1, study.endCap(), flux)});
    const PreparedThermalLoads volumeOnly = prepareOn(model, map, {generationLoad(2, generation)});
    const PreparedThermalLoads mixed = prepareOn(model, map, {fluxLoad(1, study.endCap(), flux), generationLoad(2, generation)});

    std::vector<Power> expected;
    for (std::size_t i = 0; i < mixed.heatInput().size(); ++i) {
        expected.push_back(Power::fromSi(surfaceOnly.heatInput()[i].si() + volumeOnly.heatInput()[i].si()));
    }
    CHECK(maxDifference(mixed.heatInput(), expected) <= 1e-15 * maxAbs(mixed.heatInput()));
    const double expectedTotal = flux * kCapArea + generation * kBlockVolume;
    CHECK_THAT(mixed.totalHeatRate().si(), WithinRel(expectedTotal, 1e-12));
    CHECK_THAT(mixed.appliedSurfaceHeatRate().si(), WithinRel(flux * kCapArea, 1e-12));
    CHECK_THAT(mixed.volumetricGenerationRate().si(), WithinRel(generation * kBlockVolume, 1e-12));
    // The per-kind totals add up to the vector's: nothing is counted twice.
    CHECK_THAT(mixed.appliedSurfaceHeatRate().si() + mixed.volumetricGenerationRate().si(),
               WithinRel(mixed.totalHeatRate().si(), 1e-13));
    REQUIRE(mixed.contributions().size() == 2);
    CHECK(mixed.contributions()[0].kind == ThermalLoadKind::SurfaceHeatFlux);
    CHECK(mixed.contributions()[1].kind == ThermalLoadKind::VolumetricHeatSource);
    WARN(std::format("mixed: surface {:.9g} W + volume {:.9g} W = {:.9g} W (expected {:.9g})",
                     mixed.appliedSurfaceHeatRate().si(), mixed.volumetricGenerationRate().si(),
                     mixed.totalHeatRate().si(), expectedTotal));
}

TEST_CASE("ThermalLoad_SignedCancellation", "[thermal][load][superposition]") {
    // +L and -L on the same target cancel: the sign is real and the
    // accumulation is signed. An abs() anywhere would double instead.
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalLoads surfaceCancel =
        prepareOn(model, map, {fluxLoad(1, study.endCap(), 640.0), fluxLoad(2, study.endCap(), -640.0)});
    CHECK(maxAbs(surfaceCancel.heatInput()) == 0.0);
    CHECK(surfaceCancel.totalHeatRate().si() == 0.0);
    // With many tetrahedra per node the running sums are not exactly
    // reversible (a + b + c - a - b - c rounds), so the volumetric residual is
    // bounded in rounding units of the entries it cancels, not asserted zero.
    const PreparedThermalLoads volumeOnly = prepareOn(model, map, {generationLoad(1, 2.0e6)});
    const PreparedThermalLoads volumeCancel =
        prepareOn(model, map, {generationLoad(1, 2.0e6), generationLoad(2, -2.0e6)});
    const double residual = maxAbs(volumeCancel.heatInput());
    CHECK(residual <= 8.0 * std::numeric_limits<double>::epsilon() * maxAbs(volumeOnly.heatInput()));
    CHECK(std::abs(volumeCancel.totalHeatRate().si()) <= 8.0 * std::numeric_limits<double>::epsilon() * volumeOnly.totalHeatRate().si());
    WARN(std::format("signed cancellation: surface residual exactly 0; volumetric residual {:.2e} W against "
                     "entries of {:.3e} W ({:.2f} eps)",
                     residual, maxAbs(volumeOnly.heatInput()),
                     residual / (std::numeric_limits<double>::epsilon() * maxAbs(volumeOnly.heatInput()))));
    // The per-load totals are still what each load did.
    CHECK_THAT(surfaceCancel.contributions()[0].integrated.heatRate.si(), WithinRel(640.0 * kCapArea, 1e-13));
    CHECK_THAT(surfaceCancel.contributions()[1].integrated.heatRate.si(), WithinRel(-640.0 * kCapArea, 1e-13));
}

TEST_CASE("ThermalLoad_LoadOrderOnlyAffectsTheSummationOrder", "[thermal][load][superposition]") {
    // The same loads in two orders give the same vector to rounding, and each
    // order is itself reproducible bit for bit. Order is the definition's, never
    // an unordered container's.
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalLoad> a{fluxLoad(1, study.endCap(), 111.1), generationLoad(2, 3.3e5),
                                     fluxLoad(3, study.startCap(), -77.7)};
    const std::vector<ThermalLoad> b{a[2], a[0], a[1]};
    const PreparedThermalLoads pa = prepareOn(model, map, a);
    const PreparedThermalLoads pb = prepareOn(model, map, b);
    CHECK(maxDifference(pa.heatInput(), pb.heatInput()) <= 1e-14 * maxAbs(pa.heatInput()));
    // The contributions follow the order given.
    CHECK(pa.contributions()[0].load == lid(1));
    CHECK(pb.contributions()[0].load == lid(3));
    const PreparedThermalLoads again = prepareOn(model, map, a);
    CHECK(pa == again);
}

// ===========================================================================
// Mapping and refusals
// ===========================================================================

TEST_CASE("ThermalLoad_UnresolvedReferenceFailsAndBindsNothing", "[thermal][load][mapping]") {
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);

    SECTION("a face of another object names no face of this body") {
        const FaceName foreign{ObjectId::fromValue(9999), FaceSelector{.role = FaceRole::EndCap}};
        const std::vector<ThermalLoad> loads{fluxLoad(4, foreign, 100.0)};
        CHECK(thermal::thermalLoadProblem(model, map, loads) ==
              std::optional<LoadProblem>{LoadProblem::TargetUnresolved});
        const Result<PreparedThermalLoads> refused = thermal::prepareThermalLoads(model, map, loads);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == ErrorCode::NotFound);
        CHECK_THAT(refused.error().message, ContainsSubstring("Nothing nearby is substituted"));
        CHECK(thermal::faceTargetProblem(model.map(), foreign) == std::optional<TargetProblem>{TargetProblem::Unresolved});
    }

    SECTION("a role the body does not have is not bound to a cap") {
        // A well-formed selector that names no face here. The end cap and start
        // cap both EXIST and are nearby; neither is substituted.
        const FaceName absent{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
        const std::vector<ThermalLoad> loads{fluxLoad(5, absent, 100.0)};
        CHECK(thermal::thermalLoadProblem(model, map, loads) ==
              std::optional<LoadProblem>{LoadProblem::TargetUnresolved});
        const Result<PreparedThermalLoads> refused = thermal::prepareThermalLoads(model, map, loads);
        REQUIRE_FALSE(refused.has_value());
        // And P16 itself says unresolved, so the refusal is P16's verdict.
        const Result<meshing::BoundaryFacetSet> set = meshing::boundaryFacetsOf(model.map(), absent);
        REQUIRE(set.has_value());
        CHECK_FALSE(set->fullyResolved());
        CHECK(set->facets.empty());
    }

    SECTION("a resolvable face still works, so the refusal is about the target") {
        CHECK_FALSE(thermal::thermalLoadProblem(model, map, std::vector<ThermalLoad>{fluxLoad(6, study.endCap(), 100.0)}).has_value());
    }
}

TEST_CASE("ThermalLoad_MalformedSelectorFails", "[thermal][load][mapping]") {
    // A Side role with no entity is malformed on core's own terms, which is a
    // different failure from a well-formed reference that resolves to nothing.
    LoadStudy study;
    const ThermalModel model = study.model();
    const FaceName malformed{study.feature, FaceSelector{.role = FaceRole::Side}};
    const std::vector<ThermalLoad> loads{fluxLoad(7, malformed, 10.0)};
    const std::optional<LoadProblem> problem = thermal::thermalLoadProblem(model, dofMapOf(model), loads);
    INFO("problem " << (problem.has_value() ? thermal::toString(*problem) : "none"));
    REQUIRE(problem.has_value());
    CHECK((*problem == LoadProblem::TargetInvalid || *problem == LoadProblem::TargetUnresolved));
    CHECK_FALSE(thermal::prepareThermalLoads(model, dofMapOf(model), loads).has_value());
    WARN(std::format("a Side face with no entity is refused as {}", thermal::toString(*problem)));
}

TEST_CASE("ThermalLoad_EmptyTargetIsNotAZeroLoad", "[thermal][load][mapping]") {
    // An empty facet list or an empty element list must FAIL, not return a
    // successful zero. Presented through the seams, since P16 never hands
    // an empty set for a face that resolved.
    const Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}}, {});
    std::vector<Power> q = zeros(hand);
    const std::expected<IntegratedHeat, LoadFailure> noFacets = thermal::integrateSurfaceHeatFlux(
        hand.mesh, hand.dofs, std::span<const ElementId>{}, HeatFlux::fromSi(12.0), q);
    REQUIRE_FALSE(noFacets.has_value());
    CHECK(noFacets.error().problem == LoadProblem::TargetWithoutFacets);
    CHECK(maxAbs(q) == 0.0);

    const Hand body = makeHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    std::vector<Power> r = zeros(body);
    const std::expected<IntegratedHeat, LoadFailure> noElements = thermal::integrateVolumetricHeatSource(
        body.mesh, body.dofs, std::span<const ElementId>{}, VolumetricHeatSource::fromSi(24.0), r);
    REQUIRE_FALSE(noElements.has_value());
    CHECK(noElements.error().problem == LoadProblem::BodyWithoutElements);
}

TEST_CASE("ThermalLoad_NoNearestFaceRebind", "[thermal][load][mapping]") {
    // P16 reports an absent role as Unresolved, and this layer refuses it. The
    // test that nothing was bound: a load on an absent face produces no vector
    // at all, rather than a vector on the nearest cap.
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    for (const FaceRole role : {FaceRole::HoleBottom, FaceRole::CounterboreFloor, FaceRole::SpotfaceFloor}) {
        const FaceName absent{study.feature, FaceSelector{.role = role}};
        // P16 itself says these roles name nothing on a plain extrude -- asserted,
        // not skipped, so the test cannot quietly become about something else.
        const Result<meshing::BoundaryFacetSet> set = meshing::boundaryFacetsOf(model.map(), absent);
        REQUIRE(set.has_value());
        CHECK_FALSE(set->fullyResolved());
        CHECK(set->facets.empty());
        const Result<PreparedThermalLoads> refused =
            thermal::prepareThermalLoads(model, map, std::vector<ThermalLoad>{fluxLoad(1, absent, 1.0e6)});
        INFO("role " << static_cast<int>(role));
        CHECK_FALSE(refused.has_value());
    }
    // The real faces are untouched by the refusal.
    CHECK(prepare(study, {fluxLoad(1, study.endCap(), 100.0)}).totalHeatRate().si() > 0.0);
}

TEST_CASE("ThermalLoad_TargetProblemsHaveNames", "[thermal][load][mapping]") {
    CHECK(thermal::toString(TargetProblem::SelectorInvalid) == "selector_invalid");
    CHECK(thermal::toString(TargetProblem::Unresolved) == "unresolved");
    CHECK(thermal::toString(TargetProblem::WithoutFacets) == "without_facets");
}

TEST_CASE("ThermalLoad_OneInvalidLoadRefusesTheWholeSet", "[thermal][load][mapping]") {
    // No solve-ready vector may contain only the loads that happened to be
    // valid: that would silently drop the user's intent.
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName absent{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 100.0), fluxLoad(2, absent, 100.0),
                                         generationLoad(3, 1.0e5)};
    const Result<PreparedThermalLoads> refused = thermal::prepareThermalLoads(model, map, loads);
    REQUIRE_FALSE(refused.has_value());
    CHECK(thermal::thermalLoadProblem(model, map, loads) == std::optional<LoadProblem>{LoadProblem::TargetUnresolved});
    // The diagnostic names the offending load and not the valid ones.
    CHECK_THAT(refused.error().message, ContainsSubstring(std::format("{}", lid(2))));
    CHECK_THAT(refused.error().message, !ContainsSubstring(std::format("{} ", lid(1))));
    // And the same loads without the bad one are fine.
    CHECK_FALSE(thermal::thermalLoadProblem(model, map, std::vector<ThermalLoad>{loads[0], loads[2]}).has_value());
}

// ===========================================================================
// Remesh and currentness
// ===========================================================================

TEST_CASE("ThermalLoad_PreparedLoadBindsExactMeshSource", "[thermal][load][currentness]") {
    LoadStudy study;
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 400.0), generationLoad(2, 1.0e5)};
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalLoads prepared = prepareOn(model, map, loads);

    CHECK(prepared.describes(model.mesh().mesh()));
    CHECK(prepared.describes(map));
    CHECK(prepared.dofCount() == map.dofCount());
    CHECK(prepared.source().body == model.body());
    CHECK(prepared.source().mesh == model.mesh().mesh().stamp());
    CHECK(prepared.source().nodeCount == model.mesh().nodeCount());
    CHECK(prepared.source().geometry == model.geometryRevision());
    CHECK(prepared.source().loads == loads);
    CHECK(thermal::isCurrent(prepared, model, loads));
    CHECK(thermal::requireCurrent(prepared, model, loads).has_value());
    CHECK(thermal::staleReasons(prepared.source(), thermal::currentLoadSource(model, loads)).empty());
}

TEST_CASE("ThermalLoad_RemeshInvalidatesPreparedLoad", "[thermal][load][currentness]") {
    LoadStudy study;
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 400.0)};
    // A ThermalModel BORROWS the mesher's mesh and must not outlive a remesh
    // (ThermalModel's documented lifetime): it would then see the NEW mesh. So
    // everything about generation 1 that is needed afterwards is captured by
    // VALUE first, and the old model goes out of scope before the remesh.
    meshing::MeshStamp stamp1{};
    std::size_t nodes1 = 0;
    std::optional<ThermalDofMap> oldMap;
    std::optional<PreparedThermalLoads> p1Holder;
    {
        const ThermalModel m1 = study.model();
        oldMap = dofMapOf(m1);
        p1Holder = prepareOn(m1, *oldMap, loads);
        stamp1 = m1.mesh().mesh().stamp();
        nodes1 = m1.mesh().nodeCount();
    }
    const PreparedThermalLoads& p1 = *p1Holder;

    study.remesh();  // same body, same controls, a new generation
    const ThermalModel m2 = study.model();
    const ThermalDofMap map2 = dofMapOf(m2);

    CHECK_FALSE(m2.mesh().mesh().stamp() == stamp1);
    CHECK_FALSE(p1.describes(m2.mesh().mesh()));
    CHECK_FALSE(p1.describes(map2));
    CHECK_FALSE(thermal::isCurrent(p1, m2, loads));
    const std::vector<LoadStaleReason> reasons =
        thermal::staleReasons(p1.source(), thermal::currentLoadSource(m2, loads));
    CHECK(std::ranges::find(reasons, LoadStaleReason::Mesh) != reasons.end());
    const Result<void> required = thermal::requireCurrent(p1, m2, loads);
    REQUIRE_FALSE(required.has_value());
    CHECK(required.error().code == ErrorCode::FailedPrecondition);
    CHECK_THAT(required.error().message, ContainsSubstring("mesh"));

    // Re-preparing against the new mesh gives a fresh, current load.
    const PreparedThermalLoads p2 = prepareOn(m2, map2, loads);
    CHECK(thermal::isCurrent(p2, m2, loads));
    CHECK_THAT(p2.totalHeatRate().si(), WithinRel(p1.totalHeatRate().si(), 1e-13));
    // A prepared load cannot be applied to the wrong numbering either.
    const Result<PreparedThermalLoads> wrong = thermal::prepareThermalLoads(m2, *oldMap, loads);
    CHECK_FALSE(wrong.has_value());
    WARN(std::format("remesh: stamp changed, nodes {} -> {}; P1 stale (reason mesh), P2 current, total {:.9g} W both",
                     nodes1, m2.mesh().nodeCount(), p2.totalHeatRate().si()));
}

TEST_CASE("ThermalLoad_SameCountsNewMeshIsStale", "[thermal][load][currentness]") {
    // THE ADVERSARIAL CASE: a remesh with identical node and element counts.
    // The vector is exactly as long as the new mesh needs, and it is still
    // stale, because currentness is the source and not the size.
    LoadStudy study;
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 400.0)};
    std::optional<PreparedThermalLoads> p1Holder;
    std::size_t nodes1 = 0;
    std::size_t tets1 = 0;
    std::size_t facets1 = 0;
    {
        const ThermalModel m1 = study.model();
        p1Holder = prepareOn(m1, dofMapOf(m1), loads);
        nodes1 = m1.mesh().nodeCount();
        tets1 = m1.mesh().tetrahedronCount();
        facets1 = m1.mesh().boundaryTriangleCount();
    }
    const PreparedThermalLoads& p1 = *p1Holder;

    study.remesh();
    const ThermalModel m2 = study.model();
    REQUIRE(m2.mesh().nodeCount() == nodes1);
    REQUIRE(m2.mesh().tetrahedronCount() == tets1);
    REQUIRE(m2.mesh().boundaryTriangleCount() == facets1);
    CHECK(p1.heatInput().size() == m2.mesh().nodeCount());
    CHECK_FALSE(p1.describes(m2.mesh().mesh()));
    CHECK_FALSE(thermal::isCurrent(p1, m2, loads));
    WARN(std::format("same counts: {} nodes, {} tets, {} boundary facets in both generations; vector length {} "
                     "matches M2 and P1 is still stale",
                     nodes1, tets1, facets1, p1.heatInput().size()));
}

TEST_CASE("ThermalLoad_SameIdsNewMeshIsStale", "[thermal][load][currentness]") {
    // Identical numeric NodeIds AND ElementIds as well as identical counts.
    LoadStudy study;
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 400.0)};
    std::optional<PreparedThermalLoads> p1Holder;
    meshing::MeshStamp stamp1{};
    std::vector<NodeId> nodeIds1;
    std::vector<ElementId> tetIds1;
    std::vector<ElementId> facets1;
    {
        const ThermalModel m1 = study.model();
        p1Holder = prepareOn(m1, dofMapOf(m1), loads);
        stamp1 = m1.mesh().mesh().stamp();
        for (const meshing::Node& node : m1.mesh().mesh().nodes()) {
            nodeIds1.push_back(node.id);
        }
        for (const meshing::Tetrahedron& tet : m1.mesh().mesh().tetrahedra()) {
            tetIds1.push_back(tet.id);
        }
        facets1 = facetsOf(m1, study.endCap());
    }
    const PreparedThermalLoads& p1 = *p1Holder;

    study.remesh();
    const ThermalModel m2 = study.model();
    std::vector<NodeId> nodeIds2;
    for (const meshing::Node& node : m2.mesh().mesh().nodes()) {
        nodeIds2.push_back(node.id);
    }
    std::vector<ElementId> tetIds2;
    for (const meshing::Tetrahedron& tet : m2.mesh().mesh().tetrahedra()) {
        tetIds2.push_back(tet.id);
    }
    REQUIRE(nodeIds1 == nodeIds2);
    REQUIRE(tetIds1 == tetIds2);
    CHECK(facetsOf(m2, study.endCap()) == facets1);
    CHECK_FALSE(stamp1 == m2.mesh().mesh().stamp());
    CHECK_FALSE(thermal::isCurrent(p1, m2, loads));
    CHECK_FALSE(p1.describes(m2.mesh().mesh()));
    WARN("same ids: every NodeId, ElementId and cap facet handle identical across the remesh; P1 stale by stamp alone");
}

TEST_CASE("ThermalLoad_RemeshReResolvesGeometryReference", "[thermal][load][currentness]") {
    // Coarse and fine meshes of ONE cylinder, genuinely different. The canonical
    // FaceName is the same value throughout; the facets, nodes and unknowns are
    // not; and the total heat rate is mesh-independent to P16's tolerance.
    RoundStudy part;
    const FaceName cap = part.endCap();
    const FaceName capBefore = cap;
    const double flux = 1000.0;
    const std::vector<ThermalLoad> loads{fluxLoad(1, cap, flux), generationLoad(2, 5.0e6)};
    const std::vector<ThermalLoad> loadsBefore = loads;

    const ThermalModel coarse = part.model(part.coarse);
    const ThermalModel fine = part.model(part.fine);
    const ThermalDofMap coarseMap = dofMapOf(coarse);
    const ThermalDofMap fineMap = dofMapOf(fine);

    // GENUINELY DIFFERENT, before anything is compared.
    REQUIRE(fine.mesh().tetrahedronCount() > coarse.mesh().tetrahedronCount());
    REQUIRE(fine.mesh().nodeCount() > coarse.mesh().nodeCount());
    REQUIRE(fine.mesh().boundaryTriangleCount() > coarse.mesh().boundaryTriangleCount());
    CHECK_FALSE(coarse.mesh().mesh().stamp() == fine.mesh().mesh().stamp());

    const PreparedThermalLoads pc = prepareOn(coarse, coarseMap, loads);
    const PreparedThermalLoads pf = prepareOn(fine, fineMap, loads);

    // The canonical intent is untouched by resolution.
    CHECK(cap == capBefore);
    CHECK(loads == loadsBefore);

    // Re-resolved: different facets, different node counts, the same face.
    const std::vector<ElementId> coarseFacets = facetsOf(coarse, cap);
    const std::vector<ElementId> fineFacets = facetsOf(fine, cap);
    CHECK(fineFacets.size() > coarseFacets.size());
    CHECK(pc.dofCount() == coarse.mesh().nodeCount());
    CHECK(pf.dofCount() == fine.mesh().nodeCount());
    CHECK_FALSE(thermal::isCurrent(pc, fine, loads));
    CHECK_FALSE(thermal::isCurrent(pf, coarse, loads));

    // MESH-INDEPENDENT TOTALS. The cap is a disc of radius 6 mm; a polygon
    // inscribed in it has slightly less area, less so as the mesh refines.
    const double capCad = std::numbers::pi * RoundStudy::kRadius * RoundStudy::kRadius;
    const double volCad = capCad * RoundStudy::kLength;
    const double areaCoarse = areaOfFacets(coarse.mesh().mesh(), coarseFacets);
    const double areaFine = areaOfFacets(fine.mesh().mesh(), fineFacets);
    const double volCoarse = volumeOfTetrahedra(coarse.mesh().mesh());
    const double volFine = volumeOfTetrahedra(fine.mesh().mesh());

    // Conversion identities, algebraic.
    CHECK_THAT(pc.appliedSurfaceHeatRate().si(), WithinRel(flux * areaCoarse, 1e-13));
    CHECK_THAT(pf.appliedSurfaceHeatRate().si(), WithinRel(flux * areaFine, 1e-13));
    CHECK_THAT(pc.volumetricGenerationRate().si(), WithinRel(5.0e6 * volCoarse, 1e-13));
    CHECK_THAT(pf.volumetricGenerationRate().si(), WithinRel(5.0e6 * volFine, 1e-13));
    // Mesh fidelity, P16's geometric tolerance: the fine mesh is at least as
    // close to the CAD figure as the coarse one, and both within a few percent.
    const double capErrorCoarse = relativeError(areaCoarse, capCad);
    const double capErrorFine = relativeError(areaFine, capCad);
    const double volErrorCoarse = relativeError(volCoarse, volCad);
    const double volErrorFine = relativeError(volFine, volCad);
    CHECK(capErrorFine <= capErrorCoarse);
    CHECK(volErrorFine <= volErrorCoarse);
    CHECK(capErrorCoarse < 0.1);
    CHECK(volErrorCoarse < 0.1);
    // The two prepared totals agree to the larger of the two mesh errors.
    const double bound = std::max(capErrorCoarse, volErrorCoarse) + 1e-12;
    CHECK(relativeError(pf.appliedSurfaceHeatRate().si(), pc.appliedSurfaceHeatRate().si()) <= bound);
    CHECK(relativeError(pf.volumetricGenerationRate().si(), pc.volumetricGenerationRate().si()) <= bound);

    WARN(std::format("coarse: {} nodes {} tets {} boundary facets, cap {} facets, mesh cap area {:.9g}, volume "
                     "{:.9g}; fine: {} nodes {} tets {} boundary facets, cap {} facets, cap area {:.9g}, volume "
                     "{:.9g}; CAD cap {:.9g} m^2, volume {:.9g} m^3",
                     coarse.mesh().nodeCount(), coarse.mesh().tetrahedronCount(), coarse.mesh().boundaryTriangleCount(),
                     coarseFacets.size(), areaCoarse, volCoarse, fine.mesh().nodeCount(),
                     fine.mesh().tetrahedronCount(), fine.mesh().boundaryTriangleCount(), fineFacets.size(),
                     areaFine, volFine, capCad, volCad));
    WARN(std::format("totals: q'' A coarse {:.9g} W, fine {:.9g} W, CAD {:.9g} W; q''' V coarse {:.9g} W, fine "
                     "{:.9g} W, CAD {:.9g} W",
                     pc.appliedSurfaceHeatRate().si(), pf.appliedSurfaceHeatRate().si(), flux * capCad,
                     pc.volumetricGenerationRate().si(), pf.volumetricGenerationRate().si(), 5.0e6 * volCad));
}

TEST_CASE("ThermalLoad_ADifferentBodyIsStale", "[thermal][load][currentness]") {
    // A prepared load names the solid it was prepared on. Another solid in the
    // same document is another source, whatever its loads and counts: the
    // reasons include Body and Mesh.
    RoundStudy part;
    const std::vector<ThermalLoad> loads{generationLoad(1, 1.0e5)};
    const ThermalModel cylinder = part.model(part.coarse);
    const PreparedThermalLoads prepared = prepareOn(cylinder, dofMapOf(cylinder), loads);
    const ThermalModel block = part.model(part.otherControl);
    REQUIRE_FALSE(cylinder.body() == block.body());
    const std::vector<LoadStaleReason> reasons =
        thermal::staleReasons(prepared.source(), thermal::currentLoadSource(block, loads));
    CHECK(std::ranges::find(reasons, LoadStaleReason::Body) != reasons.end());
    CHECK(std::ranges::find(reasons, LoadStaleReason::Mesh) != reasons.end());
    CHECK_FALSE(thermal::isCurrent(prepared, block, loads));
    // And a heat input is never applied to the other body's numbering.
    CHECK(thermal::thermalLoadProblem(block, dofMapOf(cylinder), loads) == std::optional<LoadProblem>{LoadProblem::DofMapMismatch});
}

TEST_CASE("ThermalLoad_EditStalesPreparedLoad", "[thermal][load][currentness]") {
    // Every kind of edit to the canonical loads makes a prepared load stale,
    // even though the mesh, the geometry and the vector's length are unchanged.
    LoadStudy study;
    const std::vector<ThermalLoad> original{fluxLoad(1, study.endCap(), 100.0), generationLoad(2, 1.0e5)};
    const ThermalModel model = study.model();
    const PreparedThermalLoads prepared = prepareOn(model, dofMapOf(model), original);
    REQUIRE(thermal::isCurrent(prepared, model, original));

    const auto reasonsFor = [&](const std::vector<ThermalLoad>& edited) {
        return thermal::staleReasons(prepared.source(), thermal::currentLoadSource(model, edited));
    };
    const std::vector<LoadStaleReason> loadsOnly{LoadStaleReason::Loads};

    SECTION("the value: 100 -> 200 W/m^2, the target unchanged") {
        std::vector<ThermalLoad> edited = original;
        edited[0] = fluxLoad(1, study.endCap(), 200.0);
        CHECK(reasonsFor(edited) == loadsOnly);
        CHECK_FALSE(thermal::isCurrent(prepared, model, edited));
    }
    SECTION("the target: end cap -> start cap") {
        std::vector<ThermalLoad> edited = original;
        edited[0] = fluxLoad(1, study.startCap(), 100.0);
        CHECK(reasonsFor(edited) == loadsOnly);
    }
    SECTION("the generation") {
        std::vector<ThermalLoad> edited = original;
        edited[1] = generationLoad(2, -1.0e5);
        CHECK(reasonsFor(edited) == loadsOnly);
    }
    SECTION("a load added, removed or reordered") {
        std::vector<ThermalLoad> added = original;
        added.push_back(fluxLoad(3, study.startCap(), 5.0));
        CHECK(reasonsFor(added) == loadsOnly);
        CHECK(reasonsFor({original[0]}) == loadsOnly);
        CHECK(reasonsFor({original[1], original[0]}) == loadsOnly);
    }
    SECTION("an identity change with the same physics") {
        std::vector<ThermalLoad> edited = original;
        edited[0] = fluxLoad(9, study.endCap(), 100.0);
        CHECK(reasonsFor(edited) == loadsOnly);
    }
}

TEST_CASE("ThermalLoad_EditStalesThermalResult", "[thermal][load][currentness]") {
    // P18-DATA's mechanism, exercised by a LOAD edit: a result computed under
    // flux 100 is stale after the definition's flux becomes 200, for reason
    // Analysis, with the mesh still current. No solve is needed.
    LoadStudy base;
    REQUIRE(base.setDefinition(ThermalAnalysisDefinition{.mesh = base.control, .loads = {fluxLoad(1, base.endCap(), 100.0)}}).has_value());
    const ThermalResultSource before = base.currentSource();
    const meshing::VolumeMesh& held = base.volume();
    const Result<ThermalResult> created = ThermalResult::create(
        before, held, std::vector<Temperature>(held.nodeCount(), 293.15_K),
        std::vector<thermal::TemperatureGradient3>(held.tetrahedronCount()),
        std::vector<thermal::HeatFlux3>(held.tetrahedronCount()), thermal::HeatBalance{}, std::nullopt);
    REQUIRE(created.has_value());
    CHECK(thermal::resultCurrency(&*created, before) == ResultCurrency::Current);

    ThermalAnalysisDefinition edited = base.definition();
    edited.loads[0] = fluxLoad(1, base.endCap(), 200.0);
    REQUIRE(base.setDefinition(edited).has_value());

    const ThermalResultSource after = base.currentSource();
    CHECK(after.mesh == before.mesh);
    CHECK(after.analysisRevision != before.analysisRevision);
    CHECK(thermal::resultCurrency(&*created, after) == ResultCurrency::Stale);
    CHECK(thermal::staleReasons(created->source(), after) == std::vector{StaleReason::Analysis});
}

TEST_CASE("ThermalLoad_MaterialEditDoesNotInvalidatePreparedGeometryTarget", "[thermal][load][currentness]") {
    // A conductivity edit leaves the mesh, the mapping and the loads alone, so
    // the prepared heat input is still exactly right -- while the thermal RESULT
    // goes stale on its own material provenance. Two gates, two dependencies.
    LoadStudy base;
    const std::vector<ThermalLoad> loads{fluxLoad(1, base.endCap(), 100.0), generationLoad(2, 1.0e5)};
    REQUIRE(base.setDefinition(ThermalAnalysisDefinition{.mesh = base.control, .loads = loads}).has_value());
    const ThermalModel m1 = base.model();
    const PreparedThermalLoads prepared = prepareOn(m1, dofMapOf(m1), loads);
    const ThermalResultSource sourceBefore = base.currentSource();

    materials::ThermalProperties thermalProperties =
        features::findMaterial(base.document, base.material)->definition().thermal;
    thermalProperties.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(50_W_per_m_K);
    REQUIRE(features::setMaterialThermal(base.document, base.material, thermalProperties).has_value());

    const ThermalModel m2 = base.model();
    CHECK(thermal::isCurrent(prepared, m2, loads));
    CHECK(thermal::requireCurrent(prepared, m2, loads).has_value());
    CHECK(maxDifference(prepared.heatInput(), prepareOn(m2, dofMapOf(m2), loads).heatInput()) == 0.0);
    // The material is not in the prepared source at all.
    CHECK(thermal::staleReasons(prepared.source(), thermal::currentLoadSource(m2, loads)).empty());
    // The result's own gate does see it.
    const ThermalResultSource sourceAfter = base.currentSource();
    CHECK(thermal::staleReasons(sourceBefore, sourceAfter) == std::vector{StaleReason::Material});
    WARN("k 45 -> 50 W/(m K): prepared heat input still current and bit-identical; a result would be stale for "
         "reason material only");
}

TEST_CASE("ThermalLoad_SolverAndBoundaryConditionEditsDoNotInvalidate", "[thermal][load][currentness]") {
    // Neither a solver tolerance nor a boundary condition is an input of a
    // prescribed heat load, so editing them leaves a prepared load current and
    // the definition overload's answer unchanged. (They DO move the analysis
    // revision, which stales a RESULT; that is a different gate.)
    LoadStudy study;
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 100.0)};
    REQUIRE(study.setDefinition(ThermalAnalysisDefinition{.mesh = study.control, .loads = loads}).has_value());
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalLoads before = prepareOn(model, map, loads);
    const std::uint64_t analysisRevision = thermal::findThermalAnalysis(study.document, study.analysis)->revision();

    ThermalAnalysisDefinition edited = study.definition();
    edited.solver.relativeResidualTolerance = 1.0e-6;
    edited.boundaryConditions.push_back(thermal::ThermalBoundaryCondition{
        ThermalBoundaryConditionId::fromValue(1),
        thermal::PrescribedTemperature{.face = study.startCap(), .value = 300_K}});
    REQUIRE(study.setDefinition(edited).has_value());
    CHECK(thermal::findThermalAnalysis(study.document, study.analysis)->revision() != analysisRevision);

    CHECK(thermal::isCurrent(before, model, edited.loads));
    const Result<PreparedThermalLoads> viaDefinition = thermal::prepareThermalLoads(model, map, edited);
    REQUIRE(viaDefinition.has_value());
    CHECK(maxDifference(viaDefinition->heatInput(), before.heatInput()) == 0.0);
    CHECK(viaDefinition->source() == before.source());
}

TEST_CASE("ThermalLoad_GeometryEditStalesPreparedLoad", "[thermal][load][currentness]") {
    // G1 -> G2 by a depth edit. The mesh is stale, so the thermal input boundary
    // refuses to produce a model at all; after remeshing, the old prepared load
    // is stale for the geometry AND the mesh.
    LoadStudy study;
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 100.0), generationLoad(2, 1.0e5)};
    const ThermalModel m1 = study.model();
    const PreparedThermalLoads p1 = prepareOn(m1, dofMapOf(m1), loads);
    const meshing::GeometryRevision g1 = m1.geometryRevision();

    REQUIRE(study.document
                .modifyObject<features::ExtrudeFeature>(
                    study.feature,
                    [](features::ExtrudeFeature& extrude) -> Result<bool> {
                        auto definition = extrude.definition();
                        definition.depth = 35_mm;
                        return extrude.setDefinition(definition).has_value();
                    })
                .has_value());
    requireReport(study.regenerator, study.document);
    // The gate: a stale mesh yields no model, so nothing can be prepared on it.
    CHECK_FALSE(study.tryModel().has_value());

    study.remesh();
    const ThermalModel m2 = study.model();
    CHECK_FALSE(m2.geometryRevision() == g1);
    const std::vector<LoadStaleReason> reasons = thermal::staleReasons(p1.source(), thermal::currentLoadSource(m2, loads));
    CHECK(std::ranges::find(reasons, LoadStaleReason::Geometry) != reasons.end());
    CHECK(std::ranges::find(reasons, LoadStaleReason::Mesh) != reasons.end());
    // The same canonical FaceName still resolves on the edited body, and the
    // heat tracks the new geometry: end cap area unchanged, volume 35/20 times.
    const PreparedThermalLoads p2 = prepareOn(m2, dofMapOf(m2), loads);
    CHECK_THAT(p2.appliedSurfaceHeatRate().si(), WithinRel(100.0 * kCapArea, 1e-12));
    CHECK_THAT(p2.volumetricGenerationRate().si(), WithinRel(1.0e5 * kCapArea * 0.035, 1e-12));
}

TEST_CASE("ThermalLoad_DocumentLoadsOnlyAreRead", "[thermal][load][currentness]") {
    // The definition overload reads definition.loads and nothing else: it
    // cannot depend on the mode, the initial condition, the solver or a BC.
    LoadStudy study;
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 100.0)};
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    ThermalAnalysisDefinition steady{.mesh = study.control, .loads = loads};
    ThermalAnalysisDefinition transient{
        .mesh = study.control,
        .mode = thermal::ThermalAnalysisMode::Transient,
        .loads = loads,
        .initialCondition = thermal::UniformInitialTemperature{.value = 350_K},
        .transient = thermal::TransientSettings{.timeStep = 0.1_s, .endTime = 5_s},
    };
    const Result<PreparedThermalLoads> a = thermal::prepareThermalLoads(model, map, steady);
    const Result<PreparedThermalLoads> b = thermal::prepareThermalLoads(model, map, transient);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);
}

// ===========================================================================
// Sparse node identities
// ===========================================================================

TEST_CASE("ThermalLoad_SparseNodeIdsUseThermalDofMap", "[thermal][load][sparse]") {
    // Handles 7, 104, 9001, 500000. The vector has FOUR entries, indexed by
    // unknown; indexing by NodeId.value() would be out of range at once.
    const Hand hand = makeHand({{7, at(0, 0, 0)}, {104, at(1, 0, 0)}, {9001, at(0, 1, 0)}, {500000, at(0, 0, 1)}},
                               {{0, 1, 2}}, {{0, 1, 2, 3}});
    std::vector<Power> q = zeros(hand);
    (void)surface(hand, 8.0, q);
    (void)volume(hand, 24.0, q);
    REQUIRE(q.size() == 4);
    // Triangle (7, 104, 9001): A = 1/2, q'' A / 3 = 4/3 W each. Tet: 24/6/4 = 1 W
    // at each of the four. Node 500000 gets the tet share only.
    CHECK_THAT(entryOf(hand, q, 7), WithinRel(4.0 / 3.0 + 1.0, 1e-15));
    CHECK_THAT(entryOf(hand, q, 104), WithinRel(4.0 / 3.0 + 1.0, 1e-15));
    CHECK_THAT(entryOf(hand, q, 9001), WithinRel(4.0 / 3.0 + 1.0, 1e-15));
    CHECK_THAT(entryOf(hand, q, 500000), WithinRel(1.0, 1e-15));
    // The entries are at the ORDINALS 0..3.
    const std::array<std::pair<NodeId::ValueType, std::size_t>, 4> ordinals{{{7, 0}, {104, 1}, {9001, 2}, {500000, 3}}};
    for (const auto& [handle, ordinal] : ordinals) {
        const Result<ThermalDofIndex> dof = hand.dofs.dofOf(NodeId::fromValue(handle));
        REQUIRE(dof.has_value());
        CHECK(dof->value() == ordinal);
    }
    WARN("sparse ids [7 104 9001 500000]: vector length 4 (not 500001), entries at unknowns [0 1 2 3]");
}

TEST_CASE("ThermalLoad_SparseLookupByUnknownAnswersOnlyForUnknownsItHas", "[thermal][load][sparse]") {
    // heatInputAt takes a ThermalDofIndex, never a NodeId, and refuses an
    // unknown past the end rather than reading beyond the vector.
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalLoads prepared = prepareOn(model, map, {fluxLoad(1, study.endCap(), 100.0)});
    const std::size_t n = prepared.heatInput().size();
    REQUIRE(n == map.dofCount());
    for (std::size_t i = 0; i < n; ++i) {
        const std::optional<Power> at_ = prepared.heatInputAt(ThermalDofIndex::fromValue(i));
        REQUIRE(at_.has_value());
        CHECK(*at_ == prepared.heatInput()[i]);
    }
    CHECK_FALSE(prepared.heatInputAt(ThermalDofIndex::fromValue(n)).has_value());
    CHECK_FALSE(prepared.heatInputAt(ThermalDofIndex::fromValue(n + 1000)).has_value());
}

TEST_CASE("ThermalLoad_LargeSparseNodeIdDoesNotSizeTheVector", "[thermal][load][sparse]") {
    // A handle near 2^32 on a three-node mesh: the vector is three entries long.
    constexpr NodeId::ValueType kLarge = 4294967294ULL;
    const Hand hand = makeHand({{1, at(0, 0, 0)}, {2, at(2, 0, 0)}, {kLarge, at(0, 3, 0)}}, {{0, 1, 2}}, {});
    std::vector<Power> q = zeros(hand);
    const IntegratedHeat done = surface(hand, 12.0, q);
    CHECK(q.size() == 3);
    CHECK(q.size() * sizeof(Power) == 3 * sizeof(double));
    CHECK(entryOf(hand, q, kLarge) == 12.0);
    CHECK(done.heatRate.si() == 36.0);
    WARN("max NodeId 4 294 967 294, 3 nodes: heat-input vector of 3 entries (24 bytes)");
}

// ===========================================================================
// Invalid input
// ===========================================================================

TEST_CASE("ThermalLoad_RejectsNonFiniteHeatFlux", "[thermal][load][invalid]") {
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    for (const double bad : {kNaN, kInf, -kInf}) {
        INFO("q'' " << bad);
        const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), bad)};
        CHECK(thermal::thermalLoadProblem(model, map, loads) == std::optional<LoadProblem>{LoadProblem::NonFiniteValue});
        CHECK_FALSE(thermal::prepareThermalLoads(model, map, loads).has_value());
        CHECK_FALSE(thermal::validate(loads[0]).has_value());
    }
    // And through the seam, with a hand-built mesh.
    const Hand hand = makeHand(referenceTriangleNodes(), {{0, 1, 2}}, {});
    std::vector<Power> q = zeros(hand);
    const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateSurfaceHeatFlux(
        hand.mesh, hand.dofs, hand.triangles, HeatFlux::fromSi(kNaN), q);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == LoadProblem::NonFiniteValue);
}

TEST_CASE("ThermalLoad_RejectsNonFiniteGeneration", "[thermal][load][invalid]") {
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    for (const double bad : {kNaN, kInf, -kInf}) {
        INFO("q''' " << bad);
        const std::vector<ThermalLoad> loads{generationLoad(1, bad)};
        CHECK(thermal::thermalLoadProblem(model, map, loads) == std::optional<LoadProblem>{LoadProblem::NonFiniteValue});
        CHECK_FALSE(thermal::validate(loads[0]).has_value());
    }
    const Hand hand = makeHand(unitTetNodes(), {}, {{0, 1, 2, 3}});
    std::vector<Power> q = zeros(hand);
    const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateVolumetricHeatSource(
        hand.mesh, hand.dofs, hand.tets, VolumetricHeatSource::fromSi(kInf), q);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == LoadProblem::NonFiniteValue);
}

TEST_CASE("ThermalLoad_ZeroAndNegativeValuesArePreservedExactly", "[thermal][load][invalid]") {
    // Zero is a valid load: a zero contribution, with its target still
    // resolved. Negative is heat extraction and is NOT abs()-corrected.
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const PreparedThermalLoads zero = prepareOn(model, map, {fluxLoad(1, study.endCap(), 0.0), generationLoad(2, 0.0)});
    CHECK(maxAbs(zero.heatInput()) == 0.0);
    CHECK(zero.contributions()[0].integrated.entities > 0);
    CHECK_THAT(zero.contributions()[0].integrated.area.si(), WithinRel(kCapArea, 1e-12));
    CHECK(zero.totalHeatRate().si() == 0.0);

    const PreparedThermalLoads positive = prepareOn(model, map, {fluxLoad(1, study.endCap(), 321.5)});
    const PreparedThermalLoads negative = prepareOn(model, map, {fluxLoad(1, study.endCap(), -321.5)});
    for (std::size_t i = 0; i < positive.heatInput().size(); ++i) {
        CHECK(negative.heatInput()[i].si() == -positive.heatInput()[i].si());
    }
    CHECK(negative.totalHeatRate().si() < 0.0);

    // A zero load on an UNRESOLVED target is still refused: zero is a value,
    // not a licence to skip resolution.
    const FaceName absent{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    CHECK(thermal::thermalLoadProblem(model, map, std::vector<ThermalLoad>{fluxLoad(3, absent, 0.0)}) ==
          std::optional<LoadProblem>{LoadProblem::TargetUnresolved});
}

TEST_CASE("ThermalLoad_ZeroAreaFacetFails", "[thermal][load][invalid]") {
    // Three collinear nodes: a triangle of zero area. It must not become a
    // silent zero contribution.
    const Hand hand = makeHand({{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(2, 0, 0)}}, {{0, 1, 2}}, {});
    std::vector<Power> q = zeros(hand);
    const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateSurfaceHeatFlux(
        hand.mesh, hand.dofs, hand.triangles, HeatFlux::fromSi(12.0), q);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().problem == LoadProblem::DegenerateFacet);
    CHECK(refused.error().error.code == ErrorCode::FailedPrecondition);
}

TEST_CASE("ThermalLoad_ZeroVolumeAndInvertedTetFail", "[thermal][load][invalid]") {
    SECTION("four coplanar nodes: zero volume") {
        const Hand hand = makeHand({{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}, {4, at(1, 1, 0)}}, {},
                                   {{0, 1, 2, 3}});
        std::vector<Power> q = zeros(hand);
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateVolumetricHeatSource(
            hand.mesh, hand.dofs, hand.tets, VolumetricHeatSource::fromSi(24.0), q);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::InvalidElement);
    }
    SECTION("an inverted tetrahedron is refused, not integrated on |V|") {
        // Nodes 1 and 2 swapped: det J flips sign. An abs(V) would accept it and
        // generate the same heat as the valid element.
        const Hand hand = makeHand(unitTetNodes(), {}, {{1, 0, 2, 3}});
        REQUIRE(tetVolumeByHand(at(1, 0, 0), at(0, 0, 0), at(0, 1, 0), at(0, 0, 1)) < 0.0);
        std::vector<Power> q = zeros(hand);
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateVolumetricHeatSource(
            hand.mesh, hand.dofs, hand.tets, VolumetricHeatSource::fromSi(24.0), q);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::InvalidElement);
        CHECK_THAT(refused.error().error.message, ContainsSubstring("inverted"));
    }
}

TEST_CASE("ThermalLoad_NonFinitePreparedContributionFails", "[thermal][load][invalid]") {
    // A finite flux whose share overflows must be refused, not accepted as an
    // infinite entry; and a running sum that overflows although every share is
    // finite must be refused too. Two different gates, two different sections.
    SECTION("the share overflows") {
        // A = 300 m^2, so the share is 1.7e308 * 100 = 1.7e310.
        const Hand hand = makeHand(referenceTriangleNodes(10.0), {{0, 1, 2}}, {});
        std::vector<Power> q = zeros(hand);
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateSurfaceHeatFlux(
            hand.mesh, hand.dofs, hand.triangles, HeatFlux::fromSi(1.7e308), q);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::NonFiniteResult);
        // WHICH gate fired is part of the claim: the share, not the running sum.
        CHECK_THAT(refused.error().error.message, ContainsSubstring("share of facet"));
    }
    SECTION("the SUM of two finite shares overflows") {
        // Two 3 m^2 triangles sharing the diagonal; each share is 1e308 (finite)
        // and a diagonal node receives two of them: 2e308 does not fit. This is
        // the running-sum gate, reached only because the share is formed as
        // q'' * (A / 3) and so does not overflow first.
        const Hand pair = makeHand({{1, at(0, 0, 0)}, {2, at(2, 0, 0)}, {3, at(2, 3, 0)}, {4, at(0, 3, 0)}},
                                   {{0, 1, 2}, {0, 2, 3}}, {});
        std::vector<Power> q = zeros(pair);
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateSurfaceHeatFlux(
            pair.mesh, pair.dofs, pair.triangles, HeatFlux::fromSi(1.0e308), q);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::NonFiniteResult);
        // The SUM gate, not the share gate: every share here is finite (1e308),
        // which holds only because a share is formed as q'' * (A / 3) and not
        // (q'' * A) / 3, whose product 3e308 overflows first.
        CHECK_THAT(refused.error().error.message, ContainsSubstring("superposing facet"));
        CHECK_THAT(refused.error().error.message, !ContainsSubstring("share of facet"));
    }
    SECTION("a volumetric share overflows") {
        const Hand body = makeHand(unitTetNodes(1.0e100), {}, {{0, 1, 2, 3}});
        std::vector<Power> q = zeros(body);
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateVolumetricHeatSource(
            body.mesh, body.dofs, body.tets, VolumetricHeatSource::fromSi(1.0e100), q);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::NonFiniteResult);
    }
}

TEST_CASE("ThermalLoad_ValuesAreCheckedBeforeTargetsAreResolved", "[thermal][load][invalid]") {
    // The order of the checks is part of the contract, and it makes the first
    // diagnostic deterministic: duplicate identities, then every load's VALUE,
    // then each target in canonical order. A NaN in the second load is reported
    // before an unresolvable target in the first. (A survivor of the first
    // mutation batch: the seam also checks the value, so deleting this check was
    // invisible until an input made the order matter.)
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName absent{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    const std::vector<ThermalLoad> loads{fluxLoad(1, absent, 10.0), fluxLoad(2, study.endCap(), kNaN)};
    CHECK(thermal::thermalLoadProblem(model, map, loads) == std::optional<LoadProblem>{LoadProblem::NonFiniteValue});
    // With the NaN repaired the unresolved target is what remains.
    const std::vector<ThermalLoad> repaired{loads[0], fluxLoad(2, study.endCap(), 10.0)};
    CHECK(thermal::thermalLoadProblem(model, map, repaired) == std::optional<LoadProblem>{LoadProblem::TargetUnresolved});
    // And duplicate identities come before values.
    const std::vector<ThermalLoad> twice{fluxLoad(5, study.endCap(), kNaN), generationLoad(5, 1.0)};
    CHECK(thermal::thermalLoadProblem(model, map, twice) == std::optional<LoadProblem>{LoadProblem::DuplicateLoadId});
}

TEST_CASE("ThermalLoad_DuplicateLoadIdsAreRefused", "[thermal][load][invalid]") {
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 100.0), generationLoad(1, 2.0e5)};
    CHECK(thermal::thermalLoadProblem(model, map, loads) == std::optional<LoadProblem>{LoadProblem::DuplicateLoadId});
    const Result<PreparedThermalLoads> refused = thermal::prepareThermalLoads(model, map, loads);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == ErrorCode::InvalidArgument);
    // The definition-level validation refuses the same thing at entry.
    CHECK_FALSE(thermal::validate(ThermalAnalysisDefinition{.mesh = study.control, .loads = loads}).has_value());
}

TEST_CASE("ThermalLoad_RepeatedAndForeignEntitiesAreRefused", "[thermal][load][invalid]") {
    const Hand hand = makeHand({{1, at(0, 0, 0)}, {2, at(2, 0, 0)}, {3, at(2, 3, 0)}, {4, at(0, 3, 0)}},
                                {{0, 1, 2}, {0, 2, 3}}, {});
    std::vector<Power> q = zeros(hand);

    SECTION("a repeated facet would be counted twice") {
        const std::vector<ElementId> doubled{hand.triangles[0], hand.triangles[0]};
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateSurfaceHeatFlux(
            hand.mesh, hand.dofs, doubled, HeatFlux::fromSi(12.0), q);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::EntitiesNotUnique);
        CHECK(maxAbs(q) == 0.0);
    }
    SECTION("descending order is also refused") {
        const std::vector<ElementId> reversed{hand.triangles[1], hand.triangles[0]};
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateSurfaceHeatFlux(
            hand.mesh, hand.dofs, reversed, HeatFlux::fromSi(12.0), q);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::EntitiesNotUnique);
    }
    SECTION("a facet the mesh does not have is not resolved to a neighbour") {
        const std::vector<ElementId> foreign{ElementId::fromValue(999999)};
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateSurfaceHeatFlux(
            hand.mesh, hand.dofs, foreign, HeatFlux::fromSi(12.0), q);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::EntityNotInMesh);
        CHECK(refused.error().error.code == ErrorCode::NotFound);
    }
    SECTION("a tetrahedron list naming a triangle is not a tetrahedron") {
        std::vector<Power> r = zeros(hand);
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateVolumetricHeatSource(
            hand.mesh, hand.dofs, hand.triangles, VolumetricHeatSource::fromSi(1.0), r);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::EntityNotInMesh);
    }
}

TEST_CASE("ThermalLoad_RepeatedAndMisorderedTetrahedraAreRefused", "[thermal][load][invalid]") {
    // The volumetric twin of the facet case: an element listed twice would
    // generate twice as much heat in it. (A survivor of the first mutation
    // batch: only the surface seam had been given a repeated list.)
    const Hand body = makeHand({{1, at(0, 0, 0)}, {2, at(1, 0, 0)}, {3, at(0, 1, 0)}, {4, at(0, 0, 1)}, {5, at(1, 1, 1)}},
                               {}, {{0, 1, 2, 3}, {4, 2, 1, 3}});
    REQUIRE(body.tets.size() == 2);
    std::vector<Power> q = zeros(body);

    SECTION("a repeated element") {
        const std::vector<ElementId> doubled{body.tets[0], body.tets[0]};
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateVolumetricHeatSource(
            body.mesh, body.dofs, doubled, VolumetricHeatSource::fromSi(24.0), q);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::EntitiesNotUnique);
        CHECK(maxAbs(q) == 0.0);
    }
    SECTION("descending order") {
        const std::vector<ElementId> reversed{body.tets[1], body.tets[0]};
        const std::expected<IntegratedHeat, LoadFailure> refused = thermal::integrateVolumetricHeatSource(
            body.mesh, body.dofs, reversed, VolumetricHeatSource::fromSi(24.0), q);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().problem == LoadProblem::EntitiesNotUnique);
    }
    SECTION("the strictly ascending list is accepted, and counts each element once") {
        const std::vector<ElementId> both{body.tets[0], body.tets[1]};
        const std::expected<IntegratedHeat, LoadFailure> done = thermal::integrateVolumetricHeatSource(
            body.mesh, body.dofs, both, VolumetricHeatSource::fromSi(24.0), q);
        REQUIRE(done.has_value());
        CHECK(done->entities == 2);
        CHECK_THAT(done->heatRate.si(), WithinRel(24.0 * (1.0 / 6.0 + 1.0 / 3.0), 1e-14));
    }
}
TEST_CASE("ThermalLoad_ANumberingForAnotherMeshIsRefused", "[thermal][load][invalid]") {
    // The vector is indexed by unknown; a numbering built for a different mesh
    // would index it by the wrong thing, even when the counts agree.
    const Hand a = makeHand(referenceTriangleNodes(), {{0, 1, 2}}, {});
    const Hand b = makeHand(referenceTriangleNodes(), {{0, 1, 2}}, {});
    REQUIRE(a.dofs.dofCount() == b.dofs.dofCount());
    std::vector<Power> q = zeros(a);
    const std::expected<IntegratedHeat, LoadFailure> foreign = thermal::integrateSurfaceHeatFlux(
        a.mesh, b.dofs, a.triangles, HeatFlux::fromSi(12.0), q);
    REQUIRE_FALSE(foreign.has_value());
    CHECK(foreign.error().problem == LoadProblem::DofMapMismatch);

    std::vector<Power> tooShort(2, Power{});
    const std::expected<IntegratedHeat, LoadFailure> shortVector = thermal::integrateSurfaceHeatFlux(
        a.mesh, a.dofs, a.triangles, HeatFlux::fromSi(12.0), tooShort);
    REQUIRE_FALSE(shortVector.has_value());
    CHECK(shortVector.error().problem == LoadProblem::DofMapMismatch);

    // And through the model: a numbering of another generation is refused.
    LoadStudy study;
    const ThermalModel m1 = study.model();
    const ThermalDofMap oldMap = dofMapOf(m1);
    study.remesh();
    const ThermalModel m2 = study.model();
    CHECK(thermal::thermalLoadProblem(m2, oldMap, std::vector<ThermalLoad>{fluxLoad(1, study.endCap(), 1.0)}) ==
          std::optional<LoadProblem>{LoadProblem::DofMapMismatch});
}

TEST_CASE("ThermalLoad_EveryLoadProblemHasAName", "[thermal][load][invalid]") {
    CHECK(thermal::toString(LoadProblem::DuplicateLoadId) == "duplicate_load_id");
    CHECK(thermal::toString(LoadProblem::NonFiniteValue) == "non_finite_value");
    CHECK(thermal::toString(LoadProblem::TargetInvalid) == "target_invalid");
    CHECK(thermal::toString(LoadProblem::TargetUnresolved) == "target_unresolved");
    CHECK(thermal::toString(LoadProblem::TargetWithoutFacets) == "target_without_facets");
    CHECK(thermal::toString(LoadProblem::BodyWithoutElements) == "body_without_elements");
    CHECK(thermal::toString(LoadProblem::DegenerateFacet) == "degenerate_facet");
    CHECK(thermal::toString(LoadProblem::InvalidElement) == "invalid_element");
    CHECK(thermal::toString(LoadProblem::EntityNotInMesh) == "entity_not_in_mesh");
    CHECK(thermal::toString(LoadProblem::EntitiesNotUnique) == "entities_not_unique");
    CHECK(thermal::toString(LoadProblem::DofMapMismatch) == "dof_map_mismatch");
    CHECK(thermal::toString(LoadProblem::NonFiniteResult) == "non_finite_result");
    CHECK(thermal::toString(LoadStaleReason::Body) == "body");
    CHECK(thermal::toString(LoadStaleReason::Mesh) == "mesh");
    CHECK(thermal::toString(LoadStaleReason::Geometry) == "geometry");
    CHECK(thermal::toString(LoadStaleReason::Loads) == "loads");
}

// ===========================================================================
// Purity
// ===========================================================================

TEST_CASE("ThermalLoad_PreparationDoesNotMutateCanonicalOrDerivedState", "[thermal][load][purity]") {
    // Preparing a load creates derived state only: the canonical loads, the
    // analysis and its revision, the CAD objects, the document revision, the
    // mesh and the mapping are exactly as they were.
    LoadStudy base;
    const std::vector<ThermalLoad> loads{fluxLoad(1, base.endCap(), 100.0), generationLoad(2, 1.0e5)};
    REQUIRE(base.setDefinition(ThermalAnalysisDefinition{.mesh = base.control, .loads = loads}).has_value());
    const ThermalModel model = base.model();
    const ThermalDofMap map = dofMapOf(model);

    const std::vector<ThermalLoad> loadsBefore = loads;
    const ThermalAnalysisDefinition definitionBefore = base.definition();
    const std::uint64_t analysisRevision = thermal::findThermalAnalysis(base.document, base.analysis)->revision();
    const std::uint64_t featureRevision = base.document.findObject(base.feature)->revision();
    const std::uint64_t documentRevision = base.document.revision();
    const meshing::MeshStamp stampBefore = model.mesh().mesh().stamp();
    const std::vector<meshing::Node> nodesBefore(model.mesh().mesh().nodes().begin(), model.mesh().mesh().nodes().end());
    const std::vector<meshing::Triangle> trianglesBefore(model.mesh().mesh().triangles().begin(),
                                                         model.mesh().mesh().triangles().end());
    const std::vector<meshing::Tetrahedron> tetsBefore(model.mesh().mesh().tetrahedra().begin(),
                                                       model.mesh().mesh().tetrahedra().end());
    const meshing::GeometryMeshMap mapBefore = model.map();

    for (int i = 0; i < 3; ++i) {
        (void)prepareOn(model, map, loads);
    }
    (void)thermal::prepareThermalLoads(model, map, base.definition());
    (void)thermal::thermalLoadProblem(model, map, loads);

    CHECK(loads == loadsBefore);
    CHECK(base.definition() == definitionBefore);
    CHECK(thermal::findThermalAnalysis(base.document, base.analysis)->revision() == analysisRevision);
    CHECK(base.document.findObject(base.feature)->revision() == featureRevision);
    CHECK(base.document.revision() == documentRevision);
    CHECK(model.mesh().mesh().stamp() == stampBefore);
    CHECK(std::ranges::equal(model.mesh().mesh().nodes(), nodesBefore));
    CHECK(std::ranges::equal(model.mesh().mesh().triangles(), trianglesBefore));
    CHECK(std::ranges::equal(model.mesh().mesh().tetrahedra(), tetsBefore));
    CHECK(model.map() == mapBefore);
    // The canonical target survives resolution as the same FaceName.
    CHECK(loads[0].surfaceHeatFlux()->face == base.endCap());
    CHECK(base.mesher.currency(base.document, base.control) == meshing::MeshCurrency::Current);
}

TEST_CASE("ThermalLoad_AFailedPreparationPublishesNothing", "[thermal][load][purity]") {
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName absent{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    const Result<PreparedThermalLoads> refused = thermal::prepareThermalLoads(
        model, map, std::vector<ThermalLoad>{generationLoad(1, 1.0e5), fluxLoad(2, absent, 1.0)});
    REQUIRE_FALSE(refused.has_value());
    // The generation was valid and processed first; none of it is observable.
    static_assert(std::is_same_v<decltype(refused), const Result<PreparedThermalLoads>>);
    CHECK(refused.error().code == ErrorCode::NotFound);
}

// ===========================================================================
// Determinism
// ===========================================================================

TEST_CASE("ThermalLoad_RepeatedPreparationIsBitIdentical", "[thermal][load][determinism]") {
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 123.456), generationLoad(2, 7.891e5),
                                         fluxLoad(3, study.startCap(), -45.6), fluxLoad(4, study.endCap(), 0.001)};
    const PreparedThermalLoads first = prepareOn(model, map, loads);
    int differences = 0;
    for (int i = 0; i < 50; ++i) {
        if (!(prepareOn(model, map, loads) == first)) {
            ++differences;
        }
    }
    CHECK(differences == 0);
    CHECK(first.source() == thermal::currentLoadSource(model, loads));
    WARN(std::format("determinism: 50 repeated preparations of a 4-load mixed set, {} bitwise differences in "
                     "the vector, the contributions or the provenance",
                     differences));
}

TEST_CASE("ThermalLoad_DiagnosticsAreDeterministic", "[thermal][load][determinism]") {
    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const FaceName absent{study.feature, FaceSelector{.role = FaceRole::HoleBottom}};
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 1.0), fluxLoad(2, absent, 1.0), fluxLoad(3, absent, 2.0)};
    const Result<PreparedThermalLoads> first = thermal::prepareThermalLoads(model, map, loads);
    REQUIRE_FALSE(first.has_value());
    for (int i = 0; i < 20; ++i) {
        const Result<PreparedThermalLoads> again = thermal::prepareThermalLoads(model, map, loads);
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error().code == first.error().code);
        CHECK(again.error().message == first.error().message);
        CHECK(thermal::thermalLoadProblem(model, map, loads) == std::optional<LoadProblem>{LoadProblem::TargetUnresolved});
    }
    // The FIRST bad load in canonical order is the one named.
    CHECK_THAT(first.error().message, ContainsSubstring(std::format("{}", lid(2))));
}

TEST_CASE("ThermalLoad_BitFingerprintForCrossPresetComparison", "[thermal][load][determinism]") {
    // A 64-bit FNV-1a hash over the bit patterns of the prepared vectors of the
    // hand cases (no mesher involved) and of the real block, printed so the three
    // presets can be compared exactly rather than to a tolerance. Equality across
    // presets is MEASURED, not promised: the test asserts only reproducibility.
    const auto hashOf = [](auto&& fill) {
        std::uint64_t hash = 14695981039346656037ULL;
        const auto mix = [&hash](double value) {
            const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
            for (int i = 0; i < 8; ++i) {
                hash ^= (bits >> (8 * i)) & 0xFFU;
                hash *= 1099511628211ULL;
            }
        };
        fill(mix);
        return hash;
    };
    const auto handHash = [&] {
        return hashOf([](auto&& mix) {
            const Hand tri = makeHand({{10, at(0.3, -0.2, 0.5)}, {20, at(1.7, 0.4, 0.9)}, {30, at(0.1, 1.3, -0.4)}}, {{0, 1, 2}}, {});
            std::vector<Power> q = zeros(tri);
            const IntegratedHeat s = surface(tri, -250.5, q);
            for (const Power p : q) {
                mix(p.si());
            }
            mix(s.area.si());
            mix(s.heatRate.si());
            const Hand tet = makeHand({{5, at(0.2, -0.4, 0.1)}, {6, at(1.7, 0.3, 0.2)}, {7, at(0.4, 1.5, 0.6)}, {8, at(0.1, 0.2, 2.0)}},
                                      {}, {{0, 1, 2, 3}});
            std::vector<Power> r = zeros(tet);
            const IntegratedHeat v = volume(tet, 18.0, r);
            for (const Power p : r) {
                mix(p.si());
            }
            mix(v.volume.si());
            mix(v.heatRate.si());
        });
    };
    const std::uint64_t hand1 = handHash();
    const std::uint64_t hand2 = handHash();
    CHECK(hand1 == hand2);

    LoadStudy study;
    const ThermalModel model = study.model();
    const ThermalDofMap map = dofMapOf(model);
    const std::vector<ThermalLoad> loads{fluxLoad(1, study.endCap(), 123.456), generationLoad(2, 7.891e5)};
    const auto blockHash = [&] {
        const PreparedThermalLoads prepared = prepareOn(model, map, loads);
        return hashOf([&prepared](auto&& mix) {
            for (const Power p : prepared.heatInput()) {
                mix(p.si());
            }
            mix(prepared.totalHeatRate().si());
        });
    };
    const std::uint64_t block1 = blockHash();
    CHECK(block1 == blockHash());
    WARN(std::format("bit fingerprint, hand cases (triangle + tetrahedron): {:016x}", hand1));
    WARN(std::format("bit fingerprint, real block, {} nodes, flux + generation: {:016x}", model.mesh().nodeCount(), block1));
}

// ===========================================================================
// Scope
// ===========================================================================

TEST_CASE("ThermalLoad_PreparationIsIndependentOfBoundaryConditionsAndMaterial", "[thermal][load][scope]") {
    // The prepared type exposes a heat input and its attribution and nothing
    // that could be a convection source or a prescribed temperature; and the
    // preparation takes no material. The interface is the evidence.
    STATIC_CHECK(std::is_same_v<decltype(std::declval<const PreparedThermalLoads&>().heatInput()), std::span<const Power>>);
    STATIC_CHECK(std::is_same_v<decltype(std::declval<const PreparedThermalLoads&>().totalHeatRate()), Power>);
    CHECK(true);
}
