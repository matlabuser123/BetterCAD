#include <bettercad/core/geometry/Shell.hpp>

#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctFaceNames.hpp"
#include "core/geometry/occt/OcctGuard.hpp"

#include <bettercad/core/units/Format.hpp>

#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffset_Error.hxx>
#include <GeomAbs_JoinType.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <format>
#include <string>
#include <string_view>

namespace bettercad::geometry {

namespace {

// The kernel's coincidence tolerance, in model units (mm): its default
// precision.
constexpr double kOffsetTolerance = 1e-7;

std::string_view errorText(BRepOffset_Error error) {
    switch (error) {
    case BRepOffset_NoError:
        return "no error reported";
    case BRepOffset_UnknownError:
        return "unknown error";
    case BRepOffset_BadNormalsOnGeometry:
        return "bad normals on the geometry";
    case BRepOffset_C0Geometry:
        return "geometry that is only C0";
    case BRepOffset_NullOffset:
        return "null offset";
    case BRepOffset_NotConnectedShell:
        return "the faces are not connected";
    case BRepOffset_CannotTrimEdges:
        return "cannot trim edges";
    case BRepOffset_CannotFuseVertices:
        return "cannot fuse vertices";
    case BRepOffset_CannotExtentEdge:
        return "cannot extend an edge";
    case BRepOffset_UserBreak:
        return "interrupted";
    case BRepOffset_MixedConnectivity:
        return "faces meet both sharply and smoothly along one edge";
    }
    return "unrecognized error";
}

/// "walls 5 mm thick inward"
std::string wallText(const ShellRequest& request) {
    return std::format("walls {} thick {}", toString(request.thickness, units::mm), toString(request.side));
}

Result<Body> failed(const ShellRequest& request, std::string_view reason) {
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("shell: the kernel cannot build {} on this body: {}; walls thicker than half the "
                                 "body where it is thinnest, or an inward wall at least as thick as a round it "
                                 "follows, cannot be built",
                                 wallText(request), reason));
}

} // namespace

Result<Body> shellBody(const Body& body, const ShellRequest& request) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "shell: the body is empty");
    }
    if (auto valid = validate(request); !valid) {
        return makeError(ErrorCode::InvalidArgument, std::format("shell: {}", valid.error().message));
    }
    const std::size_t solids = body.topology().solids;
    if (solids != 1) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("shell: a shell hollows one solid; the body has {}", solids));
    }
    const auto before = body.massProperties();
    if (!before) {
        return makeError(before.error().code, std::format("shell: {}", before.error().message));
    }

    return occt::guardKernelCall("shell", [&]() -> Result<Body> {
        // The open faces, each once, in the order their names are given.
        occt::ShapeMap open;
        for (std::size_t i = 0; i < request.openFaces.size(); ++i) {
            bool found = false;
            for (const occt::NamedFace& named : occt::BodyAccess::names(body)) {
                if (named.name == request.openFaces[i]) {
                    open.Add(named.face);
                    found = true;
                }
            }
            if (!found) {
                return makeError(ErrorCode::NotFound,
                                 std::format("shell: open face {} is not a face of the body", i + 1));
            }
        }
        TopoDS_Shape solid;
        for (TopExp_Explorer it(*shape, TopAbs_SOLID); it.More(); it.Next()) {
            solid = it.Current();
        }
        occt::ShapeList closing;
        for (int i = 1; i <= open.Extent(); ++i) {
            closing.Append(open(i));
        }

        const double offset = occt::toModel(request.thickness);
        BRepOffsetAPI_MakeThickSolid maker;
        try {
            maker.MakeThickSolidByJoin(solid, closing, request.side == ShellSide::Inward ? -offset : offset,
                                       kOffsetTolerance, BRepOffset_Skin, false, false, GeomAbs_Intersection);
        } catch (const Standard_Failure& failure) {
            return failed(request, std::format("kernel: {}: {}", failure.ExceptionType(), failure.what()));
        }
        if (!maker.IsDone()) {
            return failed(request, std::format("kernel: {}", errorText(maker.MakeOffset().Error())));
        }
        const TopoDS_Shape result = maker.Shape();

        // The kernel's own success is not enough (see shellBody()).
        const auto notShell = [&](std::string_view reason) {
            return failed(request, std::format("its result is not a shell of the body: {}", reason));
        };
        occt::ShapeMap resultFaces;
        TopExp::MapShapes(result, TopAbs_FACE, resultFaces);
        std::size_t withoutWall = 0;
        std::size_t openLeft = 0;
        occt::ShapeMap faces;
        TopExp::MapShapes(solid, TopAbs_FACE, faces);
        for (int i = 1; i <= faces.Extent(); ++i) {
            const TopoDS_Shape& face = faces(i);
            if (open.Contains(face)) {
                if (resultFaces.Contains(face)) {
                    ++openLeft;
                }
                continue;
            }
            bool wall = false;
            for (const TopoDS_Shape& image : maker.Generated(face)) {
                wall = wall || (image.ShapeType() == TopAbs_FACE && resultFaces.Contains(image));
            }
            if (maker.IsDeleted(face) || !wall) {
                ++withoutWall;
            }
        }
        if (withoutWall != 0) {
            return notShell(std::format("{} of the {} remaining faces {} no wall", withoutWall,
                                        faces.Extent() - open.Extent(), withoutWall == 1 ? "has" : "have"));
        }
        if (openLeft != 0) {
            return notShell(std::format("{} open face{} still there", openLeft, openLeft == 1 ? " is" : "s are"));
        }
        Body shelled = occt::BodyAccess::makeBody(result, occt::carriedNames(maker, result, {&body}));
        if (shelled.isEmpty() || !shelled.isValid()) {
            return notShell("it is not a valid solid");
        }
        if (const std::size_t count = shelled.topology().solids; count != 1) {
            return notShell(std::format("it has {} solids", count));
        }
        const auto after = shelled.massProperties();
        if (!after || !isFinite(after->volume) || !(after->volume > Volume{}) || !isFinite(after->surfaceArea)) {
            return makeError(ErrorCode::Internal,
                             "shell: the kernel produced a solid without finite positive volume and area");
        }
        if (request.side == ShellSide::Inward && !(after->volume < before->volume)) {
            return notShell("it removes no material");
        }
        return shelled;
    });
}

} // namespace bettercad::geometry
