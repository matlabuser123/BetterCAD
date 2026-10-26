#pragma once

#include "reference/MeshTestSupport.hpp"
#include "reference/ThermalAnalytic.hpp"

#include <ThermalReferenceModels.hpp>

#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>
#include <bettercad/thermal/ThermalAnalysisObject.hpp>
#include <bettercad/thermal/ThermalAnalysisRun.hpp>
#include <bettercad/thermal/ThermalBalance.hpp>
#include <bettercad/thermal/ThermalPost.hpp>
#include <bettercad/thermal/ThermalValidation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// Shared fixtures for the thermal reference suite (P18-REFMOD-001).
//
// WHAT COMES FROM PRODUCTION AND WHAT DOES NOT. Production supplies the STATE under test: the mesh, the nodal temperatures,
// the recovered gradients and fluxes, the balance terms, the validation verdict. Every EXPECTED value comes from
// `ThermalAnalytic.hpp`, which includes no BetterCAD header, applied to the model's own declared numbers and to the mesh's own
// node coordinates. Where a production figure and an oracle are both available they are COMPARED, never substituted for one
// another, and no check asks a production function what the answer should be.
namespace bettercad::test::thermalref {

namespace heat = analytic::heat;
using heat::Vec3;
using meshref::MeshedReference;

inline constexpr double kMmPerM = 1.0e3;

/// A position in SI metres as a plain triple.
[[nodiscard]] inline Vec3 positionOf(const Point3D& p) { return {p.x.si(), p.y.si(), p.z.si()}; }

[[nodiscard]] inline Vec3 vec(const thermal::TemperatureGradient3& g) { return {g.x.si(), g.y.si(), g.z.si()}; }
[[nodiscard]] inline Vec3 vec(const thermal::HeatFlux3& q) { return {q.x.si(), q.y.si(), q.z.si()}; }

// ---------------------------------------------------------------------------
// Tolerances
// ---------------------------------------------------------------------------

/// `|actual - expected| <= absolute + relative * |expected|`: relative alone is useless near zero, and absolute alone is
/// useless across six orders of magnitude, so each tolerance states both and the BASIS they were chosen from is written
/// beside it at the call site.
struct Tolerance {
    double absolute = 0.0;
    double relative = 0.0;
};

/// Class A: algebraically exact, so near machine precision.
inline constexpr Tolerance kClassA{1.0e-12, 1.0e-12};
/// Class B: an analytical benchmark that a linear tetrahedron reproduces EXACTLY (an affine field), through a solver whose
/// residual is judged at 1e-9 and in practice measures 1e-15.
inline constexpr Tolerance kClassB{1.0e-9, 1.0e-9};

[[nodiscard]] inline bool within(double actual, double expected, Tolerance tolerance) {
    return std::abs(actual - expected) <= tolerance.absolute + tolerance.relative * std::abs(expected);
}

/// Checks and, on failure, SAYS both numbers at full precision.
inline void requireClose(double actual, double expected, Tolerance tolerance, std::string_view what) {
    INFO(what << ": actual " << std::setprecision(17) << actual << ", expected " << expected << ", |difference| "
              << std::abs(actual - expected) << ", allowed " << tolerance.absolute + tolerance.relative * std::abs(expected));
    CHECK(within(actual, expected, tolerance));
}

inline void requireClose(const Vec3& actual, const Vec3& expected, Tolerance tolerance, std::string_view what) {
    // COMPONENT BY COMPONENT: a magnitude alone hides a swapped, transposed or sign-flipped component.
    for (std::size_t i = 0; i < 3; ++i) {
        INFO(what << " component " << i << ": actual " << std::setprecision(17) << actual[i] << ", expected " << expected[i]);
        CHECK(within(actual[i], expected[i], tolerance));
    }
}

// ---------------------------------------------------------------------------
// Evidence
// ---------------------------------------------------------------------------

/// A line of the measured record, printed only when BETTERCAD_REFMOD_EVIDENCE is set, so the evidence tables in
/// docs/verification/P18-REFMOD-001 are transcriptions of what this suite measured and an ordinary run stays quiet.
inline void evidence(const std::string& line) {
    if (std::getenv("BETTERCAD_REFMOD_EVIDENCE") != nullptr) {
        std::printf("EVIDENCE %s\n", line.c_str());
        std::fflush(stdout);
    }
}

[[nodiscard]] inline std::string format(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

// ---------------------------------------------------------------------------
// A model, meshed and run through the production entry
// ---------------------------------------------------------------------------

/// One reference document, regenerated, meshed and run. It owns its own document, regenerator and mesher, so no case can
/// depend on another having run.
class Run {
public:
    explicit Run(Document document, bool runNow = true) : ref_(std::move(document)) {
        const std::vector<ThermalAnalysisId> ids = thermal::thermalAnalysisIds(ref_.document());
        REQUIRE(ids.size() == 1);
        analysis_ = ids.front();
        (void)ref_.require();
        if (runNow) {
            rerun();
        }
    }

    /// Runs the analysis again against whatever the mesher holds NOW.
    void rerun() {
        run_ = std::make_unique<thermal::ThermalAnalysisRun>(
            thermal::runThermalAnalysis(ref_.document(), ref_.regenerator(), ref_.mesher(), analysis_));
    }

    /// Regenerates and meshes again (after the test edited the document).
    void remesh() {
        (void)ref_.regenerate();
        (void)ref_.require();
    }

    /// Generates the mesh again WITHOUT regenerating the model first: nothing about the document or the geometry moves, only the mesh object.
    void remeshOnly() { (void)ref_.require(); }

    [[nodiscard]] MeshedReference& ref() noexcept { return ref_; }
    [[nodiscard]] Document& document() noexcept { return ref_.document(); }
    [[nodiscard]] ThermalAnalysisId analysis() const noexcept { return analysis_; }
    [[nodiscard]] const thermal::ThermalAnalysisRun& run() const {
        REQUIRE(run_ != nullptr);
        return *run_;
    }
    /// Hands the current run to the caller, who keeps it across edits (a result of an OLDER document). The run lives on the heap, so
    /// the views its `derivedState()` takes stay valid after the move.
    [[nodiscard]] std::unique_ptr<thermal::ThermalAnalysisRun> takeRun() {
        REQUIRE(run_ != nullptr);
        return std::move(run_);
    }
    [[nodiscard]] const meshing::VolumeMesh& volume() {
        const meshing::VolumeMesh* held = ref_.mesher().mesh(ref_.control());
        REQUIRE(held != nullptr);
        return *held;
    }
    [[nodiscard]] const meshing::Mesh& mesh() { return volume().mesh(); }

    /// The accepted post result; the test fails if there is none.
    [[nodiscard]] const thermal::ThermalPostResult& post() const {
        const thermal::ThermalAnalysisRun& r = run();
        INFO("the run was not accepted: " << (r.failure.has_value() ? r.failure->message : std::string{"(no failure)"}));
        REQUIRE(r.accepted());
        REQUIRE(r.post.has_value());
        return *r.post;
    }
    [[nodiscard]] const thermal::ThermalBalanceResult& balance() const {
        const thermal::ThermalAnalysisRun& r = run();
        REQUIRE(r.balance.has_value());
        return *r.balance;
    }

private:
    MeshedReference ref_;
    ThermalAnalysisId analysis_{};
    std::unique_ptr<thermal::ThermalAnalysisRun> run_;
};

/// A run of the document @p built; the test fails if the model did not build.
template <typename Model>
[[nodiscard]] std::unique_ptr<Run> runOf(Result<Model> built, bool runNow = true) {
    if (!built) {
        FAIL("the reference model did not build: " << built.error().message);
    }
    return std::make_unique<Run>(std::move(built->document), runNow);
}

// ---------------------------------------------------------------------------
// Plain-array views of a mesh
// ---------------------------------------------------------------------------

/// The mesh as plain arrays, in the mesh's own node order (ascending NodeId, which is also the numbering of the nodal
/// temperature vector) -- the form the independent solver takes.
[[nodiscard]] inline heat::HeatMesh plainMesh(const meshing::Mesh& mesh) {
    heat::HeatMesh out;
    std::unordered_map<std::uint64_t, std::size_t> index;
    for (const meshing::Node& node : mesh.nodes()) {
        index.emplace(node.id.value(), out.nodes.size());
        out.nodes.push_back(positionOf(node.position));
    }
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        out.tets.push_back({index.at(tet.nodes[0].value()), index.at(tet.nodes[1].value()), index.at(tet.nodes[2].value()),
                            index.at(tet.nodes[3].value())});
    }
    return out;
}

/// The nodal temperatures of the final state, as plain doubles in node order.
[[nodiscard]] inline std::vector<double> finalTemperatures(const thermal::ThermalPostResult& post) {
    std::vector<double> out;
    for (const Temperature& t : post.finalState().temperatures) {
        out.push_back(t.si());
    }
    return out;
}

[[nodiscard]] inline double maxAbsDifference(const std::vector<double>& a, const std::vector<double>& b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

/// The tetrahedron @p tet of @p mesh as the oracle's own type.
[[nodiscard]] inline heat::Tet oracleTet(const meshing::Mesh& mesh, const meshing::Tetrahedron& tet) {
    heat::Tet out;
    for (std::size_t c = 0; c < 4; ++c) {
        const meshing::Node* node = mesh.findNode(tet.nodes[c]);
        REQUIRE(node != nullptr);
        out.p[c] = positionOf(node->position);
    }
    return out;
}

[[nodiscard]] inline Vec3 centroidOf(const heat::Tet& tet) {
    Vec3 sum{};
    for (const Vec3& corner : tet.p) {
        sum = heat::plus(sum, corner);
    }
    return heat::scaled(sum, 0.25);
}

/// The sum of the tetrahedra's volumes, by the oracle's own determinant, m^3.
[[nodiscard]] inline double meshVolume(const meshing::Mesh& mesh) {
    double sum = 0.0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        sum += std::abs(oracleTet(mesh, tet).signedVolume());
    }
    return sum;
}

/// The extent of the mesh along each axis, from its node coordinates alone (so the dimensions the tests quote are measured
/// from the mesh and cannot be a transcription of the builder's own numbers).
struct Extent {
    Vec3 low{1e300, 1e300, 1e300};
    Vec3 high{-1e300, -1e300, -1e300};
    [[nodiscard]] double along(std::size_t axis) const { return high[axis] - low[axis]; }
};

[[nodiscard]] inline Extent extentOf(const meshing::Mesh& mesh) {
    Extent e;
    for (const meshing::Node& node : mesh.nodes()) {
        const Vec3 p = positionOf(node.position);
        for (std::size_t i = 0; i < 3; ++i) {
            e.low[i] = std::min(e.low[i], p[i]);
            e.high[i] = std::max(e.high[i], p[i]);
        }
    }
    return e;
}

/// The summary of a validation report that a failing test needs to read.
[[nodiscard]] inline std::string describe(const thermal::ThermalValidationReport& report) {
    std::ostringstream out;
    out << thermal::toString(report.status());
    for (const thermal::ValidationFinding& f : report.findings) {
        out << "\n  " << thermal::toString(f.severity) << " " << thermal::toString(f.code) << ": " << f.message;
    }
    return out.str();
}

[[nodiscard]] inline bool hasFinding(const thermal::ThermalValidationReport& report, thermal::ValidationCode code) {
    return std::ranges::any_of(report.findings, [code](const thermal::ValidationFinding& f) { return f.code == code; });
}

/// The reaction of the final balance at each prescribed node, keyed by the node's position in the numbering.
[[nodiscard]] inline std::map<std::size_t, double> reactionsByDof(const thermal::ThermalBalanceResult& balance) {
    std::map<std::size_t, double> out;
    for (const thermal::ReactionHeat& r : balance.finalState().reactions) {
        out.emplace(static_cast<std::size_t>(r.dof.value()), r.heatInto.si());
    }
    return out;
}

} // namespace bettercad::test::thermalref
