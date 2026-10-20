# Linear-algebra layer (layer 5): the GENERIC numerical kernels more than one
# physics needs, and nothing that knows what the numbers mean.
#
# WHY IT EXISTS (ADR-050, ADR-052). P17 built and qualified a symmetric sparse
# direct solve inside `structural` (layer 50). P18's thermal module sits at 45
# and may not include `structural`, so the solve could not be reached from there
# by include. ADR-050 named the two honest answers -- extraction below 45, or an
# independent private link -- and ADR-052 chose extraction, because a second
# private Eigen link would have duplicated a qualified pivot gate that the two
# modules could then let drift apart.
#
# WHAT IS HERE: `solveSymmetricSparse`, its three-condition acceptance and its
# independent residual. Moved MECHANICALLY out of `structural`: the same
# algorithm, the same order of checks, the same gates. `structural` calls it
# through a wrapper that adds P17's units and P17's wording; `thermal` calls it
# through its own.
#
# WHAT IS NOT: no structural type, no thermal type, no mesh, no CAD, no load, no
# constraint, no unit. Its inputs are index and value arrays and two ratios; the
# layering check enforces that it can include nothing above core.
#
# BELOW SKETCH, which links Eigen privately for its own solver and shares no
# state with this module. Layer 5 is the free integer under 10 (ADR-035's table
# is respaced in tens so that a new module needs no renumbering).
#
# EIGEN, PRIVATE (ADR-039). Pinned by hash, MPL-2.0, already linked privately by
# sketch, assembly and structural. No public header includes it (rule 7).

bettercad_add_library(bettercad_linalg
    ALIAS linalg
    EXPORT_BASE BETTERCAD_LINALG
    EXPORT_HEADER bettercad/linalg/Export.hpp
    SOURCES
        SymmetricSolve.cpp
    PUBLIC_LINK
        BetterCAD::core
    PRIVATE_LINK
        BetterCAD::eigen
)

install(TARGETS bettercad_linalg
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
