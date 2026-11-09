# CFD layer (layer 42): the boundary a CFD analysis is prepared at, and later the
# finite-volume topology and geometry, the field maps, boundary conditions, the
# diffusion, convection, gradient and divergence operators, the momentum and
# continuity equations, the pressure-velocity coupling, the steady and transient
# solves, result recovery and the engineering integrals.
#
# ABOVE MESHING, WHICH IT CONSUMES. It reads P16's Tet4 volume mesh, its
# geometry/mesh mapping and its quality measurements, and it reaches geometry
# currency through P16's one geometry boundary rather than asking the regenerator
# itself. A finite-volume cell is one P16 tetrahedron (ADR-058): the topology is
# DERIVED from the mesh and there is no second mesh type. It cannot share
# meshing's 40: the architecture check's rule is strictly-lower (ADR-035).
#
# BELOW THERMAL AND STRUCTURAL, AND NOT BESIDE OR ABOVE THEM (ADR-059). The two
# one-way couplings the roadmap makes likely run out of CFD: a wall pressure is a
# structural load and a wall heat-transfer coefficient is a thermal boundary
# condition. Strictly-lower means the reader must sit above the read, so cfd takes
# 42, the free integer between meshing (40) and thermal (45), and no module is
# renumbered. The cost is the mirror image: this module cannot include thermal or
# structural, and it cannot link either target.
#
# BELOW IO, which will serialize its analysis intent, and below renderer, which
# will display its results. Neither direction is reversed.
#
# NO FLUID DATA OF ITS OWN. ADR-028's hard rule, which names P19: the density and
# the dynamic viscosity come through features::requireIncompressibleFlowProperties
# from the one P15 material (P19-FLUID-001, ADR-062); nothing here stores a
# density, a viscosity or a default. It links features for the values and the
# diagnostic, and reads core/materials only for P15's OWN requirement table and
# availability rule (to name the category of a failure) -- never for a second
# opinion of its own.
#
# NO LINEAR ALGEBRA LINKED, AND STILL NONE (amended by P19-DIFF-001, ADR-069).
# This comment read "There is no matrix in this milestone" until P19-DIFF-001 was
# authorized to assemble one, and that one clause is now false while the rest
# stands. The distinction the amendment draws:
#
#   the CONTAINER   CfdSparseMatrix is P19-DIFF-001's. It is a compressed sparse
#                   row value with a validating gate and a matrix-vector product,
#                   and NOTHING MORE -- no factorisation, no solve, no iteration
#   the SOLVE       still P19-LINSOLVE-001's decision, unchanged. The generic
#                   kernel it would reuse is BetterCAD::linalg (layer 5,
#                   ADR-052), linked PRIVATE, never an include of structural or
#                   thermal and never a hand copy
#
# NO LINEAR ALGEBRA LINKED -- UNTIL NOW. The paragraph above stood from
# P19-DIFF-001 until P19-LINSOLVE-001 (ADR-076) was authorized to make the
# solve, and the clause "BetterCAD::linalg is NOT linked here and no linalg
# header is included" is now false while the distinction it drew still stands.
# The amendment:
#
#   the CONTAINER   still P19-DIFF-001's, unchanged
#   the SOLVE       now BetterCAD::linalg, linked PRIVATE as that paragraph
#                   said it would be, and reached only from
#                   CfdLinearSolve.cpp. Two kernels: solveSymmetricSparse for
#                   the SYMMETRIC pressure correction -- P17's, qualified, the
#                   one structural and thermal already call -- and solveSparse
#                   for the ASYMMETRIC momentum system, which an LDLT cannot
#                   take (ADR-072 Decision 3)
#
# PRIVATE IS LOAD-BEARING: a consumer of BetterCAD::cfd does not inherit the
# link, so no linalg type may appear in a cfd PUBLIC header, and none does.
# That is why the static_assert this file asked a later milestone for --
# "CfdSparseIndex is cfd's own uint64_t alias rather than linalg::SparseIndex,
# same width and signedness by construction ... and the static_assert that turns
# 'same by construction' into 'same, enforced' belongs to the milestone that may
# legitimately include that header" -- now sits in CfdLinearSolve.cpp and not in
# a header. ADR-069 Decision 3 is thereby spent, by the milestone it was
# deferred to.

bettercad_add_library(bettercad_cfd
    ALIAS cfd
    EXPORT_BASE BETTERCAD_CFD
    EXPORT_HEADER bettercad/cfd/Export.hpp
    SOURCES
        CfdAnalysis.cpp
        CfdAnalysisObject.cpp
        CfdBoundary.cpp
        CfdContinuity.cpp
        CfdCoupling.cpp
        CfdConvection.cpp
        CfdData.cpp
        CfdDiffusion.cpp
        CfdDivergence.cpp
        CfdFields.cpp
        CfdFluid.cpp
        CfdGeometry.cpp
        CfdGradient.cpp
        CfdInitialField.cpp
        CfdLinearSolve.cpp
        CfdMesh.cpp
        CfdMomentum.cpp
        CfdSparse.cpp
        CfdTarget.cpp
        CfdTopology.cpp
        CfdResult.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::features
        BetterCAD::meshing
    PRIVATE_LINK
        BetterCAD::linalg
)

install(TARGETS bettercad_cfd
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
