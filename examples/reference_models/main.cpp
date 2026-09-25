// Builds every BetterCAD reference model, regenerates it and prints its
// fingerprint and timings. With --out <dir>, also writes each model as a
// .bcad file (as built, before regeneration) with its STEP and STL exports.
//
//   bettercad_example_reference_models [--out <dir>]
#include "AssemblyReferenceModels.hpp"
#include "DrawingReferenceModels.hpp"
#include "ReferenceModels.hpp"

#include <bettercad/assembly/Components.hpp>
#include <bettercad/assembly/Configurations.hpp>
#include <bettercad/assembly/Mates.hpp>
#include <bettercad/assembly/Resolution.hpp>
#include <bettercad/assembly/Solver.hpp>
#include <bettercad/core/Units.hpp>
#include <bettercad/drawing/Regeneration.hpp>
#include <bettercad/drawing/SheetScene.hpp>
#include <bettercad/drawing/Sheets.hpp>
#include <bettercad/drawing/Views.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/DrawingExport.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <chrono>
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
    const bool drawings = true;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            out = argv[++i];
        } else if (arg == "--drawings") {
            parts = false;
            assemblies = false;
        } else {
            std::fprintf(stderr, "usage: %s [--out <dir>] [--drawings]\n", argv[0]);
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
    return 0;
}
