#pragma once

// JSON mapping of the document object kinds known to the native format.

#include "io/json/JsonReader.hpp"

#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace bettercad::io::detail {

/// [x, y, z] in metres.
[[nodiscard]] Json pointToJson(const Point3D& point);
[[nodiscard]] Result<Point3D> pointFromJson(const Json& object, std::string_view key, std::string_view path);

/// A unit vector [x, y, z], restored bit for bit.
[[nodiscard]] Json directionToJson(const Direction3D& direction);
[[nodiscard]] Result<Direction3D> directionFromJson(const Json& object, std::string_view key,
                                                    std::string_view path);

/// Origin in metres; axes as unit vectors, restored bit for bit.
[[nodiscard]] Json frameToJson(const Frame3D& frame);
[[nodiscard]] Result<Frame3D> frameFromJson(const Json& value, std::string_view path);

[[nodiscard]] Json sketchToJson(const sketch::Sketch& sketch);
[[nodiscard]] Result<std::unique_ptr<sketch::Sketch>> sketchFromJson(const Json& data, std::string name,
                                                                     std::string_view path);

[[nodiscard]] Json extrudeToJson(const features::ExtrudeFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::ExtrudeFeature>>
extrudeFromJson(const Json& data, std::string name, std::string_view path);

/// Angle in radians; the axis as {"type": "sketch_x" | "sketch_y" | "line", "line": id}.
[[nodiscard]] Json revolveToJson(const features::RevolveFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::RevolveFeature>>
revolveFromJson(const Json& data, std::string name, std::string_view path);

/// Distances in metres, the angle in radians. Each edge reference is
/// {"curve": "line", "point": [...], "direction": [...]} or
/// {"curve": "circle", "center": [...], "axis": [...], "radius": r}.
[[nodiscard]] Json chamferToJson(const features::ChamferFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::ChamferFeature>>
chamferFromJson(const Json& data, std::string name, std::string_view path);

} // namespace bettercad::io::detail
