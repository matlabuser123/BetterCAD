// Kernel probe for P11-FEAT-009 Loft (evidence, not part of the build).
// Raw OCCT 8.0.1: how BRepOffsetAPI_ThruSections builds solids through
// planar sections, and which settings keep the correspondence BetterCAD
// chooses. One case and builder per process, so a crash shows as an exit
// code:
//   loft_kernel_probe <case> <builder>
// Builders:
//   ruled     ThruSections(solid, ruled), CheckCompatibility(false): wires as
//             given (edge i to edge i, circle seams on +X)
//   ruled-cc  the same with CheckCompatibility(true) (OCCT's default)
//   smooth    ThruSections(solid, not ruled), CheckCompatibility(false)
//   shifted   ruled, but every section after the first starts one edge later
//             (a circle's seam turned by 90 deg): a wrong correspondence
// Each line: solids, BRepCheck validity, volume with the production
// integration (adaptive, 1e-10 relative, closed shells), the analytic
// volume and relative error where there is one, bounds, centre of mass,
// time, the face types and BRepAlgoAPI_Check's self-interference result.
//
// Expected volumes are closed forms (prism, cylinder, frustum, similar
// sections) or, for sections that are not similar, the prismatoid formula
// V = h/6 (A0 + 4 Am + A1): a ruled loft between two parallel sections whose
// points correspond at equal parameters has a cross-section area quadratic
// in the height, and Am is the area of the section of averaged points,
// computed exactly here (lines average to lines; arcs of equal sweep average
// to arcs).
//
// Built against the dependency prefix, e.g.:
//   g++ -std=c++23 -O2 -D_USE_MATH_DEFINES -I<deps>/include/opencascade
//       loft_kernel_probe.cpp -L<deps>/lib -lTKOffset -lTKFillet -lTKBool
//       -lTKBO -lTKPrim -lTKTopAlgo -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG2d
//       -lTKG3d -lTKShHealing -lTKMath -lTKernel
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr double pi = 3.14159265358979323846;

struct P2 {
    double x = 0.0;
    double y = 0.0;
};

// A segment of a loop in a horizontal plane: a line from a to b, or an arc
// about c from a to b (counter-clockwise when ccw).
struct Seg {
    bool arc = false;
    P2 a, b, c;
    bool ccw = true;
};

// A closed section: a loop of segments in the plane z (normal +Z), or a
// circle (any plane, seam along xdir).
struct Section {
    bool isCircle = false;
    std::vector<Seg> loop;
    double z = 0.0;
    gp_Pnt center;
    gp_Dir normal{0, 0, 1};
    gp_Dir xdir{1, 0, 0};
    double radius = 0.0;
};

Section circle(gp_Pnt center, double radius, gp_Dir normal = gp_Dir(0, 0, 1), gp_Dir xdir = gp_Dir(1, 0, 0)) {
    Section s;
    s.isCircle = true;
    s.center = center;
    s.radius = radius;
    s.normal = normal;
    s.xdir = xdir;
    return s;
}

Section polygon(std::vector<P2> p, double z) {
    Section s;
    s.z = z;
    for (std::size_t i = 0; i < p.size(); ++i) {
        s.loop.push_back({false, p[i], p[(i + 1) % p.size()], {}, true});
    }
    return s;
}

// Axis-aligned w x h rectangle centred at (cx, cy), counter-clockwise from
// its upper-right corner.
Section rectangle(double cx, double cy, double z, double w, double h) {
    return polygon({{cx + w / 2, cy + h / 2}, {cx - w / 2, cy + h / 2}, {cx - w / 2, cy - h / 2},
                    {cx + w / 2, cy - h / 2}},
                   z);
}

// Regular n-gon of circumradius r, first vertex at angle a0.
Section regular(int n, double r, double z, double a0 = 0.0) {
    std::vector<P2> p;
    for (int i = 0; i < n; ++i) {
        const double a = a0 + 2 * pi * i / n;
        p.push_back({r * std::cos(a), r * std::sin(a)});
    }
    return polygon(p, z);
}

P2 turn(P2 p, double angle) {
    return {p.x * std::cos(angle) - p.y * std::sin(angle), p.x * std::sin(angle) + p.y * std::cos(angle)};
}

P2 add(P2 p, P2 q) {
    return {p.x + q.x, p.y + q.y};
}

// Slot: semicircles of radius r about (+-half, 0), turned by `angle` and moved
// to (cx, cy). Counter-clockwise: right arc, top line, left arc, bottom line.
Section slot(double cx, double cy, double z, double half, double r, double angle = 0.0) {
    const auto at = [&](double x, double y) { return add(turn({x, y}, angle), {cx, cy}); };
    Section s;
    s.z = z;
    s.loop = {{true, at(half, -r), at(half, r), at(half, 0), true},
              {false, at(half, r), at(-half, r), {}, true},
              {true, at(-half, r), at(-half, -r), at(-half, 0), true},
              {false, at(-half, -r), at(half, -r), {}, true}};
    return s;
}

// Rounded w x h rectangle with corner radius r, turned by `angle`.
Section rounded(double w, double h, double r, double z, double angle = 0.0) {
    const double x = w / 2, y = h / 2;
    const auto at = [&](double px, double py) { return turn({px, py}, angle); };
    Section s;
    s.z = z;
    s.loop = {{false, at(x, -y + r), at(x, y - r), {}, true},
              {true, at(x, y - r), at(x - r, y), at(x - r, y - r), true},
              {false, at(x - r, y), at(-x + r, y), {}, true},
              {true, at(-x + r, y), at(-x, y - r), at(-x + r, y - r), true},
              {false, at(-x, y - r), at(-x, -y + r), {}, true},
              {true, at(-x, -y + r), at(-x + r, -y), at(-x + r, -y + r), true},
              {false, at(-x + r, -y), at(x - r, -y), {}, true},
              {true, at(x - r, -y), at(x, -y + r), at(x - r, -y + r), true}};
    return s;
}


// Pac-man: an arc of radius r about (cx, cy) from 30 to 330 deg, closed by
// two lines through the centre.
Section pacman(double cx, double cy, double z, double r) {
    const P2 c{cx, cy};
    const P2 a = add(c, {r * std::cos(pi / 6), r * std::sin(pi / 6)});
    const P2 b = add(c, {r * std::cos(-pi / 6), r * std::sin(-pi / 6)});
    Section s;
    s.z = z;
    s.loop = {{true, a, b, c, true}, {false, b, c, {}, true}, {false, c, a, {}, true}};
    return s;
}

// Every arc split into n arcs of equal sweep.
Section split(Section s, int n) {
    std::vector<Seg> out;
    for (const Seg& seg : s.loop) {
        if (!seg.arc) {
            out.push_back(seg);
            continue;
        }
        const double r = std::hypot(seg.a.x - seg.c.x, seg.a.y - seg.c.y);
        const double t0 = std::atan2(seg.a.y - seg.c.y, seg.a.x - seg.c.x);
        double sweep = std::atan2(seg.b.y - seg.c.y, seg.b.x - seg.c.x) - t0;
        if (seg.ccw) { while (sweep <= 0) sweep += 2 * pi; } else { while (sweep >= 0) sweep -= 2 * pi; }
        P2 from = seg.a;
        for (int k = 1; k <= n; ++k) {
            const double t = t0 + sweep * k / n;
            const P2 to = k == n ? seg.b : P2{seg.c.x + r * std::cos(t), seg.c.y + r * std::sin(t)};
            out.push_back({true, from, to, seg.c, seg.ccw});
            from = to;
        }
    }
    s.loop = out;
    return s;
}


// Half disc of radius r about the origin: the line from angle a - 90 deg to
// a + 90 deg, closed by the arc round through a + 180 deg.
Section halfDisc(double r, double a, double z) {
    const P2 p{r * std::cos(a - pi / 2), r * std::sin(a - pi / 2)};
    const P2 q{r * std::cos(a + pi / 2), r * std::sin(a + pi / 2)};
    Section s;
    s.z = z;
    s.loop = {{false, p, q, {}, true}, {true, q, p, {0, 0}, true}};
    return s;
}

// Exact area of a loop by Green's theorem.
double area(const std::vector<Seg>& loop) {
    double sum = 0.0;
    for (const Seg& s : loop) {
        if (!s.arc) {
            sum += 0.5 * (s.a.x * s.b.y - s.b.x * s.a.y);
            continue;
        }
        const double r = std::hypot(s.a.x - s.c.x, s.a.y - s.c.y);
        const double t0 = std::atan2(s.a.y - s.c.y, s.a.x - s.c.x);
        double sweep = std::atan2(s.b.y - s.c.y, s.b.x - s.c.x) - t0;
        if (s.ccw) {
            while (sweep <= 0) sweep += 2 * pi;
        } else {
            while (sweep >= 0) sweep -= 2 * pi;
        }
        const double t1 = t0 + sweep;
        sum += 0.5 * (r * s.c.x * (std::sin(t1) - std::sin(t0)) - r * s.c.y * (std::cos(t1) - std::cos(t0)) +
                      r * r * sweep);
    }
    return sum;
}

// The section of averaged points of two loops matched segment by segment
// (arcs of equal sweep).
std::vector<Seg> middle(const std::vector<Seg>& p, const std::vector<Seg>& q) {
    std::vector<Seg> m;
    for (std::size_t i = 0; i < p.size(); ++i) {
        Seg s;
        s.arc = p[i].arc;
        s.ccw = p[i].ccw;
        s.a = {(p[i].a.x + q[i].a.x) / 2, (p[i].a.y + q[i].a.y) / 2};
        s.b = {(p[i].b.x + q[i].b.x) / 2, (p[i].b.y + q[i].b.y) / 2};
        s.c = {(p[i].c.x + q[i].c.x) / 2, (p[i].c.y + q[i].c.y) / 2};
        m.push_back(s);
    }
    return m;
}

double prismatoid(const Section& s0, const Section& s1) {
    return (s1.z - s0.z) / 6.0 * (area(s0.loop) + 4.0 * area(middle(s0.loop, s1.loop)) + area(s1.loop));
}

double frustum(double r1, double r2, double h) {
    return pi * h / 3.0 * (r1 * r1 + r1 * r2 + r2 * r2);
}

TopoDS_Wire wireOf(const Section& s, int shift) {
    BRepBuilderAPI_MakeWire wire;
    if (s.isCircle) {
        gp_Dir x = s.xdir;
        for (int i = 0; i < shift; ++i) {
            x = s.normal.Crossed(x);
        }
        wire.Add(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(s.center, s.normal, x), s.radius)).Edge());
        return wire.Wire();
    }
    const std::size_t n = s.loop.size();
    std::vector<TopoDS_Vertex> v;
    for (const Seg& seg : s.loop) {
        v.push_back(BRepBuilderAPI_MakeVertex(gp_Pnt(seg.a.x, seg.a.y, s.z)));
    }
    std::vector<TopoDS_Edge> edges;
    for (std::size_t i = 0; i < n; ++i) {
        const Seg& seg = s.loop[i];
        const TopoDS_Vertex& a = v[i];
        const TopoDS_Vertex& b = v[(i + 1) % n];
        if (!seg.arc) {
            edges.push_back(BRepBuilderAPI_MakeEdge(a, b));
        } else {
            const double r = std::hypot(seg.a.x - seg.c.x, seg.a.y - seg.c.y);
            const gp_Circ c(gp_Ax2(gp_Pnt(seg.c.x, seg.c.y, s.z), seg.ccw ? gp_Dir(0, 0, 1) : gp_Dir(0, 0, -1)), r);
            edges.push_back(BRepBuilderAPI_MakeEdge(c, a, b));
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        wire.Add(edges[(i + shift) % n]);
    }
    return wire.Wire();
}

struct Case {
    std::vector<Section> sections;
    double expected = 0.0; // analytic volume, 0 if none
    std::string formula;
};

bool makeCase(const std::string& name, Case& c) {
    const std::string prismatoidFormula = "h/6 (A0 + 4 Am + A1)";
    if (name == "rect-equal") {           // 10 x 20 at z = 0 and 100: a prism
        c.sections = {rectangle(0, 0, 0, 10, 20), rectangle(0, 0, 100, 10, 20)};
        c.expected = 20000;
        c.formula = "A h = 200 x 100";
    } else if (name == "circle-equal") {  // r 5 at z = 0 and 100: a cylinder
        c.sections = {circle({0, 0, 0}, 5), circle({0, 0, 100}, 5)};
        c.expected = 2500 * pi;
        c.formula = "pi r^2 h";
    } else if (name == "frustum") {       // r 10 -> r 5 over 30
        c.sections = {circle({0, 0, 0}, 10), circle({0, 0, 30}, 5)};
        c.expected = frustum(10, 5, 30);
        c.formula = "pi h/3 (r1^2 + r1 r2 + r2^2)";
    } else if (name == "rect-frustum") {  // 20 x 10 -> 10 x 5 over 30 (similar)
        c.sections = {rectangle(0, 0, 0, 20, 10), rectangle(0, 0, 30, 10, 5)};
        c.expected = 30.0 / 3.0 * (200 + 50 + std::sqrt(200.0 * 50.0));
        c.formula = "h/3 (A1 + A2 + sqrt(A1 A2))";
    } else if (name == "rect-prismatoid") { // 20 x 10 -> 10 x 20 over 30
        c.sections = {rectangle(0, 0, 0, 20, 10), rectangle(0, 0, 30, 10, 20)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "three-circles") { // r 5, 10, 5 at z 0, 50, 100
        c.sections = {circle({0, 0, 0}, 5), circle({0, 0, 50}, 10), circle({0, 0, 100}, 5)};
        c.expected = 2 * frustum(5, 10, 50);
        c.formula = "two frustums";
    } else if (name == "four-circles") {  // r 10, 6, 8, 4 at z 0, 20, 35, 60
        c.sections = {circle({0, 0, 0}, 10), circle({0, 0, 20}, 6), circle({0, 0, 35}, 8), circle({0, 0, 60}, 4)};
        c.expected = frustum(10, 6, 20) + frustum(6, 8, 15) + frustum(8, 4, 25);
        c.formula = "three frustums";
    } else if (name == "offset-circles") { // r 10 at the origin -> r 5 at (10, 0, 30)
        c.sections = {circle({0, 0, 0}, 10), circle({10, 0, 30}, 5)};
        c.expected = frustum(10, 5, 30);
        c.formula = "oblique frustum (Cavalieri)";
    } else if (name == "twisted-square") { // square 20 -> the same square turned 30 deg
        c.sections = {regular(4, 10 * std::sqrt(2.0), 0, pi / 4), regular(4, 10 * std::sqrt(2.0), 30, pi / 4 + pi / 6)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "hexagons") {      // regular hexagons R 10 -> R 5 over 30
        c.sections = {regular(6, 10, 0), regular(6, 5, 30)};
        const double a1 = 1.5 * std::sqrt(3.0) * 100;
        const double a2 = 1.5 * std::sqrt(3.0) * 25;
        c.expected = 30.0 / 3.0 * (a1 + a2 + std::sqrt(a1 * a2));
        c.formula = "h/3 (A1 + A2 + sqrt(A1 A2))";
    } else if (name == "slots") {         // slot 40 x 20 -> 20 x 10 over 30 (similar)
        c.sections = {slot(0, 0, 0, 10, 5), slot(0, 0, 30, 5, 2.5)};
        const double a1 = 20 * 10 + pi * 25;
        c.expected = 30.0 / 3.0 * (a1 + a1 / 4 + a1 / 2);
        c.formula = "h/3 (A1 + A2 + sqrt(A1 A2))";
    } else if (name == "slot-twist") {    // the slot -> the same slot turned 30 deg
        c.sections = {slot(0, 0, 0, 10, 5), slot(0, 0, 30, 10, 5, pi / 6)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "slot-offset") {   // the slot -> half size, moved by (7, 3)
        c.sections = {slot(0, 0, 0, 10, 5), slot(7, 3, 30, 5, 2.5)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "slot-tiny") {     // the slot -> a tenth of its size
        c.sections = {slot(0, 0, 0, 10, 5), slot(0, 0, 30, 1, 0.5)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "rounded") {       // rounded 40 x 20 r 4 -> 20 x 10 r 2 (similar)
        c.sections = {rounded(40, 20, 4, 0), rounded(20, 10, 2, 30)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "rounded-twist") { // rounded 40 x 20 r 4 -> 28 x 14 r 2.8 turned 20 deg
        c.sections = {rounded(40, 20, 4, 0), rounded(28, 14, 2.8, 30, pi / 9)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "pacman") {        // pac-man r 10 -> r 5 (a 300 deg arc)
        c.sections = {pacman(0, 0, 0, 10), pacman(0, 0, 30, 5)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "pacman-split") {  // the same with the arcs split into four
        c.sections = {split(pacman(0, 0, 0, 10), 4), split(pacman(0, 0, 30, 5), 4)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "slots-split") {   // the slots with each arc split in two
        c.sections = {split(slot(0, 0, 0, 10, 5), 2), split(slot(0, 0, 30, 5, 2.5), 2)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "pacman-offset") { // pac-man r 10 -> r 5 moved by (3, 1): non-coaxial 300 deg arcs
        c.sections = {pacman(0, 0, 0, 10), pacman(3, 1, 30, 5)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "pacman-twist") {  // pac-man r 10 -> r 7 turned 25 deg about the origin, centre moved
        Section b = pacman(2, -1, 30, 7);
        for (Seg& s : b.loop) { s.a = turn(s.a, 0.436); s.b = turn(s.b, 0.436); s.c = turn(s.c, 0.436); }
        c.sections = {pacman(0, 0, 0, 10), b};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = prismatoidFormula;
    } else if (name == "halfdisc-turn") { // half disc r 5 -> the same turned 90 deg about the arc's centre
        c.sections = {halfDisc(5, pi, 0), halfDisc(5, pi / 2, 30)};
        c.expected = prismatoid(c.sections[0], c.sections[1]);
        c.formula = "h A (2 + cos 90) / 3";
    } else if (name == "flipped") {       // frustum whose top circle faces -Z (normal reversed)
        c.sections = {circle({0, 0, 0}, 10), circle({0, 0, 30}, 5, gp_Dir(0, 0, -1))};
        c.expected = frustum(10, 5, 30);
        c.formula = "pi h/3 (r1^2 + r1 r2 + r2^2)";
    } else if (name == "circle-square") { // different topology
        c.sections = {circle({0, 0, 0}, 10), rectangle(0, 0, 30, 20, 20)};
    } else if (name == "tilted") {        // r 10 on z = 0 -> r 5 on a plane tilted 10 deg
        const double t = 10 * pi / 180;
        c.sections = {circle({0, 0, 0}, 10), circle({0, 0, 30}, 5, gp_Dir(0, -std::sin(t), std::cos(t)))};
    } else if (name == "coincident") {    // two sections on one plane
        c.sections = {circle({0, 0, 0}, 10), circle({0, 0, 0}, 5)};
    } else if (name == "back") {          // z 0, 50, 20: the third section goes back
        c.sections = {circle({0, 0, 0}, 10), circle({0, 0, 50}, 8), circle({0, 0, 20}, 5)};
    } else if (name == "bowtie") {        // a square to itself, each corner matched to the opposite one
        c.sections = {regular(4, 10 * std::sqrt(2.0), 0, pi / 4), regular(4, 10 * std::sqrt(2.0), 30, pi / 4 + pi)};
    } else if (name == "dart") {          // concave dart -> the dart turned 180 deg, matched index by index
        c.sections = {polygon({{10, 0}, {-10, 6}, {-4, 0}, {-10, -6}}, 0),
                      polygon({{-10, 0}, {10, -6}, {4, 0}, {10, 6}}, 30)};
    } else {
        return false;
    }
    return true;
}

void report(const TopoDS_Shape& shape, const Case& c, double seconds) {
    int solids = 0;
    for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next()) {
        ++solids;
    }
    std::map<std::string, int> surfaces;
    for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) {
        const char* names[] = {"plane", "cylinder", "cone", "sphere", "torus", "bezier", "bspline",
                               "revolution", "extrusion", "offset", "other"};
        const int type = BRepAdaptor_Surface(TopoDS::Face(e.Current())).GetType();
        ++surfaces[names[type < 11 ? type : 10]];
    }
    const bool valid = BRepCheck_Analyzer(shape).IsValid();
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props, 1e-10, true);
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    const gp_Pnt g = props.CentreOfMass();
    std::printf("done solids=%d valid=%d V=%.17g", solids, valid ? 1 : 0, props.Mass());
    if (c.expected != 0.0) {
        std::printf(" expected=%.17g (%s) rel=%.2e", c.expected, c.formula.c_str(),
                    std::abs(props.Mass() - c.expected) / c.expected);
    }
    std::printf(" box=(%.9g,%.9g,%.9g)-(%.9g,%.9g,%.9g) centre=(%.9g,%.9g,%.9g) t=%.3fs faces:", x0, y0, z0, x1, y1,
                z1, g.X(), g.Y(), g.Z(), seconds);
    for (const auto& [n, k] : surfaces) {
        std::printf(" %s=%d", n.c_str(), k);
    }
    const auto t0 = std::chrono::steady_clock::now();
    BRepAlgoAPI_Check check(shape, /*bTestSE=*/false, /*bTestSI=*/true);
    const double tc = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf(" selfcheck=%d (%.3fs)\n", check.IsValid() ? 1 : 0, tc);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::printf("usage: %s <case> <ruled|ruled-cc|smooth|shifted>\n", argv[0]);
        return 2;
    }
    Case c;
    if (!makeCase(argv[1], c)) {
        std::printf("unknown case\n");
        return 2;
    }
    const std::string builder = argv[2];
    try {
        const auto t0 = std::chrono::steady_clock::now();
        const bool ruled = builder != "smooth";
        BRepOffsetAPI_ThruSections loft(/*isSolid=*/true, ruled);
        loft.CheckCompatibility(builder == "ruled-cc");
        for (std::size_t i = 0; i < c.sections.size(); ++i) {
            loft.AddWire(wireOf(c.sections[i], builder == "shifted" && i > 0 ? 1 : 0));
        }
        loft.Build();
        if (!loft.IsDone()) {
            std::printf("not done\n");
            return 1;
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        report(loft.Shape(), c, seconds);
    } catch (const Standard_Failure& failure) {
        std::printf("Standard_Failure: %s\n", failure.what());
        return 1;
    }
    return 0;
}
