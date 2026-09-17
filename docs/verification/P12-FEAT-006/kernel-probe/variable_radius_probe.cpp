// Kernel probe (evidence, not part of the build): what OCCT 8.0.1 builds for a
// variable-radius fillet, measured against geometry computed here.
//
// Reading OCCT 8.0.1's sources (ChFiDS_FilSpine::ComputeLaw / mklaw,
// Law_Interpol::Set(points, 0., 0.), Law_Interpolate::PerformNonPeriodic):
// radius stations (Add(R1, R2, E), Add(UandR, E)) become a cubic B-spline
// with knots at the stations, through the station radii, with zero end
// slopes (a clamped cubic spline). The header calls Add(R1, R2, E) "a
// linear radius evolution law"; the code is not linear. Run 1 of this probe
// showed more: the spline also passes through the first and last radius
// again at the ends of the spine's internal extension (here [-50, 150] for
// the 100 mm edge), which GetBounds() reports; so the reference below is the
// clamped spline through (F, r_1), the stations, (L, r_n).
// ChFiDS_FilSpine::SetRadius(Law_Function) builds a local law and drops it.
// BlendFunc_EvolRad puts every section in the plane normal to the spine, with
// the ball centre at the radius from both faces; the surface is then
// approximated (tolapp3d 1e-4 by default).
//
// This probe checks each of those statements on a 100 x 60 x 40 box whose
// edge (0,0,0)-(100,0,0) is filleted. In the section x = c the fillet is a
// quarter circle of radius r(c) about (c, r, r), touching y = 0 at z = r and
// z = 0 at y = r, and the section loses (1 - pi/4) r^2. So:
//   removed volume = (1 - pi/4) * integral of r(x)^2 dx
//   contact lines: z = r(x) on y = 0, y = r(x) on z = 0
//   every fillet point P: |P - (Px, r(Px), r(Px))| = r(Px)
// with r that clamped cubic spline, computed here by its own tridiagonal
// solve and integrated exactly over the edge (Gauss-Legendre, 4 points per
// span, degree 6). Each case runs alone: variable_radius_probe <case>.
//
// Built against the dependency prefix like fillet_probe.cpp, plus -lTKGeomAlgo
// (Law) which it already links.
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Law_Function.hxx>
#include <Law_Linear.hxx>
#include <NCollection_Array1.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pnt2d.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double pi = M_PI;
constexpr double k = 1.0 - pi / 4.0;
constexpr double kBox = 100.0 * 60.0 * 40.0;

/// The clamped cubic spline through (x_i, r_i) with zero end slopes: the
/// second derivatives M_i from the standard tridiagonal system.
struct ClampedSpline {
    std::vector<double> x, r, m;

    ClampedSpline(std::vector<double> xs, std::vector<double> rs) : x(std::move(xs)), r(std::move(rs)) {
        const std::size_t n = x.size();
        m.assign(n, 0.0);
        // Rows: h_{i-1} M_{i-1} + 2(h_{i-1}+h_i) M_i + h_i M_{i+1} = 6(d_i - d_{i-1}),
        // ends: 2 h_0 M_0 + h_0 M_1 = 6 d_0 ; h_{n-2} M_{n-2} + 2 h_{n-2} M_{n-1} = -6 d_{n-2}.
        std::vector<double> a(n, 0.0), b(n, 0.0), c(n, 0.0), rhs(n, 0.0);
        auto h = [&](std::size_t i) { return x[i + 1] - x[i]; };
        auto d = [&](std::size_t i) { return (r[i + 1] - r[i]) / h(i); };
        b[0] = 2.0 * h(0);
        c[0] = h(0);
        rhs[0] = 6.0 * d(0);
        for (std::size_t i = 1; i + 1 < n; ++i) {
            a[i] = h(i - 1);
            b[i] = 2.0 * (h(i - 1) + h(i));
            c[i] = h(i);
            rhs[i] = 6.0 * (d(i) - d(i - 1));
        }
        a[n - 1] = h(n - 2);
        b[n - 1] = 2.0 * h(n - 2);
        rhs[n - 1] = -6.0 * d(n - 2);
        for (std::size_t i = 1; i < n; ++i) {
            const double w = a[i] / b[i - 1];
            b[i] -= w * c[i - 1];
            rhs[i] -= w * rhs[i - 1];
        }
        m[n - 1] = rhs[n - 1] / b[n - 1];
        for (std::size_t i = n - 1; i-- > 0;) {
            m[i] = (rhs[i] - c[i] * m[i + 1]) / b[i];
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

    /// The integral of r^2 over [x_0, x_n]: 4-point Gauss-Legendre per span is
    /// exact for the degree-6 integrand.
    double integralOfSquare(double from, double to) const {
        static const std::array<double, 4> node = {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563,
                                                   0.8611363115940526};
        static const std::array<double, 4> weight = {0.3478548451374538, 0.6521451548625461, 0.6521451548625461,
                                                     0.3478548451374538};
        double sum = 0.0;
        for (std::size_t i = 0; i + 1 < x.size(); ++i) {
            const double lo = std::max(x[i], from);
            const double hi = std::min(x[i + 1], to);
            if (hi <= lo) {
                continue;
            }
            const double mid = 0.5 * (lo + hi);
            const double half = 0.5 * (hi - lo);
            for (int j = 0; j < 4; ++j) {
                const double v = (*this)(mid + half * node[j]);
                sum += weight[j] * half * v * v;
            }
        }
        return sum;
    }
};

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

/// The volume three ways: the kernel's default adaptive integration, the
/// same with a relative tolerance of 1e-10 (what BetterCAD's
/// Body::massProperties() uses for bodies without swept-curve faces), and
/// Gauss-Kronrod over knot spans with 1e-10.
struct Volumes {
    double plain = 0.0;
    double adaptive = 0.0;
    double gaussKronrod = 0.0;
};

Volumes volumesOf(const TopoDS_Shape& shape) {
    Volumes out;
    out.plain = volumeOf(shape);
    GProp_GProps adaptive;
    BRepGProp::VolumeProperties(shape, adaptive, 1e-10, true);
    out.adaptive = adaptive.Mass();
    GProp_GProps spans;
    BRepGProp::VolumePropertiesGK(shape, spans, 1e-10, true, true);
    out.gaussKronrod = spans.Mass();
    return out;
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

bool startsAt(const TopoDS_Edge& edge, const gp_Pnt& a) {
    TopoDS_Vertex v1, v2;
    TopExp::Vertices(edge, v1, v2, true);
    return BRep_Tool::Pnt(v1).Distance(a) < 1e-9;
}

/// Deviations of the non-planar faces of @p result from the section model
/// of the edge (0,0,0)-(100,0,0) with radius @p r(x).
struct Deviation {
    double surface = 0.0;  // max | |P - (x, r, r)| - r |
    double contactY = 0.0; // max |z - r(x)| on the edge in y = 0
    double contactZ = 0.0; // max |y - r(x)| on the edge in z = 0
    int faces = 0;
};

Deviation measureBox(const TopoDS_Shape& result, const std::function<double(double)>& r) {
    Deviation out;
    for (TopExp_Explorer it(result, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Plane) {
            continue;
        }
        ++out.faces;
        double u0, u1, v0, v1;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        for (int i = 0; i <= 60; ++i) {
            for (int j = 0; j <= 60; ++j) {
                const gp_Pnt p = surface.Value(u0 + (u1 - u0) * i / 60.0, v0 + (v1 - v0) * j / 60.0);
                const double rx = r(p.X());
                out.surface = std::max(out.surface, std::abs(p.Distance(gp_Pnt(p.X(), rx, rx)) - rx));
            }
        }
        for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next()) {
            BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));
            std::vector<gp_Pnt> points;
            for (int i = 0; i <= 200; ++i) {
                points.push_back(curve.Value(curve.FirstParameter() +
                                             (curve.LastParameter() - curve.FirstParameter()) * i / 200.0));
            }
            const bool inY = std::all_of(points.begin(), points.end(), [](const gp_Pnt& p) { return std::abs(p.Y()) < 1e-6; });
            const bool inZ = std::all_of(points.begin(), points.end(), [](const gp_Pnt& p) { return std::abs(p.Z()) < 1e-6; });
            for (const gp_Pnt& p : points) {
                if (inY) {
                    out.contactY = std::max(out.contactY, std::abs(p.Z() - r(p.X())));
                } else if (inZ) {
                    out.contactZ = std::max(out.contactZ, std::abs(p.Y() - r(p.X())));
                }
            }
        }
    }
    return out;
}

using Setup = std::function<void(BRepFilletAPI_MakeFillet&, const TopoDS_Edge&)>;

/// Radius stations along the edge, by distance from (0,0,0).
struct Stations {
    std::vector<double> x;
    std::vector<double> r;
};

/// Faces of @p result by type, with areas.
void listFaces(const TopoDS_Shape& result) {
    for (TopExp_Explorer it(result, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        BRepAdaptor_Surface surface(face);
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(face, bounds, false, false);
        std::printf("    face type %d area %.6f bounds (%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n", int(surface.GetType()),
                    props.Mass(), bounds.CornerMin().X(), bounds.CornerMin().Y(), bounds.CornerMin().Z(),
                    bounds.CornerMax().X(), bounds.CornerMax().Y(), bounds.CornerMax().Z());
    }
}

/// Fillets the edge (0,0,0)-(length,0,0) of a length x 60 x 40 box and
/// measures the result against the clamped spline through @p stations, with
/// the first and last radius repeated at the law's bounds (GetBounds).
void boxCase(const char* name, double length, const Setup& add, const Stations& stations, double tolApp = -1.0,
             const Setup& after = {}) {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(length, 60.0, 40.0).Shape();
    const TopoDS_Edge edge = edgeBetween(box, gp_Pnt(0, 0, 0), gp_Pnt(length, 0, 0));
    std::printf("%s\n", name);
    try {
        BRepFilletAPI_MakeFillet fillet(box);
        if (tolApp > 0.0) {
            // SetParams(Tang, Tesp, T2d, TApp3d, TolApp2d, Fleche); the
            // defaults are (angular, 1e-4, 1e-5, 1e-4, 1e-5, 1e-3).
            fillet.SetParams(1e-2, tolApp, tolApp / 10.0, tolApp, tolApp / 10.0, tolApp * 10.0);
        }
        add(fillet, edge);
        fillet.Build();
        if (after) {
            after(fillet, edge);
        }
        if (!fillet.IsDone()) {
            std::printf("  NOT DONE (faulty contours %d)\n", fillet.NbFaultyContours());
            return;
        }
        const TopoDS_Shape& result = fillet.Shape();
        const bool valid = BRepCheck_Analyzer(result).IsValid();
        const bool selfIntersects = !BRepAlgoAPI_Check(result, false, true).IsValid();
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(result, bounds, false, false);
        const Volumes volumes = volumesOf(result);
        const double removed = length * 60.0 * 40.0 - volumes.adaptive;
        std::printf("  valid %d, self-intersects %d, bounds (%.6f %.6f %.6f)-(%.6f %.6f %.6f), removed %.9f "
                    "(adaptive 1e-10)\n",
                    valid ? 1 : 0, selfIntersects ? 1 : 0, bounds.CornerMin().X(), bounds.CornerMin().Y(),
                    bounds.CornerMin().Z(), bounds.CornerMax().X(), bounds.CornerMax().Y(), bounds.CornerMax().Z(),
                    removed);
        double first = 0.0;
        double last = 0.0;
        occ::handle<Law_Function> law;
        try {
            if (fillet.GetBounds(1, edge, first, last)) {
                law = fillet.GetLaw(1, edge);
                std::printf("  law bounds [%.9f, %.9f]; r at 0, 1/4, 1/2, 3/4, 1 of the edge: %.12f %.12f %.12f "
                            "%.12f %.12f\n",
                            first, last, law->Value(0.0), law->Value(length / 4.0), law->Value(length / 2.0),
                            law->Value(3.0 * length / 4.0), law->Value(length));
            } else {
                std::printf("  law: GetBounds false (a constant edge)\n");
            }
        } catch (const Standard_Failure& failure) {
            std::printf("  law: %s: %s\n", failure.ExceptionType(), failure.what());
        }
        if (law.IsNull() || stations.x.empty()) {
            listFaces(result);
            return;
        }
        std::vector<double> xs{first};
        std::vector<double> rs{stations.r.front()};
        xs.insert(xs.end(), stations.x.begin(), stations.x.end());
        rs.insert(rs.end(), stations.r.begin(), stations.r.end());
        xs.push_back(last);
        rs.push_back(stations.r.back());
        const ClampedSpline reference(xs, rs);
        double lawDeviation = 0.0;
        for (int i = 0; i <= 400; ++i) {
            const double w = first + (last - first) * i / 400.0;
            lawDeviation = std::max(lawDeviation, std::abs(law->Value(w) - reference(w)));
        }
        const double expected = k * reference.integralOfSquare(0.0, length);
        const Deviation fromReference =
            measureBox(result, [&](double x) { return reference(std::clamp(x, 0.0, length)); });
        const Deviation fromLaw = measureBox(result, [&](double x) { return law->Value(std::clamp(x, 0.0, length)); });
        double lowest = std::numeric_limits<double>::infinity();
        double highest = -lowest;
        for (int i = 0; i <= 1000; ++i) {
            const double v = reference(length * i / 1000.0);
            lowest = std::min(lowest, v);
            highest = std::max(highest, v);
        }
        std::printf("  max |law - reference spline| %.3e over the law's bounds; on the edge the reference runs "
                    "from %.6f to %.6f\n",
                    lawDeviation, lowest, highest);
        const auto rel = [&](double volume) {
            return std::abs(length * 60.0 * 40.0 - volume - expected) / expected;
        };
        std::printf("  removed: reference %.9f; measured rel %.3e (adaptive 1e-10), %.3e (default), %.3e "
                    "(Gauss-Kronrod 1e-10)\n",
                    expected, rel(volumes.adaptive), rel(volumes.plain), rel(volumes.gaussKronrod));
        std::printf("  %d fillet face(s); from the reference: sections %.3e mm, contact lines %.3e / %.3e mm\n",
                    fromReference.faces, fromReference.surface, fromReference.contactY, fromReference.contactZ);
        std::printf("  from the builder's own law: sections %.3e mm, contact lines %.3e / %.3e mm\n",
                    fromLaw.surface, fromLaw.contactY, fromLaw.contactZ);
    } catch (const Standard_Failure& failure) {
        std::printf("  EXCEPTION %s: %s\n", failure.ExceptionType(), failure.what());
    }
}

Setup twoStations(double r1, double r2) {
    return [=](BRepFilletAPI_MakeFillet& f, const TopoDS_Edge& e) {
        if (startsAt(e, gp_Pnt(0, 0, 0))) {
            f.Add(r1, r2, e);
        } else {
            f.Add(r2, r1, e);
        }
    };
}

/// Add(UandR, E) with (u, r) pairs, u from 0 at (0,0,0) to 1.
Setup uandr(std::vector<std::pair<double, double>> at) {
    return [=](BRepFilletAPI_MakeFillet& f, const TopoDS_Edge& e) {
        double first = 0.0;
        double last = 0.0;
        BRep_Tool::Range(e, first, last);
        const bool forward = startsAt(e, gp_Pnt(0, 0, 0));
        const int n = static_cast<int>(at.size());
        NCollection_Array1<gp_Pnt2d> points(1, n);
        for (int i = 0; i < n; ++i) {
            const auto [u, r] = forward ? at[i] : at[n - 1 - i];
            const double s = forward ? u : 1.0 - u;
            points.SetValue(i + 1, gp_Pnt2d(first + (last - first) * s, r));
        }
        f.Add(points, e);
    };
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string which = argc > 1 ? argv[1] : "";
    if (which == "1a" || which == "1b" || which == "1c") {
        const double r2 = which == "1a" ? 5.5 : which == "1b" ? 8.0 : 12.0;
        char name[96];
        std::snprintf(name, sizeof name, "1. Add(3, %.1f, E), 100 mm edge", r2);
        boxCase(name, 100.0, twoStations(3.0, r2), {{0.0, 100.0}, {3.0, r2}});
    } else if (which == "1t") {
        boxCase("1t. Add(3, 8, E), 100 mm edge, approximation tolerance 1e-6", 100.0, twoStations(3.0, 8.0),
                {{0.0, 100.0}, {3.0, 8.0}}, 1e-6);
    } else if (which == "1l") {
        boxCase("1l. Add(3, 8, E), 150 mm edge", 150.0, twoStations(3.0, 8.0), {{0.0, 150.0}, {3.0, 8.0}});
    } else if (which == "1s") {
        boxCase("1s. Add(3, 8, E), 30 mm edge", 30.0, twoStations(3.0, 8.0), {{0.0, 30.0}, {3.0, 8.0}});
    } else if (which == "2") {
        boxCase("2. Add(UandR, E), (0,3) (0.3,7) (0.5,5) (1,4), 100 mm edge", 100.0,
                uandr({{0.0, 3.0}, {0.3, 7.0}, {0.5, 5.0}, {1.0, 4.0}}), {{0.0, 30.0, 50.0, 100.0}, {3.0, 7.0, 5.0, 4.0}});
    } else if (which == "2t") {
        boxCase("2t. the same, approximation tolerance 1e-6", 100.0,
                uandr({{0.0, 3.0}, {0.3, 7.0}, {0.5, 5.0}, {1.0, 4.0}}), {{0.0, 30.0, 50.0, 100.0}, {3.0, 7.0, 5.0, 4.0}},
                1e-6);
    } else if (which == "2s") {
        boxCase("2s. Add(UandR, E), (0,4) (0.5,6) (1,4), 100 mm edge", 100.0,
                uandr({{0.0, 4.0}, {0.5, 6.0}, {1.0, 4.0}}), {{0.0, 50.0, 100.0}, {4.0, 6.0, 4.0}});
    } else if (which == "10") {
        // Every station radius fits the 40 x 60 corner, but the spline
        // between them reaches 44.2 and -3.2 (computed here).
        boxCase("10. Add(UandR, E), (0,5) (0.4,5) (0.6,36) (1,36): the law leaves [5, 36]", 100.0,
                uandr({{0.0, 5.0}, {0.4, 5.0}, {0.6, 36.0}, {1.0, 36.0}}),
                {{0.0, 40.0, 60.0, 100.0}, {5.0, 5.0, 36.0, 36.0}});
    } else if (which == "11") {
        // The same shape at 5 -> 20: the law overshoots to about 24 and
        // 1, still inside the corner.
        boxCase("11. Add(UandR, E), (0,5) (0.4,5) (0.6,20) (1,20): the law leaves [5, 20]", 100.0,
                uandr({{0.0, 5.0}, {0.4, 5.0}, {0.6, 20.0}, {1.0, 20.0}}),
                {{0.0, 40.0, 60.0, 100.0}, {5.0, 5.0, 20.0, 20.0}});
    } else if (which == "3") {
        boxCase("3. Add(Law_Linear 3 -> 8, E)", 100.0,
                [](BRepFilletAPI_MakeFillet& f, const TopoDS_Edge& e) {
                    occ::handle<Law_Linear> law = new Law_Linear();
                    law->Set(0.0, 3.0, 100.0, 8.0);
                    f.Add(law, e);
                },
                {});
    } else if (which == "4") {
        boxCase("4. Add(3, 8, E), Build, SetLaw(Law_Linear), Build", 100.0, twoStations(3.0, 8.0),
                {{0.0, 100.0}, {3.0, 8.0}}, -1.0, [](BRepFilletAPI_MakeFillet& f, const TopoDS_Edge& e) {
                    occ::handle<Law_Linear> law = new Law_Linear();
                    law->Set(0.0, 3.0, 100.0, 8.0);
                    f.SetLaw(1, e, law);
                    std::printf("  SetLaw accepted; the law at 50 is now %.12f\n", f.GetLaw(1, e)->Value(50.0));
                    f.Build();
                });
    } else if (which == "5") {
        boxCase("5. Add(3, 45, E): the radius passes the 40 mm face", 100.0, twoStations(3.0, 45.0), {});
    } else if (which == "6") {
        std::printf("6. disc r 30 h 20, top rim, Add(UandR) (0,3) (0.5,6) (1,3)\n");
        const TopoDS_Shape disc = BRepPrimAPI_MakeCylinder(30.0, 20.0).Shape();
        try {
            BRepFilletAPI_MakeFillet fillet(disc);
            for (TopExp_Explorer it(disc, TopAbs_EDGE); it.More(); it.Next()) {
                const TopoDS_Edge e = TopoDS::Edge(it.Current());
                BRepAdaptor_Curve curve(e);
                if (curve.GetType() == GeomAbs_Circle && curve.Value(curve.FirstParameter()).Z() > 10.0) {
                    NCollection_Array1<gp_Pnt2d> points(1, 3);
                    points.SetValue(1, gp_Pnt2d(curve.FirstParameter(), 3.0));
                    points.SetValue(2, gp_Pnt2d(0.5 * (curve.FirstParameter() + curve.LastParameter()), 6.0));
                    points.SetValue(3, gp_Pnt2d(curve.LastParameter(), 3.0));
                    fillet.Add(points, e);
                }
            }
            fillet.Build();
            std::printf("  done %d\n", fillet.IsDone() ? 1 : 0);
            if (fillet.IsDone()) {
                const TopoDS_Shape& result = fillet.Shape();
                const double a3 = 3.0 * (10.0 - 3.0 * pi) / (12.0 - 3.0 * pi);
                const double a6 = 6.0 * (10.0 - 3.0 * pi) / (12.0 - 3.0 * pi);
                std::printf("  valid %d, self-intersects %d, removed %.9f (constant r 3 removes %.9f, r 6 %.9f)\n",
                            BRepCheck_Analyzer(result).IsValid() ? 1 : 0,
                            BRepAlgoAPI_Check(result, false, true).IsValid() ? 0 : 1,
                            pi * 900.0 * 20.0 - volumeOf(result), 2.0 * pi * k * 9.0 * (30.0 - a3),
                            2.0 * pi * k * 36.0 * (30.0 - a6));
                listFaces(result);
            }
        } catch (const Standard_Failure& failure) {
            std::printf("  EXCEPTION %s: %s\n", failure.ExceptionType(), failure.what());
        }
    } else if (which == "7" || which == "8") {
        // Edges meeting at (0,0,0): 7 = the x and y edges, 8 = all three,
        // each from r 3 at the vertex to r 8 at its far end.
        const bool three = which == "8";
        std::printf("%s. %s edges at a box corner, each r 3 at the corner to r 8\n", which.c_str(),
                    three ? "three" : "two");
        const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 60.0, 40.0).Shape();
        const gp_Pnt o(0, 0, 0);
        std::vector<std::pair<TopoDS_Edge, double>> edges = {{edgeBetween(box, o, gp_Pnt(100, 0, 0)), 100.0},
                                                             {edgeBetween(box, o, gp_Pnt(0, 60, 0)), 60.0}};
        if (three) {
            edges.emplace_back(edgeBetween(box, o, gp_Pnt(0, 0, 40)), 40.0);
        }
        try {
            BRepFilletAPI_MakeFillet fillet(box);
            for (const auto& [e, length] : edges) {
                twoStations(3.0, 8.0)(fillet, e);
            }
            fillet.Build();
            std::printf("  done %d\n", fillet.IsDone() ? 1 : 0);
            if (fillet.IsDone()) {
                const TopoDS_Shape& result = fillet.Shape();
                const Volumes volumes = volumesOf(result);
                std::printf("  valid %d, self-intersects %d, removed %.9f (adaptive 1e-10), %.9f (Gauss-Kronrod)\n",
                            BRepCheck_Analyzer(result).IsValid() ? 1 : 0,
                            BRepAlgoAPI_Check(result, false, true).IsValid() ? 0 : 1, kBox - volumes.adaptive,
                            kBox - volumes.gaussKronrod);
                for (int ic = 1; ic <= fillet.NbContours(); ++ic) {
                    for (const auto& [e, length] : edges) {
                        double first = 0.0;
                        double last = 0.0;
                        if (fillet.Contour(e) == ic && fillet.GetBounds(ic, e, first, last)) {
                            std::printf("  contour %d, edge of %.0f mm: law bounds [%.9f, %.9f]\n", ic, length, first,
                                        last);
                        }
                    }
                }
                listFaces(result);
            }
        } catch (const Standard_Failure& failure) {
            std::printf("  EXCEPTION %s: %s\n", failure.ExceptionType(), failure.what());
        }
    } else if (which == "9") {
        // A tangent chain: the top edges of a prism whose vertical edge at
        // (100, 0) is rounded r 10 first; the chain is line, arc, line.
        std::printf("9. a tangent chain (line, arc, line), Add(3, 8, E) on its first line\n");
        const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 60.0, 40.0).Shape();
        try {
            BRepFilletAPI_MakeFillet round(box);
            round.Add(10.0, edgeBetween(box, gp_Pnt(100, 0, 0), gp_Pnt(100, 0, 40)));
            round.Build();
            const TopoDS_Shape rounded = round.Shape();
            TopoDS_Edge top;
            for (TopExp_Explorer it(rounded, TopAbs_EDGE); it.More(); it.Next()) {
                const TopoDS_Edge e = TopoDS::Edge(it.Current());
                BRepAdaptor_Curve curve(e);
                const gp_Pnt a = curve.Value(curve.FirstParameter());
                const gp_Pnt b = curve.Value(curve.LastParameter());
                if (curve.GetType() == GeomAbs_Line && std::abs(a.Z() - 40.0) < 1e-9 &&
                    std::abs(b.Z() - 40.0) < 1e-9 && std::abs(a.Y()) < 1e-9 && std::abs(b.Y()) < 1e-9) {
                    top = e;
                }
            }
            BRepFilletAPI_MakeFillet fillet(rounded);
            fillet.Add(3.0, 8.0, top);
            std::printf("  contours %d, edges in contour 1: %d\n", fillet.NbContours(), fillet.NbEdges(1));
            fillet.Build();
            std::printf("  done %d\n", fillet.IsDone() ? 1 : 0);
            if (fillet.IsDone()) {
                const TopoDS_Shape& result = fillet.Shape();
                std::printf("  valid %d, self-intersects %d\n", BRepCheck_Analyzer(result).IsValid() ? 1 : 0,
                            BRepAlgoAPI_Check(result, false, true).IsValid() ? 0 : 1);
                for (int i = 1; i <= fillet.NbEdges(1); ++i) {
                    double first = 0.0;
                    double last = 0.0;
                    const TopoDS_Edge& e = fillet.Edge(1, i);
                    const bool known = fillet.GetBounds(1, e, first, last);
                    std::printf("  edge %d: law %s [%.6f, %.6f]\n", i, known ? "on" : "(none)", first, last);
                }
                listFaces(result);
            }
        } catch (const Standard_Failure& failure) {
            std::printf("  EXCEPTION %s: %s\n", failure.ExceptionType(), failure.what());
        }
    } else {
        std::printf("usage: variable_radius_probe 1a|1b|1c|1t|1l|1s|2|2t|2s|3|4|5|6|7|8|9|10|11\n");
        return 2;
    }
    return 0;
}
