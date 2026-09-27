// Kernel probe: the runtime cost of tightening the integration tolerance for
// second moments, and whether a tighter tolerance stays deterministic.
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Mat.hxx>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct Timing {
    double seconds = 0.0;
    double xx = 0.0;
    double yy = 0.0;
    double zz = 0.0;
};

Timing run(const TopoDS_Shape& shape, double eps, int repeats) {
    const auto start = Clock::now();
    gp_Mat last;
    for (int i = 0; i < repeats; ++i) {
        GProp_GProps props;
        BRepGProp::VolumeProperties(shape, props, eps, true);
        last = props.MatrixOfInertia();
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count() / repeats;
    return Timing{elapsed, last.Value(1, 1), last.Value(2, 2), last.Value(3, 3)};
}

std::size_t faceCount(const TopoDS_Shape& shape) {
    std::size_t n = 0;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        ++n;
    }
    return n;
}

void report(const std::string& label, const TopoDS_Shape& shape, int repeats) {
    std::printf("%s (%zu faces, %d repeats)\n", label.c_str(), faceCount(shape), repeats);
    Timing baseline{};
    for (const double eps : {1e-10, 1e-12, 1e-14}) {
        const Timing t = run(shape, eps, repeats);
        if (eps == 1e-10) {
            baseline = t;
        }
        std::printf("  eps %-8g %8.4f ms   x%.2f   xx %.17g\n", eps, t.seconds * 1e3,
                    baseline.seconds > 0.0 ? t.seconds / baseline.seconds : 1.0, t.xx);
    }
    // Determinism at the tightest setting: ten runs must agree bit for bit.
    const Timing first = run(shape, 1e-14, 1);
    bool identical = true;
    for (int i = 0; i < 10; ++i) {
        const Timing again = run(shape, 1e-14, 1);
        identical = identical && again.xx == first.xx && again.yy == first.yy && again.zz == first.zz;
    }
    std::printf("  eps 1e-14 repeatable bit for bit over 10 runs: %s\n\n", identical ? "YES" : "NO");
}

} // namespace

int main() {
    std::printf("P15-MASS-001 tolerance cost probe\n\n");
    report("cylinder r 2 h 6", BRepPrimAPI_MakeCylinder(2.0, 6.0).Shape(), 200);
    report("sphere r 3", BRepPrimAPI_MakeSphere(3.0).Shape(), 200);
    report("torus R 20 r 5", BRepPrimAPI_MakeTorus(20.0, 5.0).Shape(), 100);
    report("box 10^3", BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), 500);

    // A part-like body: a plate with four drilled holes and a filleted boss --
    // planar, cylindrical and toroidal faces together, which is what a real
    // BetterCAD part presents to the integrator.
    TopoDS_Shape plate = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 80.0, 50.0, 10.0).Shape();
    plate = BRepAlgoAPI_Fuse(plate, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(40, 25, 10), gp_Dir(0, 0, 1)), 12.0, 20.0).Shape()).Shape();
    for (const auto& [x, y] : {std::pair{8.0, 8.0}, std::pair{72.0, 8.0}, std::pair{8.0, 42.0}, std::pair{72.0, 42.0}}) {
        plate = BRepAlgoAPI_Cut(plate, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, -1), gp_Dir(0, 0, 1)), 3.0, 12.0).Shape()).Shape();
    }
    plate = BRepAlgoAPI_Cut(plate, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(40, 25, -1), gp_Dir(0, 0, 1)), 6.0, 40.0).Shape()).Shape();
    report("plate, boss and 5 holes", plate, 50);
    return 0;
}
