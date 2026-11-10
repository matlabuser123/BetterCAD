// P19-TURB-ARCH-001 fixture: a tree with NO violation.
//
// THIS FILE IS DELIBERATELY NOT UNDER src/cfd/, and the reason is a rule that
// caught it. Rule 4 bans the bare turbulence symbols INSIDE cfd, where all five
// measured zero; this file exists to prove rule 1 does not match them TREE-WIDE,
// so it has to sit outside cfd. The first version of this fixture was at
// src/cfd/CfdSteady.cpp and rule 4 failed it the moment the rule was added --
// which is the rule working, not the fixture being unlucky.
//
// This comment names turbulence, a turbulent viscosity, kEpsilon and yPlus on
// purpose. Comments are not code and the checker strips both forms, so none of
// these may be flagged. /* and a block comment naming eddyViscosity too. */

namespace bettercad {

// Every identifier below contains a substring of a turbulence symbol and is
// ordinary code. These are the measured false positives of the bare patterns:
// mut (29 files), nut (6 files), epsilon (9 files), omega (4 files), LES (8).
struct Settings {
    bool mutableFlag = false;
    double epsilon = 1.0e-9;
    double omega = 0.5;
    int minutes = 60;
};

std::mutex gate;

double permutationSign(int i) { return (i % 2 == 0) ? 1.0 : -1.0; }

bool commutative(double a, double b) { return a * b == b * a; }

const char* kStyleFILES = "STYLES";

double relax(const Settings& s) { return s.epsilon * s.omega; }

} // namespace bettercad

// Added after a mutation analysis widened the pattern set. Each identifier below
// contains a substring of a turbulence symbol that was MEASURED to occur in the
// real tree and is therefore deliberately not matched: tke (10 files), closure
// (8, including src/cfd/CfdContinuity.cpp), RANS (1, via TRANSient).
namespace bettercad::cfd {
struct Continuity {
    double closureResidual = 0.0;
    int bracketed = 0;
};
const char* kTransform = "TRANSIENT";
double stkeState(const Continuity& c) { return c.closureResidual; }
} // namespace bettercad
