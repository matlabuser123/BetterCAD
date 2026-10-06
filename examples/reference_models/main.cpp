// Builds every BetterCAD reference model, regenerates it and prints its
// fingerprint and timings. With --out <dir>, also writes each model as a
// .bcad file (as built, before regeneration) with its STEP and STL exports.
//
//   bettercad_example_reference_models [--out <dir>]
#include "AssemblyReferenceModels.hpp"
#include "DrawingReferenceModels.hpp"
#include "MaterialReferenceModels.hpp"
#include "MeshReferenceModels.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/DocumentObject.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/drawing/SheetScene.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/MassProperties.hpp>
#include <bettercad/features/Material.hpp>
#include <bettercad/features/Materials.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/DrawingExport.hpp>
#include <bettercad/io/ModelExport.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/MeshQuality.hpp>
#include <bettercad/meshing/Mesher.hpp>
#include <bettercad/meshing/VolumeMesh.hpp>

#include <chrono>
#include <cmath>
#include <optional>
#include <cstddef>
#include <cstdio>
#include <span>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace bettercad;

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

int fail(std::string_view model, std::string_view step, const Error& error) {
    std::fprintf(stderr, "%.*s: %.*s failed: %s\n", static_cast<int>(model.size()), model.data(),
                 static_cast<int>(step.size()), step.data(), error.message.c_str());
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path out;
    // Which suites to run. All three by default; --drawings runs the drawing
    // suite alone, which is what the CLI reference tests need and is seconds
    // rather than a minute, because it skips twelve parts' STEP and STL
    // exports and eight assembly solves.
    bool parts = true;
    bool assemblies = true;
    bool drawings = true;
    bool materials = true;
    bool meshes = true;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            out = argv[++i];
        } else if (arg == "--drawings") {
            parts = false;
            assemblies = false;
            materials = false;
            meshes = false;
        } else if (arg == "--materials") {
            // P15-REFMOD-001. The material suite alone, which is what its CLI
            // fixture needs: seconds, rather than the minute the twelve parts'
            // STEP and STL exports and the eight assembly solves cost.
            parts = false;
            assemblies = false;
            drawings = false;
            meshes = false;
        } else if (arg == "--meshes") {
            // P16-REFMOD-001. The meshing suite alone. Its CLI fixture needs
            // the nine .bcad files and nothing else, and meshing them is the
            // expensive part of this runner -- so a fixture that also built
            // twelve parts' exports and eight assembly solves would pay twice.
            parts = false;
            assemblies = false;
            drawings = false;
            materials = false;
        } else {
            std::fprintf(stderr, "usage: %s [--out <dir>] [--drawings] [--materials] [--meshes]\n",
                         argv[0]);
            return 2;
        }
    }
    if (!out.empty()) {
        std::filesystem::create_directories(out);
    }

    for (const reference::ReferenceModelInfo& info :
         parts ? std::span{reference::kReferenceModels}
               : std::span<const reference::ReferenceModelInfo>{}) {
        const auto buildStart = Clock::now();
        auto document = reference::buildReferenceModel(info.kind);
        const double buildMs = millisecondsSince(buildStart);
        if (!document) {
            return fail(info.name, "build", document.error());
        }
        if (!out.empty()) {
            const auto path = out / (std::string{info.fileStem} + ".bcad");
            if (auto saved = io::saveDocument(*document, path); !saved) {
                return fail(info.name, "save", saved.error());
            }
        }

        features::Regenerator regenerator;
        const auto firstStart = Clock::now();
        auto first = regenerator.regenerate(*document);
        const double firstMs = millisecondsSince(firstStart);
        if (!first) {
            return fail(info.name, "regenerate", first.error());
        }
        if (!first->succeeded()) {
            for (const auto& [id, error] : first->errors) {
                std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(info.name.size()), info.name.data(),
                             error.message.c_str());
            }
            return 1;
        }
        const auto unchangedStart = Clock::now();
        auto unchanged = regenerator.regenerate(*document);
        const double unchangedMs = millisecondsSince(unchangedStart);
        if (!unchanged) {
            return fail(info.name, "regenerate", unchanged.error());
        }

        const auto print = reference::fingerprint(*document, regenerator);
        if (!print) {
            return fail(info.name, "fingerprint", print.error());
        }
        std::printf("== %.*s\n%s", static_cast<int>(info.name.size()), info.name.data(),
                    reference::toText(*print).c_str());

        if (!out.empty()) {
            const auto stem = out / std::string{info.fileStem};
            if (auto step = io::exportStep(*document, stem.string() + ".step"); !step) {
                return fail(info.name, "STEP export", step.error());
            }
            if (auto stl = io::exportStl(*document, stem.string() + ".stl"); !stl) {
                return fail(info.name, "STL export", stl.error());
            }
        }

        // What a change to a main dimension costs, and a save and a load.
        double changeMs = 0.0;
        const auto item = document->findByName(info.mainParameter);
        const auto parameter = item ? document->asParameter(*item) : std::nullopt;
        if (!parameter) {
            std::fprintf(stderr, "%.*s: no parameter '%.*s'\n", static_cast<int>(info.name.size()), info.name.data(),
                         static_cast<int>(info.mainParameter.size()), info.mainParameter.data());
            return 1;
        }
        if (auto changed = document->setParameterValue(*parameter, info.mainParameterMm * units::mm); !changed) {
            return fail(info.name, "parameter change", changed.error());
        }
        const auto changeStart = Clock::now();
        auto afterChange = regenerator.regenerate(*document);
        changeMs = millisecondsSince(changeStart);
        if (!afterChange || !afterChange->succeeded()) {
            std::fprintf(stderr, "%.*s: the model did not regenerate after changing %.*s\n",
                         static_cast<int>(info.name.size()), info.name.data(),
                         static_cast<int>(info.mainParameter.size()), info.mainParameter.data());
            return 1;
        }

        const auto scratch = std::filesystem::temp_directory_path() / (std::string{info.fileStem} + "-timing.bcad");
        const auto saveStart = Clock::now();
        auto saved = io::saveDocument(*document, scratch);
        const double saveMs = millisecondsSince(saveStart);
        if (!saved) {
            return fail(info.name, "save", saved.error());
        }
        const auto loadStart = Clock::now();
        auto reloaded = io::loadDocument(scratch);
        const double loadMs = millisecondsSince(loadStart);
        std::error_code ignored;
        std::filesystem::remove(scratch, ignored);
        if (!reloaded) {
            return fail(info.name, "load", reloaded.error());
        }
        std::printf("timing_ms build %.3f first_regeneration %.3f unchanged_regeneration %.3f "
                    "changed_%.*s_regeneration %.3f save %.3f load %.3f\n",
                    buildMs, firstMs, unchangedMs, static_cast<int>(info.mainParameter.size()),
                    info.mainParameter.data(), changeMs, saveMs, loadMs);
    }

    // The assembly reference models (P13-REFMOD-001). A second loop rather
    // than one over both, because an assembly is regenerated with the
    // assembly handlers registered and is reported by its solve rather than
    // by result bodies -- and because one of them is committed in a state
    // that deliberately does not solve.
    for (const reference::AssemblyReferenceModelInfo& info :
         assemblies ? std::span{reference::kAssemblyReferenceModels}
                    : std::span<const reference::AssemblyReferenceModelInfo>{}) {
        const auto buildStart = Clock::now();
        auto document = reference::buildAssemblyReferenceModel(info.kind);
        const double buildMs = millisecondsSince(buildStart);
        if (!document) {
            return fail(info.name, "build", document.error());
        }
        if (!out.empty()) {
            const auto path = out / (std::string{info.fileStem} + ".bcad");
            if (auto saved = io::saveDocument(*document, path); !saved) {
                return fail(info.name, "save", saved.error());
            }
        }

        features::Regenerator regenerator;
        assembly::AssemblyRegeneration pass;
        assembly::registerHandlers(regenerator, nullptr, &pass);
        const auto regenStart = Clock::now();
        auto report = regenerator.regenerateAll(*document);
        const double regenMs = millisecondsSince(regenStart);
        if (!report) {
            return fail(info.name, "regenerate", report.error());
        }
        if (!report->succeeded()) {
            for (const auto& [id, error] : report->errors) {
                std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(info.name.size()), info.name.data(),
                             error.message.c_str());
            }
            return 1;
        }

        const assembly::BodyLookup bodies = [&](ObjectId object) { return regenerator.body(object); };
        auto solved = assembly::solve(*document, {}, bodies);
        if (!solved) {
            return fail(info.name, "solve", solved.error());
        }
        if (solved->solved() != info.solves) {
            std::fprintf(stderr, "%.*s: expected the solve to %s\n", static_cast<int>(info.name.size()),
                         info.name.data(), info.solves ? "succeed" : "fail");
            return 1;
        }
        std::printf("== %.*s (%.*s)\ncomponents %zu active %zu mates %zu active %zu\n"
                    "solve %.*s unknowns %zu equations %zu dof %zu residual_mm %.6g\n",
                    static_cast<int>(info.name.size()), info.name.data(), static_cast<int>(info.label.size()),
                    info.label.data(), assembly::components(*document).size(),
                    assembly::activeComponents(*document).size(), assembly::mates(*document).size(),
                    assembly::activeMates(*document).size(),
                    static_cast<int>(assembly::toString(solved->status).size()),
                    assembly::toString(solved->status).data(), solved->unknowns, solved->equations,
                    solved->degreesOfFreedom, solved->maxResidual.si() * 1000.0);

        // Only a model that solves has an assembly to export; the one that
        // does not is expected to be refused, and that refusal is checked by
        // the test suite rather than swallowed here.
        if (!out.empty() && info.solves) {
            const auto stem = out / std::string{info.fileStem};
            if (auto step = io::exportStep(*document, stem.string() + ".step"); !step) {
                return fail(info.name, "STEP export", step.error());
            }
        }
        std::printf("timing_ms build %.3f regeneration_and_solve %.3f\n", buildMs, regenMs);
    }

    // The drawing reference models (P14-REFMOD-001). A third loop, because a
    // drawing is regenerated with the DRAWING handlers registered as well as
    // the assembly ones, and because what it produces is a sheet rather than
    // a body: the scene is built here, in a FRESH PROCESS, and written out in
    // all three formats.
    for (const reference::DrawingReferenceModelInfo& info :
         drawings ? std::span{reference::kDrawingReferenceModels}
                  : std::span<const reference::DrawingReferenceModelInfo>{}) {
        const auto buildStart = Clock::now();
        auto document = reference::buildDrawingReferenceModel(info.kind);
        const double buildMs = millisecondsSince(buildStart);
        if (!document) {
            return fail(info.name, "build", document.error());
        }
        if (!out.empty()) {
            const auto path = out / (std::string{info.fileStem} + ".bcad");
            if (auto saved = io::saveDocument(*document, path); !saved) {
                return fail(info.name, "save", saved.error());
            }
        }

        features::Regenerator regenerator;
        assembly::registerHandlers(regenerator, nullptr, nullptr);
        drawing::registerHandlers(regenerator);
        const auto regenStart = Clock::now();
        auto report = regenerator.regenerateAll(*document);
        const double regenMs = millisecondsSince(regenStart);
        if (!report) {
            return fail(info.name, "regenerate", report.error());
        }
        if (!report->succeeded()) {
            for (const auto& [id, error] : report->errors) {
                std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(info.name.size()),
                             info.name.data(), error.message.c_str());
            }
            return 1;
        }

        const drawing::BodyLookup bodies = [&](ObjectId object) { return regenerator.body(object); };
        const drawing::TransformLookup transforms = [&](ComponentId component) {
            return regenerator.transform(component);
        };

        const std::vector<SheetId> sheets = drawing::sheets(*document);
        if (sheets.empty()) {
            std::fprintf(stderr, "%.*s: has no sheet\n", static_cast<int>(info.name.size()),
                         info.name.data());
            return 1;
        }
        const auto sceneStart = Clock::now();
        std::size_t items = 0;
        std::size_t views = 0;
        for (const SheetId sheet : sheets) {
            views += drawing::viewsOn(*document, sheet).size();
            auto scene = drawing::sheetScene(*document, sheet, bodies, transforms);
            if (!scene) {
                return fail(info.name, "sheet scene", scene.error());
            }
            if (auto valid = drawing::validate(*scene); !valid) {
                return fail(info.name, "scene validation", valid.error());
            }
            items +=
                scene->items.lines.size() + scene->items.arcs.size() + scene->items.texts.size();
            if (!out.empty() && sheet == sheets.front()) {
                const auto stem = out / std::string{info.fileStem};
                if (auto pdf = io::exportPdf(*scene, stem.string() + ".pdf"); !pdf) {
                    return fail(info.name, "PDF export", pdf.error());
                }
                if (auto svg = io::exportSvg(*scene, stem.string() + ".svg"); !svg) {
                    return fail(info.name, "SVG export", svg.error());
                }
                if (auto dxf = io::exportDxf(*scene, stem.string() + ".dxf"); !dxf) {
                    return fail(info.name, "DXF export", dxf.error());
                }
            }
        }
        const double sceneMs = millisecondsSince(sceneStart);

        std::printf("== %.*s (%.*s)\nsheets %zu views %zu scene_items %zu\n",
                    static_cast<int>(info.name.size()), info.name.data(),
                    static_cast<int>(info.label.size()), info.label.data(), sheets.size(), views,
                    items);
        std::printf("timing_ms build %.3f regeneration %.3f scene_and_export %.3f\n", buildMs,
                    regenMs, sceneMs);
    }
    // The engineering-data reference models (P15-REFMOD-001). A fourth loop,
    // because what a material model produces is neither a body count nor a
    // solve nor a sheet: it is a MASS, which needs the material the document is
    // assigned as well as the geometry. Printing it here means the saved model
    // and the mass it implies are produced by the same process, and the CLI
    // fixture then recomputes that mass in a separate one.
    for (const reference::MaterialReferenceModelInfo& info :
         materials ? std::span{reference::kMaterialReferenceModels}
                   : std::span<const reference::MaterialReferenceModelInfo>{}) {
        const auto buildStart = Clock::now();
        auto document = reference::buildMaterialReferenceModel(info.kind);
        const double buildMs = millisecondsSince(buildStart);
        if (!document) {
            return fail(info.name, "build", document.error());
        }
        if (!out.empty()) {
            const auto path = out / (std::string{info.fileStem} + ".bcad");
            if (auto saved = io::saveDocument(*document, path); !saved) {
                return fail(info.name, "save", saved.error());
            }
        }

        features::Regenerator regenerator;
        assembly::registerHandlers(regenerator, nullptr, nullptr);
        const auto regenStart = Clock::now();
        auto report = regenerator.regenerateAll(*document);
        const double regenMs = millisecondsSince(regenStart);
        if (!report) {
            return fail(info.name, "regenerate", report.error());
        }
        if (!report->succeeded()) {
            for (const auto& [id, error] : report->errors) {
                std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(info.name.size()), info.name.data(),
                             error.message.c_str());
            }
            return 1;
        }

        const features::MaterialAssignment assignment = features::materialAssignment(*document);
        std::printf("== %.*s %.*s\n%.*s\nmaterials %zu assignment %.*s\n",
                    static_cast<int>(info.id.size()), info.id.data(), static_cast<int>(info.name.size()),
                    info.name.data(), static_cast<int>(info.purpose.size()), info.purpose.data(),
                    features::materialCount(*document),
                    static_cast<int>(features::toString(assignment.state).size()),
                    features::toString(assignment.state).data());

        // The mass of every body the regeneration produced, from the assigned
        // material. A model whose material is deliberately incomplete says so
        // instead of printing a number, which is the honest line for it.
        for (const ObjectId result : features::resultFeatures(*document)) {
            if (regenerator.body(result) == nullptr) {
                continue;
            }
            const DocumentObject& object = *document->findObject(result);
            auto properties = features::partMassProperties(*document, regenerator, object.id());
            if (!properties) {
                std::printf("body %s mass unavailable: %s\n", object.name().c_str(),
                            properties.error().message.c_str());
                continue;
            }
            std::printf("body %s volume_mm3 %.17g mass_kg %.17g centre_mm %.17g %.17g %.17g\n",
                        object.name().c_str(), properties->volume.in(units::mm3), properties->mass.in(units::kg),
                        properties->centreOfMass.x.in(units::mm), properties->centreOfMass.y.in(units::mm),
                        properties->centreOfMass.z.in(units::mm));
            std::printf("body %s inertia_centroidal_kg_m2 %.17g %.17g %.17g %.17g %.17g %.17g\n",
                        object.name().c_str(), properties->aboutCentreOfMass.xx.si(),
                        properties->aboutCentreOfMass.yy.si(), properties->aboutCentreOfMass.zz.si(),
                        properties->aboutCentreOfMass.xy.si(), properties->aboutCentreOfMass.xz.si(),
                        properties->aboutCentreOfMass.yz.si());
        }

        // A change to a main dimension must regenerate, which is what makes the
        // mass above a recomputed answer rather than a stored one.
        const auto item = document->findByName(info.mainParameter);
        const auto parameter = item ? document->asParameter(*item) : std::nullopt;
        if (!parameter) {
            std::fprintf(stderr, "%.*s: no parameter '%.*s'\n", static_cast<int>(info.name.size()),
                         info.name.data(), static_cast<int>(info.mainParameter.size()),
                         info.mainParameter.data());
            return 1;
        }
        if (auto changed = document->setParameterValue(*parameter, info.mainParameterMm * units::mm); !changed) {
            return fail(info.name, "parameter change", changed.error());
        }
        const auto changeStart = Clock::now();
        auto afterChange = regenerator.regenerateAll(*document);
        const double changeMs = millisecondsSince(changeStart);
        if (!afterChange || !afterChange->succeeded()) {
            std::fprintf(stderr, "%.*s: the model did not regenerate after changing %.*s\n",
                         static_cast<int>(info.name.size()), info.name.data(),
                         static_cast<int>(info.mainParameter.size()), info.mainParameter.data());
            return 1;
        }
        std::printf("timing_ms build %.3f regeneration %.3f changed_%.*s_regeneration %.3f\n", buildMs, regenMs,
                    static_cast<int>(info.mainParameter.size()), info.mainParameter.data(), changeMs);
    }
    // The meshing reference models (P16-REFMOD-001). A fifth loop, because what
    // a meshing model produces is a MESH -- derived state held by a Mesher
    // service, keyed by its control, never written to the file -- and the
    // numbers that describe it are counts, volumes and a currency, none of
    // which the other four loops have anywhere to print.
    //
    // THIS IS THE SUITE'S EVIDENCE GENERATOR as well as its CLI fixture: every
    // figure the evidence tables quote is a line printed here, from a process
    // whose exit code is itself the first assertion. It fails fast, and it
    // fails on a model that meshes when it should not just as readily as on
    // one that does not mesh when it should.
    for (const reference::MeshReferenceModelInfo& info :
         meshes ? std::span{reference::kMeshReferenceModels}
                : std::span<const reference::MeshReferenceModelInfo>{}) {
        const auto buildStart = Clock::now();
        auto document = reference::buildMeshReferenceModel(info.kind);
        const double buildMs = millisecondsSince(buildStart);
        if (!document) {
            return fail(info.name, "build", document.error());
        }
        if (!out.empty()) {
            const auto path = out / (std::string{info.fileStem} + ".bcad");
            if (auto saved = io::saveDocument(*document, path); !saved) {
                return fail(info.name, "save", saved.error());
            }
        }

        features::Regenerator regenerator;
        const auto regenStart = Clock::now();
        auto report = regenerator.regenerateAll(*document);
        const double regenMs = millisecondsSince(regenStart);
        if (!report) {
            return fail(info.name, "regenerate", report.error());
        }
        // RM-MESH-08's body is MEANT to fail, so a failed regeneration is its
        // expected outcome and a successful one is the defect.
        if (report->succeeded() != info.expectMesh) {
            std::fprintf(stderr, "%.*s: regeneration %s, expected it to %s\n",
                         static_cast<int>(info.name.size()), info.name.data(),
                         report->succeeded() ? "succeeded" : "FAILED",
                         info.expectMesh ? "succeed" : "fail");
            return 1;
        }

        const std::optional<ObjectId> controlId = document->findByName("Mesh");
        if (!controlId) {
            std::fprintf(stderr, "%.*s: no mesh control\n", static_cast<int>(info.name.size()),
                         info.name.data());
            return 1;
        }
        const auto* control = document->findObjectAs<meshing::MeshControl>(*controlId);
        if (control == nullptr) {
            std::fprintf(stderr, "%.*s: 'Mesh' is not a mesh control\n",
                         static_cast<int>(info.name.size()), info.name.data());
            return 1;
        }
        const MeshControlId key = control->meshControlId();

        std::printf("== %.*s %.*s\n%.*s\n", static_cast<int>(info.id.size()), info.id.data(),
                    static_cast<int>(info.name.size()), info.name.data(),
                    static_cast<int>(info.purpose.size()), info.purpose.data());

        meshing::Mesher mesher;
        const auto meshStart = Clock::now();
        auto meshed = mesher.generate(*document, regenerator, key);
        const double meshMs = millisecondsSince(meshStart);

        if (!info.expectMesh) {
            // THE REFUSAL IS THE RESULT. Nothing may be published: not a mesh
            // with zero elements, not a stale one relabelled, not anything.
            if (meshed) {
                std::fprintf(stderr, "%.*s: meshing SUCCEEDED and must not have\n",
                             static_cast<int>(info.name.size()), info.name.data());
                return 1;
            }
            if (mesher.heldMeshCount() != 0 || mesher.mesh(key) != nullptr) {
                std::fprintf(stderr, "%.*s: a mesh was published despite the refusal\n",
                             static_cast<int>(info.name.size()), info.name.data());
                return 1;
            }
            std::printf("expected_outcome failure actual failure published_mesh no currency %.*s\n",
                        static_cast<int>(meshing::toString(mesher.currency(*document, key)).size()),
                        meshing::toString(mesher.currency(*document, key)).data());
            std::printf("diagnostic %s\n", meshed.error().message.c_str());
            std::printf("timing_ms build %.3f regeneration %.3f meshing %.3f\n", buildMs, regenMs, meshMs);
            continue;
        }

        if (!meshed) {
            return fail(info.name, "meshing", meshed.error());
        }
        const meshing::VolumeMesh& mesh = **meshed;
        const meshing::MeshQualityReport* quality = mesher.quality(key);
        const meshing::GeometryMeshMap* map = mesher.map(key);
        if (quality == nullptr || map == nullptr) {
            std::fprintf(stderr, "%.*s: the mesher published a mesh with no quality report or no map\n",
                         static_cast<int>(info.name.size()), info.name.data());
            return 1;
        }

        // The whole measured record of one reference mesh, in one place, in
        // full double precision -- so the evidence tables are transcriptions
        // and not retypings.
        std::printf("analytic_volume_mm3 %.17g cad_volume_mm3 %.17g mesh_volume_mm3 %.17g "
                    "boundary_volume_mm3 %.17g\n",
                    info.analyticVolumeMm3, mesh.cadVolume().in(units::mm3),
                    mesh.tetrahedralVolume().in(units::mm3), mesh.boundaryVolume().in(units::mm3));
        std::printf("relative_volume_error %.6e nodes %zu tets %zu boundary_facets %zu\n",
                    std::abs(mesh.tetrahedralVolume().in(units::mm3) - info.analyticVolumeMm3) /
                        info.analyticVolumeMm3,
                    mesh.nodeCount(), mesh.tetrahedronCount(), mesh.boundaryTriangleCount());
        std::printf("conforming %s unmatched_boundary %zu unmatched_surface %zu\n",
                    mesh.conformity().conforms() ? "yes" : "NO",
                    mesh.conformity().unmatchedBoundaryFaceCount,
                    mesh.conformity().unmatchedSurfaceTriangleCount);
        std::printf("structurally_valid %s invalid %zu warning %zu failure %zu valid %zu\n",
                    quality->structurallyValid ? "yes" : "NO", quality->invalidElements,
                    quality->warningElements, quality->failureElements, quality->validElements);
        std::printf("mapping_complete %s cad_faces %zu mapped_facets %zu unmapped %zu "
                    "attributed_twice %zu faces_without_facets %zu unnamed_faces %zu\n",
                    map->report().complete() ? "yes" : "NO", map->report().cadFaceCount,
                    map->report().mappedFacetCount, map->report().unmappedFacetCount,
                    map->report().facetsWithSeveralFaces, map->report().facesWithoutFacets,
                    map->report().unnamedFaceCount);
        std::printf("global_target_mm %.17g global_is_default %s local_controls %zu "
                    "unresolved_local %zu slabs %zu\n",
                    mesh.sizing().globalTargetSize.in(units::mm),
                    mesh.sizing().globalIsDefault ? "yes" : "no", mesh.sizing().local.size(),
                    mesh.sizing().unresolvedCount(), mesh.sizing().regions.size());
        // Every boundary set the model declares, resolved against this mesh.
        for (const meshing::NamedBoundarySet& set : control->orderedBoundarySets()) {
            auto resolved = meshing::resolveBoundarySet(set, *map);
            if (!resolved) {
                return fail(info.name, "boundary set " + set.name, resolved.error());
            }
            std::printf("boundary_set %s id %llu faces %zu facets %zu resolved %s\n", set.name.c_str(),
                        static_cast<unsigned long long>(set.id.value()), set.faces.size(),
                        resolved->mapping.facets.size(), resolved->fullyResolved() ? "yes" : "NO");
        }
        if (meshing::MeshCurrency currency = mesher.currency(*document, key);
            !meshing::describesTheModel(currency)) {
            std::fprintf(stderr, "%.*s: a freshly generated mesh reports %.*s\n",
                         static_cast<int>(info.name.size()), info.name.data(),
                         static_cast<int>(meshing::toString(currency).size()),
                         meshing::toString(currency).data());
            return 1;
        }

        // A change to a main dimension must make the held mesh STALE for the
        // right reason, and the regenerated model must then remesh. This is
        // what makes every number above a recomputed answer rather than a
        // stored one.
        const auto item = document->findByName(info.mainParameter);
        const auto parameter = item ? document->asParameter(*item) : std::nullopt;
        if (!parameter) {
            std::fprintf(stderr, "%.*s: no parameter '%.*s'\n", static_cast<int>(info.name.size()),
                         info.name.data(), static_cast<int>(info.mainParameter.size()),
                         info.mainParameter.data());
            return 1;
        }
        if (auto changed = document->setParameterValue(*parameter, info.mainParameterMm * units::mm);
            !changed) {
            return fail(info.name, "parameter change", changed.error());
        }
        const meshing::MeshCurrency afterEdit = mesher.currency(*document, key);
        if (afterEdit != meshing::MeshCurrency::StaleGeometry) {
            std::fprintf(stderr, "%.*s: after changing %.*s the mesh reports %.*s, not stale_geometry\n",
                         static_cast<int>(info.name.size()), info.name.data(),
                         static_cast<int>(info.mainParameter.size()), info.mainParameter.data(),
                         static_cast<int>(meshing::toString(afterEdit).size()),
                         meshing::toString(afterEdit).data());
            return 1;
        }
        const auto changeStart = Clock::now();
        auto afterChange = regenerator.regenerateAll(*document);
        const double changeMs = millisecondsSince(changeStart);
        if (!afterChange || !afterChange->succeeded()) {
            std::fprintf(stderr, "%.*s: the model did not regenerate after changing %.*s\n",
                         static_cast<int>(info.name.size()), info.name.data(),
                         static_cast<int>(info.mainParameter.size()), info.mainParameter.data());
            return 1;
        }
        const auto remeshStart = Clock::now();
        auto remeshed = mesher.generate(*document, regenerator, key);
        const double remeshMs = millisecondsSince(remeshStart);
        if (!remeshed) {
            return fail(info.name, "remeshing after the change", remeshed.error());
        }
        std::printf("after_change %.*s_mm %.17g mesh_volume_mm3 %.17g nodes %zu tets %zu currency %.*s\n",
                    static_cast<int>(info.mainParameter.size()), info.mainParameter.data(),
                    info.mainParameterMm, (*remeshed)->tetrahedralVolume().in(units::mm3),
                    (*remeshed)->nodeCount(), (*remeshed)->tetrahedronCount(),
                    static_cast<int>(meshing::toString(mesher.currency(*document, key)).size()),
                    meshing::toString(mesher.currency(*document, key)).data());
        std::printf("timing_ms build %.3f regeneration %.3f meshing %.3f changed_regeneration %.3f "
                    "remeshing %.3f\n",
                    buildMs, regenMs, meshMs, changeMs, remeshMs);
    }
    return 0;
}
