// P19-TURB-ARCH-001 fixture: EXACTLY ONE violation --
// the GUI computes its own quantity, which ADR-079 decision 9 refuses.
//
// The comment below names turbulence too, and must not add a second violation.
// turbulence turbulent kEpsilon yPlus eddyViscosity

namespace bettercad {

double yPlusAt(int cell) { return 0.0; }

} // namespace bettercad
