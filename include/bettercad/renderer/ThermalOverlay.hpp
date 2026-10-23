#pragma once

// The glyph data, the boundary classification records and the drawable geometry of the thermal overlays
// (P18-VIZ-001).
//
// NO Qt AND NO OCCT. This turns canonical intent and a qualified result into line segments in SI METRES, and that
// resolution is the part most likely to be wrong, so it lives where a test can reach it without a GPU.
//
// THREE DIFFERENT THINGS ARE DRAWN HERE, AND THEY ARE NOT THE SAME THING.
//
//     recovered q        a VECTOR FIELD inside the solid:  q = -k grad(T), W/m^2, one per tetrahedron.
//                        Source: P18-POST. It points where heat FLOWS (hot to cold). It is drawn as POST gives it:
//                        never sign-flipped, never normalised in storage.
//     applied q''        a SCALAR heat input at a boundary, W/m^2, canonical intent. Source: P18-LOAD. POSITIVE IS
//                        HEAT INTO THE BODY, so its glyph points along -n_out when positive and +n_out when
//                        negative -- the direction the heat travels. It is NOT the recovered q.
//     convection         a boundary exchange h (Tinf - T), canonical intent (h, Tinf). Drawn as a symbol on the
//                        resolved target; no transfer is computed to draw it.
//
// A viewer that drew the recovered vector for a boundary load, or flipped one to look like the other, would be
// showing a physical claim nobody made. The viewer labels each by what it is.
//
// EVERY RESOLUTION IS P16'S. A boundary target reaches facets through `GeometryMeshMap` (`highlightFor`), never by
// a nearest face, a tolerance or a centroid test; the outward normal is the P16 boundary triangle's winding
// (`(p2 - p1) x (p3 - p1)`, outward by P16's construction), CHECKED against the owning tetrahedron by a test.
//
// A CLASSIFIER THAT SUCCEEDED AND CLASSIFIED NOTHING IS NOT A DRAWN BOUNDARY. Every record carries the expected,
// classified and rendered counts, and `BoundaryClassStatus` says which of them disagree.

#include <bettercad/core/Error.hpp>
#include <bettercad/core/Id.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/meshing/GeometryMeshMap.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>
#include <bettercad/renderer/Export.hpp>
#include <bettercad/renderer/MeshInspection.hpp>
#include <bettercad/renderer/MeshView.hpp>
#include <bettercad/thermal/ThermalBoundary.hpp>
#include <bettercad/thermal/ThermalData.hpp>
#include <bettercad/thermal/ThermalLoadVector.hpp>
#include <bettercad/thermal/ThermalPost.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bettercad::renderer {

// ---------------------------------------------------------------------------
// Overlay geometry
// ---------------------------------------------------------------------------

/// What an overlay depicts. A UI colours by this; the geometry does not differ.
enum class ThermalOverlayRole : std::uint8_t {
    /// The recovered conductive heat-flux vector of each tetrahedron (P18-POST).
    HeatFluxVectors,
    /// The applied surface heat flux q'' of each load (P18-LOAD): positive is INTO the body.
    AppliedHeatFluxArrows,
    /// A prescribed temperature on a boundary facet.
    PrescribedTemperatureSymbols,
    /// A convection condition on a boundary facet.
    ConvectionSymbols,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(ThermalOverlayRole role) noexcept;

/// One line segment in SI METRES, in the mesh's own coordinates. The single metres-to-millimetres conversion is
/// `toModelLength` in the presentation adapter, and an overlay passes through it exactly as a mesh buffer does.
struct ThermalOverlaySegment {
    Point3D from{};
    Point3D to{};

    friend bool operator==(const ThermalOverlaySegment&, const ThermalOverlaySegment&) = default;
};

/// An arrow is one shaft and FOUR barbs (two perpendicular pairs, so the head reads from any viewing direction).
constexpr std::size_t kThermalSegmentsPerArrow = 5;
/// A prescribed-temperature symbol: a stem and a cross bar in each of two perpendicular planes.
constexpr std::size_t kThermalSegmentsPerTemperatureSymbol = 3;
/// A convection symbol: a four-segment zigzag.
constexpr std::size_t kThermalSegmentsPerConvectionSymbol = 4;
/// Presentation constants, stated so a test can predict the geometry.
constexpr double kThermalArrowBarbFraction = 0.25;
constexpr double kThermalArrowBarbRadians = 0.4363323129985824; // 25 degrees

/// The segments of one overlay, plus what was left out and why. The cardinality is part of the answer.
///
/// ACCOUNTING INVARIANT: `glyphs + skipped + tooSmall + belowThreshold + duplicatesGrouped` is the number of items
/// given.
struct ThermalOverlay {
    ThermalOverlayRole role = ThermalOverlayRole::HeatFluxVectors;
    std::vector<ThermalOverlaySegment> segments{};

    /// Items that produced geometry.
    std::size_t glyphs = 0;
    /// Items with no direction (a zero heat flux), which produce NO glyph rather than an invented direction.
    std::size_t skipped = 0;
    /// Items whose DISPLAY LENGTH came out non-positive (the caller's scale underflowing against the reference).
    std::size_t tooSmall = 0;
    /// Items suppressed by the display threshold. A READABILITY choice only: the inspector still returns the
    /// actual value, and nothing in the result is zeroed.
    std::size_t belowThreshold = 0;
    /// Items that coincide exactly with one already drawn (the same prescribed temperature on the same facet).
    /// Grouped, not erased: the canonical conditions are untouched and each is still listed in the glyph data.
    std::size_t duplicatesGrouped = 0;
    /// Glyphs of a boundary overlay whose classifier cardinality is NOT Complete (partial, zero-classified, unresolved
    /// or inconsistent). They are still drawn from what was classified, and this count is what lets the caller refuse
    /// to present the overlay as the whole target. Not part of the accounting invariant: it counts glyph RECORDS, not
    /// facets, and is always zero for the heat-flux vectors.
    std::size_t incompleteTargets = 0;

    [[nodiscard]] bool empty() const noexcept { return segments.empty(); }
};

/// Whether @p scale is usable as an arrow display scale: finite and strictly positive.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT bool isUsableThermalArrowScale(double scale) noexcept;

/// A display length, in METRES of model space, for a vector of @p magnitude (`scale * magnitude / reference`).
///
/// PRESENTATION ONLY, and the units are the point: a flux is W/m^2 and a length is metres, and there is no physical
/// conversion between them. Changing @p scale changes what is drawn and nothing else. Returns zero for a zero
/// magnitude (no minimum glyph size). Refuses a non-finite or non-positive @p scale or @p reference and a negative
/// or non-finite @p magnitude.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<double> thermalDisplayArrowLength(double magnitude, double reference,
                                                                               double scale);

// ---------------------------------------------------------------------------
// The recovered heat-flux vectors
// ---------------------------------------------------------------------------

/// One recovered heat-flux vector, located at the centroid of its tetrahedron.
struct HeatFluxVectorGlyph {
    meshing::ElementId element{};
    /// The ordinal in the result's element arrays.
    std::size_t ordinal = 0;
    /// The centroid, a presentation anchor in SI metres (the mean of the four corner positions).
    Point3D centroid{};
    /// POST's stored vector, W/m^2, untouched.
    thermal::HeatFlux3 flux{};
    /// POST's stored |q|, W/m^2.
    HeatFlux magnitude{};
    /// `q / |q|`, a LOCAL display direction (unit, dimensionless); the stored flux is never normalised. Zero when
    /// `hasDirection` is false.
    Vector3D direction{};
    /// Whether the flux has a direction at all. A zero flux has none and is not given one.
    bool hasDirection = false;

    friend bool operator==(const HeatFluxVectorGlyph&, const HeatFluxVectorGlyph&) = default;
};

/// The vector glyphs of stored state @p state: one per element of @p elements, or one per tetrahedron (in POST's
/// order) when @p elements is empty. The caller's density policy is the list it passes; the data is complete.
///
/// Refuses a result that was not recovered on @p mesh, an out-of-range state, and an element the result does not
/// hold -- rather than dropping it, because a shorter list would hide that the caller and the result disagree.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<std::vector<HeatFluxVectorGlyph>>
heatFluxVectorGlyphs(const meshing::Mesh& mesh, const thermal::ThermalPostResult& post, std::size_t state,
                     std::span<const meshing::ElementId> elements = {});

/// An arrow per vector glyph, TAIL AT THE CENTROID, pointing along `q / |q|` -- the direction heat flows.
///
/// A zero flux is SKIPPED and counted. @p minimumFraction suppresses arrows whose `|q| / reference` is below it
/// (a readability threshold: counted in `belowThreshold`, and inspection is unaffected). Refuses an unusable scale
/// or reference or a fraction outside `[0, 1)`.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ThermalOverlay>
heatFluxVectorOverlay(std::span<const HeatFluxVectorGlyph> glyphs, double scale, double reference,
                      double minimumFraction = 0.0);

// ---------------------------------------------------------------------------
// Boundary classification, with its cardinality
// ---------------------------------------------------------------------------

/// What a boundary glyph depicts.
enum class BoundaryTargetKind : std::uint8_t {
    AppliedHeatFlux,
    PrescribedTemperature,
    Convection,
    NamedBoundarySet,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(BoundaryTargetKind kind) noexcept;

/// How the three counts of one boundary target relate.
enum class BoundaryClassStatus : std::uint8_t {
    /// Resolved, non-empty, and expected == classified == rendered.
    Complete,
    /// A reference no longer resolves. KEPT and reported; nothing is rebound to a nearby face.
    Unresolved,
    /// The classifier reported success and classified NOTHING (or the core classified none of what the map
    /// resolved). THE P16 TRAP: this is a failure, never an empty highlight that looks like a success.
    ZeroClassified,
    /// Fewer entities were classified or rendered than the mapping resolved. Not complete, and not reported so.
    Partial,
    /// More were classified or rendered than the mapping resolved: the owners disagree.
    Inconsistent,
};

[[nodiscard]] BETTERCAD_RENDERER_EXPORT std::string_view toString(BoundaryClassStatus status) noexcept;

/// The expected, classified and rendered counts of one boundary target.
///
/// ```text
/// expected    facets P16's mapping resolved the target to (the forward answer)
/// classified  facets the owner of the target's physics integrated over (P18-LOAD/BC) -- or, for a named set,
///             the facets P16's REVERSE linkage attributes to the set's faces
/// rendered    render triangles the highlight covers
/// ```
struct BoundaryCardinality {
    BoundaryTargetKind kind = BoundaryTargetKind::AppliedHeatFlux;
    /// The load, condition or set id (its numeric value), for the report.
    std::uint64_t id = 0;
    std::size_t expected = 0;
    std::size_t classified = 0;
    std::size_t rendered = 0;
    BoundaryClassStatus status = BoundaryClassStatus::ZeroClassified;

    /// Only `Complete`. A predicate of the status, so a record cannot claim to be whole with a count missing.
    [[nodiscard]] bool complete() const noexcept { return status == BoundaryClassStatus::Complete; }

    friend bool operator==(const BoundaryCardinality&, const BoundaryCardinality&) = default;
};

/// The cardinality record for @p mapping given @p classified and @p rendered.
///
/// THE PURE FUNCTION THE TRAP IS TESTED THROUGH: a hand-built mapping that resolved with zero facets, or a
/// classifier count of zero, is judged here exactly as a real one would be.
///
/// ```text
/// not every reference resolved                   -> Unresolved
/// expected == 0 and classified > 0               -> Inconsistent
/// expected == 0 or classified == 0               -> ZeroClassified
/// classified or rendered above expected          -> Inconsistent
/// classified or rendered below expected          -> Partial
/// otherwise                                      -> Complete
/// ```
[[nodiscard]] BETTERCAD_RENDERER_EXPORT BoundaryCardinality
classifyBoundaryCardinality(BoundaryTargetKind kind, std::uint64_t id, const meshing::BoundaryFacetSet& mapping,
                            std::size_t classified, std::size_t rendered) noexcept;

// --- the glyph data ---------------------------------------------------------

/// One applied surface heat flux q'' (P18-LOAD), with its resolved boundary facets.
struct AppliedHeatFluxGlyph {
    ThermalLoadId load{};
    FaceName face{};
    /// The canonical q'' in W/m^2, POSITIVE INTO THE BODY. Read from the canonical load.
    HeatFlux flux{};
    /// The area P18-LOAD integrated over.
    Area area{};
    /// The facets P16 resolved the face to, ascending.
    std::vector<meshing::ElementId> facets{};
    BoundaryCardinality cardinality{};
    /// Whether the flux has a direction (a zero flux has none).
    bool hasDirection = false;

    friend bool operator==(const AppliedHeatFluxGlyph&, const AppliedHeatFluxGlyph&) = default;
};

/// One prescribed temperature (P18-BC), with its resolved boundary facets.
struct PrescribedTemperatureGlyph {
    ThermalBoundaryConditionId condition{};
    FaceName face{};
    /// The canonical Tbar in K. Never converted for display here.
    Temperature value{};
    std::vector<meshing::ElementId> facets{};
    BoundaryCardinality cardinality{};

    friend bool operator==(const PrescribedTemperatureGlyph&, const PrescribedTemperatureGlyph&) = default;
};

/// One convection condition (P18-BC), with its resolved boundary facets.
struct ConvectionGlyph {
    ThermalBoundaryConditionId condition{};
    FaceName face{};
    /// The canonical h in W/(m^2 K) and Tinf in K.
    HeatTransferCoefficient coefficient{};
    Temperature ambient{};
    std::vector<meshing::ElementId> facets{};
    BoundaryCardinality cardinality{};

    friend bool operator==(const ConvectionGlyph&, const ConvectionGlyph&) = default;
};

/// A named boundary set, highlighted.
struct NamedBoundaryRecord {
    BoundarySetId set{};
    std::string name{};
    /// P16's mapping, carried whole, so an unresolved reference stays distinguishable from an empty result.
    MeshHighlight highlight{};
    BoundaryCardinality cardinality{};
};

/// The glyphs of the surface heat-flux loads of @p loads, in the order given. A volumetric load has no face and
/// produces no glyph. @p prepared supplies the facet count P18-LOAD integrated over.
///
/// @p view must be the volume-boundary view (the Triangle3 elements), because P16's facets are Triangle3 elements.
/// Refuses a load whose prepared contribution is missing, and an unusable mesh, view or map.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<std::vector<AppliedHeatFluxGlyph>>
appliedHeatFluxGlyphs(const MeshView& view, const meshing::Mesh& mesh, const meshing::GeometryMeshMap& map,
                      std::span<const thermal::ThermalLoad> loads, const thermal::PreparedThermalLoads& prepared);

[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<std::vector<PrescribedTemperatureGlyph>>
prescribedTemperatureGlyphs(const MeshView& view, const meshing::Mesh& mesh, const meshing::GeometryMeshMap& map,
                            std::span<const thermal::ThermalBoundaryCondition> conditions,
                            const thermal::PreparedThermalBoundary& prepared);

[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<std::vector<ConvectionGlyph>>
convectionGlyphs(const MeshView& view, const meshing::Mesh& mesh, const meshing::GeometryMeshMap& map,
                 std::span<const thermal::ThermalBoundaryCondition> conditions,
                 const thermal::PreparedThermalBoundary& prepared);

/// The highlight of @p set and its cardinality. Expected is the forward resolution; classified is the REVERSE
/// linkage (`sourceOfFacet` over every boundary triangle, counting those attributed to a face the set resolved
/// to); rendered is the highlight's triangle count. Three independent answers that must agree.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<NamedBoundaryRecord>
namedBoundaryHighlight(const MeshView& view, const meshing::Mesh& mesh, const meshing::GeometryMeshMap& map,
                       const meshing::NamedBoundarySet& set);

// --- the drawable geometry --------------------------------------------------

/// Whether @p length is usable as a symbol length: finite and strictly positive.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT bool isUsableThermalSymbolLength(double length) noexcept;

/// An arrow per facet of each glyph, HEAD AT THE FACET CENTROID, pointing along the direction the heat TRAVELS:
///
/// ```text
/// q'' > 0   heat enters the body   direction = -n_out   (the head arrives at the surface from outside)
/// q'' < 0   heat leaves the body   direction = +n_out   (the head arrives at the surface from inside the body, on its way out)
/// q'' = 0   no direction, no arrow (counted in `skipped`)
/// ```
///
/// The sign is P18-LOAD's, read from the canonical load; nothing here re-derives it. The length is
/// `thermalDisplayArrowLength(|q''|, reference, scale)`. REFUSES a facet the mesh does not hold or that has no
/// outward normal.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ThermalOverlay>
appliedHeatFluxOverlay(const meshing::Mesh& mesh, std::span<const AppliedHeatFluxGlyph> glyphs, double scale,
                       double reference);

/// A symbol per facet of each glyph, standing on the facet centroid along the OUTWARD normal. An identical
/// prescribed temperature on the same facet is drawn once (`duplicatesGrouped`); the canonical conditions are
/// untouched.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ThermalOverlay>
prescribedTemperatureOverlay(const meshing::Mesh& mesh, std::span<const PrescribedTemperatureGlyph> glyphs,
                             double symbolLength);

/// A symbol per facet of each convection glyph. CONVECTION IS ADDITIVE: two conditions on one facet are both
/// drawn, never merged.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<ThermalOverlay>
convectionOverlay(const meshing::Mesh& mesh, std::span<const ConvectionGlyph> glyphs, double symbolLength);

/// The unit outward normal of boundary triangle @p facet of @p mesh, from the stored winding, or an error when the
/// mesh holds no such triangle or it has zero area.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<Vector3D> outwardNormalOf(const meshing::Mesh& mesh,
                                                                         meshing::ElementId facet);

/// The centroid of boundary triangle @p facet of @p mesh.
[[nodiscard]] BETTERCAD_RENDERER_EXPORT Result<Point3D> facetCentroidOf(const meshing::Mesh& mesh,
                                                                        meshing::ElementId facet);

} // namespace bettercad::renderer
