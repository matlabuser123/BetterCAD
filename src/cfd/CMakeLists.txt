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
# NO FLUID DATA OF ITS OWN. ADR-028's hard rule, which names P19: the density comes
# through features::requireDensity and the viscosity, from P19-FLUID-001, through
# the same P15 material; nothing here stores a density, a viscosity or a default.
# That is why this module links features rather than reaching into core/materials
# for a second opinion.
#
# NO LINEAR ALGEBRA LINKED. There is no matrix in this milestone. Admitting one is
# P19-LINSOLVE-001's decision, and the generic kernel it would reuse is
# BetterCAD::linalg (layer 5, ADR-052), linked PRIVATE, never an include of
# structural or thermal and never a hand copy.

bettercad_add_library(bettercad_cfd
    ALIAS cfd
    EXPORT_BASE BETTERCAD_CFD
    EXPORT_HEADER bettercad/cfd/Export.hpp
    SOURCES
        CfdAnalysis.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::features
        BetterCAD::meshing
)

install(TARGETS bettercad_cfd
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
