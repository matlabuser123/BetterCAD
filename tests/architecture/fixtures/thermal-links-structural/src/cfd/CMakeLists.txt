bettercad_add_library(bettercad_cfd
    ALIAS cfd
    EXPORT_BASE BETTERCAD_CFD
    EXPORT_HEADER bettercad/cfd/Export.hpp
    SOURCES CfdAnalysis.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::features
        BetterCAD::meshing
)
# A comment may name BetterCAD::thermal and BetterCAD::structural without linking them.