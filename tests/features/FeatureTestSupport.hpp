#pragma once

#include <bettercad/core/Units.hpp>
#include <bettercad/core/document/Document.hpp>
#include <bettercad/features/Regenerator.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace bettercad::test {

template <typename Id>
Id require(const Result<Id>& id) {
    REQUIRE(id.has_value());
    return *id;
}

/// Axis-aligned rectangle of four connected lines (shared corner points),
/// counter-clockwise unless @p clockwise. Returns the lines.
inline std::array<EntityId, 4> addRectangle(sketch::Sketch& sketch, Length x, Length y, Length width,
                                            Length height, bool clockwise = false) {
    std::array<EntityId, 4> corners{
        require(sketch.addPoint(Point2D{x, y})),
        require(sketch.addPoint(Point2D{x + width, y})),
        require(sketch.addPoint(Point2D{x + width, y + height})),
        require(sketch.addPoint(Point2D{x, y + height})),
    };
    if (clockwise) {
        std::swap(corners[1], corners[3]);
    }
    std::array<EntityId, 4> lines{};
    for (std::size_t i = 0; i < 4; ++i) {
        lines[i] = require(sketch.addLine(corners[i], corners[(i + 1) % 4]));
    }
    return lines;
}

inline features::RegenerationReport requireReport(features::Regenerator& regenerator, Document& doc) {
    auto report = regenerator.regenerate(doc);
    REQUIRE(report.has_value());
    return *report;
}

inline double volumeMm3(const features::Regenerator& regenerator, ObjectId feature) {
    const geometry::Body* body = regenerator.body(feature);
    REQUIRE(body != nullptr);
    const auto props = body->massProperties();
    REQUIRE(props.has_value());
    return props->volume.in(units::mm3);
}

} // namespace bettercad::test
