# Geometry layer: BetterCAD's solid-modelling API backed by Open CASCADE.
#
# The public headers (include/bettercad/core/geometry/) contain no OCCT types.
# Everything that touches OCCT lives in occt/, and OCCT is a PRIVATE
# dependency, so consumers never see OCCT headers.
bettercad_add_library(bettercad_geometry
    ALIAS geometry
    EXPORT_BASE BETTERCAD_GEOMETRY
    EXPORT_HEADER bettercad/core/geometry/Export.hpp
    SOURCES
        EdgeMatching.hpp
        Edges.cpp
        Profile.cpp
        occt/OcctBody.cpp
        occt/OcctBody.hpp
        occt/OcctBooleans.cpp
        occt/OcctChamfer.cpp
        occt/OcctEdges.cpp
        occt/OcctGuard.hpp
        occt/OcctKernel.cpp
        occt/OcctMesh.cpp
        occt/OcctPrimitives.cpp
        occt/OcctSession.hpp
        occt/OcctStep.cpp
        occt/OcctSweeps.cpp
        occt/OcctTopology.hpp
    PUBLIC_LINK
        BetterCAD::core
    PRIVATE_LINK
        TKernel
        TKMath
        TKG3d
        TKG2d
        TKGeomBase
        TKBRep
        TKTopAlgo
        TKPrim
        TKBO
        TKBool
        TKFillet
        TKMesh
        TKLCAF
        TKXCAF
        TKXSBase
        TKDESTEP
)

# OCCT headers use M_PI/M_PI_2, which <cmath> only provides in strict ISO mode
# (-std=c++23, no GNU extensions) when _USE_MATH_DEFINES is set.
target_compile_definitions(bettercad_geometry PRIVATE _USE_MATH_DEFINES)

install(TARGETS bettercad_geometry
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
