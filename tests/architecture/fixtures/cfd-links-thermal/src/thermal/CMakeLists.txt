bettercad_add_library(bettercad_thermal
    ALIAS thermal
    EXPORT_BASE BETTERCAD_THERMAL
    EXPORT_HEADER bettercad/thermal/Export.hpp
    SOURCES ThermalSolve.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::features
        BetterCAD::meshing
    PRIVATE_LINK
        BetterCAD::linalg
)
