// P19-TURB-ARCH-001 fixture: EXACTLY ONE violation --
// a command carries a model field, which ADR-079 decision 8 refuses.
//
// The comment below names turbulence too, and must not add a second violation.
// turbulence turbulent kEpsilon yPlus eddyViscosity

namespace bettercad {

void setTurbulenceModel(int variant);

} // namespace bettercad
