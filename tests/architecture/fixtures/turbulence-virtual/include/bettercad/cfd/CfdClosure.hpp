// P19-TURB-ARCH-001 fixture: EXACTLY ONE violation -- a virtual interface inside
// cfd, which ADR-079 decision 2 refuses. A mutation analysis found this was the
// one claim in that decision nothing checked.
//
// The comment may name turbulence freely: turbulence kEpsilon yPlus.

#pragma once

namespace bettercad::cfd {

struct Closure {
    virtual ~Closure() = default;
};

} // namespace bettercad::cfd
