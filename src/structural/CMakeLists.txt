# Structural analysis layer (layer 50): the boundary a structural analysis is
# prepared at, and later the Tet4 element, the DOF model, loads, restraints,
# assembly, the linear solve and result recovery.
#
# ABOVE MESHING, WHICH IT CONSUMES. It reads P16's Tet4 volume mesh, its
# geometry/mesh mapping and its quality measurements, and it reaches geometry
# currency through P16's one geometry boundary rather than asking the
# regenerator itself. It cannot SHARE meshing's layer the way drawing does:
# the architecture check's rule is strictly-lower, so two modules on one layer
# may not include each other at all (ADR-035).
#
# BELOW IO, which will serialize its analysis intent, and below renderer, which
# will display its results. Neither direction is reversed: meshing knows
# nothing about a solver, and this module knows nothing about Qt, about the CLI
# or about a file format.
#
# NO MATERIAL DATA OF ITS OWN. ADR-028's hard rule, which names P17: the
# material comes through features::requireEffectiveMaterial and
# features::requireLinearElasticConstants, and nothing here stores a modulus,
# a default or a typical value. That is why this module links features rather
# than reaching into core/materials directly for a second opinion.
#
# NO LINEAR ALGEBRA YET. Eigen is in the tree, pinned and already qualified,
# but it is declared private to bettercad_sketch. Admitting it here is
# P17-SOLVE-001's decision and carries a licence question the owner must answer
# (the project has no licence yet), so this module does not link it.

bettercad_add_library(bettercad_structural
    ALIAS structural
    EXPORT_BASE BETTERCAD_STRUCTURAL
    EXPORT_HEADER bettercad/structural/Export.hpp
    SOURCES
        StructuralAnalysis.cpp
        StructuralAnalysisObject.cpp
        StructuralData.cpp
        StructuralDof.cpp
        StructuralMaterial.cpp
        StructuralResult.cpp
        Tet4Element.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::features
        BetterCAD::meshing
)

install(TARGETS bettercad_structural
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
