#include <bettercad/core/geometry/Draft.hpp>

#include "core/geometry/occt/OcctBody.hpp"
#include "core/geometry/occt/OcctFaceNames.hpp"
#include "core/geometry/occt/OcctGuard.hpp"
#include "core/geometry/occt/OcctTopology.hpp"

#include <bettercad/core/geometry/Faces.hpp>

#include <BRepAlgoAPI_Check.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <Draft_ErrorStatus.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::geometry {

namespace {

std::string_view statusText(Draft_ErrorStatus status) {
    switch (status) {
    case Draft_NoError:
        return "no error reported";
    case Draft_FaceRecomputation:
        return "a face cannot be recomputed";
    case Draft_EdgeRecomputation:
        return "an edge cannot be recomputed";
    case Draft_VertexRecomputation:
        return "a vertex cannot be recomputed";
    }
    return "unrecognized status";
}

/// "a sphere", "a torus", "another kind of surface".
std::string_view surfaceText(FaceSurface surface) {
    switch (surface) {
    case FaceSurface::Plane:
        return "a plane";
    case FaceSurface::Cylinder:
        return "a cylinder";
    case FaceSurface::Cone:
        return "a cone";
    case FaceSurface::Sphere:
        return "a sphere";
    case FaceSurface::Torus:
        return "a torus";
    case FaceSurface::Other:
        break;
    }
    return "another kind of surface";
}

std::size_t countOf(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
    occt::ShapeMap map;
    TopExp::MapShapes(shape, type, map);
    return static_cast<std::size_t>(map.Extent());
}

/// "faces 6, edges 12, vertices 8"
std::string topologyText(const TopoDS_Shape& shape) {
    return std::format("faces {}, edges {}, vertices {}", countOf(shape, TopAbs_FACE), countOf(shape, TopAbs_EDGE),
                       countOf(shape, TopAbs_VERTEX));
}

Result<Body> cannotBuild(std::string_view reason) {
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("draft: the kernel cannot build the draft: {}; an angle this large may make faces "
                                 "vanish",
                                 reason));
}

Result<Body> cannotTurn(std::size_t index, std::string_view reason) {
    return makeError(ErrorCode::FailedPrecondition,
                     std::format("draft: face {} cannot be turned about the neutral plane ({}); a face parallel to "
                                 "the plane, or a chain of tangent faces that reaches one, cannot be drafted",
                                 index + 1, reason));
}

} // namespace

Result<Body> draftFaces(const Body& body, const DraftRequest& request) {
    const TopoDS_Shape* shape = occt::BodyAccess::shape(body);
    if (shape == nullptr) {
        return makeError(ErrorCode::FailedPrecondition, "draft: the body is empty");
    }
    if (auto valid = validate(request); !valid) {
        return makeError(ErrorCode::InvalidArgument, std::format("draft: {}", valid.error().message));
    }
    const std::size_t solids = body.topology().solids;
    if (solids != 1) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("draft: a draft turns faces of one solid; the body has {}", solids));
    }

    return occt::guardKernelCall("draft", [&]() -> Result<Body> {
        // The faces each name is on, in the order the names are given.
        const std::vector<occt::KernelFace> faces = occt::kernelFaces(body);
        std::vector<std::pair<std::size_t, TopoDS_Face>> turned;
        for (std::size_t i = 0; i < request.faces.size(); ++i) {
            bool found = false;
            for (const occt::KernelFace& face : faces) {
                if (!std::ranges::binary_search(face.info.names, request.faces[i])) {
                    continue;
                }
                found = true;
                const FaceSurface surface = face.info.surface;
                if (surface != FaceSurface::Plane && surface != FaceSurface::Cylinder &&
                    surface != FaceSurface::Cone) {
                    return makeError(ErrorCode::FailedPrecondition,
                                     std::format("draft: face {} is {}; only planes, cylinders and cones can be "
                                                 "drafted",
                                                 i + 1, surfaceText(surface)));
                }
                turned.emplace_back(i, face.face);
            }
            if (!found) {
                return makeError(ErrorCode::NotFound, std::format("draft: face {} is not a face of the body", i + 1));
            }
        }
        TopoDS_Shape solid;
        for (TopExp_Explorer it(*shape, TopAbs_SOLID); it.More(); it.Next()) {
            solid = it.Current();
        }

        const gp_Dir pull = occt::toModel(request.neutralPlane.normal());
        const gp_Pln neutral(occt::toModel(request.neutralPlane.origin()), pull);
        const double angle = request.angle.si();
        BRepOffsetAPI_DraftAngle draft(solid);
        for (const auto& [index, face] : turned) {
            try {
                draft.Add(face, pull, angle, neutral);
            } catch (const Standard_Failure& failure) {
                return cannotTurn(index, std::format("kernel: {}: {}", failure.ExceptionType(), failure.what()));
            }
            if (!draft.AddDone()) {
                return cannotTurn(index, std::format("kernel: {}", statusText(draft.Status())));
            }
        }
        try {
            draft.Build();
        } catch (const Standard_Failure& failure) {
            return cannotBuild(std::format("kernel: {}: {}", failure.ExceptionType(), failure.what()));
        }
        if (!draft.IsDone()) {
            return cannotBuild(std::format("kernel: {}", statusText(draft.Status())));
        }
        const TopoDS_Shape result = draft.Shape();

        // The kernel's own success is not enough (see draftFaces()).
        const auto notDraft = [](std::string_view reason) {
            return cannotBuild(std::format("its result is not a draft of the body: {}", reason));
        };
        if (result.IsNull() || countOf(result, TopAbs_SOLID) != 1) {
            return notDraft(std::format("it has {} solids", result.IsNull() ? 0 : countOf(result, TopAbs_SOLID)));
        }
        if (const std::string before = topologyText(solid), after = topologyText(result); before != after) {
            return notDraft(std::format("its topology changed from {} to {}", before, after));
        }
        occt::ShapeMap resultFaces;
        TopExp::MapShapes(result, TopAbs_FACE, resultFaces);
        occt::ShapeMap inputFaces;
        TopExp::MapShapes(solid, TopAbs_FACE, inputFaces);
        std::size_t lost = 0;
        for (int i = 1; i <= inputFaces.Extent(); ++i) {
            const TopoDS_Shape image = draft.ModifiedShape(inputFaces(i));
            if (image.IsNull() || image.ShapeType() != TopAbs_FACE || !resultFaces.Contains(image)) {
                ++lost;
            }
        }
        if (lost != 0) {
            return notDraft(std::format("{} of its {} faces {} no image", lost, inputFaces.Extent(),
                                        lost == 1 ? "has" : "have"));
        }

        std::vector<occt::NamedFace> names;
        for (const occt::NamedFace& named : occt::BodyAccess::names(body)) {
            const TopoDS_Shape image = draft.ModifiedShape(named.face);
            names.push_back({TopoDS::Face(image), named.name});
        }
        Body drafted = occt::BodyAccess::makeBody(result, occt::canonicalNames(result, std::move(names)));
        if (!drafted.isValid()) {
            return notDraft("it is not a valid solid");
        }
        if (!BRepAlgoAPI_Check(result, /*bTestSE=*/false, /*bTestSI=*/true).IsValid()) {
            return notDraft("it intersects itself");
        }
        const auto properties = drafted.massProperties();
        if (!properties || !isFinite(properties->volume) || !(properties->volume > Volume{}) ||
            !isFinite(properties->surfaceArea)) {
            return makeError(ErrorCode::Internal,
                             "draft: the kernel produced a solid without finite positive volume and area");
        }
        return drafted;
    });
}

} // namespace bettercad::geometry
