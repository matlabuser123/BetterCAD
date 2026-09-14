#include "TestHelpers.hpp"
#include "support/BracketModel.hpp"
#include "support/MeshAnalysis.hpp"
#include "support/TestFiles.hpp"

#include <bettercad/core/Units.hpp>
#include <bettercad/core/geometry/Mesh.hpp>
#include <bettercad/core/geometry/Primitives.hpp>
#include <bettercad/features/ResultBodies.hpp>
#include <bettercad/io/ModelExport.hpp>
#include <bettercad/io/Stl.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace bettercad;
using namespace bettercad::literals;
using bettercad::test::BracketModel;
using bettercad::test::checkSurface;
using bettercad::test::enclosedVolume;
using bettercad::test::errorCode;
using bettercad::test::parseAsciiStl;
using bettercad::test::parseBinaryStl;
using bettercad::test::readFile;
using bettercad::test::StlData;
using bettercad::test::TempDir;
using bettercad::test::Triangle;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinRel;

namespace {

/// A unit right triangle in the XY plane, facing +Z, offset by (x, 0, 0) mm.
geometry::Mesh triangleMesh(double xMm) {
    geometry::Mesh mesh;
    mesh.vertices = {Point3D{xMm * units::mm, 0_mm, 0_mm}, Point3D{(xMm + 1.0) * units::mm, 0_mm, 0_mm},
                     Point3D{xMm * units::mm, 1_mm, 0_mm}};
    mesh.triangles = {{0, 1, 2}};
    return mesh;
}

StlData requireStl(std::string_view bytes, io::StlFormat format) {
    auto data = format == io::StlFormat::Binary ? parseBinaryStl(bytes) : parseAsciiStl(bytes);
    REQUIRE(data.has_value());
    return *data;
}

} // namespace

TEST_CASE("STL output follows the binary and ASCII layouts", "[io][stl]") {
    const std::vector<geometry::Mesh> meshes{triangleMesh(0.0), triangleMesh(10.0)};

    SECTION("binary") {
        const auto stl = io::meshesToStl(meshes, io::StlFormat::Binary, "Part");
        REQUIRE(stl.has_value());
        CHECK(stl->size() == 84 + 2 * 50);
        CHECK_FALSE(stl->starts_with("solid")); // that would mark it as ASCII
        const StlData data = requireStl(*stl, io::StlFormat::Binary);
        CHECK_THAT(data.header, StartsWith("BetterCAD "));
        CHECK_THAT(data.header, ContainsSubstring("units mm: Part"));
        REQUIRE(data.triangles.size() == 2);
        CHECK(data.triangles[1] == Triangle{{{10.0, 0.0, 0.0}, {11.0, 0.0, 0.0}, {10.0, 1.0, 0.0}}});
        CHECK(data.normals[0] == test::Vertex{0.0, 0.0, 1.0});
    }
    SECTION("ASCII") {
        const auto stl = io::meshesToStl(meshes, io::StlFormat::Ascii, "Part");
        REQUIRE(stl.has_value());
        CHECK_THAT(*stl, StartsWith("solid Part\n"
                                    "  facet normal 0 0 1\n"
                                    "    outer loop\n"
                                    "      vertex 0 0 0\n"
                                    "      vertex 1 0 0\n"
                                    "      vertex 0 1 0\n"
                                    "    endloop\n"
                                    "  endfacet\n"));
        CHECK(stl->ends_with("endsolid Part\n"));
        const StlData data = requireStl(*stl, io::StlFormat::Ascii);
        REQUIRE(data.triangles.size() == 2);
        CHECK(data.triangles[1] == Triangle{{{10.0, 0.0, 0.0}, {11.0, 0.0, 0.0}, {10.0, 1.0, 0.0}}});
    }
    SECTION("names are reduced to printable ASCII") {
        const auto stl = io::meshesToStl(meshes, io::StlFormat::Ascii, "Pl\xC3\xA5t\nX");
        REQUIRE(stl.has_value());
        CHECK_THAT(*stl, StartsWith("solid Pl__t_X\n"));
    }
}

TEST_CASE("ASCII and binary STL hold the same 32-bit coordinates", "[io][stl]") {
    // Coordinates that are not exact in binary floating point.
    geometry::Mesh mesh;
    mesh.vertices = {Point3D{0.1_mm, 1.0 / 3.0 * units::mm, 123.456789_mm},
                     Point3D{-2.5e-3_mm, 1e4_mm, 7.0_mm}, Point3D{3.0_mm, -0.3_mm, 0.7_mm}};
    mesh.triangles = {{0, 1, 2}};
    const std::vector<geometry::Mesh> meshes{mesh};
    const StlData binary = requireStl(*io::meshesToStl(meshes, io::StlFormat::Binary, "x"), io::StlFormat::Binary);
    const StlData ascii = requireStl(*io::meshesToStl(meshes, io::StlFormat::Ascii, "x"), io::StlFormat::Ascii);
    CHECK(ascii.triangles == binary.triangles);
    CHECK(ascii.normals == binary.normals);
    CHECK(binary.triangles[0][0][1] == static_cast<double>(static_cast<float>(1.0 / 3.0)));
}

TEST_CASE("STL output rejects what it cannot represent", "[io][stl]") {
    geometry::Mesh mesh = triangleMesh(0.0);
    SECTION("non-finite and out-of-range coordinates") {
        mesh.vertices[1].x = Length::fromSi(std::numeric_limits<double>::infinity());
        CHECK(errorCode(io::meshesToStl({&mesh, 1}, io::StlFormat::Binary, "x")) == ErrorCode::InvalidArgument);
        mesh.vertices[1].x = Length::fromSi(1e36); // 1e39 mm exceeds the float range
        CHECK(errorCode(io::meshesToStl({&mesh, 1}, io::StlFormat::Ascii, "x")) == ErrorCode::InvalidArgument);
    }
    SECTION("indices outside the vertex list") {
        mesh.triangles = {{0, 1, 3}};
        CHECK(errorCode(io::meshesToStl({&mesh, 1}, io::StlFormat::Binary, "x")) == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("Exported STL files are closed surfaces of the model's result bodies", "[io][stl][export]") {
    const auto format = GENERATE(io::StlFormat::Binary, io::StlFormat::Ascii);
    CAPTURE(format == io::StlFormat::Binary ? "binary" : "ASCII");
    BracketModel model;
    TempDir dir;
    const auto path = dir.path() / "bracket.stl";

    const io::StlExportOptions options{.mesh = {.linearDeflection = 0.01_mm}, .format = format};
    const auto summary = io::exportStl(model.doc, path, options);
    REQUIRE(summary.has_value());
    REQUIRE(summary->bodies.size() == 2);
    CHECK(summary->bodies[0].name == "Pocket");
    CHECK(summary->bodies[1].name == "Slot");

    const std::string bytes = readFile(path);
    CHECK(summary->bytes == bytes.size());
    const StlData data = requireStl(bytes, format);
    CHECK(data.triangles.size() == summary->bodies[0].triangles + summary->bodies[1].triangles);

    // Split the triangles back into the two bodies (they are written in order).
    const auto split = data.triangles.begin() + static_cast<std::ptrdiff_t>(summary->bodies[0].triangles);
    const std::vector<Triangle> pocket(data.triangles.begin(), split);
    const std::vector<Triangle> slot(split, data.triangles.end());
    CHECK(checkSurface(pocket).watertight());
    CHECK(checkSurface(slot).watertight());

    // Volumes agree with the exact solids to within the deflection over the
    // surface (plus float rounding, far smaller).
    const auto bodies = features::regenerateResultBodies(model.doc);
    REQUIRE(bodies.has_value());
    for (std::size_t i = 0; i < 2; ++i) {
        const auto properties = (*bodies)[i].body.massProperties();
        REQUIRE(properties.has_value());
        const double exact = properties->volume.in(units::mm3);
        const double meshed = enclosedVolume(i == 0 ? pocket : slot);
        CHECK(std::abs(meshed - exact) <= 0.01 * properties->surfaceArea.in(units::mm2));
    }
    CHECK_THAT(enclosedVolume(pocket), WithinRel(BracketModel::kPocketVolume, 1e-3));
    CHECK_THAT(enclosedVolume(slot), WithinRel(BracketModel::kSlotVolume, 1e-3));
}

TEST_CASE("STL export fails cleanly when there is nothing valid to write", "[io][stl][export]") {
    TempDir dir;
    const auto path = dir.path() / "out.stl";
    SECTION("no bodies") {
        const Document empty("Empty");
        const auto result = io::exportStl(empty, path);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(result.error().message, ContainsSubstring("no bodies"));
    }
    SECTION("the model does not regenerate") {
        BracketModel model;
        REQUIRE(model.doc.setParameterValue(model.width, -(10_mm)).has_value());
        const auto result = io::exportStl(model.doc, path);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::FailedPrecondition);
        CHECK_THAT(result.error().message, ContainsSubstring("Base"));
    }
    SECTION("the file cannot be written") {
        BracketModel model;
        CHECK(errorCode(io::exportStl(model.doc, dir.path() / "missing" / "out.stl")) == ErrorCode::IoError);
    }
    CHECK_FALSE(std::filesystem::exists(path));
}
