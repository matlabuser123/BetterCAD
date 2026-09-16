// Kernel probe for P12-SKETCH-002 (evidence, not part of the build).
// Raw OCCT 8.0.1: how ellipse and B-spline edges come out of planar faces,
// prisms and revolutions, and how accurately BRepGProp integrates them. It
// decided the B-spline representation and the mass-property settings behind
// makePrism()/makeRevolution() for curved profiles.
//
//   curve_kernel_probe
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       curve_kernel_probe.cpp -L<deps>/lib -lTKShHealing -lTKPrim -lTKTopAlgo -lTKGeomAlgo
//       -lTKBRep -lTKGeomBase -lTKG2d -lTKG3d -lTKMath -lTKernel
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <GProp_GProps.hxx>
#include <Geom_BSplineCurve.hxx>
#include <NCollection_Array1.hxx>
#include <ShapeUpgrade_ShapeDivideClosed.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>

#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <string>
#include <tuple>
#include <vector>

namespace {

using Poles = std::vector<std::array<double, 2>>;

const double pi = std::numbers::pi;

occ::handle<Geom_BSplineCurve> curve(const Poles& poles, const std::vector<double>& knots,
                                     const std::vector<int>& mults, int degree, bool periodic) {
    NCollection_Array1<gp_Pnt> p(1, static_cast<int>(poles.size()));
    for (std::size_t i = 0; i < poles.size(); ++i) {
        p.SetValue(static_cast<int>(i) + 1, gp_Pnt(poles[i][0], poles[i][1], 0.0));
    }
    NCollection_Array1<double> k(1, static_cast<int>(knots.size()));
    NCollection_Array1<int> m(1, static_cast<int>(mults.size()));
    for (std::size_t i = 0; i < knots.size(); ++i) {
        k.SetValue(static_cast<int>(i) + 1, knots[i]);
        m.SetValue(static_cast<int>(i) + 1, mults[i]);
    }
    return new Geom_BSplineCurve(p, k, m, degree, periodic);
}

TopoDS_Face face(const TopoDS_Edge& edge) {
    BRepBuilderAPI_MakeWire wire(edge);
    BRepBuilderAPI_MakeFace f(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0))), wire.Wire(), true);
    return f.Face();
}

/// Gauss-Kronrod volume integration, with and without knot spans.
void gk(const char* indent, const TopoDS_Shape& solid, double expected) {
    for (const bool spans : {false, true}) {
        for (const double eps : {1e-6, 1e-10, 1e-12}) {
            GProp_GProps v;
            const double err = BRepGProp::VolumePropertiesGK(solid, v, eps, /*OnlyClosed=*/true, spans);
            std::printf("%svolume GK spans=%d eps %.0e  %.17g rel=%.2e (reported %.2e)\n", indent, spans ? 1 : 0, eps,
                        v.Mass(), std::abs(v.Mass() / expected - 1), err);
        }
    }
}

void report(const std::string& name, const TopoDS_Face& profile, double expectedArea, double height) {
    GProp_GProps area;
    BRepGProp::SurfaceProperties(profile, area);
    GProp_GProps areaTight;
    const double areaError = BRepGProp::SurfaceProperties(profile, areaTight, 1e-10);
    const TopoDS_Shape prism = BRepPrimAPI_MakePrism(profile, gp_Vec(0, 0, height)).Shape();
    GProp_GProps volumeDefault;
    BRepGProp::VolumeProperties(prism, volumeDefault, /*OnlyClosed=*/true);
    GProp_GProps volume10;
    const double error10 = BRepGProp::VolumeProperties(prism, volume10, 1e-10, true);
    GProp_GProps volume13;
    const double error13 = BRepGProp::VolumeProperties(prism, volume13, 1e-13, true);
    const double expected = expectedArea * height;
    std::printf("%-34s faceValid=%d prismValid=%d\n", name.c_str(), BRepCheck_Analyzer(profile).IsValid() ? 1 : 0,
                BRepCheck_Analyzer(prism).IsValid() ? 1 : 0);
    std::printf("  area   Gauss  %.17g rel=%.2e | eps 1e-10 %.17g rel=%.2e (reported %.2e)  expected %.17g\n",
                area.Mass(), std::abs(area.Mass() / expectedArea - 1), areaTight.Mass(),
                std::abs(areaTight.Mass() / expectedArea - 1), areaError, expectedArea);
    std::printf("  volume Gauss  %.17g rel=%.2e\n", volumeDefault.Mass(), std::abs(volumeDefault.Mass() / expected - 1));
    std::printf("  volume 1e-10  %.17g rel=%.2e (reported %.2e)\n", volume10.Mass(),
                std::abs(volume10.Mass() / expected - 1), error10);
    std::printf("  volume 1e-13  %.17g rel=%.2e (reported %.2e)\n", volume13.Mass(),
                std::abs(volume13.Mass() / expected - 1), error13);
    gk("  ", prism, expected);
}

void points(const std::string& name, const occ::handle<Geom_BSplineCurve>& c) {
    std::printf("%s: domain [%g, %g] periodic=%d closed=%d\n", name.c_str(), c->FirstParameter(), c->LastParameter(),
                c->IsPeriodic() ? 1 : 0, c->IsClosed() ? 1 : 0);
    const double first = c->FirstParameter();
    const double last = c->LastParameter();
    for (int i = 0; i <= 8; ++i) {
        const double u = first + (last - first) * i / 8.0;
        const gp_Pnt p = c->Value(u);
        std::printf("  u=%-6g (%.12g, %.12g)\n", u, p.X(), p.Y());
    }
}

void run(const std::string& name, const std::function<void()>& body) {
    try {
        body();
    } catch (const Standard_Failure& failure) {
        std::printf("%s: Standard_Failure %s\n", name.c_str(), failure.what());
    }
}

} // namespace

int main() {
    std::printf("P12-SKETCH-002 curve kernel probe (OCCT 8.0.1)\n\n");
    // The quadratic periodic spline on the 60 mm square passes through the
    // edge midpoints (30, 0), (60, 30), (30, 60), (0, 30) and through
    // (52.5, 7.5) halfway between the first two; it encloses 3000 mm^2.
    const Poles square{{0, 0}, {60, 0}, {60, 60}, {0, 60}};

    run("periodic knots 0..4", [&] {
        const auto c = curve(square, {0, 1, 2, 3, 4}, {1, 1, 1, 1, 1}, 2, true);
        points("A periodic, knots 0..n, mults 1", c);
        report("A periodic", face(BRepBuilderAPI_MakeEdge(c).Edge()), 3000.0, 10.0);
    });
    run("unclamped", [&] {
        // Poles unwrapped (the first `degree` repeated), uniform knots
        // 0..n+2d, domain [d, n+d].
        Poles unwrapped = square;
        unwrapped.push_back(square[0]);
        unwrapped.push_back(square[1]);
        const auto c = curve(unwrapped, {0, 1, 2, 3, 4, 5, 6, 7, 8}, {1, 1, 1, 1, 1, 1, 1, 1, 1}, 2, false);
        points("B unclamped, knots 0..8", c);
        report("B unclamped", face(BRepBuilderAPI_MakeEdge(c).Edge()), 3000.0, 10.0);
    });
    run("bezier pieces", [&] {
        // The same curve as C0-knotted Bezier pieces: midpoint, corner,
        // midpoint, ...
        const Poles pieces{{30, 0}, {60, 0}, {60, 30}, {60, 60}, {30, 60}, {0, 60}, {0, 30}, {0, 0}, {30, 0}};
        const auto c = curve(pieces, {0, 1, 2, 3, 4}, {3, 2, 2, 2, 3}, 2, false);
        points("C Bezier pieces, knots 0..4, interior mults 2", c);
        report("C Bezier pieces", face(BRepBuilderAPI_MakeEdge(c).Edge()), 3000.0, 10.0);
    });
    run("revolved spline", [&] {
        // The square centred 50 mm from the Y axis: V = 2 pi 50 (5/6 400).
        const auto c = curve({{40, 20}, {60, 20}, {60, 40}, {40, 40}}, {0, 1, 2, 3, 4}, {1, 1, 1, 1, 1}, 2, true);
        const TopoDS_Shape revolved =
            BRepPrimAPI_MakeRevol(face(BRepBuilderAPI_MakeEdge(c).Edge()), gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)))
                .Shape();
        const double expected = 2 * pi * 50 * (5.0 * 400.0 / 6.0);
        GProp_GProps v;
        const double err = BRepGProp::VolumeProperties(revolved, v, 1e-10, true);
        std::printf("G revolved spline eps 1e-10: %.17g rel=%.2e (reported %.2e)\n", v.Mass(),
                    std::abs(v.Mass() / expected - 1), err);
        gk("G revolved spline ", revolved, expected);
    });
    run("surface areas and centres", [&] {
        // Lateral area = perimeter x height; the ellipse's perimeter by the
        // arithmetic-geometric mean (13.364893220555254 * 10 for a = 30,
        // b = 10 is 133.64893220555254 mm, cross-checked by quadrature).
        const gp_Elips e(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 30.0, 10.0);
        const TopoDS_Face profile = face(BRepBuilderAPI_MakeEdge(e).Edge());
        const TopoDS_Shape prism = BRepPrimAPI_MakePrism(profile, gp_Vec(0, 0, 20)).Shape();
        const double expected = 2 * pi * 300.0 + 133.64893220555254 * 20.0;
        for (const double eps : {1e-10, 1e-12}) {
            GProp_GProps a;
            const double err = BRepGProp::SurfaceProperties(prism, a, eps);
            std::printf("H ellipse prism area eps %.0e: %.17g rel=%.2e (reported %.2e)\n", eps, a.Mass(),
                        std::abs(a.Mass() / expected - 1), err);
        }
        GProp_GProps g;
        BRepGProp::SurfaceProperties(prism, g);
        std::printf("H ellipse prism area Gauss: %.17g rel=%.2e\n", g.Mass(), std::abs(g.Mass() / expected - 1));

        // The periodic quadratic spline square: perimeter from 4 parabola arcs
        // (M0 (30, 0) -> P1 (60, 0) -> M1 (60, 30)): closed form 4 L.
        const auto c = curve({{0, 0}, {60, 0}, {60, 60}, {0, 60}}, {0, 1, 2, 3, 4}, {1, 1, 1, 1, 1}, 2, true);
        const TopoDS_Shape splinePrism =
            BRepPrimAPI_MakePrism(face(BRepBuilderAPI_MakeEdge(c).Edge()), gp_Vec(0, 0, 10)).Shape();
        // Arc length of B(t) = (1-t)^2 M0 + 2t(1-t) P1 + t^2 M1.
        const double ax = 30 - 120 + 60;
        const double ay = 0 - 0 + 30;
        const double bx = 2 * (60 - 30);
        const double by = 0.0;
        const double A = 4 * (ax * ax + ay * ay);
        const double B = 4 * (ax * bx + ay * by);
        const double C = bx * bx + by * by;
        const auto F = [&](double t) {
            const double q = std::sqrt(A * t * t + B * t + C);
            return (2 * A * t + B) / (4 * A) * q +
                   (4 * A * C - B * B) / (8 * std::pow(A, 1.5)) * std::log(2 * std::sqrt(A) * q + 2 * A * t + B);
        };
        const double perimeter = 4 * (F(1) - F(0));
        const double splineArea = 2 * 3000.0 + perimeter * 10.0;
        for (const double eps : {1e-10, 1e-12}) {
            GProp_GProps a;
            const double err = BRepGProp::SurfaceProperties(splinePrism, a, eps);
            std::printf("I spline prism area eps %.0e: %.17g rel=%.2e (reported %.2e) expected %.17g\n", eps,
                        a.Mass(), std::abs(a.Mass() / splineArea - 1), err, splineArea);
        }
        GProp_GProps gs;
        BRepGProp::SurfaceProperties(splinePrism, gs);
        std::printf("I spline prism area Gauss: %.17g rel=%.2e\n", gs.Mass(), std::abs(gs.Mass() / splineArea - 1));

        // Closed faces split into pieces first (on a copy).
        for (const int pieces : {2, 8, 32}) {
            for (const auto& [label, shape, want] :
                 {std::tuple<const char*, TopoDS_Shape, double>{"ellipse", prism, expected},
                  std::tuple<const char*, TopoDS_Shape, double>{"spline", splinePrism, splineArea}}) {
                ShapeUpgrade_ShapeDivideClosed divide(shape);
                divide.SetNbSplitPoints(pieces);
                divide.Perform();
                const TopoDS_Shape split = divide.Result();
                int faces = 0;
                for (TopExp_Explorer it(split, TopAbs_FACE); it.More(); it.Next()) {
                    ++faces;
                }
                GProp_GProps a;
                const double err = BRepGProp::SurfaceProperties(split, a, 1e-10);
                GProp_GProps g;
                BRepGProp::SurfaceProperties(split, g);
                std::printf("K %s split %d (%d faces): eps 1e-10 %.17g rel=%.2e (reported %.2e) | Gauss %.17g rel=%.2e\n",
                            label, pieces, faces, a.Mass(), std::abs(a.Mass() / want - 1), err, g.Mass(),
                            std::abs(g.Mass() / want - 1));
            }
        }

        // Centre of mass with the Gauss-Kronrod integrator: (30, 30, 5).
        GProp_GProps v;
        BRepGProp::VolumePropertiesGK(splinePrism, v, 1e-10, true, true, /*CGFlag=*/true, /*IFlag=*/false);
        const gp_Pnt cg = v.CentreOfMass();
        std::printf("J spline prism GK centre (%.17g, %.17g, %.17g)\n", cg.X(), cg.Y(), cg.Z());
        GProp_GProps vNo;
        BRepGProp::VolumePropertiesGK(splinePrism, vNo, 1e-10, true, true, /*CGFlag=*/false, /*IFlag=*/false);
        const gp_Pnt cgNo = vNo.CentreOfMass();
        std::printf("J spline prism GK without CGFlag centre (%.17g, %.17g, %.17g) mass %.17g\n", cgNo.X(), cgNo.Y(),
                    cgNo.Z(), vNo.Mass());
    });
    run("ellipse", [&] {
        const gp_Elips e(gp_Ax2(gp_Pnt(5, -7, 0), gp_Dir(0, 0, 1), gp_Dir(std::cos(pi / 6), std::sin(pi / 6), 0)),
                         30.0, 12.0);
        report("D ellipse a=30 b=12", face(BRepBuilderAPI_MakeEdge(e).Edge()), pi * 360.0, 25.0);
        const gp_Elips circleLike(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 10.0, 10.0);
        report("E ellipse a=b=10", face(BRepBuilderAPI_MakeEdge(circleLike).Edge()), pi * 100.0, 25.0);
        // Revolved about the Y axis: V = 2 pi 50 pi a b.
        const gp_Elips ring(gp_Ax2(gp_Pnt(50, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 10.0, 6.0);
        const TopoDS_Shape revolved =
            BRepPrimAPI_MakeRevol(face(BRepBuilderAPI_MakeEdge(ring).Edge()), gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)))
                .Shape();
        const double expected = 2 * pi * 50 * pi * 60;
        for (const double eps : {1e-10, 1e-12, 1e-13}) {
            GProp_GProps v;
            const double err = BRepGProp::VolumeProperties(revolved, v, eps, true);
            std::printf("F revolved ellipse eps %.0e: %.17g rel=%.2e (reported %.2e)\n", eps, v.Mass(),
                        std::abs(v.Mass() / expected - 1), err);
        }
        gk("F revolved ellipse ", revolved, expected);
        GProp_GProps g;
        BRepGProp::VolumeProperties(revolved, g, true);
        std::printf("F revolved ellipse Gauss: %.17g rel=%.2e\n", g.Mass(), std::abs(g.Mass() / expected - 1));
    });
    return 0;
}
