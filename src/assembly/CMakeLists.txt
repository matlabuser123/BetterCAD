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
        solver/Solver.cpp
        solver/SolverSystem.cpp
        solver/SolverSystem.hpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::features
    PRIVATE_LINK
        BetterCAD::eigen
)

install(TARGETS bettercad_assembly
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
