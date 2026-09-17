#pragma once

#include <bettercad/core/document/References.hpp>
#include <bettercad/core/geometry/Body.hpp>
#include <bettercad/core/geometry/Faces.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

namespace bettercad::test {

/// Every property of a body that must repeat exactly: validity, topology,
/// volume, area, centre, bounds and the names of its faces.
struct BodyPrint {
    bool valid = false;
    geometry::TopologySummary topology{};
    double volume = 0.0;
    double area = 0.0;
    std::array<double, 3> centre{};
    std::array<double, 3> lower{};
    std::array<double, 3> upper{};
    std::vector<std::vector<FaceName>> names{};

    friend bool operator==(const BodyPrint&, const BodyPrint&) = default;
};

inline BodyPrint printOf(const geometry::Body& body) {
    BodyPrint print{.valid = body.isValid(), .topology = body.topology()};
    const auto properties = body.massProperties();
    REQUIRE(properties.has_value());
    print.volume = properties->volume.si();
    print.area = properties->surfaceArea.si();
    print.centre = {properties->centerOfMass.x.si(), properties->centerOfMass.y.si(),
                    properties->centerOfMass.z.si()};
    const auto box = body.boundingBox();
    REQUIRE(box.has_value());
    print.lower = {box->min.x.si(), box->min.y.si(), box->min.z.si()};
    print.upper = {box->max.x.si(), box->max.y.si(), box->max.z.si()};
    const auto faces = geometry::listFaces(body);
    REQUIRE(faces.has_value());
    for (const geometry::FaceInfo& face : *faces) {
        print.names.push_back(face.names);
    }
    return print;
}

} // namespace bettercad::test
