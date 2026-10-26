# A comment may name BetterCAD::structural without linking it.
bettercad_add_library(bettercad_linalg
    ALIAS linalg
    EXPORT_BASE BETTERCAD_LINALG
    EXPORT_HEADER bettercad/linalg/Export.hpp
    SOURCES SymmetricSolve.cpp
    PUBLIC_LINK
        BetterCAD::core
    PRIVATE_LINK
        BetterCAD::eigen
)
