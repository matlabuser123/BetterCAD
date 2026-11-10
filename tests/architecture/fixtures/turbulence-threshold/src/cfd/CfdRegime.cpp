// P19-TURB-ARCH-001 fixture: EXACTLY ONE violation --
// a laminar-regime threshold is fixed here, which ADR-079 decision 10 refuses.
//
// The comment below names turbulence too, and must not add a second violation.
// turbulence turbulent kEpsilon yPlus eddyViscosity

namespace bettercad {

constexpr double kLaminarReynolds = 2300.0;

} // namespace bettercad
