// P19-TURB-ARCH-001 fixture: EXACTLY ONE violation --
// canonical intent gains a turbulence mode, which ADR-079 decision 1 refuses.
//
// The comment below names turbulence too, and must not add a second violation.
// turbulence turbulent kEpsilon yPlus eddyViscosity

#pragma once

namespace bettercad {

enum class CfdTurbulenceMode : unsigned char { Laminar, Turbulent };

} // namespace bettercad
