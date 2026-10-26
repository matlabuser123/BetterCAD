// RM-THERM-06 (a rigid transform) and RM-THERM-07 (local refinement) -- the two reference models that depend on what P16 does
// with the geometry (P18-REFMOD-001).
//
// RM-THERM-06. A body and the SAME body under a rigid motion x' = R x + t (R a proper rotation about no coordinate axis, t in
// all three coordinates, no scaling). Temperature is a scalar and does not move; grad T and q are vectors and rotate:
//
//     T'(x') = T(x)        grad T' = R grad T        q' = R q        |grad T'| = |grad T|        |q'| = |q|
//
// and the heat rate, the reactions and the balance do not change. The two MESHES are judged by correspondence of physical
// points (x -> R x + t), never by node id, and the boundary and the interior node populations are recorded SEPARATELY.
//
// RM-THERM-07. A beam with generation, its two caps held, and a 5 mm local size on ONE cap: the baseline (none), the TARGET
// (the start cap) and the MIRROR (the end cap). Refinement is measured in the VOLUME -- tetrahedra whose centroids lie in a band
// beside the cap -- and never inferred from the cap's own surface triangles, which a planar face does not refine at all.

#include "reference/ThermalPatchSupport.hpp"

#include <bettercad/meshing/GeometryMeshMap.hpp>

#include <algorithm>
#include <functional>
#include <set>

namespace {

using namespace bettercad;
using namespace bettercad::test::thermalref;

// ---------------------------------------------------------------------------
// RM-THERM-06
// ---------------------------------------------------------------------------

/// R: the matrix whose COLUMNS are the placement's triad (the local axes expressed in the global frame), and t in metres.
struct Placement {
    heat::Mat3 r;
    Vec3 t;
};

[[nodiscard]] Placement placementOfTheModel() {
    const reference::RigidPlacement p = reference::thermalTransformedPlacement();
    return {heat::fromColumns(p.xAxis, p.yAxis, p.normal), {p.origin[0] / kMmPerM, p.origin[1] / kMmPerM, p.origin[2] / kMmPerM}};
}

[[nodiscard]] Vec3 movePoint(const Placement& p, const Vec3& x) { return heat::plus(heat::apply(p.r, x), p.t); }

/// The populations of a mesh, counted separately: nodes on the boundary (those a boundary triangle uses), nodes inside, the
/// boundary triangles, the tetrahedra.
struct Populations {
    std::size_t boundaryNodes = 0;
    std::size_t interiorNodes = 0;
    std::size_t boundaryTriangles = 0;
    std::size_t tetrahedra = 0;
};

[[nodiscard]] Populations populationsOf(const meshing::Mesh& mesh) {
    std::set<std::uint64_t> onBoundary;
    for (const meshing::Triangle& tri : mesh.triangles()) {
        for (const meshing::NodeId id : tri.nodes) {
            onBoundary.insert(id.value());
        }
    }
    Populations p;
    p.boundaryNodes = onBoundary.size();
    p.interiorNodes = mesh.nodes().size() - onBoundary.size();
    p.boundaryTriangles = mesh.triangles().size();
    p.tetrahedra = mesh.tetrahedra().size();
    return p;
}

struct TransformedPair {
    std::unique_ptr<Run> base;
    std::unique_ptr<Run> placed;
    FaceName baseStart, baseEnd, baseSide1;
    FaceName placedStart, placedEnd, placedSide1;
};

[[nodiscard]] TransformedPair buildPair() {
    auto base = reference::buildThermalTransformedBaseReferenceModel();
    auto placed = reference::buildThermalTransformedPlacedReferenceModel();
    REQUIRE(base.has_value());
    REQUIRE(placed.has_value());
    TransformedPair pair;
    pair.baseStart = base->start();
    pair.baseEnd = base->end();
    pair.baseSide1 = base->side(1);
    pair.placedStart = placed->start();
    pair.placedEnd = placed->end();
    pair.placedSide1 = placed->side(1);
    pair.base = std::make_unique<Run>(std::move(base->document));
    pair.placed = std::make_unique<Run>(std::move(placed->document));
    return pair;
}

} // namespace

TEST_CASE("ThermalReference_RM06_TheTransformIsARigidMotion", "[refmod][thermal][rm06]") {
    // det R = +1 and R^T R = I, from the numbers the model's frame is built from, written down here by hand: the thirds
    // triad X = (2,2,-1)/3, Y = (-1,2,2)/3, N = (2,-1,2)/3, which mixes all three axes (so a permutation or a rotation about
    // one axis would not survive it), and t = (37, -19, 23) mm in all three coordinates.
    const Placement p = placementOfTheModel();
    requireClose(p.r[0][0], 2.0 / 3.0, kClassA, "R[0][0]");
    requireClose(p.r[2][0], -1.0 / 3.0, kClassA, "R[2][0]: X has a negative z component");
    requireClose(heat::determinant(p.r), 1.0, kClassA, "det R");
    CHECK(heat::orthogonalityError(p.r) <= 1.0e-15);
    requireClose(p.t, Vec3{0.037, -0.019, 0.023}, kClassA, "the translation, in metres");
    evidence("RM-THERM-06 det " + format(heat::determinant(p.r)) + " orthogonality " + format(heat::orthogonalityError(p.r)));
}

TEST_CASE("ThermalReference_RM06_ScalarsInvariantVectorsRotated", "[refmod][thermal][rm06]") {
    // The slab arrangement: the datum cap at 400 K, the opposite cap at 300 K. The field is AFFINE along the extrusion axis, so
    // it is known in closed form in BOTH frames: T = 400 - 100 w/0.024 along the axis w (the base's z, the placed model's N), and
    // it does not depend on where the interior node of either mesh happens to sit.
    TransformedPair pair = buildPair();
    const Placement placement = placementOfTheModel();
    const meshing::Mesh& baseMesh = pair.base->mesh();
    const meshing::Mesh& placedMesh = pair.placed->mesh();

    // THE TWO MESH POPULATIONS, SEPARATELY. P16 does not promise the same tetrahedralisation in a rotated frame; for THIS block on
    // THIS frame the counts are measured to agree (RM-MESH-06, RM-FEA-06), and that is asserted, not assumed.
    const Populations bp = populationsOf(baseMesh);
    const Populations pp = populationsOf(placedMesh);
    evidence("RM-THERM-06 base boundary_nodes " + std::to_string(bp.boundaryNodes) + " interior_nodes " + std::to_string(bp.interiorNodes) +
             " boundary_triangles " + std::to_string(bp.boundaryTriangles) + " tets " + std::to_string(bp.tetrahedra));
    evidence("RM-THERM-06 placed boundary_nodes " + std::to_string(pp.boundaryNodes) + " interior_nodes " + std::to_string(pp.interiorNodes) +
             " boundary_triangles " + std::to_string(pp.boundaryTriangles) + " tets " + std::to_string(pp.tetrahedra));
    CHECK(bp.boundaryNodes == pp.boundaryNodes);
    CHECK(bp.interiorNodes == pp.interiorNodes);
    CHECK(bp.boundaryTriangles == pp.boundaryTriangles);
    CHECK(bp.tetrahedra == pp.tetrahedra);

    // THE BOUNDARY POPULATION corresponds point for point (the corners of the block are rigid images of each other). THE INTERIOR
    // NODE DOES NOT, AND THAT IS WHAT "JUDGE THEM SEPARATELY" MEANS: it is placed by the mesher from an axis-aligned size tree, so
    // in a rotated frame it is not the image of the base's. Its displacement from the image is measured and reported.
    const std::vector<bool> baseOnBoundary = [&] {
        std::vector<bool> b(baseMesh.nodes().size(), false);
        std::unordered_map<std::uint64_t, std::size_t> index;
        for (std::size_t i = 0; i < baseMesh.nodes().size(); ++i) {
            index.emplace(baseMesh.nodes()[i].id.value(), i);
        }
        for (const meshing::Triangle& tri : baseMesh.triangles()) {
            for (const meshing::NodeId id : tri.nodes) {
                b[index.at(id.value())] = true;
            }
        }
        return b;
    }();
    std::vector<std::optional<std::size_t>> imageOf(baseMesh.nodes().size());
    double interiorShift = 0.0;
    for (std::size_t i = 0; i < baseMesh.nodes().size(); ++i) {
        const Vec3 image = movePoint(placement, positionOf(baseMesh.nodes()[i].position));
        double best = 1e300;
        std::size_t bestJ = 0;
        for (std::size_t j = 0; j < placedMesh.nodes().size(); ++j) {
            const double d = heat::norm(heat::minus(positionOf(placedMesh.nodes()[j].position), image));
            if (d < best) {
                best = d;
                bestJ = j;
            }
        }
        if (baseOnBoundary[i]) {
            REQUIRE(best < 1.0e-9); // every boundary node has its image
            imageOf[i] = bestJ;
        } else {
            interiorShift = best;
        }
    }
    evidence("RM-THERM-06 the interior node's distance from the image of the base's: " + format(interiorShift * 1e3) + " mm");
    CHECK(interiorShift < 1.0e-5); // a few microns at most; measured 0.27 micron (cause not established: frame dependence of the mesher)
    requireClose(meshVolume(baseMesh), 0.09 * 0.055 * 0.024, Tolerance{0.0, 1e-12}, "the base volume (hand: 90 x 55 x 24 mm)");
    requireClose(meshVolume(placedMesh), meshVolume(baseMesh), Tolerance{0.0, 1e-12}, "the placed volume");

    const thermal::ThermalPostResult& basePost = pair.base->post();
    const thermal::ThermalPostResult& placedPost = pair.placed->post();

    // TEMPERATURE is a scalar: against the closed form in each frame, and boundary node against its image.
    const Vec3 axis = heat::apply(placement.r, Vec3{0.0, 0.0, 1.0});
    double worstScalar = 0.0;
    for (std::size_t i = 0; i < baseMesh.nodes().size(); ++i) {
        const double tb = basePost.finalState().temperatures[i].si();
        requireClose(tb, 400.0 - 100.0 * baseMesh.nodes()[i].position.z.si() / 0.024, kClassB, "base temperature against the closed form");
        if (imageOf[i].has_value()) {
            worstScalar = std::max(worstScalar, std::abs(tb - placedPost.finalState().temperatures[*imageOf[i]].si()));
        }
    }
    for (std::size_t j = 0; j < placedMesh.nodes().size(); ++j) {
        const Vec3 xp = positionOf(placedMesh.nodes()[j].position);
        requireClose(placedPost.finalState().temperatures[j].si(), 400.0 - 100.0 * heat::dot(heat::minus(xp, placement.t), axis) / 0.024,
                     kClassB, "placed temperature against the closed form in the rotated frame");
    }
    CHECK(worstScalar <= 1.0e-9);

    // GRADIENT AND FLUX rotate: every element of each mesh against the closed form (R times the base's), component by component.
    const Vec3 gBase{0.0, 0.0, -100.0 / 0.024};
    const Vec3 qBase = heat::scaled(gBase, -15.0);
    double worstG = 0.0;
    double worstQ = 0.0;
    double worstMagnitude = 0.0;
    for (std::size_t e = 0; e < baseMesh.tetrahedra().size(); ++e) {
        requireClose(vec(basePost.finalState().fields.gradients[e]), gBase, Tolerance{1e-6, 1e-9}, "base gradient [0, 0, -100/0.024]");
        requireClose(vec(basePost.finalState().fields.heatFluxes[e]), qBase, Tolerance{1e-4, 1e-9}, "base flux");
    }
    for (std::size_t e = 0; e < placedMesh.tetrahedra().size(); ++e) {
        const Vec3 gp = vec(placedPost.finalState().fields.gradients[e]);
        const Vec3 qp = vec(placedPost.finalState().fields.heatFluxes[e]);
        const Vec3 rg = heat::apply(placement.r, gBase);
        const Vec3 rq = heat::apply(placement.r, qBase);
        for (std::size_t c = 0; c < 3; ++c) {
            worstG = std::max(worstG, std::abs(gp[c] - rg[c]));
            worstQ = std::max(worstQ, std::abs(qp[c] - rq[c]));
        }
        worstMagnitude = std::max({worstMagnitude, std::abs(heat::norm(gp) - heat::norm(gBase)), std::abs(heat::norm(qp) - heat::norm(qBase))});
    }
    evidence("RM-THERM-06 scalar " + format(worstScalar) + " placed_gradient_vs_R_g " + format(worstG) + " placed_flux_vs_R_q " + format(worstQ) +
             " magnitude " + format(worstMagnitude));
    CHECK(worstG <= 1.0e-6);
    CHECK(worstQ <= 1.0e-4);
    CHECK(worstMagnitude <= 1.0e-4);
    // and the rotation is not the identity on this gradient: the placed gradient does not point along any coordinate axis
    const Vec3 rg = heat::apply(placement.r, gBase);
    CHECK(std::abs(rg[0]) > 1.0);
    CHECK(std::abs(rg[1]) > 1.0);
    CHECK(std::abs(rg[2]) > 1.0);

    // HEAT RATE, REACTIONS, BALANCE and the conduction energy do not change. Qdot = k A dT/L = 15 x 0.00495 x 100/0.024.
    const double expectedQ = 15.0 * 0.09 * 0.055 * 100.0 / 0.024;
    requireClose(expectedQ, 309.375, kClassA, "Qdot by hand");
    for (Run* run : {pair.base.get(), pair.placed.get()}) {
        const thermal::ThermalHeatBalance& b = run->balance().finalState();
        requireClose(b.terms.prescribedTemperatureReaction.si(), 0.0, Tolerance{1e-9, 0.0}, "the reactions of a sourceless slab sum to zero");
        double inflow = 0.0;
        for (const thermal::ReactionHeat& r : b.reactions) {
            inflow += std::max(0.0, r.heatInto.si());
        }
        requireClose(inflow, expectedQ, Tolerance{1e-9, 1e-9}, "the heat supplied by the hot cap");
        CHECK(run->balance().worstNormalizedResidual() <= 1.0e-12);
    }
    double energyBase = 0.0;
    double energyPlaced = 0.0;
    for (std::size_t e = 0; e < baseMesh.tetrahedra().size(); ++e) {
        const Vec3 g = vec(basePost.finalState().fields.gradients[e]);
        energyBase += 15.0 * std::abs(oracleTet(baseMesh, baseMesh.tetrahedra()[e]).signedVolume()) * heat::dot(g, g);
    }
    for (std::size_t e = 0; e < placedMesh.tetrahedra().size(); ++e) {
        const Vec3 gp = vec(placedPost.finalState().fields.gradients[e]);
        energyPlaced += 15.0 * std::abs(oracleTet(placedMesh, placedMesh.tetrahedra()[e]).signedVolume()) * heat::dot(gp, gp);
    }
    requireClose(energyPlaced, energyBase, Tolerance{0.0, 1e-9}, "the conduction integral k |grad T|^2 dV is invariant");
    requireClose(energyBase, 15.0 * 0.09 * 0.055 * 0.024 * heat::dot(gBase, gBase), Tolerance{0.0, 1e-9}, "and equals k V |grad T|^2");
}

TEST_CASE("ThermalReference_RM06_ANonAxisAlignedAffineFieldRotates", "[refmod][thermal][rm06]") {
    // A field that points along NO body axis: T = 300 + b . x with b = (7.2, -3.5, 11.4) K/m in the BASE frame, and the same
    // physical field in the placed frame, T'(x') = T(R^T (x' - t)), whose gradient is R b. It is imposed on the boundary nodes of
    // each real P16 mesh (no canonical condition can do that) and the interior node is left free; an affine field makes the
    // solution exact whatever the mesh, so the comparison does not depend on where either mesh put its interior node.
    //
    // T' = T at every boundary node and its image; grad T' = R grad T and q' = R q in every element; the reactions -- heat, a
    // scalar -- are the same at a node and its image when the boundary triangulation corresponds. A component swap, a transposed
    // R or an unrotated flux cannot survive a gradient that points along no coordinate axis.
    TransformedPair pair = buildPair();
    const Placement placement = placementOfTheModel();
    const meshing::Mesh& baseMesh = pair.base->mesh();
    const meshing::Mesh& placedMesh = pair.placed->mesh();
    const Vec3 b{7.2, -3.5, 11.4};
    const Vec3 rb = heat::apply(placement.r, b);
    const heat::Mat3 rt = heat::fromColumns(placement.r[0], placement.r[1], placement.r[2]); // R^T: the rows of R are the columns of R^T
    const PatchSolution base = solveAffinePatch(baseMesh, 15.0, [&](const Vec3& x) { return 300.0 + heat::dot(b, x); });
    const PatchSolution placed = solveAffinePatch(placedMesh, 15.0, [&](const Vec3& xp) {
        return 300.0 + heat::dot(b, heat::apply(rt, heat::minus(xp, placement.t)));
    });

    // every element, both meshes, against the closed forms
    double worstG = 0.0;
    double worstQ = 0.0;
    for (std::size_t e = 0; e < base.gradient.size(); ++e) {
        requireClose(base.gradient[e], b, kClassB, "base gradient");
        requireClose(base.flux[e], heat::scaled(b, -15.0), kClassB, "base flux");
    }
    for (std::size_t e = 0; e < placed.gradient.size(); ++e) {
        requireClose(placed.gradient[e], rb, kClassB, "placed gradient is R b");
        requireClose(placed.flux[e], heat::scaled(rb, -15.0), kClassB, "placed flux is -k R b");
        for (std::size_t c = 0; c < 3; ++c) {
            worstG = std::max(worstG, std::abs(placed.gradient[e][c] - rb[c]));
            worstQ = std::max(worstQ, std::abs(placed.flux[e][c] + 15.0 * rb[c]));
        }
        CHECK(std::abs(heat::norm(placed.gradient[e]) - heat::norm(b)) <= 1.0e-8);
    }
    // the interior node of each mesh lands on its own plane
    for (std::size_t i = 0; i < baseMesh.nodes().size(); ++i) {
        if (!base.boundary[i]) {
            requireClose(base.temperature[i], 300.0 + heat::dot(b, positionOf(baseMesh.nodes()[i].position)), kClassB, "base interior node");
        }
    }
    for (std::size_t j = 0; j < placedMesh.nodes().size(); ++j) {
        if (!placed.boundary[j]) {
            const Vec3 xp = positionOf(placedMesh.nodes()[j].position);
            requireClose(placed.temperature[j], 300.0 + heat::dot(b, heat::apply(rt, heat::minus(xp, placement.t))), kClassB, "placed interior node");
        }
    }
    // scalars and heat: a boundary node and its image
    double worstScalar = 0.0;
    double worstReaction = 0.0;
    double reactionBase = 0.0;
    double reactionPlaced = 0.0;
    std::size_t matched = 0;
    for (std::size_t i = 0; i < baseMesh.nodes().size(); ++i) {
        if (!base.boundary[i]) {
            continue;
        }
        const Vec3 image = movePoint(placement, positionOf(baseMesh.nodes()[i].position));
        for (std::size_t j = 0; j < placedMesh.nodes().size(); ++j) {
            if (heat::norm(heat::minus(positionOf(placedMesh.nodes()[j].position), image)) < 1.0e-9) {
                worstScalar = std::max(worstScalar, std::abs(base.temperature[i] - placed.temperature[j]));
                worstReaction = std::max(worstReaction, std::abs(base.reaction[i] - placed.reaction[j]));
                reactionBase += std::abs(base.reaction[i]);
                reactionPlaced += std::abs(placed.reaction[j]);
                ++matched;
            }
        }
    }
    REQUIRE(matched == 8); // every boundary node of the 9-node block found its image (the sums above are not empty)
    evidence("RM-THERM-06 affine patch: gradient_vs_R_b " + format(worstG) + " flux_vs_R_q " + format(worstQ) + " boundary_scalar " + format(worstScalar) +
             " boundary_reaction_difference " + format(worstReaction) + " sum|reaction| base " + format(reactionBase) + " placed " + format(reactionPlaced));
    CHECK(worstScalar <= 1.0e-9);
    // the same heat leaves through the same boundary: the total reaction, the sum of the magnitudes, is a scalar of the problem
    requireClose(reactionPlaced, reactionBase, Tolerance{1e-9, 1e-9}, "the total heat exchanged at the boundary is invariant");
}

// ---------------------------------------------------------------------------
// RM-THERM-07
// ---------------------------------------------------------------------------

namespace {

constexpr double kBeamLength = 0.1;
constexpr double kBandDepth = 0.010; // 10 mm: twice the 5 mm control's slab, so the band contains it

/// The metrics of one band of the VOLUME: the tetrahedra whose centroid lies in [lo, hi] along z.
struct Band {
    std::size_t tets = 0;
    double meanSize = 0.0;
    double medianSize = 0.0;
    double maxSize = 0.0;
};

/// A tetrahedron's characteristic size: the edge of the regular tetrahedron of the same volume, (6 sqrt(2) V)^(1/3). The suite's
/// own measure, from the oracle's volume; no P16 quality metric is used to decide what "refined" means.
[[nodiscard]] double characteristicSize(double volume) { return std::cbrt(6.0 * std::sqrt(2.0) * volume); }

[[nodiscard]] Band bandOf(const meshing::Mesh& mesh, double lo, double hi) {
    std::vector<double> sizes;
    for (const meshing::Tetrahedron& tet : mesh.tetrahedra()) {
        const heat::Tet oracle = oracleTet(mesh, tet);
        const double z = centroidOf(oracle)[2];
        if (z >= lo && z <= hi) {
            sizes.push_back(characteristicSize(std::abs(oracle.signedVolume())));
        }
    }
    Band band;
    band.tets = sizes.size();
    if (!sizes.empty()) {
        std::sort(sizes.begin(), sizes.end());
        double sum = 0.0;
        for (const double s : sizes) {
            sum += s;
        }
        band.meanSize = sum / static_cast<double>(sizes.size());
        band.medianSize = sizes[sizes.size() / 2];
        band.maxSize = sizes.back();
    }
    return band;
}

/// Volume-weighted RMS of the gradient error against the closed form dT/dz = q'''/(2k) (L - 2z), over the tetrahedra whose
/// centroid lies in [lo, hi] -- the quantity the refinement is meant to improve.
[[nodiscard]] double gradientErrorIn(const meshing::Mesh& mesh, const thermal::ThermalPostResult& post, double lo, double hi) {
    double sum = 0.0;
    double volume = 0.0;
    for (std::size_t e = 0; e < mesh.tetrahedra().size(); ++e) {
        const heat::Tet oracle = oracleTet(mesh, mesh.tetrahedra()[e]);
        const Vec3 c = centroidOf(oracle);
        if (c[2] < lo || c[2] > hi) {
            continue;
        }
        const double exact = reference::kLocalGenerationWattsPerM3 / (2.0 * reference::kLocalMaterial.conductivity) * (kBeamLength - 2.0 * c[2]);
        const Vec3 g = vec(post.finalState().fields.gradients[e]);
        const double v = std::abs(oracle.signedVolume());
        // the transverse components are zero in the exact field
        sum += v * ((g[2] - exact) * (g[2] - exact) + g[0] * g[0] + g[1] * g[1]);
        volume += v;
    }
    return volume > 0.0 ? std::sqrt(sum / volume) : 0.0;
}

struct BeamRun {
    std::unique_ptr<Run> run;
    FaceName start, end;
};

[[nodiscard]] BeamRun beamRun(reference::ThermalLocalRefinement refinement) {
    auto built = reference::buildThermalLocalRefinementReferenceModel(refinement);
    REQUIRE(built.has_value());
    BeamRun out;
    out.start = built->start();
    out.end = built->end();
    out.run = std::make_unique<Run>(std::move(built->document));
    return out;
}

} // namespace

TEST_CASE("ThermalReference_RM07_RefinementIsMeasuredInTheVolumeBand", "[refmod][thermal][rm07]") {
    BeamRun baseline = beamRun(reference::ThermalLocalRefinement::None);
    BeamRun target = beamRun(reference::ThermalLocalRefinement::Start);
    BeamRun mirror = beamRun(reference::ThermalLocalRefinement::End);

    // THE SURFACE IS NOT THE EVIDENCE. A planar cap has two triangles whatever its local size, so the boundary triangle count is
    // the SAME in all three meshes -- which is exactly why counting cap triangles would prove nothing -- while the VOLUME differs.
    const meshing::Mesh& mb = baseline.run->mesh();
    const meshing::Mesh& mt = target.run->mesh();
    const meshing::Mesh& mm = mirror.run->mesh();
    CHECK(mb.triangles().size() == mt.triangles().size());
    CHECK(mt.triangles().size() == mm.triangles().size());
    CHECK(mt.tetrahedra().size() > mb.tetrahedra().size());
    CHECK(mm.tetrahedra().size() > mb.tetrahedra().size());

    const double nearStart[2] = {0.0, kBandDepth};
    const double nearEnd[2] = {kBeamLength - kBandDepth, kBeamLength};
    const Band tb = bandOf(mb, nearStart[0], nearStart[1]);
    const Band tt = bandOf(mt, nearStart[0], nearStart[1]);
    const Band tm = bandOf(mm, nearStart[0], nearStart[1]);
    const Band eb = bandOf(mb, nearEnd[0], nearEnd[1]);
    const Band et = bandOf(mt, nearEnd[0], nearEnd[1]);
    const Band em = bandOf(mm, nearEnd[0], nearEnd[1]);
    const auto line = [](const char* name, const Band& b) {
        return std::string{name} + " tets " + std::to_string(b.tets) + " mean_size_mm " + format(b.meanSize * 1e3) + " median_mm " +
               format(b.medianSize * 1e3) + " max_mm " + format(b.maxSize * 1e3);
    };
    evidence("RM-THERM-07 nodes baseline " + std::to_string(mb.nodes().size()) + " target " + std::to_string(mt.nodes().size()) + " mirror " +
             std::to_string(mm.nodes().size()) + " | tets " + std::to_string(mb.tetrahedra().size()) + " " + std::to_string(mt.tetrahedra().size()) +
             " " + std::to_string(mm.tetrahedra().size()) + " | boundary triangles " + std::to_string(mb.triangles().size()));
    evidence("RM-THERM-07 start-band baseline: " + line("", tb));
    evidence("RM-THERM-07 start-band target:   " + line("", tt));
    evidence("RM-THERM-07 start-band mirror:   " + line("", tm));
    evidence("RM-THERM-07 end-band   baseline: " + line("", eb));
    evidence("RM-THERM-07 end-band   target:   " + line("", et));
    evidence("RM-THERM-07 end-band   mirror:   " + line("", em));

    // THE TARGET CONTROL REFINES THE START BAND, measured in the volume: the baseline has NO element whose centroid lies in the
    // band (its elements span the beam), and the target mesh has dozens, whose mean characteristic size is a fraction of the
    // unrefined mesh's. (These are REGRESSION GUARDS SET FROM THE MEASURED BEHAVIOUR of P16's skin sizing, as the brief asks for
    // this model -- "freeze quantitative criteria from measured P16 behavior" -- and not a priori bounds: P16 treats the size as
    // a request on size-tree boxes, so individual elements in the skin reach 2 to 4 times it, and the guard is on the MEAN.)
    double baselineMean = 0.0;
    for (const meshing::Tetrahedron& tet : mb.tetrahedra()) {
        baselineMean += characteristicSize(std::abs(oracleTet(mb, tet).signedVolume())) / static_cast<double>(mb.tetrahedra().size());
    }
    evidence("RM-THERM-07 the unrefined mesh's mean characteristic size (mm) " + format(baselineMean * 1e3));
    CHECK(tb.tets == 0);
    CHECK(eb.tets == 0);
    CHECK(tt.tets >= 8);
    CHECK(em.tets >= 8);
    // one third of the unrefined mean: measured 0.20 (target) and 0.28 (mirror) -- Netgen's meshes are not mirror images of each other
    CHECK(tt.meanSize <= baselineMean / 3.0);
    CHECK(em.meanSize <= baselineMean / 3.0);
    // and the REST WAS LEFT ALONE: the far band of each refined document is still coarse, in absolute terms (measured 48.7 mm and
    // 32.4 mm against an unrefined mean of 55.4 mm) -- so a control that refined everything would fail here, not pass
    CHECK(et.meanSize > baselineMean / 2.0);
    CHECK(tm.meanSize > baselineMean / 2.0);
    // THE MIRROR CONTROL MOVES IT: the end band is refined in the mirror document and not in the target document, and the start
    // band is refined in the target document and not in the mirror document.
    CHECK(em.tets > et.tets);
    CHECK(tt.tets > tm.tets);
    CHECK(em.meanSize < et.meanSize);
    CHECK(tt.meanSize < tm.meanSize);
}
TEST_CASE("ThermalReference_RM07_MappingPreservedAndTheSolveIsValid", "[refmod][thermal][rm07]") {
    // The canonical references are the CAD faces; after refinement they must still resolve to the SAME faces, found by geometry:
    // the held nodes of the start cap are exactly the nodes at z = 0, and of the end cap exactly those at z = L. Nothing is
    // rebound, and every document is accepted.
    for (const reference::ThermalLocalRefinement refinement :
         {reference::ThermalLocalRefinement::None, reference::ThermalLocalRefinement::Start, reference::ThermalLocalRefinement::End}) {
        BeamRun beam = beamRun(refinement);
        Run& run = *beam.run;
        INFO("refinement " << static_cast<int>(refinement));
        CHECK(run.run().accepted());
        const meshing::GeometryMeshMap& map = run.ref().map();
        for (const FaceName& cap : {beam.start, beam.end}) {
            Result<meshing::BoundaryFacetSet> facets = meshing::boundaryFacetsOf(map, cap);
            REQUIRE(facets.has_value());
            CHECK(facets->fullyResolved());
            CHECK(facets->facets.size() == 2); // a planar face: two triangles, whatever the local size
        }
        // The held set found by production equals the set found by COORDINATES.
        std::set<std::size_t> geometric;
        for (std::size_t i = 0; i < run.mesh().nodes().size(); ++i) {
            const double z = run.mesh().nodes()[i].position.z.si();
            if (std::abs(z) < 1e-9 || std::abs(z - kBeamLength) < 1e-9) {
                geometric.insert(i);
            }
        }
        std::set<std::size_t> produced;
        for (const thermal::ReactionHeat& r : run.balance().finalState().reactions) {
            REQUIRE(run.run().dofMap.has_value());
        const Result<meshing::NodeId> node = run.run().dofMap->nodeOf(r.dof);
            REQUIRE(node.has_value());
            for (std::size_t i = 0; i < run.mesh().nodes().size(); ++i) {
                if (run.mesh().nodes()[i].id == *node) {
                    produced.insert(i);
                }
            }
        }
        CHECK(produced == geometric);
        // the heat balance closes on every mesh
        CHECK(run.balance().worstNormalizedResidual() <= 1.0e-12);
        // generated heat = q''' V with V the beam's exact volume (a box): 1e5 x 0.04 x 0.06 x 0.1 = 24 W
        requireClose(run.balance().finalState().terms.generatedHeat.si(), 24.0, Tolerance{0.0, 1e-11}, "the generated heat, 1e5 x 2.4e-4 m^3");
    }
}

TEST_CASE("ThermalReference_RM07_TheTrendIsMeasuredNotAssumed", "[refmod][thermal][rm07]") {
    // THE TREND, MEASURED. The field is the parabola T(z) = Ts + q'''/(2k) z (L - z), whose gradient is LARGEST at the two held
    // caps (2500 K/m). The gradient error is measured over the region beside each cap (z within 25 mm of it, so that the baseline,
    // which has no element inside the 10 mm band, still has elements to judge).
    //
    // THE BRIEF'S EXPECTATION -- refinement where the gradient is steep "does not make the local error systematically worse and
    // should usually reduce it" -- IS NOT MET BY THIS GEOMETRY, and the measurement says why: P16 refines a SKIN one target deep
    // beside the named face, the unrefined core behind it dominates the error (the baseline is already off by two thirds of the
    // cap gradient and the temperature of the nodes inside the skin is 16 K RMS low), and a nodal-conserving solve puts exactly
    // 12 W through each cap on every mesh whatever its refinement. So the refinement changes the local error by a few percent
    // either way, and that is what is gated: NOT MATERIALLY DIFFERENT. It is a finding for the owner (local sizing is a skin, not a
    // graded refinement), recorded in AUDIT.md, and it is not presented as an improvement.
    BeamRun baseline = beamRun(reference::ThermalLocalRefinement::None);
    BeamRun target = beamRun(reference::ThermalLocalRefinement::Start);
    BeamRun mirror = beamRun(reference::ThermalLocalRefinement::End);
    const double region = 0.025;
    const auto errors = [&](BeamRun& b) {
        const thermal::ThermalPostResult& post = b.run->post();
        return std::array<double, 2>{gradientErrorIn(b.run->mesh(), post, 0.0, region),
                                     gradientErrorIn(b.run->mesh(), post, kBeamLength - region, kBeamLength)};
    };
    const std::array<double, 2> eb = errors(baseline);
    const std::array<double, 2> et = errors(target);
    const std::array<double, 2> em = errors(mirror);
    evidence("RM-THERM-07 gradient_rms_K_m near_start/near_end baseline " + format(eb[0]) + " " + format(eb[1]) + " target " + format(et[0]) +
             " " + format(et[1]) + " mirror " + format(em[0]) + " " + format(em[1]));
    evidence("RM-THERM-07 change of the refined side's error relative to the baseline: target " + format(et[0] / eb[0] - 1.0) + " mirror " +
             format(em[1] / eb[1] - 1.0));

    // The baseline is genuinely wrong near the caps (else there is nothing to improve and the comparison shows nothing).
    CHECK(eb[0] > 0.5 * 2500.0);
    CHECK(eb[1] > 0.5 * 2500.0);
    // Not materially different where the control is: within 10 percent either way (measured +4.7 and +4.1 percent).
    CHECK(std::abs(et[0] / eb[0] - 1.0) <= 0.10);
    CHECK(std::abs(em[1] / eb[1] - 1.0) <= 0.10);
    // and the heat through each cap is the generated heat's half, on every mesh, to rounding
    for (BeamRun* beam : {&baseline, &target, &mirror}) {
        double start = 0.0;
        double end = 0.0;
        for (const thermal::ReactionHeat& r : beam->run->balance().finalState().reactions) {
            REQUIRE(beam->run->run().dofMap.has_value());
        const Result<meshing::NodeId> node = beam->run->run().dofMap->nodeOf(r.dof);
            REQUIRE(node.has_value());
            (beam->run->mesh().findNode(*node)->position.z.si() < 0.05 ? start : end) += r.heatInto.si();
        }
        requireClose(start, -12.0, Tolerance{1e-9, 1e-10}, "heat removed through the start cap (q''' V / 2)");
        requireClose(end, -12.0, Tolerance{1e-9, 1e-10}, "heat removed through the end cap (q''' V / 2)");
    }
}