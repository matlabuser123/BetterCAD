// Kernel probe (evidence, not part of the build): what OCCT 8.0.1's
// BRepFilletAPI_MakeFillet offers for the P12-FEAT-006 capabilities —
// variable-radius fillets, setbacks and corner transitions — and how its
// results compare with reference volumes.
//
// The fillet builder's public interface (BRepFilletAPI_MakeFillet.hxx) takes
// a radius per contour as a constant, two end values (linear), a law, or
// (parameter, radius) pairs, and a cross-section kind (SetFilletShape:
// rational, quasi-angular, polynomial). It has no setback distances and no
// choice of corner blend: where filleted edges meet, ChFi3d computes the
// corner itself (ChFi3d_Builder::PerformTwoCorner, PerformThreeCorner,
// PerformMoreThreeCorner are implementation steps, not options).
//
//  A. A 100 x 60 x 40 box, one 100 mm edge filleted r 5: the removed volume
//     is (1 - pi/4) r^2 L exactly.
//  B. The same edge from r 3 to r 5.5, 8 and 12 with Add(R1, R2, E), which
//     the header calls linear: compared with what a linear radius would
//     remove, (1 - pi/4) L (R1^2 + R1 R2 + R2^2) / 3. The differences
//     (0.7 to 2.6 %) are not a property of the sections: the radius law
//     OCCT builds is not linear (variable_radius_probe.cpp matches it to
//     1e-14 mm).
//  C. The same with (u, r) pairs 3, 8, 3 at the ends and the middle.
//  D. The three edges at one vertex filleted r 5 (the kernel's own corner).
//  E. D with the three radii 3, 5, 8.
//  F. D with the three edges from r 3 at the corner to r 8 away from it.
//  G. A with each fillet shape (rational, quasi-angular, polynomial).
//  H. B with the end radius too large for the face (r 45 on a 40 mm face).
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       fillet_probe.cpp -L<deps>/lib -lTKFillet -lTKBool -lTKBO
//       -lTKShHealing -lTKPrim -lTKTopAlgo -lTKGeomAlgo -lTKBRep
//       -lTKGeomBase -lTKG2d -lTKG3d -lTKMath -lTKernel
#include <BRepAlgoAPI_Check.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <ChFi3d_FilletShape.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_Array1.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pnt2d.hxx>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr double pi = M_PI;
constexpr double none = std::numeric_limits<double>::quiet_NaN();
constexpr double kBox = 100.0 * 60.0 * 40.0;

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

/// The box edge from @p a to @p b (either way round).
TopoDS_Edge edgeBetween(const TopoDS_Shape& shape, const gp_Pnt& a, const gp_Pnt& b) {
    for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(it.Current());
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(edge, v1, v2);
        const gp_Pnt p1 = BRep_Tool::Pnt(v1);
        const gp_Pnt p2 = BRep_Tool::Pnt(v2);
        if ((p1.Distance(a) < 1e-9 && p2.Distance(b) < 1e-9) || (p1.Distance(b) < 1e-9 && p2.Distance(a) < 1e-9)) {
            return edge;
        }
    }
    return {};
}

/// Whether the edge runs from @p a (its first vertex) towards @p b.
bool startsAt(const TopoDS_Edge& edge, const gp_Pnt& a) {
    TopoDS_Vertex v1, v2;
    TopExp::Vertices(edge, v1, v2, true);
    return BRep_Tool::Pnt(v1).Distance(a) < 1e-9;
}

void run(const char* name, const std::function<void(BRepFilletAPI_MakeFillet&, const TopoDS_Shape&)>& add,
         double removed, ChFi3d_FilletShape shape = ChFi3d_Rational) {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 60.0, 40.0).Shape();
    const auto start = std::chrono::steady_clock::now();
    std::printf("%s: ", name);
    try {
        BRepFilletAPI_MakeFillet fillet(box, shape);
        add(fillet, box);
        fillet.Build();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (!fillet.IsDone()) {
            std::printf("NOT DONE (faulty contours %d, faulty vertices %d), %.3f s\n", fillet.NbFaultyContours(),
                        fillet.NbFaultyVertices(), seconds);
            return;
        }
        const TopoDS_Shape& result = fillet.Shape();
        const bool valid = BRepCheck_Analyzer(result).IsValid();
        const bool selfIntersects = !BRepAlgoAPI_Check(result, false, true).IsValid();
        const double cut = kBox - volumeOf(result);
        std::printf("valid %d self-intersects %d surfaces %d removed %.9f", valid ? 1 : 0, selfIntersects ? 1 : 0,
                    fillet.NbSurfaces(), cut);
        if (!std::isnan(removed)) {
            std::printf(" reference %.9f rel %.2e", removed, std::abs(cut - removed) / removed);
        }
        std::printf(", %.3f s\n", seconds);
    } catch (const Standard_Failure& failure) {
        std::printf("EXCEPTION %s: %s\n", failure.ExceptionType(), failure.what());
    }
}

} // namespace

int main() {
    const double k = 1.0 - pi / 4.0;
    const gp_Pnt o(0, 0, 0);
    const gp_Pnt x(100, 0, 0);
    const gp_Pnt y(0, 60, 0);
    const gp_Pnt z(0, 0, 40);
    std::printf("OCCT fillet probe\n\n");

    run("A. one edge r 5", [&](BRepFilletAPI_MakeFillet& f, const TopoDS_Shape& s) { f.Add(5.0, edgeBetween(s, o, x)); },
        k * 25.0 * 100.0);
    for (const double r2 : {5.5, 8.0, 12.0}) {
        char name[64];
        std::snprintf(name, sizeof name, "B. one edge r 3 to r %.1f (linear)", r2);
        run(name,
            [&](BRepFilletAPI_MakeFillet& f, const TopoDS_Shape& s) {
                const TopoDS_Edge e = edgeBetween(s, o, x);
                // R1 at the edge's first vertex.
                if (startsAt(e, o)) {
                    f.Add(3.0, r2, e);
                } else {
                    f.Add(r2, 3.0, e);
                }
            },
            k * 100.0 * (9.0 + 3.0 * r2 + r2 * r2) / 3.0);
    }
    run("C. one edge r 3, 8, 3 at u = 0, 0.5, 1",
        [&](BRepFilletAPI_MakeFillet& f, const TopoDS_Shape& s) {
            const TopoDS_Edge e = edgeBetween(s, o, x);
            double first = 0.0;
            double last = 0.0;
            BRep_Tool::Range(e, first, last);
            NCollection_Array1<gp_Pnt2d> points(1, 3);
            points.SetValue(1, gp_Pnt2d(first, 3.0));
            points.SetValue(2, gp_Pnt2d((first + last) / 2.0, 8.0));
            points.SetValue(3, gp_Pnt2d(last, 3.0));
            f.Add(points, e);
        },
        none);
    const auto corner = [&](double rx, double ry, double rz) {
        return [=, &o, &x, &y, &z](BRepFilletAPI_MakeFillet& f, const TopoDS_Shape& s) {
            f.Add(rx, edgeBetween(s, o, x));
            f.Add(ry, edgeBetween(s, o, y));
            f.Add(rz, edgeBetween(s, o, z));
        };
    };
    // Three edges at a vertex, r each: the edge strips less their overlaps
    // near the corner, plus the corner's sphere octant region: removed =
    // k r^2 (Lx + Ly + Lz) - 3 k r^3 + r^3 (1 - pi/6) ... written out:
    // the cube [0, r]^3 at the corner keeps its sphere octant, so it loses
    // r^3 - pi r^3 / 6; each edge strip outside that cube loses k r^2 (L - r).
    const auto cornerRemoved = [&](double r) {
        return k * r * r * ((100.0 - r) + (60.0 - r) + (40.0 - r)) + r * r * r * (1.0 - pi / 6.0);
    };
    run("D. three edges at a vertex, r 5", corner(5.0, 5.0, 5.0), cornerRemoved(5.0));
    run("E. three edges at a vertex, r 3, 5, 8", corner(3.0, 5.0, 8.0), none);
    run("F. three edges at a vertex, r 3 at the vertex to 8",
        [&](BRepFilletAPI_MakeFillet& f, const TopoDS_Shape& s) {
            for (const gp_Pnt& end : {x, y, z}) {
                const TopoDS_Edge e = edgeBetween(s, o, end);
                if (startsAt(e, o)) {
                    f.Add(3.0, 8.0, e);
                } else {
                    f.Add(8.0, 3.0, e);
                }
            }
        },
        none);
    for (const auto& [shape, label] :
         {std::pair{ChFi3d_Rational, "rational"}, {ChFi3d_QuasiAngular, "quasi-angular"},
          {ChFi3d_Polynomial, "polynomial"}}) {
        char name[64];
        std::snprintf(name, sizeof name, "G. one edge r 5, %s", label);
        run(name, [&](BRepFilletAPI_MakeFillet& f, const TopoDS_Shape& s) { f.Add(5.0, edgeBetween(s, o, x)); },
            k * 25.0 * 100.0, shape);
    }
    run("H. one edge r 3 to r 45 (too large for the 40 mm face)",
        [&](BRepFilletAPI_MakeFillet& f, const TopoDS_Shape& s) {
            const TopoDS_Edge e = edgeBetween(s, o, x);
            if (startsAt(e, o)) {
                f.Add(3.0, 45.0, e);
            } else {
                f.Add(45.0, 3.0, e);
            }
        },
        none);
    return 0;
}
