// P19-TURB-ARCH-001 fixture: an INNOCENT cfd file, which rule 4 must not flag.
//
// `closure` is cfd's own word -- it occurs in 7 of the 46 real cfd files, as
// worstClosure and cellClosure -- so it is deliberately absent from rule 4's
// symbol list. If rule 4 is ever widened to include it, this file goes red and
// the widening has to justify itself against the measurement.

namespace bettercad::cfd {

struct Balance {
    double worstClosure = 0.0;
    double cellClosure = 0.0;
};

double closureOf(const Balance& b) { return b.worstClosure + b.cellClosure; }

} // namespace bettercad::cfd
