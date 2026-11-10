// P19-TURB-ARCH-001 fixture: EXACTLY ONE violation --
// persistence writes a model field, which ADR-079 decision 7 refuses.
//
// The comment below names turbulence too, and must not add a second violation.
// turbulence turbulent kEpsilon yPlus eddyViscosity

namespace bettercad {

void writeTurbulentViscosity(double value);

} // namespace bettercad
