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
        Blends.cpp
        EdgeMatching.hpp
        Edges.cpp
        FaceMatching.hpp
        Faces.cpp
        Holes.cpp
        LoftPlan.cpp
        LoftPlan.hpp
        Profile.cpp
        ProfileExtent.hpp
        SweepPlan.cpp
        SweepPlan.hpp
        occt/OcctBlend.cpp
        occt/OcctBlend.hpp
        occt/OcctBody.cpp
        occt/OcctBody.hpp
        occt/OcctBooleans.cpp
        occt/OcctChamfer.cpp
        occt/OcctEdges.cpp
        occt/OcctFaceNames.cpp
        occt/OcctFaceNames.hpp
        occt/OcctFaces.cpp
        occt/OcctFillet.cpp
        occt/OcctHole.cpp
        occt/OcctGuard.hpp
        occt/OcctKernel.cpp
        occt/OcctMesh.cpp
        occt/OcctPrimitives.cpp
        occt/OcctSession.hpp
        occt/OcctStep.cpp
        occt/OcctSweeps.cpp
        occt/OcctTopology.hpp
        occt/OcctTransform.cpp
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
        TKOffset
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
