// The PERMITTED direction, in the same tree: Eigen from src/ is fine, so this
// fixture proves the rule fires on the public header and NOT merely on the
// word "Eigen" anywhere.
#include <Eigen/SparseCore>

namespace bettercad::structural {
void allowed() {}
} // namespace bettercad::structural
