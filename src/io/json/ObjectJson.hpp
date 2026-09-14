#pragma once

// JSON mapping of the document object kinds known to the native format.

#include "io/json/JsonReader.hpp"

#include <bettercad/core/math/Frame.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/sketch/Sketch.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace bettercad::io::detail {

/// Origin in metres; axes as unit vectors, restored bit for bit.
[[nodiscard]] Json frameToJson(const Frame3D& frame);
[[nodiscard]] Result<Frame3D> frameFromJson(const Json& value, std::string_view path);

[[nodiscard]] Json sketchToJson(const sketch::Sketch& sketch);
[[nodiscard]] Result<std::unique_ptr<sketch::Sketch>> sketchFromJson(const Json& data, std::string name,
                                                                     std::string_view path);

[[nodiscard]] Json extrudeToJson(const features::ExtrudeFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::ExtrudeFeature>>
extrudeFromJson(const Json& data, std::string name, std::string_view path);

} // namespace bettercad::io::detail
