#pragma once

// JSON mapping of the document object kinds known to the native format.

#include "io/json/JsonReader.hpp"

#include <bettercad/core/document/References.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Frame.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/features/ChamferFeature.hpp>
#include <bettercad/features/CircularPatternFeature.hpp>
#include <bettercad/features/CombineFeature.hpp>
#include <bettercad/features/Datums.hpp>
#include <bettercad/features/DraftFeature.hpp>
#include <bettercad/features/ExtrudeFeature.hpp>
#include <bettercad/features/FilletFeature.hpp>
#include <bettercad/features/HoleFeature.hpp>
#include <bettercad/features/LinearPatternFeature.hpp>
#include <bettercad/features/LoftFeature.hpp>
#include <bettercad/features/MirrorFeature.hpp>
#include <bettercad/features/RevolveFeature.hpp>
#include <bettercad/features/RibFeature.hpp>
#include <bettercad/features/ShellFeature.hpp>
#include <bettercad/features/SplitFeature.hpp>
#include <bettercad/features/SweepFeature.hpp>
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

/// {"object": id, "plane": "xy" | "yz" | "xz"}, the object only when set.
[[nodiscard]] Json planeReferenceToJson(const PlaneReference& reference);
[[nodiscard]] Result<PlaneReference> planeReferenceFromJson(const Json& value, std::string_view path);
/// {"object": id, "axis": "x" | "y" | "z"}, the object only when set.
[[nodiscard]] Json axisReferenceToJson(const AxisReference& reference);
[[nodiscard]] Result<AxisReference> axisReferenceFromJson(const Json& value, std::string_view path);

/// "kind": "fixed" with "frame"; "offset" with "base", "offset" (metres) and
/// an optional "offset_parameter"; "angled" with "base", "axis", "angle"
/// (radians) and an optional "angle_parameter".
[[nodiscard]] Json datumPlaneToJson(const features::DatumPlane& datum);
[[nodiscard]] Result<std::unique_ptr<features::DatumPlane>> datumPlaneFromJson(const Json& data, std::string name,
                                                                               std::string_view path);
/// "kind": "fixed" with "axis" {"origin", "direction"}; "intersection" with
/// "first" and "second".
[[nodiscard]] Json datumAxisToJson(const features::DatumAxis& datum);
[[nodiscard]] Result<std::unique_ptr<features::DatumAxis>> datumAxisFromJson(const Json& data, std::string name,
                                                                             std::string_view path);
/// "kind": "fixed" with "frame"; "offset" with an optional "base" (an ID),
/// "x", "y", "z" (metres), "rx", "ry", "rz" (radians) and optional
/// "<key>_parameter"s.
[[nodiscard]] Json coordinateSystemToJson(const features::CoordinateSystem& system);
[[nodiscard]] Result<std::unique_ptr<features::CoordinateSystem>>
coordinateSystemFromJson(const Json& data, std::string name, std::string_view path);

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

/// The profile's ID; the path as {"sketch": id, "edges": [ids]} (in the
/// order of travel); "orientation": "follow_path"; the operation; and an
/// optional target.
[[nodiscard]] Json sweepToJson(const features::SweepFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::SweepFeature>> sweepFromJson(const Json& data, std::string name,
                                                                            std::string_view path);

/// Distances in metres, the angle in radians. Each edge reference is
/// {"curve": "line", "point": [...], "direction": [...]} or
/// {"curve": "circle", "center": [...], "axis": [...], "radius": r}.
[[nodiscard]] Json chamferToJson(const features::ChamferFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::ChamferFeature>>
chamferFromJson(const Json& data, std::string name, std::string_view path);

/// The radius in metres; edge references as for chamfers.
[[nodiscard]] Json filletToJson(const features::FilletFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::FilletFeature>>
filletFromJson(const Json& data, std::string name, std::string_view path);

/// Lengths in metres, the angle in radians; the face as
/// {"surface": "plane", "point": [...], "normal": [...]} and the centre as
/// face-local [u, v]. Keys a type or extent does not use are left out.
[[nodiscard]] Json holeToJson(const features::HoleFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::HoleFeature>>
holeFromJson(const Json& data, std::string name, std::string_view path);

/// The source's ID; each direction as {"direction": [x, y, z] (as given, not
/// normalized), "count": n, "count_parameter": id, "spacing": metres,
/// "spacing_parameter": id}, parameters only when set; "second" only for a
/// grid.
[[nodiscard]] Json linearPatternToJson(const features::LinearPatternFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::LinearPatternFeature>>
linearPatternFromJson(const Json& data, std::string name, std::string_view path);

/// The source's ID; the axis as {"origin": [...] (metres), "direction":
/// [x, y, z] (as given)}; "count" and an optional "count_parameter";
/// "spacing": "full_circle" | "included_angle" | "angle_step", with "angle"
/// (radians) and an optional "angle_parameter" for the last two only; and
/// "rotation": "positive" | "negative".
[[nodiscard]] Json circularPatternToJson(const features::CircularPatternFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::CircularPatternFeature>>
circularPatternFromJson(const Json& data, std::string name, std::string_view path);

/// The source's ID; the plane as {"origin": [...] (metres), "normal":
/// [x, y, z] (as given), "offset": metres} with an optional
/// "offset_parameter"; "scope": "feature" | "body"; and "keep_original".
[[nodiscard]] Json mirrorToJson(const features::MirrorFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::MirrorFeature>> mirrorFromJson(const Json& data, std::string name,
                                                                              std::string_view path);

/// "sections" in the loft's order, each {"sketch": id, "offset": metres}
/// with an optional "offset_parameter"; "interpolation": "ruled"; the
/// operation; and an optional target.
[[nodiscard]] Json loftToJson(const features::LoftFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::LoftFeature>> loftFromJson(const Json& data, std::string name,
                                                                          std::string_view path);

/// "target"; "plane" as a plane reference (see planeReferenceToJson());
/// "keep": "front" | "back" | "both" (P12-FEAT-002).
[[nodiscard]] Json splitToJson(const features::SplitFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::SplitFeature>> splitFromJson(const Json& data, std::string name,
                                                                            std::string_view path);

/// A face name as {"feature": id, "face": {...}}, the face as in a plane
/// reference (P12-FEAT-003). The selector is validated on reading.
[[nodiscard]] Json faceNameToJson(const FaceName& name);
[[nodiscard]] Result<FaceName> faceNameFromJson(const Json& value, std::string_view path);

/// "target"; "profile" (the sketch's ID); "edges" (entity IDs in order);
/// "thickness" in metres with an optional "thickness_parameter";
/// "placement": "symmetric" | "along_normal" | "against_normal"; "flipped"
/// (P12-FEAT-005).
[[nodiscard]] Json ribToJson(const features::RibFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::RibFeature>> ribFromJson(const Json& data, std::string name,
                                                                        std::string_view path);

/// "target"; "faces" as face names; "neutral_plane" as a plane reference;
/// "angle" in radians with an optional "angle_parameter" (P12-FEAT-004).
[[nodiscard]] Json draftToJson(const features::DraftFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::DraftFeature>> draftFromJson(const Json& data, std::string name,
                                                                            std::string_view path);

/// "target"; "open_faces" as face names; "thickness" in metres with an
/// optional "thickness_parameter"; "side": "inward" | "outward"
/// (P12-FEAT-003).
[[nodiscard]] Json shellToJson(const features::ShellFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::ShellFeature>> shellFromJson(const Json& data, std::string name,
                                                                            std::string_view path);

/// "target"; "tools" as feature IDs in order; "operation": "join" | "cut" |
/// "intersect" (P12-FEAT-002).
[[nodiscard]] Json combineToJson(const features::CombineFeature& feature);
[[nodiscard]] Result<std::unique_ptr<features::CombineFeature>> combineFromJson(const Json& data, std::string name,
                                                                                std::string_view path);

} // namespace bettercad::io::detail
