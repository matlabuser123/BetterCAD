#include <bettercad/core/geometry/Exchange.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/io/ModelExport.hpp>

#include "io/FileIo.hpp"

#include <format>

namespace bettercad::io {

namespace {

Result<std::vector<features::ResultBody>> bodiesToExport(const Document& document) {
    auto bodies = features::regenerateResultBodies(document);
    if (!bodies) {
        return std::unexpected(bodies.error());
    }
    if (bodies->empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("document '{}' has no bodies to export", document.name()));
    }
    return bodies;
}

} // namespace

Result<ExportSummary> exportStep(const Document& document, const std::filesystem::path& path) {
    auto bodies = bodiesToExport(document);
    if (!bodies) {
        return std::unexpected(bodies.error());
    }
    std::vector<geometry::NamedBody> named;
    ExportSummary summary;
    for (const features::ResultBody& body : *bodies) {
        named.push_back({body.name, body.body});
        summary.bodies.push_back({.feature = body.feature, .name = body.name});
    }
    const geometry::StepOptions options{
        .name = detail::displayPath(path.filename()),
        .author = document.metadata().author,
    };
    auto step = geometry::writeStep(named, options);
    if (!step) {
        return std::unexpected(step.error());
    }
    if (auto written = detail::writeFileAtomically(path, *step); !written) {
        return std::unexpected(written.error());
    }
    summary.bytes = step->size();
    return summary;
}

Result<ExportSummary> exportStl(const Document& document, const std::filesystem::path& path,
                                const StlExportOptions& options) {
    auto bodies = bodiesToExport(document);
    if (!bodies) {
        return std::unexpected(bodies.error());
    }
    std::vector<geometry::Mesh> meshes;
    ExportSummary summary;
    for (const features::ResultBody& body : *bodies) {
        auto mesh = geometry::triangulate(body.body, options.mesh);
        if (!mesh) {
            return makeError(mesh.error().code, std::format("{}: {}", body.name, mesh.error().message));
        }
        summary.bodies.push_back({.feature = body.feature, .name = body.name, .triangles = mesh->triangles.size()});
        meshes.push_back(std::move(*mesh));
    }
    auto stl = meshesToStl(meshes, options.format, document.name());
    if (!stl) {
        return std::unexpected(stl.error());
    }
    if (auto written = detail::writeFileAtomically(path, *stl); !written) {
        return std::unexpected(written.error());
    }
    summary.bytes = stl->size();
    return summary;
}

} // namespace bettercad::io
