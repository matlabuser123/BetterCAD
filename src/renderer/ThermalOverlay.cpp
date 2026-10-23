// The glyph data, the boundary classification records and the drawable geometry of the thermal overlays
// (P18-VIZ-001).
//
// NO THERMAL PHYSICS IS COMPUTED HERE. A vector glyph carries POST's stored flux and the direction `q / |q|` taken
// locally for drawing; an applied-flux arrow takes its sign from the canonical load; a symbol is a pose on a facet.
// The only arithmetic is geometry in SI metres: a centroid, a unit outward normal, a perpendicular frame and the
// points of a glyph. The metres-to-millimetres conversion is not here (it is `toModelLength` in the presentation
// adapter), and a grep of the CODE here for the literal conversion factor finds nothing.

#include <bettercad/renderer/ThermalOverlay.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <set>
#include <utility>

namespace bettercad::renderer {
namespace {

using V3 = std::array<double, 3>;

[[nodiscard]] V3 positionOf(const Point3D& p) noexcept { return {p.x.si(), p.y.si(), p.z.si()}; }
[[nodiscard]] Point3D pointOf(const V3& v) noexcept {
    return Point3D{Length::fromSi(v[0]), Length::fromSi(v[1]), Length::fromSi(v[2])};
}
[[nodiscard]] V3 plus(const V3& a, const V3& b) noexcept { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
[[nodiscard]] V3 minus(const V3& a, const V3& b) noexcept { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
[[nodiscard]] V3 times(const V3& a, double s) noexcept { return {a[0] * s, a[1] * s, a[2] * s}; }
[[nodiscard]] V3 crossOf(const V3& a, const V3& b) noexcept {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}
[[nodiscard]] double lengthOf(const V3& a) noexcept { return std::hypot(a[0], a[1], a[2]); }

/// Two unit vectors perpendicular to the unit vector @p d and to each other. The axis least aligned with @p d seeds
/// the first, so the cross product never degenerates.
[[nodiscard]] std::pair<V3, V3> perpendicularFrame(const V3& d) noexcept {
    const double ax = std::abs(d[0]);
    const double ay = std::abs(d[1]);
    const double az = std::abs(d[2]);
    V3 seed{0.0, 0.0, 1.0};
    if (ax <= ay && ax <= az) {
        seed = {1.0, 0.0, 0.0};
    } else if (ay <= az) {
        seed = {0.0, 1.0, 0.0};
    }
    V3 u = crossOf(d, seed);
    u = times(u, 1.0 / lengthOf(u));
    const V3 v = crossOf(d, u);
    return {u, v};
}

void addSegment(std::vector<ThermalOverlaySegment>& out, const V3& from, const V3& to) {
    out.push_back(ThermalOverlaySegment{pointOf(from), pointOf(to)});
}

/// One shaft ending at @p head, travelling along the unit direction @p d, with four barbs. The tail is
/// `head - length * d`.
void addArrow(std::vector<ThermalOverlaySegment>& out, const V3& head, const V3& d, double length) {
    addSegment(out, minus(head, times(d, length)), head);
    const auto [u, v] = perpendicularFrame(d);
    const double barb = length * kThermalArrowBarbFraction;
    const double c = std::cos(kThermalArrowBarbRadians);
    const double s = std::sin(kThermalArrowBarbRadians);
    for (const V3& w : {u, times(u, -1.0), v, times(v, -1.0)}) {
        // The barb leans back along the shaft by cos(theta) and sideways by sin(theta).
        addSegment(out, head, plus(head, times(plus(times(d, -c), times(w, s)), barb)));
    }
}

[[nodiscard]] std::unexpected<Error> failWith(ErrorCode code, std::string message) {
    return makeError(code, std::move(message));
}

/// The three corners of boundary triangle @p facet, or the reason there are none.
[[nodiscard]] Result<std::array<V3, 3>> cornersOf(const meshing::Mesh& mesh, meshing::ElementId facet) {
    const meshing::Triangle* triangle = mesh.findTriangle(facet);
    if (triangle == nullptr) {
        return failWith(ErrorCode::NotFound,
                        std::format("element {} is not a boundary triangle of the mesh", facet.value()));
    }
    std::array<V3, 3> out{};
    for (std::size_t i = 0U; i < 3U; ++i) {
        const meshing::Node* node = mesh.findNode(triangle->nodes[i]);
        if (node == nullptr) {
            return failWith(ErrorCode::NotFound,
                            std::format("boundary triangle {} names node {} and the mesh holds no such node",
                                        facet.value(), triangle->nodes[i].value()));
        }
        out[i] = positionOf(node->position);
    }
    return out;
}

} // namespace

std::string_view toString(ThermalOverlayRole role) noexcept {
    switch (role) {
    case ThermalOverlayRole::HeatFluxVectors:
        return "heat_flux_vectors";
    case ThermalOverlayRole::AppliedHeatFluxArrows:
        return "applied_heat_flux_arrows";
    case ThermalOverlayRole::PrescribedTemperatureSymbols:
        return "prescribed_temperature_symbols";
    case ThermalOverlayRole::ConvectionSymbols:
        return "convection_symbols";
    }
    return "unknown";
}

std::string_view toString(BoundaryTargetKind kind) noexcept {
    switch (kind) {
    case BoundaryTargetKind::AppliedHeatFlux:
        return "applied_heat_flux";
    case BoundaryTargetKind::PrescribedTemperature:
        return "prescribed_temperature";
    case BoundaryTargetKind::Convection:
        return "convection";
    case BoundaryTargetKind::NamedBoundarySet:
        return "named_boundary_set";
    }
    return "unknown";
}

std::string_view toString(BoundaryClassStatus status) noexcept {
    switch (status) {
    case BoundaryClassStatus::Complete:
        return "complete";
    case BoundaryClassStatus::Unresolved:
        return "unresolved";
    case BoundaryClassStatus::ZeroClassified:
        return "zero_classified";
    case BoundaryClassStatus::Partial:
        return "partial";
    case BoundaryClassStatus::Inconsistent:
        return "inconsistent";
    }
    return "unknown";
}

bool isUsableThermalArrowScale(double scale) noexcept { return std::isfinite(scale) && scale > 0.0; }

bool isUsableThermalSymbolLength(double length) noexcept { return std::isfinite(length) && length > 0.0; }

Result<double> thermalDisplayArrowLength(double magnitude, double reference, double scale) {
    if (!isUsableThermalArrowScale(scale)) {
        return failWith(ErrorCode::InvalidArgument,
                        std::format("the arrow scale {} is not finite and positive", scale));
    }
    if (!isUsableThermalArrowScale(reference)) {
        return failWith(ErrorCode::InvalidArgument,
                        std::format("the arrow reference {} is not finite and positive", reference));
    }
    if (!std::isfinite(magnitude) || magnitude < 0.0) {
        return failWith(ErrorCode::InvalidArgument,
                        std::format("the magnitude {} is not a finite non-negative number", magnitude));
    }
    // NO MINIMUM GLYPH SIZE: a floor belongs to the layer that knows its pixel budget, and one here would make a
    // zero flux look like a small one.
    // THE LENGTH MUST BE A NUMBER. Every operand is finite and the quotient can still overflow (a reference of 1e-300
    // against a flux of 1e10): an infinite arrow is not a large one, and 0 * inf downstream would be NaN geometry.
    const double length = scale * (magnitude / reference);
    if (!std::isfinite(length)) {
        return failWith(ErrorCode::InvalidArgument,
                        std::format("the arrow length for magnitude {} against reference {} at scale {} overflows",
                                    magnitude, reference, scale));
    }
    return length;
}

// ---------------------------------------------------------------------------
// Geometry of one facet
// ---------------------------------------------------------------------------

Result<Vector3D> outwardNormalOf(const meshing::Mesh& mesh, meshing::ElementId facet) {
    const Result<std::array<V3, 3>> corners = cornersOf(mesh, facet);
    if (!corners.has_value()) {
        return std::unexpected(corners.error());
    }
    // THE STORED WINDING: n is proportional to (p2 - p1) x (p3 - p1), outward by P16's construction (the boundary
    // triangles carry the winding of the tetrahedron that owns them). A test checks it against that tetrahedron.
    const V3 area = crossOf(minus((*corners)[1], (*corners)[0]), minus((*corners)[2], (*corners)[0]));
    const double norm = lengthOf(area);
    if (!(norm > 0.0) || !std::isfinite(norm)) {
        return failWith(ErrorCode::FailedPrecondition,
                        std::format("boundary triangle {} has no outward normal (zero or non-finite area)",
                                    facet.value()));
    }
    return Vector3D{area[0] / norm, area[1] / norm, area[2] / norm};
}

Result<Point3D> facetCentroidOf(const meshing::Mesh& mesh, meshing::ElementId facet) {
    const Result<std::array<V3, 3>> corners = cornersOf(mesh, facet);
    if (!corners.has_value()) {
        return std::unexpected(corners.error());
    }
    const V3 sum = plus(plus((*corners)[0], (*corners)[1]), (*corners)[2]);
    return pointOf(times(sum, 1.0 / 3.0));
}

// ---------------------------------------------------------------------------
// The recovered heat-flux vectors
// ---------------------------------------------------------------------------

Result<std::vector<HeatFluxVectorGlyph>> heatFluxVectorGlyphs(const meshing::Mesh& mesh,
                                                              const thermal::ThermalPostResult& post,
                                                              std::size_t state,
                                                              std::span<const meshing::ElementId> elements) {
    if (!mesh.owns(post.source().system.mesh)) {
        return failWith(ErrorCode::FailedPrecondition,
                        "the result was recovered on another mesh generation than the mesh presented");
    }
    if (state >= post.stateCount()) {
        return failWith(ErrorCode::InvalidArgument,
                        std::format("state {} requested of a result holding {}", state, post.stateCount()));
    }
    const std::span<const meshing::ElementId> all = post.elementIds();
    const std::span<const meshing::ElementId> wanted = elements.empty() ? all : elements;

    std::vector<HeatFluxVectorGlyph> out;
    out.reserve(wanted.size());
    for (const meshing::ElementId element : wanted) {
        // THROUGH THE ELEMENT IDENTITY, never the raw id as an index.
        const std::optional<std::size_t> ordinal = post.ordinalOf(element);
        const meshing::Tetrahedron* tet = mesh.findTetrahedron(element);
        if (!ordinal.has_value() || tet == nullptr) {
            return failWith(ErrorCode::NotFound,
                            std::format("element {} is not a tetrahedron the result holds", element.value()));
        }
        V3 sum{0.0, 0.0, 0.0};
        for (const meshing::NodeId id : tet->nodes) {
            const meshing::Node* node = mesh.findNode(id);
            if (node == nullptr) {
                return failWith(ErrorCode::NotFound,
                                std::format("tetrahedron {} names node {} and the mesh holds no such node",
                                            element.value(), id.value()));
            }
            sum = plus(sum, positionOf(node->position));
        }
        const Result<thermal::HeatFlux3> flux = post.heatFluxOf(state, element);
        if (!flux.has_value()) {
            return std::unexpected(flux.error());
        }
        const Result<HeatFlux> magnitude = post.heatFluxMagnitudeOf(state, element);
        if (!magnitude.has_value()) {
            return std::unexpected(magnitude.error());
        }
        HeatFluxVectorGlyph glyph;
        glyph.element = element;
        glyph.ordinal = *ordinal;
        glyph.centroid = pointOf(times(sum, 0.25));
        glyph.flux = *flux;
        glyph.magnitude = *magnitude;
        // The display direction is formed LOCALLY and the stored flux is left exactly as POST made it. A zero
        // flux has no direction and is not given one.
        const double m = glyph.magnitude.si();
        if (m > 0.0 && std::isfinite(m)) {
            glyph.direction = Vector3D{flux->x.si() / m, flux->y.si() / m, flux->z.si() / m};
            glyph.hasDirection = true;
        }
        out.push_back(glyph);
    }
    return out;
}

Result<ThermalOverlay> heatFluxVectorOverlay(std::span<const HeatFluxVectorGlyph> glyphs, double scale,
                                             double reference, double minimumFraction) {
    if (!isUsableThermalArrowScale(scale) || !isUsableThermalArrowScale(reference)) {
        return failWith(ErrorCode::InvalidArgument,
                        std::format("the arrow scale {} and reference {} must both be finite and positive", scale,
                                    reference));
    }
    if (!std::isfinite(minimumFraction) || minimumFraction < 0.0 || minimumFraction >= 1.0) {
        return failWith(ErrorCode::InvalidArgument,
                        std::format("the display threshold {} is not in [0, 1)", minimumFraction));
    }
    ThermalOverlay out;
    out.role = ThermalOverlayRole::HeatFluxVectors;
    for (const HeatFluxVectorGlyph& glyph : glyphs) {
        if (!glyph.hasDirection) {
            ++out.skipped;
            continue;
        }
        if (glyph.magnitude.si() / reference < minimumFraction) {
            ++out.belowThreshold;
            continue;
        }
        const Result<double> length = thermalDisplayArrowLength(glyph.magnitude.si(), reference, scale);
        if (!length.has_value()) {
            return std::unexpected(length.error());
        }
        if (!(*length > 0.0)) {
            ++out.tooSmall;
            continue;
        }
        const V3 direction{glyph.direction.x, glyph.direction.y, glyph.direction.z};
        const V3 tail = positionOf(glyph.centroid);
        // TAIL AT THE CENTROID, head `length` further along q / |q|: the direction heat flows, as POST gives it.
        addArrow(out.segments, plus(tail, times(direction, *length)), direction, *length);
        ++out.glyphs;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Boundary classification
// ---------------------------------------------------------------------------

BoundaryCardinality classifyBoundaryCardinality(BoundaryTargetKind kind, std::uint64_t id,
                                                const meshing::BoundaryFacetSet& mapping, std::size_t classified,
                                                std::size_t rendered) noexcept {
    BoundaryCardinality out;
    out.kind = kind;
    out.id = id;
    out.expected = mapping.facets.size();
    out.classified = classified;
    out.rendered = rendered;
    if (!mapping.fullyResolved()) {
        out.status = BoundaryClassStatus::Unresolved;
    } else if (out.expected == 0U && classified > 0U) {
        out.status = BoundaryClassStatus::Inconsistent;
    } else if (out.expected == 0U || classified == 0U) {
        // THE TRAP: the classifier succeeded and classified nothing. Not a complete highlight, not an empty one.
        out.status = BoundaryClassStatus::ZeroClassified;
    } else if (classified > out.expected || rendered > out.expected) {
        out.status = BoundaryClassStatus::Inconsistent;
    } else if (classified < out.expected || rendered < out.expected) {
        out.status = BoundaryClassStatus::Partial;
    } else {
        out.status = BoundaryClassStatus::Complete;
    }
    return out;
}

Result<std::vector<AppliedHeatFluxGlyph>> appliedHeatFluxGlyphs(const MeshView& view, const meshing::Mesh& mesh,
                                                               const meshing::GeometryMeshMap& map,
                                                               std::span<const thermal::ThermalLoad> loads,
                                                               const thermal::PreparedThermalLoads& prepared) {
    if (!view.describes(mesh) || !prepared.describes(mesh)) {
        return failWith(ErrorCode::FailedPrecondition,
                        "the mesh view and the prepared loads must both describe the mesh presented");
    }
    // THE LOADS ASKED FOR MUST BE THE LOADS THE CONTRIBUTIONS WERE INTEGRATED FROM: by value, the comparison the
    // core's own isCurrent makes. Counts that agree prove nothing about a load moved to another face.
    if (!std::ranges::equal(prepared.source().loads, loads)) {
        return failWith(ErrorCode::FailedPrecondition,
                        "the loads presented are not the loads the prepared contributions were integrated from");
    }
    std::vector<AppliedHeatFluxGlyph> out;
    for (const thermal::ThermalLoad& load : loads) {
        const thermal::SurfaceHeatFluxLoad* surface = load.surfaceHeatFlux();
        if (surface == nullptr) {
            continue; // a volumetric source has no face
        }
        const thermal::ThermalLoadContribution* contribution = nullptr;
        for (const thermal::ThermalLoadContribution& candidate : prepared.contributions()) {
            if (candidate.load == load.id()) {
                contribution = &candidate;
                break;
            }
        }
        if (contribution == nullptr) {
            return failWith(ErrorCode::NotFound,
                            std::format("the prepared loads hold no contribution for load {}", load.id().value()));
        }
        Result<MeshHighlight> highlight = highlightFor(view, mesh, map, surface->face);
        if (!highlight.has_value()) {
            return std::unexpected(highlight.error());
        }
        AppliedHeatFluxGlyph glyph;
        glyph.load = load.id();
        glyph.face = surface->face;
        glyph.flux = surface->flux;
        glyph.area = contribution->integrated.area;
        glyph.facets = highlight->mapping.facets;
        glyph.hasDirection = std::isfinite(surface->flux.si()) && surface->flux.si() != 0.0;
        glyph.cardinality = classifyBoundaryCardinality(
            BoundaryTargetKind::AppliedHeatFlux, static_cast<std::uint64_t>(load.id().value()), highlight->mapping,
            contribution->integrated.entities, highlight->triangles.size());
        out.push_back(std::move(glyph));
    }
    return out;
}

Result<std::vector<PrescribedTemperatureGlyph>>
prescribedTemperatureGlyphs(const MeshView& view, const meshing::Mesh& mesh, const meshing::GeometryMeshMap& map,
                            std::span<const thermal::ThermalBoundaryCondition> conditions,
                            const thermal::PreparedThermalBoundary& prepared) {
    if (!view.describes(mesh) || !prepared.describes(mesh)) {
        return failWith(ErrorCode::FailedPrecondition,
                        "the mesh view and the prepared boundary must both describe the mesh presented");
    }
    if (!std::ranges::equal(prepared.source().conditions, conditions)) {
        return failWith(ErrorCode::FailedPrecondition,
                        "the conditions presented are not the conditions the prepared boundary was resolved from");
    }
    std::vector<PrescribedTemperatureGlyph> out;
    for (const thermal::ThermalBoundaryCondition& condition : conditions) {
        const thermal::PrescribedTemperature* held = condition.prescribedTemperature();
        if (held == nullptr) {
            continue;
        }
        const thermal::PreparedTemperatureBoundary* resolved = nullptr;
        for (const thermal::PreparedTemperatureBoundary& candidate : prepared.temperatures()) {
            if (candidate.condition == condition.id()) {
                resolved = &candidate;
                break;
            }
        }
        if (resolved == nullptr) {
            return failWith(ErrorCode::NotFound,
                            std::format("the prepared boundary holds no resolution for condition {}",
                                        condition.id().value()));
        }
        Result<MeshHighlight> highlight = highlightFor(view, mesh, map, held->face);
        if (!highlight.has_value()) {
            return std::unexpected(highlight.error());
        }
        PrescribedTemperatureGlyph glyph;
        glyph.condition = condition.id();
        glyph.face = held->face;
        glyph.value = held->value;
        glyph.facets = highlight->mapping.facets;
        glyph.cardinality = classifyBoundaryCardinality(
            BoundaryTargetKind::PrescribedTemperature, static_cast<std::uint64_t>(condition.id().value()),
            highlight->mapping, resolved->facetCount, highlight->triangles.size());
        out.push_back(std::move(glyph));
    }
    return out;
}

Result<std::vector<ConvectionGlyph>> convectionGlyphs(const MeshView& view, const meshing::Mesh& mesh,
                                                      const meshing::GeometryMeshMap& map,
                                                      std::span<const thermal::ThermalBoundaryCondition> conditions,
                                                      const thermal::PreparedThermalBoundary& prepared) {
    if (!view.describes(mesh) || !prepared.describes(mesh)) {
        return failWith(ErrorCode::FailedPrecondition,
                        "the mesh view and the prepared boundary must both describe the mesh presented");
    }
    if (!std::ranges::equal(prepared.source().conditions, conditions)) {
        return failWith(ErrorCode::FailedPrecondition,
                        "the conditions presented are not the conditions the prepared boundary was resolved from");
    }
    std::vector<ConvectionGlyph> out;
    for (const thermal::ThermalBoundaryCondition& condition : conditions) {
        const thermal::ConvectionBoundary* held = condition.convection();
        if (held == nullptr) {
            continue;
        }
        const thermal::PreparedConvectionBoundary* resolved = nullptr;
        for (const thermal::PreparedConvectionBoundary& candidate : prepared.convections()) {
            if (candidate.condition == condition.id()) {
                resolved = &candidate;
                break;
            }
        }
        if (resolved == nullptr) {
            return failWith(ErrorCode::NotFound,
                            std::format("the prepared boundary holds no resolution for condition {}",
                                        condition.id().value()));
        }
        Result<MeshHighlight> highlight = highlightFor(view, mesh, map, held->face);
        if (!highlight.has_value()) {
            return std::unexpected(highlight.error());
        }
        ConvectionGlyph glyph;
        glyph.condition = condition.id();
        glyph.face = held->face;
        glyph.coefficient = held->coefficient;
        glyph.ambient = held->ambient;
        glyph.facets = highlight->mapping.facets;
        glyph.cardinality = classifyBoundaryCardinality(
            BoundaryTargetKind::Convection, static_cast<std::uint64_t>(condition.id().value()), highlight->mapping,
            resolved->facets.size(), highlight->triangles.size());
        out.push_back(std::move(glyph));
    }
    return out;
}

Result<NamedBoundaryRecord> namedBoundaryHighlight(const MeshView& view, const meshing::Mesh& mesh,
                                                   const meshing::GeometryMeshMap& map,
                                                   const meshing::NamedBoundarySet& set) {
    Result<MeshHighlight> highlight = highlightFor(view, mesh, map, set);
    if (!highlight.has_value()) {
        return std::unexpected(highlight.error());
    }
    // THE REVERSE LINKAGE, AS THE INDEPENDENT COUNT. The forward answer (CAD face -> facets) is `expected`; this
    // asks every boundary triangle where it came from (mesh -> CAD) and counts those attributed to a face the set
    // resolved to. Two directions of P16's mapping that must agree, plus the render triangles that must cover them.
    std::vector<std::size_t> faces;
    for (const meshing::FaceResolution& resolution : highlight->mapping.requested) {
        if (resolution.state == meshing::MappingState::Resolved) {
            faces.insert(faces.end(), resolution.faces.begin(), resolution.faces.end());
        }
    }
    std::ranges::sort(faces);
    faces.erase(std::unique(faces.begin(), faces.end()), faces.end());
    std::size_t classified = 0U;
    for (const meshing::Triangle& triangle : mesh.triangles()) {
        const Result<meshing::FacetSource> source = sourceOfFacet(mesh, map, triangle.id);
        if (source.has_value() && std::ranges::binary_search(faces, source->face)) {
            ++classified;
        }
    }
    NamedBoundaryRecord out;
    out.set = set.id;
    out.name = set.name;
    out.cardinality = classifyBoundaryCardinality(BoundaryTargetKind::NamedBoundarySet,
                                                  static_cast<std::uint64_t>(set.id.value()), highlight->mapping,
                                                  classified, highlight->triangles.size());
    out.highlight = std::move(*highlight);
    return out;
}

// ---------------------------------------------------------------------------
// The drawable boundary geometry
// ---------------------------------------------------------------------------

Result<ThermalOverlay> appliedHeatFluxOverlay(const meshing::Mesh& mesh, std::span<const AppliedHeatFluxGlyph> glyphs,
                                              double scale, double reference) {
    if (!isUsableThermalArrowScale(scale) || !isUsableThermalArrowScale(reference)) {
        return failWith(ErrorCode::InvalidArgument,
                        std::format("the arrow scale {} and reference {} must both be finite and positive", scale,
                                    reference));
    }
    ThermalOverlay out;
    out.role = ThermalOverlayRole::AppliedHeatFluxArrows;
    for (const AppliedHeatFluxGlyph& glyph : glyphs) {
        if (glyph.cardinality.status != BoundaryClassStatus::Complete) {
            ++out.incompleteTargets;
        }
        if (!glyph.hasDirection) {
            out.skipped += glyph.facets.size();
            continue;
        }
        const Result<double> length = thermalDisplayArrowLength(std::abs(glyph.flux.si()), reference, scale);
        if (!length.has_value()) {
            return std::unexpected(length.error());
        }
        if (!(*length > 0.0)) {
            out.tooSmall += glyph.facets.size();
            continue;
        }
        // POSITIVE q'' IS HEAT INTO THE BODY, so the heat travels along -n_out; a negative one leaves along +n_out.
        // The sign is the canonical load's, read here and not re-derived.
        const double along = glyph.flux.si() > 0.0 ? -1.0 : 1.0;
        for (const meshing::ElementId facet : glyph.facets) {
            const Result<Vector3D> normal = outwardNormalOf(mesh, facet);
            if (!normal.has_value()) {
                return std::unexpected(normal.error());
            }
            const Result<Point3D> centroid = facetCentroidOf(mesh, facet);
            if (!centroid.has_value()) {
                return std::unexpected(centroid.error());
            }
            const V3 direction{along * normal->x, along * normal->y, along * normal->z};
            // HEAD AT THE SURFACE: the arrow ends where the heat crosses the boundary.
            addArrow(out.segments, positionOf(*centroid), direction, *length);
            ++out.glyphs;
        }
    }
    return out;
}

Result<ThermalOverlay> prescribedTemperatureOverlay(const meshing::Mesh& mesh,
                                                    std::span<const PrescribedTemperatureGlyph> glyphs,
                                                    double symbolLength) {
    if (!isUsableThermalSymbolLength(symbolLength)) {
        return failWith(ErrorCode::InvalidArgument,
                        std::format("the symbol length {} is not finite and positive", symbolLength));
    }
    ThermalOverlay out;
    out.role = ThermalOverlayRole::PrescribedTemperatureSymbols;
    // ONE SYMBOL PER DISTINCT (FACET, TEMPERATURE). An identical prescription of one facet by two conditions is one
    // mathematical constraint (P18-BC collapses it), so it is drawn once and counted as grouped; the canonical
    // conditions are untouched and each is still in the glyph list.
    std::set<std::pair<meshing::ElementId::ValueType, double>> drawn;
    for (const PrescribedTemperatureGlyph& glyph : glyphs) {
        if (glyph.cardinality.status != BoundaryClassStatus::Complete) {
            ++out.incompleteTargets;
        }
        for (const meshing::ElementId facet : glyph.facets) {
            if (!drawn.emplace(facet.value(), glyph.value.si()).second) {
                ++out.duplicatesGrouped;
                continue;
            }
            const Result<Vector3D> normal = outwardNormalOf(mesh, facet);
            if (!normal.has_value()) {
                return std::unexpected(normal.error());
            }
            const Result<Point3D> centroid = facetCentroidOf(mesh, facet);
            if (!centroid.has_value()) {
                return std::unexpected(centroid.error());
            }
            const V3 n{normal->x, normal->y, normal->z};
            const V3 base = positionOf(*centroid);
            const V3 top = plus(base, times(n, symbolLength));
            addSegment(out.segments, base, top);
            const auto [u, v] = perpendicularFrame(n);
            for (const V3& bar : {u, v}) {
                addSegment(out.segments, minus(top, times(bar, symbolLength * 0.5)),
                           plus(top, times(bar, symbolLength * 0.5)));
            }
            ++out.glyphs;
        }
    }
    return out;
}

Result<ThermalOverlay> convectionOverlay(const meshing::Mesh& mesh, std::span<const ConvectionGlyph> glyphs,
                                         double symbolLength) {
    if (!isUsableThermalSymbolLength(symbolLength)) {
        return failWith(ErrorCode::InvalidArgument,
                        std::format("the symbol length {} is not finite and positive", symbolLength));
    }
    ThermalOverlay out;
    out.role = ThermalOverlayRole::ConvectionSymbols;
    for (const ConvectionGlyph& glyph : glyphs) {
        if (glyph.cardinality.status != BoundaryClassStatus::Complete) {
            ++out.incompleteTargets;
        }
        // ADDITIVE: every condition is drawn, even over a facet another one already covers.
        for (const meshing::ElementId facet : glyph.facets) {
            const Result<Vector3D> normal = outwardNormalOf(mesh, facet);
            if (!normal.has_value()) {
                return std::unexpected(normal.error());
            }
            const Result<Point3D> centroid = facetCentroidOf(mesh, facet);
            if (!centroid.has_value()) {
                return std::unexpected(centroid.error());
            }
            const V3 n{normal->x, normal->y, normal->z};
            const V3 base = positionOf(*centroid);
            const V3 u = perpendicularFrame(n).first;
            const std::array<V3, 5> path{
                base,
                plus(plus(base, times(n, symbolLength * 0.25)), times(u, symbolLength * 0.125)),
                plus(plus(base, times(n, symbolLength * 0.5)), times(u, -symbolLength * 0.125)),
                plus(plus(base, times(n, symbolLength * 0.75)), times(u, symbolLength * 0.125)),
                plus(plus(base, times(n, symbolLength)), times(u, -symbolLength * 0.125)),
            };
            for (std::size_t i = 0U; i + 1U < path.size(); ++i) {
                addSegment(out.segments, path[i], path[i + 1U]);
            }
            ++out.glyphs;
        }
    }
    return out;
}

} // namespace bettercad::renderer
