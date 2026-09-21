#include <bettercad/assembly/Component.hpp>
#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/core/geometry/Exchange.hpp>
#include <bettercad/core/geometry/Transform.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/io/ModelExport.hpp>

#include "io/FileIo.hpp"

#include <format>
#include <map>
#include <utility>
#include <vector>

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

/// What an assembly export is made of: the distinct parts, and where each
/// active component puts one.
struct PlacedAssembly {
    geometry::StepAssembly shapes{};
    /// Parallel to shapes.instances, in the same order.
    std::vector<ExportedComponent> components{};
    /// Parallel to shapes.parts, in the same order.
    std::vector<ObjectId> partObjects{};
};

std::string nameOf(const Document& document, ObjectId id) {
    const auto name = document.nameOf(id);
    return name ? std::string{*name} : std::format("{}", id);
}

/// Regenerates @p document with the assembly handlers registered and collects
/// every active component's part and solved transform.
///
/// The transform comes from Regenerator::transforms(), which is what ADR-008's
/// final pass published for this pass -- so it is the SOLVED position, and it
/// is never stale: a pass that could not solve publishes none, and a component
/// without one is a failure here rather than a component exported where its
/// intent happened to put it.
Result<PlacedAssembly> placedAssembly(const Document& document) {
    Document copy = document.clone();
    features::Regenerator regenerator;
    assembly::AssemblyRegeneration pass;
    assembly::registerHandlers(regenerator, nullptr, &pass);
    auto report = regenerator.regenerateAll(copy);
    if (!report) {
        return std::unexpected(report.error());
    }
    if (!report->succeeded()) {
        std::string problems;
        for (const auto& [id, error] : report->errors) {
            problems += std::format("\n  {}: {}", nameOf(copy, id), error.message);
        }
        for (const ObjectId id : report->blocked) {
            problems += std::format("\n  {}: not regenerated (depends on a failed item)", nameOf(copy, id));
        }
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("the assembly does not regenerate:{}", problems));
    }

    const std::vector<ComponentId> active = assembly::activeComponents(copy);
    if (active.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("document '{}' has no components in force to export", copy.name()));
    }

    PlacedAssembly placed;
    placed.shapes.name = copy.name();
    // One product per distinct part, in the order the parts are first
    // placed. That is deterministic because the walk below is: components
    // come back in ascending ID order, and the same document therefore
    // always writes the same products in the same order. (It is NOT
    // ascending part-object order -- a component with a low ID placing a
    // part with a high one puts that part first, and AssemblyStep_
    // ProductOrderFollowsFirstPlacement pins exactly that.)
    std::map<ObjectId, std::size_t> partIndex;
    for (const ComponentId id : active) {
        const assembly::Component* component = assembly::findComponent(copy, id);
        if (component == nullptr) {
            return makeError(ErrorCode::Internal, std::format("{} is active but not present", id));
        }
        const ObjectReference& reference = component->definition().part;
        if (reference.document) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} ('{}') places a part in another document, which this export "
                                         "cannot resolve",
                                         id, component->name()));
        }
        const geometry::Body* body = regenerator.body(reference.object);
        if (body == nullptr || body->isEmpty()) {
            return makeError(ErrorCode::NotFound,
                             std::format("{} ('{}') places {}, which produced no body", id, component->name(),
                                         nameOf(copy, reference.object)));
        }
        const RigidTransform3D* transform = regenerator.transform(id);
        if (transform == nullptr) {
            // ADR-005: a transform that is one edit out of date is worse than
            // none, so the pass publishes none rather than a stale one. An
            // export has nothing honest to write here.
            return makeError(ErrorCode::FailedPrecondition,
                             std::format("{} ('{}') has no solved transform; the assembly did not solve ({})",
                                         id, component->name(), toString(pass.trigger)));
        }
        auto [it, inserted] = partIndex.try_emplace(reference.object, placed.shapes.parts.size());
        if (inserted) {
            placed.shapes.parts.push_back({nameOf(copy, reference.object), *body});
            placed.partObjects.push_back(reference.object);
        }
        placed.shapes.instances.push_back(
            {.name = component->name(), .part = it->second, .placement = *transform});
        placed.components.push_back(
            {.component = id, .part = reference.object, .name = component->name()});
    }
    return placed;
}

/// Whether @p document is an assembly for export purposes: it has at least
/// one component, in force or not. A document whose every component is
/// suppressed is still an assembly, and exporting it is a failure with a
/// reason rather than a silent fall back to exporting its parts unplaced.
bool isAssembly(const Document& document) {
    return !assembly::components(document).empty();
}

} // namespace

Result<ExportSummary> exportStep(const Document& document, const std::filesystem::path& path) {
    const geometry::StepOptions options{
        .name = detail::displayPath(path.filename()),
        .author = document.metadata().author,
    };

    ExportSummary summary;
    Result<std::string> step = makeError(ErrorCode::Internal, "unreachable");
    if (isAssembly(document)) {
        auto placed = placedAssembly(document);
        if (!placed) {
            return std::unexpected(placed.error());
        }
        for (std::size_t i = 0; i < placed->shapes.parts.size(); ++i) {
            summary.bodies.push_back(
                {.feature = placed->partObjects[i], .name = placed->shapes.parts[i].name});
        }
        summary.components = std::move(placed->components);
        step = geometry::writeStepAssembly(placed->shapes, options);
    } else {
        auto bodies = bodiesToExport(document);
        if (!bodies) {
            return std::unexpected(bodies.error());
        }
        std::vector<geometry::NamedBody> named;
        for (const features::ResultBody& body : *bodies) {
            named.push_back({body.name, body.body});
            summary.bodies.push_back({.feature = body.feature, .name = body.name});
        }
        step = geometry::writeStep(named, options);
    }
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
    std::vector<geometry::Mesh> meshes;
    ExportSummary summary;
    if (isAssembly(document)) {
        auto placed = placedAssembly(document);
        if (!placed) {
            return std::unexpected(placed.error());
        }
        // STL carries no product structure, so each instance is triangulated
        // where it sits. The repetition is the format's, not this path's.
        for (std::size_t i = 0; i < placed->shapes.instances.size(); ++i) {
            const geometry::StepInstance& instance = placed->shapes.instances[i];
            auto moved = geometry::transformed(placed->shapes.parts[instance.part].body, instance.placement);
            if (!moved) {
                return makeError(moved.error().code,
                                 std::format("{}: {}", instance.name, moved.error().message));
            }
            auto mesh = geometry::triangulate(*moved, options.mesh);
            if (!mesh) {
                return makeError(mesh.error().code, std::format("{}: {}", instance.name, mesh.error().message));
            }
            summary.components.push_back(placed->components[i]);
            summary.bodies.push_back({.feature = placed->components[i].part,
                                      .name = instance.name,
                                      .triangles = mesh->triangles.size()});
            meshes.push_back(std::move(*mesh));
        }
    } else {
        auto bodies = bodiesToExport(document);
        if (!bodies) {
            return std::unexpected(bodies.error());
        }
        for (const features::ResultBody& body : *bodies) {
            auto mesh = geometry::triangulate(body.body, options.mesh);
            if (!mesh) {
                return makeError(mesh.error().code, std::format("{}: {}", body.name, mesh.error().message));
            }
            summary.bodies.push_back(
                {.feature = body.feature, .name = body.name, .triangles = mesh->triangles.size()});
            meshes.push_back(std::move(*mesh));
        }
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
