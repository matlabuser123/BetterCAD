/// \file
/// P19-GRAD-001's compile-fail group: what the type system must make
/// impossible, ADR-070.
///
/// THE CLAIM THIS GROUP CARRIES. Three things, and the second is the one a value
/// test provably cannot reach:
///
/// 1. A `CfdGradientField` cannot be CONJURED. Possession is the evidence
///    (ADR-036): the only door is a reconstruction, which runs the whole input
///    gate, so a field that exists has been through it. The default constructor
///    is private and only `CfdGradientBuilder` is a friend.
/// 2. A PRESSURE GRADIENT AND A VELOCITY GRADIENT CANNOT BE SWAPPED. ADR-070
///    Decision 3 chose one template with two instantiations precisely so that
///    the compiler separates them, and REJECTED the alternative -- one field
///    class carrying a runtime `CfdVariable` tag -- because that turns this
///    class of error into a `std::nullopt` a caller can ignore. No value test
///    can show that a swap is impossible; only a compile error can.
/// 3. THE CELL VALUES ARE SI DOUBLES ON PURPOSE, and typed quantities are
///    refused there. That is not an oversight: ADR-068 settled that a bulk field
///    array is a caller-owned span of doubles, and the units live on the
///    ACCESSOR instead. These cases are what stop that decision being quietly
///    reversed.
///
/// EVERY HELPER IS `[[maybe_unused]] static`, AND BOTH WORDS EARN THEIR PLACE.
/// `static` gives it internal linkage, so `-Wmissing-declarations` cannot fire
/// -- which matters because a probe that can exit non-zero for a reason
/// UNRELATED to its claim proves nothing, the hazard P18-ELEM-001 recorded as
/// its finding F3. With internal linkage each case emits exactly the one
/// diagnostic its name describes. `[[maybe_unused]]` is then needed because: Each is called
/// only from inside a `#if defined(BETTERCAD_CF_...)` block, so in the CONTROL
/// configuration -- no macro, the build every preset runs -- they are unused,
/// and this translation unit is compiled with `-Werror`. P19-GEOM-001 recorded
/// what a failing control costs: it broke `cmake --build` for the whole project.

#include <bettercad/cfd/CfdData.hpp>
#include <bettercad/cfd/CfdGradient.hpp>
#include <bettercad/cfd/CfdTopology.hpp>
#include <bettercad/core/units/Units.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace bettercad;
using namespace bettercad::cfd;

// ---------------------------------------------------------------------------
// A field cannot be conjured
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_PRESSURE_GRADIENT_FIELD)
[[maybe_unused]] static CfdPressureGradientField conjuredPressure() {
    // The default constructor is private: a reconstruction is the only door, so
    // a field that was never gated does not exist.
    return CfdPressureGradientField{};
}
#endif

#if defined(BETTERCAD_CF_DEFAULT_CONSTRUCT_A_VELOCITY_GRADIENT_FIELD)
[[maybe_unused]] static CfdVelocityGradientField conjuredVelocity() {
    return CfdVelocityGradientField{};
}
#endif

// ---------------------------------------------------------------------------
// The two fields are different types
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_PRESSURE_GRADIENT_FIELD_IS_A_VELOCITY_GRADIENT_FIELD)
[[maybe_unused]] static std::size_t swappedFields(const CfdPressureGradientField& pressure) {
    // ADR-070 Decision 3's whole point: the quantity is the template argument,
    // so these are distinct types and the compiler says so.
    const CfdVelocityGradientField& velocity = pressure;
    return velocity.cells();
}
#endif

#if defined(BETTERCAD_CF_A_VELOCITY_GRADIENT_FIELD_IS_A_PRESSURE_GRADIENT_FIELD)
[[maybe_unused]] static std::size_t swappedFieldsBack(const CfdVelocityGradientField& velocity) {
    const CfdPressureGradientField& pressure = velocity;
    return pressure.cells();
}
#endif

// ---------------------------------------------------------------------------
// The two quantities are different types
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_PRESSURE_GRADIENT_IS_A_VELOCITY_GRADIENT)
[[maybe_unused]] static VelocityGradient swappedQuantity() {
    return PressureGradient::fromSi(1.0);
}
#endif

#if defined(BETTERCAD_CF_A_VELOCITY_GRADIENT_IS_A_PRESSURE_GRADIENT)
[[maybe_unused]] static PressureGradient swappedQuantityBack() {
    return VelocityGradient::fromSi(1.0);
}
#endif

#if defined(BETTERCAD_CF_A_PRESSURE_GRADIENT_IS_A_PRESSURE)
[[maybe_unused]] static Pressure gradientIsAPressure() {
    // `Pa/m` is not `Pa`: dividing by a length changed the dimension, which is
    // the whole reason no bare double is used here.
    return PressureGradient::fromSi(1.0);
}
#endif

#if defined(BETTERCAD_CF_A_VELOCITY_GRADIENT_IS_A_VELOCITY)
[[maybe_unused]] static Velocity gradientIsAVelocity() {
    return VelocityGradient::fromSi(1.0);
}
#endif

#if defined(BETTERCAD_CF_A_PRESSURE_GRADIENT_IS_A_BARE_DOUBLE)
[[maybe_unused]] static PressureGradient gradientFromADouble() {
    // `Quantity` has only a defaulted default constructor and `fromSi`, so there
    // is no implicit conversion from a number with no unit attached.
    return 1.0;
}
#endif

#if defined(BETTERCAD_CF_A_GRADIENT_SUM_MIXES_DIMENSIONS)
[[maybe_unused]] static double mixedSum() {
    // The same-dimension operators are hidden friends, so mixing finds no viable
    // operator rather than silently converting.
    return (PressureGradient::fromSi(1.0) + VelocityGradient::fromSi(1.0)).si();
}
#endif

#if defined(BETTERCAD_CF_A_PRESSURE_GRADIENT_VECTOR_IS_A_VELOCITY_GRADIENT_VECTOR)
[[maybe_unused]] static VelocityGradient3 swappedVector(const PressureGradient3& pressure) {
    return pressure;
}
#endif

// ---------------------------------------------------------------------------
// The two inputs are different types
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_GRADIENT_INPUT_IS_A_VELOCITY_GRADIENT_INPUT)
[[maybe_unused]] static CfdVelocityGradientInput swappedInput(const CfdGradientInput& pressure) {
    // A velocity input carries a component and a component-blocked array; a
    // pressure input carries neither. Conflating them would let a caller ask for
    // a velocity gradient without saying which component.
    return pressure;
}
#endif

#if defined(BETTERCAD_CF_A_VELOCITY_GRADIENT_INPUT_IS_A_GRADIENT_INPUT)
[[maybe_unused]] static CfdGradientInput swappedInputBack(const CfdVelocityGradientInput& velocity) {
    return velocity;
}
#endif

#if defined(BETTERCAD_CF_THE_PRESSURE_ENTRY_POINT_TAKES_A_VELOCITY_INPUT)
[[maybe_unused]] static bool wrongEntryPoint(const CfdTopology& topology, const CfdGeometry& geometry,
                                      const PreparedCfdBoundary& boundary,
                                      const CfdVelocityGradientInput& input) {
    return reconstructCfdPressureGradient(topology, geometry, boundary, input).has_value();
}
#endif

// ---------------------------------------------------------------------------
// The cell values are SI doubles, deliberately
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_THE_CELL_VALUE_TAKES_TYPED_PRESSURES)
[[maybe_unused]] static CfdGradientInput typedCellValues(std::span<const Pressure> pressures) {
    // ADR-068's split: a BULK array is a caller-owned span of SI doubles and the
    // units live on the accessor. Accepting a typed span here would reverse that
    // decision silently.
    CfdGradientInput input;
    input.cellValue = pressures;
    return input;
}
#endif

#if defined(BETTERCAD_CF_THE_CURRENT_VELOCITY_TAKES_TYPED_VECTORS)
[[maybe_unused]] static CfdVelocityGradientInput typedVelocities(std::span<const Velocity3> velocities) {
    CfdVelocityGradientInput input;
    input.currentVelocity = velocities;
    return input;
}
#endif

// ---------------------------------------------------------------------------
// Tags and indices are not integers, and not each other
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_GRADIENT_PROBLEM_IS_A_GRADIENT_METHOD)
[[maybe_unused]] static CfdGradientMethod problemIsAMethod() {
    return CfdGradientProblem::CellValueNotFinite;
}
#endif

#if defined(BETTERCAD_CF_A_GRADIENT_METHOD_IS_A_GRADIENT_PROBLEM)
[[maybe_unused]] static CfdGradientProblem methodIsAProblem() {
    return CfdGradientMethod::LeastSquares;
}
#endif

#if defined(BETTERCAD_CF_A_GRADIENT_METHOD_IS_AN_INTEGER)
[[maybe_unused]] static CfdGradientMethod methodFromAnInteger() {
    // A scoped enumeration, so an integer is not one of its values.
    return 1;
}
#endif

#if defined(BETTERCAD_CF_A_COMPONENT_IS_AN_INTEGER)
[[maybe_unused]] static CfdVelocityGradientInput componentFromAnInteger() {
    CfdVelocityGradientInput input;
    input.component = 1;
    return input;
}
#endif

// ---------------------------------------------------------------------------
// The summary and the field hold what they hold, and nothing else
// ---------------------------------------------------------------------------

#if defined(BETTERCAD_CF_A_SUMMARY_HOLDS_THE_GRADIENTS)
[[maybe_unused]] static std::size_t summaryHoldsTheField(const CfdGradientSummary& summary) {
    // A gradient is DERIVED (ADR-058): the summary measures the reconstruction,
    // it does not carry a second copy of the answer.
    return summary.gradients.size();
}
#endif

#if defined(BETTERCAD_CF_AN_INPUT_HOLDS_A_FIELD)
[[maybe_unused]] static std::size_t inputHoldsAField(const CfdGradientInput& input) {
    return input.field.cells();
}
#endif

#if defined(BETTERCAD_CF_A_FIELD_EXPOSES_ITS_SI_ARRAY)
[[maybe_unused]] static double fieldExposesItsArray(const CfdPressureGradientField& field) {
    // The storage is private, so a consumer cannot reach past the typed accessor
    // and read the gradient as a bare number.
    return field.si_[0];
}
#endif

#if defined(BETTERCAD_CF_A_FIELD_SUMMARY_IS_WRITABLE)
[[maybe_unused]] static void summaryIsWritable(const CfdPressureGradientField& field) {
    // `summary()` returns a const reference: the measurements a reconstruction
    // made cannot be edited afterwards to say something else.
    field.summary().cells = 3;
}
#endif
