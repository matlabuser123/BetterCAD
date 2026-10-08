#pragma once

// Rule 7 (ADR-039): Eigen is PRIVATE to the modules that link it, so a PUBLIC
// header must not include it. One violation, and nothing else wrong with this
// tree -- the include below is the only reason the checker should fail on it.
#include <Eigen/SparseCholesky>

namespace bettercad::structural {
class Leak {};
} // namespace bettercad::structural
