// RM-THERM-02, -03, -04 and -05 -- the steady reference models (P18-REFMOD-001).
//
//   RM-THERM-02  a slab between two held faces: T linear, q = k dT/L, Qdot = k A dT/L, reactions +Qdot and -Qdot
//   RM-THERM-03  the composite wall: a closed form P18 v1 cannot be asked to reproduce, and the two refusals that say why
//   RM-THERM-04  uniform generation: conservation, the production chain against an independent solve on real P16 meshes, and
//                CONVERGENCE shown on structured meshes the suite controls (a cube against its Fourier series)
//   RM-THERM-05  conduction in series with convection: a resistance network
//
// EVERY EXPECTED VALUE is a number typed in from the model's header or computed by `ThermalAnalytic.hpp`, which includes no
// BetterCAD header. Production supplies the state under test; it is never asked what the answer should be.

#include "reference/ThermalReferenceSupport.hpp"
#include "thermal/ThermalPostTestSupport.hpp"

#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/meshing/MeshControl.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/sketch/Sketch.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

#include <functional>

namespace {

using namespace bettercad;
using namespace bettercad::test::thermalref;
using namespace bettercad::literals;

/// The reactions of the final balance as (position of the held node, heat supplied INTO the domain, W).
struct Reaction {
    Vec3 at;
    double heatInto;
};

[[nodiscard]] std::vector<Reaction> reactionsOf(Run& run) {
    std::vector<Reaction> out;
    REQUIRE(run.run().dofMap.has_value());
    for (const thermal::ReactionHeat& r : run.balance().finalState().reactions) {
        const Result<meshing::NodeId> node = run.run().dofMap->nodeOf(r.dof);
        REQUIRE(node.has_value());
        const meshing::Node* held = run.mesh().findNode(*node);
        REQUIRE(held != nullptr);
        out.push_back({positionOf(held->position), r.heatInto.si()});
    }
    return out;
}

[[nodiscard]] double sumWhere(const std::vector<Reaction>& reactions, const std::function<bool(const Vec3&)>& selector) {
    double sum = 0.0;
    for (const Reaction& r : reactions) {
        if (selector(r.at)) {
            sum += r.heatInto;
        }
    }
    return sum;
}

} // namespace

// ===========================================================================
// RM-THERM-02: the slab
// ===========================================================================

TEST_CASE("ThermalReference_RM02_SlabAtThreeMeshLevels", "[refmod][thermal][rm02]") {
    // Qdot = k A (Th - Tc) / L with k = 8, A = 0.25, L = 1, Th = 400, Tc = 300 is exactly 200 W, and T is linear in z. The
    // solution is AFFINE, so a linear tetrahedron reproduces it on ANY mesh: the three levels are three genuinely different
    // meshes (asserted below) that must all give the same answer to rounding.
    const heat::Slab slab{.conductivity = 8.0, .area = 0.25, .length = 1.0, .hot = 400.0, .cold = 300.0};
    requireClose(slab.heatRate(), 200.0, kClassA, "the slab's Qdot, by hand");
    requireClose(slab.flux(), 800.0, kClassA, "the slab's flux, by hand");
    requireClose(slab.gradient(), -100.0, kClassA, "the slab's gradient, by hand");

    std::vector<std::size_t> nodeCounts;
    for (const double faceMm : {0.0, 100.0, 50.0}) {
        INFO("faces " << faceMm << " mm");
        const std::optional<Length> size =
            faceMm > 0.0 ? std::optional<Length>{Length::fromSi(faceMm * 1.0e-3)} : std::optional<Length>{};
        auto run = runOf(reference::buildThermalSlabReferenceModel(size));
        const meshing::Mesh& mesh = run->mesh();
        nodeCounts.push_back(mesh.nodes().size());
        const thermal::ThermalPostResult& post = run->post();

        // THE UNITS, from the mesh: 0.5 x 0.5 x 1 m. A 1000x error in length shows here and nowhere else.
        const Extent extent = extentOf(mesh);
        requireClose(extent.along(0), 0.5, kClassA, "extent x (m)");
        requireClose(extent.along(1), 0.5, kClassA, "extent y (m)");
        requireClose(extent.along(2), 1.0, kClassA, "extent z (m)");
        requireClose(extent.along(0) * extent.along(1), slab.area, kClassA, "cross-section area measured from the mesh");

        // 1. TEMPERATURE PROFILE: every node, against the line.
        double worstT = 0.0;
        for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
            const double z = mesh.nodes()[i].position.z.si();
            worstT = std::max(worstT, std::abs(post.finalState().temperatures[i].si() - slab.temperature(z)));
        }
        INFO("worst nodal temperature error " << worstT);
        CHECK(worstT <= 1.0e-9);

        // 2. GRADIENT AND FLUX in every element, every component (qx and qy are ZERO, qz is +800: heat flows toward the cold cap).
        double worstG = 0.0;
        double worstQ = 0.0;
        for (std::size_t e = 0; e < mesh.tetrahedra().size(); ++e) {
            const Vec3 g = vec(post.finalState().fields.gradients[e]);
            const Vec3 q = vec(post.finalState().fields.heatFluxes[e]);
            worstG = std::max({worstG, std::abs(g[0]), std::abs(g[1]), std::abs(g[2] - slab.gradient())});
            worstQ = std::max({worstQ, std::abs(q[0]), std::abs(q[1]), std::abs(q[2] - slab.flux())});
            CHECK(q[2] > 0.0);
        }
        CHECK(worstG <= 1.0e-8);
        CHECK(worstQ <= 1.0e-7);

        // 3. THE TOTAL HEAT RATE three ways: q_z A, the hot cap's reaction, minus the cold cap's reaction.
        requireClose(post.finalState().fields.heatFluxes[0].z.si() * slab.area, slab.heatRate(), Tolerance{1e-7, 1e-9}, "q_z A");
        const std::vector<Reaction> reactions = reactionsOf(*run);
        const double hot = sumWhere(reactions, [&](const Vec3& p) { return std::abs(p[2]) < 1e-9; });
        const double cold = sumWhere(reactions, [&](const Vec3& p) { return std::abs(p[2] - 1.0) < 1e-9; });
        requireClose(hot, +200.0, Tolerance{1e-9, 1e-9}, "the hot cap's reaction (heat INTO the body)");
        requireClose(cold, -200.0, Tolerance{1e-9, 1e-9}, "the cold cap's reaction (heat OUT of the body)");
        requireClose(hot + cold, 0.0, Tolerance{1e-9, 0.0}, "the reactions sum to zero (no source)");

        // 4. THE BALANCE: nothing else in it, and it closes.
        const thermal::ThermalHeatBalance& b = run->balance().finalState();
        requireClose(b.terms.appliedSurfaceHeat.si(), 0.0, kClassA, "surface heat");
        requireClose(b.terms.generatedHeat.si(), 0.0, kClassA, "generated heat");
        requireClose(b.terms.convectionHeat.si(), 0.0, kClassA, "convection heat");
        CHECK(run->balance().balanced());
        CHECK(run->balance().worstNormalizedResidual() <= 1.0e-12);

        evidence("RM-THERM-02 faces_mm " + format(faceMm) + " nodes " + std::to_string(mesh.nodes().size()) + " tets " +
                 std::to_string(mesh.tetrahedra().size()) + " worst_T " + format(worstT) + " hot " + format(hot) + " cold " +
                 format(cold) + " normalized_balance " + format(run->balance().worstNormalizedResidual()));
    }
    // The three meshes are different meshes.
    REQUIRE(nodeCounts.size() == 3);
    CHECK(nodeCounts[0] < nodeCounts[1]);
    CHECK(nodeCounts[1] < nodeCounts[2]);
}

// ===========================================================================
// RM-THERM-03: the composite wall
// ===========================================================================

TEST_CASE("ThermalReference_RM03_ClosedFormAgainstHandArithmetic", "[refmod][thermal][rm03]") {
    // A = 0.2 m^2; layer 1: 0.1 m of k = 2; layer 2: 0.15 m of k = 5; 400 K to 300 K.
    //   R1 = 0.1/(2 x 0.2) = 0.25 K/W      R2 = 0.15/(5 x 0.2) = 0.15 K/W      Rtotal = 0.40 K/W
    //   Qdot = 100/0.40 = 250 W            T_interface = 400 - 250 x 0.25 = 337.5 K       q = 250/0.2 = 1250 W/m^2
    //   gradient in layer 1 = -1250/2 = -625 K/m           in layer 2 = -1250/5 = -250 K/m
    const heat::CompositeWall wall = heat::compositeWall({{0.1, 2.0}, {0.15, 5.0}}, 0.2, 400.0, 300.0);
    requireClose(wall.resistances[0], 0.25, kClassA, "R1");
    requireClose(wall.resistances[1], 0.15, kClassA, "R2");
    requireClose(wall.totalResistance, 0.40, kClassA, "Rtotal");
    requireClose(wall.heatRate, 250.0, kClassA, "Qdot");
    requireClose(wall.interfaceTemperatures[1], 337.5, kClassA, "the interface temperature");
    requireClose(wall.interfaceTemperatures[2], 300.0, kClassA, "the cold face");
    requireClose(wall.flux, 1250.0, kClassA, "the flux, the SAME in both layers");
    requireClose(wall.gradients[0], -625.0, kClassA, "gradient in layer 1");
    requireClose(wall.gradients[1], -250.0, kClassA, "gradient in layer 2");
    // The flux CONTINUES across the interface and the gradient JUMPS: -k1 g1 = -k2 g2 = q.
    requireClose(-2.0 * wall.gradients[0], -5.0 * wall.gradients[1], kClassA, "flux continuity across the interface");
    CHECK(wall.gradients[0] != wall.gradients[1]);
}

TEST_CASE("ThermalReference_RM03_AnAveragedConductivityIsNotACompositeWall", "[refmod][thermal][rm03]") {
    // WHY NO SINGLE-MATERIAL STAND-IN IS BUILT. The series conductivity k_eff = L / sum(L_i/k_i) gives the right HEAT RATE for
    // this wall -- and the wrong interface temperature, the wrong gradient in each layer and no flux jump at all, so a document
    // that used it would pass a heat-rate check and be called a composite wall by anyone reading the name. The oracle shows
    // the difference; the suite builds neither.
    const std::vector<heat::Layer> layers{{0.1, 2.0}, {0.15, 5.0}};
    const heat::CompositeWall wall = heat::compositeWall(layers, 0.2, 400.0, 300.0);
    const double totalLength = 0.25;
    const double kEffective = totalLength / (0.1 / 2.0 + 0.15 / 5.0);
    requireClose(kEffective, 3.125, kClassA, "the series conductivity, by hand: 0.25/0.08");
    const heat::Slab averaged{.conductivity = kEffective, .area = 0.2, .length = totalLength, .hot = 400.0, .cold = 300.0};
    requireClose(averaged.heatRate(), wall.heatRate, kClassA, "the averaged slab carries the SAME heat rate");
    // ...but it is a different wall:
    CHECK(std::abs(averaged.temperature(0.1) - wall.interfaceTemperatures[1]) > 20.0); // 360 K against 337.5 K
    CHECK(std::abs(averaged.gradient() - wall.gradients[0]) > 100.0);                  // -400 against -625 K/m
    CHECK(std::abs(averaged.gradient() - wall.gradients[1]) > 100.0);                  // -400 against -250 K/m
    evidence("RM-THERM-03 averaged slab interface " + format(averaged.temperature(0.1)) + " composite interface " +
             format(wall.interfaceTemperatures[1]));
}

TEST_CASE("ThermalReference_RM03_UnsupportedLayeredCapabilityExplicit", "[refmod][thermal][rm03]") {
    // P18 v1 cannot represent a layered wall, for two independent reasons, and BOTH are shown here to hold.
    SECTION("the material is the document's: a second assignment REPLACES the first, it does not add a region") {
        auto built = reference::buildThermalSlabReferenceModel();
        REQUIRE(built.has_value());
        Document& document = built->document;
        const MaterialId first = *document.materialAssignment();
        features::MaterialDefinition other;
        other.designation = "Second layer";
        other.mechanical.density = materials::MaterialProperty<Density>::known(Density::fromSi(2000.0));
        other.thermal.thermalConductivity = materials::MaterialProperty<ThermalConductivity>::known(ThermalConductivity::fromSi(2.0));
        other.thermal.specificHeatCapacity = materials::MaterialProperty<SpecificHeatCapacity>::known(SpecificHeatCapacity::fromSi(900.0));
        const Result<MaterialId> second = features::createMaterial(document, "Second", other);
        REQUIRE(second.has_value());
        REQUIRE(features::assignMaterial(document, *second).has_value());
        REQUIRE(document.materialAssignment().has_value());
        // ONE assignment slot, and it now holds the second material: the first is no longer assigned to anything.
        CHECK(*document.materialAssignment() == *second);
        CHECK(*document.materialAssignment() != first);
        // So the whole body has ONE conductivity: the slab, run with it, has the same gradient in EVERY element (no interface,
        // no flux jump), which is the signature of a homogeneous wall and the opposite of RM-THERM-03's.
        Run run(std::move(document));
        const thermal::ThermalPostResult& post = run.post();
        const Vec3 first_g = vec(post.finalState().fields.gradients.front());
        for (const thermal::TemperatureGradient3& g : post.finalState().fields.gradients) {
            requireClose(vec(g), first_g, Tolerance{1e-8, 1e-9}, "every element has the same gradient: one material, no layers");
        }
        // ...and the body carries the SECOND material's conductivity (k = 2), not the first's (k = 8): the same 100 K over 1 m now moves
        // k A dT/L = 2 x 0.25 x 100 / 1 = 50 W (it was 200 W), and the gradient is still -100 K/m. A replaced assignment, not a layer.
        double hotReaction = 0.0;
        for (const auto& [dof, heat] : reactionsByDof(run.balance())) {
            hotReaction += std::max(heat, 0.0);
        }
        requireClose(hotReaction, 50.0, Tolerance{1e-9, 1e-9}, "the heat rate with the replaced material (k = 2)");
    }
    SECTION("P16 meshes one solid: a body of two solids is refused by name, so two bodies cannot stand in for two layers") {
        Document document{"TwoSolids"};
        features::Regenerator regenerator;
        auto sketch = std::make_unique<sketch::Sketch>("Profile", Frame3D::xy());
        {
            // Two disjoint rectangles in one sketch, extruded: two solids in ONE body (the construction P16's own
            // multi-solid test uses).
            const auto rect = [&](Length x, Length y, Length w, Length h) {
                const Point2D a{x, y};
                const Point2D b{x + w, y};
                const Point2D c{x + w, y + h};
                const Point2D d{x, y + h};
                REQUIRE(sketch->addLine(a, b).has_value());
                REQUIRE(sketch->addLine(b, c).has_value());
                REQUIRE(sketch->addLine(c, d).has_value());
                REQUIRE(sketch->addLine(d, a).has_value());
            };
            rect(0_mm, 0_mm, 10_mm, 10_mm);
            rect(20_mm, 0_mm, 5_mm, 5_mm);
        }
        const Result<ObjectId> profile = document.addObject(std::move(sketch));
        REQUIRE(profile.has_value());
        auto extrude = features::ExtrudeFeature::create("Solid", {.profile = SketchId::fromValue(profile->value()), .depth = 2_mm});
        REQUIRE(extrude.has_value());
        const Result<ObjectId> feature = document.addObject(std::move(*extrude));
        REQUIRE(feature.has_value());
        const auto report = regenerator.regenerateAll(document);
        REQUIRE(report.has_value());
        REQUIRE(report->succeeded());

        const Result<meshing::VolumeMesh> meshed = meshing::volumeMeshFor(document, regenerator, *feature);
        REQUIRE_FALSE(meshed.has_value());
        CHECK_THAT(std::string{meshed.error().message},
                   Catch::Matchers::ContainsSubstring(std::string{meshing::toString(meshing::VolumeMeshFailure::MultipleSolids)}));
    }
}

// ===========================================================================
// RM-THERM-04: uniform generation
// ===========================================================================

TEST_CASE("ThermalReference_RM04_GenerationResultantAndConservationOnTheSlab", "[refmod][thermal][rm04]") {
    // Qgen = q''' V. The slab is a BOX, so its volume is exact (0.08 x 0.08 x 0.16 = 1.024e-3 m^3) and the generated heat is
    // exactly 2e5 x 1.024e-3 = 204.8 W -- a hard gate against a mm^3 / m^3 mix-up, which would be a factor of 1e9. The caps
    // carry it away: the reactions sum to -Qgen and the balance closes.
    auto run = runOf(reference::buildThermalGenerationReferenceModel());
    const meshing::Mesh& mesh = run->mesh();
    const Extent extent = extentOf(mesh);
    requireClose(extent.along(0), 0.08, kClassA, "extent x (m)");
    requireClose(extent.along(1), 0.08, kClassA, "extent y (m)");
    requireClose(extent.along(2), 0.16, kClassA, "extent z (m)");
    const double volume = meshVolume(mesh); // the oracle's own triple products
    requireClose(volume, 1.024e-3, Tolerance{0.0, 1e-12}, "the mesh volume, by the oracle's determinants");
    const double expectedHeat = reference::kGenerationWattsPerM3 * 1.024e-3;
    requireClose(expectedHeat, 204.8, kClassA, "q''' V by hand");

    const thermal::ThermalHeatBalance& b = run->balance().finalState();
    requireClose(b.terms.generatedHeat.si(), expectedHeat, Tolerance{0.0, 1e-12}, "the generated heat the balance holds");
    REQUIRE(b.generationLoads.size() == 1);
    requireClose(b.generationLoads.front().heatInto.si(), expectedHeat, Tolerance{0.0, 1e-12}, "the load's own heat");
    const std::vector<Reaction> reactions = reactionsOf(*run);
    const double total = sumWhere(reactions, [](const Vec3&) { return true; });
    requireClose(total, -expectedHeat, Tolerance{1e-9, 1e-11}, "the held caps remove exactly the generated heat");
    CHECK(run->balance().worstNormalizedResidual() <= 1.0e-12);
    evidence("RM-THERM-04 slab generated " + format(b.terms.generatedHeat.si()) + " expected " + format(expectedHeat) + " reactions " +
             format(total) + " nodes " + std::to_string(mesh.nodes().size()));
}

TEST_CASE("ThermalReference_RM04_ProductionEqualsAnIndependentSolveOnRealMeshes", "[refmod][thermal][rm04]") {
    // THE BOX AND THE TUBE, on the meshes P16 actually produces. The expected field comes from an INDEPENDENT dense
    // finite-element solve of the same mesh (ThermalAnalytic.hpp's `solveConduction`: shape gradients from the 4 x 4 inverse,
    // Ke = k V G^T G, source q''' V/4, Dirichlet nodes chosen by their COORDINATES, Gaussian elimination). It shares nothing
    // with production but the node coordinates, so agreement to 1e-9 K is not a mirror.
    SECTION("the generation slab (caps held at 300 K)") {
        auto run = runOf(reference::buildThermalGenerationReferenceModel());
        const heat::HeatMesh plain = plainMesh(run->mesh());
        std::vector<std::optional<double>> held(plain.nodes.size());
        for (std::size_t i = 0; i < plain.nodes.size(); ++i) {
            if (std::abs(plain.nodes[i][2]) < 1e-9 || std::abs(plain.nodes[i][2] - 0.16) < 1e-9) {
                held[i] = reference::kGenerationFaceKelvin;
            }
        }
        const heat::HeatSolution oracle = heat::solveConduction(plain, reference::kGenerationMaterial.conductivity,
                                                                reference::kGenerationWattsPerM3, held);
        const double difference = maxAbsDifference(finalTemperatures(run->post()), oracle.temperature);
        evidence("RM-THERM-04 slab production-vs-independent " + format(difference));
        CHECK(difference <= 1.0e-9);
        requireClose(oracle.generated, 204.8, Tolerance{0.0, 1e-12}, "the independent solve's own q''' V");
    }
    SECTION("the tube, at three mesh levels") {
        const heat::GenerationTube tube{.conductivity = reference::kTubeMaterial.conductivity,
                                        .generation = reference::kTubeGenerationWattsPerM3,
                                        .outerKelvin = reference::kTubeOuterKelvin,
                                        .innerRadius = reference::kTubeInnerRadiusMm / kMmPerM,
                                        .outerRadius = reference::kTubeOuterRadiusMm / kMmPerM,
                                        .length = reference::kTubeLengthMm / kMmPerM};
        // By hand: Tmax = T(ri) = 300 + 90 - 60 ln 2 = 348.4112 K, Qgen = 3e6 pi (0.0016 - 0.0004) 0.006 = 67.858 W.
        requireClose(tube.temperature(tube.innerRadius), 348.41120, Tolerance{5e-5, 0.0}, "T(ri) by hand");
        requireClose(tube.generatedHeat(), 3.0e6 * heat::pi * 0.0012 * 0.006, kClassA, "Qgen by hand");

        struct Level {
            double deflectionMm;
            double targetMm;
        };
        std::vector<std::size_t> nodeCounts;
        std::vector<double> volumeDeficiency;
        for (const Level level : {Level{2.0, 10.0}, Level{0.1, 10.0}, Level{0.02, 10.0}}) {
            INFO("deflection " << level.deflectionMm << " mm, target " << level.targetMm << " mm");
            auto run = runOf(reference::buildThermalTubeReferenceModel(Length::fromSi(level.deflectionMm * 1e-3),
                                                                        Length::fromSi(level.targetMm * 1e-3)));
            const meshing::Mesh& mesh = run->mesh();
            nodeCounts.push_back(mesh.nodes().size());
            const heat::HeatMesh plain = plainMesh(mesh);

            // The held set, by RADIUS (the outer wall): production's boundary mapping is not consulted.
            std::vector<std::optional<double>> held(plain.nodes.size());
            std::size_t heldCount = 0;
            for (std::size_t i = 0; i < plain.nodes.size(); ++i) {
                if (std::abs(std::hypot(plain.nodes[i][0], plain.nodes[i][1]) - tube.outerRadius) < 1e-7) {
                    held[i] = reference::kTubeOuterKelvin;
                    ++heldCount;
                }
            }
            REQUIRE(heldCount > 0);
            const heat::HeatSolution oracle = heat::solveConduction(plain, tube.conductivity, tube.generation, held);
            const std::vector<double> produced = finalTemperatures(run->post());
            const double difference = maxAbsDifference(produced, oracle.temperature);
            CHECK(difference <= 1.0e-9);

            // The production chain's own held-node count agrees with the geometry's (the boundary mapping found the same wall).
            CHECK(run->balance().finalState().reactions.size() == heldCount);

            // GENERATED HEAT = q''' x (the mesh's volume, by the oracle's determinants), and the wall removes it.
            const double v = meshVolume(mesh);
            requireClose(oracle.totalVolume, v, Tolerance{0.0, 1e-12}, "the two volume sums");
            requireClose(run->balance().finalState().terms.generatedHeat.si(), tube.generation * v, Tolerance{0.0, 1e-11},
                         "the generated heat");
            const std::vector<Reaction> reactions = reactionsOf(*run);
            requireClose(sumWhere(reactions, [](const Vec3&) { return true; }), -tube.generation * v, Tolerance{1e-9, 1e-10},
                         "the held wall removes the generated heat");
            CHECK(run->balance().worstNormalizedResidual() <= 1.0e-12);

            // THE MESH IS A POLYGON: its volume falls short of the true annulus by the chord sagitta, and the shortfall shrinks
            // with the surface deflection. This one converges.
            const double deficiency = 1.0 - v / tube.volume();
            volumeDeficiency.push_back(deficiency);

            // THE FIELD AGAINST THE CLOSED FORM -- BOUNDED AND REPORTED, NOT CLAIMED TO CONVERGE (see the model's header and
            // AUDIT.md): the independent solve agrees with production, so what remains is the mesh. Volume-weighted RMS and the
            // hottest node, against the closed form, as fractions of the 48.4 K rise.
            std::vector<double> weight(plain.nodes.size(), 0.0);
            for (const std::array<std::size_t, 4>& e : plain.tets) {
                heat::Tet t;
                for (std::size_t c = 0; c < 4; ++c) {
                    t.p[c] = plain.nodes[e[c]];
                }
                for (std::size_t c = 0; c < 4; ++c) {
                    weight[e[c]] += std::abs(t.signedVolume()) / 4.0;
                }
            }
            double sum = 0.0;
            double total = 0.0;
            double hottest = 0.0;
            for (std::size_t i = 0; i < plain.nodes.size(); ++i) {
                const double r = std::hypot(plain.nodes[i][0], plain.nodes[i][1]);
                const double e = produced[i] - tube.temperature(r);
                sum += weight[i] * e * e;
                total += weight[i];
                hottest = std::max(hottest, produced[i]);
            }
            const double rms = std::sqrt(sum / total);
            const double rise = tube.temperature(tube.innerRadius) - reference::kTubeOuterKelvin;
            evidence("RM-THERM-04 tube defl_mm " + format(level.deflectionMm) + " target_mm " + format(level.targetMm) + " nodes " +
                     std::to_string(mesh.nodes().size()) + " tets " + std::to_string(mesh.tetrahedra().size()) + " vs_independent " +
                     format(difference) + " volume_deficiency " + format(deficiency) + " rms_K " + format(rms) + " hottest_K " +
                     format(hottest) + " exact_hottest_K " + format(tube.temperature(tube.innerRadius)));
            CHECK(rms <= 0.12 * rise);                                 // measured 7-8% of the rise on every level
            CHECK(std::abs(hottest - tube.temperature(tube.innerRadius)) <= 0.25 * rise); // measured 10-19%
            CHECK(hottest >= reference::kTubeOuterKelvin);
        }
        // Three different meshes, and the GEOMETRY converges: the polygon fills the true annulus better and better.
        REQUIRE(nodeCounts.size() == 3);
        CHECK(nodeCounts[0] < nodeCounts[1]);
        CHECK(nodeCounts[1] < nodeCounts[2]);
        CHECK(volumeDeficiency[0] > volumeDeficiency[1]);
        CHECK(volumeDeficiency[1] > volumeDeficiency[2]);
        CHECK(volumeDeficiency[2] < 1.0e-3);
    }
}

TEST_CASE("ThermalReference_RM04_SpatialRefinementOnStructuredMeshes", "[refmod][thermal][rm04][convergence]") {
    // THE CONVERGENCE STUDY, on meshes whose refinement the suite controls (P16's cannot be refined into one; AUDIT.md has the
    // measurements). A CUBE of side a = 0.1 m, k = 10, uniform generation q''' = 1e6 W/m^3, ALL SIX faces held at 300 K: a
    // smooth three-dimensional field with a closed form (the triple Fourier sine series, in ThermalAnalytic.hpp), no
    // singularity, and a hottest point at the centre of about 56 K above the faces.
    //
    // The mesh is the regular Kuhn grid (six tetrahedra per cell, no jitter) of N cells per side, N = 2, 4, 8. The PRODUCTION
    // chain runs on it through the algebraic seams (generation load, assembly, constraint map, steady solve, recovery); an
    // independent dense solve of the same mesh must agree to 1e-9 K at every level; and the error against the series must
    // fall like h^2, the rate a linear tetrahedron has for a smooth field.
    constexpr double kSide = 0.1;
    constexpr double kK = 10.0;
    constexpr double kQ = 1.0e6;
    constexpr double kFace = 300.0;

    struct Result {
        std::size_t cells;
        std::size_t nodes;
        double centreError;
        double maxError;
        double rms;
        double gradientRms;
    };
    std::vector<Result> results;
    double exactCentre = 0.0;
    for (const std::size_t n : {4UL, 8UL, 16UL}) {
        INFO("cells per side " << n);
        pstest::GridSpec spec;
        spec.side = n + 1;
        spec.h = kSide / static_cast<double>(n);
        spec.jitter = 0.0;
        actest::AsmHand hand = pstest::makeGrid(spec);
        const meshing::Mesh& mesh = hand.mesh;

        // production
        const std::vector<Power> heatInput = actest::generationHeat(hand, kQ);
        const thermal::ThermalSystem system = actest::assemble(hand, kK, std::nullopt, heatInput);
        std::vector<thermal::ThermalTemperatureConstraint> list;
        std::vector<std::optional<double>> held(mesh.nodes().size());
        for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
            const Vec3 p = positionOf(mesh.nodes()[i].position);
            const bool onFace = std::abs(p[0]) < 1e-9 || std::abs(p[0] - kSide) < 1e-9 || std::abs(p[1]) < 1e-9 ||
                                std::abs(p[1] - kSide) < 1e-9 || std::abs(p[2]) < 1e-9 || std::abs(p[2] - kSide) < 1e-9;
            if (onFace) {
                list.push_back({actest::dofIx(i), Temperature::fromSi(kFace)});
                held[i] = kFace;
            }
        }
        const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, list);
        REQUIRE(constraints.has_value());
        const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, *constraints, thermal::ThermalSolverSettings{});
        REQUIRE(solved.has_value());
        auto post = thermal::recoverSteadyFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(kK), *solved);
        REQUIRE(post.has_value());
        const std::vector<double> produced = finalTemperatures(*post);

        // independent (a dense solve: skipped at N = 16, whose 3375 unknowns the series alone judges)
        const heat::HeatMesh plain = plainMesh(mesh);
        if (n <= 8) {
            const heat::HeatSolution oracle = heat::solveConduction(plain, kK, kQ, held);
            CHECK(maxAbsDifference(produced, oracle.temperature) <= 1.0e-9);
            requireClose(oracle.totalVolume, kSide * kSide * kSide, Tolerance{0.0, 1e-12}, "the cube's volume");
            requireClose(oracle.generated, kQ * kSide * kSide * kSide, Tolerance{0.0, 1e-12}, "q''' V = 1000 W");
            double reactions = 0.0;
            for (const double r : oracle.reaction) {
                reactions += r;
            }
            requireClose(reactions, -oracle.generated, Tolerance{1e-8, 1e-11}, "the six held faces remove exactly the generated heat");
        }

        // against the closed form
        std::vector<double> weight(plain.nodes.size(), 0.0);
        for (const std::array<std::size_t, 4>& e : plain.tets) {
            heat::Tet t;
            for (std::size_t c = 0; c < 4; ++c) {
                t.p[c] = plain.nodes[e[c]];
            }
            for (std::size_t c = 0; c < 4; ++c) {
                weight[e[c]] += std::abs(t.signedVolume()) / 4.0;
            }
        }
        double sum = 0.0;
        double total = 0.0;
        double maxError = 0.0;
        double centreError = 0.0;
        for (std::size_t i = 0; i < plain.nodes.size(); ++i) {
            const double exact = kFace + heat::cubeGenerationRise(kSide, kQ, kK, plain.nodes[i]);
            const double e = produced[i] - exact;
            sum += weight[i] * e * e;
            total += weight[i];
            maxError = std::max(maxError, std::abs(e));
            if (std::abs(plain.nodes[i][0] - kSide / 2) < 1e-9 && std::abs(plain.nodes[i][1] - kSide / 2) < 1e-9 &&
                std::abs(plain.nodes[i][2] - kSide / 2) < 1e-9) {
                centreError = std::abs(e);
                exactCentre = exact;
            }
        }

        // the element gradient against the series' own gradient at each centroid
        double gradSum = 0.0;
        double gradVolume = 0.0;
        for (std::size_t e = 0; e < plain.tets.size(); ++e) {
            heat::Tet t;
            for (std::size_t c = 0; c < 4; ++c) {
                t.p[c] = plain.nodes[plain.tets[e][c]];
            }
            const Vec3 centroid = centroidOf(t);
            // the series' own gradient, termwise (truncated at 31 odd terms: a few K/m against errors of hundreds)
            const Vec3 exactGradient = heat::cubeGenerationGradient(kSide, kQ, kK, centroid);
            const Vec3 g = vec(post->finalState().fields.gradients[e]);
            const Vec3 d = heat::minus(g, exactGradient);
            const double v = std::abs(t.signedVolume());
            gradSum += v * heat::dot(d, d);
            gradVolume += v;
        }
        results.push_back({n, mesh.nodes().size(), centreError, maxError, std::sqrt(sum / total), std::sqrt(gradSum / gradVolume)});
        evidence("RM-THERM-04 cube N " + std::to_string(n) + " nodes " + std::to_string(mesh.nodes().size()) + " centre_error_K " +
                 format(centreError) + " max_error_K " + format(maxError) + " rms_K " + format(results.back().rms) +
                 " gradient_rms_K_m " + format(results.back().gradientRms));
    }
    // The closed form's centre, to the digits the series carries; the classic 0.0562 q a^2/k (a hand value, not the series).
    requireClose(exactCentre - kFace, 0.0562 * kQ * kSide * kSide / kK, Tolerance{0.0, 5e-3}, "the series' centre rise against the classic 0.0562 q a^2/k");

    REQUIRE(results.size() == 3);
    // THE GATES, derived from the theory of linear tetrahedra on a smooth field BEFORE the study was run and kept: the nodal
    // maximum and the volume-weighted RMS error fall like h^2, so halving h must cut them by at least 2^1.5 = 2.83 (a margin
    // under the theoretical 4), and the element gradient falls like h, so by at least 2^0.9 = 1.87 once the mesh is in the
    // asymptotic regime. (A first draft ran N = 2, 4, 8. N = 2 has ONE interior node, so its pair with N = 4 is not asymptotic
    // -- the RMS fell by 2.31 and the gradient by 1.57 -- and the gates were not loosened to admit it: the level was dropped and
    // N = 16 added, so that two pairs of meshes that can have an asymptotic rate remain.)
    for (std::size_t i = 1; i < results.size(); ++i) {
        CHECK(results[i].maxError < results[i - 1].maxError / 2.83);
        CHECK(results[i].rms < results[i - 1].rms / 2.83);
        CHECK(results[i].centreError < results[i - 1].centreError);
        CHECK(results[i].gradientRms < results[i - 1].gradientRms); // never worse
    }
    CHECK(results[2].gradientRms < results[1].gradientRms / 1.87); // the finest pair is the asymptotic one for the gradient
    // and the finest level is genuinely close
    CHECK(results.back().rms < 0.01 * (exactCentre - kFace));
    CHECK(results.back().maxError < 0.05 * (exactCentre - kFace));
}
// ===========================================================================
// RM-THERM-05: conduction in series with convection
// ===========================================================================

TEST_CASE("ThermalReference_RM05_ResistanceNetwork", "[refmod][thermal][rm05]") {
    // L = 0.5 m, A = 0.2 m^2, k = 10, h = 20, Thot = 400, Tinf = 300:
    //   Rcond = 0.5/(10 x 0.2) = 0.25 K/W     Rconv = 1/(20 x 0.2) = 0.25 K/W     Qdot = 100/0.5 = 200 W
    //   Ts = 300 + 200 x 0.25 = 350 K  =  400 - 200 x 0.25
    const heat::ConvectionSlab network{.conductivity = 10.0, .area = 0.2, .length = 0.5, .coefficient = 20.0, .hot = 400.0, .ambient = 300.0};
    requireClose(network.conductionResistance(), 0.25, kClassA, "Rcond by hand");
    requireClose(network.convectionResistance(), 0.25, kClassA, "Rconv by hand");
    requireClose(network.heatRate(), 200.0, kClassA, "Qdot by hand");
    requireClose(network.surfaceTemperature(), 350.0, kClassA, "Ts by hand");
    requireClose(network.surfaceTemperatureFromConduction(), 350.0, kClassA, "Ts the other way");

    auto run = runOf(reference::buildThermalConvectionReferenceModel());
    const meshing::Mesh& mesh = run->mesh();
    const Extent extent = extentOf(mesh);
    requireClose(extent.along(0) * extent.along(1), network.area, kClassA, "A measured from the mesh");
    requireClose(extent.along(2), network.length, kClassA, "L measured from the mesh");
    const thermal::ThermalPostResult& post = run->post();

    // SURFACE TEMPERATURE and the whole profile.
    double worst = 0.0;
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        worst = std::max(worst, std::abs(post.finalState().temperatures[i].si() - network.temperature(mesh.nodes()[i].position.z.si())));
        if (std::abs(mesh.nodes()[i].position.z.si() - network.length) < 1e-9) {
            requireClose(post.finalState().temperatures[i].si(), 350.0, kClassB, "the convecting surface's temperature");
        }
    }
    CHECK(worst <= 1.0e-9);

    // CONDUCTION FLUX = Qdot / A = 1000 W/m^2 along +z, gradient -100 K/m, in every element.
    for (std::size_t e = 0; e < mesh.tetrahedra().size(); ++e) {
        requireClose(vec(post.finalState().fields.heatFluxes[e]), Vec3{0.0, 0.0, network.flux()}, Tolerance{1e-7, 1e-9}, "element flux");
        requireClose(vec(post.finalState().fields.gradients[e]), Vec3{0.0, 0.0, -100.0}, Tolerance{1e-8, 1e-9}, "element gradient");
    }

    // RATES: the convection transfer h A (Ts - Tinf), the hot cap's reaction, and the balance terms, with their signs.
    const thermal::ThermalHeatBalance& b = run->balance().finalState();
    REQUIRE(b.convections.size() == 1);
    requireClose(b.convections.front().heatOut.si(), 200.0, Tolerance{1e-9, 1e-9}, "heat OUT through the convection");
    requireClose(b.convections.front().heatInto.si(), -200.0, Tolerance{1e-9, 1e-9}, "heat INTO the body through the convection");
    requireClose(b.terms.convectionHeat.si(), -200.0, Tolerance{1e-9, 1e-9}, "the balance's convection term (into the body)");
    requireClose(b.terms.prescribedTemperatureReaction.si(), +200.0, Tolerance{1e-9, 1e-9}, "the hot cap supplies Qdot INTO the body");
    requireClose(b.convections.front().area.si(), network.area, Tolerance{0.0, 1e-12}, "the convecting area");
    CHECK(run->balance().balanced());
    CHECK(run->balance().worstNormalizedResidual() <= 1.0e-12);
    double surfaceKelvin = 0.0;
    std::size_t surfaceNodes = 0;
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        if (std::abs(mesh.nodes()[i].position.z.si() - network.length) < 1e-9) {
            surfaceKelvin += post.finalState().temperatures[i].si();
            ++surfaceNodes;
        }
    }
    REQUIRE(surfaceNodes > 0);
    evidence("RM-THERM-05 Ts (mean of " + std::to_string(surfaceNodes) + " surface nodes) " +
             format(surfaceKelvin / static_cast<double>(surfaceNodes)) + " convection_out " + format(b.convections.front().heatOut.si()) +
             " reaction " + format(b.terms.prescribedTemperatureReaction.si()) + " normalized " + format(run->balance().worstNormalizedResidual()));
}

TEST_CASE("ThermalReference_RM05_HeatGainSubcase", "[refmod][thermal][rm05]") {
    // The same slab held at 280 K, below the ambient: heat flows the OTHER way. Qdot = (280-300)/0.5 = -40 W, the surface is
    // at Tinf + Qdot Rconv = 290 K, the convection brings +40 W INTO the body and the held cap removes it.
    const heat::ConvectionSlab network{.conductivity = 10.0, .area = 0.2, .length = 0.5, .coefficient = 20.0, .hot = 280.0, .ambient = 300.0};
    requireClose(network.heatRate(), -40.0, kClassA, "Qdot by hand");
    requireClose(network.surfaceTemperature(), 290.0, kClassA, "Ts by hand");

    auto run = runOf(reference::buildThermalConvectionReferenceModel(), /*runNow=*/false);
    {
        const thermal::ThermalAnalysis* object = thermal::findThermalAnalysis(run->document(), run->analysis());
        REQUIRE(object != nullptr);
        thermal::ThermalAnalysisDefinition definition = object->definition();
        for (thermal::ThermalBoundaryCondition& c : definition.boundaryConditions) {
            if (c.prescribedTemperature() != nullptr) {
                c = thermal::ThermalBoundaryCondition{c.id(), thermal::PrescribedTemperature{.face = c.face(), .value = Temperature::fromSi(280.0)}};
            }
        }
        const Result<bool> set = run->document().modifyObject<thermal::ThermalAnalysis>(
            ObjectId::fromValue(run->analysis().value()),
            [&definition](thermal::ThermalAnalysis& a) -> Result<bool> { return a.setDefinition(definition); });
        REQUIRE(set.has_value());
    }
    run->rerun();
    const thermal::ThermalPostResult& post = run->post();
    const meshing::Mesh& mesh = run->mesh();
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        const double z = mesh.nodes()[i].position.z.si();
        requireClose(post.finalState().temperatures[i].si(), 280.0 + (290.0 - 280.0) * z / 0.5, kClassB, "temperature profile of the gain");
    }
    const thermal::ThermalHeatBalance& b = run->balance().finalState();
    requireClose(b.terms.convectionHeat.si(), +40.0, Tolerance{1e-9, 1e-9}, "convection brings heat INTO the body");
    requireClose(b.terms.prescribedTemperatureReaction.si(), -40.0, Tolerance{1e-9, 1e-9}, "the held cap REMOVES it");
    for (std::size_t e = 0; e < mesh.tetrahedra().size(); ++e) {
        CHECK(post.finalState().fields.heatFluxes[e].z.si() < 0.0); // toward the held cap
    }
}


// ===========================================================================
// RM-THERM-02 (surface heat flux): the only accepted document with a surface load
// ===========================================================================

TEST_CASE("ThermalReference_RM02_SurfaceHeatFluxIntoAHeldSlab", "[refmod][thermal][rm02]") {
    // 50 x 50 x 100 mm, k = 5, the start cap held at 300 K, +500 W/m^2 INTO the body through the end cap. In steady state
    // the heat leaves through the held cap:  Qdot = q'' A = 500 x 0.0025 = 1.25 W,  dT/dz = +q''/k = +100 K/m,
    // T(z) = 300 + 100 z (310 K at the end cap),  flux = -k dT/dz = -500 W/m^2 along z (toward the held cap).
    auto run = runOf(reference::buildThermalSurfaceFluxReferenceModel());
    const meshing::Mesh& mesh = run->mesh();
    const Extent extent = extentOf(mesh);
    requireClose(extent.along(0), 0.05, kClassA, "extent x (m)");
    requireClose(extent.along(1), 0.05, kClassA, "extent y (m)");
    requireClose(extent.along(2), 0.10, kClassA, "extent z (m)");
    const double area = extent.along(0) * extent.along(1);
    const double qdot = 500.0 * area;
    requireClose(qdot, 1.25, kClassA, "Qdot by hand");

    const thermal::ThermalPostResult& post = run->post();
    std::size_t endNodes = 0;
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        const double z = mesh.nodes()[i].position.z.si();
        requireClose(post.finalState().temperatures[i].si(), 300.0 + 100.0 * z, kClassB, "temperature profile");
        if (std::abs(z - 0.10) < 1e-9) {
            requireClose(post.finalState().temperatures[i].si(), 310.0, kClassB, "the end cap");
            ++endNodes;
        }
    }
    REQUIRE(endNodes >= 4);
    REQUIRE(post.finalState().fields.gradients.size() == mesh.tetrahedra().size());
    for (std::size_t e = 0; e < mesh.tetrahedra().size(); ++e) {
        requireClose(vec(post.finalState().fields.gradients[e]), Vec3{0.0, 0.0, 100.0}, Tolerance{1e-8, 1e-9}, "element gradient");
        requireClose(vec(post.finalState().fields.heatFluxes[e]), Vec3{0.0, 0.0, -500.0}, Tolerance{1e-7, 1e-9}, "element flux");
    }

    // THE SIGN OF THE LOAD AND OF THE REACTION: +Qdot INTO the body through the surface, -Qdot supplied by the held cap.
    const thermal::ThermalHeatBalance& b = run->balance().finalState();
    REQUIRE(b.surfaceLoads.size() == 1);
    requireClose(b.surfaceLoads.front().heatInto.si(), +qdot, Tolerance{1e-9, 1e-9}, "the surface load's own heat (into the body)");
    requireClose(b.terms.appliedSurfaceHeat.si(), +qdot, Tolerance{1e-9, 1e-9}, "the balance's applied surface heat");
    requireClose(b.terms.prescribedTemperatureReaction.si(), -qdot, Tolerance{1e-9, 1e-9}, "the held cap REMOVES it");
    requireClose(b.terms.generatedHeat.si(), 0.0, kClassA, "generated heat");
    requireClose(b.terms.convectionHeat.si(), 0.0, kClassA, "convection heat");
    CHECK(run->balance().balanced());
    CHECK(run->balance().worstNormalizedResidual() <= 1.0e-12);
}

// ===========================================================================
// The declared intent of every solving document, pinned by HAND LITERALS
// ===========================================================================

namespace {

/// Everything the user decided in a document's analysis, as plain numbers (kelvin, W/m^2, W/m^3, W/(m^2 K), s) in declaration order.
struct Intent {
    std::vector<double> heldKelvin;
    std::vector<double> coefficient;
    std::vector<double> ambientKelvin;
    std::vector<double> surfaceFlux;
    std::vector<double> generation;
    std::optional<double> initialKelvin;
    std::optional<double> stepSeconds;
    std::optional<double> endSeconds;
};

[[nodiscard]] Intent intentOf(const Document& document) {
    const std::vector<ThermalAnalysisId> ids = thermal::thermalAnalysisIds(document);
    REQUIRE(ids.size() == 1);
    const thermal::ThermalAnalysis* analysis = thermal::findThermalAnalysis(document, ids.front());
    REQUIRE(analysis != nullptr);
    const thermal::ThermalAnalysisDefinition& d = analysis->definition();
    Intent out;
    for (const thermal::ThermalBoundaryCondition& c : d.boundaryConditions) {
        if (const thermal::PrescribedTemperature* p = c.prescribedTemperature()) {
            out.heldKelvin.push_back(p->value.si());
        } else if (const thermal::ConvectionBoundary* v = c.convection()) {
            out.coefficient.push_back(v->coefficient.si());
            out.ambientKelvin.push_back(v->ambient.si());
        }
    }
    for (const thermal::ThermalLoad& l : d.loads) {
        if (const thermal::SurfaceHeatFluxLoad* s = l.surfaceHeatFlux()) {
            out.surfaceFlux.push_back(s->flux.si());
        } else if (const thermal::VolumetricHeatSourceLoad* g = l.volumetricHeatSource()) {
            out.generation.push_back(g->generation.si());
        }
    }
    if (d.initialCondition.has_value()) {
        out.initialKelvin = d.initialCondition->value.si();
    }
    if (d.transient.has_value()) {
        out.stepSeconds = d.transient->timeStep.si();
        out.endSeconds = d.transient->endTime.si();
    }
    return out;
}

} // namespace

TEST_CASE("ThermalReference_Intent_TheDeclaredNumbersAreTheBriefsNotTheOracles", "[refmod][thermal][intent]") {
    // The oracle and the builder read the SAME header constants, so a constant edited there would keep every test green. These
    // literals are written here, from the brief and the hand calculations, and compared with what the DOCUMENT declares.
    const auto intent = [](auto built) {
        REQUIRE(built.has_value());
        return intentOf(built->document);
    };
    {
        const Intent i = intent(reference::buildThermalSlabReferenceModel());
        CHECK(i.heldKelvin == std::vector<double>{400.0, 300.0});
    }
    {
        const Intent i = intent(reference::buildThermalGenerationReferenceModel());
        CHECK(i.heldKelvin == std::vector<double>{300.0, 300.0});
        CHECK(i.generation == std::vector<double>{2.0e5});
    }
    {
        const Intent i = intent(reference::buildThermalTubeReferenceModel());
        CHECK(i.heldKelvin == std::vector<double>{300.0});
        CHECK(i.generation == std::vector<double>{3.0e6});
    }
    {
        const Intent i = intent(reference::buildThermalConvectionReferenceModel());
        CHECK(i.heldKelvin == std::vector<double>{400.0});
        CHECK(i.coefficient == std::vector<double>{20.0});
        CHECK(i.ambientKelvin == std::vector<double>{300.0});
    }
    {
        const Intent i = intent(reference::buildThermalSurfaceFluxReferenceModel());
        CHECK(i.heldKelvin == std::vector<double>{300.0});
        CHECK(i.surfaceFlux == std::vector<double>{500.0});
    }
    {
        const Intent i = intent(reference::buildThermalLumpedReferenceModel());
        CHECK(i.coefficient == std::vector<double>(6, 25.0));
        CHECK(i.ambientKelvin == std::vector<double>(6, 300.0));
        REQUIRE(i.initialKelvin.has_value());
        CHECK(*i.initialKelvin == 400.0);
        CHECK(*i.stepSeconds == 100.0);
        CHECK(*i.endSeconds == 4600.0);
    }
    {
        const Intent i = intent(reference::buildThermalInsulatedGenerationReferenceModel());
        CHECK(i.generation == std::vector<double>{1.0e6});
        REQUIRE(i.initialKelvin.has_value());
        CHECK(*i.initialKelvin == 300.0);
        CHECK(*i.stepSeconds == 0.3);
        CHECK(*i.endSeconds == 1.0);
    }
    {
        const Intent i = intent(reference::buildThermalTransientSlabReferenceModel());
        CHECK(i.heldKelvin == std::vector<double>{400.0});
        CHECK(i.coefficient == std::vector<double>{100.0, 10.0});
        CHECK(i.ambientKelvin == std::vector<double>{300.0, 350.0});
        REQUIRE(i.initialKelvin.has_value());
        CHECK(*i.initialKelvin == 300.0);
        CHECK(*i.stepSeconds == 50.0);
        CHECK(*i.endSeconds == 500.0);
    }
    {
        const Intent i = intent(reference::buildThermalLocalRefinementReferenceModel(reference::ThermalLocalRefinement::None));
        CHECK(i.heldKelvin == std::vector<double>{300.0, 300.0});
        CHECK(i.generation == std::vector<double>{1.0e5});
    }
}