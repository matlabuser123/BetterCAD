#pragma once

#include "reference/Analytic.hpp"
#include "support/BracketModel.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/TestFiles.hpp"
#include "support/occt/StepReadBack.hpp"

#include <ReferenceModels.hpp>

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Command.hpp>
#include <bettercad/core/document/Commands.hpp>
#include <bettercad/core/geometry/Edges.hpp>
#include <bettercad/core/geometry/Faces.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/io/DocumentFile.hpp>
#include <bettercad/io/ModelExport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

// Checks shared by the reference-model tests. Each takes a model's document
// and its independently computed expectations; none derives an expected
// value from the model's own result.
namespace bettercad::test {

namespace refmodel {

/// Relative tolerance for volumes and areas. The kernel integrates planes,
/// cylinders, cones and tori to rounding level: the worst measured on these
/// models is 1.2e-13 (the pulley). A loft's B-spline sides cost more, 6.0e-12
/// on the bracket's gusset. 1e-10 covers both with room to spare, and is far
/// below any wrong feature, which is off by parts per thousand at least.
inline constexpr double kRel = 1e-10;
/// For centroids, in mm: rounding level for the analytic surfaces above
/// (measured below 1e-9 mm). A body with lofted faces needs more; the
/// mounting bracket passes its own tolerance.
inline constexpr double kPositionMm = 1e-9;
/// The kernel's bounding boxes contain the exact box and exceed it by at
/// most its confusion tolerance.
inline constexpr double kBoundsPaddingMm = 1e-7;
/// STEP stores coordinates as decimal text.
inline constexpr double kRelStep = 1e-9;

/// Regenerates and requires full success.
inline features::RegenerationReport requireRegenerated(features::Regenerator& regenerator, Document& doc) {
    auto report = regenerator.regenerate(doc);
    REQUIRE(report.has_value());
    INFO(describe(*report));
    REQUIRE(report->succeeded());
    return *report;
}

inline reference::ModelFingerprint requireFingerprint(const Document& doc, const features::Regenerator& regenerator) {
    auto print = reference::fingerprint(doc, regenerator);
    REQUIRE(print.has_value());
    return *print;
}

/// The properties of a feature's body as @p regenerator last built it.
inline reference::BodyFingerprint featureBody(const features::Regenerator& regenerator, ObjectId feature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    auto summary = reference::bodyFingerprint(*body, feature, {});
    REQUIRE(summary.has_value());
    return *summary;
}

/// The only result body of a model.
inline reference::BodyFingerprint onlyBody(const reference::ModelFingerprint& print) {
    REQUIRE(print.bodies.size() == 1);
    return print.bodies.front();
}

/// A valid single solid with finite, positive properties and a finite box.
inline void checkSound(const reference::BodyFingerprint& body) {
    CHECK(body.valid);
    CHECK(body.topology.solids == 1);
    CHECK(std::isfinite(body.volumeMm3));
    CHECK(body.volumeMm3 > 0.0);
    CHECK(std::isfinite(body.areaMm2));
    CHECK(body.areaMm2 > 0.0);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(std::isfinite(body.centroidMm[i]));
        CHECK(std::isfinite(body.minMm[i]));
        CHECK(std::isfinite(body.maxMm[i]));
        CHECK(body.minMm[i] < body.maxMm[i]);
        CHECK(body.minMm[i] <= body.centroidMm[i]);
        CHECK(body.centroidMm[i] <= body.maxMm[i]);
    }
}

/// Volume, area and centroid against the expected solid; the relative
/// errors are printed for the evidence. @p positionTolerance is the
/// centroid's, in mm: the default suits bodies bounded by planes, cylinders,
/// cones and tori, and a body with lofted (B-spline) faces needs more (see
/// the mounting bracket).
inline void checkProperties(const reference::BodyFingerprint& body, const analytic::Solid& expected,
                            bool checkArea = true, double positionTolerance = kPositionMm) {
    using Catch::Matchers::WithinAbs;
    using Catch::Matchers::WithinRel;
    INFO("volume expected " << expected.volume << " mm^3, actual " << body.volumeMm3 << " mm^3, abs error "
                            << std::abs(body.volumeMm3 - expected.volume) << ", rel error "
                            << std::abs(body.volumeMm3 - expected.volume) / expected.volume);
    CHECK_THAT(body.volumeMm3, WithinRel(expected.volume, kRel));
    if (checkArea) {
        INFO("area expected " << expected.area << " mm^2, actual " << body.areaMm2 << " mm^2, rel error "
                              << std::abs(body.areaMm2 - expected.area) / expected.area);
        CHECK_THAT(body.areaMm2, WithinRel(expected.area, kRel));
    }
    for (std::size_t i = 0; i < 3; ++i) {
        INFO("centroid[" << i << "] expected " << expected.centroid[i] << " mm, actual " << body.centroidMm[i]
                         << " mm, difference " << std::abs(body.centroidMm[i] - expected.centroid[i]) << " mm");
        CHECK_THAT(body.centroidMm[i], WithinAbs(expected.centroid[i], positionTolerance));
    }
}

/// The kernel's box contains [min, max] and exceeds it by at most
/// kBoundsPaddingMm on each side.
inline void checkBounds(const reference::BodyFingerprint& body, const std::array<double, 3>& min,
                        const std::array<double, 3>& max) {
    for (std::size_t i = 0; i < 3; ++i) {
        INFO("axis " << i << ": kernel " << body.minMm[i] << " .. " << body.maxMm[i] << " mm, exact " << min[i]
                     << " .. " << max[i] << " mm");
        CHECK(body.minMm[i] <= min[i] + 1e-9);
        CHECK(body.minMm[i] >= min[i] - kBoundsPaddingMm - 1e-9);
        CHECK(body.maxMm[i] >= max[i] - 1e-9);
        CHECK(body.maxMm[i] <= max[i] + kBoundsPaddingMm + 1e-9);
    }
}

/// Number of edges of @p body on the circle (centre, axis, radius).
inline std::size_t circleEdges(const geometry::Body& body, const std::array<double, 3>& centre,
                               const Direction3D& axis, double radius) {
    const auto signature = geometry::circleSignature(
        Point3D{centre[0] * units::mm, centre[1] * units::mm, centre[2] * units::mm}, axis, radius * units::mm);
    REQUIRE(signature.has_value());
    const auto found = geometry::findEdges(body, *signature);
    REQUIRE(found.has_value());
    return found->size();
}

/// Number of edges of @p body on the line through @p point along @p direction.
inline std::size_t lineEdges(const geometry::Body& body, const std::array<double, 3>& point,
                             const Direction3D& direction) {
    const auto signature = geometry::lineSignature(
        Point3D{point[0] * units::mm, point[1] * units::mm, point[2] * units::mm}, direction);
    const auto found = geometry::findEdges(body, signature);
    REQUIRE(found.has_value());
    return found->size();
}

/// Regenerates the model @p times from scratch (every sketch solved and
/// every body rebuilt each time) and requires the same fingerprint every
/// time, bit for bit: nothing accumulates or drifts.
inline void checkRepeatedRegeneration(Document& doc, const reference::ModelFingerprint& baseline, int times) {
    features::Regenerator regenerator;
    for (int i = 0; i < times; ++i) {
        INFO("full regeneration " << i + 1 << " of " << times);
        auto report = regenerator.regenerateAll(doc);
        REQUIRE(report.has_value());
        REQUIRE(report->succeeded());
        CHECK(requireFingerprint(doc, regenerator) == baseline);
    }
}

/// Save, replace the document with an empty one, load, regenerate: the
/// model (IDs, names, parameters, definitions, dependencies) and every
/// geometric property come back exactly.
inline void checkSaveLoad(Document& doc, const std::filesystem::path& path) {
    features::Regenerator before;
    requireRegenerated(before, doc);
    const reference::ModelFingerprint expected = requireFingerprint(doc, before);
    const Document copy = doc.clone();
    REQUIRE(io::saveDocument(doc, path).has_value());
    doc = Document("Closed");

    auto loaded = io::loadDocument(path);
    REQUIRE(loaded.has_value());
    CHECK(equivalent(*loaded, copy));
    CHECK(loaded->id() == copy.id());
    CHECK(loaded->itemIds() == copy.itemIds());
    for (const DocumentObject& object : copy.objects()) {
        const DocumentObject* restored = loaded->findObject(object.id());
        REQUIRE(restored != nullptr);
        CHECK(restored->typeName() == object.typeName());
        CHECK(restored->dependencies() == object.dependencies());
    }
    features::Regenerator after;
    requireRegenerated(after, *loaded);
    CHECK(requireFingerprint(*loaded, after) == expected);
    doc = std::move(*loaded);
}

/// Exports STEP and reads it back with the kernel.
inline void checkStepExport(const Document& doc, const std::filesystem::path& path, double volume,
                            const std::array<double, 3>& min, const std::array<double, 3>& max) {
    using Catch::Matchers::WithinAbs;
    using Catch::Matchers::WithinRel;
    const auto summary = io::exportStep(doc, path);
    REQUIRE(summary.has_value());
    CHECK(summary->bodies.size() == 1);
    const auto contents = readStepFile(path);
    REQUIRE(contents.has_value());
    CHECK(contents->solids == 1);
    CHECK(contents->valid);
    INFO("STEP volume " << contents->volumeMm3 << " mm^3, expected " << volume << " mm^3, rel error "
                        << std::abs(contents->volumeMm3 - volume) / volume);
    CHECK_THAT(contents->volumeMm3, WithinRel(volume, kRelStep));
    for (std::size_t i = 0; i < 3; ++i) {
        INFO("STEP bounds axis " << i << ": " << contents->minMm[i] << " .. " << contents->maxMm[i]);
        CHECK_THAT(contents->minMm[i], WithinAbs(min[i], 1e-6));
        CHECK_THAT(contents->maxMm[i], WithinAbs(max[i], 1e-6));
    }
}

/// Exports binary STL with a 0.01 mm deflection and checks the mesh
/// independently: closed and consistently oriented (every edge shared by
/// two triangles in opposite directions), finite, outward (positive
/// enclosed volume), and within the deflection of the exact volume:
/// |V_mesh - V| <= deflection x area.
inline void checkStlExport(const Document& doc, const std::filesystem::path& path, double volume, double area) {
    const double deflectionMm = 0.01;
    const auto summary =
        io::exportStl(doc, path, {.mesh = {.linearDeflection = deflectionMm * units::mm}, .format = io::StlFormat::Binary});
    REQUIRE(summary.has_value());
    CHECK(summary->bodies.size() == 1);
    const auto mesh = parseBinaryStl(readFile(path));
    REQUIRE(mesh.has_value());
    CHECK_FALSE(mesh->triangles.empty());
    const SurfaceCheck surface = checkSurface(mesh->triangles);
    CHECK(surface.watertight());
    bool finite = true;
    for (const Triangle& t : mesh->triangles) {
        for (const Vertex& v : t) {
            finite = finite && std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }
    }
    CHECK(finite);
    const double meshed = enclosedVolume(mesh->triangles);
    INFO("STL: " << mesh->triangles.size() << " triangles, meshed " << meshed << " mm^3, exact " << volume
                 << " mm^3, difference " << std::abs(meshed - volume) << " mm^3, bound " << deflectionMm * area
                 << " mm^3");
    CHECK(meshed > 0.0);
    CHECK(std::abs(meshed - volume) <= deflectionMm * area);
}

/// Executes a command through @p history and requires success.
inline void execute(CommandHistory& history, Document& doc, std::unique_ptr<Command> command) {
    const auto executed = history.execute(doc, std::move(command));
    INFO((executed ? std::string{} : executed.error().message));
    REQUIRE(executed.has_value());
}

} // namespace refmodel

} // namespace bettercad::test
