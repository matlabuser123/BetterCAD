// Kernel probe (evidence, not part of the build), after the P12-FEAT-006 scope
// decision: the call sequence BetterCAD's variable-radius fillet uses, and the
// geometry checks it makes on the result, tried on OCCT 8.0.1 before they
// are written into the product.
//
// The sequence: Add(E) with no radius; the contour's edge count and first
// vertex (which says which way the spine runs); SetRadius(UandR, IC, 1) with
// the stations in the spine's direction; Build; GetBounds / GetLaw /
// IsConstant / Radius; the faces Generated from E.
//
// The checks, for a straight edge between two planes with outward normals
// n1, n2 at angle g: in the plane normal to the edge at s the fillet is an
// arc of radius r(s) about
//   C(s) = E(s) - k r(s) (n1 + n2) / (1 + n1.n2),  k = +1 convex, -1 concave,
// touching each plane at r(s) tan(g/2) from the edge, and the section loses
// (convex) or gains (concave) r^2 (tan(g/2) - g/2). r is the clamped cubic
// spline through (-L/2, r_0), the stations, (3L/2, r_n) (variable_radius_probe).
// Convexity is decided from the face orientations: the direction into face 1
// from the edge is n1 x t, with t the edge's tangent as the face's wire runs
// it; the edge is convex when that direction points against n2. ChFi3d's own
// DefineConnectType is printed alongside.
//
// Cases:
//   box      the 12 edges of a 100 x 60 x 40 box: first vertex of each
//            spine, compared with the end first along the canonical direction
//   reversed the edge (0,0,0)-(100,0,0) added reversed: does the spine turn?
//   hex      a vertical edge of a hexagonal prism (faces at 120 deg, g = 60):
//            stations (0,3) (0.5,6) (1,4)
//   concave  the inside vertical edge of an L prism: stations (0,6) (1,2)
//   flat     three equal stations (0,4) (0.4,4) (1,4) on the box edge
//   down     the box edge along z at (100, 60): stations (0,2) (1,7)
//   against  a prism edge built running -y: stations (0,2) (0.3,4) (1,7)
//   all      four stations (0,2) (0.25,5) (0.75,3) (1,6) on a box edge
//
// Built like the other probes (plus nothing new): see fillet-probe.log.
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <ChFi3d.hxx>
#include <GProp_GProps.hxx>
#include <Law_Function.hxx>
#include <NCollection_Array1.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt2d.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double pi = M_PI;

struct ClampedSpline {
    std::vector<double> x, r, m;
    ClampedSpline(std::vector<double> xs, std::vector<double> rs) : x(std::move(xs)), r(std::move(rs)) {
        const std::size_t n = x.size();
        std::vector<double> a(n, 0.0), b(n, 0.0), c(n, 0.0), d(n, 0.0);
        m.assign(n, 0.0);
        const auto h = [&](std::size_t i) { return x[i + 1] - x[i]; };
        const auto s = [&](std::size_t i) { return (r[i + 1] - r[i]) / h(i); };
        b[0] = 2 * h(0);
        c[0] = h(0);
        d[0] = 6 * s(0);
        for (std::size_t i = 1; i + 1 < n; ++i) {
            a[i] = h(i - 1);
            b[i] = 2 * (h(i - 1) + h(i));
            c[i] = h(i);
            d[i] = 6 * (s(i) - s(i - 1));
        }
        a[n - 1] = h(n - 2);
        b[n - 1] = 2 * h(n - 2);
        d[n - 1] = -6 * s(n - 2);
        for (std::size_t i = 1; i < n; ++i) {
            const double w = a[i] / b[i - 1];
            b[i] -= w * c[i - 1];
            d[i] -= w * d[i - 1];
        }
        m[n - 1] = d[n - 1] / b[n - 1];
        for (std::size_t i = n - 1; i-- > 0;) {
            m[i] = (d[i] - c[i] * m[i + 1]) / b[i];
        }
    }
    double operator()(double t) const {
        std::size_t i = 0;
        while (i + 2 < x.size() && t > x[i + 1]) {
            ++i;
        }
        const double hi = x[i + 1] - x[i];
        const double A = (x[i + 1] - t) / hi;
        const double B = (t - x[i]) / hi;
        return A * r[i] + B * r[i + 1] + ((A * A * A - A) * m[i] + (B * B * B - B) * m[i + 1]) * hi * hi / 6.0;
    }
    double integralOfSquare(double from, double to) const {
        static const double node[] = {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563,
                                      0.8611363115940526};
        static const double weight[] = {0.3478548451374538, 0.6521451548625461, 0.6521451548625461,
                                        0.3478548451374538};
        double sum = 0;
        for (std::size_t i = 0; i + 1 < x.size(); ++i) {
            const double lo = std::max(x[i], from);
            const double hi = std::min(x[i + 1], to);
            if (hi <= lo) {
                continue;
            }
            for (int j = 0; j < 4; ++j) {
                const double v = (*this)((lo + hi) / 2 + (hi - lo) / 2 * node[j]);
                sum += weight[j] * (hi - lo) / 2 * v * v;
            }
        }
        return sum;
    }
};

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props, 1e-10, true);
    return props.Mass();
}

TopoDS_Shape prism(const std::vector<gp_Pnt>& corners, double height) {
    BRepBuilderAPI_MakePolygon polygon;
    for (const gp_Pnt& p : corners) {
        polygon.Add(p);
    }
    polygon.Close();
    const TopoDS_Face face = BRepBuilderAPI_MakeFace(polygon.Wire(), true).Face();
    return BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, height)).Shape();
}

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

/// The canonical direction of a line: the first non-zero component positive.
gp_Dir canonical(const gp_Dir& d) {
    const double c[] = {d.X(), d.Y(), d.Z()};
    for (double v : c) {
        if (std::abs(v) > 1e-12) {
            return v > 0 ? d : d.Reversed();
        }
    }
    return d;
}

struct EdgeFrame {
    gp_Pnt start; // the end first along the canonical direction
    gp_Dir t;     // canonical direction
    double length = 0;
    gp_Dir n1, n2;
    gp_Dir in1, in2; // into each face from the edge
    bool convex = true;
    TopoDS_Face f1, f2;
};

/// The edge's frame from its two planar faces.
EdgeFrame frameOf(const TopoDS_Shape& shape, const TopoDS_Edge& edge) {
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> map;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, map);
    std::vector<TopoDS_Face> faces;
    for (const TopoDS_Shape& f : map.FindFromKey(edge)) {
        faces.push_back(TopoDS::Face(f));
    }
    EdgeFrame frame;
    BRepAdaptor_Curve curve(edge);
    const gp_Pnt a = curve.Value(curve.FirstParameter());
    const gp_Pnt b = curve.Value(curve.LastParameter());
    frame.t = canonical(gp_Dir(gp_Vec(a, b)));
    frame.start = gp_Vec(a, b).Dot(gp_Vec(frame.t)) > 0 ? a : b;
    frame.length = a.Distance(b);
    const auto normalAndInward = [&](const TopoDS_Face& face, gp_Dir& normal, gp_Dir& inward) {
        gp_Pln plane = BRepAdaptor_Surface(face).Plane();
        normal = plane.Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) {
            normal.Reverse();
        }
        // The edge as the face's wire runs it: material on the left seen
        // from outside.
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
            if (it.Current().IsSame(edge)) {
                const TopoDS_Edge oriented = TopoDS::Edge(it.Current());
                BRepAdaptor_Curve c(oriented);
                gp_Vec tangent = c.DN(c.FirstParameter(), 1);
                if (oriented.Orientation() == TopAbs_REVERSED) {
                    tangent.Reverse();
                }
                inward = gp_Dir(gp_Vec(normal).Crossed(tangent));
                return;
            }
        }
    };
    frame.f1 = faces.at(0);
    frame.f2 = faces.at(1);
    normalAndInward(frame.f1, frame.n1, frame.in1);
    normalAndInward(frame.f2, frame.n2, frame.in2);
    frame.convex = frame.in1.Dot(frame.n2) < 0;
    return frame;
}

void run(const char* name, const TopoDS_Shape& shape, const TopoDS_Edge& edge,
         const std::vector<std::pair<double, double>>& stations) {
    std::printf("%s\n", name);
    const EdgeFrame f = frameOf(shape, edge);
    const double g = f.n1.Angle(f.n2);
    const ChFiDS_TypeOfConcavity kernelKind = ChFi3d::DefineConnectType(edge, f.f1, f.f2, 1e-10, false);
    std::printf("  length %.9f, face angle %.9f deg, %s (ChFi3d: %s), into faces %s %s\n", f.length,
                g * 180 / pi, f.convex ? "convex" : "concave",
                kernelKind == ChFiDS_Convex ? "convex" : kernelKind == ChFiDS_Concave ? "concave" : "other",
                f.in1.Dot(f.n2) < 0 ? "-" : "+", f.in2.Dot(f.n1) < 0 ? "-" : "+");
    try {
        BRepFilletAPI_MakeFillet maker(shape);
        maker.Add(edge);
        const int ic = maker.Contour(edge);
        const TopoDS_Vertex first = maker.FirstVertex(ic);
        const bool forward = BRep_Tool::Pnt(first).Distance(f.start) < 1e-9;
        std::printf("  contour %d, %d edge(s), spine %s the canonical direction, abscissa of the start %.9f\n", ic,
                    maker.NbEdges(ic), forward ? "along" : "against",
                    maker.Abscissa(ic, forward ? first : maker.LastVertex(ic)));
        const int n = static_cast<int>(stations.size());
        NCollection_Array1<gp_Pnt2d> uandr(1, n);
        for (int i = 0; i < n; ++i) {
            const auto [u, r] = forward ? stations[i] : stations[n - 1 - i];
            uandr.SetValue(i + 1, gp_Pnt2d(forward ? u : 1.0 - u, r));
        }
        maker.SetRadius(uandr, ic, 1);
        maker.Build();
        if (!maker.IsDone()) {
            std::printf("  NOT DONE\n");
            return;
        }
        const TopoDS_Shape& result = maker.Shape();
        std::printf("  valid %d, self-intersects %d\n", BRepCheck_Analyzer(result).IsValid() ? 1 : 0,
                    BRepAlgoAPI_Check(result, false, true).IsValid() ? 0 : 1);
        // The law, in canonical position s from the start.
        const double L = f.length;
        std::vector<double> xs{-L / 2};
        std::vector<double> rs{stations.front().second};
        for (const auto& [u, r] : stations) {
            xs.push_back(u * L);
            rs.push_back(r);
        }
        xs.push_back(1.5 * L);
        rs.push_back(stations.back().second);
        const ClampedSpline law(xs, rs);
        bool constant = true;
        for (const auto& [u, r] : stations) {
            constant = constant && r == stations.front().second;
        }
        if (maker.IsConstant(ic, edge)) {
            std::printf("  kernel: constant radius %.12f (stations all %s)\n", maker.Radius(ic, edge),
                        constant ? "equal" : "NOT equal");
        } else {
            double w0 = 0, w1 = 0;
            maker.GetBounds(ic, edge, w0, w1);
            const occ::handle<Law_Function> kernelLaw = maker.GetLaw(ic, edge);
            double worst = 0;
            for (int i = 0; i <= 200; ++i) {
                const double s = L * i / 200.0;
                const double w = forward ? s : L - s;
                worst = std::max(worst, std::abs(kernelLaw->Value(w) - law(s)));
            }
            std::printf("  kernel law bounds [%.9f, %.9f] (predicted [%.9f, %.9f]); max |kernel - law| on the edge "
                        "%.3e\n",
                        w0, w1, -L / 2, 1.5 * L, worst);
        }
        // Generated faces and the section model.
        const auto& generated = maker.Generated(edge);
        std::printf("  faces generated from the edge: %d\n", static_cast<int>(generated.Size()));
        const double sign = f.convex ? 1.0 : -1.0;
        const gp_Vec bis = (gp_Vec(f.n1) + gp_Vec(f.n2)) / (1.0 + f.n1.Dot(f.n2));
        const double tanHalf = std::tan(g / 2);
        double section = 0, contact = 0;
        int onPlane1 = 0, onPlane2 = 0, elsewhere = 0;
        const gp_Pln plane1(f.start, f.n1);
        const gp_Pln plane2(f.start, f.n2);
        for (const TopoDS_Shape& s : generated) {
            const TopoDS_Face face = TopoDS::Face(s);
            BRepAdaptor_Surface surface(face);
            double u0, u1, v0, v1;
            BRepTools::UVBounds(face, u0, u1, v0, v1);
            for (int i = 0; i <= 16; ++i) {
                for (int j = 0; j <= 16; ++j) {
                    const gp_Pnt p = surface.Value(u0 + (u1 - u0) * i / 16, v0 + (v1 - v0) * j / 16);
                    const double at = gp_Vec(f.start, p).Dot(gp_Vec(f.t));
                    const double r = law(at);
                    const gp_Pnt c = f.start.Translated(gp_Vec(f.t) * at - bis * (sign * r));
                    section = std::max(section, std::abs(p.Distance(c) - r));
                }
            }
            for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next()) {
                BRepAdaptor_Curve c(TopoDS::Edge(e.Current()));
                for (int i = 0; i <= 32; ++i) {
                    const gp_Pnt p = c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * i / 32);
                    const double at = gp_Vec(f.start, p).Dot(gp_Vec(f.t));
                    const gp_Pnt foot = f.start.Translated(gp_Vec(f.t) * at);
                    const double expected = law(at) * tanHalf;
                    if (plane1.Distance(p) < 1e-7) {
                        ++onPlane1;
                        contact = std::max(contact, std::abs(p.Distance(foot) - expected));
                    } else if (plane2.Distance(p) < 1e-7) {
                        ++onPlane2;
                        contact = std::max(contact, std::abs(p.Distance(foot) - expected));
                    } else {
                        ++elsewhere;
                    }
                }
            }
        }
        std::printf("  section model %.3e mm; contact %.3e mm (samples on face 1 %d, face 2 %d, elsewhere %d)\n",
                    section, contact, onPlane1, onPlane2, elsewhere);
        const double areaFactor = tanHalf - g / 2;
        const double expected = sign * areaFactor * law.integralOfSquare(0, L);
        const double removed = volumeOf(shape) - volumeOf(result);
        std::printf("  volume change %.9f, expected %.9f, rel %.3e\n", removed, expected,
                    std::abs(removed - expected) / std::abs(expected));
    } catch (const Standard_Failure& failure) {
        std::printf("  EXCEPTION %s: %s\n", failure.ExceptionType(), failure.what());
    }
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string which = argc > 1 ? argv[1] : "";
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 60.0, 40.0).Shape();
    if (which == "box") {
        std::printf("box: the spine direction of each edge\n");
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
        TopExp::MapShapes(box, TopAbs_EDGE, edges);
        for (int i = 1; i <= edges.Extent(); ++i) {
            const TopoDS_Edge edge = TopoDS::Edge(edges(i));
            const EdgeFrame f = frameOf(box, edge);
            BRepFilletAPI_MakeFillet maker(box);
            maker.Add(edge);
            const int ic = maker.Contour(edge);
            const bool forward = BRep_Tool::Pnt(maker.FirstVertex(ic)).Distance(f.start) < 1e-9;
            std::printf("  edge %2d from (%g, %g, %g) along (%g, %g, %g): spine %s, %s\n", i, f.start.X(),
                        f.start.Y(), f.start.Z(), f.t.X(), f.t.Y(), f.t.Z(), forward ? "along" : "against",
                        f.convex ? "convex" : "concave");
        }
    } else if (which == "reversed") {
        const TopoDS_Edge edge = edgeBetween(box, gp_Pnt(0, 0, 0), gp_Pnt(100, 0, 0));
        for (const bool reverse : {false, true}) {
            BRepFilletAPI_MakeFillet maker(box);
            maker.Add(reverse ? TopoDS::Edge(edge.Reversed()) : edge);
            const int ic = maker.Contour(edge);
            const gp_Pnt p = BRep_Tool::Pnt(maker.FirstVertex(ic));
            std::printf("reversed: edge added %s: contour %d, first vertex (%g, %g, %g)\n",
                        reverse ? "reversed" : "as found", ic, p.X(), p.Y(), p.Z());
        }
    } else if (which == "hex") {
        std::vector<gp_Pnt> corners;
        for (int i = 0; i < 6; ++i) {
            corners.emplace_back(30 * std::cos(i * pi / 3), 30 * std::sin(i * pi / 3), 0);
        }
        const TopoDS_Shape hex = prism(corners, 50);
        run("hex: vertical edge at (30, 0), stations (0,3) (0.5,6) (1,4)", hex,
            edgeBetween(hex, gp_Pnt(30, 0, 0), gp_Pnt(30, 0, 50)), {{0, 3}, {0.5, 6}, {1, 4}});
    } else if (which == "concave") {
        const TopoDS_Shape l =
            prism({{0, 0, 0}, {80, 0, 0}, {80, 20, 0}, {20, 20, 0}, {20, 60, 0}, {0, 60, 0}}, 40);
        run("concave: the inside edge at (20, 20), stations (0,6) (1,2)", l,
            edgeBetween(l, gp_Pnt(20, 20, 0), gp_Pnt(20, 20, 40)), {{0, 6}, {1, 2}});
    } else if (which == "flat") {
        run("flat: box edge (0,0,0)-(100,0,0), stations (0,4) (0.4,4) (1,4)", box,
            edgeBetween(box, gp_Pnt(0, 0, 0), gp_Pnt(100, 0, 0)), {{0, 4}, {0.4, 4}, {1, 4}});
    } else if (which == "down") {
        run("down: box edge (100,60,0)-(100,60,40), stations (0,2) (1,7)", box,
            edgeBetween(box, gp_Pnt(100, 60, 0), gp_Pnt(100, 60, 40)), {{0, 2}, {1, 7}});
    } else if (which == "against") {
        // The polygon runs clockwise seen from +z, so the prism's edge at
        // x = 100 runs from (100, 60) to (100, 0): against +y.
        const TopoDS_Shape block = prism({{0, 0, 0}, {0, 60, 0}, {100, 60, 0}, {100, 0, 0}}, 40);
        run("against: prism edge (100,60,0)-(100,0,0), stations (0,2) (0.3,4) (1,7)", block,
            edgeBetween(block, gp_Pnt(100, 60, 0), gp_Pnt(100, 0, 0)), {{0, 2}, {0.3, 4}, {1, 7}});
    } else if (which == "all") {
        run("all: box edge (0,60,40)-(100,60,40), stations (0,2) (0.25,5) (0.75,3) (1,6)", box,
            edgeBetween(box, gp_Pnt(0, 60, 40), gp_Pnt(100, 60, 40)), {{0, 2}, {0.25, 5}, {0.75, 3}, {1, 6}});
    } else {
        std::printf("usage: station_contract_probe box|reversed|hex|concave|flat|down|against|all\n");
        return 2;
    }
    return 0;
}
