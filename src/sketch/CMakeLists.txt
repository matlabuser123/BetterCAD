# Sketch layer (layer 1): 2D sketch data model, constraints and solver.
# Depends on core only; it has no dependency on the 3D geometry kernel.
bettercad_add_library(bettercad_sketch
    ALIAS sketch
    EXPORT_BASE BETTERCAD_SKETCH
    EXPORT_HEADER bettercad/sketch/Export.hpp
    SOURCES
        Sketch.cpp
        SketchCommands.cpp
        SketchRegeneration.cpp
        constraints/SketchConstraints.cpp
        entities/Entities.cpp
        solver/Solver.cpp
        solver/SolverSystem.cpp
        solver/SolverSystem.hpp
    PUBLIC_LINK
        BetterCAD::core
    PRIVATE_LINK
        BetterCAD::eigen
)

install(TARGETS bettercad_sketch
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
