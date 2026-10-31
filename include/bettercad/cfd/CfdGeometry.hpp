#pragma once

// P19-GEOM-001 -- the finite-volume geometric metrics of a topology that was
// derived from a proved mesh (ADR-065).
//
// WHAT THIS ADDS TO ADR-064. The topology named the cells and the faces and said
// which cells sit on either side of each one. It holds no coordinate: a face is
// three NodeIds and a cell is an ElementId, so nothing yet says how big a cell is,
// where its centroid sits, or how much area a face presents and which way. This
// file is the first in the chain to compute a NUMBER A SOLVER WILL DIVIDE BY, and
// it is the last derived structure before the field maps (P19-DOF-001) and the
// operators have something to integrate over.
//
// WHY THE ARITHMETIC IS SAFE, AND IN WHAT SENSE. Every quantity a finite-volume
// method divides by -- the cell volume, the face area, the distance between two
// cell centroids, and the projection of that distance onto the face normal -- is
// proved non-zero IN EXACT ARITHMETIC for any mesh that yields a topology. The
// proof is in ADR-065, Decision 5, and it rests on the two refusals an adversarial
// review forced ADR-064 to add (`NonPositiveCellVolume` and
// `InconsistentFaceOrientation`). In outline: a positive cell volume makes the
// owner's opposite vertex lie strictly off the face plane, so the owner's centroid
// is at -h_P/4 from it; the opposite winding makes the neighbour's centroid lie at
// +h_N/4; so d . n = (h_P + h_N) / 4 > 0, and the face area is positive because
// V = A h / 3.
//
// "IN EXACT ARITHMETIC" IS LOAD-BEARING, AND THIS FILE SAID OTHERWISE FIRST. It
// claimed the quantities were proved non-zero full stop, that the builder's guards
// were therefore unreachable, and that the only reachable refusal was the stamp
// binding. Two reviews disproved that: FOUR of the guards are reachable, and the
// proof's step `V > 0 => A > 0` holds of the EXACT values while a guard only ever
// sees computed ones. ADR-065, Decision 5 carries the per-guard status and the
// witnesses; the table under `buildCfdGeometry` below is the summary, and each
// reachable guard now has a test, as ADR-063's nine and ADR-064's eight do.
//
// THERE IS STILL NO REFUSAL ENUM HERE, for a different reason than the one first
// given (ADR-065, Decision 5). It is not that the guards cannot fire. It is that
// every witness is a mesh ADR-063 and ADR-064 would call valid and no mesher would
// produce -- an exactly collinear face, two cells 1e-18 m from their shared plane,
// coordinates at 1.2e308 -- failing a floating-point comparison rather than
// exhibiting an engineering defect. The `FailedPrecondition` names the cell or
// face and the value it computed, which is what a reader of such a failure needs.
// If a real mesh ever reaches one, that is the moment to give it a name.
//
// WHAT THIS FILE DOES NOT DO. It sets no threshold: an acceptance bound on a
// quality metric is P19-VALID-001's decision, and P19-MESH-001's owner finding F1
// already records that the eight P16 reference meshes are poor finite-volume
// meshes. Everything here is MEASURED AND NEVER JUDGED. No field storage
// (P19-DOF-001), no boundary condition (P19-BC-001), no gradient, no matrix and no
// solve.

#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/cfd/Export.hpp>
#include <bettercad/core/Error.hpp>
#include <bettercad/core/math/Direction.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Units.hpp>
#include <bettercad/meshing/Mesh.hpp>
#include <bettercad/meshing/MeshIds.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace bettercad::cfd {

class CfdModel;

// ---------------------------------------------------------------------------
// The point-level formulas
// ---------------------------------------------------------------------------
//
// PURE FUNCTIONS OF THREE OR FOUR POINTS, exposed for the reason ADR-063 exposed
// `inspectCfdMesh`: a formula that can only be reached through a gate cannot be
// shown to be right on an input the gate refuses. These are where the degenerate
// cases are tested, and where the analytic validation starts.
//
// EVERY ONE IS EXACT FOR ITS SHAPE. A triangle is planar and a Tet4 has planar
// faces and a uniform interior, so the centroid of a tetrahedron IS the mean of
// its four vertices and the area vector of a triangle IS the half cross product.
// None of these is a quadrature.

/// The ORIENTED area of the triangle @p a @p b @p c: `1/2 (b - a) x (c - a)`.
///
/// Its right-hand normal follows the stored order, so the sign is the caller's
/// winding -- for a CFD face, ADR-063's "wound out of the owner". This is the
/// signed sibling of `meshing::triangleArea`, which is deliberately unsigned
/// because the meshing module declines to have an opinion about which way a
/// triangle faces; the opinion is ADR-063's, so the signed form lives here.
///
/// Returns a zero vector for three collinear or coincident points, which is the
/// correct answer and not an error: such a triangle presents no area.
[[nodiscard]] BETTERCAD_CFD_EXPORT AreaVector3D triangleAreaVector(const Point3D& a, const Point3D& b,
                                                                   const Point3D& c) noexcept;

/// The centroid of the triangle @p a @p b @p c: the mean of its vertices. Exact.
[[nodiscard]] BETTERCAD_CFD_EXPORT Point3D triangleCentroid(const Point3D& a, const Point3D& b,
                                                            const Point3D& c) noexcept;

/// The centroid of the tetrahedron @p a @p b @p c @p d: the mean of its four
/// vertices. Exact for a uniform tetrahedron, and independent of the orientation.
[[nodiscard]] BETTERCAD_CFD_EXPORT Point3D tetrahedronCentroid(const Point3D& a, const Point3D& b, const Point3D& c,
                                                               const Point3D& d) noexcept;

// NO `cellVolume(a, b, c, d)` HERE. `meshing::signedVolume` already is that
// function, with ADR-032's sign convention, and this module calls it. One formula
// for a tetrahedron's volume in the repository means the convention cannot drift
// between the mesh that validated it and the solver that integrates over it.

// ---------------------------------------------------------------------------
// What was measured
// ---------------------------------------------------------------------------

/// The totals and extremes of one geometry. MEASURED, NEVER JUDGED: there is no
/// threshold here and no verdict (ADR-065, Decision 7). P19-VALID-001 owns
/// acceptance.
///
/// Every sum runs in ASCENDING INDEX ORDER, which is part of the answer and not a
/// detail: `core/math/Vector.hpp` states the rule for summing forces, and it holds
/// here -- floating-point addition is not associative, so a sum taken in another
/// order is a different number, and a run-to-run difference in a conservation
/// residual would be worthless.
///
/// THE TWO NON-ORTHOGONALITY FIELDS ARE DIFFERENT POPULATIONS and are kept apart
/// rather than merged into one worst case. The internal one is ADR-063's
/// definition, over the faces between two cells, and is directly comparable with
/// the numbers P19-MESH-001 recorded; the boundary one is over faces with one
/// cell, where `d` is the cell-to-face vector. A single merged extreme would be
/// comparable with nothing.
struct CfdGeometrySummary {
    /// Sum of the cell volumes. For a planar-faced body this is the body's exact
    /// volume to accumulation; for a curved one the tetrahedra approach it from
    /// below (RM-MESH-02) or from above where a hole is inscribed (RM-MESH-03).
    Volume totalVolume{};
    /// Sum of |Sf| over the boundary faces: the area of the closed surface.
    Area totalBoundaryArea{};
    /// Sum of |Sf| over the internal faces.
    Area totalInternalArea{};
    Volume smallestCell{};
    Volume largestCell{};
    /// Smallest `d . n` over the INTERNAL faces, and over the BOUNDARY faces.
    ///
    /// TWO FIELDS FOR THE SAME REASON THE ANGLES ARE TWO, and an adversarial
    /// review found this as ONE. `d . n` is `(h_P + h_N) / 4` across an internal
    /// face and `h_P / 4` at a boundary one, so the merged minimum mixed two
    /// quantities of different kinds and the one a diffusion coefficient divides
    /// by ACROSS AN INTERIOR FACE was not reported at all. Measured on the
    /// suite's two-cell fixture, the merged field read 0.0025 m (a boundary face)
    /// while the internal minimum was 0.00433 m. P19-VALID-001 will set a
    /// threshold against these, so the merge mattered.
    ///
    /// WHICH OF THE TWO IS SMALLER IS NOT FIXED. Per face the boundary value is
    /// the smaller, but these are minima over DIFFERENT SETS of faces, so a mesh
    /// whose worst sliver is interior reverses them -- RM-MESH-02 does, at
    /// 0.141 mm internal against 0.189 mm boundary. (Those two figures were
    /// written here as 1.41e-4 and 1.89e-4 mm first, wrong by a factor of a
    /// thousand, which an adversarial review caught.)
    ///
    /// Zero where there is no face of that kind -- including an EMPTY topology,
    /// which is reachable: P19-TOPO-001 decided deliberately not to re-refuse an
    /// empty mesh, because `EmptyMesh` is the mesh contract's refusal. An earlier
    /// version of this comment claimed positivity unconditionally.
    Length smallestInternalOrthogonalDistance{};
    Length smallestBoundaryOrthogonalDistance{};
    /// Over INTERNAL faces: the angle between `C_N - C_P` and the face normal.
    /// Strictly below 90 degrees, by the same proof.
    Angle worstNonOrthogonality{};
    Angle meanNonOrthogonality{};
    /// Over BOUNDARY faces: the angle between `C_f - C_P` and the face normal.
    Angle worstBoundaryNonOrthogonality{};
    /// Over INTERNAL faces: `|C_f - x| / |d|`, where x is where the line joining
    /// the two cell centroids crosses the face plane. Dimensionless.
    double worstSkewness = 0.0;
    /// The largest per-cell closure residual, `|sum Sf| / sum |Sf|`, which is
    /// zero algebraically and a few rounding errors in double precision.
    double worstCellClosure = 0.0;
    /// `sum Sf` over every boundary face: the surface-vector sum of the whole
    /// closed domain, which is zero for the same reason.
    AreaVector3D boundarySurfaceVectorSum{};
    /// `|sum Sf| / sum |Sf|` over the boundary: the dimensionless form of the
    /// field above.
    double domainClosure = 0.0;

    friend bool operator==(const CfdGeometrySummary&, const CfdGeometrySummary&) = default;
};

// ---------------------------------------------------------------------------
// The geometry
// ---------------------------------------------------------------------------

/// The finite-volume metrics of one topology of one generation of one mesh.
///
/// A VALUE THAT BORROWS NOTHING, like `CfdMeshContract` and `CfdTopology`: it
/// copies what it needs, so it cannot dangle behind a mesh that has been remeshed.
/// It is DERIVED STATE and is never persisted (ADR-058), it is keyed to the
/// MeshStamp it came from, and `describes` is how a holder checks it is being
/// applied to the same mesh generation.
///
/// IT IS INDEXED BY THE TOPOLOGY'S INDICES AND DOES NOT HOLD THE TOPOLOGY. A
/// caller holds both; a `CellIndex` means the same cell in each, because both were
/// derived from one mesh generation and ADR-064's assignment is deterministic.
///
/// WHAT IS STORED AND WHAT IS DERIVED (ADR-065, Decision 4):
///
/// ```text
/// stored   per cell   volume, centroid, signed surface-vector sum, closure
///          per face   area vector, centroid
/// derived  per face   area, unit normal, owner-to-neighbour, cell-to-face,
///                     orthogonal distance, non-orthogonality, skewness
/// ```
///
/// The area vector is PRIMARY and the area and the unit normal are derived from it
/// (ADR-065, Decision 2), so `|Sf|` and `Sf / |Sf|` are exactly consistent with the
/// stored vector, and the four area vectors of a cell still cancel to rounding
/// rather than to the error of a square root and a division.
///
/// WHY SOME ACCESSORS RETURN AN OPTIONAL AND SOME DO NOT. Every accessor is total
/// for an index this geometry does not hold, and the rule for which total answer
/// is a CONSEQUENCE of Decision 5's proof: where zero is provably impossible for
/// an index that IS held, zero is returned and means "not held"; where zero is a
/// legal measurement -- a centroid at the origin, a perfectly orthogonal face, a
/// perfectly closed cell -- an empty optional is returned instead, because
/// fabricating the zero would make the two indistinguishable. That is the rule
/// `Mesh::bounds()` states for an empty mesh, applied here.
class BETTERCAD_CFD_EXPORT CfdGeometry {
public:
    [[nodiscard]] std::size_t cells() const noexcept { return volume_.size(); }
    [[nodiscard]] std::size_t faces() const noexcept { return areaVector_.size(); }
    [[nodiscard]] std::size_t internalFaces() const noexcept { return internalFaces_; }
    [[nodiscard]] std::size_t boundaryFaces() const noexcept { return faces() - internalFaces_; }
    [[nodiscard]] const CfdGeometrySummary& summary() const noexcept { return summary_; }

    // --- per cell -----------------------------------------------------------

    /// The volume of @p cell, from `meshing::signedVolume`. Strictly positive for
    /// a cell this geometry holds, so a zero means @p cell is not one.
    [[nodiscard]] Volume cellVolume(CellIndex cell) const noexcept;

    /// The centroid of @p cell: the mean of its four vertices. An optional,
    /// because the origin is a legal centroid.
    [[nodiscard]] std::optional<Point3D> cellCentroid(CellIndex cell) const noexcept;

    /// `sum Sf` over @p cell's four faces, each signed so that it points OUT of
    /// @p cell -- that is, negated on a face @p cell neighbours rather than owns.
    ///
    /// THE MILESTONE'S KEY IDENTITY, and zero is the right answer: the four
    /// outward area vectors of a tetrahedron cancel algebraically (ADR-065,
    /// Decision 2 writes the expansion out).
    ///
    /// It is STORED rather than recomputed so that the accumulation order, and
    /// the sign each face is taken with, are fixed ONCE instead of per caller.
    /// The sign is the part that matters: a caller summing a cell's four stored
    /// area vectors without consulting `owner` gets a residual of order one, not
    /// of order eps.
    ///
    /// A weaker claim was made here first -- that another ORDER would change the
    /// last bit -- and the suite's own measurements contradict it: on four of
    /// five hand-built tetrahedra and on two of the eight reference meshes the
    /// cancellation is bit-exact, so on those inputs no order can change
    /// anything. What is guaranteed is that the value this reports is the same in
    /// every build, not that a caller could not reproduce it.
    [[nodiscard]] std::optional<AreaVector3D> cellSurfaceVectorSum(CellIndex cell) const noexcept;

    /// The dimensionless form of the sum above: `|sum Sf| / sum |Sf|` over
    /// @p cell's faces. Zero is perfect closure, so this is an optional.
    [[nodiscard]] std::optional<double> cellClosure(CellIndex cell) const noexcept;

    // --- per face -----------------------------------------------------------

    /// `Sf` for @p face: `1/2 (b - a) x (c - a)` on the nodes ADR-064 stores,
    /// which are wound OUT OF THE OWNER. So it points owner -> neighbour across an
    /// internal face and out of the fluid at a boundary face, with no sign array
    /// and no orientation field anywhere.
    ///
    /// Non-zero for a face this geometry holds, so a zero vector means @p face is
    /// not one.
    [[nodiscard]] AreaVector3D faceAreaVector(FaceIndex face) const noexcept;

    /// `|Sf|`. Strictly positive for a held face, by Decision 5's proof
    /// (`V = A h / 3` with both V and h positive), so a zero means not held.
    [[nodiscard]] Area faceArea(FaceIndex face) const noexcept;

    /// `Sf / |Sf|`. An optional because `Direction3D` has no invalid value, not
    /// because a held face could fail to have one.
    [[nodiscard]] std::optional<Direction3D> faceUnitNormal(FaceIndex face) const noexcept;

    /// The centroid of @p face: the mean of its three nodes. An optional, because
    /// the origin is a legal centroid.
    [[nodiscard]] std::optional<Point3D> faceCentroid(FaceIndex face) const noexcept;

    // --- per face, relational -----------------------------------------------

    /// `d = C_N - C_P` across @p face. Empty for a boundary face, which has no
    /// neighbour, and for a face this geometry does not hold -- an optional for the
    /// reason ADR-064 made `neighbour` one: a sentinel is the kind of thing a
    /// caller forgets to check.
    [[nodiscard]] std::optional<Translation3D> ownerToNeighbour(FaceIndex face) const noexcept;

    /// `C_f - C_P`: from the OWNER's centroid to the face centroid. Total over
    /// every face, internal and boundary.
    ///
    /// Its projection on the normal is EXACTLY `h_P / 4`, always, and not merely
    /// at worst: `C_P = (3 C_f + apex) / 4`, so `(C_f - C_P) . n = h_P / 4`
    /// identically. So a zero means @p face is not held. (This said "at worst"
    /// first, which implies a bound where there is an identity.)
    [[nodiscard]] Translation3D cellToFace(FaceIndex face) const noexcept;

    /// `d . n`, the distance a diffusion coefficient divides by: `d` is the
    /// owner-to-neighbour vector on an internal face and the cell-to-face vector
    /// on a boundary face. Strictly positive for a held face (Decision 5), so a
    /// zero means not held.
    [[nodiscard]] Length orthogonalDistance(FaceIndex face) const noexcept;

    /// `d . n / |d|`, the COSINE of the non-orthogonality angle, clamped to
    /// `[-1, 1]`, with `d` as in `orthogonalDistance`.
    ///
    /// THE FORM A SOLVER SHOULD USE, and the reason it is an accessor rather than
    /// a division left to the caller. The quotient is what a non-orthogonal
    /// correction multiplies by, and it is FAR BETTER CONDITIONED THAN THE ANGLE
    /// -- but it is not well conditioned absolutely, and this said "well
    /// conditioned everywhere" first. Its absolute error is of order
    /// `eps * L / |d|`, where L is the distance of the nodes from the model
    /// origin. Measured on one two-cell fixture translated along (1,1,1), with the
    /// cosine and `|d|` held fixed:
    ///
    /// ```text
    /// translation   L / |d|    error in the cosine
    /// 0             3.6        0
    /// 1 m           8.8        0
    /// 100 m         6.3e2      2.9e-15
    /// 10 km         6.3e4      8.2e-13
    /// ```
    ///
    /// And driving the cosine from 0.9918 down to 0.0920 at a fixed translation
    /// does NOT raise it, which is what rules out the `eps / cos` this comment
    /// claimed first. The angle's loss at the same place is `sqrt(2 eps)`, six
    /// orders worse, which is the useful comparison.
    ///
    /// AND COMPUTED, IT CAN EXCEED ONE. On the most regular interior face a
    /// tetrahedral mesh can have, `d` is exactly parallel to `n` and the quotient
    /// rounds to `1.000000000000000222`: one eps above one, and outside the domain
    /// of `acos`, `asin` and anything else that takes a cosine. ADR-065's first
    /// draft recommended the bare division to callers for its conditioning and
    /// did not say that; a review pointed out that the suite's own clamp fixture
    /// proves the recommendation hands back an out-of-range number. So the clamp
    /// lives HERE, once, and `nonOrthogonality` is defined on top of this.
    ///
    /// AN OPTIONAL, WHICH IS THE ONE ACCESSOR THAT DEPARTS FROM THIS CLASS'S RULE,
    /// and the departure is deliberate. By the rule below, zero is provably
    /// impossible for a held face (Decision 5 (vi)), so this should return a bare
    /// `double` with zero meaning "not held", as `orthogonalDistance` does. It
    /// returns an optional instead because `nonOrthogonality` MUST be one -- zero
    /// is a legal angle -- and defining one as the `arccos` of the other is
    /// simplest when both have the same shape. A review pointed out that the
    /// reason first given here ("for the reason `nonOrthogonality` is one") was
    /// the opposite of the real one.
    [[nodiscard]] std::optional<double> orthogonalityCosine(FaceIndex face) const noexcept;

    /// The angle between `d` and the face normal, with `d` as in
    /// `orthogonalDistance`: `arccos(orthogonalityCosine(face))`. Zero is a
    /// perfectly orthogonal face, hence an optional. Strictly below 90 degrees for
    /// a held face.
    ///
    /// ILL-CONDITIONED NEAR ZERO, AND THAT IS NOT A DEFECT IN THE GEOMETRY.
    /// `d(theta)/d(cos) = -1 / sin(theta)` diverges as `theta -> 0`, so a cosine
    /// perturbed by one eps becomes an angle of `sqrt(2 eps)`, about `2.1e-8` rad.
    /// Measured under a rigid transform of a whole mesh -- which changes nothing
    /// -- the cosine of an exactly orthogonal face moves by `4.4e-16` and its
    /// angle by `2.107e-8` rad. So THIS is the reported form and
    /// `orthogonalityCosine` is the computed one; a consumer that needs precision
    /// near zero takes the cosine.
    ///
    /// Total over boundary faces as well as internal ones, where skewness is not
    /// (ADR-065, Decision 6): at a boundary face `C_f - C_P` is the direction a
    /// boundary gradient really is evaluated along, so the angle is a measurement;
    /// skewness there would be identically zero by construction, which
    /// `CfdGeometry_TheBoundarySkewnessDecision6RejectedWouldBeIdenticallyZero`
    /// computes rather than asserts in prose.
    [[nodiscard]] std::optional<Angle> nonOrthogonality(FaceIndex face) const noexcept;

    /// `|C_f - x| / |d|`, where x is where the line joining the two cell centroids
    /// crosses the plane of @p face: the error in interpolating between two cell
    /// values. Dimensionless.
    ///
    /// Empty for a BOUNDARY face, where there is no second value to interpolate
    /// with, and empty for a face not held. Zero is perfect alignment.
    [[nodiscard]] std::optional<double> skewness(FaceIndex face) const noexcept;

    // --- boundary -----------------------------------------------------------

    /// The outward unit normal of @p boundaryFace: out of the fluid.
    ///
    /// THE SAME VECTOR AS `faceUnitNormal` of the face it is, and defined in terms
    /// of it rather than stored again. It is named because "out of the fluid" is
    /// what a boundary condition needs to be true, and because the guarantee is
    /// worth a name even when the implementation is a translation: ADR-063 winds
    /// every boundary triangle out of its owner, and `InwardBoundaryFace` refuses
    /// a mesh that does not. A test checks it points away from the owner's
    /// centroid, which is independent of the winding the builder used.
    [[nodiscard]] std::optional<Direction3D> boundaryOutwardNormal(BoundaryFaceIndex boundaryFace) const noexcept;

    // --- binding ------------------------------------------------------------

    /// The mesh generation these metrics were computed from.
    [[nodiscard]] const meshing::MeshStamp& mesh() const noexcept { return mesh_; }
    /// The geometry that mesh was built from.
    [[nodiscard]] const meshing::GeometryRevision& geometry() const noexcept { return geometry_; }
    /// Whether this geometry was computed from @p mesh. STAMPS, NEVER COUNTS.
    [[nodiscard]] bool describes(const meshing::Mesh& mesh) const noexcept { return mesh.owns(mesh_); }
    /// Whether this geometry is indexed by @p topology's indices: the same mesh
    /// generation, and the same counts.
    ///
    /// The stamp is the identity; the counts corroborate it. Two topologies of one
    /// generation are equal field for field -- ADR-064's assignment is
    /// deterministic and a test asserts it -- so agreeing on the stamp is already
    /// decisive, and the counts are checked because a caller who gets this wrong
    /// would otherwise index past the end of an array.
    [[nodiscard]] bool describes(const CfdTopology& topology) const noexcept;

    friend bool operator==(const CfdGeometry&, const CfdGeometry&) = default;

private:
    CfdGeometry() = default;

    /// The assembler, defined in CfdGeometry.cpp. A NAMED FRIEND for the reason
    /// ADR-064 gave: the constructor is private, because possession of a
    /// CfdGeometry is proof that the metrics were computed from a mesh the
    /// topology describes and found finite, and only a friend may assemble one.
    friend struct CfdGeometryAssembler;
    /// The staged gate, which sets the geometry revision from the model it has.
    friend BETTERCAD_CFD_EXPORT Result<CfdGeometry> requireCfdGeometry(const CfdModel& model,
                                                                       const CfdTopology& topology);

    meshing::MeshStamp mesh_{};
    meshing::GeometryRevision geometry_{};
    CfdGeometrySummary summary_{};
    std::size_t internalFaces_ = 0;

    /// Per cell, in CellIndex order.
    std::vector<Volume> volume_{};
    std::vector<Point3D> cellCentroid_{};
    std::vector<AreaVector3D> cellSurfaceSum_{};
    std::vector<double> cellClosure_{};

    /// Per face, in FaceIndex order.
    std::vector<AreaVector3D> areaVector_{};
    std::vector<Point3D> faceCentroid_{};

    /// Per face: the owner's CellIndex, and the neighbour's or `kInvalid`. COPIED
    /// from the topology rather than looked up through it, so that every accessor
    /// is a read of this value and none needs the topology passed back in.
    std::vector<CellIndex> owner_{};
    std::vector<CellIndex> neighbour_{};
    /// Per boundary face: the face it is. Copied for the same reason.
    std::vector<FaceIndex> boundaryFace_{};
};

// ---------------------------------------------------------------------------
// Building one
// ---------------------------------------------------------------------------

/// Computes the metrics of @p topology on @p mesh.
///
/// A PLAIN MESH AND A PLAIN TOPOLOGY, for the reason ADR-063 and ADR-064 took
/// plain inputs: the staged gate below adds the control name and the geometry
/// revision, and a builder reachable only through it could not be shown a mesh
/// generation the topology does not describe.
///
/// Refuses, with FailedPrecondition. EVERY SITE, with its real status: an earlier
/// version of this table listed four of the nine sites, and the version after that
/// listed all nine with THREE VERDICTS WRONG and a claim that an attempt to reach
/// them had been made and failed. No attempt had been made. Both reviews that
/// found this are recorded in ADR-065, Decision 5, which carries the witnesses.
///
/// ```text
/// topology.describes(mesh) is false      REACHABLE   tested
/// a face area is not finite or zero      REACHABLE   tested: three vertices at
///                                                    0, v, 2v are exactly
///                                                    collinear, and the
///                                                    degenerate cell's volume is
///                                                    rounding noise ADR-064
///                                                    accepts when positive
/// a centroid is not finite               REACHABLE   tested: four coordinates at
///                                                    1.2e308 are each finite and
///                                                    their SUM overflows, while
///                                                    the determinant stays finite
///                                                    because the magnitude enters
///                                                    it only linearly. The one
///                                                    witness that is NOT a
///                                                    rounding accident
/// an orthogonal distance is <= 0         REACHABLE   tested: two cells 1e-18 m
///                                                    from their shared plane but
///                                                    far apart in it
/// a face has no finite skewness          REACHABLE   tested: the same
///                                                    configuration drives
///                                                    d . Sf to zero
/// a cell volume is not finite or <= 0    unreachable  the same signedVolume call
///                                                    on the same node order
///                                                    ADR-064's gate made, so the
///                                                    comparison is bit-identical
/// a face has no finite angle             unreachable  the angle is arccos of a
///                                                    clamped value, so it is
///                                                    finite whenever the cosine
///                                                    exists; and |d| == 0 forces
///                                                    d . n == 0 exactly, so the
///                                                    distance guard fires first
/// a cell names an element the mesh lacks unreachable  same stamp, and mesh
///                                                    storage is append-only
/// a cell names a node the mesh lacks     unreachable  as above
/// a face names a node the mesh lacks     unreachable  as above
/// ```
///
/// The four unreachable guards are retained because the proof that they cannot
/// fire is in exact arithmetic, and because `MeshBuilder::build()` may be called
/// twice on a growing builder -- two Mesh values, one stamp -- so append-only
/// storage is what keeps the topology's handles resolving rather than an
/// incidental property.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdGeometry> buildCfdGeometry(const meshing::Mesh& mesh,
                                                                        const CfdTopology& topology);

/// Computes the metrics for a validated CFD input whose @p topology was derived
/// from the generation @p model now holds.
///
/// THE STAGED GATE (ADR-053's shape, as ADR-063 and ADR-064 used it). It names the
/// meshing control in a refusal, which the plain builder cannot, and carries the
/// model's geometry revision onto the result.
[[nodiscard]] BETTERCAD_CFD_EXPORT Result<CfdGeometry> requireCfdGeometry(const CfdModel& model,
                                                                          const CfdTopology& topology);

} // namespace bettercad::cfd
