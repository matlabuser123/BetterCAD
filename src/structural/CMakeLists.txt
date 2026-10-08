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
# EIGEN, PRIVATE, ADMITTED BY P17-SOLVE-001 (ADR-039). The note that used to
# stand here reserved this decision for that milestone, and it made it: Eigen
# 5.0.1 is already pinned by hash and already linked PRIVATE by
# bettercad_sketch and bettercad_assembly, so a third private link adds no
# dependency and no new licence obligation.
#
# THE LICENCE WAS VERIFIED FROM THE VENDORED TREE, not from memory. Eigen
# 5.0.1's COPYING.README says it is "primarily MPL2 licensed", it ships no
# COPYING.LGPL and no COPYING.GPL (which Eigen 3.x did), and every file
# SimplicialLDLT needs carries the Mozilla header -- including Amd.h, whose
# CSparse-derived ordering states that Tim Davis executed a licence permitting
# MPL-2.0 distribution. MPL-2.0 is FILE-LEVEL weak copyleft, so it attaches to
# Eigen's own files and does not decide BetterCAD's licence.
#
# PRIVATE, AND NO PUBLIC HEADER INCLUDES IT. include/bettercad/structural/
# exposes BetterCAD types only, so nothing above layer 50 -- io 60,
# renderer 70, scripting 70, the app, the CLI -- sees Eigen. That invariant
# holds across the whole repository and this milestone did not break it.

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
        StructuralTarget.cpp
        StructuralLoad.cpp
        StructuralConstraints.cpp
        StructuralSystem.cpp
        StructuralSolve.cpp
        Tet4Element.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::features
        BetterCAD::meshing
    PRIVATE_LINK
        BetterCAD::eigen
)

install(TARGETS bettercad_structural
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
