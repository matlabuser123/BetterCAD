# Feature layer (layer 2): parametric features that turn sketches into solids.
bettercad_add_library(bettercad_features
    ALIAS features
    EXPORT_BASE BETTERCAD_FEATURES
    EXPORT_HEADER bettercad/features/Export.hpp
    SOURCES
        Feature.cpp
        Regenerator.cpp
        ResultBodies.cpp
        SolidSupport.cpp
        SolidSupport.hpp
        Validation.cpp
        chamfer/ChamferFeature.cpp
        chamfer/ChamferRegeneration.cpp
        extrude/ExtrudeFeature.cpp
        extrude/ExtrudeRegeneration.cpp
        fillet/FilletFeature.cpp
        fillet/FilletRegeneration.cpp
        profile/Profiles.cpp
        revolve/RevolveFeature.cpp
        revolve/RevolveRegeneration.cpp
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
