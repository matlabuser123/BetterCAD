# Thermal analysis layer (layer 45): the boundary a thermal analysis is
# prepared at, and later the Tet4 conduction and capacity matrices, the scalar
# temperature DOF model, heat loads, temperature and convection boundary
# conditions, assembly, the steady and transient solves and result recovery.
#
# ABOVE MESHING, WHICH IT CONSUMES. It reads P16's Tet4 volume mesh, its
# geometry/mesh mapping and its quality measurements, and it reaches geometry
# currency through P16's one geometry boundary rather than asking the
# regenerator itself. It cannot share meshing's 40: the architecture check's
# rule is strictly-lower (ADR-035).
#
# BELOW STRUCTURAL, AND NOT BESIDE IT (ADR-050). The roadmap's one known
# coupling runs structural -> thermal: a temperature field drives thermal
# expansion and thermal stress, so a later thermo-mechanical milestone will
# want structural (50) to READ a thermal result. Strictly-lower means that
# consumer must sit above this module, and 45 is the free integer that lets it
# do so without a renumber. The cost is the mirror image: this module cannot
# include structural, so P17's sparse-solver API is not reachable from here.
# P18-SOLVE-001 owns that question -- extraction below 45, or an independent
# private link -- and this file records the constraint rather than the answer.
#
# BELOW IO, which will serialize its analysis intent, and below renderer, which
# will display its results. Neither direction is reversed.
#
# NO MATERIAL DATA OF ITS OWN. ADR-028's hard rule, which names P18: the
# conductivity comes through features::requireThermalConductivity and the
# transient properties through features::requireTransientConductionProperties,
# and nothing here stores a conductivity, a default or a typical value. That is
# why this module links features rather than reaching into core/materials for
# a second opinion.
#
# NO LINEAR ALGEBRA LINKED. The element matrices of P18-ELEM-001 are 4x4 and
# 3x4 fixed arrays computed by hand-written loops, as P17's kernel is.
# Admitting a library is P18-SOLVE-001's decision, under rule 7: Eigen may be
# linked PRIVATE and never included from a public header.

bettercad_add_library(bettercad_thermal
    ALIAS thermal
    EXPORT_BASE BETTERCAD_THERMAL
    EXPORT_HEADER bettercad/thermal/Export.hpp
    SOURCES
        ThermalAnalysis.cpp
        ThermalAnalysisObject.cpp
        ThermalData.cpp
        ThermalDof.cpp
        ThermalElement.cpp
        ThermalLoadVector.cpp
        ThermalMaterial.cpp
        ThermalResult.cpp
        ThermalTarget.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::features
        BetterCAD::meshing
)

install(TARGETS bettercad_thermal
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
