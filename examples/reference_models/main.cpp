// Builds every BetterCAD reference model, regenerates it and prints its
// fingerprint and timings. With --out <dir>, also writes each model as a
// .bcad file (as built, before regeneration) with its STEP and STL exports.
//
//   bettercad_example_reference_models [--out <dir>]
#include "ReferenceModels.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

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
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            out = argv[++i];
        } else {
            std::fprintf(stderr, "usage: %s [--out <dir>]\n", argv[0]);
            return 2;
        }
    }
    if (!out.empty()) {
        std::filesystem::create_directories(out);
    }

    for (const reference::ReferenceModelInfo& info : reference::kReferenceModels) {
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
    return 0;
}
