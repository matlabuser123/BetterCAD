#include <bettercad/meshing/MeshQuality.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <initializer_list>
#include <map>
#include <numeric>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace bettercad::meshing {
namespace {

/// A vector of SI values. The same local helper pattern SurfaceMesh.cpp uses:
/// the arithmetic is done on plain doubles and wrapped once at the boundary,
/// which is the module's established way of keeping units honest without
/// paying for a dimensioned vector type.
struct Vec {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] Vec of(const Point3D& p) noexcept {
    return Vec{p.x.si(), p.y.si(), p.z.si()};
}

[[nodiscard]] Vec sub(const Vec& a, const Vec& b) noexcept {
    return Vec{a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] double dot(const Vec& a, const Vec& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec cross(const Vec& a, const Vec& b) noexcept {
    return Vec{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] double norm(const Vec& a) noexcept {
    return std::sqrt(dot(a, a));
}

/// acos with its domain protected.
///
/// A dot product of two unit vectors can round to 1 + 1e-16, and acos of that
/// is NaN. Clamping is floating-point hygiene on a value that is already
/// mathematically in range -- NOT mesh repair, and NOT a tolerance on
/// geometry: the vectors are normalised first and checked for finiteness, so
/// the only thing being absorbed is the last bit of the representation.
[[nodiscard]] double safeAcos(double cosine) noexcept {
    return std::acos(std::clamp(cosine, -1.0, 1.0));
}

/// The four faces of a tetrahedron, wound to face OUT of it.
///
/// The same table as src/meshing/VolumeMesh.cpp's boundary extraction, and for
/// the same reason: with a positive signed volume on (0,1,2,3) these windings
/// give outward normals. Written out rather than generated, because an index
/// permutation from a loop is the kind of thing that is wrong in one of four
/// cases and still passes a test that only counts faces.
constexpr std::array<std::array<std::size_t, 3>, 4> kTetFaces{{
    {0, 2, 1},
    {0, 1, 3},
    {1, 2, 3},
    {0, 3, 2},
}};

/// The six edges of a tetrahedron, as vertex index pairs.
constexpr std::array<std::array<std::size_t, 2>, 6> kTetEdges{{
    {0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3},
}};

[[nodiscard]] bool allFinite(std::initializer_list<double> values) noexcept {
    return std::ranges::all_of(values, [](double v) { return std::isfinite(v); });
}

struct Stats {
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
};

[[nodiscard]] Stats statsOf(std::span<const double> values) {
    Stats stats;
    if (values.empty()) {
        return stats;
    }
    stats.minimum = *std::ranges::min_element(values);
    stats.maximum = *std::ranges::max_element(values);
    // Deterministic iteration order, plain accumulation. Compensated
    // summation is not used because nothing has shown it is needed; adding it
    // on suspicion would be optimising without evidence.
    stats.mean = std::accumulate(values.begin(), values.end(), 0.0) /
                 static_cast<double>(values.size());
    return stats;
}

} // namespace

std::string_view toString(QualityMetric metric) noexcept {
    switch (metric) {
    case QualityMetric::TetVolume:
        return "tet_volume";
    case QualityMetric::TetJacobianDeterminant:
        return "tet_jacobian_determinant";
    case QualityMetric::TetMinEdgeLength:
        return "tet_min_edge_length";
    case QualityMetric::TetMaxEdgeLength:
        return "tet_max_edge_length";
    case QualityMetric::TetMeanEdgeLength:
        return "tet_mean_edge_length";
    case QualityMetric::TetAspectRatio:
        return "tet_aspect_ratio";
    case QualityMetric::TetRadiusRatio:
        return "tet_radius_ratio";
    case QualityMetric::TetInradius:
        return "tet_inradius";
    case QualityMetric::TetCircumradius:
        return "tet_circumradius";
    case QualityMetric::TetMinDihedralAngle:
        return "tet_min_dihedral_angle";
    case QualityMetric::TetMaxDihedralAngle:
        return "tet_max_dihedral_angle";
    case QualityMetric::TriangleArea:
        return "triangle_area";
    case QualityMetric::TriangleMinEdgeLength:
        return "triangle_min_edge_length";
    case QualityMetric::TriangleMaxEdgeLength:
        return "triangle_max_edge_length";
    case QualityMetric::TriangleMeanEdgeLength:
        return "triangle_mean_edge_length";
    case QualityMetric::TriangleShapeQuality:
        return "triangle_shape_quality";
    case QualityMetric::TriangleMinAngle:
        return "triangle_min_angle";
    case QualityMetric::TriangleMaxAngle:
        return "triangle_max_angle";
    }
    return "unknown_quality_metric";
}

QualityDirection direction(QualityMetric metric) noexcept {
    switch (metric) {
    case QualityMetric::TetRadiusRatio:
    case QualityMetric::TetMinDihedralAngle:
    case QualityMetric::TriangleShapeQuality:
    case QualityMetric::TriangleMinAngle:
        return QualityDirection::HigherIsBetter;
    case QualityMetric::TetAspectRatio:
    case QualityMetric::TetMaxDihedralAngle:
    case QualityMetric::TriangleMaxAngle:
        return QualityDirection::LowerIsBetter;
    // Dimensioned sizes. Summarised for context, never classified: "is this
    // volume good?" has no scale-free answer.
    case QualityMetric::TetVolume:
    case QualityMetric::TetJacobianDeterminant:
    case QualityMetric::TetMinEdgeLength:
    case QualityMetric::TetMaxEdgeLength:
    case QualityMetric::TetMeanEdgeLength:
    case QualityMetric::TetInradius:
    case QualityMetric::TetCircumradius:
    case QualityMetric::TriangleArea:
    case QualityMetric::TriangleMinEdgeLength:
    case QualityMetric::TriangleMaxEdgeLength:
    case QualityMetric::TriangleMeanEdgeLength:
        return QualityDirection::ContextOnly;
    }
    return QualityDirection::ContextOnly;
}

std::string_view unitOf(QualityMetric metric) noexcept {
    switch (metric) {
    case QualityMetric::TetVolume:
    case QualityMetric::TetJacobianDeterminant:
        return "m^3";
    case QualityMetric::TetMinEdgeLength:
    case QualityMetric::TetMaxEdgeLength:
    case QualityMetric::TetMeanEdgeLength:
    case QualityMetric::TetInradius:
    case QualityMetric::TetCircumradius:
    case QualityMetric::TriangleMinEdgeLength:
    case QualityMetric::TriangleMaxEdgeLength:
    case QualityMetric::TriangleMeanEdgeLength:
        return "m";
    case QualityMetric::TriangleArea:
        return "m^2";
    case QualityMetric::TetMinDihedralAngle:
    case QualityMetric::TetMaxDihedralAngle:
    case QualityMetric::TriangleMinAngle:
    case QualityMetric::TriangleMaxAngle:
        return "rad";
    case QualityMetric::TetAspectRatio:
    case QualityMetric::TetRadiusRatio:
    case QualityMetric::TriangleShapeQuality:
        return "1";
    }
    return "?";
}

std::string_view toString(QualityClass classification) noexcept {
    switch (classification) {
    case QualityClass::Valid:
        return "valid";
    case QualityClass::Warning:
        return "warning";
    case QualityClass::Failure:
        return "failure";
    case QualityClass::Invalid:
        return "invalid";
    }
    return "unknown_quality_class";
}

Result<TetQuality> evaluateTetQuality(const Mesh& mesh, const Tetrahedron& tet) {
    std::array<Vec, 4> p{};
    for (std::size_t corner = 0; corner < 4; ++corner) {
        const Node* node = mesh.findNode(tet.nodes[corner]);
        if (node == nullptr) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("quality: {} names {}, which is no node of this mesh",
                                         tet.id, tet.nodes[corner]));
        }
        p[corner] = of(node->position);
    }
    // A repeated handle is a structural defect P16-DATA-001 names; quality is
    // not computed for it.
    for (std::size_t a = 0; a < 4; ++a) {
        for (std::size_t b = a + 1; b < 4; ++b) {
            if (tet.nodes[a] == tet.nodes[b]) {
                return makeError(
                    ErrorCode::FailedPrecondition,
                    std::format("quality: {} repeats a node handle, {}", tet.id, tet.nodes[a]));
            }
        }
    }

    TetQuality quality;
    quality.element = tet.id;

    // SIGNED VOLUME FROM P16-DATA-001, not recomputed here. Reusing it is what
    // keeps one convention in the repository: a second determinant with its
    // own sign habit is how an inverted element becomes acceptable somewhere.
    const Node* n0 = mesh.findNode(tet.nodes[0]);
    const Node* n1 = mesh.findNode(tet.nodes[1]);
    const Node* n2 = mesh.findNode(tet.nodes[2]);
    const Node* n3 = mesh.findNode(tet.nodes[3]);
    quality.volume = signedVolume(n0->position, n1->position, n2->position, n3->position);
    const double volume = quality.volume.si();

    // POSITIVE AND FINITE, OR STRUCTURALLY INVALID. No absolute value: a
    // negative volume is an inverted element and is refused here rather than
    // measured.
    //
    // The finiteness test covers non-finite COORDINATES as well, and is the
    // only check made for them: a NaN coordinate makes the determinant NaN,
    // and MeshBuilder refuses a non-finite coordinate at the point of entry
    // anyway, so a separate per-coordinate loop here would be a branch nothing
    // can reach. What it does catch is a finite coordinate whose determinant
    // OVERFLOWS -- a body at 1e90 m -- which is reachable and is tested.
    if (!std::isfinite(volume)) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("quality: {} has a non-finite signed volume", tet.id));
    }
    if (volume == 0.0) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("quality: {} is degenerate (zero signed volume)",
                                     tet.id));
    }
    if (volume < 0.0) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("quality: {} is inverted (signed volume {} m^3)",
                                     tet.id, volume));
    }

    // --- edges -------------------------------------------------------------
    std::array<double, 6> edges{};
    for (std::size_t e = 0; e < kTetEdges.size(); ++e) {
        edges[e] = norm(sub(p[kTetEdges[e][1]], p[kTetEdges[e][0]]));
    }
    const Stats edgeStats = statsOf(edges);
    quality.minEdge = Length::fromSi(edgeStats.minimum);
    quality.maxEdge = Length::fromSi(edgeStats.maximum);
    quality.meanEdge = Length::fromSi(edgeStats.mean);

    // An edge of zero length cannot happen with a positive volume, but the
    // division below must not be the thing that discovers it.
    if (!(edgeStats.minimum > 0.0)) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("quality: {} has a zero-length edge", tet.id));
    }
    quality.aspectRatio = edgeStats.maximum / edgeStats.minimum;

    // --- faces, areas, outward normals -------------------------------------
    std::array<Vec, 4> normals{};
    double totalArea = 0.0;
    for (std::size_t f = 0; f < kTetFaces.size(); ++f) {
        const Vec u = sub(p[kTetFaces[f][1]], p[kTetFaces[f][0]]);
        const Vec v = sub(p[kTetFaces[f][2]], p[kTetFaces[f][0]]);
        const Vec c = cross(u, v);
        const double magnitude = norm(c);
        if (!(magnitude > 0.0) || !std::isfinite(magnitude)) {
            // Two separate reasons, one refusal. A ZERO-area face cannot occur
            // with a positive signed volume, so that half guards the
            // normalisation below rather than the geometry -- never normalise a
            // zero vector. A NON-FINITE one is reachable: the cross product of
            // two edges of a body 1e90 m across is 1e180, and squaring it for
            // the magnitude overflows. Tested.
            return makeError(
                ErrorCode::FailedPrecondition,
                std::format("quality: {} has a face whose area is zero or not finite", tet.id));
        }
        totalArea += 0.5 * magnitude;
        normals[f] = Vec{c.x / magnitude, c.y / magnitude, c.z / magnitude};
    }

    // --- inradius, r = 3V / A ----------------------------------------------
    quality.inradius = Length::fromSi(3.0 * volume / totalArea);

    // --- circumradius -------------------------------------------------------
    //
    // The circumcentre c, relative to p0, satisfies 2 a_i . c = |a_i|^2 for
    // the three edge vectors a_i = p_i - p0. Solving that 3x3 system gives
    //
    //     c = ( |a1|^2 (a2 x a3) + |a2|^2 (a3 x a1) + |a3|^2 (a1 x a2) )
    //         / (2 det[a1 a2 a3])
    //
    // and det[a1 a2 a3] = 6V, so the denominator is 12V -- which is exactly
    // why this is ill-conditioned for a near-degenerate element, and why the
    // result is checked for finiteness instead of trusted.
    const Vec a1 = sub(p[1], p[0]);
    const Vec a2 = sub(p[2], p[0]);
    const Vec a3 = sub(p[3], p[0]);
    const Vec t1 = cross(a2, a3);
    const Vec t2 = cross(a3, a1);
    const Vec t3 = cross(a1, a2);
    const double n1sq = dot(a1, a1);
    const double n2sq = dot(a2, a2);
    const double n3sq = dot(a3, a3);
    const double denominator = 12.0 * volume;
    const Vec centre{(n1sq * t1.x + n2sq * t2.x + n3sq * t3.x) / denominator,
                     (n1sq * t1.y + n2sq * t2.y + n3sq * t3.y) / denominator,
                     (n1sq * t1.z + n2sq * t2.z + n3sq * t3.z) / denominator};
    const double circumradius = norm(centre);
    quality.circumradius = Length::fromSi(circumradius);

    // --- radius ratio, 3r / R ----------------------------------------------
    quality.radiusRatio = 3.0 * quality.inradius.si() / circumradius;

    // --- internal dihedral angles -------------------------------------------
    //
    // A tetrahedron has four faces and six edges, and EVERY PAIR OF FACES
    // SHARES EXACTLY ONE EDGE -- four choose two is six. So the six pairs of
    // outward normals give the six edge dihedrals, with no edge-to-face table
    // to get wrong.
    //
    // THE CONVENTION, stated because confusing it is the classic error: the
    // INTERNAL dihedral is measured inside the material, and for outward unit
    // normals n_i, n_j it is
    //
    //     theta = acos( -(n_i . n_j) )
    //
    // A regular tetrahedron gives acos(1/3) = 70.5288 deg. The angle BETWEEN
    // THE OUTWARD NORMALS is the supplement, acos(-1/3) = 109.4712 deg, and
    // reporting that as the dihedral would make every regular element look
    // like a bad one -- or worse, make a bad one look acceptable.
    std::array<double, 6> dihedrals{};
    std::size_t pair = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = i + 1; j < 4; ++j) {
            dihedrals[pair++] = safeAcos(-dot(normals[i], normals[j]));
        }
    }
    const Stats dihedralStats = statsOf(dihedrals);
    quality.minDihedral = Angle::fromSi(dihedralStats.minimum);
    quality.maxDihedral = Angle::fromSi(dihedralStats.maximum);

    // Every metric finite, or the element is Invalid rather than "bad".
    quality.defined = allFinite({quality.volume.si(), quality.minEdge.si(), quality.maxEdge.si(),
                                 quality.meanEdge.si(), quality.aspectRatio, quality.radiusRatio,
                                 quality.inradius.si(), quality.circumradius.si(),
                                 quality.minDihedral.si(), quality.maxDihedral.si()}) &&
                       circumradius > 0.0;
    return quality;
}

Result<TriangleQuality> evaluateTriangleQuality(const Mesh& mesh, const Triangle& triangle) {
    std::array<Vec, 3> p{};
    for (std::size_t corner = 0; corner < 3; ++corner) {
        const Node* node = mesh.findNode(triangle.nodes[corner]);
        if (node == nullptr) {
            return makeError(ErrorCode::InvalidArgument,
                             std::format("quality: {} names {}, which is no node of this mesh",
                                         triangle.id, triangle.nodes[corner]));
        }
        p[corner] = of(node->position);
    }
    for (std::size_t a = 0; a < 3; ++a) {
        for (std::size_t b = a + 1; b < 3; ++b) {
            if (triangle.nodes[a] == triangle.nodes[b]) {
                return makeError(ErrorCode::FailedPrecondition,
                                 std::format("quality: {} repeats a node handle, {}", triangle.id,
                                             triangle.nodes[a]));
            }
        }
    }

    TriangleQuality quality;
    quality.element = triangle.id;

    // AREA FROM P16-DATA-001, reused.
    const Node* n0 = mesh.findNode(triangle.nodes[0]);
    const Node* n1 = mesh.findNode(triangle.nodes[1]);
    const Node* n2 = mesh.findNode(triangle.nodes[2]);
    quality.area = triangleArea(n0->position, n1->position, n2->position);
    const double area = quality.area.si();
    if (!std::isfinite(area)) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("quality: {} has a non-finite area", triangle.id));
    }
    // A DEGENERATE TRIANGLE IS INVALID, not quality zero. P16-SURF-001's
    // contract, honoured rather than reinterpreted.
    if (area == 0.0) {
        return makeError(
            ErrorCode::FailedPrecondition,
            std::format("quality: {} is degenerate (zero area), so its nodes are "
                        "collinear or coincident",
                        triangle.id));
    }

    std::array<double, 3> edges{norm(sub(p[1], p[0])), norm(sub(p[2], p[1])),
                                norm(sub(p[0], p[2]))};
    const Stats edgeStats = statsOf(edges);
    quality.minEdge = Length::fromSi(edgeStats.minimum);
    quality.maxEdge = Length::fromSi(edgeStats.maximum);
    quality.meanEdge = Length::fromSi(edgeStats.mean);
    if (!(edgeStats.minimum > 0.0)) {
        return makeError(ErrorCode::FailedPrecondition,
                         std::format("quality: {} has a zero-length edge", triangle.id));
    }

    // q = 4 sqrt(3) A / (l1^2 + l2^2 + l3^2).
    //
    // Derived, not copied: for an equilateral triangle of side a, A =
    // sqrt(3) a^2 / 4 and the edge-square sum is 3 a^2, so
    // q = 4 sqrt(3) (sqrt(3) a^2 / 4) / (3 a^2) = 1. Degeneracy sends A to
    // zero with the edges finite, so q goes to zero. Range (0, 1].
    const double squareSum = edges[0] * edges[0] + edges[1] * edges[1] + edges[2] * edges[2];
    quality.shapeQuality = 4.0 * std::sqrt(3.0) * area / squareSum;

    // Interior angles from the law of cosines at each vertex, using the two
    // edges that meet there.
    const auto angleAt = [&](std::size_t at, std::size_t b, std::size_t c) {
        const Vec u = sub(p[b], p[at]);
        const Vec v = sub(p[c], p[at]);
        const double lu = norm(u);
        const double lv = norm(v);
        return safeAcos(dot(u, v) / (lu * lv));
    };
    std::array<double, 3> angles{angleAt(0, 1, 2), angleAt(1, 2, 0), angleAt(2, 0, 1)};
    const Stats angleStats = statsOf(angles);
    quality.minAngle = Angle::fromSi(angleStats.minimum);
    quality.maxAngle = Angle::fromSi(angleStats.maximum);

    quality.defined = allFinite({area, quality.minEdge.si(), quality.maxEdge.si(),
                                 quality.meanEdge.si(), quality.shapeQuality,
                                 quality.minAngle.si(), quality.maxAngle.si()});
    return quality;
}

Result<void> validate(const QualityThresholds& thresholds) {
    for (const auto& [metric, limit] : thresholds.limits) {
        const QualityDirection way = direction(metric);
        if (way == QualityDirection::ContextOnly) {
            return makeError(
                ErrorCode::InvalidArgument,
                std::format("quality policy: {} is a dimensioned size, not a quality score, so a "
                            "threshold on it would be a threshold on the model's units",
                            toString(metric)));
        }
        for (const std::optional<double>& bound : {limit.warning, limit.failure}) {
            if (bound.has_value() && !std::isfinite(*bound)) {
                return makeError(ErrorCode::InvalidArgument,
                                 std::format("quality policy: {} has a non-finite threshold ({})",
                                             toString(metric), *bound));
            }
        }
        if (!limit.warning.has_value() || !limit.failure.has_value()) {
            continue;
        }
        // A failure must be on the worse side of a warning. Otherwise a value
        // could be classified Failure while being better than the warning
        // bound, which is not a strict policy but a mistake.
        const bool ordered = way == QualityDirection::HigherIsBetter
                                 ? *limit.failure <= *limit.warning
                                 : *limit.failure >= *limit.warning;
        if (!ordered) {
            return makeError(
                ErrorCode::InvalidArgument,
                std::format("quality policy: {} is {}, so its failure threshold ({}) must be on "
                            "the worse side of its warning threshold ({})",
                            toString(metric),
                            way == QualityDirection::HigherIsBetter ? "higher-is-better"
                                                                    : "lower-is-better",
                            *limit.failure, *limit.warning));
        }
    }
    return {};
}

QualityThresholds reportOnlyThresholds() {
    return QualityThresholds{};
}

namespace {

/// Classifies one measured value, and records a finding when it is not Valid.
[[nodiscard]] QualityClass classify(QualityMetric metric, ElementId element, ElementType type,
                                    double value, const QualityThresholds& thresholds,
                                    std::vector<QualityFinding>& findings) {
    // A NON-FINITE VALUE IS INVALID AND IS NEVER COMPARED. A NaN makes every
    // comparison false, so a threshold check would quietly call it Valid --
    // which is the defect this branch exists to prevent.
    //
    // It is reachable, and that took a change to make true. Every measured
    // quantity of an element now passes through here, including the two radii,
    // so the ONE number that goes non-finite for a near-degenerate element --
    // the circumradius -- produces a finding that names it. While the radii had
    // no metric name, this branch could not fire and a mutation removing it
    // survived the suite.
    if (!std::isfinite(value)) {
        findings.push_back(QualityFinding{metric, element, type, value, std::nullopt,
                                          QualityClass::Invalid,
                                          std::format("{}: {} is not finite ({})", element,
                                                      toString(metric), value)});
        return QualityClass::Invalid;
    }

    const auto found = thresholds.limits.find(metric);
    if (found == thresholds.limits.end()) {
        return QualityClass::Valid;
    }
    const QualityDirection way = direction(metric);
    const QualityThreshold& limit = found->second;

    // Failure before warning: the worse verdict wins, and the comparisons are
    // STRICT so a value exactly on a threshold is on the good side of it.
    const auto worseThan = [way](double v, double bound) {
        return way == QualityDirection::HigherIsBetter ? v < bound : v > bound;
    };
    if (limit.failure.has_value() && worseThan(value, *limit.failure)) {
        findings.push_back(QualityFinding{
            metric, element, type, value, limit.failure, QualityClass::Failure,
            std::format("{}: {} = {} {}, failure threshold {}", element, toString(metric),
                        value, unitOf(metric), *limit.failure)});
        return QualityClass::Failure;
    }
    if (limit.warning.has_value() && worseThan(value, *limit.warning)) {
        findings.push_back(QualityFinding{
            metric, element, type, value, limit.warning, QualityClass::Warning,
            std::format("{}: {} = {} {}, warning threshold {}", element, toString(metric),
                        value, unitOf(metric), *limit.warning)});
        return QualityClass::Warning;
    }
    return QualityClass::Valid;
}

[[nodiscard]] QualityClass worse(QualityClass a, QualityClass b) noexcept {
    // Invalid > Failure > Warning > Valid, which is the enumeration order.
    return static_cast<std::uint8_t>(a) >= static_cast<std::uint8_t>(b) ? a : b;
}

void summarise(MeshQualityReport& report, QualityMetric metric,
               const std::vector<std::pair<ElementId, double>>& samples) {
    if (samples.empty()) {
        return;
    }
    MetricSummary summary;
    summary.count = samples.size();
    std::vector<double> values;
    values.reserve(samples.size());
    for (const auto& [element, value] : samples) {
        values.push_back(value);
    }
    const Stats stats = statsOf(values);
    summary.minimum = stats.minimum;
    summary.maximum = stats.maximum;
    summary.mean = stats.mean;

    // THE WORST ELEMENT FOR THIS METRIC, BY THIS METRIC'S OWN DIRECTION.
    // A ContextOnly metric has no worst, and leaving the handle invalid says
    // so rather than naming an arbitrary element.
    const QualityDirection way = direction(metric);
    if (way != QualityDirection::ContextOnly) {
        const auto chosen = std::ranges::min_element(
            samples, [way](const std::pair<ElementId, double>& a,
                           const std::pair<ElementId, double>& b) {
                if (a.second == b.second) {
                    // A tie goes to the lower ElementId, so the answer does
                    // not depend on iteration order.
                    return a.first < b.first;
                }
                return way == QualityDirection::HigherIsBetter ? a.second < b.second
                                                               : a.second > b.second;
            });
        summary.worst = chosen->first;
    }
    report.summaries.emplace(metric, summary);
}

} // namespace

MeshQualityReport evaluateMeshQuality(const Mesh& mesh, const QualityThresholds& thresholds) {
    MeshQualityReport report;
    report.thresholds = thresholds;

    // THE POLICY IS CHECKED BEFORE IT IS USED. A bound on a dimensioned size
    // names a metric nothing classifies, so an unchecked policy would be partly
    // honoured and partly ignored without saying so -- a control that quietly
    // does nothing, which is the defect P16-SIZE-001's audit found four times.
    // On failure the diagnostic is recorded, NOTHING is classified, and
    // satisfiesPolicy() is false: a verdict that was never reached is not a
    // pass.
    const QualityThresholds reportOnly{};
    if (const Result<void> checked = validate(thresholds); !checked.has_value()) {
        report.thresholdPolicyError = checked.error();
    }
    const QualityThresholds& active =
        report.thresholdPolicyError.has_value() ? reportOnly : thresholds;

    // STRUCTURE FIRST, and P16-DATA-001's validator is the one definition of
    // it. An empty mesh is already structurally invalid (EmptyMesh), so it
    // cannot come back as a pass for want of bad elements.
    report.structural = validate(mesh);
    report.structurallyValid = report.structural.dataValid();

    report.tetCount = mesh.tetrahedra().size();
    report.triangleCount = mesh.triangles().size();

    std::map<QualityMetric, std::vector<std::pair<ElementId, double>>> samples;
    const auto note = [&samples](QualityMetric metric, ElementId element, double value) {
        samples[metric].emplace_back(element, value);
    };

    // Ascending ElementId, because Mesh stores and enumerates elements that
    // way -- so the report's order is the mesh's order and neither depends on
    // a hash.
    for (const Tetrahedron& tet : mesh.tetrahedra()) {
        const Result<TetQuality> quality = evaluateTetQuality(mesh, tet);
        if (!quality.has_value()) {
            // STRUCTURALLY REFUSED: no metric was computed, so the finding
            // names none and carries no value. See QualityFinding::metric --
            // naming TetVolume with a value of 0 for an inverted element whose
            // volume is negative would be fabricated structured data.
            ++report.invalidElements;
            report.findings.push_back(QualityFinding{std::nullopt, tet.id,
                                                     ElementType::Tetrahedron4, std::nullopt,
                                                     std::nullopt, QualityClass::Invalid,
                                                     quality.error().message});
            continue;
        }
        report.tets.push_back(*quality);

        // EVERY MEASURED QUANTITY, in metric order, so that the finiteness
        // check reaches all of them and a finding can name the one that
        // failed. A ContextOnly metric is checked for finiteness and never
        // compared against a bound.
        const std::array<std::pair<QualityMetric, double>, 11> measured{{
            {QualityMetric::TetVolume, quality->volume.si()},
            {QualityMetric::TetJacobianDeterminant, quality->jacobianDeterminant().si()},
            {QualityMetric::TetMinEdgeLength, quality->minEdge.si()},
            {QualityMetric::TetMaxEdgeLength, quality->maxEdge.si()},
            {QualityMetric::TetMeanEdgeLength, quality->meanEdge.si()},
            {QualityMetric::TetAspectRatio, quality->aspectRatio},
            {QualityMetric::TetRadiusRatio, quality->radiusRatio},
            {QualityMetric::TetInradius, quality->inradius.si()},
            {QualityMetric::TetCircumradius, quality->circumradius.si()},
            {QualityMetric::TetMinDihedralAngle, quality->minDihedral.si()},
            {QualityMetric::TetMaxDihedralAngle, quality->maxDihedral.si()},
        }};

        QualityClass classification = QualityClass::Valid;
        for (const auto& [metric, value] : measured) {
            classification =
                worse(classification, classify(metric, tet.id, ElementType::Tetrahedron4, value,
                                               active, report.findings));
        }
        // SAMPLES ONLY FROM A FULLY MEASURED ELEMENT. A half-measured one would
        // contribute an infinity that poisons every aggregate it touches, and
        // an aggregate over an element the report calls Invalid would be
        // describing something it has already refused.
        if (quality->defined) {
            for (const auto& [metric, value] : measured) {
                note(metric, tet.id, value);
            }
        }
        switch (classification) {
        case QualityClass::Valid:
            ++report.validElements;
            break;
        case QualityClass::Warning:
            ++report.warningElements;
            break;
        case QualityClass::Failure:
            ++report.failureElements;
            break;
        case QualityClass::Invalid:
            ++report.invalidElements;
            break;
        }
    }

    for (const Triangle& triangle : mesh.triangles()) {
        const Result<TriangleQuality> quality = evaluateTriangleQuality(mesh, triangle);
        if (!quality.has_value()) {
            ++report.invalidElements;
            report.findings.push_back(QualityFinding{std::nullopt, triangle.id,
                                                     ElementType::Triangle3, std::nullopt,
                                                     std::nullopt, QualityClass::Invalid,
                                                     quality.error().message});
            continue;
        }
        report.triangles.push_back(*quality);

        const std::array<std::pair<QualityMetric, double>, 7> measured{{
            {QualityMetric::TriangleArea, quality->area.si()},
            {QualityMetric::TriangleMinEdgeLength, quality->minEdge.si()},
            {QualityMetric::TriangleMaxEdgeLength, quality->maxEdge.si()},
            {QualityMetric::TriangleMeanEdgeLength, quality->meanEdge.si()},
            {QualityMetric::TriangleShapeQuality, quality->shapeQuality},
            {QualityMetric::TriangleMinAngle, quality->minAngle.si()},
            {QualityMetric::TriangleMaxAngle, quality->maxAngle.si()},
        }};

        QualityClass classification = QualityClass::Valid;
        for (const auto& [metric, value] : measured) {
            classification =
                worse(classification, classify(metric, triangle.id, ElementType::Triangle3, value,
                                               active, report.findings));
        }
        if (quality->defined) {
            for (const auto& [metric, value] : measured) {
                note(metric, triangle.id, value);
            }
        }
        switch (classification) {
        case QualityClass::Valid:
            ++report.validElements;
            break;
        case QualityClass::Warning:
            ++report.warningElements;
            break;
        case QualityClass::Failure:
            ++report.failureElements;
            break;
        case QualityClass::Invalid:
            ++report.invalidElements;
            break;
        }
    }

    // std::map, so this is enumeration order in every build configuration.
    for (const auto& [metric, values] : samples) {
        summarise(report, metric, values);
    }

    // Most severe first, then metric, then element. Stated and tested,
    // because a report whose order moves cannot be diffed between runs.
    //
    // An absent metric sorts before every present one, so an element-level
    // refusal leads the findings for its class rather than landing in the
    // middle of the per-metric ones.
    const auto metricKey = [](const QualityFinding& finding) {
        return finding.metric.has_value()
                   ? static_cast<int>(static_cast<std::uint8_t>(*finding.metric))
                   : -1;
    };
    std::ranges::stable_sort(report.findings, [&metricKey](const QualityFinding& a,
                                                           const QualityFinding& b) {
        return std::tuple{static_cast<std::uint8_t>(b.classification), metricKey(a),
                          a.element.value()} <
               std::tuple{static_cast<std::uint8_t>(a.classification), metricKey(b),
                          b.element.value()};
    });
    return report;
}

} // namespace bettercad::meshing
