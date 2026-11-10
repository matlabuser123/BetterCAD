// P19-TURB-ARCH-001 fixture: EXACTLY ONE violation --
// the CLI computes its own quantity, which ADR-079 decision 9 refuses.
//
// The comment below names turbulence too, and must not add a second violation.
// turbulence turbulent kEpsilon yPlus eddyViscosity

namespace bettercad {

double eddyViscosityOf(double k, double e) { return 0.09 * k * k / e; }

} // namespace bettercad
