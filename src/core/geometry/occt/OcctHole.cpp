#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctGuard.hpp"
#include "core/geometry/occt/OcctTopology.hpp"

#include <bettercad/core/geometry/Booleans.hpp>
#include <bettercad/core/geometry/Hole.hpp>
#include <bettercad/core/geometry/Profile.hpp>
#include <bettercad/core/geometry/Sweeps.hpp>

#include <BRepClass_FaceClassifier.hxx>
#include <BRepIntCurveSurface_Inter.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <TopAbs_State.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace bettercad::geometry {

namespace {

// Room the hole's entry must leave inside its face, and a blind hole or head
// before the material ends, in model units (mm); as for chamfers and fillets.
constexpr double kMarginMm = 1e-3;
// The cutter starts this far outside the face, so its entry is never
// coplanar with the face (mm).
constexpr double kEntryLeadMm = 0.01;
// A through hole's cutter runs this far past the body's bounding box (mm).
constexpr double kThroughOverrunMm = 1.0;
// A blind hole must remove its own volume to within this fraction of the
// body's volume; less means it broke into another cavity or face.
constexpr double kContainmentTolerance = 1e-9;

double mm(Length value) {
    return value.in(units::mm);
}

/// The cutter's section in the plane of the axis: (radius, depth) in mm,
/// depth measured into the material from the face.
std::vector<std::pair<double, double>> sectionOf(const HoleRequest& request, double bottomMm) {
    const double r = mm(request.diameter) / 2.0;
    std::vector<std::pair<double, double>> section{{0.0, -kEntryLeadMm}};
    switch (request.type) {
    case HoleType::Simple:
        section.emplace_back(r, -kEntryLeadMm);
        break;
    case HoleType::Counterbore: {
        const double rb = mm(request.counterboreDiameter) / 2.0;
        section.emplace_back(rb, -kEntryLeadMm);
        section.emplace_back(rb, mm(request.counterboreDepth));
        section.emplace_back(r, mm(request.counterboreDepth));
        break;
    }
    case HoleType::Countersink: {
        const double rs = mm(request.countersinkDiameter) / 2.0;
        section.emplace_back(rs, -kEntryLeadMm);
        section.emplace_back(rs, 0.0);
        section.emplace_back(r, mm(countersinkDepth(request)));
        break;
    }
    }
    section.emplace_back(r, bottomMm);
    section.emplace_back(0.0, bottomMm);
    return section;
}

/// Volume the cutter removes below the face, in mm^3, if all of it is
/// inside the material (a blind hole's own volume).
double cutterVolumeBelowFace(const HoleRequest& request) {
    const double pi = std::numbers::pi;
    const double r = mm(request.diameter) / 2.0;
    double volume = pi * r * r * mm(request.depth);
    if (request.type == HoleType::Counterbore) {
        const double rb = mm(request.counterboreDiameter) / 2.0;
        volume += pi * (rb * rb - r * r) * mm(request.counterboreDepth);
    } else if (request.type == HoleType::Countersink) {
        const double rs = mm(request.countersinkDiameter) / 2.0;
        const double h = mm(countersinkDepth(request));
        volume += pi * h * (rs * rs + rs * r + r * r) / 3.0 - pi * r * r * h; // frustum beyond the cylinder
    }
    return volume;
}

/// Radius of the hole's outline at the face: the head if it has one.
double entryRadiusMm(const HoleRequest& request) {
    switch (request.type) {
    case HoleType::Counterbore:
        return mm(request.counterboreDiameter) / 2.0;
    case HoleType::Countersink:
        return mm(request.countersinkDiameter) / 2.0;
    case HoleType::Simple:
        break;
    }
    return mm(request.diameter) / 2.0;
}

/// Depth of the hole's head below the face, in mm (0 for a simple hole).
double headDepthMm(const HoleRequest& request) {
    switch (request.type) {
    case HoleType::Counterbore:
        return mm(request.counterboreDepth);
    case HoleType::Countersink:
        return mm(countersinkDepth(request));
    case HoleType::Simple:
        break;
    }
    return 0.0;
}

} // namespace

Result<Body> cutHole(const Body& body, const HoleRequest& request) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "hole: the body is empty");
    }
    if (auto valid = validate(request); !valid) {
        return makeError(ErrorCode::InvalidArgument, std::format("hole: {}", valid.error().message));
    }
    const auto before = body.massProperties();
    const auto box = body.boundingBox();
    if (!before || !box) {
        return makeError(ErrorCode::Internal, "hole: cannot measure the body");
    }
    const std::size_t solids = body.topology().solids;

    return occt::guardKernelCall("hole", [&]() -> Result<Body> {
        const std::string plane = describe(request.face);
        const std::string centre =
            std::format("({:.6g}, {:.6g}) mm", mm(request.center.x), mm(request.center.y));
        const Point3D centrePoint = facePoint(request.face, request.center);
        const gp_Pnt c = occt::toModel(centrePoint);
        const Direction3D inward = request.face.normal.reversed();
        const gp_Dir d = occt::toModel(inward);

        // 1. The placement face: the face on the plane, facing its way, under
        //    the centre.
        const std::vector<occt::KernelFace> faces = occt::kernelFaces(*shape);
        const auto onPlane = occt::matchingFaces(faces, request.face);
        if (onPlane.empty()) {
            return makeError(ErrorCode::NotFound,
                             std::format("hole: the placement face ({}) matches no face of the body", plane));
        }
        std::vector<const occt::KernelFace*> under;
        for (const occt::KernelFace* face : onPlane) {
            const BRepClass_FaceClassifier classifier(face->face, c, Precision::Confusion());
            if (classifier.State() == TopAbs_IN || classifier.State() == TopAbs_ON) {
                under.push_back(face);
            }
        }
        if (under.empty()) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("hole: the centre {} is not on a face of the body on the {} ({} face(s) lie on "
                                         "that plane elsewhere)",
                                         centre, plane, onPlane.size()));
        }
        if (under.size() > 1) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("hole: the placement is ambiguous: the centre {} lies on {} faces on the {}",
                                         centre, under.size(), plane));
        }
        const TopoDS_Face& face = under.front()->face;

        // 2. The entry outline must lie inside the face.
        const double entryRadius = entryRadiusMm(request);
        double clearance = std::numeric_limits<double>::infinity();
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
            const TopoDS_Edge& edge = TopoDS::Edge(it.Current());
            if (BRep_Tool::Degenerated(edge)) {
                continue;
            }
            const auto gap = occt::distance(c, edge);
            if (!gap) {
                return makeError(ErrorCode::Internal, "hole: cannot measure the room around the centre");
            }
            clearance = std::min(clearance, *gap);
        }
        if (clearance < entryRadius + kMarginMm) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("hole: the hole does not fit on its face: its entry is {:.6g} mm across, but "
                                         "the centre {} is only {:.6g} mm from the face's edge (a hole must stay at "
                                         "least {:g} mm inside its face)",
                                         2.0 * entryRadius, centre, clearance, kMarginMm));
        }

        // 3. How much material there is along the axis from the face.
        BRepIntCurveSurface_Inter intersections;
        double exit = std::numeric_limits<double>::infinity();
        for (intersections.Init(*shape, gp_Lin(c, d), Precision::Confusion()); intersections.More();
             intersections.Next()) {
            if (intersections.W() > kMarginMm / 2.0) {
                exit = std::min(exit, intersections.W());
            }
        }
        if (!std::isfinite(exit)) {
            return makeError(ErrorCode::Internal, "hole: cannot find where the axis leaves the material");
        }
        const bool blind = request.extent == HoleExtent::Blind;
        if (blind && mm(request.depth) + kMarginMm > exit) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("hole: a blind hole {:.6g} mm deep would reach the far side: there are only "
                                         "{:.6g} mm of material along its axis; make it a through hole",
                                         mm(request.depth), exit));
        }
        if (!blind && request.type != HoleType::Simple && headDepthMm(request) + kMarginMm > exit) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("hole: the {} ({:.6g} mm deep) is as deep as the material along the axis "
                                         "({:.6g} mm)",
                                         toString(request.type), headDepthMm(request), exit));
        }

        // 4. The cutter: the section revolved about the axis. A through
        //    cutter runs past the body's bounding box, however thick it is.
        double bottom = mm(request.depth);
        if (!blind) {
            double far = 0.0;
            for (const double x : {box->min.x.in(units::mm), box->max.x.in(units::mm)}) {
                for (const double y : {box->min.y.in(units::mm), box->max.y.in(units::mm)}) {
                    for (const double z : {box->min.z.in(units::mm), box->max.z.in(units::mm)}) {
                        far = std::max(far, (x - c.X()) * d.X() + (y - c.Y()) * d.Y() + (z - c.Z()) * d.Z());
                    }
                }
            }
            bottom = far + kThroughOverrunMm;
        }
        const Point3D alongU = facePoint(request.face, Point2D{request.center.x + Length::fromSi(1.0), request.center.y});
        const auto radial = Direction3D::fromComponents((alongU.x - centrePoint.x).si(), (alongU.y - centrePoint.y).si(),
                                                        (alongU.z - centrePoint.z).si());
        if (!radial) {
            return makeError(ErrorCode::Internal, "hole: cannot set up the hole's section plane");
        }
        const Direction3D sectionNormal = *Direction3D::fromComponents(
            radial->y() * inward.z() - radial->z() * inward.y(), radial->z() * inward.x() - radial->x() * inward.z(),
            radial->x() * inward.y() - radial->y() * inward.x());
        auto sectionPlane = Frame3D::create(centrePoint, sectionNormal, *radial);
        if (!sectionPlane) {
            return makeError(ErrorCode::Internal, std::format("hole: {}", sectionPlane.error().message));
        }
        PlanarRegion section{.plane = *sectionPlane, .outer = {}, .holes = {}};
        const auto points = sectionOf(request, bottom);
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& [r0, t0] = points[i];
            const auto& [r1, t1] = points[(i + 1) % points.size()];
            section.outer.segments.emplace_back(LineSegment2D{Point2D{r0 * units::mm, t0 * units::mm},
                                                              Point2D{r1 * units::mm, t1 * units::mm}});
        }
        auto cutter = makeRevolution(section, Axis3D{centrePoint, inward}, Angle{}, Angle::fromSi(2.0 * std::numbers::pi));
        if (!cutter) {
            return makeError(cutter.error().code, std::format("hole: {}", cutter.error().message));
        }

        // 5. Subtract it.
        auto result = booleanDifference(body, *cutter);
        if (!result) {
            return makeError(result.error().code, std::format("hole: {}", result.error().message));
        }

        // 6. Check the result.
        if (result->isEmpty() || !result->isValid()) {
            return makeError(ErrorCode::Internal, "hole: the kernel produced an invalid solid");
        }
        if (result->topology().solids != solids) {
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("hole: the hole splits the body into {} solids", result->topology().solids));
        }
        const auto after = result->massProperties();
        if (!after || !isFinite(after->volume) || !(after->volume > Volume{}) || !isFinite(after->surfaceArea)) {
            return makeError(ErrorCode::Internal,
                             "hole: the kernel produced a solid without finite positive volume and area");
        }
        const double volumeBefore = before->volume.in(units::mm3);
        const double removed = volumeBefore - after->volume.in(units::mm3);
        const double tolerance = kContainmentTolerance * volumeBefore;
        if (!(removed > tolerance)) {
            return makeError(ErrorCode::FailedPrecondition, "hole: the hole removes no material");
        }
        if (blind) {
            const double expected = cutterVolumeBelowFace(request);
            if (std::abs(removed - expected) > tolerance) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("hole: the blind hole breaks out of the material: it removes {:.6g} mm^3 "
                                             "instead of its own {:.6g} mm^3",
                                             removed, expected));
            }
        }
        return result;
    });
}

} // namespace bettercad::geometry
