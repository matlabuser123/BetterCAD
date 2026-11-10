// P19-TURB-ARCH-001 fixture: EXACTLY ONE violation --
// a turbulence model arrives with no milestone, which ADR-058 defers in full.
//
// The comment below names turbulence too, and must not add a second violation.
// turbulence turbulent kEpsilon yPlus eddyViscosity

namespace bettercad {

double wallFunctionU(double yStar) { return yStar; }

} // namespace bettercad
