// P19-TURB-ARCH-001 fixture: EXACTLY ONE violation -- a bare turbulence symbol
// inside cfd, which rule 4 bans because all five measured zero in the 46 real
// cfd files. Rule 1 cannot match these tree-wide; inside cfd it can.
//
// The comment may name turbulence freely: turbulence kEpsilon yPlus.

namespace bettercad::cfd {

double blend(double nut, double k) { return nut * k; }

} // namespace bettercad::cfd
