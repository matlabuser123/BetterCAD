// Kernel probe (evidence, not part of the build): the accuracy and the reference
// frame of GProp_GProps::MatrixOfInertia() for the SECOND moments of a solid's
// volume, which P15-MASS-001 derives every inertia tensor from.
//
// Three questions the kernel's own header does not answer:
//
//   1. REFERENCE POINT. MatrixOfInertia() is documented as being "in the central
//      coordinate system (G, Gx, Gy, Gz), where G is the centre of mass", while
//      the GProp_GProps(gp_Pnt) constructor describes its argument as the
//      "reference point of the system used for inertia accumulation". If the
//      location moved the result's frame, a caller could ask for any frame
//      directly; if it does not, only the centroidal tensor is available and every
//      other frame must come from Huygens. The probe reports the matrix for a body
//      at the origin, the same body translated far away, and the same body with
//      several different SystemLocations.
//
//   2. SIGN. The same header calls the off-diagonals "the products of inertia" --
//      the name of the POSITIVE convention -- while laying them out as an inertia
//      matrix, which is the negated one. Probed on a body whose centroidal
//      products are all non-zero.
//
//   3. ACCURACY. The kernel's volume is exact to ~5e-15 relative on planar,
//      cylindrical and spherical faces (P3). Its second moments need a higher
//      degree of quadrature over the same faces, so they need not be. The probe
//      compares against closed forms over four decades of size, and reports the
//      xx - yy residual for bodies where symmetry makes those exactly equal --
//      a self-check that needs no closed form at all.
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       second_moment_probe.cpp -L<deps>/lib -lTKBO -lTKPrim -lTKTopAlgo
//       -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG2d -lTKG3d -lTKMath -lTKernel
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Mat.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>

namespace {

constexpr double pi = std::numbers::pi;
#ifndef PROBE_EPS
#define PROBE_EPS 1e-10
#endif
constexpr double kEps = PROBE_EPS;

struct Moments {
    double volume = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double cz = 0.0;
    double xx = 0.0;
    double yy = 0.0;
    double zz = 0.0;
    double xy = 0.0;
    double xz = 0.0;
    double yz = 0.0;
};

Moments measure(const TopoDS_Shape& shape, const gp_Pnt& location) {
    GProp_GProps props(location);
    BRepGProp::VolumeProperties(shape, props, kEps, /*OnlyClosed=*/true);
    const gp_Mat& m = props.MatrixOfInertia();
    const gp_Pnt g = props.CentreOfMass();
    return Moments{props.Mass(),      g.X(),           g.Y(),           g.Z(),
                   m.Value(1, 1),     m.Value(2, 2),   m.Value(3, 3),
                   m.Value(1, 2),     m.Value(1, 3),   m.Value(2, 3)};
}

Moments measure(const TopoDS_Shape& shape) {
    return measure(shape, gp_Pnt(0.0, 0.0, 0.0));
}

double relative(double measured, double exact) {
    if (exact == 0.0) {
        return std::abs(measured);
    }
    return std::abs(measured - exact) / std::abs(exact);
}

void reportDiagonal(const std::string& label, const Moments& got, double xx, double yy, double zz) {
    std::printf("  %-34s xx %.17g  rel %.2e\n", label.c_str(), got.xx, relative(got.xx, xx));
    std::printf("  %-34s yy %.17g  rel %.2e\n", "", got.yy, relative(got.yy, yy));
    std::printf("  %-34s zz %.17g  rel %.2e\n", "", got.zz, relative(got.zz, zz));
    std::printf("  %-34s off xy %.3e  xz %.3e  yz %.3e\n", "", got.xy, got.xz, got.yz);
}

} // namespace

int main() {
    std::printf("P15-MASS-001 second moment probe (OCCT, eps %g)\n\n", kEps);

    // ---- 1. REFERENCE POINT -------------------------------------------------
    // A 2 x 3 x 5 box with a corner at the origin. Its centroidal moments are
    // V(b^2+c^2)/12 = 85, 72.5, 32.5 and its moments about the ORIGIN are
    // V(b^2+c^2)/3 = 340, 290, 130, with products -45, -75, -112.5. Which set
    // comes back says which frame the matrix is in, and the two differ by a
    // factor of four -- not by rounding.
    std::printf("REFERENCE POINT -- 2 x 3 x 5 box, corner at the origin\n");
    std::printf("  centroidal would be 85, 72.5, 32.5 with products 0, 0, 0\n");
    std::printf("  about the origin    340, 290, 130  with products -45, -75, -112.5\n");
    {
        const TopoDS_Shape box = BRepPrimAPI_MakeBox(2.0, 3.0, 5.0).Shape();
        for (const gp_Pnt& location :
             {gp_Pnt(0, 0, 0), gp_Pnt(1, 1.5, 2.5), gp_Pnt(100, 100, 100), gp_Pnt(-7, 11, 0.5)}) {
            const Moments got = measure(box, location);
            std::printf("  location (%6.1f,%6.1f,%6.1f)  V %.15g  G (%g, %g, %g)\n", location.X(),
                        location.Y(), location.Z(), got.volume, got.cx, got.cy, got.cz);
            std::printf("      xx %.15g  yy %.15g  zz %.15g\n", got.xx, got.yy, got.zz);
            std::printf("      xy %.3e  xz %.3e  yz %.3e\n", got.xy, got.xz, got.yz);
        }
        // Translated far away. If the frame is centroidal the numbers do not move;
        // if it is the absolute origin they grow by V x 2 x 1000^2 = 6e7.
        const TopoDS_Shape moved =
            BRepPrimAPI_MakeBox(gp_Pnt(1000.0, 1000.0, 1000.0), 2.0, 3.0, 5.0).Shape();
        const Moments got = measure(moved);
        std::printf("  same box at (1000,1000,1000), location origin\n");
        std::printf("      xx %.15g  yy %.15g  zz %.15g\n", got.xx, got.yy, got.zz);
        std::printf("      xy %.3e  xz %.3e  yz %.3e\n", got.xy, got.xz, got.yz);
    }

    // ---- 2. SIGN ------------------------------------------------------------
    // Three fused boxes, sharing whole faces, symmetric about no plane through
    // their centroid. Hand-computed: V = 16, centroid (1.25, 1.5, 1.25),
    // integral xy dV = -6, integral xz dV = -9, integral yz dV = +6 about it.
    // So the tensor convention gives +6, +9, -6 and the positive-products
    // convention gives -6, -9, +6.
    std::printf("\nSIGN -- three fused boxes, centroidal products all non-zero\n");
    std::printf("  tensor convention (negated)  xy +6  xz +9  yz -6\n");
    std::printf("  positive products            xy -6  xz -9  yz +6\n");
    {
        const TopoDS_Shape b1 = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 4.0, 2.0, 1.0).Shape();
        const TopoDS_Shape b2 = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 1), 1.0, 2.0, 2.0).Shape();
        const TopoDS_Shape b3 = BRepPrimAPI_MakeBox(gp_Pnt(0, 2, 1), 1.0, 2.0, 2.0).Shape();
        const TopoDS_Shape twelve = BRepAlgoAPI_Fuse(b1, b2).Shape();
        const TopoDS_Shape staircase = BRepAlgoAPI_Fuse(twelve, b3).Shape();
        const Moments got = measure(staircase);
        std::printf("  V %.15g  G (%.15g, %.15g, %.15g)\n", got.volume, got.cx, got.cy, got.cz);
        std::printf("      xx %.15g (89/3 = %.15g)  rel %.2e\n", got.xx, 89.0 / 3.0,
                    relative(got.xx, 89.0 / 3.0));
        std::printf("      yy %.15g (98/3 = %.15g)  rel %.2e\n", got.yy, 98.0 / 3.0,
                    relative(got.yy, 98.0 / 3.0));
        std::printf("      zz %.15g (113/3 = %.15g) rel %.2e\n", got.zz, 113.0 / 3.0,
                    relative(got.zz, 113.0 / 3.0));
        std::printf("      xy %.15g  rel-to-+6 %.2e\n", got.xy, relative(got.xy, 6.0));
        std::printf("      xz %.15g  rel-to-+9 %.2e\n", got.xz, relative(got.xz, 9.0));
        std::printf("      yz %.15g  rel-to--6 %.2e\n", got.yz, relative(got.yz, -6.0));
    }

    // ---- 3. ACCURACY -------------------------------------------------------
    std::printf("\nACCURACY -- boxes, over four decades\n");
    for (const double s : {0.1, 1.0, 10.0, 100.0, 1000.0}) {
        const double a = 2.0 * s;
        const double b = 3.0 * s;
        const double c = 5.0 * s;
        const double v = a * b * c;
        const TopoDS_Shape box = BRepPrimAPI_MakeBox(a, b, c).Shape();
        char label[64];
        std::snprintf(label, sizeof label, "box %g x %g x %g", a, b, c);
        reportDiagonal(label, measure(box), v * (b * b + c * c) / 12.0,
                       v * (a * a + c * c) / 12.0, v * (a * a + b * b) / 12.0);
    }

    std::printf("\nACCURACY -- cylinders. xx and yy are EXACTLY equal by symmetry,\n");
    std::printf("so |xx - yy| / xx is an error estimate that needs no closed form.\n");
    for (const auto& [r, h] : {std::pair{2.0, 6.0}, std::pair{10.0, 30.0}, std::pair{0.5, 100.0},
                               std::pair{250.0, 5.0}, std::pair{1.0, 1.0}}) {
        const double v = pi * r * r * h;
        const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(r, h).Shape();
        const Moments got = measure(cylinder);
        const double transverse = v * (r * r / 4.0 + h * h / 12.0);
        const double axial = v * r * r / 2.0;
        std::printf("  r %-6g h %-6g  V rel %.2e\n", r, h, relative(got.volume, v));
        std::printf("      xx rel %.2e   yy rel %.2e   zz rel %.2e   |xx-yy|/xx %.2e\n",
                    relative(got.xx, transverse), relative(got.yy, transverse),
                    relative(got.zz, axial), std::abs(got.xx - got.yy) / std::abs(got.xx));
        std::printf("      off xy %.3e  xz %.3e  yz %.3e\n", got.xy, got.xz, got.yz);
    }

    std::printf("\nACCURACY -- spheres. xx = yy = zz = 8 pi r^5 / 15, and the three\n");
    std::printf("must be exactly equal by symmetry.\n");
    for (const double r : {0.5, 3.0, 25.0, 500.0}) {
        const TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(r).Shape();
        const Moments got = measure(sphere);
        const double exact = 8.0 * pi * std::pow(r, 5) / 15.0;
        const double spread = (std::max({got.xx, got.yy, got.zz}) -
                               std::min({got.xx, got.yy, got.zz})) / std::abs(got.xx);
        std::printf("  r %-6g  V rel %.2e   xx rel %.2e  yy rel %.2e  zz rel %.2e  spread %.2e\n",
                    r, relative(got.volume, 4.0 / 3.0 * pi * r * r * r), relative(got.xx, exact),
                    relative(got.yy, exact), relative(got.zz, exact), spread);
        std::printf("      off xy %.3e  xz %.3e  yz %.3e\n", got.xy, got.xz, got.yz);
    }

    std::printf("\nEPS SENSITIVITY -- does a tighter request help the cylinder?\n");
    {
        const double r = 2.0;
        const double h = 6.0;
        const double v = pi * r * r * h;
        const double transverse = v * (r * r / 4.0 + h * h / 12.0);
        const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(r, h).Shape();
        for (const double eps : {1e-6, 1e-8, 1e-10, 1e-12, 1e-14}) {
            GProp_GProps props;
            BRepGProp::VolumeProperties(cylinder, props, eps, /*OnlyClosed=*/true);
            const gp_Mat& m = props.MatrixOfInertia();
            std::printf("  eps %-8g xx rel %.2e  yy rel %.2e  |xx-yy|/xx %.2e\n", eps,
                        relative(m.Value(1, 1), transverse), relative(m.Value(2, 2), transverse),
                        std::abs(m.Value(1, 1) - m.Value(2, 2)) / std::abs(m.Value(1, 1)));
        }
    }
    return 0;
}
