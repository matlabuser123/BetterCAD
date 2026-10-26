// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: cfd (layer 42) depends on structural (layer 50).
//
// THE DIRECTION ADR-059 FIXES. A wall pressure is a structural LOAD, so structural
// must be able to include cfd and cfd must never include structural -- the reverse
// would make the flow solve depend on the stress solve that depends on it.
#include <bettercad/structural/StructuralAnalysis.hpp>