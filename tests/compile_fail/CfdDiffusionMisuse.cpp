/// \file
/// P19-DIFF-001's compile-fail group: what the type system must make
/// impossible, ADR-069.
///
/// THE CLAIM THIS GROUP CARRIES. Three things, and the third is the one a value
/// test provably cannot reach:
///
/// 1. A `CfdSparseMatrix` cannot be CONJURED. Possession is the evidence
///    (ADR-036): the only door is `fromCsr`, which validates, so a matrix whose
///    arrays are inconsistent does not exist. Because a `CfdDiffusionSystem`
///    holds one by value, that guarantee extends to the whole system.
/// 2. The assembled arrays are READ-ONLY. A consumer cannot reach in and change
///    a coefficient, which is what makes the validation at the door meaningful
///    rather than a formality.
/// 3. `mu`, the face area and the orthogonal distance CANNOT BE SWAPPED. They
///    appear only as the product `mu A / (d . n)`, so no assertion on a
///    coefficient can tell them apart -- P18 recorded the same hazard for
///    `rho * cp`, where swapping the two gave bit-identical matrices. Here the
///    three are a `DynamicViscosity`, an `Area` and a `Length`, so the swap is a
///    compile error and these cases are the evidence that it is.
///
/// EVERY HELPER IS `[[maybe_unused]]`, AND THAT IS NOT TIDYING. Each is called
/// only from inside a `#if defined(BETTERCAD_CF_...)` block, so in the CONTROL
/// configuration -- no macro, the build every preset runs -- they are unused,
/// and this translation unit is compiled with `-Werror`.

#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdDiffusion.hpp>
#include <bettercad/cfd/CfdSparse.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::cfd;

// ---------------------------------------------------------------------------
// A value cannot be conjured
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_SPARSE_MATRIX)
[[maybe_unused]] CfdSparseMatrix conjured() {
    // The default constructor is private: `fromCsr` is the only door.
    return CfdSparseMatrix{};
}
#endif

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_DIFFUSION_SYSTEM)
[[maybe_unused]] std::size_t conjuredSystem() {
    // It holds a matrix BY VALUE, so the matrix's guarantee extends to it: an
    // unvalidated system is not representable either.
    CfdDiffusionSystem system;
    return system.source.size();
}
#endif

#if defined(BETTERCAD_CF_ASSEMBLE_A_SPARSE_MATRIX_FROM_PARTS)
[[maybe_unused]] CfdSparseMatrix assembled() {
    // Nor by naming the members: they are private, and there is no friend a
    // caller can reach.
    return CfdSparseMatrix{2, 2, {0, 1, 2}, {0, 1}, {1.0, 1.0}};
}
#endif

// ---------------------------------------------------------------------------
// The arrays are read-only
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_SPARSE_VALUES_ARE_WRITABLE)
[[maybe_unused]] void overwriteAValue(const CfdSparseMatrix& matrix) {
    matrix.values()[0] = 0.0;
}
#endif

#if defined(BETTERCAD_CF_THE_SPARSE_ROW_START_IS_WRITABLE)
[[maybe_unused]] void overwriteTheLayout(const CfdSparseMatrix& matrix) {
    matrix.rowStart()[0] = 7;
}
#endif

#if defined(BETTERCAD_CF_THE_SPARSE_COLUMNS_ARE_WRITABLE)
[[maybe_unused]] void overwriteAColumn(const CfdSparseMatrix& matrix) {
    matrix.inner()[0] = 7;
}
#endif

// ---------------------------------------------------------------------------
// The three factors of the coefficient are not interchangeable
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_VISCOSITY_IS_AN_AREA)
[[maybe_unused]] CfdDiffusionInput areaAsViscosity(Area area) {
    CfdDiffusionInput input;
    input.viscosity = area;
    return input;
}
#endif

#if defined(BETTERCAD_CF_A_VISCOSITY_IS_A_LENGTH)
[[maybe_unused]] CfdDiffusionInput lengthAsViscosity(Length length) {
    CfdDiffusionInput input;
    input.viscosity = length;
    return input;
}
#endif

#if defined(BETTERCAD_CF_A_VISCOSITY_IS_A_BARE_DOUBLE)
[[maybe_unused]] CfdDiffusionInput doubleAsViscosity() {
    CfdDiffusionInput input;
    input.viscosity = 1.002e-3;
    return input;
}
#endif

#if defined(BETTERCAD_CF_AN_AREA_IS_A_LENGTH)
[[maybe_unused]] Length areaAsLength(Area area) { return area; }
#endif

// ---------------------------------------------------------------------------
// The index spaces are not each other
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_COMPONENT_IS_AN_INTEGER)
[[maybe_unused]] CfdDiffusionInput componentFromInteger() {
    CfdDiffusionInput input;
    input.component = 1;
    return input;
}
#endif

#if defined(BETTERCAD_CF_A_FACE_INDEX_IS_A_CELL_INDEX)
[[maybe_unused]] CellIndex faceAsCell(FaceIndex face) { return face; }
#endif

#if defined(BETTERCAD_CF_A_DIFFUSION_PROBLEM_IS_A_SPARSE_PROBLEM)
[[maybe_unused]] CfdSparseProblem diffusionAsSparse() {
    return CfdDiffusionProblem::ViscosityNotUsable;
}
#endif

#if defined(BETTERCAD_CF_A_SPARSE_PROBLEM_IS_A_DIFFUSION_PROBLEM)
[[maybe_unused]] CfdDiffusionProblem sparseAsDiffusion() {
    return CfdSparseProblem::NotSymmetric;
}
#endif

// ---------------------------------------------------------------------------
// A gradient is not a velocity, and the correction is not bare doubles
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_VELOCITY_GRADIENT_IS_A_VELOCITY)
[[maybe_unused]] Velocity3 gradientAsVelocity(const VelocityGradient3& gradient) { return gradient; }
#endif

#if defined(BETTERCAD_CF_A_VELOCITY_IS_A_VELOCITY_GRADIENT)
[[maybe_unused]] VelocityGradient3 velocityAsGradient(const Velocity3& velocity) { return velocity; }
#endif

#if defined(BETTERCAD_CF_THE_CORRECTION_TAKES_BARE_DOUBLES)
[[maybe_unused]] CfdDiffusionInput bareCorrection(std::span<const double> values) {
    CfdDiffusionInput input;
    // The correction is a typed gradient per face, not three doubles per face:
    // a caller that packed them by hand would be choosing a layout this module
    // never agreed to.
    input.faceGradient = values;
    return input;
}
#endif

#if defined(BETTERCAD_CF_THE_CURRENT_VELOCITY_TAKES_TYPED_VECTORS)
[[maybe_unused]] CfdDiffusionInput typedCurrent(std::span<const Velocity3> velocities) {
    CfdDiffusionInput input;
    // And conversely: the current field IS component-blocked doubles, per
    // ADR-066, so a span of typed vectors is the wrong layout and is refused at
    // compile time rather than reinterpreted.
    input.currentVelocity = velocities;
    return input;
}
#endif

// ---------------------------------------------------------------------------
// The cfd module owns no field storage
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_SUMMARY_HOLDS_A_CELL_ARRAY)
[[maybe_unused]] std::size_t summaryValues() {
    CfdDiffusionSummary summary;
    return summary.values.size();
}
#endif

#if defined(BETTERCAD_CF_A_SUMMARY_HOLDS_THE_SOURCE)
[[maybe_unused]] std::size_t summarySource() {
    CfdDiffusionSummary summary;
    return summary.source.size();
}
#endif

#if defined(BETTERCAD_CF_AN_INPUT_HOLDS_A_MATRIX)
[[maybe_unused]] std::size_t inputMatrix() {
    CfdDiffusionInput input;
    return input.matrix.rows();
}
#endif

#if defined(BETTERCAD_CF_A_DECOMPOSITION_ARM_IS_A_TRANSLATION)
[[maybe_unused]] Translation3D armAsTranslation(const CfdFaceDecomposition& split) {
    // An arm is an ORIENTED AREA, not a displacement. The type says so, which is
    // what makes `t . Sf` a Volume rather than a number.
    return split.deferredArm;
}
#endif
