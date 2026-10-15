#pragma once

#include "reference/Analytic.hpp"
#include "reference/MeshTestSupport.hpp"

#include <StructuralReferenceModels.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/structural/StructuralAnalysis.hpp>
#include <bettercad/structural/StructuralAnalysisObject.hpp>
#include <bettercad/structural/StructuralConstraints.hpp>
#include <bettercad/structural/StructuralDof.hpp>
#include <bettercad/structural/StructuralLoadVector.hpp>
#include <bettercad/structural/StructuralMaterial.hpp>
#include <bettercad/structural/StructuralPost.hpp>
#include <bettercad/structural/StructuralReaction.hpp>
#include <bettercad/structural/StructuralSolve.hpp>
#include <bettercad/structural/StructuralSystem.hpp>
#include <bettercad/structural/StructuralValidation.hpp>
#include <bettercad/structural/Tet4Element.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Checks shared by the structural reference models (P17-REFMOD-001).
//
// EVERY ORACLE HERE IS COMPUTED FROM THE MESH'S OWN NODES, THE MODEL'S OWN
// DIMENSIONS AND THE DOCUMENT'S OWN MATERIAL, through `Analytic.hpp`'s closed
// forms -- and none of it reads a production B, D, von Mises, facet
// integration or equilibrium check to decide what the answer SHOULD be. Where
// a production figure is also available it is COMPARED, never substituted.
//
// What this header does read from production is the STATE: the solved
// displacement field, the recovered element fields, the reactions, the
// assembled stiffness matrix. Those are the things under test, and a check
// has to be given them to have anything to check.
//
// This is common infrastructure rather than per-model code, which the brief
// requires in as many words.
namespace bettercad::test::structref {

using meshref::kMmPerM;
using meshref::MeshedReference;
using meshref::Vec3;

/// A position in SI metres, as a plain triple for arithmetic.
using Si3 = std::array<double, 3>;

[[nodiscard]] inline Si3 positionSi(const Point3D& p) {
    return {p.x.si(), p.y.si(), p.z.si()};
}

[[nodiscard]] inline Si3 minus(const Si3& a, const Si3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
[[nodiscard]] inline Si3 plus(const Si3& a, const Si3& b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
[[nodiscard]] inline Si3 scaled(const Si3& a, double k) {
    return {a[0] * k, a[1] * k, a[2] * k};
}
[[nodiscard]] inline Si3 cross(const Si3& a, const Si3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
[[nodiscard]] inline double dot(const Si3& a, const Si3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
[[nodiscard]] inline double norm(const Si3& a) {
    return std::sqrt(dot(a, a));
}

/// The outward area vector of a triangle with the stored winding:
/// `(1/2)(p2 - p1) x (p3 - p1)`, in m^2. The suite's own, not
/// `structural::facetAreaVector`.
[[nodiscard]] inline Si3 areaVector(const Si3& p1, const Si3& p2, const Si3& p3) {
    return scaled(cross(minus(p2, p1), minus(p3, p1)), 0.5);
}

/// The signed volume of a tetrahedron in m^3, the suite's own determinant.
[[nodiscard]] inline double signedVolumeSi(const Si3& p1, const Si3& p2, const Si3& p3, const Si3& p4) {
    return dot(minus(p2, p1), cross(minus(p3, p1), minus(p4, p1))) / 6.0;
}

/// `R v`, with R's COLUMNS the placement's triad -- the rotation half of
/// `meshref::placed`, for a VECTOR, which is not translated.
[[nodiscard]] inline Si3 rotated(const Si3& v, const reference::RigidPlacement& placement) {
    const std::array<double, 3>& x = placement.xAxis;
    const std::array<double, 3>& y = placement.yAxis;
    const std::array<double, 3>& n = placement.normal;
    return {v[0] * x[0] + v[1] * y[0] + v[2] * n[0], v[0] * x[1] + v[1] * y[1] + v[2] * n[1],
            v[0] * x[2] + v[1] * y[2] + v[2] * n[2]};
}

/// `R p + t`, in SI, for a POINT.
[[nodiscard]] inline Si3 placedSi(const Si3& p, const reference::RigidPlacement& placement) {
    const Si3 origin{placement.origin[0] / kMmPerM, placement.origin[1] / kMmPerM,
                     placement.origin[2] / kMmPerM};
    return plus(rotated(p, placement), origin);
}

/// The SI node positions of @p mesh, by handle.
[[nodiscard]] inline std::map<meshing::NodeId::ValueType, Si3> nodePositionsSi(const meshing::Mesh& mesh) {
    std::map<meshing::NodeId::ValueType, Si3> positions;
    for (const meshing::Node& node : mesh.nodes()) {
        positions.emplace(node.id.value(), positionSi(node.position));
    }
    return positions;
}

/// The four corners of @p tet, in the order the mesh stores them.
[[nodiscard]] inline std::array<Point3D, structural::kTet4Nodes> cornersOf(const meshing::Mesh& mesh,
                                                                           const meshing::Tetrahedron& tet) {
    std::array<Point3D, structural::kTet4Nodes> corners{};
    for (std::size_t i = 0; i < structural::kTet4Nodes; ++i) {
        const meshing::Node* node = mesh.findNode(tet.nodes[i]);
        REQUIRE(node != nullptr);
        corners[i] = node->position;
    }
    return corners;
}

/// The centroid of @p tet, in SI.
[[nodiscard]] inline Si3 centroidSi(const meshing::Mesh& mesh, const meshing::Tetrahedron& tet) {
    const std::array<Point3D, 4> corners = cornersOf(mesh, tet);
    Si3 sum{};
    for (const Point3D& corner : corners) {
        sum = plus(sum, positionSi(corner));
    }
    return scaled(sum, 0.25);
}

/// The six components of a production strain and stress, in the suite's own
/// `Voigt6` layout, so they can be compared with `Analytic.hpp`'s closed
/// forms component for component.
[[nodiscard]] inline analytic::Voigt6 voigtOf(const structural::Strain6& e) {
    return {e.xx, e.yy, e.zz, e.gammaXy, e.gammaYz, e.gammaZx};
}
[[nodiscard]] inline analytic::Voigt6 voigtOf(const structural::Stress6& s) {
    return {s.xx.si(), s.yy.si(), s.zz.si(), s.xy.si(), s.yz.si(), s.zx.si()};
}

/// The largest absolute entry of a six-vector, as the scale a relative error
/// is judged against. Floored, so a zero state does not divide by zero.
[[nodiscard]] inline double scaleOf(const analytic::Voigt6& v, double floor) {
    double worst = floor;
    for (const double entry : v) {
        worst = std::max(worst, std::abs(entry));
    }
    return worst;
}

/// The largest relative difference between two six-vectors, against the
/// scale of @p expected.
[[nodiscard]] inline double worstRelative(const analytic::Voigt6& actual, const analytic::Voigt6& expected,
                                          double floor) {
    const double scale = scaleOf(expected, floor);
    double worst = 0.0;
    for (std::size_t i = 0; i < 6; ++i) {
        worst = std::max(worst, std::abs(actual[i] - expected[i]) / scale);
    }
    return worst;
}

/// `K u`, over BetterCAD's CSR copy of the assembled stiffness, in newtons.
///
/// A plain row loop, written here so the patch test's force check shares no
/// code with the solver's residual loop.
[[nodiscard]] inline std::vector<double> multiply(const structural::StiffnessMatrix& k,
                                                  std::span<const double> u) {
    REQUIRE(k.columns() == u.size());
    std::vector<double> out(k.rows(), 0.0);
    const std::span<const structural::StiffnessMatrix::Index> rowStart = k.rowStart();
    const std::span<const structural::StiffnessMatrix::Index> inner = k.innerIndices();
    const std::span<const double> values = k.values();
    for (std::size_t row = 0; row < k.rows(); ++row) {
        double sum = 0.0;
        for (auto entry = rowStart[row]; entry < rowStart[row + 1]; ++entry) {
            sum += values[static_cast<std::size_t>(entry)] * u[static_cast<std::size_t>(inner[static_cast<std::size_t>(entry)])];
        }
        out[row] = sum;
    }
    return out;
}

/// The material the document resolves for @p body, as the three plain numbers
/// the closed forms take.
struct ResolvedSteel {
    double youngs = 0.0;
    double poisson = 0.0;
    double shear = 0.0;
    std::optional<double> density{};
};

[[nodiscard]] inline ResolvedSteel steelOf(const Document& document, ObjectId body,
                                           structural::StructuralAnalysisMode mode) {
    Result<structural::StructuralMaterial> material =
        structural::resolveStructuralMaterial(document, body, mode);
    INFO((material.has_value() ? std::string{} : material.error().message));
    REQUIRE(material.has_value());
    ResolvedSteel steel;
    steel.youngs = material->elastic().youngsModulus.si();
    steel.poisson = material->elastic().poissonRatio.value();
    steel.shear = material->elastic().shearModulus.si();
    if (material->density()) {
        steel.density = material->density()->si();
    }
    return steel;
}

/// One structural reference model, regenerated and ready to mesh and solve.
///
/// It owns its own document, regenerator and mesher through `MeshedReference`,
/// so no reference case can depend on another having run -- which the brief
/// requires of every case.
class StructuralReference {
public:
    explicit StructuralReference(Document document) : meshed_(std::move(document)) {
        const std::optional<ObjectId> found =
            meshed_.document().findByName(std::string{reference::kStructuralStudyName});
        REQUIRE(found.has_value());
        analysis_ = *found;
        REQUIRE(study() != nullptr);
    }

    [[nodiscard]] MeshedReference& meshed() noexcept { return meshed_; }
    [[nodiscard]] Document& document() noexcept { return meshed_.document(); }
    [[nodiscard]] const Document& document() const noexcept { return meshed_.document(); }
    [[nodiscard]] features::Regenerator& regenerator() noexcept { return meshed_.regenerator(); }
    [[nodiscard]] meshing::Mesher& mesher() noexcept { return meshed_.mesher(); }
    [[nodiscard]] ObjectId body() const { return meshed_.body(); }
    [[nodiscard]] MeshControlId control() const noexcept { return meshed_.control(); }

    [[nodiscard]] ObjectId analysisObject() const noexcept { return analysis_; }
    [[nodiscard]] AnalysisId analysis() const noexcept {
        return AnalysisId::fromValue(analysis_.value());
    }
    [[nodiscard]] const structural::StructuralAnalysis* study() const noexcept {
        return document().findObjectAs<structural::StructuralAnalysis>(analysis_);
    }
    [[nodiscard]] structural::StructuralAnalysisDefinition definition() const {
        const structural::StructuralAnalysis* found = study();
        REQUIRE(found != nullptr);
        return found->definition();
    }

    /// Replaces the analysis definition through the document, so the edit
    /// moves the object's revision as a command would.
    void setDefinition(structural::StructuralAnalysisDefinition definition) {
        const Result<bool> changed = document().modifyObject<structural::StructuralAnalysis>(
            analysis_, [&](structural::StructuralAnalysis& object) -> Result<bool> {
                return object.setDefinition(std::move(definition));
            });
        INFO((changed.has_value() ? std::string{} : changed.error().message));
        REQUIRE(changed.has_value());
    }

    /// Replaces the meshing intent through the document.
    void setMeshDefinition(const meshing::MeshControlDefinition& definition) {
        REQUIRE(document()
                    .modifyObject<meshing::MeshControl>(
                        meshed_.controlObject(),
                        [&definition](meshing::MeshControl& control) {
                            return control.setDefinition(definition);
                        })
                    .has_value());
    }

    /// Generates the mesh from the document's own intent and requires success.
    const meshing::VolumeMesh& require() { return meshed_.require(); }

    [[nodiscard]] structural::StructuralValidationReport validate() {
        return structural::validateStructuralAnalysisForSolve(document(), regenerator(), mesher(),
                                                              analysis());
    }

    /// The production entry, under the production DEFAULT equilibrium
    /// tolerance unless a caller states another -- and a caller that does
    /// says why, in its own comment.
    [[nodiscard]] Result<structural::StructuralSolveOutcome> solve(
        const Point3D& origin = Point3D{},
        const structural::EquilibriumTolerance& tolerance = {}) {
        return structural::solveStructuralAnalysis(document(), regenerator(), mesher(), analysis(),
                                                   origin, tolerance);
    }

    /// Meshes and solves, requiring success.
    [[nodiscard]] structural::StructuralSolveOutcome requireSolve(
        const Point3D& origin = Point3D{},
        const structural::EquilibriumTolerance& tolerance = {}) {
        require();
        Result<structural::StructuralSolveOutcome> outcome = solve(origin, tolerance);
        if (!outcome) {
            FAIL("solve refused: " << outcome.error().message);
        }
        return std::move(*outcome);
    }

    // --- the stages, for the checks that need the system itself ----------

    [[nodiscard]] structural::StructuralModel model() {
        Result<structural::StructuralModel> model = structural::requireStructuralModel(
            document(), regenerator(), mesher(), control());
        INFO((model.has_value() ? std::string{} : model.error().message));
        REQUIRE(model.has_value());
        return std::move(*model);
    }

    [[nodiscard]] structural::StructuralMaterial material() {
        Result<structural::StructuralMaterial> material =
            structural::resolveStructuralMaterial(document(), body(), definition().mode);
        INFO((material.has_value() ? std::string{} : material.error().message));
        REQUIRE(material.has_value());
        return *material;
    }

    [[nodiscard]] ResolvedSteel steel() { return steelOf(document(), body(), definition().mode); }

    [[nodiscard]] structural::PreparedLoads prepareLoads() {
        const structural::StructuralModel m = model();
        Result<structural::PreparedLoads> prepared =
            structural::prepareStructuralLoads(m, material(), definition().loads);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }

    [[nodiscard]] structural::GlobalStructuralSystem assemble() {
        const structural::StructuralModel m = model();
        const structural::PreparedLoads loads = prepareLoads();
        Result<structural::GlobalStructuralSystem> system =
            structural::assembleStructuralSystem(m, material(), loads);
        INFO((system.has_value() ? std::string{} : system.error().message));
        REQUIRE(system.has_value());
        return std::move(*system);
    }

    [[nodiscard]] structural::PreparedRestraints prepareRestraints(const structural::MeshDofMap& numbering) {
        const structural::StructuralModel m = model();
        Result<structural::PreparedRestraints> prepared =
            structural::prepareStructuralRestraints(m, numbering, definition().restraints);
        INFO((prepared.has_value() ? std::string{} : prepared.error().message));
        REQUIRE(prepared.has_value());
        return std::move(*prepared);
    }

    /// The current nodes of @p face, through P16's mapping.
    [[nodiscard]] std::vector<meshing::NodeId> nodesOf(const FaceName& face) {
        const structural::StructuralModel m = model();
        Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(m.map(), face);
        REQUIRE(facets.has_value());
        REQUIRE(facets->fullyResolved());
        Result<std::vector<meshing::NodeId>> nodes =
            meshing::boundaryNodesOf(m.map(), m.mesh().mesh(), facets->facets);
        REQUIRE(nodes.has_value());
        REQUIRE_FALSE(nodes->empty());
        return std::move(*nodes);
    }

    /// The current boundary facets of @p face.
    [[nodiscard]] std::vector<meshing::ElementId> facetsOf(const FaceName& face) {
        const structural::StructuralModel m = model();
        Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(m.map(), face);
        REQUIRE(facets.has_value());
        REQUIRE(facets->fullyResolved());
        return facets->facets;
    }

private:
    MeshedReference meshed_;
    ObjectId analysis_{};
};

/// A test-side checksum over every ordered channel of a solve, for determinism
/// evidence only. It hashes each double's own representation, so it is
/// bit-exact, and it says two runs produced the same numbers -- nothing about
/// whether they are right.
class Fingerprint {
public:
    void mix(double value) {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        hash_ ^= bits;
        hash_ *= 1099511628211ULL;
    }
    void mix(std::size_t value) { mix(static_cast<double>(value)); }

    void mix(const structural::RecoveredFields& fields) {
        for (const structural::NodalDisplacement& d : fields.displacements()) {
            mix(static_cast<double>(d.node.value()));
            mix(d.displacement.x.si());
            mix(d.displacement.y.si());
            mix(d.displacement.z.si());
            mix(d.magnitude.si());
        }
        for (const structural::ElementFields& e : fields.elements()) {
            mix(static_cast<double>(e.element.value()));
            for (const structural::TensorComponent which : structural::kTensorComponents) {
                mix(e.strain.component(which));
                mix(e.stress.component(which).si());
            }
            for (std::size_t i = 0; i < 3; ++i) {
                mix(e.principalStress.at(i).si());
                mix(e.principalStrain.at(i));
            }
            mix(e.vonMises.si());
            mix(e.hydrostatic.si());
        }
    }

    void mix(const structural::SupportReactions& reactions) {
        for (const structural::SupportReaction& r : reactions.nodal()) {
            mix(static_cast<double>(r.node.value()));
            mix(r.force.x.si());
            mix(r.force.y.si());
            mix(r.force.z.si());
        }
    }

    void mix(const structural::StructuralSolveOutcome& outcome) {
        mix(outcome.freeEquations);
        mix(outcome.residual.normalized);
        mix(outcome.pivotRatio);
        mix(outcome.strainEnergy.si());
        mix(outcome.largestDisplacement.si());
        mix(outcome.largestVonMises.si());
        mix(outcome.fields);
        mix(outcome.reactions);
    }

    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
    std::uint64_t hash_ = 1469598103934665603ULL;
};

[[nodiscard]] inline std::uint64_t fingerprintOf(const structural::StructuralSolveOutcome& outcome) {
    Fingerprint print;
    print.mix(outcome);
    return print.value();
}

/// The volume-weighted mean of one stress component over every element, and
/// the total volume it was taken over, with the volumes from the suite's own
/// determinant.
struct VolumeMean {
    double mean = 0.0;
    double volume = 0.0;
    std::size_t elements = 0;
};

[[nodiscard]] inline VolumeMean meanStressZz(const meshing::Mesh& mesh,
                                             const structural::RecoveredFields& fields) {
    VolumeMean out;
    double weighted = 0.0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const std::array<Point3D, 4> c = cornersOf(mesh, tet);
        const double v = signedVolumeSi(positionSi(c[0]), positionSi(c[1]), positionSi(c[2]),
                                        positionSi(c[3]));
        REQUIRE(v > 0.0);
        Result<structural::ElementFields> e = fields.fieldsOf(mesh, tet.id);
        REQUIRE(e.has_value());
        weighted += v * e->stress.zz.si();
        out.volume += v;
        ++out.elements;
    }
    REQUIRE(out.volume > 0.0);
    out.mean = weighted / out.volume;
    return out;
}

/// Which nodes of @p mesh no boundary triangle references: the mesher's own
/// interior points.
[[nodiscard]] inline std::vector<meshing::NodeId> interiorNodes(const meshing::Mesh& mesh) {
    const std::set<meshing::NodeId::ValueType> boundary = meshref::boundaryNodeHandles(mesh);
    std::vector<meshing::NodeId> interior;
    for (const meshing::Node& node : mesh.nodes()) {
        if (!boundary.contains(node.id.value())) {
            interior.push_back(node.id);
        }
    }
    return interior;
}

} // namespace bettercad::test::structref
