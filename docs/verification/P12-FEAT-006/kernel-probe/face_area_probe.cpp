// Kernel probe (evidence, not part of the build): the area of a planar face
// bounded by a variable-radius fillet's contact edge, from the kernel's
// default surface integration (what geometry::listFaces() reports) and from
// the same integration with a relative tolerance of 1e-10, against the exact
// area.
//
// A 100 x 60 x 40 box built as BetterCAD builds prisms (a counter-clockwise
// polygon swept up); the top back edge (y = 60, z = 40) is filleted with
// stations (0, 3) (0.4, 4) (0.7, 6) (1, 8), in the kernel's direction along
// the edge (the edge runs -x). The top face keeps 6000 mm^2 less the strip
// up to the contact line, the integral of r(x) over the edge, with r the
// clamped cubic spline of station_contract_probe.cpp.
//
// Built like the other probes; see station-contract-probe.log.
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_Array1.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt2d.hxx>

#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

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
    /// Integral of r over [from, to]: 4-point Gauss-Legendre per span, exact for cubics.
    double integral(double from, double to) const {
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
                sum += weight[j] * (hi - lo) / 2 * (*this)((lo + hi) / 2 + (hi - lo) / 2 * node[j]);
            }
        }
        return sum;
    }
};

} // namespace

int main() {
    BRepBuilderAPI_MakePolygon polygon;
    for (const gp_Pnt& p : {gp_Pnt(0, 0, 0), gp_Pnt(100, 0, 0), gp_Pnt(100, 60, 0), gp_Pnt(0, 60, 0)}) {
        polygon.Add(p);
    }
    polygon.Close();
    const TopoDS_Shape block =
        BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire(), true).Face(), gp_Vec(0, 0, 40)).Shape();
    TopoDS_Edge edge;
    for (TopExp_Explorer it(block, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge e = TopoDS::Edge(it.Current());
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(e, v1, v2);
        const gp_Pnt a = BRep_Tool::Pnt(v1);
        const gp_Pnt b = BRep_Tool::Pnt(v2);
        if (std::abs(a.Y() - 60) < 1e-9 && std::abs(b.Y() - 60) < 1e-9 && std::abs(a.Z() - 40) < 1e-9 &&
            std::abs(b.Z() - 40) < 1e-9) {
            edge = e;
        }
    }
    BRepFilletAPI_MakeFillet maker(block);
    maker.Add(edge);
    const int ic = maker.Contour(edge);
    const bool forward = BRep_Tool::Pnt(maker.FirstVertex(ic)).X() < 50.0;
    const std::vector<std::pair<double, double>> stations = {{0, 3}, {0.4, 4}, {0.7, 6}, {1, 8}};
    NCollection_Array1<gp_Pnt2d> uandr(1, 4);
    for (int k = 0; k < 4; ++k) {
        const auto [u, r] = forward ? stations[k] : stations[3 - k];
        uandr.SetValue(k + 1, gp_Pnt2d(forward ? u : 1.0 - u, r));
    }
    maker.SetRadius(uandr, ic, 1);
    maker.Build();
    std::printf("face area probe: the spine runs %s +x; built %d\n", forward ? "along" : "against",
                maker.IsDone() ? 1 : 0);
    ClampedSpline law({-0.5, 0, 0.4, 0.7, 1, 1.5}, {3, 3, 4, 6, 8, 8});
    const double exact = 6000.0 - 100.0 * law.integral(0.0, 1.0);
    for (TopExp_Explorer it(maker.Shape(), TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane || std::abs(surface.Plane().Location().Z() - 40) > 1e-9 ||
            std::abs(surface.Plane().Axis().Direction().Z()) < 0.5) {
            continue;
        }
        GProp_GProps plain;
        BRepGProp::SurfaceProperties(face, plain);
        GProp_GProps tight;
        const double error = BRepGProp::SurfaceProperties(face, tight, 1e-10);
        std::printf("top face: default %.9f (rel %.3e), eps 1e-10 %.9f (rel %.3e, reported error %.1e), exact "
                    "%.9f\n",
                    plain.Mass(), std::abs(plain.Mass() - exact) / exact, tight.Mass(),
                    std::abs(tight.Mass() - exact) / exact, error, exact);
    }
    return 0;
}
