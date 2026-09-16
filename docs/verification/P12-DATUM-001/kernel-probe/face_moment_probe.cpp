// Kernel probe (evidence, not part of the build): whether the kernel's
// per-face volume integrators can be summed face by face about one apex, as
// Body::massProperties() does since P12-DATUM-001 for bodies with faces on
// surfaces of extrusion or revolution.
//
// For every face and an apex P (the mean of the shape's vertices):
//   ada  BRepGProp_Vinert, location P, Perform(face[, domain], eps 1e-10)
//   gk   BRepGProp_VinertGK, location P, Perform(face[, domain], eps 1e-10,
//        centre of gravity on), faces loaded with spans
// both give the face's signed cone volume m and first moment about P,
// m (G - P). The kernel documents the cone for Vinert; the probe shows
// whether GK integrates the same quantities (per face, not only in total),
// whether the per-face sums reproduce the whole-shape results, and how
// accurate and fast both are on curved faces that were cut (trimmed), which
// BetterCAD's own rectangle integration does not take.
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       face_moment_probe.cpp -L<deps>/lib -lTKBO -lTKOffset -lTKPrim
//       -lTKTopAlgo -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG2d -lTKG3d
//       -lTKMath -lTKernel
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Domain.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepGProp_Vinert.hxx>
#include <BRepGProp_VinertGK.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Geom_BSplineCurve.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace {

const double pi = std::numbers::pi;

using Clock = std::chrono::steady_clock;

struct Sum {
    double mass = 0.0;
    gp_XYZ moment{0, 0, 0};
    double seconds = 0.0;
};

gp_Pnt meanVertex(const TopoDS_Shape& shape) {
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    gp_XYZ sum(0, 0, 0);
    for (int i = 1; i <= vertices.Extent(); ++i) {
        sum += BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))).XYZ();
    }
    return gp_Pnt(sum / vertices.Extent());
}

double largest(const gp_XYZ& v) {
    return std::max({std::abs(v.X()), std::abs(v.Y()), std::abs(v.Z())});
}

void measure(const std::string& name, const TopoDS_Shape& shape, double expected) {
    const gp_Pnt apex = meanVertex(shape);
    std::printf("%s (V = %.17g), apex (%.6g, %.6g, %.6g)\n", name.c_str(), expected, apex.X(), apex.Y(), apex.Z());
    Sum ada;
    Sum gk;
    int index = 0;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next(), ++index) {
        const TopoDS_Face& face = TopoDS::Face(it.Current());
        const bool natural = face.NbChildren() == 0;

        auto t0 = Clock::now();
        BRepGProp_Face plain(face);
        BRepGProp_Vinert a;
        a.SetLocation(apex);
        if (natural) {
            a.Perform(plain, 1e-10);
        } else {
            BRepGProp_Domain domain(face);
            a.Perform(plain, domain, 1e-10);
        }
        auto t1 = Clock::now();
        BRepGProp_Face spanned(face, /*IsUseSpan=*/true);
        BRepGProp_VinertGK g;
        g.SetLocation(apex);
        if (natural) {
            g.Perform(spanned, 1e-10, /*CGFlag=*/true, /*IFlag=*/false);
        } else {
            BRepGProp_Domain domain(face);
            g.Perform(spanned, domain, 1e-10, /*CGFlag=*/true, /*IFlag=*/false);
        }
        auto t2 = Clock::now();

        const gp_XYZ ma = a.Mass() * (a.CentreOfMass().XYZ() - apex.XYZ());
        const gp_XYZ mg = g.Mass() * (g.CentreOfMass().XYZ() - apex.XYZ());
        const double ta = std::chrono::duration<double>(t1 - t0).count();
        const double tg = std::chrono::duration<double>(t2 - t1).count();
        std::printf("  face %2d natural %d  ada m %22.17g M (%.10g, %.10g, %.10g) %.3fs\n", index, natural ? 1 : 0,
                    a.Mass(), ma.X(), ma.Y(), ma.Z(), ta);
        std::printf("                     gk  m %22.17g M (%.10g, %.10g, %.10g) %.3fs  |dm| %.2e |dM| %.2e\n",
                    g.Mass(), mg.X(), mg.Y(), mg.Z(), tg, std::abs(a.Mass() - g.Mass()), largest(ma - mg));
        ada.mass += a.Mass();
        ada.moment += ma;
        ada.seconds += ta;
        gk.mass += g.Mass();
        gk.moment += mg;
        gk.seconds += tg;
    }

    GProp_GProps whole;
    BRepGProp::VolumeProperties(shape, whole, 1e-10, true);
    GProp_GProps wholeGk;
    BRepGProp::VolumePropertiesGK(shape, wholeGk, 1e-10, true, true, true);
    const gp_XYZ ga = apex.XYZ() + ada.moment / ada.mass;
    const gp_XYZ gg = apex.XYZ() + gk.moment / gk.mass;
    std::printf("  face sums  ada V rel %.2e  G (%.12g, %.12g, %.12g)  %.3fs\n", std::abs(ada.mass / expected - 1),
                ga.X(), ga.Y(), ga.Z(), ada.seconds);
    std::printf("             gk  V rel %.2e  G (%.12g, %.12g, %.12g)  %.3fs\n", std::abs(gk.mass / expected - 1),
                gg.X(), gg.Y(), gg.Z(), gk.seconds);
    std::printf("  whole      ada V rel %.2e  |G - sum G| %.2e;  gk V rel %.2e  |G - sum G| %.2e\n\n",
                std::abs(whole.Mass() / expected - 1), largest(whole.CentreOfMass().XYZ() - ga),
                std::abs(wholeGk.Mass() / expected - 1), largest(wholeGk.CentreOfMass().XYZ() - gg));
}

TopoDS_Face planarFace(const TopoDS_Wire& wire) {
    return BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0))), wire, true)
        .Face();
}

TopoDS_Face planarFace(const TopoDS_Edge& edge) {
    return planarFace(BRepBuilderAPI_MakeWire(edge).Wire());
}

// The periodic quadratic spline on the square (x0, y0) + [0, side]^2: area
// 5/6 side^2 (Archimedes), symmetric about both mid-lines.
TopoDS_Edge periodicSquare(double x0, double y0, double side) {
    NCollection_Array1<gp_Pnt> poles(1, 4);
    poles.SetValue(1, gp_Pnt(x0, y0, 0));
    poles.SetValue(2, gp_Pnt(x0 + side, y0, 0));
    poles.SetValue(3, gp_Pnt(x0 + side, y0 + side, 0));
    poles.SetValue(4, gp_Pnt(x0, y0 + side, 0));
    NCollection_Array1<double> knots(1, 5);
    NCollection_Array1<int> mults(1, 5);
    for (int i = 1; i <= 5; ++i) {
        knots.SetValue(i, i - 1.0);
        mults.SetValue(i, 1);
    }
    return BRepBuilderAPI_MakeEdge(occ::handle<Geom_BSplineCurve>(
                                       new Geom_BSplineCurve(poles, knots, mults, 2, true)))
        .Edge();
}

// Volume of the ring swept by the ellipse (centre 50 from the Y axis, semi-axes
// 10 radial and 6 axial) where z > 30: each profile point at radius r > 30
// sweeps the arc 2 acos(30 / r). With r = 50 + 10 cos t the integrand is
// smooth; composite 8-point Gauss-Legendre on 256 pieces.
double ringBeyond30() {
    static const double nodes[8] = {-0.9602898564975363, -0.7966664774136267, -0.5255324099163290,
                                    -0.1834346424956498, 0.1834346424956498,  0.5255324099163290,
                                    0.7966664774136267,  0.9602898564975363};
    static const double weights[8] = {0.1012285362903763, 0.2223810344533745, 0.3137066458778873,
                                      0.3626837833783620, 0.3626837833783620, 0.3137066458778873,
                                      0.2223810344533745, 0.1012285362903763};
    const int pieces = 256;
    const double h = pi / pieces;
    double sum = 0.0;
    for (int k = 0; k < pieces; ++k) {
        for (int i = 0; i < 8; ++i) {
            const double t = (k + 0.5 + 0.5 * nodes[i]) * h;
            const double r = 50.0 + 10.0 * std::cos(t);
            // dA = (radial width) (axial height): 10 sin t dt * 12 sin t.
            sum += 0.5 * h * weights[i] * 2.0 * std::acos(30.0 / r) * r * 120.0 * std::sin(t) * std::sin(t);
        }
    }
    return sum;
}

} // namespace

int main() {
    std::printf("P12-DATUM-001 face moment probe (OCCT 8.0.1)\n\n");

    // A 60 x 40 x 20 block with a radius-8 hole through it along Z at (20, 20).
    const TopoDS_Shape block = BRepPrimAPI_MakeBox(60, 40, 20).Shape();
    const TopoDS_Shape pin = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20, 20, -1), gp_Dir(0, 0, 1)), 8, 22).Shape();
    measure("block with a hole", BRepAlgoAPI_Cut(block, pin).Shape(), 60 * 40 * 20 - pi * 64 * 20);

    // An elliptic prism a = 30, b = 10, h = 20, centred on the origin, with
    // x > 15 cut away: its side face no longer covers its parameter
    // rectangle. Removed segment area: a b (acos(c/a) - (c/a) sqrt(1 - c^2/a^2)).
    const gp_Elips ellipse(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 30.0, 10.0);
    const TopoDS_Shape prism =
        BRepPrimAPI_MakePrism(planarFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(ellipse).Edge()).Wire()),
                              gp_Vec(0, 0, 20))
            .Shape();
    measure("elliptic prism", prism, pi * 300 * 20);
    const TopoDS_Shape slab = BRepPrimAPI_MakeBox(gp_Pnt(15, -20, -1), 30, 40, 22).Shape();
    const double segment = 300.0 * (std::acos(0.5) - 0.5 * std::sqrt(0.75));
    measure("elliptic prism cut at x = 15", BRepAlgoAPI_Cut(prism, slab).Shape(), (pi * 300 - segment) * 20);

    // The spline square prism (area 3000, h = 10), whole and cut at y = 30:
    // half of it.
    const TopoDS_Shape splinePrism =
        BRepPrimAPI_MakePrism(planarFace(periodicSquare(0, 0, 60)), gp_Vec(0, 0, 10)).Shape();
    measure("spline prism", splinePrism, 30000);
    measure("spline prism cut at y = 30",
            BRepAlgoAPI_Cut(splinePrism, BRepPrimAPI_MakeBox(gp_Pnt(-1, 30, -1), 62, 31, 12).Shape()).Shape(),
            15000);

    // The revolved ellipse (Pappus 2 pi 50 * pi 60) with z > 30 cut away:
    // its revolution face is trimmed by a line that is no parameter line.
    const gp_Elips ring(gp_Ax2(gp_Pnt(50, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), 10.0, 6.0);
    const TopoDS_Shape torus = BRepPrimAPI_MakeRevol(planarFace(BRepBuilderAPI_MakeEdge(ring).Edge()),
                                                     gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)))
                                   .Shape();
    measure("revolved ellipse cut at z = 30",
            BRepAlgoAPI_Cut(torus, BRepPrimAPI_MakeBox(gp_Pnt(-70, -10, 30), 140, 20, 40).Shape()).Shape(),
            2 * pi * 50 * pi * 60 - ringBeyond30());
    return 0;
}
