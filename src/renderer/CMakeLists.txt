# Renderer layer (layer 70): the 3D viewer (INFRA-VIEWER-001).
#
# The viewport BetterCAD did not have. Its public header
# (include/bettercad/renderer/Viewer.hpp) contains no OCCT and no Qt types:
# everything OCCT lives in occt/, and the Qt widget that hosts a view lives in
# apps/bettercad/, which is where rule 2 of the architecture check allows Qt.
#
# The viewer OWNS NO ENGINEERING STATE. It holds presentations, visibility,
# selection and a camera, and its only link back to the model is the ObjectId
# a caller supplies when displaying a body.
#
# It sits at layer 70 because it uses geometry (layer 0) and meshing (layer 40).
# Nothing depends on it: a renderer is a leaf.
#
# MeshView.cpp is NOT under occt/ and that is deliberate: the read-only mesh
# visualisation adapter (P16-VIZ-001) contains no OCCT and no Qt, so it is
# testable without a display, and rule 1 of the architecture check has nothing
# to contain. Everything that talks to OCCT stays in occt/.
bettercad_add_library(bettercad_renderer
    ALIAS renderer
    EXPORT_BASE BETTERCAD_RENDERER
    EXPORT_HEADER bettercad/renderer/Export.hpp
    SOURCES
        MeshInspection.cpp
        MeshScene.cpp
        MeshView.cpp
        occt/OcctMeshPresentation.cpp
        occt/OcctViewer.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::geometry
        # Public: MeshView.hpp takes a meshing::Mesh and hands back NodeId and
        # ElementId, so a consumer of this module's headers needs meshing too.
        BetterCAD::meshing
    PRIVATE_LINK
        # The modelling toolkits are needed because the viewer displays a
        # TopoDS_Shape through AIS_Shape; the visualization ones are what
        # INFRA-VIEWER-001 added to the dependency build.
        TKernel
        TKMath
        TKG2d
        TKG3d
        TKBRep
        TKTopAlgo
        TKService
        TKV3d
        TKOpenGl
)

# OCCT headers use M_PI/M_PI_2, which <cmath> only provides in strict ISO mode
# (-std=c++23, no GNU extensions) when _USE_MATH_DEFINES is set.
target_compile_definitions(bettercad_renderer PRIVATE _USE_MATH_DEFINES)

install(TARGETS bettercad_renderer
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
