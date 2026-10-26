bettercad_add_library(bettercad_cfd
    ALIAS cfd
    EXPORT_BASE BETTERCAD_CFD
    EXPORT_HEADER bettercad/cfd/Export.hpp
    SOURCES CfdAnalysis.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::meshing
    PRIVATE_LINK
        BetterCAD::structural
)
