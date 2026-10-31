// Build-failure tests for the finite-volume geometric metrics (see CMakeLists.txt
// in this directory). Built once without any BETTERCAD_CF_* macro as a control,
// which must compile -- so every failure below is attributable to its own line and
// not to a missing header.
//
// WHAT THESE PROVE is P19-GEOM-001's units half (ADR-065). The topology's cases
// proved that four SUBSCRIPTS do not mix; these prove that the QUANTITIES do not
// either. A finite-volume method multiplies and divides lengths, areas, volumes
// and oriented areas constantly, and in SI every one of them is a bare `double`
// underneath:
//
//   Volume          a cell volume, m^3
//   Area            a face area, m^2
//   Length          an orthogonal distance, m
//   AreaVector3D    an oriented area, three m^2 components
//   Translation3D   a displacement, three m components
//   Angle           a non-orthogonality, radians
//   double          a skewness, a closure residual
//
// Nothing about the numbers distinguishes them -- a face area of 0.0025 and an
// orthogonal distance of 0.0025 are the same bits -- so "units are part of
// correctness" cannot rest on naming, and these cases are the proof that it rests
// on the type system.
//
// AND TWO ABSENCES ARE PROVED HERE TOO, because an ADR saying a thing does not
// exist is not the same as the compiler saying so: `CfdGeometrySummary` has NO
// verdict, threshold or acceptance member (ADR-065, Decision 7 -- measured, never
// judged), and there is no `CfdGeometryProblem` enum (Decision 5 -- every way the
// arithmetic could fail is refused upstream).
#include <bettercad/cfd/CfdGeometry.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/math/Point.hpp>
#include <bettercad/core/math/Vector.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <optional>

using namespace bettercad;
using namespace bettercad::cfd;

namespace {

// EVERY ONE IS [[maybe_unused]], AND THAT IS NOT TIDYING. Each is called only
// from inside a `#if defined(BETTERCAD_CF_...)` block, so in the CONTROL
// configuration -- no macro, the build every preset runs -- all eight are unused
// anonymous-namespace functions and `-Wunused -Werror` makes each one a hard
// error. The control target has no EXCLUDE_FROM_ALL, so that broke
// `cmake --build --preset <any>` for the whole project and would have failed the
// build stage of all seventeen qualification stages. `ctest -R
// compile_fail.cfdgeom.` did NOT catch it, because the control is a build target
// and not a registered test, so a green 29 of 29 was consistent with a project
// that did not build. An adversarial review found it.
//
// The sibling CfdTopologyMisuse.cpp does not need these because its helpers are
// called from the control path itself.
[[maybe_unused]] void takesVolume(Volume /*unused*/) {}
[[maybe_unused]] void takesArea(Area /*unused*/) {}
[[maybe_unused]] void takesLength(Length /*unused*/) {}
[[maybe_unused]] void takesAngle(Angle /*unused*/) {}
[[maybe_unused]] void takesAreaVector(AreaVector3D /*unused*/) {}
[[maybe_unused]] void takesTranslation(Translation3D /*unused*/) {}
[[maybe_unused]] void takesPoint(Point3D /*unused*/) {}
[[maybe_unused]] void takesDouble(double /*unused*/) {}

/// What a consumer of computed metrics receives. Never called: these cases are
/// about what it CANNOT do, which the compiler decides without running it.
[[maybe_unused]] void reads(const CfdGeometry& g) {
    const CellIndex cell = CellIndex::fromValue(0);
    const FaceIndex face = FaceIndex::fromValue(0);
    const BoundaryFaceIndex boundary = BoundaryFaceIndex::fromValue(0);

    // The accessors, used correctly, so the control exercises every signature the
    // cases below misuse.
    [[maybe_unused]] const Volume volume = g.cellVolume(cell);
    [[maybe_unused]] const std::optional<Point3D> centroid = g.cellCentroid(cell);
    [[maybe_unused]] const AreaVector3D areaVector = g.faceAreaVector(face);
    [[maybe_unused]] const Area area = g.faceArea(face);
    [[maybe_unused]] const std::optional<Direction3D> normal = g.faceUnitNormal(face);
    [[maybe_unused]] const Translation3D toFace = g.cellToFace(face);
    [[maybe_unused]] const Length span = g.orthogonalDistance(face);
    [[maybe_unused]] const std::optional<Angle> angle = g.nonOrthogonality(face);
    [[maybe_unused]] const std::optional<double> skew = g.skewness(face);
    [[maybe_unused]] const std::optional<Direction3D> outward = g.boundaryOutwardNormal(boundary);
    [[maybe_unused]] const CfdGeometrySummary& summary = g.summary();

    // ---- the quantities do not interconvert ------------------------------

#if defined(BETTERCAD_CF_A_VOLUME_IS_NOT_AN_AREA)
    // THE CONFUSION THE UNITS LAYER EXISTS FOR: a finite-volume balance divides a
    // face flux by a cell volume, and m^3 against m^2 is a factor of a metre that
    // no test on a unit cube would reveal.
    takesArea(g.cellVolume(cell));
#endif

#if defined(BETTERCAD_CF_AN_AREA_IS_NOT_A_VOLUME)
    takesVolume(g.faceArea(face));
#endif

#if defined(BETTERCAD_CF_AN_AREA_IS_NOT_A_LENGTH)
    // A face area and an orthogonal distance are the quantities a diffusion
    // coefficient divides one by the other; swapping them is dimensionally wrong
    // and numerically plausible.
    takesLength(g.faceArea(face));
#endif

#if defined(BETTERCAD_CF_A_LENGTH_IS_NOT_AN_AREA)
    takesArea(g.orthogonalDistance(face));
#endif

#if defined(BETTERCAD_CF_A_VOLUME_IS_NOT_A_BARE_NUMBER)
    // `si()` is where a quantity becomes a number, and it has to be asked for.
    takesDouble(g.cellVolume(cell));
#endif

#if defined(BETTERCAD_CF_A_BARE_NUMBER_IS_NOT_A_VOLUME)
    takesVolume(1.0);
#endif

#if defined(BETTERCAD_CF_AN_ANGLE_IS_NOT_A_BARE_NUMBER)
    // A non-orthogonality angle is radians and a skewness is a pure number; the
    // two are both "about one" on a bad mesh.
    takesDouble(*g.nonOrthogonality(face));
#endif

#if defined(BETTERCAD_CF_A_SKEWNESS_IS_NOT_AN_ANGLE)
    takesAngle(*g.skewness(face));
#endif

    // ---- the vectors do not interconvert ---------------------------------

#if defined(BETTERCAD_CF_AN_AREA_VECTOR_IS_NOT_A_TRANSLATION)
    // Sf and d are the two vectors every flux formula pairs. One is m^2 per
    // component and the other m, and `S . d` is a volume BECAUSE they differ.
    takesTranslation(g.faceAreaVector(face));
#endif

#if defined(BETTERCAD_CF_A_TRANSLATION_IS_NOT_AN_AREA_VECTOR)
    takesAreaVector(g.cellToFace(face));
#endif

#if defined(BETTERCAD_CF_AN_AREA_VECTOR_IS_NOT_A_POINT)
    takesPoint(g.faceAreaVector(face));
#endif

#if defined(BETTERCAD_CF_AN_AREA_VECTOR_IS_NOT_A_DIMENSIONLESS_VECTOR)
    // Vector3D is three bare doubles -- the type a caller reaches for when it
    // stops caring about units.
    [[maybe_unused]] const Vector3D plain = g.faceAreaVector(face);
#endif

#if defined(BETTERCAD_CF_AN_AREA_VECTOR_AND_A_TRANSLATION_DO_NOT_ADD)
    [[maybe_unused]] const auto nonsense = g.faceAreaVector(face) + g.cellToFace(face);
#endif

#if defined(BETTERCAD_CF_AN_AREA_VECTOR_HAS_NO_BARE_COMPONENTS)
    takesDouble(g.faceAreaVector(face).x);
#endif

#if defined(BETTERCAD_CF_THE_MAGNITUDE_OF_AN_AREA_VECTOR_IS_NOT_A_LENGTH)
    // |Sf| is an area. A reader who expected a length would get a number that
    // looks right at every scale where the mesh is about one metre across.
    takesLength(magnitude(g.faceAreaVector(face)));
#endif

#if defined(BETTERCAD_CF_THE_MAGNITUDE_OF_A_TRANSLATION_IS_NOT_AN_AREA)
    takesArea(magnitude(g.cellToFace(face)));
#endif

#if defined(BETTERCAD_CF_A_TRANSLATION_DOT_AN_AREA_VECTOR_IS_NOT_AN_AREA)
    // `d . Sf` IS a Volume, by dimension rather than by convention: Length x
    // Area. That is what makes the divergence-theorem identity type-check.
    takesArea(dot(g.cellToFace(face), g.faceAreaVector(face)));
#endif

#if defined(BETTERCAD_CF_A_POINT_DIFFERENCE_IS_NOT_A_POINT)
    // point - point is a VECTOR. A caller who kept it as a position would be
    // treating a displacement as an absolute location.
    takesPoint(*g.faceCentroid(face) - *g.cellCentroid(cell));
#endif

    // ---- the subscripts still do not mix, on these accessors -------------

#if defined(BETTERCAD_CF_PASSING_A_FACE_WHERE_A_CELL_IS_WANTED_FOR_A_VOLUME)
    [[maybe_unused]] const Volume wrong = g.cellVolume(face);
#endif

#if defined(BETTERCAD_CF_PASSING_A_CELL_WHERE_A_FACE_IS_WANTED_FOR_AN_AREA)
    [[maybe_unused]] const Area wrong = g.faceArea(cell);
#endif

#if defined(BETTERCAD_CF_PASSING_A_FACE_WHERE_A_BOUNDARY_FACE_IS_WANTED)
    // The boundary normal is addressed in the boundary range, not the face range:
    // ADR-064 keeps the arithmetic out of the caller's hands.
    [[maybe_unused]] const auto wrong = g.boundaryOutwardNormal(face);
#endif

#if defined(BETTERCAD_CF_AN_INTEGER_IS_NOT_A_CELL_FOR_A_VOLUME)
    [[maybe_unused]] const Volume wrong = g.cellVolume(0);
#endif

    // ---- what the summary does NOT have ----------------------------------

#if defined(BETTERCAD_CF_THE_SUMMARY_HAS_NO_VERDICT)
    // ADR-065, Decision 7: MEASURED, NEVER JUDGED. An acceptance threshold is
    // P19-VALID-001's decision, and there is nowhere here to put one.
    [[maybe_unused]] const bool verdict = summary.acceptable;
#endif

#if defined(BETTERCAD_CF_THE_SUMMARY_HAS_NO_THRESHOLD)
    [[maybe_unused]] const auto limit = summary.maximumSkewness;
#endif

#if defined(BETTERCAD_CF_THE_SUMMARY_HAS_NO_QUALITY_GRADE)
    [[maybe_unused]] const auto grade = summary.grade;
#endif

#if defined(BETTERCAD_CF_THE_GEOMETRY_HAS_NO_PROBLEM_ENUM)
    // ADR-065, Decision 5: there is no refusal enum, because every value in one
    // would be a promise the code does not keep.
    [[maybe_unused]] const auto problem = g.firstProblem();
#endif

#if defined(BETTERCAD_CF_THE_GEOMETRY_CANNOT_BE_REMEASURED_IN_PLACE)
    // Derived state is recomputed wholesale, never edited: there is no setter.
    g.setCellVolume(cell, Volume{});
#endif
}

#if defined(BETTERCAD_CF_A_GEOMETRY_CANNOT_BE_BUILT_WITHOUT_ITS_BUILDER)
// Possession of a CfdGeometry is proof that the metrics were computed from a mesh
// the topology describes and found finite and positive. A public constructor would
// make that proof worthless, so there is none.
[[maybe_unused]] void conjure() {
    const CfdGeometry conjured{};
    (void)conjured;
}
#endif

#if defined(BETTERCAD_CF_A_SUMMARY_IS_NOT_A_GEOMETRY)
[[maybe_unused]] void confuse(const CfdGeometry& g) {
    const CfdGeometry copy = g.summary();
    (void)copy;
}
#endif

} // namespace
