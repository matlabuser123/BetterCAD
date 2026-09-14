# Feature layer (layer 2): parametric features that turn sketches into solids.
bettercad_add_library(bettercad_features
    ALIAS features
    EXPORT_BASE BETTERCAD_FEATURES
    EXPORT_HEADER bettercad/features/Export.hpp
    SOURCES
        FeatureCommands.cpp
        Regenerator.cpp
        ResultBodies.cpp
        Validation.cpp
        extrude/ExtrudeFeature.cpp
        extrude/ExtrudeRegeneration.cpp
        profile/Profiles.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::geometry
        BetterCAD::sketch
)

install(TARGETS bettercad_features
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
