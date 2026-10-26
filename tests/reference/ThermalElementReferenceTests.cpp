// RM-THERM-01 -- the linear-temperature patch test (P18-REFMOD-001).
//
// THE STRONGEST SINGLE TEST OF THE ELEMENT. A linear tetrahedron interpolates an affine field EXACTLY, so for
//
//     T(x, y, z) = a + b x + c y + d z          a = 287.25 K, b = 7.2, c = -3.5, d = 11.4 K/m
//
// it must give grad T = [b, c, d] in every element, q = -k [b, c, d] in every element, nodal temperatures that are the
// field, and -- on a mesh that has an INTERIOR node -- an interior node that the solve puts exactly on the plane when the
// boundary nodes are held to it. Every expected number comes from `ThermalAnalytic.hpp`, which includes no BetterCAD header.
//
// THE COEFFICIENTS ARE ALL DIFFERENT AND NONE IS ZERO, so a permuted component, a transposed gradient or a sign error
// cannot pass. THE SKEW TETRAHEDRON IS NOT AXIS-ALIGNED, so a Jacobian indexed the wrong way cannot pass either.

#include "reference/ThermalReferenceSupport.hpp"
#include "thermal/ThermalPostTestSupport.hpp"

#include <bettercad/thermal/ThermalElement.hpp>
#include <bettercad/thermal/ThermalSolve.hpp>

namespace {

using namespace bettercad;
using namespace bettercad::test::thermalref;
using heat::Affine;

constexpr double kConductivity = 5.0;
constexpr Affine kField{287.25, 7.2, -3.5, 11.4};

[[nodiscard]] Point3D at(const Vec3& p) {
    return Point3D{Length::fromSi(p[0]), Length::fromSi(p[1]), Length::fromSi(p[2])};
}

struct Fixture {
    const char* name;
    std::array<Vec3, 4> corners;
};

/// The unit tetrahedron, and a skew, non-orthogonal one with a positive determinant (1.1175, checked by hand:
/// (p2-p1) . ((p3-p1) x (p4-p1)) = 1.2 x 0.945 + 0.1 x (-0.165) = 1.1175).
const std::array<Fixture, 2> kFixtures{{
    {"unit", {{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}}},
    {"skew", {{{0.0, 0.0, 0.0}, {1.2, 0.1, 0.0}, {0.2, 1.1, 0.15}, {0.1, 0.3, 0.9}}}},
}};

[[nodiscard]] thermal::Tet4Corners cornersOf(const Fixture& fixture) {
    return {at(fixture.corners[0]), at(fixture.corners[1]), at(fixture.corners[2]), at(fixture.corners[3])};
}

[[nodiscard]] std::array<double, 4> nodalField(const Fixture& fixture) {
    std::array<double, 4> t{};
    for (std::size_t i = 0; i < 4; ++i) {
        t[i] = kField.at(fixture.corners[i]);
    }
    return t;
}

[[nodiscard]] thermal::Tet4Temperatures asTemperatures(const std::array<double, 4>& values) {
    return {Temperature::fromSi(values[0]), Temperature::fromSi(values[1]), Temperature::fromSi(values[2]),
            Temperature::fromSi(values[3])};
}

} // namespace

TEST_CASE("ThermalReference_RM01_FixturesAreWhatTheyClaimToBe", "[refmod][thermal][rm01]") {
    // The determinant of the skew tetrahedron, by the oracle's own triple product, against the hand value; and both are
    // positively oriented. A fixture that is not what the comment says would make every check below mean less.
    heat::Tet unit;
    unit.p = kFixtures[0].corners;
    heat::Tet skew;
    skew.p = kFixtures[1].corners;
    requireClose(unit.signedVolume(), 1.0 / 6.0, kClassA, "unit tetrahedron volume");
    requireClose(6.0 * skew.signedVolume(), 1.1175, kClassA, "skew determinant (hand value 1.1175)");
    CHECK(skew.signedVolume() > 0.0);
    // Not axis-aligned: every pair of its edges that leaves the first corner has a non-zero dot product with another edge.
    const Vec3 e1 = heat::minus(skew.p[1], skew.p[0]);
    const Vec3 e2 = heat::minus(skew.p[2], skew.p[0]);
    const Vec3 e3 = heat::minus(skew.p[3], skew.p[0]);
    CHECK(heat::dot(e1, e2) != 0.0);
    CHECK(heat::dot(e1, e3) != 0.0);
    CHECK(heat::dot(e2, e3) != 0.0);
}

TEST_CASE("ThermalReference_RM01_AffineNodalInterpolation", "[refmod][thermal][rm01]") {
    // T(x_i) = T_i at all four nodes, and the linear interpolant -- the barycentric combination of the nodal values --
    // equals the field at interior points. The barycentric weights are the ORACLE'S (a 4 x 4 solve), so what is being
    // checked is the field, the fixture and the weights; the production element is checked in the next cases.
    for (const Fixture& fixture : kFixtures) {
        INFO(fixture.name);
        heat::Tet tet;
        tet.p = fixture.corners;
        const std::array<double, 4> nodal = nodalField(fixture);
        for (std::size_t i = 0; i < 4; ++i) {
            const std::array<double, 4> w = tet.barycentric(fixture.corners[i]);
            for (std::size_t j = 0; j < 4; ++j) {
                requireClose(w[j], i == j ? 1.0 : 0.0, kClassA, "barycentric weight at a corner");
            }
            requireClose(nodal[i], kField.at(fixture.corners[i]), kClassA, "nodal value");
        }
        for (const Vec3& weights : {Vec3{0.25, 0.25, 0.25}, Vec3{0.1, 0.2, 0.3}, Vec3{0.6, 0.15, 0.05}}) {
            const double w4 = 1.0 - weights[0] - weights[1] - weights[2];
            const std::array<double, 4> w{weights[0], weights[1], weights[2], w4};
            Vec3 x{};
            double interpolated = 0.0;
            for (std::size_t i = 0; i < 4; ++i) {
                x = heat::plus(x, heat::scaled(fixture.corners[i], w[i]));
                interpolated += w[i] * nodal[i];
            }
            requireClose(interpolated, kField.at(x), kClassA, "interpolant at an interior point");
        }
    }
}

TEST_CASE("ThermalReference_RM01_ConstantGradient", "[refmod][thermal][rm01]") {
    // grad T = [b, c, d] EXACTLY, every component, through the production element.
    for (const Fixture& fixture : kFixtures) {
        INFO(fixture.name);
        Result<thermal::Tet4Geometry> geometry = thermal::computeTet4Geometry(cornersOf(fixture));
        REQUIRE(geometry.has_value());
        const thermal::TemperatureGradient3 gradient = geometry->gradientFrom(asTemperatures(nodalField(fixture)));
        requireClose(vec(gradient), Vec3{7.2, -3.5, 11.4}, kClassA, "grad T");
        evidence(std::string{"RM-THERM-01 "} + fixture.name + " gradient " + format(gradient.x.si()) + " " + format(gradient.y.si()) +
                 " " + format(gradient.z.si()));
    }
}

TEST_CASE("ThermalReference_RM01_FourierFlux", "[refmod][thermal][rm01]") {
    // q = -k grad T = [-36, 17.5, -57] W/m^2 for k = 5: the HAND values, and the oracle's, component by component.
    const Vec3 hand{-36.0, 17.5, -57.0};
    requireClose(kField.flux(kConductivity), hand, kClassA, "the oracle's Fourier flux against the hand values");
    for (const Fixture& fixture : kFixtures) {
        INFO(fixture.name);
        Result<thermal::Tet4Geometry> geometry = thermal::computeTet4Geometry(cornersOf(fixture));
        REQUIRE(geometry.has_value());
        const thermal::TemperatureGradient3 gradient = geometry->gradientFrom(asTemperatures(nodalField(fixture)));
        // The production flux of this gradient is -k times it, with no sign and no component moved.
        const Vec3 g = vec(gradient);
        const Vec3 q{-kConductivity * g[0], -kConductivity * g[1], -kConductivity * g[2]};
        requireClose(q, hand, kClassA, "q = -k grad T");
        requireClose(heat::norm(q), std::sqrt(36.0 * 36.0 + 17.5 * 17.5 + 57.0 * 57.0), kClassA, "|q|");
    }
}

TEST_CASE("ThermalReference_RM01_ElementMatrixIndependentOracle", "[refmod][thermal][rm01]") {
    // Ke = k V G^T G against a DENSE calculation from the 4 x 4 inverse of [1 x y z], then its algebraic properties.
    for (const Fixture& fixture : kFixtures) {
        INFO(fixture.name);
        heat::Tet tet;
        tet.p = fixture.corners;
        const auto expected = tet.stiffness(kConductivity);
        Result<thermal::Tet4Conduction> ke =
            thermal::computeTet4Conduction(cornersOf(fixture), ThermalConductivity::fromSi(kConductivity));
        REQUIRE(ke.has_value());

        double scale = 0.0;
        for (const auto& row : expected) {
            for (const double v : row) {
                scale = std::max(scale, std::abs(v));
            }
        }
        double worst = 0.0;
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                worst = std::max(worst, std::abs((*ke)(i, j).si() - expected[i][j]));
            }
        }
        INFO("worst entry difference " << worst << ", scale " << scale);
        CHECK(worst <= 1.0e-12 * scale);
        evidence(std::string{"RM-THERM-01 "} + fixture.name + " Ke max entry error " + format(worst) + " scale " + format(scale));

        // Symmetric, exactly (it is a dot product, not symmetrised afterwards).
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                CHECK((*ke)(i, j).si() == (*ke)(j, i).si());
            }
        }
        // Ke [1 1 1 1]^T = 0: a uniform temperature conducts nothing.
        for (std::size_t i = 0; i < 4; ++i) {
            double row = 0.0;
            for (std::size_t j = 0; j < 4; ++j) {
                row += (*ke)(i, j).si();
            }
            CHECK(std::abs(row) <= 1.0e-12 * scale);
        }
        // Rank 3: elimination with a pivot threshold finds exactly three pivots.
        {
            heat::Dense a(4, std::vector<double>(4));
            for (std::size_t i = 0; i < 4; ++i) {
                for (std::size_t j = 0; j < 4; ++j) {
                    a[i][j] = (*ke)(i, j).si();
                }
            }
            std::size_t pivots = 0;
            std::vector<bool> used(4, false);
            for (std::size_t col = 0; col < 4; ++col) {
                std::size_t best = 4;
                double bestValue = 1.0e-9 * scale;
                for (std::size_t r = 0; r < 4; ++r) {
                    if (!used[r] && std::abs(a[r][col]) > bestValue) {
                        best = r;
                        bestValue = std::abs(a[r][col]);
                    }
                }
                if (best == 4) {
                    continue;
                }
                used[best] = true;
                ++pivots;
                for (std::size_t r = 0; r < 4; ++r) {
                    if (r != best) {
                        const double f = a[r][col] / a[best][col];
                        for (std::size_t c = 0; c < 4; ++c) {
                            a[r][c] -= f * a[best][c];
                        }
                    }
                }
            }
            CHECK(pivots == 3);
        }
        // x^T Ke x >= 0 for independent vectors (positive SEMIdefinite).
        for (const std::array<double, 4>& x : {std::array<double, 4>{1.0, -2.0, 0.5, 3.0}, std::array<double, 4>{0.0, 1.0, 0.0, 0.0},
                                              std::array<double, 4>{-3.0, 2.0, 2.0, -1.0}, std::array<double, 4>{1.0, 1.0, 1.0, 1.0}}) {
            double quadratic = 0.0;
            for (std::size_t i = 0; i < 4; ++i) {
                for (std::size_t j = 0; j < 4; ++j) {
                    quadratic += x[i] * expected[i][j] * x[j];
                }
            }
            CHECK(quadratic >= -1.0e-12 * scale);
        }
    }
}

TEST_CASE("ThermalReference_RM01_EnergyConsistency", "[refmod][thermal][rm01]") {
    // Te^T Ke Te = integral of k |grad T|^2 dV = k V |grad T|^2 for the affine field: the weak form and the strong form agree.
    // The left side is the PRODUCTION Ke applied to the nodal field; the right side is the oracle's volume and the typed
    // gradient.
    for (const Fixture& fixture : kFixtures) {
        INFO(fixture.name);
        heat::Tet tet;
        tet.p = fixture.corners;
        const std::array<double, 4> nodal = nodalField(fixture);
        Result<thermal::Tet4Conduction> ke =
            thermal::computeTet4Conduction(cornersOf(fixture), ThermalConductivity::fromSi(kConductivity));
        REQUIRE(ke.has_value());
        const std::array<Power, 4> flow = ke->heatFlowFrom(asTemperatures(nodal));
        double energy = 0.0;
        for (std::size_t i = 0; i < 4; ++i) {
            energy += nodal[i] * flow[i].si();
        }
        const double expected = kConductivity * tet.signedVolume() * heat::dot(kField.gradient(), kField.gradient());
        // The nodal values are ~300 K and the energy is a difference of large products, so the allowance is relative to the
        // square of the field's own scale.
        requireClose(energy, expected, Tolerance{1.0e-9, 1.0e-9}, "Te^T Ke Te = k V |grad T|^2");
        // The four nodal heat flows sum to zero: conduction redistributes heat inside the element and creates none.
        requireClose(flow[0].si() + flow[1].si() + flow[2].si() + flow[3].si(), 0.0, Tolerance{1.0e-9, 0.0}, "sum of nodal heat flows");
        evidence(std::string{"RM-THERM-01 "} + fixture.name + " energy " + format(energy) + " expected " + format(expected));
    }
}

TEST_CASE("ThermalReference_RM01_NegativeOrientationIsRefusedNotCorrected", "[refmod][thermal][rm01]") {
    // Swapping two corners flips the sign of the determinant. The element must REFUSE: not take abs(detJ), not reorder.
    for (const Fixture& fixture : kFixtures) {
        INFO(fixture.name);
        thermal::Tet4Corners swapped = cornersOf(fixture);
        std::swap(swapped[1], swapped[2]);
        CHECK(thermal::tet4GeometryProblem(swapped).has_value());
        CHECK_FALSE(thermal::computeTet4Geometry(swapped).has_value());
        CHECK_FALSE(thermal::computeTet4Conduction(swapped, ThermalConductivity::fromSi(kConductivity)).has_value());
        // and the same corners the right way round are accepted, so the refusal is about the orientation.
        CHECK_FALSE(thermal::tet4GeometryProblem(cornersOf(fixture)).has_value());
    }
}

// ---------------------------------------------------------------------------
// The patch on a real mesh with an interior node
// ---------------------------------------------------------------------------

TEST_CASE("ThermalReference_RM01_PatchOnAMeshWithAnInteriorNode", "[refmod][thermal][rm01][patch]") {
    // The committed block meshes to 9 nodes and 12 tetrahedra: the eight corners and ONE interior node. The affine field is
    // imposed on the eight boundary nodes (no canonical boundary condition can say "a different temperature at each node",
    // so this goes through the algebraic seam, on the REAL mesh), the interior node is left free, and the solve must put it
    // exactly on the plane. With no interior node the check would be vacuous.
    auto run = runOf(reference::buildThermalPatchReferenceModel(), /*runNow=*/false);
    const meshing::Mesh& mesh = run->mesh();
    REQUIRE(mesh.nodes().size() == 9);
    REQUIRE(mesh.tetrahedra().size() == 12);

    // The interior node, by geometry (the block is 40 x 30 x 20 mm).
    std::vector<std::size_t> interior;
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        const Vec3 p = positionOf(mesh.nodes()[i].position);
        const bool onFace = std::abs(p[0]) < 1e-9 || std::abs(p[0] - 0.040) < 1e-9 || std::abs(p[1]) < 1e-9 ||
                            std::abs(p[1] - 0.030) < 1e-9 || std::abs(p[2]) < 1e-9 || std::abs(p[2] - 0.020) < 1e-9;
        if (!onFace) {
            interior.push_back(i);
        }
    }
    REQUIRE(interior.size() == 1);

    // Build the hand view of the real mesh and assemble through the production seam.
    Result<thermal::ThermalDofMap> dofs = thermal::buildThermalDofMap(mesh);
    REQUIRE(dofs.has_value());
    std::vector<meshing::ElementId> tetIds;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        tetIds.push_back(tet.id);
    }
    actest::AsmHand hand{.mesh = mesh, .triangles = {}, .tets = tetIds, .dofs = std::move(*dofs)};
    const thermal::ThermalSystem system = actest::assemble(hand, kConductivity, std::nullopt, actest::noHeat(hand));

    std::vector<thermal::ThermalTemperatureConstraint> list;
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        if (i != interior.front()) {
            list.push_back({actest::dofIx(i), Temperature::fromSi(kField.at(positionOf(mesh.nodes()[i].position)))});
        }
    }
    const auto constraints = thermal::buildThermalConstraintMap(hand.dofs, list);
    REQUIRE(constraints.has_value());
    const auto solved = thermal::solveSteadyThermalSystem(system, hand.dofs, *constraints, thermal::ThermalSolverSettings{});
    REQUIRE(solved.has_value());
    auto post = thermal::recoverSteadyFromSolution(hand.mesh, hand.dofs, ThermalConductivity::fromSi(kConductivity), *solved);
    REQUIRE(post.has_value());
    const std::vector<double> temperatures = finalTemperatures(*post);

    // 1. THE INTERIOR NODE LIES ON THE PLANE: the patch test proper.
    const Vec3 xi = positionOf(mesh.nodes()[interior.front()].position);
    requireClose(temperatures[interior.front()], kField.at(xi), kClassB, "the interior node's temperature");
    evidence("RM-THERM-01 patch interior node T " + format(temperatures[interior.front()]) + " affine " + format(kField.at(xi)));

    // 2. EVERY ELEMENT'S GRADIENT AND FLUX ARE THE FIELD'S, component by component.
    double worstGradient = 0.0;
    double worstFlux = 0.0;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const Result<thermal::TemperatureGradient3> g = post->gradientOf(0, tet.id);
        const Result<thermal::HeatFlux3> q = post->heatFluxOf(0, tet.id);
        REQUIRE(g.has_value());
        REQUIRE(q.has_value());
        requireClose(vec(*g), kField.gradient(), kClassB, "element gradient");
        requireClose(vec(*q), kField.flux(kConductivity), kClassB, "element heat flux");
        for (std::size_t c = 0; c < 3; ++c) {
            worstGradient = std::max(worstGradient, std::abs(vec(*g)[c] - kField.gradient()[c]));
            worstFlux = std::max(worstFlux, std::abs(vec(*q)[c] - kField.flux(kConductivity)[c]));
        }
    }
    evidence("RM-THERM-01 patch worst gradient component error " + format(worstGradient) + " flux " + format(worstFlux));

    // 3. THE FIELD ITSELF, at every node and at the centroid of every element (the interpolant there is the mean of the
    //    four corners, and an affine field is its own interpolant).
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        requireClose(temperatures[i], kField.at(positionOf(mesh.nodes()[i].position)), kClassB, "nodal temperature");
    }
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const heat::Tet oracle = oracleTet(mesh, tet);
        double mean = 0.0;
        for (std::size_t c = 0; c < 4; ++c) {
            const meshing::Node* node = mesh.findNode(tet.nodes[c]);
            // the temperature of this corner, through the numbering: the node's position in the ascending list
            std::size_t position = 0;
            for (const meshing::Node& n : mesh.nodes()) {
                if (n.id == node->id) {
                    break;
                }
                ++position;
            }
            mean += 0.25 * temperatures[position];
        }
        requireClose(mean, kField.at(centroidOf(oracle)), kClassB, "interpolant at an element centroid");
    }

    // 4. THE FORCE CHECK. For an affine field the heat each BOUNDARY node must receive from its constraint is the flux through
    //    its tributary boundary: -sum over the node's boundary triangles of (q . S_out)/3, with S_out the OUTWARD area vector
    //    (the oracle's own cross product of the triangle's stored winding). The interior node receives nothing.
    std::map<std::size_t, double> expected;
    std::unordered_map<std::uint64_t, std::size_t> nodeIndex;
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        nodeIndex.emplace(mesh.nodes()[i].id.value(), i);
    }
    const Vec3 q = kField.flux(kConductivity);
    for (const meshing::Triangle& tri : mesh.triangles()) {
        std::array<Vec3, 3> p{};
        for (std::size_t c = 0; c < 3; ++c) {
            p[c] = positionOf(mesh.findNode(tri.nodes[c])->position);
        }
        const Vec3 area = heat::scaled(heat::cross(heat::minus(p[1], p[0]), heat::minus(p[2], p[0])), 0.5);
        const double outward = heat::dot(q, area); // W out through this triangle
        for (std::size_t c = 0; c < 3; ++c) {
            expected[nodeIndex.at(tri.nodes[c].value())] -= outward / 3.0;
        }
    }
    double sumOfReactions = 0.0;
    double worst = 0.0;
    const auto residual = thermal::steadyFullResidual(system.conductance(), system.heat(), post->finalState().temperatures);
    REQUIRE(residual.has_value());
    for (std::size_t i = 0; i < mesh.nodes().size(); ++i) {
        const double reaction = (*residual)[i]; // the heat the constraint supplies into the domain at node i
        sumOfReactions += reaction;
        if (i == interior.front()) {
            requireClose(reaction, 0.0, Tolerance{1.0e-9, 0.0}, "the interior node's residual (K T - Q)");
        } else {
            worst = std::max(worst, std::abs(reaction - expected.at(i)));
            requireClose(reaction, expected.at(i), Tolerance{1.0e-9, 1.0e-9}, "boundary node reaction");
        }
    }
    requireClose(sumOfReactions, 0.0, Tolerance{1.0e-9, 0.0}, "the reactions of an affine patch sum to zero (no source)");
    evidence("RM-THERM-01 patch worst reaction error " + format(worst));
}
