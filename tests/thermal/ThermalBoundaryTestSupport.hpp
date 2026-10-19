#pragma once

// Shared fixtures for the P18-BC-001 tests: hand-built meshes for the seams, real
// P16 fixtures for the authority claims, and TEST-SIDE arithmetic for every
// expected value.
//
// EXPECTED VALUES ARE COMPUTED HERE. `computeTri3Convection`, `integrateConvection`
// and `resolveTemperatureConstraints` are production, so the analytical tests form
// their expectations from coordinates with arithmetic written out in the test:
// a cross product for a triangle's area, `h A / 12` and `h T_inf A / 3` typed in
// as literals, the global accumulation written as a loop here. The only
// production inputs an expectation shares are P16's own facet SETS, because which
// triangles lie on a CAD face is P16's question, not this milestone's -- and
// those are checked against the CAD face's known area.

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
#include <bettercad/thermal/ThermalBoundary.hpp>
#include <bettercad/thermal/ThermalConvection.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalDof.hpp>
#include <bettercad/thermal/ThermalResult.hpp>
#include <bettercad/thermal/ThermalTarget.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bctest {

using namespace bettercad;
using namespace bettercad::literals;
using namespace bettercad::test;
using meshing::ElementId;
using meshing::NodeId;
using meshing::RegionId;
using thermal::ThermalAnalysis;
using thermal::ThermalAnalysisDefinition;
using thermal::ThermalBoundaryCondition;
using thermal::ThermalConductance;
using thermal::ThermalDofIndex;
using thermal::ThermalDofMap;
using thermal::ThermalModel;

inline constexpr double kInf = std::numeric_limits<double>::infinity();
inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// The block of `BoundaryStudy`: 40 x 30 mm in plan, 20 mm deep. Derived by hand
/// from the dimensions, so the caps' area is exact.
inline constexpr double kCapArea = 0.040 * 0.030;

[[nodiscard]] inline Point3D at(double x, double y, double z) {
    return Point3D{Length::fromSi(x), Length::fromSi(y), Length::fromSi(z)};
}

[[nodiscard]] inline ThermalBoundaryConditionId bid(std::uint64_t value) {
    return ThermalBoundaryConditionId::fromValue(value);
}

[[nodiscard]] inline ThermalBoundaryCondition temperatureBc(std::uint64_t id, const FaceName& face, double kelvin) {
    return ThermalBoundaryCondition{bid(id), thermal::PrescribedTemperature{.face = face, .value = Temperature::fromSi(kelvin)}};
}

[[nodiscard]] inline ThermalBoundaryCondition convectionBc(std::uint64_t id, const FaceName& face, double h,
                                                           double ambient) {
    return ThermalBoundaryCondition{
        bid(id), thermal::ConvectionBoundary{.face = face,
                                             .coefficient = HeatTransferCoefficient::fromSi(h),
                                             .ambient = Temperature::fromSi(ambient)}};
}

// -----------------------------------------------------------------------
// Test-side arithmetic. NOT production's.
// -----------------------------------------------------------------------

/// `0.5 |(p2-p1) x (p3-p1)|`, written out.
[[nodiscard]] inline double triangleAreaByHand(const Point3D& p1, const Point3D& p2, const Point3D& p3) {
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

[[nodiscard]] inline double relativeError(double actual, double expected) {
    return expected == 0.0 ? std::abs(actual) : std::abs(actual - expected) / std::abs(expected);
}

/// A dense symmetric accumulation of facet terms, written in the test: the thing
/// P18-ASSEMBLY-001 will do, done here only to check totals and sign.
struct Dense {
    std::size_t n = 0;
    std::vector<double> k;  ///< n x n, row-major
    std::vector<double> q;  ///< n

    explicit Dense(std::size_t unknowns) : n(unknowns), k(unknowns * unknowns, 0.0), q(unknowns, 0.0) {}

    void add(const thermal::ConvectionFacet& facet) {
        for (std::size_t r = 0; r < 3; ++r) {
            const auto row = static_cast<std::size_t>(facet.dofs[r].value());
            q[row] += facet.term.source(r).si();
            for (std::size_t c = 0; c < 3; ++c) {
                k[row * n + static_cast<std::size_t>(facet.dofs[c].value())] += facet.term(r, c).si();
            }
        }
    }

    /// `K T - Q` for temperatures @p t (kelvin, by unknown).
    [[nodiscard]] std::vector<double> residual(const std::vector<double>& t) const {
        std::vector<double> out(n, 0.0);
        for (std::size_t r = 0; r < n; ++r) {
            double sum = 0.0;
            for (std::size_t c = 0; c < n; ++c) {
                sum += k[r * n + c] * t[c];
            }
            out[r] = sum - q[r];
        }
        return out;
    }

    [[nodiscard]] double sumQ() const {
        double s = 0.0;
        for (const double v : q) {
            s += v;
        }
        return s;
    }
};

[[nodiscard]] inline double sum(const std::vector<double>& values) {
    double s = 0.0;
    for (const double v : values) {
        s += v;
    }
    return s;
}

// -----------------------------------------------------------------------
// Hand-built meshes, for the seams
// -----------------------------------------------------------------------

struct Hand {
    meshing::Mesh mesh;
    std::vector<ElementId> triangles;
    ThermalDofMap dofs;
};

using HandNodes = std::vector<std::pair<NodeId::ValueType, Point3D>>;

/// A mesh with exactly the nodes asked for (so sparse handles are possible), the
/// triangles given as indices into @p nodes, and its numbering.
[[nodiscard]] inline Hand makeHand(const HandNodes& nodes, const std::vector<std::array<std::size_t, 3>>& triangles) {
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
    meshing::Mesh mesh = builder.build();
    Result<ThermalDofMap> map = thermal::buildThermalDofMap(mesh);
    INFO((map.has_value() ? std::string{} : map.error().message));
    REQUIRE(map.has_value());
    return Hand{std::move(mesh), std::move(triangleIds), std::move(*map)};
}

/// The brief's triangle: (0,0,0), (2,0,0), (0,3,0), area 3 m^2.
[[nodiscard]] inline HandNodes referenceTriangleNodes(double scale = 1.0) {
    return {{1, at(0, 0, 0)}, {2, at(2.0 * scale, 0, 0)}, {3, at(0, 3.0 * scale, 0)}};
}

[[nodiscard]] inline double dofValue(const Hand& hand, NodeId::ValueType handle) {
    const Result<ThermalDofIndex> dof = hand.dofs.dofOf(NodeId::fromValue(handle));
    REQUIRE(dof.has_value());
    return static_cast<double>(dof->value());
}

[[nodiscard]] inline ThermalDofIndex dofOfHandle(const Hand& hand, NodeId::ValueType handle) {
    const Result<ThermalDofIndex> dof = hand.dofs.dofOf(NodeId::fromValue(handle));
    REQUIRE(dof.has_value());
    return *dof;
}

[[nodiscard]] inline std::vector<thermal::ConvectionFacet> convect(const Hand& hand, double h, double ambient,
                                                                   std::vector<ElementId> facets = {}) {
    if (facets.empty()) {
        facets = hand.triangles;
    }
    auto done = thermal::integrateConvection(hand.mesh, hand.dofs, facets, HeatTransferCoefficient::fromSi(h),
                                             Temperature::fromSi(ambient));
    INFO((done.has_value() ? std::string{} : done.error().error.message));
    REQUIRE(done.has_value());
    return std::move(*done);
}

// -----------------------------------------------------------------------
// Real P16 fixtures
// -----------------------------------------------------------------------

/// The block of the other thermal tests, with a thermal analysis carrying
/// @p conditions and a mesh.
struct BoundaryStudy {
    Document document{"Part"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId control{};
    ThermalAnalysisId analysis{};
    MaterialId material{};
    std::array<EntityId, 4> lines{};

    explicit BoundaryStudy(std::vector<ThermalBoundaryCondition> conditions = {}) {
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
            "Study", ThermalAnalysisDefinition{.mesh = control, .boundaryConditions = std::move(conditions)});
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

    [[nodiscard]] thermal::ThermalResultSource currentSource() const {
        const Result<thermal::ThermalResultSource> source =
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

[[nodiscard]] inline ThermalDofMap dofMapOf(const ThermalModel& model) {
    Result<ThermalDofMap> map = thermal::buildThermalDofMap(model.mesh().mesh());
    INFO((map.has_value() ? std::string{} : map.error().message));
    REQUIRE(map.has_value());
    return std::move(*map);
}

[[nodiscard]] inline thermal::PreparedThermalBoundary prepareOn(const ThermalModel& model, const ThermalDofMap& map,
                                                                const std::vector<ThermalBoundaryCondition>& conditions) {
    Result<thermal::PreparedThermalBoundary> prepared = thermal::prepareThermalBoundary(model, map, conditions);
    INFO((prepared.has_value() ? std::string{} : prepared.error().message));
    REQUIRE(prepared.has_value());
    return std::move(*prepared);
}

/// The facet set P16 resolves @p face to, REQUIRED to be fully resolved and
/// non-empty.
[[nodiscard]] inline std::vector<ElementId> facetsOf(const ThermalModel& model, const FaceName& face) {
    const Result<meshing::BoundaryFacetSet> set = meshing::boundaryFacetsOf(model.map(), face);
    REQUIRE(set.has_value());
    REQUIRE(set->fullyResolved());
    REQUIRE_FALSE(set->facets.empty());
    return set->facets;
}

/// Sum of the facets' areas, by the test's own cross product.
[[nodiscard]] inline double areaOfFacets(const meshing::Mesh& mesh, const std::vector<ElementId>& facets) {
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

/// The unique nodes of @p facets, as the test sees them: a set of NodeId values.
[[nodiscard]] inline std::vector<NodeId::ValueType> uniqueNodeHandles(const meshing::Mesh& mesh,
                                                                      const std::vector<ElementId>& facets) {
    std::vector<NodeId::ValueType> out;
    for (const ElementId id : facets) {
        const meshing::Triangle* triangle = mesh.findTriangle(id);
        REQUIRE(triangle != nullptr);
        for (const NodeId node : triangle->nodes) {
            out.push_back(node.value());
        }
    }
    std::ranges::sort(out);
    out.erase(std::ranges::unique(out).begin(), out.end());
    return out;
}

/// Every convection term of @p prepared accumulated into a dense system, the way
/// P18-ASSEMBLY-001 will scatter them -- written here, in the test, to check
/// totals and signs.
[[nodiscard]] inline Dense denseOf(const thermal::PreparedThermalBoundary& prepared, std::size_t unknowns) {
    Dense dense(unknowns);
    for (const thermal::PreparedConvectionBoundary& convection : prepared.convections()) {
        for (const thermal::ConvectionFacet& facet : convection.facets) {
            dense.add(facet);
        }
    }
    return dense;
}

/// The expected dense system for a constant (h, T_inf) over @p facets, written
/// out from the mesh coordinates with the hand formulas h A / 12 and h T_inf A / 3.
[[nodiscard]] inline Dense expectedDense(const meshing::Mesh& mesh, const ThermalDofMap& map,
                                         const std::vector<ElementId>& facets, double h, double ambient) {
    Dense dense(static_cast<std::size_t>(map.dofCount()));
    for (const ElementId id : facets) {
        const meshing::Triangle* triangle = mesh.findTriangle(id);
        REQUIRE(triangle != nullptr);
        std::array<Point3D, 3> p{};
        std::array<std::size_t, 3> row{};
        for (std::size_t i = 0; i < 3; ++i) {
            const meshing::Node* node = mesh.findNode(triangle->nodes[i]);
            REQUIRE(node != nullptr);
            p[i] = node->position;
            const Result<ThermalDofIndex> dof = map.dofOf(triangle->nodes[i]);
            REQUIRE(dof.has_value());
            row[i] = static_cast<std::size_t>(dof->value());
        }
        const double area = triangleAreaByHand(p[0], p[1], p[2]);
        const double unit = h * area / 12.0;
        for (std::size_t r = 0; r < 3; ++r) {
            dense.q[row[r]] += h * ambient * area / 3.0;
            for (std::size_t c = 0; c < 3; ++c) {
                dense.k[row[r] * dense.n + row[c]] += (r == c ? 2.0 * unit : unit);
            }
        }
    }
    return dense;
}

/// The largest absolute difference between two dense systems' K and Q.
[[nodiscard]] inline double worstDifference(const Dense& a, const Dense& b) {
    REQUIRE(a.n == b.n);
    double worst = 0.0;
    for (std::size_t i = 0; i < a.k.size(); ++i) {
        worst = std::max(worst, std::abs(a.k[i] - b.k[i]));
    }
    for (std::size_t i = 0; i < a.q.size(); ++i) {
        worst = std::max(worst, std::abs(a.q[i] - b.q[i]));
    }
    return worst;
}

[[nodiscard]] inline double largestEntry(const Dense& a) {
    double biggest = 0.0;
    for (const double v : a.k) {
        biggest = std::max(biggest, std::abs(v));
    }
    for (const double v : a.q) {
        biggest = std::max(biggest, std::abs(v));
    }
    return biggest;
}
/// A cylinder, 6 mm radius and 20 mm long, with a coarse and a fine meshing
/// control, for the reason P16-SIZE-001 measured: OCCT triangulates a planar face
/// with two triangles whatever the deflection, so a block cannot be refined by a
/// global target at all, while a curved wall subdivides and the target governs.
struct RoundStudy {
    Document document{"Round"};
    features::Regenerator regenerator;
    meshing::Mesher mesher;
    ObjectId feature{};
    MeshControlId coarse{};
    MeshControlId fine{};
    /// A SECOND solid in the same document, with its own control.
    ObjectId otherBody{};
    MeshControlId otherControl{};

    static constexpr double kRadius = 0.006;

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
        // The boundary triangulation is governed by deflection, not by the volume
        // target, so a finer VOLUME alone leaves the cap with the same facets.
        // Tightening the deflection as well makes the SURFACE differ, which is
        // the case a boundary condition must re-resolve.
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

} // namespace bctest
