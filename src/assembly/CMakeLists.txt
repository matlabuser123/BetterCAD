# Assembly layer (layer 3): components, and later mates and their solver.
# Sits above features, whose bodies and references it uses, and below io,
# which must serialize it (ADR-006).
bettercad_add_library(bettercad_assembly
    ALIAS assembly
    EXPORT_BASE BETTERCAD_ASSEMBLY
    EXPORT_HEADER bettercad/assembly/Export.hpp
    SOURCES
        Component.cpp
        Components.cpp
        Mate.cpp
        Mates.cpp
        Placement.cpp
        Resolution.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::features
)

install(TARGETS bettercad_assembly
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
