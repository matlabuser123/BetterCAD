#pragma once

#include "features/FeatureTestSupport.hpp"
#include "support/ShellModels.hpp"

#include <bettercad/core/document/Document.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/core/geometry/VariableFillet.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/VariableFilletFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::test {

// P12-FEAT-006 references: variable-radius fillets of straight edges between
// planes, computed here without BetterCAD's law code.
//
// The law (documented in VariableFillet.hpp) is the cubic spline through
// (-1/2, r_0), the stations and (3/2, r_n) with zero slope at -1/2 and 3/2.
// ReferenceLaw builds it in Hermite form from its knot slopes, a different
// linear system from the product's second-derivative one; for two stations
// it is the closed form a + (b - a) phi(u), phi(u) = u - 4/7 u (1 - u)(1 - 2 u),
// derived by hand (docs/verification/P12-FEAT-006).
//
// In the plane normal to the edge, a fillet of radius r between faces whose
// outward normals are g apart is the arc tangent to both, r tan(g/2) from
// the edge on each face; it takes the area r^2 (tan(g/2) - g/2) from a
// convex corner, or adds it to a concave one. Along the edge the areas are
// integrated exactly (6-point Gauss-Legendre on each station span, exact to
// degree 11; the integrands are of degree 10 at most).

/// Radius stations: (position, radius in mm).
using Stations = std::vector<std::pair<double, double>>;

/// phi above: the two-station law's shape.
inline double twoStationShape(double u) {
    return u - 4.0 / 7.0 * u * (1.0 - u) * (1.0 - 2.0 * u);
}

class ReferenceLaw {
public:
    explicit ReferenceLaw(const Stations& stations) {
        x_.push_back(-0.5);
        r_.push_back(stations.front().second);
        for (const auto& [u, r] : stations) {
            x_.push_back(u);
            r_.push_back(r);
        }
        x_.push_back(1.5);
        r_.push_back(stations.back().second);
        const std::size_t n = x_.size();
        // C2 at each inner knot i (1 .. n-2), with h[i] = x[i+1] - x[i] and
        // s[i] the chord slope of span i:
        //   h[i] k[i-1] + 2 (h[i-1] + h[i]) k[i] + h[i-1] k[i+1] = 3 (h[i] s[i-1] + h[i-1] s[i]),
        // and k[0] = k[n-1] = 0 (the zero end slopes).
        const auto h = [&](std::size_t i) { return x_[i + 1] - x_[i]; };
        const auto s = [&](std::size_t i) { return (r_[i + 1] - r_[i]) / h(i); };
        std::vector<double> sub(n, 0.0);
        std::vector<double> diag(n, 1.0);
        std::vector<double> sup(n, 0.0);
        std::vector<double> rhs(n, 0.0);
        for (std::size_t i = 1; i + 1 < n; ++i) {
            sub[i] = i > 1 ? h(i) : 0.0; // k[0] = 0 contributes nothing
            diag[i] = 2.0 * (h(i - 1) + h(i));
            sup[i] = i + 2 < n ? h(i - 1) : 0.0; // nor does k[n-1]
            rhs[i] = 3.0 * (h(i) * s(i - 1) + h(i - 1) * s(i));
        }
        for (std::size_t i = 2; i + 1 < n; ++i) {
            const double f = sub[i] / diag[i - 1];
            diag[i] -= f * sup[i - 1];
            rhs[i] -= f * rhs[i - 1];
        }
        k_.assign(n, 0.0);
        for (std::size_t i = n - 2; i >= 1; --i) {
            k_[i] = (rhs[i] - (i + 2 < n ? sup[i] * k_[i + 1] : 0.0)) / diag[i];
        }
    }

    /// The radius (mm) at @p u.
    double operator()(double u) const {
        std::size_t i = 0;
        while (i + 2 < x_.size() && u > x_[i + 1]) {
            ++i;
        }
        const double h = x_[i + 1] - x_[i];
        const double t = (u - x_[i]) / h;
        const double t2 = t * t;
        const double t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * r_[i] + (t3 - 2 * t2 + t) * h * k_[i] + (-2 * t3 + 3 * t2) * r_[i + 1] +
               (t3 - t2) * h * k_[i + 1];
    }

    /// The integral over [0, 1] of @p f(u, r(u)), exact for polynomials up
    /// to degree 11 on each station span.
    template <typename F>
    double integrate(F f) const {
        static constexpr std::array<double, 6> node = {-0.9324695142031521, -0.6612093864662645,
                                                      -0.2386191860831909, 0.2386191860831909,
                                                      0.6612093864662645,  0.9324695142031521};
        static constexpr std::array<double, 6> weight = {0.1713244923791704, 0.3607615730481386,
                                                        0.4679139345726910, 0.4679139345726910,
                                                        0.3607615730481386, 0.1713244923791704};
        double sum = 0.0;
        for (std::size_t i = 1; i + 2 < x_.size(); ++i) {
            const double mid = (x_[i] + x_[i + 1]) / 2.0;
            const double half = (x_[i + 1] - x_[i]) / 2.0;
            for (std::size_t j = 0; j < node.size(); ++j) {
                const double u = mid + half * node[j];
                sum += weight[j] * half * f(u, (*this)(u));
            }
        }
        return sum;
    }

private:
    std::vector<double> x_;
    std::vector<double> r_;
    std::vector<double> k_;
};

/// Area a fillet of radius 1 takes from (or adds to) a corner between faces
/// whose outward normals are @p g apart: the kite of the two tangent points
/// less the sector between them.
inline double sectionArea(double g) {
    return std::tan(g / 2.0) - g / 2.0;
}

/// Distance of that area's centroid from the edge, along the bisector, for
/// radius 1. The kite (area tan(g/2)) has its centroid at
/// (1 + sin^2(g/2)) / (3 cos(g/2)); the sector (area g/2, centre 1/cos(g/2)
/// out) at 1/cos(g/2) - 4 sin(g/2) / (3 g).
inline double sectionCentroid(double g) {
    const double kite = std::tan(g / 2.0);
    const double kiteAt = (1.0 + std::sin(g / 2.0) * std::sin(g / 2.0)) / (3.0 * std::cos(g / 2.0));
    const double sector = g / 2.0;
    const double sectorAt = 1.0 / std::cos(g / 2.0) - 4.0 * std::sin(g / 2.0) / (3.0 * g);
    return (kite * kiteAt - sector * sectorAt) / (kite - sector);
}

/// A straight edge rounded with a variable radius.
struct RoundedEdge {
    Vec3 start;       // mm: the end first along the canonical direction
    Vec3 direction;   // unit, canonical
    double length;    // mm
    Vec3 bisector;    // unit, from the edge towards the changed area's centroid
    double angle;     // between the faces' outward normals
    bool convex;      // convex edges lose material, concave ones gain it
    Stations stations;

    /// The material the fillet removes (negative volume) or adds.
    [[nodiscard]] Part part() const {
        const ReferenceLaw law(stations);
        const double r2 = law.integrate([](double, double r) { return r * r; });
        const double ur2 = law.integrate([](double u, double r) { return u * r * r; });
        const double r3 = law.integrate([](double, double r) { return r * r * r; });
        const double volume = (convex ? -1.0 : 1.0) * sectionArea(angle) * length * r2;
        const double along = length * ur2 / r2;
        const double out = sectionCentroid(angle) * r3 / r2;
        Vec3 centre{};
        for (std::size_t i = 0; i < 3; ++i) {
            centre[i] = start[i] + direction[i] * along + bisector[i] * out;
        }
        return {volume, centre};
    }

    /// The area of each face the fillet no longer covers (convex) or newly
    /// covers (concave): the strip from the edge to the contact line.
    [[nodiscard]] double strip() const {
        const ReferenceLaw law(stations);
        return std::tan(angle / 2.0) * length * law.integrate([](double, double r) { return r; });
    }
};

/// An edge of an axis-aligned box's convex corner: faces at 90 degrees.
inline RoundedEdge boxEdge(const Vec3& start, const Vec3& direction, double length, const Vec3& inward,
                           Stations stations) {
    return {start, direction, length, inward, kPi / 2.0, true, std::move(stations)};
}

inline std::vector<geometry::RadiusStation> radiusStations(const Stations& stations) {
    std::vector<geometry::RadiusStation> result;
    for (const auto& [u, r] : stations) {
        result.push_back({.position = u, .radius = r * units::mm});
    }
    return result;
}

/// Every property of a body that must repeat exactly: validity, topology,
/// volume, area, centre, bounds and the names of its faces.
struct BodyPrint {
    bool valid = false;
    geometry::TopologySummary topology{};
    double volume = 0.0;
    double area = 0.0;
    std::array<double, 3> centre{};
    std::array<double, 3> lower{};
    std::array<double, 3> upper{};
    std::vector<std::vector<FaceName>> names{};

    friend bool operator==(const BodyPrint&, const BodyPrint&) = default;
};

inline BodyPrint printOf(const geometry::Body& body) {
    BodyPrint print{.valid = body.isValid(), .topology = body.topology()};
    const auto properties = body.massProperties();
    REQUIRE(properties.has_value());
    print.volume = properties->volume.si();
    print.area = properties->surfaceArea.si();
    print.centre = {properties->centerOfMass.x.si(), properties->centerOfMass.y.si(),
                    properties->centerOfMass.z.si()};
    const auto box = body.boundingBox();
    REQUIRE(box.has_value());
    print.lower = {box->min.x.si(), box->min.y.si(), box->min.z.si()};
    print.upper = {box->max.x.si(), box->max.y.si(), box->max.z.si()};
    const auto faces = geometry::listFaces(body);
    REQUIRE(faces.has_value());
    for (const geometry::FaceInfo& face : *faces) {
        print.names.push_back(face.names);
    }
    return print;
}

// A block with two top edges rounded with variable radii:
//
//   width        100 mm
//   depth        50 mm
//   low          3 mm
//   high         8 mm
//   BlockSketch  XY plane: rectangle x 0..width, y 0..depth; lines front
//                (y = 0, running +x), right, back (y = depth, running -x), left
//   Block        extrude 20 mm (new body)
//   Taper        variable-radius fillet of Block:
//                  the front top edge (y = 0, z = 20): low at 0, high at 1
//                  the back top edge (y = depth, z = 20): 2 mm at 0, 4 mm at
//                  0.3, 7 mm at 1 (the profile's loop runs counter-clockwise,
//                  so the back edge runs -x: the kernel's spine runs against
//                  the edge's canonical direction, and uneven stations show
//                  which way they were applied)
//
// Position 0 is at x = 0 on both edges (their canonical direction is +x).
// IDs: width 1, depth 2, low 3, high 4, BlockSketch 5, Block 6, Taper 7.
struct TaperedBlockModel : FaceKindModel {
    static constexpr std::string_view kDocumentId = "7c3f9a52-1e84-4b6d-a0f2-5d9e8b3c1a67";
    static constexpr double kHeight = 20.0;
    ParameterId width, depth, low, high;
    ObjectId blockSketch, block, taper;
    std::array<EntityId, 4> lines{};

    TaperedBlockModel() : FaceKindModel(kDocumentId, "TaperedBlock") {
        using namespace bettercad::literals;
        using namespace bettercad::features;
        width = doc.createParameter("width", 100_mm, units::mm).value();
        depth = doc.createParameter("depth", 50_mm, units::mm).value();
        low = doc.createParameter("low", 3_mm, units::mm).value();
        high = doc.createParameter("high", 8_mm, units::mm).value();
        auto profile = std::make_unique<sketch::Sketch>("BlockSketch");
        lines = addSizedRectangle(*profile, 0.0, 0.0, 100.0, 50.0, width, depth);
        blockSketch = doc.addObject(std::move(profile)).value();
        block = add(ExtrudeFeature::create("Block", {.profile = sketchOf(blockSketch), .depth = 20_mm}));
        taper = add(VariableFilletFeature::create("Taper", definition(50.0)));
    }

    static geometry::EdgeSignature topEdge(double y) {
        return geometry::lineSignature(Point3D{Length{}, y * units::mm, kHeight * units::mm},
                                       Direction3D::unitX());
    }

    /// Taper's definition for a block @p d deep.
    [[nodiscard]] features::VariableFilletDefinition definition(double d) const {
        using namespace bettercad::literals;
        return {.target = featureOf(block),
                .edges = {{.edge = topEdge(0.0),
                           .stations = {{.position = 0.0, .radius = 3_mm, .radiusParameter = low},
                                        {.position = 1.0, .radius = 8_mm, .radiusParameter = high}}},
                          {.edge = topEdge(d),
                           .stations = {{.position = 0.0, .radius = 2_mm},
                                        {.position = 0.3, .radius = 4_mm},
                                        {.position = 1.0, .radius = 7_mm}}}}};
    }

    static Stations backStations() { return {{0.0, 2.0}, {0.3, 4.0}, {1.0, 7.0}}; }

    static RoundedEdge frontEdge(double w, double lo, double hi) {
        return boxEdge({0, 0, kHeight}, {1, 0, 0}, w, {0, kHalfRoot2, -kHalfRoot2}, {{0.0, lo}, {1.0, hi}});
    }
    static RoundedEdge backEdge(double w, double d) {
        return boxEdge({0, d, kHeight}, {1, 0, 0}, w, {0, -kHalfRoot2, -kHalfRoot2}, backStations());
    }

    /// The block and its rounded edges.
    static ExpectedShell shape(double w, double d, double lo, double hi) {
        return {{boxPart({0, 0, 0}, {w, d, kHeight}), frontEdge(w, lo, hi).part(), backEdge(w, d).part()},
                {0, 0, 0},
                {w, d, kHeight}};
    }
    /// The top face (Block's end cap) less both strips.
    static double topArea(double w, double d, double lo, double hi) {
        return w * d - frontEdge(w, lo, hi).strip() - backEdge(w, d).strip();
    }
};

} // namespace bettercad::test
