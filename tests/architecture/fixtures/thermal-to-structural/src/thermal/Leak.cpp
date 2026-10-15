// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: thermal (layer 45) depends on structural (layer 50).
//
// THE DIRECTION ADR-050 FIXES. A thermo-mechanical coupling reads a
// temperature field to compute thermal strain, so structural must be able to
// include thermal and thermal must never include structural -- the reverse
// would make the conduction solve depend on the stress solve that depends on
// it. It is also what keeps P17's solver out of reach from here: reuse is by
// extraction below 45 or by an independent link, never by this include.
#include <bettercad/structural/StructuralAnalysis.hpp>
