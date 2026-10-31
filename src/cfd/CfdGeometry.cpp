// The finite-volume geometric metrics (P19-GEOM-001, ADR-065).
//
// THREE PASSES, each in ascending index order, nothing keyed by a hash or an
// address: the cells (volume and centroid), then the faces (area vector, centroid
// and the relational metrics the summary needs), then the cells again for the
// surface-vector sums, which need the faces' area vectors. Two builds of one mesh
// and topology are therefore equal field for field, in every preset.
//
// THE ARITHMETIC IS DONE ON SI VALUES AND WRAPPED ONCE, which is the boundary
// `meshing::signedVolume` and `distance(Point3D, Point3D)` already use. Wrapping
// each intermediate would cost nothing in correctness and a great deal in
// readability for a cross product.

#include <bettercad/cfd/CfdGeometry.hpp>

#include <bettercad/cfd/CfdAnalysis.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bettercad::cfd {
namespace {

using Vec = std::array<double, 3>;

[[nodiscard]] Vec si(const Point3D& p) noexcept { return {p.x.si(), p.y.si(), p.z.si()}; }

[[nodiscard]] Vec sub(const Vec& a, const Vec& b) noexcept { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

[[nodiscard]] Vec cross(const Vec& a, const Vec& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

} // namespace

// ---------------------------------------------------------------------------
// The point-level formulas
// ---------------------------------------------------------------------------

AreaVector3D triangleAreaVector(const Point3D& a, const Point3D& b, const Point3D& c) noexcept {
    const Vec pa = si(a);
    const Vec s = cross(sub(si(b), pa), sub(si(c), pa));
    return {Area::fromSi(0.5 * s[0]), Area::fromSi(0.5 * s[1]), Area::fromSi(0.5 * s[2])};
}

Point3D triangleCentroid(const Point3D& a, const Point3D& b, const Point3D& c) noexcept {
    return {(a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0, (a.z + b.z + c.z) / 3.0};
}

Point3D tetrahedronCentroid(const Point3D& a, const Point3D& b, const Point3D& c, const Point3D& d) noexcept {
    return {(a.x + b.x + c.x + d.x) / 4.0, (a.y + b.y + c.y + d.y) / 4.0, (a.z + b.z + c.z + d.z) / 4.0};
}

// ---------------------------------------------------------------------------
// Accessors. Every one is total for an index this geometry does not hold, and
// which total answer it gives follows ADR-065's rule: a provably impossible zero
// where there is one, an empty optional where zero is a legal measurement.
// ---------------------------------------------------------------------------

Volume CfdGeometry::cellVolume(CellIndex cell) const noexcept {
    if (!cell.isValid() || cell.value() >= volume_.size()) {
        return Volume{};
    }
    return volume_[cell.value()];
}

std::optional<Point3D> CfdGeometry::cellCentroid(CellIndex cell) const noexcept {
    if (!cell.isValid() || cell.value() >= cellCentroid_.size()) {
        return std::nullopt;
    }
    return cellCentroid_[cell.value()];
}

std::optional<AreaVector3D> CfdGeometry::cellSurfaceVectorSum(CellIndex cell) const noexcept {
    if (!cell.isValid() || cell.value() >= cellSurfaceSum_.size()) {
        return std::nullopt;
    }
    return cellSurfaceSum_[cell.value()];
}

std::optional<double> CfdGeometry::cellClosure(CellIndex cell) const noexcept {
    if (!cell.isValid() || cell.value() >= cellClosure_.size()) {
        return std::nullopt;
    }
    return cellClosure_[cell.value()];
}

AreaVector3D CfdGeometry::faceAreaVector(FaceIndex face) const noexcept {
    if (!face.isValid() || face.value() >= areaVector_.size()) {
        return AreaVector3D{};
    }
    return areaVector_[face.value()];
}

Area CfdGeometry::faceArea(FaceIndex face) const noexcept {
    if (!face.isValid() || face.value() >= areaVector_.size()) {
        return Area{};
    }
    return magnitude(areaVector_[face.value()]);
}

std::optional<Direction3D> CfdGeometry::faceUnitNormal(FaceIndex face) const noexcept {
    if (!face.isValid() || face.value() >= areaVector_.size()) {
        return std::nullopt;
    }
    const AreaVector3D& s = areaVector_[face.value()];
    return Direction3D::fromComponents(s.x.si(), s.y.si(), s.z.si());
}

std::optional<Point3D> CfdGeometry::faceCentroid(FaceIndex face) const noexcept {
    if (!face.isValid() || face.value() >= faceCentroid_.size()) {
        return std::nullopt;
    }
    return faceCentroid_[face.value()];
}

std::optional<Translation3D> CfdGeometry::ownerToNeighbour(FaceIndex face) const noexcept {
    if (!face.isValid() || face.value() >= neighbour_.size()) {
        return std::nullopt;
    }
    const CellIndex neighbour = neighbour_[face.value()];
    if (!neighbour.isValid()) {
        return std::nullopt;
    }
    return cellCentroid_[neighbour.value()] - cellCentroid_[owner_[face.value()].value()];
}

Translation3D CfdGeometry::cellToFace(FaceIndex face) const noexcept {
    if (!face.isValid() || face.value() >= faceCentroid_.size()) {
        return Translation3D{};
    }
    return faceCentroid_[face.value()] - cellCentroid_[owner_[face.value()].value()];
}

Length CfdGeometry::orthogonalDistance(FaceIndex face) const noexcept {
    const std::optional<Direction3D> normal = faceUnitNormal(face);
    if (!normal) {
        return Length{};
    }
    // d is the owner-to-neighbour vector where there is one and the cell-to-face
    // vector at the boundary. Both are positive on the normal (ADR-065, Decision
    // 5), so they are the same quantity measured where each is available, not two
    // different conventions.
    const std::optional<Translation3D> d = ownerToNeighbour(face);
    return dot(d.value_or(cellToFace(face)), *normal);
}

std::optional<double> CfdGeometry::orthogonalityCosine(FaceIndex face) const noexcept {
    const std::optional<Direction3D> normal = faceUnitNormal(face);
    if (!normal) {
        return std::nullopt;
    }
    const Translation3D d = ownerToNeighbour(face).value_or(cellToFace(face));
    const Length span = magnitude(d);
    if (span.si() <= 0.0) {
        return std::nullopt;
    }
    // THE CLAMP IS NOT PADDING, AND IT LIVES HERE SO THERE IS ONE OF IT. On the
    // most regular face possible d is EXACTLY parallel to n, the quotient rounds
    // to 1.000000000000000222, and acos of that is NaN -- so the best input is the
    // one that breaks it, which
    // CfdGeometry_ThePerfectlyOrthogonalFaceIsWhereTheCosineExceedsOne measures.
    //
    // ADR-065's first draft recommended that callers form this quotient
    // themselves for its conditioning, without saying it can leave [-1, 1]. A
    // review found that the recommendation therefore handed back a number no
    // inverse trigonometric function accepts. The division is now an accessor and
    // the clamp is part of it.
    return std::clamp(dot(d, *normal).si() / span.si(), -1.0, 1.0);
}

std::optional<Angle> CfdGeometry::nonOrthogonality(FaceIndex face) const noexcept {
    // DEFINED ON THE COSINE, not beside it: two clamps would be two things to
    // keep true, and the angle is only the reported form of this number.
    const std::optional<double> cosine = orthogonalityCosine(face);
    if (!cosine) {
        return std::nullopt;
    }
    return arccos(*cosine);
}

std::optional<double> CfdGeometry::skewness(FaceIndex face) const noexcept {
    const std::optional<Translation3D> d = ownerToNeighbour(face);
    if (!d) {
        // Boundary, or a face this geometry does not hold. Skewness measures the
        // error in interpolating BETWEEN TWO cell values, and a boundary face has
        // one (ADR-065, Decision 6).
        return std::nullopt;
    }
    const AreaVector3D& s = areaVector_[face.value()];
    const Volume along = dot(*d, s);
    if (along.si() == 0.0) {
        return std::nullopt;
    }
    const Point3D owner = cellCentroid_[owner_[face.value()].value()];
    const Point3D centre = faceCentroid_[face.value()];
    // Where the line joining the two cell centroids crosses the plane of the face.
    // Sf is used rather than the unit normal because the ratio is scale-free, which
    // is also how P19-MESH-001 wrote it.
    const double lambda = dot(centre - owner, s).si() / along.si();
    const Point3D crossing = owner + *d * lambda;
    const Length span = magnitude(*d);
    if (span.si() <= 0.0) {
        return std::nullopt;
    }
    return distance(centre, crossing) / span;
}

std::optional<Direction3D> CfdGeometry::boundaryOutwardNormal(BoundaryFaceIndex boundaryFace) const noexcept {
    if (!boundaryFace.isValid() || boundaryFace.value() >= boundaryFace_.size()) {
        return std::nullopt;
    }
    return faceUnitNormal(boundaryFace_[boundaryFace.value()]);
}

bool CfdGeometry::describes(const CfdTopology& topology) const noexcept {
    return mesh_.isValid() && topology.mesh() == mesh_ && topology.cells() == cells() && topology.faces() == faces()
           && topology.internalFaces() == internalFaces_;
}

// ---------------------------------------------------------------------------
// The assembler
// ---------------------------------------------------------------------------

/// Computes the metrics from a mesh the topology describes.
///
/// A NAMED FRIEND, not a free function in the anonymous namespace: CfdGeometry's
/// constructor is private, because possession of one is proof that the metrics
/// were computed from the right mesh generation and found finite and positive, so
/// only a friend may assemble one.
struct CfdGeometryAssembler {
    [[nodiscard]] static Result<CfdGeometry> assemble(const meshing::Mesh& mesh, const CfdTopology& topology,
                                                      std::string_view subject) {
        CfdGeometry g;
        g.mesh_ = topology.mesh();
        g.geometry_ = topology.geometry();
        g.internalFaces_ = topology.internalFaces();

        // --- pass 1: the cells ------------------------------------------------
        g.volume_.reserve(topology.cells());
        g.cellCentroid_.reserve(topology.cells());
        for (std::size_t c = 0; c < topology.cells(); ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            const meshing::Tetrahedron* tet = mesh.findTetrahedron(topology.cellOf(cell));
            if (tet == nullptr) {
                // Unreachable for a mesh the topology describes: the stamp is the
                // same, and mesh storage is append-only. Checked rather than
                // dereferenced on faith, as the topology's own walk does.
                return std::unexpected(
                    Error{ErrorCode::FailedPrecondition,
                          std::format("{}: cell {} names element {}, which the mesh does not hold", subject, c,
                                      topology.cellOf(cell).value())});
            }
            std::array<Point3D, 4> p{};
            for (std::size_t k = 0; k < 4; ++k) {
                const meshing::Node* node = mesh.findNode(tet->nodes[k]);
                if (node == nullptr) {
                    return std::unexpected(Error{ErrorCode::FailedPrecondition,
                                                 std::format("{}: cell {} names node {}, which the mesh does not hold",
                                                             subject, c, tet->nodes[k].value())});
                }
                p[k] = node->position;
            }
            // ONE FORMULA FOR A TETRAHEDRON'S VOLUME in the repository: this calls
            // the same function P16's validation and ADR-064's gate call, so the
            // sign convention cannot drift between them (ADR-065, Decision 3).
            const Volume volume = meshing::signedVolume(p[0], p[1], p[2], p[3]);
            if (!isFinite(volume) || volume.si() <= 0.0) {
                // UNREACHABLE, AND THE ONLY ONE OF THE FOUR VALUE GUARDS THAT IS
                // (ADR-065, Decision 5). This calls the same `meshing::signedVolume`
                // on the same node order ADR-064's gate called, with the same
                // predicate, so the comparison is bit-identical. Kept because that
                // argument is about this code and could stop being true.
                return std::unexpected(Error{ErrorCode::FailedPrecondition,
                                             std::format("{}: cell {} (element {}) has a volume of {} m^3", subject, c,
                                                         topology.cellOf(cell).value(), volume.si())});
            }
            const Point3D centroid = tetrahedronCentroid(p[0], p[1], p[2], p[3]);
            // REACHABLE, and the one witness that is NOT a rounding accident:
            // MeshBuilder refuses only a NON-FINITE coordinate, so a mesh may hold
            // 1.2e308 four times over, and `(a + b + c + d) / 4` overflows at the
            // SECOND addition while the determinant stays finite because the
            // magnitude enters each of its terms only linearly. Deterministic, in
            // every rounding mode. See
            // CfdGeometry_RefusesACentroidThatOverflowedWhileTheVolumeStayedFinite.
            if (!isFinite(centroid.x) || !isFinite(centroid.y) || !isFinite(centroid.z)) {
                return std::unexpected(
                    Error{ErrorCode::FailedPrecondition,
                          std::format("{}: cell {} has a centroid that is not finite", subject, c)});
            }
            g.volume_.push_back(volume);
            g.cellCentroid_.push_back(centroid);
        }

        // --- pass 2: the faces ------------------------------------------------
        g.areaVector_.reserve(topology.faces());
        g.faceCentroid_.reserve(topology.faces());
        g.owner_.reserve(topology.faces());
        g.neighbour_.reserve(topology.faces());
        for (std::size_t f = 0; f < topology.faces(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            const std::array<meshing::NodeId, 3> nodes = topology.faceNodes(face);
            std::array<Point3D, 3> p{};
            for (std::size_t k = 0; k < 3; ++k) {
                const meshing::Node* node = mesh.findNode(nodes[k]);
                if (node == nullptr) {
                    return std::unexpected(Error{ErrorCode::FailedPrecondition,
                                                 std::format("{}: face {} names node {}, which the mesh does not hold",
                                                             subject, f, nodes[k].value())});
                }
                p[k] = node->position;
            }
            const AreaVector3D s = triangleAreaVector(p[0], p[1], p[2]);
            const Area area = magnitude(s);
            if (!isFinite(s) || !isFinite(area) || area.si() <= 0.0) {
                // REACHABLE, and tested. `V = A h / 3` with V and h positive makes
                // A positive IN EXACT ARITHMETIC only: three vertices at 0, v, 2v are
                // exactly collinear, so this area is exactly zero while the
                // degenerate cell's volume is rounding noise ADR-064 accepts when it
                // comes out positive. See
                // CfdGeometry_RefusesAZeroAreaFaceOfACellWhoseVolumeComputedPositive.
                // This comment claimed the guard was undemonstrable.
                return std::unexpected(Error{ErrorCode::FailedPrecondition,
                                             std::format("{}: face {} presents an area of {} m^2", subject, f,
                                                         area.si())});
            }
            const Point3D centroid = triangleCentroid(p[0], p[1], p[2]);
            if (!isFinite(centroid.x) || !isFinite(centroid.y) || !isFinite(centroid.z)) {
                return std::unexpected(
                    Error{ErrorCode::FailedPrecondition,
                          std::format("{}: face {} has a centroid that is not finite", subject, f)});
            }
            g.areaVector_.push_back(s);
            g.faceCentroid_.push_back(centroid);
            g.owner_.push_back(topology.owner(face));
            const std::optional<CellIndex> neighbour = topology.neighbour(face);
            g.neighbour_.push_back(neighbour.value_or(CellIndex{}));
        }

        g.boundaryFace_.reserve(topology.boundaryFaces());
        for (std::size_t b = 0; b < topology.boundaryFaces(); ++b) {
            g.boundaryFace_.push_back(topology.faceOf(BoundaryFaceIndex::fromValue(b)));
        }

        // --- pass 3: the per-cell surface-vector sums -------------------------
        //
        // Each of a cell's faces contributes +Sf if the cell OWNS it and -Sf if the
        // cell is its neighbour, because ADR-064 stores one winding per face, wound
        // out of its owner. Getting that sign wrong is the one way this identity can
        // be made to look satisfied while every flux is wrong, so the sign is read
        // from `owner`, never assumed from the order the faces come in.
        g.cellSurfaceSum_.assign(topology.cells(), AreaVector3D{});
        g.cellClosure_.assign(topology.cells(), 0.0);
        for (std::size_t c = 0; c < topology.cells(); ++c) {
            const CellIndex cell = CellIndex::fromValue(c);
            AreaVector3D sum{};
            Area scale{};
            for (const FaceIndex face : topology.cellFaces(cell)) {
                const AreaVector3D& s = g.areaVector_[face.value()];
                sum = sum + (topology.owner(face) == cell ? s : -s);
                scale = scale + magnitude(s);
            }
            g.cellSurfaceSum_[c] = sum;
            g.cellClosure_[c] = scale.si() > 0.0 ? magnitude(sum).si() / scale.si() : 0.0;
        }

        if (const std::optional<Error> refusal = summarise(g, subject)) {
            return std::unexpected(*refusal);
        }
        return g;
    }

private:
    /// The totals and extremes, in ascending index order throughout. Also the one
    /// place the orthogonal distance is checked, because that check needs both
    /// centroids and the area vector, which only exist once pass 2 has run.
    [[nodiscard]] static std::optional<Error> summarise(CfdGeometry& g, std::string_view subject) {
        CfdGeometrySummary s;
        s.smallestCell = g.volume_.empty() ? Volume{} : g.volume_.front();
        s.largestCell = s.smallestCell;
        for (std::size_t c = 0; c < g.volume_.size(); ++c) {
            s.totalVolume = s.totalVolume + g.volume_[c];
            s.smallestCell = std::min(s.smallestCell, g.volume_[c]);
            s.largestCell = std::max(s.largestCell, g.volume_[c]);
            s.worstCellClosure = std::max(s.worstCellClosure, g.cellClosure_[c]);
        }

        bool firstInternalDistance = true;
        bool firstBoundaryDistance = true;
        Angle internalSum{};
        std::size_t internalCount = 0;
        Area boundaryScale{};
        for (std::size_t f = 0; f < g.areaVector_.size(); ++f) {
            const FaceIndex face = FaceIndex::fromValue(f);
            const Area area = magnitude(g.areaVector_[f]);
            const bool internal = f < g.internalFaces_;
            if (internal) {
                s.totalInternalArea = s.totalInternalArea + area;
            } else {
                s.totalBoundaryArea = s.totalBoundaryArea + area;
                s.boundarySurfaceVectorSum = s.boundarySurfaceVectorSum + g.areaVector_[f];
                boundaryScale = boundaryScale + area;
            }

            const Length normalSpan = g.orthogonalDistance(face);
            if (!isFinite(normalSpan) || normalSpan.si() <= 0.0) {
                // REACHABLE, and tested. This is (h_P + h_N) / 4 across an internal
                // face and h_P / 4 at a boundary one, both positive in EXACT
                // arithmetic -- but for two cells 1e-18 m from their shared plane and
                // far apart in it, the true value sits two orders below the rounding
                // error of differencing the centroids and the sign is an accident.
                // See CfdGeometry_RefusesAnOrthogonalDistanceThatLostItsSignToCancellation,
                // which also reaches the skewness guard below.
                return Error{ErrorCode::FailedPrecondition,
                             std::format("{}: face {} has an orthogonal distance of {} m", subject, f,
                                         normalSpan.si())};
            }
            // THE TWO POPULATIONS KEPT APART, as the angles are: see the two
            // fields' comment for what the merged minimum hid.
            if (internal) {
                if (firstInternalDistance || normalSpan < s.smallestInternalOrthogonalDistance) {
                    s.smallestInternalOrthogonalDistance = normalSpan;
                    firstInternalDistance = false;
                }
            } else {
                if (firstBoundaryDistance || normalSpan < s.smallestBoundaryOrthogonalDistance) {
                    s.smallestBoundaryOrthogonalDistance = normalSpan;
                    firstBoundaryDistance = false;
                }
            }

            const std::optional<Angle> angle = g.nonOrthogonality(face);
            if (!angle || !isFinite(*angle)) {
                return Error{ErrorCode::FailedPrecondition,
                             std::format("{}: face {} has no finite non-orthogonality angle", subject, f)};
            }
            if (internal) {
                s.worstNonOrthogonality = std::max(s.worstNonOrthogonality, *angle);
                internalSum = internalSum + *angle;
                ++internalCount;
                const std::optional<double> skew = g.skewness(face);
                if (!skew || !std::isfinite(*skew)) {
                    return Error{ErrorCode::FailedPrecondition,
                                 std::format("{}: internal face {} has no finite skewness", subject, f)};
                }
                s.worstSkewness = std::max(s.worstSkewness, *skew);
            } else {
                s.worstBoundaryNonOrthogonality = std::max(s.worstBoundaryNonOrthogonality, *angle);
            }
        }
        s.meanNonOrthogonality =
            internalCount == 0 ? Angle{} : internalSum / static_cast<double>(internalCount);
        s.domainClosure = boundaryScale.si() > 0.0 ? magnitude(s.boundarySurfaceVectorSum).si() / boundaryScale.si()
                                                   : 0.0;
        g.summary_ = s;
        return std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// Building one
// ---------------------------------------------------------------------------

Result<CfdGeometry> buildCfdGeometry(const meshing::Mesh& mesh, const CfdTopology& topology) {
    // THE STAMP BINDING, the only refusal here an ORDINARY mesh reaches -- the
    // four value guards above are reachable too, but only on inputs no mesher
    // produces (ADR-065, Decision 5). A topology for one generation says nothing
    // about the next, however equal their counts: counts are never compared for
    // identity. This said "the only refusal here a mesh can actually reach".
    if (!topology.describes(mesh)) {
        return std::unexpected(
            Error{ErrorCode::FailedPrecondition,
                  std::format("the finite-volume topology was derived from mesh generation {}.{}, but the metrics "
                              "were asked for generation {}.{}: a topology for one generation says nothing about "
                              "the next",
                              topology.mesh().mesh.value(), topology.mesh().generation, mesh.stamp().mesh.value(),
                              mesh.stamp().generation)});
    }
    return CfdGeometryAssembler::assemble(mesh, topology, "the finite-volume metrics of this mesh");
}

Result<CfdGeometry> requireCfdGeometry(const CfdModel& model, const CfdTopology& topology) {
    const meshing::Mesh& mesh = model.mesh().mesh();
    if (!topology.describes(mesh)) {
        return std::unexpected(
            Error{ErrorCode::FailedPrecondition,
                  std::format("the finite-volume topology for meshing control {} was derived from mesh generation "
                              "{}.{}, but the model now holds {}.{}: a topology for one generation says nothing "
                              "about the next",
                              model.control().value(), topology.mesh().mesh.value(), topology.mesh().generation,
                              mesh.stamp().mesh.value(), mesh.stamp().generation)});
    }
    const std::string subject =
        std::format("the finite-volume metrics of meshing control {}", model.control().value());
    Result<CfdGeometry> geometry = CfdGeometryAssembler::assemble(mesh, topology, subject);
    if (!geometry) {
        return geometry;
    }
    // The model's revision, not the topology's copy of it: the staged gate is what
    // has a CfdModel to ask, exactly as requireCfdTopology does.
    geometry->geometry_ = model.geometryRevision();
    return geometry;
}

} // namespace bettercad::cfd
